// motion.cpp — HC-SR501 PIR on the ESP32-CAM. On each new motion event (after warm-up
// and outside the cooldown), grab a frame and send it to Telegram.
#include <Arduino.h>
#include "esp_camera.h"

#include "config.h"
#include "telegram.h"

static bool active = false;
static uint32_t events = 0;
static uint32_t lastSent = 0;
static bool sentOnce = false;

bool motionActive() { return active; }
uint32_t motionEvents() { return events; }

void motionSetup() {
#if PEEKBOX_PIR_ENABLED
  // HC-SR501 drives its output push-pull at 3.3 V; the pull-down keeps the pin
  // quiet if the sensor is unplugged.
  pinMode(PEEKBOX_PIR_PIN, INPUT_PULLDOWN);
  Serial.printf("[pir] on GPIO%d, warming up for %d s; telegram %s\n", PEEKBOX_PIR_PIN,
                PEEKBOX_PIR_WARMUP_MS / 1000, telegramEnabled() ? "enabled" : "disabled (no token)");
#endif
}

static void sendSnapshot() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[pir] capture for telegram failed");
    return;
  }
  char caption[64];
  snprintf(caption, sizeof(caption), "PeekBox: motion detected (event #%lu)",
           (unsigned long)events);
  telegramSendPhoto(fb->buf, fb->len, caption);
  esp_camera_fb_return(fb);
}

void motionLoop() {
#if PEEKBOX_PIR_ENABLED
  if (millis() < PEEKBOX_PIR_WARMUP_MS) return;  // sensor output is noise while settling

  bool now = digitalRead(PEEKBOX_PIR_PIN) == HIGH;
  if (now == active) return;
  active = now;
  if (!active) return;

  events++;
  Serial.printf("[pir] motion #%lu\n", (unsigned long)events);
  if (!telegramEnabled()) return;
  if (sentOnce && millis() - lastSent < PEEKBOX_MOTION_COOLDOWN_MS) return;
  lastSent = millis();
  sentOnce = true;
  sendSnapshot();
#endif
}
