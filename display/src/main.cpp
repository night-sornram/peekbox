// PeekBox display firmware — GeekMagic SmallTV Ultra.
//
// Polls http://peekbox-cam.local/capture, decodes each JPEG straight to the panel,
// and draws an FPS + RSSI strip on top. Touch button: short press = pause/resume,
// long press = toggle the info strip.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing shared/secrets.h: copy shared/secrets.example.h to shared/secrets.h and fill it in."
#endif

#include "board_smalltv_ultra.h"

static constexpr int OVERLAY_H = 16;            // multiple of 8 so JPEG MCU rows align
static constexpr uint32_t LONG_PRESS_MS = 800;
static constexpr uint32_t DEBOUNCE_MS = 30;

static TFT_eSPI tft;

// The only frame buffer. Frames that don't fit are dropped, never reallocated.
static uint8_t jpegBuf[PEEKBOX_MAX_JPEG_BYTES];

static IPAddress camIp(PEEKBOX_CAM_IP);
static bool camFromMdns = false;
static uint32_t lastMdnsTry = 0;

static bool paused = false;
static bool showInfo = true;

static uint32_t lastPoll = 0;
static uint32_t fpsWindowStart = 0;
static uint16_t framesInWindow = 0;
static float fps = 0;
static uint8_t failStreak = 0;
static bool overlayDirty = true;

// ---- Drawing -----------------------------------------------------------------

static bool drawBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height()) return false;
  // Leave the info strip alone so it doesn't flicker; it is redrawn only when it changes.
  if (showInfo && y < OVERLAY_H) return true;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

static void drawOverlay() {
  if (!showInfo) return;
  tft.fillRect(0, 0, tft.width(), OVERLAY_H, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  char left[24];
  snprintf(left, sizeof(left), paused ? "PAUSED" : "%.1f fps", fps);
  tft.drawString(left, 4, 0);
  char right[16];
  snprintf(right, sizeof(right), "%d dBm", WiFi.RSSI());
  tft.setTextDatum(TR_DATUM);
  tft.drawString(right, tft.width() - 4, 0);
  overlayDirty = false;
}

static void showMessage(const char *line1, const char *line2 = nullptr) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(line1, tft.width() / 2, tft.height() / 2 - (line2 ? 10 : 0));
  if (line2) tft.drawString(line2, tft.width() / 2, tft.height() / 2 + 10);
  overlayDirty = true;
}

// ---- Camera discovery ----------------------------------------------------------

static void resolveCam() {
  lastMdnsTry = millis();
  int n = MDNS.queryService("http", "tcp");
  for (int i = 0; i < n; i++) {
    if (MDNS.hostname(i).startsWith(PEEKBOX_CAM_HOSTNAME)) {
      camIp = MDNS.IP(i);
      camFromMdns = true;
      Serial.printf("[mdns] %s.local -> %s\n", PEEKBOX_CAM_HOSTNAME, camIp.toString().c_str());
      return;
    }
  }
  camIp = IPAddress(PEEKBOX_CAM_IP);
  camFromMdns = false;
  Serial.printf("[mdns] %s.local not found, using fallback %s\n", PEEKBOX_CAM_HOSTNAME,
                camIp.toString().c_str());
}

// ---- Fetch + decode ------------------------------------------------------------

// Reads one /capture response into jpegBuf. Returns the JPEG length, or 0 on failure.
static size_t fetchFrame() {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(PEEKBOX_HTTP_TIMEOUT_MS);
  if (!http.begin(client, camIp.toString(), PEEKBOX_HTTP_PORT, "/capture")) return 0;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[http] GET /capture -> %d\n", code);
    http.end();
    return 0;
  }

  int len = http.getSize();  // -1 when the server doesn't send Content-Length
  if (len > (int)sizeof(jpegBuf)) {
    Serial.printf("[http] frame %d B > buffer %u B, dropped\n", len, (unsigned)sizeof(jpegBuf));
    http.end();
    return 0;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t got = 0;
  uint32_t deadline = millis() + PEEKBOX_HTTP_TIMEOUT_MS;
  while ((len < 0 || got < (size_t)len) && millis() < deadline) {
    size_t avail = stream->available();
    if (avail) {
      if (got + avail > sizeof(jpegBuf)) {
        Serial.println("[http] frame overflowed buffer, dropped");
        http.end();
        return 0;
      }
      got += stream->readBytes(jpegBuf + got, avail);
    } else if (!stream->connected()) {
      break;
    } else {
      delay(1);
    }
  }
  http.end();

  if (len >= 0 && got != (size_t)len) {
    Serial.printf("[http] short read %u/%d\n", (unsigned)got, len);
    return 0;
  }
  if (got < 4 || jpegBuf[0] != 0xFF || jpegBuf[1] != 0xD8) {
    Serial.println("[http] not a JPEG");
    return 0;
  }
  return got;
}

static void pollFrame() {
  size_t len = fetchFrame();
  if (!len) {
    if (++failStreak == 5) {
      showMessage("No signal from camera", camIp.toString().c_str());
    }
    // Fallback IP not answering (or the cam's DHCP lease moved): look it up again.
    if (failStreak >= 5 && millis() - lastMdnsTry > PEEKBOX_MDNS_RETRY_MS) resolveCam();
    return;
  }
  if (failStreak >= 5) overlayDirty = true;
  failStreak = 0;

  JRESULT r = TJpgDec.drawJpg(0, 0, jpegBuf, len);
  if (r != JDR_OK) {
    Serial.printf("[jpeg] decode error %d\n", r);
    return;
  }
  framesInWindow++;
}

// ---- Button ----------------------------------------------------------------------

#if BOARD_BUTTON_PIN >= 0
static void onShortPress() {
  paused = !paused;
  Serial.println(paused ? "[btn] paused" : "[btn] resumed");
  overlayDirty = true;
  if (paused && !showInfo) {
    // Make the paused state visible even with the strip hidden.
    tft.fillRect(0, 0, 70, OVERLAY_H, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("PAUSED", 4, 0, 2);
  }
}

static void onLongPress() {
  showInfo = !showInfo;
  Serial.println(showInfo ? "[btn] info on" : "[btn] info off");
  overlayDirty = true;
}

static void pollButton() {
  static bool stable = false, lastRaw = false, longFired = false;
  static uint32_t changedAt = 0, pressedAt = 0;

  bool raw = digitalRead(BOARD_BUTTON_PIN) == BOARD_BUTTON_ACTIVE;
  uint32_t now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = now;
  }
  if (now - changedAt < DEBOUNCE_MS) return;

  if (raw != stable) {
    stable = raw;
    if (stable) {
      pressedAt = now;
      longFired = false;
    } else if (!longFired) {
      onShortPress();
    }
  } else if (stable && !longFired && now - pressedAt >= LONG_PRESS_MS) {
    longFired = true;  // fire while still held, so the user gets feedback
    onLongPress();
  }
}
#endif

#ifdef PEEKBOX_BUTTON_PROBE
static const uint8_t probePins[] = BOARD_PROBE_PINS;

static void probeButton() {
  static int last[sizeof(probePins)];
  static bool init = false;
  for (size_t i = 0; i < sizeof(probePins); i++) {
    int v = digitalRead(probePins[i]);
    if (!init || v != last[i]) {
      Serial.printf("[probe] GPIO%u = %d\n", probePins[i], v);
      last[i] = v;
    }
  }
  init = true;
}
#endif

// ---- Setup / loop --------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.println("\n[peekbox] display booting");

  pinMode(BOARD_BL_PIN, OUTPUT);
  digitalWrite(BOARD_BL_PIN, BOARD_BL_ON);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);  // TJpg_Decoder emits little-endian RGB565
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(drawBlock);

#if BOARD_BUTTON_PIN >= 0
  pinMode(BOARD_BUTTON_PIN, INPUT);
#endif
#ifdef PEEKBOX_BUTTON_PROBE
  for (uint8_t p : probePins) pinMode(p, INPUT);
  Serial.println("[probe] touch the button and watch which GPIO changes");
#endif

  showMessage("PeekBox", "connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(PEEKBOX_DISP_HOSTNAME);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(PEEKBOX_WIFI_SSID, PEEKBOX_WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 30000) {
      Serial.println("[wifi] timed out, rebooting");
      ESP.restart();
    }
    delay(250);
  }
  Serial.printf("[wifi] connected, IP %s, RSSI %d dBm, free heap %u\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), ESP.getFreeHeap());

  if (!MDNS.begin(PEEKBOX_DISP_HOSTNAME)) Serial.println("[mdns] failed to start");
  showMessage("Looking for camera...", PEEKBOX_CAM_HOSTNAME ".local");
  resolveCam();
  tft.fillScreen(TFT_BLACK);
  fpsWindowStart = millis();
}

void loop() {
  MDNS.update();
#if BOARD_BUTTON_PIN >= 0
  pollButton();
#endif
#ifdef PEEKBOX_BUTTON_PROBE
  probeButton();
#endif

  uint32_t now = millis();
  if (!paused && now - lastPoll >= PEEKBOX_POLL_INTERVAL_MS) {
    lastPoll = now;
    pollFrame();
  }

  if (now - fpsWindowStart >= 1000) {
    fps = framesInWindow * 1000.0f / (now - fpsWindowStart);
    framesInWindow = 0;
    fpsWindowStart = now;
    overlayDirty = true;
  }
  if (overlayDirty && failStreak < 5) drawOverlay();
  yield();
}
