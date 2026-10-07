#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts SNTP. Call once Wi-Fi has an IP. Non-blocking; the clock is re-synced periodically. */
esp_err_t espb_time_start(const char *server);

/* True if the system clock looks plausible (not the 1970 default). */
bool espb_time_is_valid(void);

/* Blocks until the clock is valid, or returns ESP_ERR_TIMEOUT. */
esp_err_t espb_time_wait_valid(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif