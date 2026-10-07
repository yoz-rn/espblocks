#include "espb_net.h"

#include <WiFi.h>
#include<freertos/event_groups.h>

static const char *TAG = "espb_net";
static const uint32_t BACKOFF_MIN_MS = 1000;
static const uint32_t BACKOFF_MAX_MS = 60000;
static const EventBits_t BIT_UP = BIT0;

static EventGroupHandle_t s_events = NULL;
static esp_timer_handle_t s_retry_timer = NULL;
static uint32_t s_backoff_ms = BACKOFF_MIN_MS;
static bool s_started = false;

static void retry_cb(void *) {
    if (espb_net_is_up()) return; // connected in the meantime: do nothing
    ESP_LOGI(TAG, "reconnecting...");
    WiFi.reconnect();
}

static void on_event(arduino_event_id_t id, arduino_event_info_t info) {
    switch (id)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        esp_timer_stop(s_retry_timer);  // a retry may still be pending
        s_backoff_ms = BACKOFF_MIN_MS;
        xEventGroupSetBits(s_events, BIT_UP);
        ESP_LOGI(TAG, "connected");
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        xEventGroupClearBits(s_events, BIT_UP);
        if (esp_timer_is_active(s_retry_timer)) break; // retry already scheduled: ignore duplicate events
        ESP_LOGW(TAG, "disconnected (reason %d), retry in %lu ms",
                 info.wifi_sta_disconnected.reason, (unsigned long)s_backoff_ms);
        esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
        s_backoff_ms *= 2;
        if (s_backoff_ms > BACKOFF_MAX_MS) s_backoff_ms = BACKOFF_MAX_MS;
        break;
    
    default:
        break;
    }
}

esp_err_t espb_net_start(const char *ssid, const char *pass) {
    if (s_started) return ESP_ERR_INVALID_STATE;
    if (ssid == NULL || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    s_events = xEventGroupCreate();
    if (s_events == NULL) return ESP_ERR_NO_MEM;

    esp_timer_create_args_t args = {};
    args.callback = retry_cb;
    args.name = "espb_net_retry";
    esp_err_t err = esp_timer_create(&args, &s_retry_timer);
    if (err != ESP_OK) return err;

    WiFi.persistent(false); // never copy credentials into the default nvs partition
    WiFi.setAutoReconnect(false); // yeah right
    WiFi.onEvent(on_event);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    s_started = true;
    return ESP_OK;
}

bool espb_net_is_up(void) {
    return s_events != NULL && (xEventGroupGetBits(s_events) & BIT_UP) != 0;
}

esp_err_t espb_net_wait_up(uint32_t timeout_ms) {
    if (s_events == NULL) return ESP_ERR_INVALID_STATE;
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_UP, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_UP) ? ESP_OK : ESP_ERR_TIMEOUT;
}