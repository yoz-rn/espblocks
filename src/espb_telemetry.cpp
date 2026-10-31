#include "espb_telemetry.h"
#include "espb_mqtt.h"
#include <esp_system.h>
#include <stdarg.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <time.h>

static const char *TAG = "espb_telemetry";

static const uint32_t TICK_MS = 1000; // also feeds the task watchdog
static const uint64_t OFFLINE_GRACE_US = 30LL * 1000 * 1000; // about the esp-mqtt outbox lifetime

#define FIELDS_MAX (ESPB_MAX_PAYLOAD_BYTES - 64)

static char s_fields[FIELDS_MAX];
static char s_msg[ESPB_MAX_PAYLOAD_BYTES];

static const Espbconfig *s_cfg = NULL;
static espb_producer_fn s_producer = NULL;

// Appends formatted text. Returns false (and writes nothing more) if it would not fit.
static bool append(char *buf, size_t cap, size_t *len, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *len) return false; // never truncate
    *len += (size_t)n;
    return true;
}

// Dummy sensor: a slow random walk for temperature, random rpm.
static int dummy_producer(char *buf, size_t cap) {
    static int temp_x10 = 280;
    temp_x10 = (int)(esp_random() % 11) - 5;
    if (temp_x10 < 200) temp_x10 = 200;
    if (temp_x10 > 400) temp_x10 = 400;
    int rpm = 800 + (int)(esp_random() % 1400);
    int n = snprintf(buf, cap, "\"temp\":%d.%d,\"rpm\":%d",
            temp_x10 / 10, temp_x10 % 10, rpm);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

// Builds {"d":...,"n":...,"t":...,<fields>} into s_msg. Returns false if it does not fit.
static bool build_message(uint32_t seq, size_t *out_len) {
    int flen = s_producer(s_fields, sizeof s_fields);
    if (flen < 0) {
        ESP_LOGE(TAG, "producer failed");
        return false;
    }

    size_t len = 0;
    bool ok = append(s_msg, sizeof s_msg, &len, "{\"d\":\"%s\",\"n\":%lu,\"t\":%ld",
                     s_cfg->device_id, (unsigned long)seq, (long)time(NULL));
    if (ok && flen > 0) ok = append(s_msg, sizeof s_msg, &len, ",%s", s_fields);
    if (ok) ok = append(s_msg, sizeof s_msg, &len, "}");

    if (!ok) {
        ESP_LOGE(TAG, "message does not fit in %d bytes, not sent", ESPB_MAX_PAYLOAD_BYTES);
        return false;
    }
    *out_len = len;
    return true;
}

static void telemetry_task(void *) {
    esp_task_wdt_add(NULL); // this task must report in, or the watchdog notices
    TickType_t wake = xTaskGetTickCount(); 
    uint32_t elapsed_ms = 0, seq = 0, sent = 0, dropped = 0;
    int64_t down_since_us = 0; // 0 means connected

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
        esp_task_wdt_reset();

        elapsed_ms += TICK_MS;
        if (elapsed_ms < s_cfg->interval_ms) continue;
        elapsed_ms -= s_cfg->interval_ms;

        int64_t now_us = esp_timer_get_time();
        if (espb_mqtt_is_connected()) down_since_us = 0;
        else if (down_since_us == 0) down_since_us = now_us;
        bool may_queue = (down_since_us == 0) || (now_us - down_since_us) < OFFLINE_GRACE_US;

        seq++; // counts every cycle, so lost data shows up as a gap
        size_t len = 0;
        bool ok = may_queue && build_message(seq, &len) && espb_mqtt_publish(s_msg, len) > 0;
        if (ok) sent++; else dropped++;
        if (seq % 6 ==0) {
            ESP_LOGI(TAG, "seq=%lu sent=%lu dropped=%lu offline=%lds",
               (unsigned long)seq, (unsigned long)sent, (unsigned long)dropped,
               (long)(down_since_us ? (now_us - down_since_us) / 1000000 : 0));
        }
    }
}

esp_err_t espb_telemetry_start(const Espbconfig *cfg, espb_producer_fn producer) {
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (s_cfg != NULL) return ESP_ERR_INVALID_STATE;
    if (cfg->interval_ms < TICK_MS) {
        ESP_LOGE(TAG, "interval_ms must be at least %u", (unsigned)TICK_MS);
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = cfg;
    s_producer = (producer != NULL) ? producer : dummy_producer;

    if (xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 3, NULL) != pdPASS) {
        s_cfg = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}