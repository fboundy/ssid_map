# Penrith MRT Xebro — ESP32-S3 Wi-Fi selector / NAT router

Firmware for ESP32-S3 boards with 16 MB flash + 8 MB octal PSRAM. The device
always advertises one access point and can join one of a user-selected list of
upstream networks, routing AP clients through it (NAT).

Two build targets (`platformio.ini`):

| env | board | notes |
|---|---|---|
| `esp32s3_n16r8` | Sparkle IoT ESP32-S3 N16R8 | headless; console on UART0 (CH343 bridge) |
| `ws_touch_lcd_19` | Waveshare ESP32-S3-Touch-LCD-1.9 | adds a status readout on the 1.9" ST7789 LCD and an hourly Ookla speed test; native USB for flashing/console |

Native **ESP-IDF 5.4** build. The original Arduino-core version is kept in
[arduino/](arduino/) for reference; it behaves the same but cannot tune the
Wi-Fi/lwIP build options that matter for routing throughput.

## Behaviour

| Situation | What the device does |
|---|---|
| Boot, no SSIDs selected | Brings up AP **Penrith MRT Xebro**, scans every 30 s, waits (`waiting`). |
| Boot, SSIDs selected | 60 s countdown (`countdown`), then joins the **strongest visible** selected SSID and enables NAT (`routing`). |
| Uplink drops / join fails | Rescans, waits 5 s, joins the strongest selected SSID again (`retry`). |
| An enabled **over-ride SSID** is in range | NAT disabled, uplink dropped, stays AP-only (`override`) until it disappears. While routing it rescans every 30 s to check for this (only if an over-ride is enabled). |
| User presses *Disconnect* | Stays AP-only (`hold`) until *Connect now* or reboot. |

Everything (selected SSIDs + passwords, over-ride SSIDs + enabled flags, AP
subnet) is stored in NVS and survives power cycles and reflashes.

### Access point

* SSID `Penrith MRT Xebro`; WPA2 passphrase set in `include/config.h`
  (empty = open AP).
* Default subnet `192.168.137.0/24`: device is `.1`, DHCP leases `.10`–`.29`.
  Editable on the web page (private /24 only); applies after reboot.
* Clients get the ESP32 as gateway **and** DNS. A small DNS proxy on the device:
  * while not routing, answers every name with the AP IP so phones/laptops
    pop the captive-portal page straight onto the UI;
  * while routing, forwards queries to the upstream resolver.
* Regulatory domain `GB`, channels 1–13 (IDF's default world domain is 1–11).

### Web UI — `http://192.168.137.1/`

* **Status** — current mode, countdown, uplink IP/RSSI, *Connect now* /
  *Disconnect* / *Rescan*.
* **Available networks** — live scan sorted by signal. *Select* (with password)
  adds an SSID to the selected list; *+Over-ride* adds it as an over-ride.
* **Selected networks** — edit password, connect to a specific one, remove.
  Networks not in the scan (hidden SSIDs) can be added by name.
* **Over-ride SSIDs** — pick from the scan or type one; enable/disable each
  with the checkbox; remove.
* **Named host** — give one connected device (picked from the client list or
  typed as a MAC) a name. Only one at a time: setting a new one replaces it.
  Purely a label for the LCD — it does not affect routing.
* **Access point** — change the /24 network, reboot.

JSON API (form-encoded POSTs) lives under `/api/*`; see [src/web_ui.cpp](src/web_ui.cpp).

### LCD status page (`ws_touch_lcd_19` only)

Landscape 320×170, split in two. Refreshed once a second, redrawn only when
something changes. No touch interaction.

**Top half** — the uplink:

* Left: `Connected to:` above the SSID, set in the largest of four faces that
  fits the column.
* Right: the latest speed test as a green down-arrow and a blue up-arrow with
  the figures in Mbit/s.
* No uplink, NAT off, or a failed test: the right column becomes a red
  no-entry symbol over `No Internet`. While a test runs: `Speed test...`.
* If an enabled over-ride SSID is in range the whole half becomes
  `Over-ridden by` above that SSID, in red.

**Bottom half** — the named host: `<name> Connected` in green with its lease
IP underneath, `<name> Not Connected` in grey, or `No named host set`.

Driver: IDF `esp_lcd` ST7789 over SPI3 (SCLK 10, MOSI 13, CS 12, DC 11, RST 9,
backlight 14 on LEDC PWM — from Waveshare's demo code), 35-px panel offset,
vendor porch/VCOM/gamma tuning registers, framebuffer in PSRAM pushed through
DMA bounce buffers. Text uses a bitmap font generated from Consolas by GDI+
(`src/fonts.h`, regenerate with `pwsh tools/gen_font.ps1 -Out src/fonts.h`);
arrows and the no-entry symbol are drawn as primitives.
`tools/preview_lcd.py` renders the same layout to a PNG from `fonts.h`, so
spacing can be checked without flashing.

### Speed test (`ws_touch_lcd_19` only, `-DXEBRO_SPEEDTEST`)

An Ookla (speedtest.net) test runs 5 s after the uplink comes up and then
every hour while routing; the web page also has a *Speed test* button.
[src/speedtest.cpp](src/speedtest.cpp) fetches the nearest server from
`speedtest.net/api/js/servers` (HTTPS, IDF cert bundle), then streams a
plain-HTTP download (`/download?size=`, falling back to the classic
`random4000x4000.jpg`) and a sized upload (`/upload`, falling back to
`upload.php`) against the server's port 8080, 6 s each, single TCP stream.
Results go to the LCD, the web status card and the serial log. Timings and a
fixed server override are in `config.h` (`SPEEDTEST_*`). Each run moves
roughly 2 × 6 s × link rate of data (~35 MB at 20 Mbit/s).

## Throughput tuning (`sdkconfig.defaults`)

* NAT: `LWIP_IP_FORWARD`, `LWIP_IPV4_NAPT`, `LWIP_L2_TO_L3_COPY`.
* Wi-Fi and lwIP hot paths in IRAM; lwIP task pinned to core 1 (Wi-Fi on core 0).
* 24 static / 64 dynamic RX buffers, 64 dynamic TX buffers, 32-frame AMPDU
  block-ack windows, modem sleep off, IPv6 off, `-O2`, 240 MHz, octal PSRAM.

The radio is shared between the AP and the uplink on one channel, so the ceiling
is roughly half of what a single link would do; measured 17.5/19.4 Mbit/s
down/up against a 500/100 uplink.

## Building

PlatformIO with the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform 54.03.21, `framework = espidf` (IDF 5.4.2).

```
cp include/config.example.h include/config.h   # then set AP_PASS (gitignored)
pio run -e esp32s3_n16r8                     # Sparkle (headless)
pio run -e esp32s3_n16r8 -t upload           # flash via COM5 (see platformio.ini)
pio run -e ws_touch_lcd_19 -t upload --upload-port COM4   # Waveshare, native USB
pio device monitor                           # serial log at 115200
```

`include/config.h` holds the AP passphrase and is not committed; the committed
`config.example.h` has every other tunable at its default. The `arduino/`
project uses the same arrangement.

The Waveshare board enumerates as an Espressif "USB Serial Device" (VID 303A,
PID 1001); the COM number varies, so pass `--upload-port`. Logs go to both
UART0 and the USB port.

Notes for this Windows machine:

* Run PlatformIO from PowerShell/VS Code, not Git Bash — the platform's
  toolchain installer (`idf_tools.py`) refuses to run under MSYS.
* Windows long-path support is off, so PlatformIO could not unpack the
  `framework-espidf` / `framework-arduinoespressif32-libs` archives itself.
  They were extracted with a `\\?\`-path script into `~/.platformio/packages/`
  with a hand-written `.piopm`; enabling long paths (admin) avoids this.
* The bundled `esptool` needs `click < 8.2` in PlatformIO's venv
  (`~/.platformio/penv/Scripts/python -m pip install "click<8.2"`).
* pioarduino pins `tool-scons` 4.8.1 but PlatformIO Core 6.2 needs 4.11.1 and
  they overwrite each other every run. `~/.platformio/platforms/espressif32/platform.json`
  has `tool-scons.package-version` changed to `4.41101.0` (original saved as
  `platform.json.orig`).

## Layout

```
platformio.ini      envs for both boards; framework = espidf
sdkconfig.defaults  IDF options (NAT, throughput, memory)
partitions.csv      nvs + 4 MB factory app
include/config.h    tunables: AP name/password, timings (local; from config.example.h)
src/main.cpp        app_main, Wi-Fi bring-up, state machine, NAT enable/disable
src/store.*         NVS-backed lists (selected, over-rides, AP subnet)
src/dns_proxy.*     captive / forwarding DNS task on UDP 53
src/web_ui.*        esp_http_server routes + cJSON status
src/page.h          the single-page UI (inline HTML/CSS/JS)
src/display.*       ST7789 status page (compiled with -DXEBRO_DISPLAY_WS19)
src/fonts.h         generated 1-bit bitmap fonts (Consolas 16/20/24/36 px)
tools/gen_font.ps1  regenerates fonts.h; tools/preview_lcd.py previews the LCD
arduino/            previous Arduino-core implementation
```
