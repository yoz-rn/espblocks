#include "espb_config.h"

#include <string.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <mbedtls/platform_util.h>

static const char *TAG = "espb_config";
static const char *PART = "prov";
static const char *NS = "espblocks";

#define TRY(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) return e_; } while (0)

static esp_err_t check(esp_err_t err, const char *key) {
    if (err == ESP_OK) return ESP_OK;
    
    if (err == ESP_ERR_NVS_NOT_FOUND) ESP_LOGE(TAG, "missing key: %s", key);
    else if (err == ESP_ERR_NVS_INVALID_LENGTH) ESP_LOGE(TAG, "value too long for key: %s", key);
    else ESP_LOGE(TAG, "cannot read key %s: %s", key, esp_err_to_name(err));
    return err;
}

static esp_err_t get_str(nvs_handle_t h, const char *key, char *dst, size_t cap) {
    size_t len = cap;
    return check(nvs_get_str(h, key, dst, &len), key);
}

static esp_err_t load_all(nvs_handle_t h, Espbconfig *c) {
    TRY(get_str(h, "wifi_ssid", c->wifi_ssid, sizeof c->wifi_ssid));
    TRY(get_str(h, "wifi_pass", c->wifi_pass, sizeof c->wifi_pass));
    TRY(get_str(h, "mqtt_host", c->mqtt_host, sizeof c->mqtt_host));
    TRY(check(nvs_get_u16(h, "mqtt_port", &c->mqtt_port), "mqtt_port"));
    TRY(get_str(h, "mqtt_user", c->mqtt_user, sizeof c->mqtt_user));
    TRY(get_str(h, "mqtt_pass", c->mqtt_pass, sizeof c->mqtt_pass));
    TRY(get_str(h, "device_id", c->device_id, sizeof c->device_id));
    TRY(check(nvs_get_u32(h, "interval_ms", &c->interval_ms), "interval_ms"));
    TRY(get_str(h, "ca_cert", c->ca_cert, sizeof c->ca_cert));

    if (strcmp(c->mqtt_user, c->device_id) != 0) {
        ESP_LOGE(TAG, "mqtt_user must equal device_id (the broker ACL depends on it)");
        return ESP_ERR_INVALID_STATE;
    }

    if (c->mqtt_port == 0 || c->interval_ms == 0) {
        ESP_LOGE(TAG, "mqtt_port and interval_ms must be greater than zero");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t espb_config_load(Espbconfig *out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof *out);

    esp_err_t err = nvs_flash_init_partition(PART);
    if (err != ESP_OK) {
        // Deliberately no erase-and-retry: a partition is never wiped automatically.
        ESP_LOGE(TAG, "cannot init partition '%s': %s", PART, esp_err_to_name(err));
        return err;
    }

    nvs_handle_t h;
    err = nvs_open_from_partition(PART, NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot open namespace '%s' (was prov.bin flashed?): %s", NS, esp_err_to_name(err));
        return err;
    }

    err = load_all(h, out);
    nvs_close(h);

    if (err != ESP_OK) mbedtls_platform_zeroize(out, sizeof *out); // no half-filled secrets
    return err;
}