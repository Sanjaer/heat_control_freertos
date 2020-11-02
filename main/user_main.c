/* TODO: header
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <esp_http_server.h>
#include "driver/gpio.h"

#define GPIO_OUTPUT_IO_1        5
#define GPIO_OUTPUT_PIN_SEL     ((1ULL<<GPIO_OUTPUT_IO_1))
#define MAIN_FORM              "<form action=\"/control\"> \
                                Time ON: <input type=\"text\" name=\"t_on\"> \
                                <input type=\"submit\" value=\"Submit\"> \
                                </form>" \
                                "<form action=\"/control\"> \
                                Time OFF: <input type=\"text\" name=\"t_off\"> \
                                <input type=\"submit\" value=\"Submit\"> \
                                </form>" \
                                "<form action=\"/control\"> \
                                Temperature: <input type=\"text\" name=\"temp\"> \
                                <input type=\"submit\" value=\"Submit\"> \
                                </form>"

static const char *TAG_MAIN = "main";
static const char *TAG_WIFI = "wifi";
static const char *TAG_SERVER = "server";
static const char *SEMICOLON_ENCODE = "%3A";


/* Global variables */
static httpd_handle_t server = NULL;


/* Task declarations */
static void relay_task(void *arg);


/* Local functions declarations */
esp_err_t gpio_setup(void);
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
bool validate_value(char *data, int key);


/* Handlers declarations */
esp_err_t control_get_handler(httpd_req_t *req);
esp_err_t echo_post_handler(httpd_req_t *req);
esp_err_t ctrl_put_handler(httpd_req_t *req);
static void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data);
static void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data);


/* URI declarations */
httpd_uri_t control = {
    .uri       = "/control",
    .method    = HTTP_GET,
    .handler   = control_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = MAIN_FORM
};


/* Task definitions */
static void relay_task(void *arg){

    char cnt = 0;

    for (;;) {
        cnt = !cnt;
        vTaskDelay(3000 / portTICK_RATE_MS);
        gpio_set_level(GPIO_OUTPUT_IO_1, cnt);
    }
}


/* Local functions definitions */
esp_err_t gpio_setup(void){

    esp_err_t result;

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO15/16
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    result = gpio_config(&io_conf);
    // Return gpio_config result
    return result;

}



httpd_handle_t start_webserver(void){

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG_SERVER, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG_SERVER, "Registering URI handlers");
        httpd_register_uri_handler(server, &control);
        return server;
    }

    ESP_LOGI(TAG_SERVER, "Error starting server!");
    return NULL;
}


void stop_webserver(httpd_handle_t server){

    // Stop the httpd server
    httpd_stop(server);

}


/* Extract, validate and resend if ok value to control task */
bool validate_value(char *data, int key){

    bool result = false;
    char *piece;

    switch (key){

        case 1:
        // t_on
        printf("Dis data starts: %s\n", data);
        if ((data[2] == '%') && (piece = strstr(data, SEMICOLON_ENCODE)) != NULL){
            int hh_mm[2];
            hh_mm[0] = ((data[0] - '0') * 10) + (data[1] - '0');
            printf("Dis HH: %d\n", hh_mm[0]);
            hh_mm[1] = ((piece[3] - '0') * 10) + (piece[4] - '0');
            printf("Dis MM: %d\n", hh_mm[1]);
        }
        break;

        case 2:
        // t_off
        break;

        case 3:
        // temperature
        break;

        default:
        result = false;
        break;
    }

    return result;

}

/* Handlers */
/* An HTTP GET handler */
esp_err_t control_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination if string "Host" found in req */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG_SERVER, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG_SERVER, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "t_on", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG_SERVER, "Found URL query parameter => t_on=%s", param);
                validate_value(param, 1);
            }
            if (httpd_query_key_value(buf, "t_off", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG_SERVER, "Found URL query parameter => t_off=%s", param);
                validate_value(param, 2);
            }
            if (httpd_query_key_value(buf, "temp", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG_SERVER, "Found URL query parameter => temp=%s", param);
                validate_value(param, 3);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");


    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, strlen(resp_str));
    ESP_LOGI(TAG_SERVER, "Sent response");
    return ESP_OK;
}


/* Connection handlers */
static void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG_WIFI, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG_WIFI, "Starting webserver");
        *server = start_webserver();
    }
}

/* Main */
void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    ESP_ERROR_CHECK(example_connect());

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    server = start_webserver();

    if (gpio_setup() != ESP_OK){
        ESP_LOGI(TAG_MAIN, "Error configuring GPIOs");
    } else {
        ESP_LOGI(TAG_MAIN, "Starting tasks");
        // Task to control the relay
        xTaskCreate(relay_task, "relay_task", 1024, NULL, 10, NULL);
    }
}
