/* Heat controller

    MIT Licensed

    Copyright 2020 Pablo San José Burgos

    Permission is hereby granted, free of charge, to any person obtaining a copy 
    of this software and associated documentation files (the "Software"), 
    to deal in the Software without restriction, including without limitation the rights to 
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
    of the Software, and to permit persons to whom the Software is furnished to do so, 
    subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies 
    or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS 
    OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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

#include "sht3x.h"

#define GPIO_OUTPUT_IO_1        5
#define GPIO_OUTPUT_PIN_SEL     ((1ULL<<GPIO_OUTPUT_IO_1))


#define TON_CODE                (uint8_t)1
#define TOFF_CODE               (uint8_t)2
#define TEMP_CODE               (uint8_t)3


static const char *TAG_MAIN = "main";
static const char *TAG_WIFI = "wifi";
static const char *TAG_SERVER = "server";
static const char *TAG_TASK_READ = "ReadQ";
static const char *TAG_TASK_WRITE = "WriteQ";
static const char *SEMICOLON_ENCODE = "%3A";


/* Global variables */
// Handles
static httpd_handle_t server = NULL;
static xQueueHandle i2c_lecture_queue = NULL;
static xQueueHandle web_lecture_queue = NULL;

// Strings
static char* HO_pos_global = 0;
static char* MO_pos_global = 0;
static char* HF_pos_global = 0;
static char* MF_pos_global = 0;
static char* TC_pos_global = 0;

static uint8_t time_on_global[] = {0,0};
static uint8_t time_off_global[] = {23,59};
static uint8_t ref_temp_global = 18;
static char  formated_html[] =   "<h2>Smart heat controller </h2>" \
                                "<form action=\"/control\"> \
                                Time ON: <input type=\"text\" name=\"t_on\"> \
                                <input type=\"submit\" value=\"Submit\"> \
                                </form>"\
                                "<form action=\"/control\"> \
                                Time OFF: <input type=\"text\" name=\"t_off\"> \
                                <input type=\"submit\" value=\"Submit\"> \
                                </form>"\
                                "<form action=\"/control\">\
                                Temperature: <input type=\"text\" name=\"temp\">\
                                <input type=\"submit\" value=\"Submit\">\
                                </form>"\
                                "<p>Time on: HO:MO</p>\
                                <p>Time off: HF:MF</p>\
                                <p>Temperature: TCC</p>";



/* Task declarations */
static void control_task(void *arg);


/* Local functions declarations */
esp_err_t gpio_setup(void);
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
bool validate_value(char *data, uint8_t key);
void initial_replacement(void);
void replace_in_html(char* target, char* insertion, uint32_t num_of_bytes);


/* Handlers declarations */
esp_err_t control_get_handler(httpd_req_t *req);
esp_err_t echo_post_handler(httpd_req_t *req);
esp_err_t ctrl_put_handler(httpd_req_t *req);
static void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data);
static void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data);


/* URI declarations */
httpd_uri_t default_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = control_get_handler,
    .user_ctx  = formated_html
};

httpd_uri_t control_uri = {
    .uri       = "/control",
    .method    = HTTP_GET,
    .handler   = control_get_handler,
    .user_ctx  = formated_html
};


/* Task definitions */
static void control_task(void *arg){

    struct SensorData *data_recvd = malloc(sizeof(struct SensorData));
    uint8_t code_updated=0;

    for (;;) {
        memset(data_recvd, 0, sizeof(struct SensorData));

        if (xQueueReceive(i2c_lecture_queue, data_recvd, portMAX_DELAY)) {
            // ESP_LOGI(TAG_TASK_READ, "Temperature from queue: %f\n", data_recvd->temperature);
            if (data_recvd->temperature < ref_temp_global){
                gpio_set_level(GPIO_OUTPUT_IO_1, 1);
            } else {
                gpio_set_level(GPIO_OUTPUT_IO_1, 0);
            }
        }



        if (xQueueReceive(web_lecture_queue, &code_updated, portMAX_DELAY)){

            switch (code_updated){
                case TON_CODE:
                // TON Updated
                break;

                case TOFF_CODE:
                // TOFF Updated
                break;

                case TEMP_CODE:
                // Temperature updated
                break;
            }

            ESP_LOGI(TAG_TASK_READ, "Code updated: %d\n", code_updated);
        }

        vTaskDelay(2000 / portTICK_RATE_MS);
    }
}

/**
 * @brief task to show use case of a queue publish sensor data
 */
static void i2c_task_example(void *arg)
{
    uint8_t sensor_data[DATA_MSG_SIZE];
    struct SensorData *data_recvd = malloc(sizeof(struct SensorData));
    static uint32_t count = 0;

    vTaskDelay(100 / portTICK_RATE_MS);
    
    // Init i2c master
    i2c_master_init();
    
    while (1) {

        // Counter num of iterations, no practical use
        count++;
 
        // Set to zeroes all data variables
        memset(sensor_data, 0, DATA_MSG_SIZE);
        memset(data_recvd, 0, sizeof(struct SensorData));

        // Single shot data acquisition mode, clock stretching
        i2c_master_sht30_read(I2C_EXAMPLE_MASTER_NUM, SHT30_CMD_START_MSB, SHT30_CMD_START_LSB, sensor_data, DATA_MSG_SIZE);

        // Convert raw data to true values
        data_recvd->temperature = convert_raw_to_celsius(sensor_data);
        data_recvd->humidity = convert_raw_to_humidity(sensor_data);

        // Print values
        // printf("count: %d\n", count);
        // printf("temp=%f, hum=%f\n", data_recvd->temperature, data_recvd->humidity);

        if (!xQueueSend(i2c_lecture_queue, data_recvd, portMAX_DELAY)){
            ESP_LOGI(TAG_TASK_WRITE, "ERROR Writing to i2c queue\n");
        }

        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    i2c_driver_delete(I2C_EXAMPLE_MASTER_NUM);
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
        httpd_register_uri_handler(server, &control_uri);
        httpd_register_uri_handler(server, &default_uri);
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
bool validate_value(char *data, uint8_t key){

    bool result = false;
    char *piece;

    switch (key){

        case TON_CODE:
            // t_on
            printf("Validating t_on\n");
            if ((data[2] == '%') && ((piece = strstr(data, SEMICOLON_ENCODE)) != NULL)){

                uint8_t hh_mm[2];

                // Extract data from URL
                hh_mm[0] = ((data[0] - '0') * 10) + (data[1] - '0');
                hh_mm[1] = ((piece[3] - '0') * 10) + (piece[4] - '0');

                // Verify legal hour
                if (hh_mm[0] < 24){
                    // Verify legal minute
                    if (hh_mm[1] < 60){
                        // Update global marker
                        time_on_global[0] = hh_mm[0];
                        time_on_global[1] = hh_mm[1];
                        // If it has been validated, update HTML
                        replace_in_html(HO_pos_global, data, 2);
                        replace_in_html(MO_pos_global, piece+3, 2);
                        // Set as valid
                        result = true;
                    }
                }

                // TODO: ##ifdef for debugging
                printf("Dis HH: %d\n", hh_mm[0]);
                printf("Dis MM: %d\n", hh_mm[1]);

            }
            break;

        case TOFF_CODE:
            // t_off
            printf("Validating t_off\n");
            printf("Dis data starts: %s\n", data);
            if ((data[2] == '%') && (piece = strstr(data, SEMICOLON_ENCODE)) != NULL){
                uint8_t hh_mm[2];
                hh_mm[0] = ((data[0] - '0') * 10) + (data[1] - '0');
                hh_mm[1] = ((piece[3] - '0') * 10) + (piece[4] - '0');

                // Verify legal hour
                if (hh_mm[0] < 24){
                    // Verify legal minute
                    if (hh_mm[1] < 60){
                        // Update global marker
                        time_off_global[0] = hh_mm[0];
                        time_off_global[1] = hh_mm[1];
                        // If it has been validated, update HTML
                        replace_in_html(HF_pos_global, data, 2);
                        replace_in_html(MF_pos_global, piece+3, 2);
                        // Set as valid
                        result = true;
                    }
                }  

                // TODO: ##ifdef for debugging
                printf("Dis HH: %d\n", hh_mm[0]);
                printf("Dis MM: %d\n", hh_mm[1]);
            }
            break;

        case TEMP_CODE:
            // temperature
            printf("Validating temperature\n");
            if (atoi(data) > 0){
                ref_temp_global = atoi(data);
                result = true;
            }
            // If it has been validated, update HTML
            if (result){
                replace_in_html(TC_pos_global, data, 2);
            }
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
    uint32_t notif = 0;

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

            // Data received updates time on via query string
            if (httpd_query_key_value(buf, "t_on", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG_SERVER, "Found URL query parameter => t_on=%s", param);
                // Validate data from user
                if(validate_value(param, TON_CODE)){
                    printf("Antes del *notif = TON_CODE;\n");
                    notif = (uint32_t)TON_CODE;
                    printf("después del *notif = TON_CODE;\n");
                }

            }

            // Data received updates time off via query string
            if (httpd_query_key_value(buf, "t_off", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG_SERVER, "Found URL query parameter => t_off=%s", param);
                if(validate_value(param, TOFF_CODE)){
                    notif = TOFF_CODE;
                }

            }
            
            // Data received updates reference temperature via query string
            if (httpd_query_key_value(buf, "temp", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG_SERVER, "Found URL query parameter => temp=%s", param);
                if(validate_value(param, TEMP_CODE)){
                    notif = TEMP_CODE;
                }
            }
            printf("Antes del notif\n");
            if (notif > 0){
                if (!xQueueSend(web_lecture_queue, &notif, portMAX_DELAY)){
                    ESP_LOGI(TAG_TASK_WRITE, "ERROR Writing to web queue\n");
                }
            }
        }

        free(buf);

    }

    // TODO: Update to meaningful values
    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");

    printf("%s\n", formated_html);


    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) formated_html;
    printf("%s\n", resp_str);
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


void initial_replacement(){

    HO_pos_global = strstr(formated_html, "HO");
    MO_pos_global = strstr(formated_html, "MO");
    HF_pos_global = strstr(formated_html, "HF");
    MF_pos_global = strstr(formated_html, "MF");
    TC_pos_global = strstr(formated_html, "TC");

}

void replace_in_html(char* target, char* insertion, uint32_t num_of_bytes){

    uint32_t i;

    for (i = 0; i < num_of_bytes; i++){
        printf("i:%d\n", i);
        *(target + i) = *(insertion+i);
    }

}

/* Main */
void app_main(){
    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    ESP_ERROR_CHECK(example_connect());

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    initial_replacement();

    server = start_webserver();

    // Create queue to publish temperature sensor's data
    // TODO: remove magic number 10
    i2c_lecture_queue = xQueueCreate(10, sizeof(struct SensorData));
    web_lecture_queue = xQueueCreate(10, sizeof(uint32_t));

    if (gpio_setup() != ESP_OK){
        ESP_LOGI(TAG_MAIN, "Error configuring GPIOs");
    } else {
        ESP_LOGI(TAG_MAIN, "Starting tasks");
        // Task to control the relay
        xTaskCreate(control_task, "control_task", 1024, NULL, 10, NULL);
        // Task to control SHT30 sensor
        xTaskCreate(i2c_task_example, "i2c_task_example", 2048, NULL, 10, NULL);
    }
}
