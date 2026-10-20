# Parts list — the enclosure

One vehicle-mounted unit. The ESP32 in this repo is the **router**: it joins
the vehicle's satellite terminal Wi-Fi (`Vehicle Mobile 1` / `Vehicle Mobile 2`) and
hands a private AP to the [Xebro](https://www.xebro.co.uk/home) 
gateway, which bridges it to the vehicle radio. The LCD shows which
satellite terminal it is on, how fast, and whether the Xebro has joined.

the in-vehicle procedure.

Prices are as listed in October 2026 and include VAT unless noted.

## Electronics

| Qty | Part | Supplier | Price |
|---:|---|---|---|
| 1 | **Waveshare ESP32-S3 1.9" Display Dev Board with IMU — With Touch** — WAV-30939 | [The Pi Hut](https://thepihut.com/products/esp32-s3-1-9-display-dev-board-with-imu-touch-option?variant=55135475106177) | £19.20 |
| 1 | **Xebro** radio gateway | [xebro.co.uk](https://www.xebro.co.uk/home) | — |

> **It must be the With-Touch variant**, even though the firmware never uses
> the touch controller. Waveshare sell two mechanically different boards under
> one name, and the bezel fits only this one. The cheaper No-Touch board
> (WAV-30995, £15.40) will not fit it.

| | **With Touch** — what the bezel fits | No Touch |
|---|---|---|
| PCB | 52.60 × 29.00 | 51.00 × 26.00 |
| Front face | cover glass **55.10 × 31.40**, overhanging the PCB by 1.2 mm all round | LCD module 49.72 × 25.80, sitting *inside* the PCB outline |
| Active area | 43.12 × 23.10 | 42.72 × 22.70 |
| Depth | 11.80 to the rear components | 15.90 |
| M2 pattern | 40.75 × 25.00 | different again |

The bezel works by trapping that overhanging cover glass in a 55.6 × 31.9
pocket and pressing it against the sheet. The No-Touch board has no such
glass — its largest feature is the bare PCB, which would sit in that pocket
with 2 mm of slop per side and nothing to seal against. The window position
and the clamp ribs are wrong for it too.

The With-Touch board is also the only one with the **IPEX antenna pad**, if an
external aerial on the board itself is ever wanted (the 0 Ω link has to be
moved to use it). Both figures above are from Waveshare's own drawings, in the
`-DAD.rar` downloads linked from the [wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.9).

## Power

Everything runs from the vehicle's 12 V. The fan takes 12 V directly; the
ESP32 takes 5 V from the converter.

| Qty | Part | Supplier | Price |
|---:|---|---|---|
| 1 | **ProCar heavy-duty fused universal plug**, 12/24 V 16 A, 16 A fuse fitted — P04195 | [12 Volt Planet](https://www.12voltplanet.co.uk/procar-heavy-duty-fused-universal-plug-1224v-16a-max-67722010.html) | £5.94 |
| 1 | **Panel-mount cigar-lighter socket**, 12/24 V 20 A — P01722 | [12 Volt Planet](https://www.12voltplanet.co.uk/panel-mount-cigar-lighter-power-socket-12v24v-20a-max.html) | £4.58 |
| 1 | **12 V → 5 V 3 A USB-C buck converter**, epoxy-potted, waterproof | [Amazon B0GJS9V3S4](https://www.amazon.co.uk/dp/B0GJS9V3S4) | — |

The socket needs a **29 mm** panel cutout and about **55 mm** of clearance
behind it — check that against wherever it goes in the case.

## Antenna feed

| Qty | Part | Supplier | Price |
|---:|---|---|---|
| 1 | **N plug → RP-TNC plug**, 200 series, 350 mm — N30T60-200-350 | [RF Shop](https://rfshop.co.uk/product/n-plug-to-rp-tnc-plug-200-series-cable-350mm-n30t60-200-350/) | £15.56 ex VAT |
| 1 | **N plug → N plug**, 400 series, 500 mm — N30N30-400-500 | [RF Shop](https://rfshop.co.uk/product/coaxial-cable-assembly-n-plug-to-n-plug-400-series-0-5m-n30n30-400-500/) | £20.97 ex VAT |

Feeds the vehicle antenna to the **Simoco SDM730**, whose antenna port is a
TNC-threaded socket.

> **Check the polarity once.** The pigtail is **RP-TNC**, not TNC — reverse
> polarity swaps the centre contacts while keeping the same 7/16-28 thread,
> so a wrong one screws on just as tightly as a right one and the only
> symptom is no centre-conductor contact.
>
> Look into the centre of the radio's socket. A **pin** means it is an RP-TNC
> jack and this cable is correct. A **hole** means it is a conventional TNC
> jack, the cable's socket has nothing to meet, and the part needed instead is
> [N30T30-200-500](https://rfshop.co.uk/product/n-plug-male-pin-to-tnc-plug-male-pin-200-series-cable-0-5m-n30t30-200-500/)
> — N plug to TNC plug, 200 series, 0.5 m, £14.02 ex VAT.

## Enclosure

| Qty | Part | Supplier | Price |
|---:|---|---|---|
| 1 | **TANGZON waterproof hard case**, IP66, PP shell, foam insert, pressure valve | [Amazon B0GFMYP6DM](https://www.amazon.co.uk/dp/B0GFMYP6DM) | — |

The case takes the bezel in its lid. Internal dimensions were not on the
listing — confirm them before cutting.

## Panel mounting — the display

| Qty | Part | Spec |
|---:|---|---|
| 1 | Bezel, front | printed — [`bezel/front.stl`](bezel/front.stl) |
| 1 | Bezel, back | printed — [`bezel/back.stl`](bezel/back.stl) |
| 1 | Protective sheet | **1 mm** clear acrylic or polycarbonate, cut **56.1 × 32.4 mm**, corners eased to ~R1.25 |
| 4 | **M3 × 16 countersunk** machine screws | from the front, self-tapping into the printed back part |

Cutout **59.6 × 35.9 mm** R3.25 plus four **Ø3.4 mm** holes at ±34.8, ±22.95.
[`bezel/panel_cutout.dxf`](bezel/panel_cutout.dxf) is that at 1:1. Good for
panels up to 5.83 mm thick. M3 × 16 suits 3 mm; add whatever yours is over
that to the screw length.

## Cooling

| Qty | Part | Supplier | Price |
|---:|---|---|---|
| 1 | **GDSTIME 40 mm 12 V fan**, 40 × 40 × 20 mm, 3-pin, 6000 rpm (sold in 2s) | [Amazon B08688CPNC](https://www.amazon.co.uk/dp/B08688CPNC) | — |
| 1 | Fan grille | printed — [`fan-grille/fan_grille_40.stl`](fan-grille/fan_grille_40.stl) |
| 4 | Fan screws, M3, **2.5 mm longer** than the fan's own | to clear the grille |

The fan is **12 V**, so it feeds off the supply side, not the USB converter.
It is a 4020 (20 mm deep, not the usual 10 mm) — the grille fits either, the
mounting pattern is the same, but check the depth against the case.

## Printing

| Part | Orientation |
|---|---|
| Bezel front | face down — flat front on the bed |
| Bezel back | **lid down** — the 45° flare makes this support-free; flange-down would bridge the whole cavity |
| Fan grille | flat, either way up |

No supports. 0.2 mm layers, 3 perimeters, 30 % infill. PETG or ABS, not PLA —
this lives in a vehicle. Roughly 25 g of filament for all three.

## Deliberately not used

* The board's four **M2 mounting holes** — the bezel clamps it instead, so no
  M2 hardware or standoffs.
* Its **MX1.25 LiPo connector** and **TF-card slot** — the back box encloses
  both. See [bezel/README.md](bezel/README.md) to cut access.
