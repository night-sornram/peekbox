# PeekBox

A tiny DIY CCTV. An **AI-Thinker ESP32-CAM** serves 240×240 JPEG frames over WiFi, and a
**GeekMagic SmallTV Ultra** polls them and shows them live. When the optional PIR sensor
sees motion, the camera sends a snapshot to a Telegram bot.

```
 ┌──────────────┐  GET /capture (JPEG)   ┌────────────────────┐
 │  ESP32-CAM   │ ◄───────────────────── │  SmallTV Ultra     │
 │  + HC-SR501  │                        │  ESP8266 + ST7789  │
 └──────┬───────┘                        └────────────────────┘
        │ motion → sendPhoto (HTTPS)
        ▼
   Telegram bot
```

| Path | What |
|---|---|
| `cam/` | ESP32-CAM firmware: camera, HTTP server, mDNS, PIR, Telegram |
| `display/` | SmallTV Ultra firmware: TFT_eSPI + TJpg_Decoder, polling loop, button |
| `shared/config.h` | Frame size, port, poll interval, camera IP fallback, PIR settings |
| `shared/secrets.example.h` | Template for your WiFi and Telegram credentials |

### Camera HTTP API

- `GET http://peekbox-cam.local/capture` returns one 240×240 JPEG (quality 12)
- `GET http://peekbox-cam.local/status` returns JSON, for example
  `{"uptime_s":42,"rssi":-58,"free_heap":180000,"free_psram":4000000,"motion":false,"motion_events":3}`

## Hardware

- AI-Thinker ESP32-CAM (OV2640, 4 MB PSRAM), plus a USB-to-serial adapter or an ESP32-CAM-MB board to flash it
- GeekMagic SmallTV Ultra (ESP8266, 240×240 ST7789 IPS, USB-C with CH340)
- Optional: HC-SR501 PIR sensor
- A **5 V supply that can deliver at least 1 A** for the camera

## Wiring

### Power the ESP32-CAM

Feed **5 V (≥1 A)** into the `5V` pin and `GND`. The camera and WiFi draw current in spikes.
A weak supply, a thin cable, or the 3.3 V pin causes brownout resets
(`Brownout detector was triggered`) and camera init failures. If you see those, fix the power first.

### PIR (HC-SR501) → ESP32-CAM

| HC-SR501 | ESP32-CAM | Notes |
|---|---|---|
| VCC | 5V | The sensor needs 4.5–20 V |
| GND | GND | |
| OUT | GPIO13 | Output is 3.3 V, safe for the ESP32 |

- GPIO13 is also an SD-card data line. PeekBox doesn't use the SD card, so leave the slot empty.
- On the sensor, set the jumper to **H** (retrigger). Turn the *time* pot fully
  counter-clockwise (shortest hold, ~3 s) and set *sensitivity* to taste.
- The sensor needs about 1 minute to settle after power-up. Motion is ignored during that time.
- No PIR? Set `PEEKBOX_PIR_ENABLED` to `0` in `shared/config.h`.

### SmallTV Ultra

Nothing to wire. It's powered and flashed over its USB-C port.

## Setup

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
   (`pipx install platformio` or `uv tool install platformio`).
2. Create your secrets file and fill it in:
   ```bash
   cp shared/secrets.example.h shared/secrets.h
   ```
   `shared/secrets.h` is gitignored. Never commit it.
   - For Telegram, create a bot with [@BotFather](https://t.me/BotFather), send your bot a message,
     then open `https://api.telegram.org/bot<TOKEN>/getUpdates` and copy `chat.id`.
     Leave the token empty to disable Telegram.
3. Camera address: the display looks up `peekbox-cam.local` over mDNS. If that fails, it
   uses `PEEKBOX_CAM_IP` from `shared/config.h`. Pick an address on your LAN, then either
   reserve it for the cam in your router or set `PEEKBOX_CAM_STATIC_IP 1` (also set the
   gateway, subnet and DNS there).

Find serial ports with `pio device list`. On macOS they look like
`/dev/cu.usbserial-XXXX` or `/dev/cu.wchusbserial-XXXX`.

## ⚠️ First: back up the SmallTV stock firmware

Before flashing the SmallTV for the first time, read out its whole 4 MB flash so you can
always go back:

```bash
esptool.py --port <port> read_flash 0 0x400000 smalltv-backup.bin
```

If you don't have `esptool.py` on PATH, use the copy PlatformIO ships:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --port <port> read_flash 0 0x400000 smalltv-backup.bin
```

The file should be exactly 4,194,304 bytes. Keep it somewhere safe; `*.bin` is gitignored.
To restore later: `esptool.py --port <port> write_flash 0 smalltv-backup.bin`.

## Flashing

### Camera (ESP32-CAM)

1. Put it in download mode: connect **GPIO0 to GND**, then reset or power-cycle.
   (An ESP32-CAM-MB board does this automatically.)
2. Build and upload:
   ```bash
   cd cam
   pio run -t upload --upload-port <port>
   ```
3. Remove the GPIO0–GND link and press reset. Then watch the log:
   ```bash
   pio device monitor --port <port>
   ```
   You should see `camera ready`, the IP address, and `http://peekbox-cam.local/`.
   Open `http://peekbox-cam.local/capture` in a browser to check it.

If the camera fails to start, the firmware retries 5 times, power-cycling the sensor between
tries, and logs the error code. Then it reboots. Check the ribbon cable and the power supply.

### Display (SmallTV Ultra)

```bash
cd display
pio run -t upload --upload-port <port>
pio device monitor --port <port>
```

If esptool can't connect, the board may need GPIO0 held to GND at power-up to enter flash
mode (as noted on the ESPHome device page).

### Display controls

| Button | Action |
|---|---|
| Short press | Pause / resume |
| Long press (≥0.8 s) | Show / hide the FPS + RSSI strip |

### Finding the touch button pin

The SmallTV Ultra's touch-button GPIO isn't documented in any source I could find, so the
button is **disabled** by default (`BOARD_BUTTON_PIN -1`). To find it:

```bash
cd display
pio run -e smalltv_ultra_probe -t upload --upload-port <port>
pio device monitor --port <port>
```

Touch the button and watch for a `[probe] GPIOn = …` line that changes (it watches GPIO4,
12 and 16). Then set that pin in `display/platformio.ini` under `build_flags`, for example
`-D BOARD_BUTTON_PIN=4`. If the value drops to 0 when touched, also add
`-D BOARD_BUTTON_ACTIVE=LOW`.

## SmallTV Ultra pin map

Set in `display/platformio.ini`. Sources and open questions are in
`display/src/board_smalltv_ultra.h`.

| Signal | GPIO |
|---|---|
| SCLK | 14 |
| MOSI | 13 |
| DC | 0 |
| RST | 2 |
| CS | 15 (ESPHome leaves CS out; harmless if it's tied low) |
| Backlight | 5, active-low |

The panel runs in SPI mode 3 with colour inversion on. If red and blue look swapped, change
`TFT_RGB_ORDER` to `TFT_BGR` (the two sources disagree on this).

## License

MIT, see [LICENSE](LICENSE).
