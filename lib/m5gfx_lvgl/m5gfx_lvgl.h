#ifndef __M5GFX_LVGL_H__
#define __M5GFX_LVGL_H__

#include "lvgl.h"
#include "M5Unified.h"
#include "M5GFX.h"

void m5gfx_lvgl_init(void);

// When true, the LVGL flush callback skips all M5.Display (LCD) SPI traffic and
// just acks the frame. The CoreS3 LCD and SD card share the SPI bus AND the LCD
// reuses MISO (GPIO35) as its D/C line (driven as an output during a flush),
// which collides with SD writes and wedges the card under sustained recording.
// The recorder sets this true while capturing so the SD card owns the bus.
extern volatile bool g_lcd_flush_suppress;

#endif  // __M5GFX_LVGL_H__