# Penrith MRT Xebro — ESP32-S3 Wi-Fi selector / NAT router

Firmware for a Sparkle IoT ESP32-S3 N16R8 (ESP32-S3-WROOM-1, 16 MB flash, 8 MB
PSRAM). The device always advertises one access point and can join one of a
user-selected list of upstream networks, routing AP clients through it (NAT).

## Behaviour

| Situation | What the device does |
|---|---|
| Boot, no SSIDs selected | Brings up AP **Penrith MRT Xebro**, scans every 10 s, waits (`waiting`). |
| Boot, SSIDs selected | 60 s countdown (`countdown`), then joins the **strongest visible** selected SSID and enables NAT (`routing`). |
| Uplink drops / join fails | Rescans, waits 5 s, joins the strongest selected SSID again (`retry`). |
| An enabled **over-ride SSID** is in range | NAT disabled, uplink dropped, stays AP-only (`override`) until it disappears. While routing it rescans every 30 s to check for this. |
| User presses *Disconnect* | Stays AP-only (`hold`) until *Connect now* or reboot. |

Everything (selected SSIDs + passwords, over-ride SSIDs + enabled flags, AP
subnet) is stored in NVS and survives power cycles.

### Access point

* SSID `Penrith MRT Xebro`, WPA2 passphrase set in `include/config.h` (copy from `config.example.h`; change in
  `include/config.h`, set empty for an open AP).
* Default subnet `192.168.137.0/24`: device is `.1`, DHCP leases from `.10`.
  Editable on the web page (private /24 only); applies after reboot.
* Clients get the ESP32 as gateway **and** DNS. A tiny DNS proxy on the device:
  * while not routing, answers every name with the AP IP so phones/laptops
    pop the captive-portal page straight onto the UI;
  * while routing, forwards queries to the upstream resolver.

### Web UI — `http://192.168.137.1/`

* **Status** — current mode, countdown, uplink IP/RSSI, *Connect now* /
  *Disconnect* / *Rescan*.
* **Available networks** — live scan sorted by signal. *Select* (with password)
  adds an SSID to the selected list; *+Over-ride* adds it as an over-ride.
* **Selected networks** — edit password, connect to a specific one, remove.
  Networks not in the scan (hidden SSIDs) can be added by name.
* **Over-ride SSIDs** — pick from the scan or type one; enable/disable each
  with the checkbox; remove.
* **Access point** — change the /24 network, reboot.

JSON API (form-encoded POSTs) lives under `/api/*`; see `src/web_ui.cpp`.

## Building

PlatformIO with the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform 54.03.21 (Arduino core 3.2.1 / ESP-IDF 5.4). NAT uses
`WiFi.AP.enableNAPT()`, which needs Arduino core 3.x.

```
pio run              # build
pio run -t upload    # flash (COM5, 921600 baud — see platformio.ini)
pio device monitor   # serial log at 115200
```

Notes for this Windows machine:

* Run PlatformIO from PowerShell/VS Code, not Git Bash — the platform's
  toolchain installer (`idf_tools.py`) refuses to run under MSYS.
* Windows long-path support is off, so PlatformIO could not unpack the
  `framework-arduinoespressif32-libs` archive itself. It was extracted manually
  into `~/.platformio/packages/framework-arduinoespressif32-libs` with a
  `.piopm` marker; enabling long paths (admin) avoids this in future.
* The bundled `esptool` needs `click < 8.2` in PlatformIO's venv
  (`~/.platformio/penv/Scripts/python -m pip install "click<8.2"`).

## Layout

```
include/config.h   tunables: AP name/password, timings, defaults
src/main.cpp       state machine, Wi-Fi events, NAT enable/disable
src/store.*        NVS-backed lists (selected, over-rides, AP subnet)
src/dns_proxy.*    captive / forwarding DNS on UDP 53
src/web_ui.*       HTTP routes + JSON status
src/page.h         the single-page UI (inline HTML/CSS/JS)
```

## Limits

* Arduino's soft-AP DHCP hands out 10 leases; `AP_MAX_CLIENTS` is 8.
* Routing throughput is what the ESP32-S3 radio can do in AP+STA on one
  channel — fine for a few devices, not a whole office.
* Scanning while routing briefly pauses traffic (~1 s); it only happens when at
  least one over-ride SSID is enabled, or when you press *Rescan*.
