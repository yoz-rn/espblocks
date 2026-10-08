#include <Arduino.h>
#include <WiFi.h>

#include "espb_config.h"
#include "espb_net.h"
#include "espb_time.h"
#include "espb_mqtt.h"

#ifndef ESPBLOCKS_VERSION
#define ESPBLOCKS_VERSION "dev-unversioned"
#endif

static const char *TAG = "main";
static const char *NTP_SERVER = "pool.ntp.org";
static const uint32_t WARN_EVERY_MS = 15000;
static const uint32_t HEAP_LOG_EVERY_MS = 60000;

static Espbconfig g_cfg;

enum class Stage { FAILED, WAIT_WIFI, WAIT_CLOCK, WAIT_MQTT, RUNNING };
static Stage g_stage = Stage::WAIT_WIFI;
static uint32_t g_stage_since_ms = 0;
static uint32_t g_last_warn_ms = 0;
static uint32_t g_last_heap_ms = 0;

static bool due(uint32_t last, uint32_t every) {
  return (uint32_t)(millis() - last) >= every;
}

static void enter(Stage next) {
  g_stage = next;
  g_stage_since_ms = millis();
  g_last_warn_ms = g_stage_since_ms;
  g_last_heap_ms = g_stage_since_ms;
}

static uint32_t seconds_in_stage() {
  return (millis() - g_stage_since_ms) / 1000;
}

/* ---- diagnostics (to move into a diag module later) ---- */

static void log_heap(const char *when) {
  ESP_LOGI(TAG, "heap [%s]: free=%u min=%u largest=%u", when,
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

static void log_partitions() {
  esp_partition_iterator_t it = esp_partition_find(
    ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *p = esp_partition_get(it);
    ESP_LOGI(TAG, "partition %-10s type=%d sub=0x%02x addr=0x%06lx size=0x%06lx",
             p->label, p->type, p->subtype, (unsigned long)p->address, (unsigned long)p->size);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
}

static void log_banner() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  // ESP_LOGI(TAG, "espblocks %s", ESPBLOCKS_VERSION);
  ESP_LOGI(TAG, "IDF %s", esp_get_idf_version());
  ESP_LOGI(TAG, "chip: %d core, revision %d", chip.cores, chip.revision);
  ESP_LOGI(TAG, "flash: %lu bytes", (unsigned long)ESP.getFlashChipSize());
  log_heap("boot");
}

static void log_clock() {
  time_t now = time(NULL);
  struct tm tm;
  char ts[32];
  gmtime_r(&now, &tm);
  strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);
  ESP_LOGI(TAG, "clock synced: %s", ts);
}

/* ---- boot ---- */

void setup() {
  log_banner();
  log_partitions();

  esp_err_t err = espb_config_load(&g_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "config load failed: %s", esp_err_to_name(err));
    enter(Stage::FAILED);
    return;
  }
  ESP_LOGI(TAG, "config ok: device=%s, broker=%s:%u, interval=%lu ms",
           g_cfg.device_id, g_cfg.mqtt_host, g_cfg.mqtt_port, (unsigned long)g_cfg.interval_ms);
  
  err = espb_net_start(g_cfg.wifi_ssid, g_cfg.wifi_pass);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
    enter(Stage::FAILED);
    return;
  }
  enter(Stage::WAIT_WIFI);
}

void loop() {
  switch (g_stage)
  {
  case Stage::WAIT_WIFI:
    if (espb_net_is_up()) {
      ESP_LOGI(TAG, "wifi up, ip=%s", WiFi.localIP().toString().c_str());
      ESP_LOGI(TAG, "clock before sync: epoch=%ld", (long)time(NULL));
      espb_time_start(NTP_SERVER);
      enter(Stage::WAIT_CLOCK);
    } else if (due(g_last_warn_ms, WARN_EVERY_MS)) {
      ESP_LOGW(TAG, "waiting for wifi (%lu s)", (unsigned long)seconds_in_stage());
      g_last_warn_ms = millis();
    }
    break;
  
  case Stage::WAIT_CLOCK:
    if (!espb_net_is_up()) {
      ESP_LOGW(TAG, "wifi lost while waiting for clock");
      enter(Stage::WAIT_WIFI);
    } else if (espb_time_is_valid()) {
      log_clock();
      esp_err_t err = espb_mqtt_start(&g_cfg);
      if (err == ESP_OK) {
        enter(Stage::WAIT_MQTT);
      } else {
        ESP_LOGE(TAG, "mqtt failed to start: %s", esp_err_to_name(err));
        enter(Stage::FAILED);
      }
    } else if (due(g_last_warn_ms, WARN_EVERY_MS)) {
      ESP_LOGW(TAG, "waiting for the clock (%lu s)", (unsigned long) seconds_in_stage());
      g_last_warn_ms = millis();
    }
    break;

  case Stage::WAIT_MQTT:
    if (espb_mqtt_is_connected()) {
      const char *hello = "hello";
      int id = espb_mqtt_publish(hello, strlen(hello));
      ESP_LOGI(TAG, "queued test message: id=%d", id);
      log_heap("mqtt connected");
      enter(Stage::RUNNING);
    } else if (due(g_last_warn_ms, WARN_EVERY_MS)) {
      ESP_LOGW(TAG, "mqtt not connected yet (%lu s), client keeps retrying", (unsigned long)seconds_in_stage());
      g_last_warn_ms = millis();
    }
    break;

  case Stage::RUNNING:
    if (due(g_last_heap_ms, HEAP_LOG_EVERY_MS)) {
      log_heap("running");
      g_last_heap_ms = millis();
    }
    break;

  default:
    break;
  }
  delay(200);
}