#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <WiFi.h>


#include "espb_config.h"
#include "espb_net.h"

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

} else {
  ESP_LOGE("main", "config load failed: %s", esp_err_to_name(err)); 
}
  
}

void loop() {
  delay(10000);
}