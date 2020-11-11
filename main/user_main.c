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
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "lwip/apps/sntp.h"

#include <esp_http_server.h>
#include "driver/gpio.h"

#include "sht3x.h"

#define GPIO_OUTPUT_IO_1        5
#define GPIO_OUTPUT_PIN_SEL     ((1ULL<<GPIO_OUTPUT_IO_1))


#define TON_CODE                (uint8_t)1
#define TOFF_CODE               (uint8_t)2
#define TEMP_CODE               (uint8_t)3
#define DATA_FIELD_SIZE         2
#define TEMP_HYSTERESIS         1

/* Tags for logs */
static const char *TAG_MAIN = "main";
static const char *TAG_WIFI = "wifi";
static const char *TAG_SERVER = "server";
static const char *TAG_TASK_READ = "ReadQ";
static const char *TAG_TASK_WRITE = "WriteQ";
static const char *TAG_SNTP = "sntp";
static const char *TAG_CTRL = "ctrl";

/* Global consts */
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
static char  formated_html[] =  "<h2>Smart heat controller </h2>" \
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
static void i2c_task_sht30(void *arg);
static void sntp_task(void *arg);


/* Local functions declarations */
static void sleep_seconds(const uint32_t deep_sleep_sec);
bool is_time_set (void);
static void initialize_sntp(void);
esp_err_t gpio_setup(void);
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
bool validate_value(char *data, uint8_t key);
void initial_mapping(void);
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
    uint8_t state=0;
    time_t now;
    struct tm timeinfo;
    uint32_t time_on, time_off, time_now;

    for (;;) {

        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Set as integer to prevent loads of ifs
        time_on = time_on_global[0]*100 + time_on_global[1];
        time_off = time_off_global[0]*100 + time_off_global[1];
        time_now = timeinfo.tm_hour*100 + timeinfo.tm_min;
        printf("STATE:%d\n", state);
        printf("time_on: %d, time_off: %d, time_now: %d\n", time_on, time_off, time_now);

        
        // Main state machine
        switch(state){

            // Check if hours active change day (pass 00:00AM)
            case 1:
                time_on = time_on_global[0]*100 + time_on_global[1];
                time_off = time_off_global[0]*100 + time_off_global[1];
                printf("time_on: %d, time_off: %d, time_now: %d\n", time_on, time_off, time_now);

                if (time_on < time_off){
                    // It doesn't
                    state = 2;
                } else {
                    // It does change day
                    state = 3;
                }
                break;

            // Check if it is time to be active no day between
            case 2:
                time(&now);
                localtime_r(&now, &timeinfo);

                // Set as integer to prevent loads of ifs
                time_on = time_on_global[0]*100 + time_on_global[1];
                time_off = time_off_global[0]*100 + time_off_global[1];
                time_now = timeinfo.tm_hour*100 + timeinfo.tm_min;

                printf("time_on: %d, time_off: %d, time_now: %d\n", time_on, time_off, time_now);

                if ((time_on <= time_now) && (time_off > time_now)){
                    // We are on time
                    state = 4;
                } else{
                    // No time to be on
                    state = 5;
                }
                break;

            // Check if it is time to be active w/ day between
            case 3:

                if ((time_on >= time_now) && (time_off < time_now)){
                    // We are on time
                    state = 4;
                } else{
                    // No time to be on
                    state = 5;
                }
                break;

            // Check if the temperature is low
            // If so, start heating
            case 4:
                if (data_recvd->temperature < ref_temp_global+TEMP_HYSTERESIS){
                    // Heat
                    gpio_set_level(GPIO_OUTPUT_IO_1, 1);                
                    state = 0;
                } else {
                    // send to stop heating
                    state = 5;
                }
                break;

            case 5:
                gpio_set_level(GPIO_OUTPUT_IO_1, 0);
                state = 0;
                break;

            default:
                memset(data_recvd, 0, sizeof(struct SensorData));

                if (xQueueReceive(i2c_lecture_queue, data_recvd, portMAX_DELAY)) {
                    ESP_LOGI(TAG_TASK_READ, "Temperature from queue: %f\n", data_recvd->temperature);
                }
                // TODO: update current temperature html
                state = 1;
                break;

        }

        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

/**
 * @brief task that inits i2c comm, reads info from SHT30 sensor and writes to queue
 */
static void i2c_task_sht30(void *arg) {

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

static void sntp_task(void *arg) {

    char strftime_buf[64];
    time_t now;
    struct tm timeinfo;
    const int retry_count = 10;
    uint8_t retry = 0;

    initialize_sntp();

    // time() returns the time since 00:00:00 UTC, January 1, 1970 (Unix timestamp) in seconds. 
    // If now is not a null pointer, the returned value is also stored in the object pointed to by second.
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    for (;;){
        uint8_t retry = 0;
        // tm_year = The number of years since 1900
        // update 'now' variable with current time
        time(&now);
        localtime_r(&now, &timeinfo);
        // TODO infinite loop <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        while (!is_time_set() && ++retry < retry_count) {
            ESP_LOGI(TAG_SNTP, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG_SNTP, "The current date/time in Madrid is: %s", strftime_buf);

        vTaskDelay(60000 / portTICK_RATE_MS);
    }

}


/* Local functions definitions */
// Function that sends the ESP8266 to sleep for deep_sleep_sec seconds
// TB Used or Removed
static void sleep_seconds(const uint32_t deep_sleep_sec) {
    
    ESP_LOGI(TAG_CTRL, "Entering deep sleep for %d seconds", deep_sleep_sec);
    esp_deep_sleep(1000000LL * deep_sleep_sec);

}

// Function that checks whether the time is set or not
// TB Used or Removed
bool is_time_set (void) {
    bool result = false;
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGI(TAG_SNTP, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
    } else{
        ESP_LOGI(TAG_SNTP, "Time is already set.");
        result = true;
    }

    return result;
}

// Function to initialize SNTP, to be called only once
static void initialize_sntp(void) {

    ESP_LOGI(TAG_SNTP, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

}

// Function to configure GPIOs
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
                        replace_in_html(HO_pos_global, data, DATA_FIELD_SIZE);
                        replace_in_html(MO_pos_global, piece+3, DATA_FIELD_SIZE);
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
                        replace_in_html(HF_pos_global, data, DATA_FIELD_SIZE);
                        replace_in_html(MF_pos_global, piece+3, DATA_FIELD_SIZE);
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
                replace_in_html(TC_pos_global, data, DATA_FIELD_SIZE);
            }
            break;

        default:
            result = false;
            break;
    }

    return result;

}

// Function that maps the HTML for later replacement of Key fields
void initial_mapping(){

    HO_pos_global = strstr(formated_html, "HO");
    MO_pos_global = strstr(formated_html, "MO");
    HF_pos_global = strstr(formated_html, "HF");
    MF_pos_global = strstr(formated_html, "MF");
    TC_pos_global = strstr(formated_html, "TC");

}

void replace_in_html(char* target, char* insertion, uint32_t num_of_bytes){

    for (uint32_t i = 0; i < num_of_bytes; i++){
        *(target + i) = *(insertion+i);
    }

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
                    notif = (uint32_t)TON_CODE;
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

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) formated_html;
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
void app_main(){
    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(sntp_task, "sntp_task", 2048, NULL, 10, NULL);

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    initial_mapping();

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
        xTaskCreate(i2c_task_sht30, "i2c_task_sht30", 2048, NULL, 10, NULL);
    }
}
