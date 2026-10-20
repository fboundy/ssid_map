# ESP32-S3 Wi-Fi uplink selector and NAT router

A small router for a vehicle or a go-bag. It watches for a list of Wi-Fi
networks you trust — a satellite terminal, a phone hotspot, a base station —
joins whichever of them is strongest, and shares that connection over its own
access point.

The problem it solves is a device that needs internet but can only join a
Wi-Fi network, and can't be told which one. It joins *this* router's access
point and stays there. The router takes care of finding an uplink, switching
between them, and reconnecting when one drops.

On a board with a screen it also shows, at a glance, which uplink it is using,
how fast that uplink actually is, and whether the device you care about has
connected.

```
┌─────────────────────────┐      ┌──────────────┐      ┌─────────────┐
│  uplink networks        │      │              │      │             │
│  (strongest is chosen)  │─ ─ ─▶│  this router │◀─ ─ ─│ your device │
│  sat terminal, hotspot… │ WiFi │   NAT + DHCP │ WiFi │             │
└─────────────────────────┘      └──────────────┘      └─────────────┘
```

**Jump to:** [Quick start](#quick-start) · [Configuration](#configuration) ·
[Web interface](#web-interface) · [Display](#the-display) ·
[Performance](#performance) · [Hardware](#hardware)

---

## What it does

From cold, it brings up its access point immediately, so the web interface is
reachable within a second or so — you never have to wait for it to sort its
uplink out before you can talk to it.

At the same time it starts scanning. **As soon as the first scan completes, it
joins the strongest network on its saved list and starts routing** — about
7 seconds from power-on in practice, of which 3 are the scan itself. There is
no countdown or grace period; the scan has to finish before "strongest" means
anything, and that is the only delay.

If the uplink drops or a join fails, it waits five seconds, scans again and
picks the strongest saved network afresh — which may be a different one. It
keeps doing that indefinitely.

If no networks have been saved yet, it simply waits, scanning every 30 seconds
so the web interface shows a current list. Saving your first network starts the
connection immediately.

### States

The web interface and the logs name the current state. You will mostly see
`routing`.

| State | Meaning |
|---|---|
| `waiting` | No networks saved yet. Scanning; nothing to connect to. |
| `retry` | Looking for a saved network, or waiting out the five seconds after a failure. |
| `connecting` | Joining a network. |
| `routing` | Connected, NAT on, clients have internet. The normal state. |
| `hold` | You pressed **Disconnect**. Stays access-point-only until you reconnect or reboot. |
| `override` | An override network is in range — see below. |

### Override networks

You can mark a network as an **override**. While an override network is
visible, the router drops its uplink and refuses to route, staying
access-point-only until that network goes away again.

This is for standing the router down when something else should be handling
the traffic — when the vehicle is parked somewhere with its own connection,
for instance, and you would rather not route over a metered link. Overrides
are a list and each can be enabled or disabled independently.

While routing, the radio has to leave its channel to scan, which interrupts
traffic for about a second. So it only rescans for overrides if at least one
is actually enabled — if you use no overrides, it never does this.

Everything — saved networks and their passwords, overrides, the access point's
subnet, the named device — is stored in NVS and survives power cycles and
reflashing.

---

## Quick start

You need [PlatformIO](https://platformio.org/). The build uses the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform
54.03.21 with `framework = espidf` (ESP-IDF 5.4.2).

**`include/config.h` is not in the repository** — it holds the access point's
passphrase. Copy the template before your first build:

```bash
cp include/config.example.h include/config.h
```

Open it and set `AP_PASS`. Everything else is already at a working value, so a
fresh clone builds as soon as you have made that copy.

Then build and flash whichever board you have:

```bash
# Board with the 1.9" screen (Waveshare ESP32-S3-Touch-LCD-1.9)
pio run -e ws_touch_lcd_19 -t upload --upload-port COM4

# Headless board (Sparkle IoT ESP32-S3 N16R8)
pio run -e esp32s3_n16r8 -t upload

# Serial log, 115200 baud
pio device monitor
```

The Waveshare board has native USB and appears as an Espressif "USB Serial
Device" (VID `303A`, PID `1001`). Its COM port number changes between
machines, so pass `--upload-port`. Logs go to both UART0 and the USB port.

### Supported boards

Both are ESP32-S3 with 16 MB flash and 8 MB octal PSRAM.

| Environment | Board | What you get |
|---|---|---|
| `ws_touch_lcd_19` | Waveshare ESP32-S3-Touch-LCD-1.9 | Everything, including the status display and the hourly speed test. Native USB for flashing and logs. |
| `esp32s3_n16r8` | Sparkle IoT ESP32-S3 N16R8 | Routing and the web interface, no display or speed test. Console on UART0 via a CH343 bridge. |

This is a native ESP-IDF build rather than Arduino, because the Wi-Fi and lwIP
settings that matter for routing throughput can only be reached from a real
IDF build — see [Performance](#performance).

---

## Configuration

There are two levels: things fixed when you build, and things you change from
the web interface while it is running.

### At build time — `include/config.h`

Copied from `config.example.h`, and not committed because it holds the
passphrase. The settings you are most likely to touch:

| Setting | Default | What it does |
|---|---|---|
| `AP_SSID` | `Penrith MRT Xebro` | **Names the whole device.** |
| `AP_PASS` | — | WPA2 passphrase. Leave it empty for an open access point. |
| `AP_DEFAULT_NETWORK` | `192.168.137.0` | Access point subnet, before any change made from the web interface. |
| `WIFI_COUNTRY` | `GB` | Regulatory domain. |
| `LCD_ROTATE_180` | `1` | Screen orientation — see [Display](#the-display). |
| `SPEEDTEST_*` | hourly, 6 s | Speed test timing — see [Speed test](#speed-test). |

`AP_SSID` is worth singling out. It does not only name the access point: the
web page's title and heading, the splash on the screen and the boot banner all
take their text from it, so changing that one line renames the device
everywhere. It is checked at compile time against Wi-Fi's 32-character limit,
as is `AP_PASS` against WPA2's 8-to-63.

### At run time — the web interface

Saved networks, overrides, the named device and the access point's subnet are
all set from the web page and kept in NVS. Nothing needs reflashing.

---

## The access point

* **Name and passphrase** come from `AP_SSID` and `AP_PASS`. An empty
  passphrase makes it an open network.
* **Subnet** defaults to `192.168.137.0/24` — the router is `.1` and hands out
  leases from `.10` to `.29`. You can change the subnet from the web page
  (any private /24); it takes effect after a reboot.
* **Channel 1**, up to 8 clients, regulatory domain `GB` so channels 1–13 are
  available. ESP-IDF's default world domain only allows 1–11.

Clients get the router as both their gateway and their DNS server, which lets
it do something useful with name lookups:

* **While it has no uplink**, it answers every lookup with its own address.
  Phones and laptops notice this and pop up their "sign in to network" page,
  which lands straight on the web interface — so a device that has just joined
  shows you the settings page without anyone having to type an address.
* **While routing**, it forwards lookups to the upstream resolver as normal.

---

## Web interface

At `http://192.168.137.1/`, or just join the access point and accept the
sign-in prompt.

| Section | What it is for |
|---|---|
| **Status** | Current state, uplink address and signal, the last speed test. Buttons for **Connect now**, **Disconnect** and **Rescan**. |
| **Available networks** | Live scan, strongest first. **Select** adds a network to the saved list (with its password); **+Override** marks it as an override. |
| **Selected networks** | The saved list. Change a password, force a connection to a particular one, or remove it. Hidden networks can be added by typing the name. |
| **Override networks** | Add by picking from the scan or typing a name; tick to enable, untick to disable, or remove. |
| **Named device** | Give one connected device a name, by picking it from the client list or typing its MAC. One at a time — setting a new one replaces it. This is only a label for the screen; it has no effect on routing. |
| **Access point** | Change the subnet, or reboot. |

Everything the page shows comes from a small JSON API under `/api/*`, which
you are welcome to drive yourself — the routes are at the bottom of
[`src/web_ui.cpp`](src/web_ui.cpp).

---

## The display

*Boards with a screen only.* Landscape, 320 × 170, updated once a second and
redrawn only when something actually changes. There is no touch interaction —
the screen is purely a readout.

It is split into two halves with the same layout: a small caption and a large
name on the left, a status block on the right.

**Top half — the internet uplink.** `Internet Connection:` above the network
name, with the latest speed test on the right: a green down-arrow and a blue
up-arrow with their figures under a shared `Mb/s` label.

* No uplink, NAT off, or a failed test shows a red no-entry symbol over
  `No Internet` instead of the figures.
* While a test is running, `Speed test...`.
* If an override network is in range, the whole half is replaced by
  `Over-ridden by:` and that network's name, in red.

**Bottom half — the named device.** `Device Connection:` above the name you
gave it, with its DHCP lease address underneath while it is connected, and on
the right either a green tick over `Connected` or a red no-entry symbol over
`Not Connected`. If you have not named a device, it reads `None set`.

Network names are set in the largest of four font sizes that fits the column,
so short names are large and long ones step down gracefully rather than
falling off a cliff. A 16-character name still gets the second-largest face.

### Orientation

`LCD_ROTATE_180` in `config.h` turns the image over, so the board can be
mounted with its USB-C socket on either side. It is set to `1` by default,
putting the socket on the left as you look at the screen.

**If you are using the printed bezel, this has to agree with `usb_left` in
[`hardware/bezel/bezel.scad`](hardware/bezel/bezel.scad).** The bezel is
handed — its window is deliberately off-centre and its clamp ribs avoid the
buttons at one end only — so the two settings must describe the same
orientation.

### How it is drawn

IDF's `esp_lcd` ST7789 driver over SPI3, with the vendor's porch, VCOM and
gamma registers (the panel shows nothing without them). The framebuffer lives
in PSRAM and is pushed out through DMA bounce buffers. Text uses a bitmap font
generated from Consolas; arrows, the tick and the no-entry symbol are drawn as
primitives.

```bash
pwsh tools/gen_font.ps1 -Out src/fonts.h   # regenerate the fonts
python tools/preview_lcd.py preview.png    # render the layout without flashing
```

`preview_lcd.py` reads the same `fonts.h` and mirrors the drawing code, so you
can check spacing and font choices for every screen state as an image rather
than by reflashing and squinting.

---

## Speed test

*Boards with a screen only.* An [Ookla](https://www.speedtest.net/) test runs
five seconds after the uplink comes up and then hourly while routing, so the
figure on the screen reflects the link you are actually on. There is also a
**Speed test** button on the web page.

It picks the nearest server from Speedtest's own server list over HTTPS, then
runs a six-second download and a sized upload straight against that server,
single TCP stream. Results go to the screen, the web page and the serial log.

Timings and a fixed-server override are the `SPEEDTEST_*` settings in
`config.h`. Be aware of the cost: each run moves roughly
2 × 6 s × your link rate — about **35 MB per run at 20 Mbit/s**, or 35 MB an
hour at the default interval. On a metered link, lengthen
`SPEEDTEST_INTERVAL_MS` or shorten `SPEEDTEST_SECONDS`.

---

## Performance

**Measured: 17.5 Mbit/s down, 19.4 Mbit/s up**, routing through to a 500/100
uplink.

That is the hardware ceiling rather than a tuning problem. One radio is shared
between the access point and the uplink on a single channel, so it time-slices
between them, and every packet crosses the NAT table on a 240 MHz core. Expect
roughly half of what a single link would manage.

If your uplink is slower than about 17 Mbit/s, the router will not be your
bottleneck.

Everything in `sdkconfig.defaults` that matters for this:

* **NAT** — `LWIP_IP_FORWARD`, `LWIP_IPV4_NAPT`, `LWIP_L2_TO_L3_COPY`.
* **Hot paths in IRAM** — Wi-Fi and lwIP, so flash access cannot stall them.
* **Cores split** — lwIP pinned to core 1, Wi-Fi driver on core 0.
* **Buffers** — 24 static and 64 dynamic RX, 64 dynamic TX, 32-frame AMPDU
  block-ack windows.
* **Everything else out of the way** — modem sleep off, IPv6 off, `-O2`,
  240 MHz, octal PSRAM.

One caveat worth knowing: under a saturating test, loaded latency rises to
around 150–200 ms as those deep buffers fill. That only happens when the
router itself is the bottleneck. On a slower uplink the queues stay shallow
and latency stays low — but if it matters to you, halving the buffer counts
trades a little throughput for a lot less queueing delay.

---

## Hardware

[**hardware/PARTS.md**](hardware/PARTS.md) is the full parts list for one
unit, with suppliers and part numbers.

[**hardware/bezel/**](hardware/bezel/) is a parametric two-part 3D-printed
bezel that mounts the Waveshare board into a flat panel behind a 1 mm
protective sheet. It clamps the board rather than screwing to it, and closes
into a box behind the panel that covers the pin headers. Includes STLs, a
panel-cutout DXF and the board dimensions it depends on.

[**hardware/fan-grille/**](hardware/fan-grille/) is a printable finger guard
for a 40 mm fan, parametric for other sizes.

Both are OpenSCAD, so the dimensions are readable and editable rather than
baked into a mesh.

---

## Project layout

```
platformio.ini        build environments for both boards
sdkconfig.defaults    ESP-IDF options: NAT, throughput, memory
partitions.csv        NVS + 4 MB factory app
include/config.h      your settings (not committed; copy config.example.h)

src/main.cpp          startup, Wi-Fi, the state machine, NAT on and off
src/store.*           NVS-backed lists: saved networks, overrides, subnet
src/dns_proxy.*       captive-portal and forwarding DNS, on UDP 53
src/web_ui.*          HTTP routes and the JSON status
src/page.h            the web page, inline HTML/CSS/JS
src/display.*         the status screen
src/speedtest.*       the Ookla client
src/fonts.h           generated bitmap fonts

tools/gen_font.ps1    regenerates fonts.h
tools/preview_lcd.py  renders the screen layout to a PNG

hardware/             bezel, fan grille, parts list
```

---

## Building on Windows

Not required reading — only relevant if the build misbehaves on Windows. These
are all environment problems rather than anything in this repository.

* **Run PlatformIO from PowerShell or VS Code, not Git Bash.** The platform's
  toolchain installer (`idf_tools.py`) refuses to run under MSYS.
* **Long path support.** With it disabled, PlatformIO cannot unpack the
  `framework-espidf` archive — the ESP-IDF tree has paths over 260 characters.
  Enabling long paths (needs administrator) is the clean fix; otherwise
  extract the archive manually into `~/.platformio/packages/` and write a
  `.piopm` marker by hand.
* **esptool needs `click < 8.2`.** Newer click changes an API esptool uses:
  `~/.platformio/penv/Scripts/python -m pip install "click<8.2"`.
* **SCons version clash.** pioarduino pins `tool-scons` 4.8.1 while PlatformIO
  Core 6.2 wants 4.11.1, and they overwrite each other on every run, leaving a
  broken tree (`No module named SCons.Tool.FortranCommon`). Set
  `tool-scons.package-version` to `4.41101.0` in
  `~/.platformio/platforms/espressif32/platform.json`.
