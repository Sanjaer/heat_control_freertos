#include <stdlib.h>
#include <string.h>
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
#include "driver/gpio.h"

#include "lwip/apps/sntp.h"


static const char *TAG_SNTP = "sntp";

void sntp_task(void *arg);

bool is_time_set (void);
static void initialize_sntp(void);