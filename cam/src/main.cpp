// PeekBox camera firmware — AI-Thinker ESP32-CAM.
//
//   GET /capture -> one 240x240 JPEG
//   GET /status  -> JSON: uptime, rssi, free heap, motion state
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_camera.h"

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing shared/secrets.h: copy shared/secrets.example.h to shared/secrets.h and fill it in."
#endif

#include "camera_pins.h"

static constexpr int CAMERA_INIT_ATTEMPTS = 5;

static WebServer server(PEEKBOX_HTTP_PORT);

// ---- Camera ------------------------------------------------------------------

static bool initCameraOnce() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_240X240;
  c.jpeg_quality = PEEKBOX_JPEG_QUALITY;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;  // always serve the newest frame, never a stale one

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("[cam] esp_camera_init failed: 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }
  return true;
}

static void initCamera() {
  if (!psramFound()) {
    Serial.println("[cam] WARNING: PSRAM not found. Check the board is an AI-Thinker with PSRAM.");
  }
  for (int attempt = 1; attempt <= CAMERA_INIT_ATTEMPTS; attempt++) {
    Serial.printf("[cam] camera init attempt %d/%d\n", attempt, CAMERA_INIT_ATTEMPTS);
    if (initCameraOnce()) {
      Serial.println("[cam] camera ready (240x240 JPEG)");
      return;
    }
    esp_camera_deinit();
    // Power-cycle the sensor through PWDN before retrying.
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    delay(200);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(300 * attempt);
  }
  Serial.println("[cam] FATAL: camera init failed. Check the ribbon cable, the 5V supply (>=1A)"
                 " and that this is an AI-Thinker board. Rebooting in 5 s.");
  delay(5000);
  ESP.restart();
}

// ---- WiFi --------------------------------------------------------------------

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(PEEKBOX_CAM_HOSTNAME);
  WiFi.setSleep(false);  // modem sleep adds 100+ ms latency to every request
#if PEEKBOX_CAM_STATIC_IP
  if (!WiFi.config(IPAddress(PEEKBOX_CAM_IP), IPAddress(PEEKBOX_CAM_GATEWAY),
                   IPAddress(PEEKBOX_CAM_SUBNET), IPAddress(PEEKBOX_CAM_DNS))) {
    Serial.println("[wifi] static IP config failed, falling back to DHCP");
  }
#endif
  WiFi.begin(PEEKBOX_WIFI_SSID, PEEKBOX_WIFI_PASS);
  // Must follow begin(): the 19.5 dBm default overwrote it when set earlier (seen on core 3.3.12).
  if (!WiFi.setTxPower(PEEKBOX_CAM_WIFI_TX_POWER)) {
    Serial.println("[wifi] setTxPower failed, using default power");
  }
  Serial.printf("[wifi] connecting to %s", PEEKBOX_WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 30000) {
      Serial.println("\n[wifi] timed out, rebooting");
      ESP.restart();
    }
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n[wifi] connected, IP %s, RSSI %d dBm, TX power %.1f dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), (int)WiFi.getTxPower() / 4.0f);  // wifi_power_t is in 0.25 dBm units
}

// ---- Motion state (PIR); filled in by motion.cpp ------------------------------

bool motionActive();
uint32_t motionEvents();
void motionSetup();
void motionLoop();

// ---- HTTP --------------------------------------------------------------------

static void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(503, "text/plain", "capture failed");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

static void handleStatus() {
  char json[192];
  snprintf(json, sizeof(json),
           "{\"uptime_s\":%lu,\"rssi\":%d,\"free_heap\":%lu,\"free_psram\":%lu,"
           "\"motion\":%s,\"motion_events\":%lu}",
           (unsigned long)(millis() / 1000), WiFi.RSSI(), (unsigned long)ESP.getFreeHeap(),
           (unsigned long)ESP.getFreePsram(), motionActive() ? "true" : "false",
           (unsigned long)motionEvents());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[peekbox] camera booting");

  pinMode(FLASH_LED_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_LED_GPIO_NUM, LOW);

  initCamera();
  connectWifi();

  if (MDNS.begin(PEEKBOX_CAM_HOSTNAME)) {
    MDNS.addService("http", "tcp", PEEKBOX_HTTP_PORT);
    Serial.printf("[mdns] http://%s.local/\n", PEEKBOX_CAM_HOSTNAME);
  } else {
    Serial.println("[mdns] failed to start; use the IP address instead");
  }

  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([] { server.send(404, "text/plain", "try /capture or /status"); });
  server.begin();
  Serial.println("[http] server started");

  motionSetup();
}

void loop() {
  server.handleClient();
  motionLoop();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] lost connection, reconnecting");
    WiFi.reconnect();
    delay(1000);
  }
}
