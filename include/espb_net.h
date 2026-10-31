#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts Wi-Fi in station mode. Non-blocking. Reconnects forever with backoff. */
esp_err_t espb_net_start(const char *ssid, const char *pass);

/* True while the device has an IP address. */
bool espb_net_is_up(void);

/* Blocks until connected, or returns ESP_ERR_TIMEOUT. */
esp_err_t espb_net_wait_up(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif