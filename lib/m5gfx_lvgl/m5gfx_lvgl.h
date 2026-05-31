#ifndef __M5GFX_LVGL_H__
#define __M5GFX_LVGL_H__

#include "lvgl.h"
#include "M5Unified.h"
#include "M5GFX.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void m5gfx_lvgl_init(void);

// Shared SPI-bus mutex (PRD FR8). The CoreS3 LCD and SD card share the SPI bus
// AND the LCD reuses MISO (GPIO35) as its D/C line (driven as an output during a
// flush), so LCD flushes (core 1) and SD writes (the writer task, core 0) must be
// serialised. The LCD flush try-takes with a short timeout and skips the frame on
// miss (it never blocks the UI/capture thread); the SD writer holds it per op.
extern SemaphoreHandle_t g_spi_bus_mutex;

#endif  // __M5GFX_LVGL_H__