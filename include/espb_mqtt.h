#pragma once

#include <esp_err.h>

#include "espb_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the MQTT client over TLS. Refuses to start while the clock is invalid.
 * cfg must stay valid for the lifetime of the client (it holds the CA certificate).
 */
esp_err_t espb_mqtt_start(const Espbconfig *cfg);

bool espb_mqtt_is_connected(void);

/* Queues one message (QoS 1) on this device's telemetry topic.
 * Returns a message id (> 0) or a negative value on failure.
 */
int espb_mqtt_publish(const char *data, size_t len);

#ifdef __cplusplus
}
#endif