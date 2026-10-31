#pragma once

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESPB_STR_MAX 64
#define ESPB_CA_MAX 1024

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_host[ESPB_STR_MAX];
    uint16_t mqtt_port;
    char mqtt_user[ESPB_STR_MAX];
    char mqtt_pass[ESPB_STR_MAX];
    char device_id[ESPB_STR_MAX];
    uint32_t interval_ms;
    char ca_cert[ESPB_CA_MAX];
} Espbconfig;

/* Loads the settings from the "prov" partition.
 * On success: ESP_OK and every field is filled.
 * On failure: an error code, and *out is zeroed. Nothing is ever truncated.
 */

esp_err_t espb_config_load(Espbconfig *out);

#ifdef __cplusplus
}
#endif