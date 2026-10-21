// board_smalltv_ultra.h — GeekMagic SmallTV Ultra (ESP8266EX, 4 MB flash, ST7789 240x240 IPS).
//
// The TFT_eSPI pins themselves are set as build_flags in platformio.ini (TFT_eSPI reads
// them from there; nothing is edited inside the library). This header keeps the parts
// the sketch drives directly, plus the sources for every pin.
//
// Sources:
//   [1] smalltv-mod, src/board_esp8266.h + src/Gfx.cpp
//       https://github.com/giovi321/smalltv-mod (commit 4842509, 2026-10-16)
//       SCLK=14, MOSI=13, DC=0, RST=2, CS=15, BL=5 (active-low), ST7789 in SPI mode 3, RGB order.
//   [2] ESPHome device page "geekmagic ultra"
//       https://devices.esphome.io/devices/geekmagic-ultra/
//       clk=GPIO14, mosi=GPIO13, dc=GPIO00, reset=GPIO02, backlight GPIO05 PWM inverted,
//       spi_mode: mode3, invert_colors: true, color_order: BGR, no cs_pin.
//
// UNCERTAIN — check on first flash:
//   * CS: [1] uses GPIO15, [2] has no CS (panel CS probably tied low). Driving GPIO15 is
//     harmless either way, so we follow [1]. GPIO15 is a boot-strap pin (must be low at
//     boot); the board's pull-down handles that.
//   * Colour order: [1] says RGB, [2] says BGR. If red and blue are swapped, change
//     TFT_RGB_ORDER in platformio.ini.
//   * Touch button: neither source documents a GPIO for the Ultra's touch button
//     (smalltv-mod has no button support on ESP8266; the ESPHome touch config is for the
//     ESP32-based SmallTV Pro). It is DISABLED until you find the pin with the probe build
//     (see README: "Finding the touch button pin").
#pragma once

// Backlight: GPIO5, active-low [1][2].
#define BOARD_BL_PIN            5
#define BOARD_BL_ON             LOW

// Touch button. -1 = disabled. Set to the GPIO found with the probe build.
#ifndef BOARD_BUTTON_PIN
#define BOARD_BUTTON_PIN        -1
#endif
// Level the pin reads while touched. TTP223-style touch ICs are usually active-high.
#ifndef BOARD_BUTTON_ACTIVE
#define BOARD_BUTTON_ACTIVE     HIGH
#endif

// GPIOs not used by the display, flash or UART: candidates for the button probe.
// (GPIO0/2/15 are boot-strap pins and are taken by the display anyway.)
#define BOARD_PROBE_PINS        {4, 12, 16}
