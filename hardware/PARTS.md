# Parts list

Everything needed for one panel-mounted unit. Quantities are per device.

## Electronics

| Qty | Part | Notes |
|---:|---|---|
| 1 | **Waveshare ESP32-S3-Touch-LCD-1.9** | ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, 1.9" 170×320 ST7789. The bezel and the `ws_touch_lcd_19` firmware are built around this exact board — see [bezel/README.md](bezel/README.md) for the dimensions it relies on. |
| 1 | USB-C cable + 5 V supply | Powers the board and is the flashing/console port. Allow for it exiting sideways behind the panel. |

The headless `esp32s3_n16r8` build targets a **Sparkle IoT ESP32-S3 N16R8**
instead. Same firmware minus the LCD and the speed test; no bezel, no sheet,
no screws. Either board does the routing.

## Panel mounting

| Qty | Part | Spec |
|---:|---|---|
| 1 | Bezel, front | printed — `bezel/front.stl` |
| 1 | Bezel, back | printed — `bezel/back.stl` |
| 1 | Protective sheet | **1 mm** clear acrylic or polycarbonate, cut **56.1 × 32.4 mm**, corners eased to ~R1.25 |
| 4 | **M3 × 16 countersunk** machine screws | from the front, self-tapping into the printed back part |
| — | Panel | yours. Cutout **59.6 × 35.9 mm** R3.25, plus four **Ø3.4 mm** holes at ±34.8, ±22.95. Up to 5.83 mm thick — `bezel/panel_cutout.dxf` is the drawing at 1:1 |

M3 × 16 suits a 3 mm panel: 3 mm bezel + 3 mm panel + ~10 mm into the back
part. Add whatever your panel is over 3 mm to the screw length.

## Cooling

| Qty | Part | Spec |
|---:|---|---|
| 1 | Fan | **40 × 40 mm**, mounting holes Ø3 on 32 mm centres |
| 1 | Fan grille | printed — `fan-grille/fan_grille_40.stl` |
| 4 | Fan screws | M3, **2.5 mm longer** than the fan's own, to pass through the grille |

Fans usually ship with self-tapping screws sized for the fan alone; the grille
adds 2.5 mm, so the supplied ones are often just short.

> The fan is not wired to anything in this project and its voltage was never
> established — check it matches whatever supply you are putting it on.

## Printing

| Part | Orientation | Notes |
|---|---|---|
| Bezel front | face down | flat front on the bed |
| Bezel back | **lid down** | the 45° flare makes this support-free; flange-down would bridge the whole cavity |
| Fan grille | flat, either way up | |

No supports for any of them. 0.2 mm layers, 3 perimeters, 30 % infill. PETG or
ABS if it will sit in sunlight — PLA creeps under the screw preload once warm.
Roughly 25 g of filament for all three.

## Not needed

The board's own four **M2 mounting holes are unused** — the bezel clamps it
rather than screwing to it, so no M2 hardware or standoffs.

The board has an MX1.25 LiPo connector and a TF-card slot; neither is used,
and the back box encloses both. See [bezel/README.md](bezel/README.md) if you
want to cut access to them.
