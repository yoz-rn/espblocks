#include "espb_mqtt.h"
#include "espb_time.h"

#include <mqtt_client.h>
#include <esp_log.h>

static const char *TAG = "espb_mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static char s_uri[96];
static char s_topic[96];

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "broker acknowledged msg_id=%d", ev->msg_id);
        break;

    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "transport error: tls=0x%x stack=0x%x certflags=0x%x",
                     ev->error_handle->esp_tls_last_esp_err,
                     ev->error_handle->esp_tls_stack_err,
                     ev->error_handle->esp_tls_cert_verify_flags);
        } else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "broker refused the connection, code %d",
                     ev->error_handle->connect_return_code);
        }
        break;

    default:
        break;
    }
}

esp_err_t espb_mqtt_start(const Espbconfig *cfg) {
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (s_client != NULL) return ESP_ERR_INVALID_STATE;

    if (!espb_time_is_valid()) {
        ESP_LOGE(TAG, "clock not valid");
        return ESP_ERR_INVALID_STATE;
    }
    int n = snprintf(s_uri, sizeof s_uri, "mqtts://%s:%u", cfg->mqtt_host, cfg->mqtt_port);
    if (n < 0 || n >= (int)sizeof s_uri) return ESP_ERR_INVALID_SIZE;
    n = snprintf(s_topic, sizeof s_topic, "espblocks/%s/telemetry", cfg->device_id);
    if (n < 0 || n >= (int)sizeof s_topic) return ESP_ERR_INVALID_SIZE;

    esp_mqtt_client_config_t mc = {};
    mc.uri = s_uri;
    mc.client_id = cfg->device_id;
    mc.username = cfg->mqtt_user;
    mc.password = cfg->mqtt_pass;
    mc.cert_pem = cfg->ca_cert;
    mc.keepalive = 60;

    s_client = esp_mqtt_client_init(&mc);
    if (s_client == NULL) return ESP_FAIL;

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, NULL);
    return esp_mqtt_client_start(s_client);
}

bool espb_mqtt_is_connected(void) { return s_connected; }

int espb_mqtt_publish(const char *data, size_t len) {
    if (s_client == NULL || data == NULL) return -1;
    return esp_mqtt_client_enqueue(s_client, s_topic, data, (int)len, 1, 0, true);
}