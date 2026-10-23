// shared/config.h — settings used by both the camera (cam/) and the display (display/).
// Credentials do NOT live here; see secrets.example.h.
#pragma once

// ---- Network -----------------------------------------------------------------
#define PEEKBOX_CAM_HOSTNAME   "peekbox-cam"        // advertised as peekbox-cam.local
#define PEEKBOX_DISP_HOSTNAME  "peekbox-display"
#define PEEKBOX_HTTP_PORT      80

// Fallback address for the camera, used by the display when the mDNS lookup fails.
// Either reserve this IP for the cam in your router (DHCP reservation), or set
// PEEKBOX_CAM_STATIC_IP to 1 so the cam configures it itself.
#define PEEKBOX_CAM_STATIC_IP  0
#define PEEKBOX_CAM_IP         192, 168, 0, 197
#define PEEKBOX_CAM_GATEWAY    192, 168, 1, 1
#define PEEKBOX_CAM_SUBNET     255, 255, 255, 0
#define PEEKBOX_CAM_DNS        192, 168, 1, 1

// Camera WiFi transmit power (an Arduino-ESP32 wifi_power_t value). The default
// WIFI_POWER_19_5dBm draws the largest current spikes; lowering it eases brownouts
// on a weak 5 V supply at the cost of range. It is a fallback, not a replacement
// for a proper >=1 A supply. Options: WIFI_POWER_19_5dBm, 17dBm, 15dBm, 13dBm,
// 11dBm, 8_5dBm, 7dBm, 5dBm, 2dBm (written e.g. WIFI_POWER_8_5dBm).
#define PEEKBOX_CAM_WIFI_TX_POWER  WIFI_POWER_11dBm

// ---- Frames ------------------------------------------------------------------
#define PEEKBOX_FRAME_W        240
#define PEEKBOX_FRAME_H        240
#define PEEKBOX_JPEG_QUALITY   12     // esp32-camera scale: 0 (best) .. 63 (worst)

// Largest JPEG the display accepts. It holds exactly one buffer of this size;
// bigger frames are dropped. 240x240 at quality 12 is typically 6-14 KB.
#define PEEKBOX_MAX_JPEG_BYTES (20 * 1024)

// ---- Display polling ---------------------------------------------------------
#define PEEKBOX_POLL_INTERVAL_MS   50     // minimum gap between /capture requests
#define PEEKBOX_HTTP_TIMEOUT_MS    3000
#define PEEKBOX_MDNS_RETRY_MS      30000  // how often to retry mDNS while on the fallback IP

// ---- Motion / Telegram -------------------------------------------------------
#define PEEKBOX_PIR_ENABLED        1
#define PEEKBOX_PIR_PIN            13     // AI-Thinker GPIO13 (free when the SD card is unused)
#define PEEKBOX_PIR_WARMUP_MS      60000  // HC-SR501 needs ~1 min to settle after power-up
#define PEEKBOX_MOTION_COOLDOWN_MS 30000  // minimum gap between Telegram snapshots
