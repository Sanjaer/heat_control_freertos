#include "sntp_m.h"

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
void initialize_sntp(void) {

    ESP_LOGI(TAG_SNTP, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

}
