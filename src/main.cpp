#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <WiFi.h>


#include "espb_config.h"
#include "espb_net.h"
#include "espb_time.h"
#include "espb_mqtt.h"

#ifndef ESPBLOCKS_VERSION
#define ESPBLOCKS_VERSION "dev-unversioned"
#endif

static Espbconfig g_cfg;

void printPartitions() {
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *p = esp_partition_get(it);
    Serial.printf("%-10s type=%d sub=0x%02x addr=0x%06lx size=0x%06lx\n",
                  p->label, p->type, p->subtype,
                  (unsigned long)p->address, (unsigned long)p->size);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  esp_chip_info_t chip;
  esp_chip_info(&chip);

  Serial.printf("espblocks %s\n", ESPBLOCKS_VERSION);
  Serial.printf("IDF %s\n", esp_get_idf_version());
  Serial.printf("chip: %d core, revision %d\n", chip.cores, chip.revision);
  Serial.printf("flash: %lu bytes\n", (unsigned long)ESP.getFlashChipSize());
  Serial.printf("heap: free=%lu min=%lu largest=%lu\n",
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMinFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
  printPartitions();

  esp_err_t err = espb_config_load(&g_cfg);
  if (err == ESP_OK) {
    ESP_LOGI("main", "config ok: device=%s broker=%s:%u interval=%lu ms",
              g_cfg.device_id, g_cfg.mqtt_host, g_cfg.mqtt_port,
              (unsigned long)g_cfg.interval_ms);
    
    err = espb_net_start(g_cfg.wifi_ssid,g_cfg.wifi_pass);
    if (err != ESP_OK) {
      ESP_LOGE("main", "wifi start failed: %s", esp_err_to_name(err));
    } else if (espb_net_wait_up(20000) == ESP_OK) {
      ESP_LOGI("main", "wifi up, ip=%s", WiFi.localIP().toString().c_str());
    } else {
      ESP_LOGW("main", "wifi not up after 20 s, still retrying in the background");
    }

    ESP_LOGI("main", "clock before sync: epoch=%ld", (long)time(NULL));
    espb_time_start("pool.ntp.org");
    if (espb_time_wait_valid(15000) == ESP_OK) {
      time_t now = time(NULL);
      struct tm tm;
      char ts[32];
      gmtime_r(&now, &tm);
      strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);
      ESP_LOGI("main", "clock synced: %s", ts);
    } else {
      ESP_LOGW("main", "clock not synced after 15 s, TLS must not start yet");
    }

    delay(2000);

    esp_err_t me = espb_mqtt_start(&g_cfg);
    if (me != ESP_OK) ESP_LOGE("main", "mqtt start failed: %s", esp_err_to_name(me));
    else {
      for (int i = 0; i < 75 && !espb_mqtt_is_connected(); i++) delay (200);
      if (espb_mqtt_is_connected()) {
        const char *hello = "hello";
        int id = espb_mqtt_publish(hello, strlen(hello));
        ESP_LOGI("main", "queues test message, id=%d", id);
      } else {
        ESP_LOGW("main", "mqtt not connected after 15 s");
      }
      ESP_LOGI("main", "heap: free=%u min=%u largest=%u",
                (unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    }

  } else {
    ESP_LOGE("main", "config load failed: %s", esp_err_to_name(err)); 
  }
  
}

void loop() {
  delay(10000);
}