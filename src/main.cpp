#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <nvs.h>

#ifndef ESPBLOCKS_VERSION
#define ESPBLOCKS_VERSION "dev-unversioned"
#endif

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

static void printProvisioning() {
  esp_err_t err = nvs_flash_init_partition("prov");
  if (err != ESP_OK) {
    Serial.printf("prov: init gagal: %s\n", esp_err_to_name(err));
    return;  // sengaja tidak menghapus partisi otomatis
  }

  nvs_handle_t h;
  err = nvs_open_from_partition("prov", "espblocks", NVS_READONLY, &h);
  if (err != ESP_OK) {
    Serial.printf("prov: namespace tidak dibuka: %s\n", esp_err_to_name(err));
    return;
  }

  char buf[64];
  size_t len = sizeof(buf);
  if (nvs_get_str(h, "device_id", buf, &len) == ESP_OK)
    Serial.printf("device_id=%s\n", buf);

  len = sizeof(buf);
  if (nvs_get_str(h, "mqtt_host", buf, &len) == ESP_OK)
    Serial.printf("mqtt_host=%s\n", buf);

  uint16_t port = 0;
  if (nvs_get_u16(h, "mqtt_port", &port) == ESP_OK)
    Serial.printf("mqtt_port=%u\n", port);

  uint32_t interval = 0;
  if (nvs_get_u32(h, "interval_ms", &interval) == ESP_OK)
    Serial.printf("interval_ms=%lu\n", (unsigned long)interval);

  // rahasia: cetak panjangnya saja, bukan isinya
  const char *secrets[] = {"wifi_pass", "mqtt_pass", "ca_cert"};
  for (const char *key : secrets) {
    len = 0;
    if (nvs_get_str(h, key, NULL, &len) == ESP_OK)
      Serial.printf("%s: %u byte\n", key, (unsigned)len);
  }

  nvs_close(h);
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
  printProvisioning();
}

void loop() {
  delay(10000);
}