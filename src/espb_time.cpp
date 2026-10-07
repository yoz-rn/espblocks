#include "espb_time.h"

#include <Arduino.h>

static const char *TAG = "espb_time";

// Anything earlier means the clock was never set (2025-01-01 00:00:00 UTC).
static const time_t MIN_VALID_EPOCH = 1735689600;

esp_err_t espb_time_start(const char *server) {
    if (server == NULL || server[0] == '\0') return ESP_ERR_INVALID_ARG;
    configTime(0, 0, server); // UTC; timestamps in the payload are epoch seconds
    ESP_LOGI(TAG, "sntp started");
    return ESP_OK;
}

bool espb_time_is_valid(void) {
    return time(NULL) >= MIN_VALID_EPOCH;
}

esp_err_t espb_time_wait_valid(uint32_t timeout_ms) {
    const uint32_t step_ms = 200;
    for (uint32_t waited = 0; waited < timeout_ms; waited += step_ms) {
        if (espb_time_is_valid()) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
    return espb_time_is_valid() ? ESP_OK : ESP_ERR_TIMEOUT;
}