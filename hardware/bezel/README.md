# Panel-mount bezel — Waveshare ESP32-S3-Touch-LCD-1.9

Two 3D-printed parts that mount the display module into a flat panel and
sandwich it in place, with a 1 mm protective sheet over the screen.

| File | What it is |
|---|---|
| `bezel.scad` | parametric source — **edit `panel_t` here first** |
| `front.stl` | front bezel (outside of the panel) |
| `back.stl` | back clamp (inside the panel) |
| `panel_cutout.dxf` | the cutout and screw holes, for marking out or CNC |

Printed as supplied: bezel is **79.6 × 55.9 mm**, standing **3 mm** proud of
the panel, with a box **19 mm** deep behind it that encloses the pin headers
(their tips sit 15.5 mm back, leaving 1.5 mm under the lid).

## Panel preparation

```
        79.6 wide bezel                            all dimensions mm
   ┌─────────────────────────────────┐
   │  ○                           ○  │   ○ = 3.4 dia, at ±34.8, ±22.95
   │     ┌───────────────────┐       │       (four, M3 clearance)
   │     │   59.6 × 35.9     │       │   ▭ = cutout, R3.25 corners
   │     │   cutout  R3.25   │       │
   │     └───────────────────┘       │
   │  ○                           ○  │
   └─────────────────────────────────┘
```

* **Cutout 59.6 × 35.9 mm**, corner radius 3.25 (a 6 mm cutter is fine).
* **Four Ø3.4 mm holes** on a 69.6 × 45.9 mm pattern, i.e. ±34.8, ±22.95
  from the cutout centre.
* Panel thickness **3 mm as supplied**; anything up to **5.83 mm** works by
  changing `panel_t` in `bezel.scad` and re-exporting. Thicker than that and
  the back clamp can no longer reach the PCB.

`panel_cutout.dxf` has exactly this geometry at 1:1.

## Protective sheet

Cut the 1 mm sheet to **56.1 × 32.4 mm**. It drops into the front bezel behind
the window and the module's cover glass presses against it. The pocket is
56.6 × 32.9, so it is a slip fit — file the corners to roughly R1.25.

## Both parts are handed

The clear aperture is **not centred on the glass**: it sits 1.9 mm towards the
end away from the USB-C socket, so the black border is about **7.1 mm wide at
the USB end** and 3.3 mm at the other (confirmed on the board). The window is
cut 45.1 × 24.7 and shifted to match, and the back clamp's ribs are likewise
offset to miss the buttons at the USB end.

So both parts only fit one way round. Each has **`USB` debossed on its back
face** at the end the socket goes to — check it lines up with the board before
you drive the screws.

As supplied the board sits with the **USB-C socket on the left** as you look
at the screen, which is why the firmware is built with `LCD_ROTATE_180 1`.
The two must agree: `usb_left` in `bezel.scad` mirrors both printed parts,
`LCD_ROTATE_180` in `include/config.h` turns the image over.

## Assembly

1. Drop the protective sheet into the back of the front bezel.
2. Lay the module in behind it, glass first, into the 55.6 × 31.9 pocket,
   with the USB-C socket at the end marked `USB`. The headers and the socket
   face away from you.
3. From the front of the panel, push the bezel's spigot through the cutout.
4. From behind, slide the back box over the module, `USB` mark to the same
   end — the two ribs go either side of the PCB — until it sits flat on the
   panel.
5. Four **M3 × 16 countersunk** screws from the front. They self-tap into
   Ø2.6 pilot holes 7 mm deep in the back part; drive them gently.

### What the box encloses

The back box covers the pin headers completely. The only opening is a
**16 × 8 mm slot for the USB-C plug** in the side wall at the `USB` end, plus
ventilation slots in the lid.

Everything else on the back of the board — the battery connector, the IPEX
antenna pad, the TF-card slot on the opposite short edge — ends up sealed in.
If you need any of them, add a cut-out in `bezel.scad`; the USB slot
(`usb_open_*`) is the pattern to copy.

## How it holds together

```
   z=0     front face of the bezel
   3.0     back of the face plate ─┐ 1 mm protective sheet
   4.0     front of the cover glass ┘
   6.54    back of the cover glass
  11.83    back of the PCB            ← the ribs bear here
  15.8     back of the rear components
  21.5     tips of the pin headers
```

The front bezel locates the glass; the back clamp's ribs press on the PCB to
push the module forward against the sheet. The ribs run **0.3 mm proud**
(`preload`) so the stack is always in compression. If the module rattles,
increase `preload` by 0.2 mm or add a strip of thin foam tape behind the
sheet; if the back clamp will not sit flat on the panel, reduce it.

The ribs bear only where the PCB is clear of components: the strips along the
long edges, between **y 11.0–14.5** and **x −8 to +21.5** from the board
centre. That keeps them clear of the pin-header bodies (which reach 10.14 mm
from the centreline), the **BOOT and RST buttons** and the mounting standoff
at the USB end, and the standoff at the far end. `rib_x_lo` is what to change
if they still catch anything. The board's four M2 holes are **not** used — it
is a clamp, not a screwed mount.

## Printing

* 0.2 mm layers, 3 perimeters, 30 % infill. PETG or ABS if it will sit in
  sunlight; PLA will creep under the screw preload if it gets warm.
* Print the **front part face down** — the flat front on the bed.
* Print the **back part lid down**. Its walls flare out to the flange at 45°
  precisely so this needs no supports; printing it flange-down instead would
  leave the lid bridging the whole cavity.
* No supports for either part. The `USB` marks are 0.6 mm recesses.
* The window chamfer prints cleanly face down; it is the top surface.

## Source dimensions

Sizes marked *measured* were taken off the board; the rest come from
Waveshare's drawing `ESP32-S3-Touch-LCD-1.9-20250411.pdf`,
in [ESP32-S3-Touch-LCD-1.9-DAD.rar](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.9/ESP32-S3-Touch-LCD-1.9-DAD.rar)
(linked from the [wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.9)).

| Feature | Size | Source |
|---|---|---|
| Cover glass | 55.10 × 31.40, 4 × R1.00 | measured (drawing: 55.00) |
| Clear aperture | 44.70 × 24.30, offset 1.9 in x | measured |
| Active pixel area | 43.12 × 23.10 | drawing |
| Display module outline | 52.36 × 28.80 | drawing |
| PCB | 52.60 × 29.00 × 1.6 | drawing (glass overhangs it ~1.2 all round) |
| Glass front → back of cover glass | 2.54 | measured (drawing: 2.50) |
| Glass front → back of PCB | 7.83 | measured (drawing: 7.80) |
| Glass front → back of rear components | 11.80 | drawing |
| Glass front → tips of pin headers | 17.50 | measured |
| Mounting holes | 4 × M2, 40.75 × 25.00 pattern | drawing (unused here) |
| USB-C | one short edge, on the back face | drawing |

The window is cut to the measured **clear aperture**, not the active pixel
area, so its edge lands on the edge of the glass mask. The aperture is
1.6 mm wider and 1.2 mm taller than the active area, so there is that much
margin before any pixels are hidden.

## Regenerating

Needs [OpenSCAD](https://openscad.org) (no install required — the portable
zip works):

```
openscad -o front.stl        -D 'part="front"'  bezel.scad
openscad -o back.stl         -D 'part="back"'   bezel.scad
openscad -o panel_cutout.dxf -D 'part="cutout"' bezel.scad
```

`part="assembly"` shows the parts with the module and panel in place, and
`part="section"` cuts that in half to check the stack-up after changing
`panel_t` or `sheet_t`. The key figures are echoed on every run.
