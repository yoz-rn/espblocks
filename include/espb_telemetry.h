#pragma once

#include "espb_config.h"

#ifndef ESPB_MAX_PAYLOAD_BYTES
#define ESPB_MAX_PAYLOAD_BYTES 256   /* build-time limit (D5); sizes the static buffers */
#endif

/* A producer writes JSON *fields* without braces, e.g. "temp":28.4,"rpm":1500
 * and returns the length written (0 is allowed), or a negative value on error. */
typedef int (*espb_producer_fn)(char *buf, size_t cap);

/* Starts the telemetry task. producer may be NULL to use the built-in dummy data.
 * cfg must stay valid for the lifetime of the task. */
esp_err_t espb_telemetry_start(const Espbconfig *cfg, espb_producer_fn producer);