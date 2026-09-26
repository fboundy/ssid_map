# Panel-mount bezel — Waveshare ESP32-S3-Touch-LCD-1.9

Two 3D-printed parts that mount the display module into a flat panel and
sandwich it in place, with a 2 mm protective sheet over the screen.

| File | What it is |
|---|---|
| `bezel.scad` | parametric source — **edit `panel_t` here first** |
| `front.stl` | front bezel (outside of the panel) |
| `back.stl` | back clamp (inside the panel) |
| `panel_cutout.dxf` | the cutout and screw holes, for marking out or CNC |

Printed as supplied: bezel is **79.5 × 55.9 mm**, standing **3 mm** proud of
the panel, with **10.8 mm** behind it (plus the pin headers if fitted).

## Panel preparation

```
        79.5 wide bezel                            all dimensions mm
   ┌─────────────────────────────────┐
   │  ○                           ○  │   ○ = 3.4 dia, at ±34.75, ±22.95
   │     ┌───────────────────┐       │       (four, M3 clearance)
   │     │   59.5 × 35.9     │       │   ▭ = cutout, R3.25 corners
   │     │   cutout  R3.25   │       │
   │     └───────────────────┘       │
   │  ○                           ○  │
   └─────────────────────────────────┘
```

* **Cutout 59.5 × 35.9 mm**, corner radius 3.25 (a 6 mm cutter is fine).
* **Four Ø3.4 mm holes** on a 69.5 × 45.9 mm pattern, i.e. ±34.75, ±22.95
  from the cutout centre.
* Panel thickness **3 mm as supplied**; anything up to **6.8 mm** works by
  changing `panel_t` in `bezel.scad` and re-exporting. Thicker than that and
  the back clamp can no longer reach the PCB.

`panel_cutout.dxf` has exactly this geometry at 1:1.

## Protective sheet

Cut the 2 mm sheet to **56 × 32.4 mm**. It drops into the front bezel behind
the window and the module's cover glass presses against it. The pocket is
56.5 × 32.9, so it is a slip fit — file the corners to roughly R1.25.

## Assembly

1. Drop the protective sheet into the back of the front bezel.
2. Lay the module in behind it, glass first, into the 55.5 × 31.9 pocket.
   The 2 × 20 headers and the USB-C socket face away from you.
3. From the front of the panel, push the bezel's spigot through the cutout.
4. From behind, slide the back clamp over the module — the two ribs go
   either side of the PCB — until it sits flat on the panel.
5. Four **M3 × 16 countersunk** screws from the front, into the bosses on the
   back clamp. They self-tap into Ø2.6 pilot holes; drive them gently.

The USB-C socket, the pin headers and the battery connector are all clear:
the back clamp is open behind the module and the cable exits to the side.

## How it holds together

```
   z=0     front face of the bezel
   3.0     back of the face plate ─┐
   5.0     front of the cover glass ├ 2 mm protective sheet
   7.5     back of the cover glass ─┘
  12.8     back of the PCB          ← the ribs bear here
  16.8     back of the rear components
```

The front bezel locates the glass; the back clamp's ribs press on the PCB to
push the module forward against the sheet. The ribs run **0.3 mm proud**
(`preload`) so the stack is always in compression. If the module rattles,
increase `preload` by 0.2 mm or add a strip of thin foam tape behind the
sheet; if the back clamp will not sit flat on the panel, reduce it.

The ribs bear only where the PCB is clear of components: the strips along the
long edges, between **y 11.0–14.5** and **x −14 to +21.5** from the board
centre. That keeps them clear of the pin-header bodies (which reach 10.14 mm
from the centreline), the two buttons and the mounting standoff at the left
end, and the standoff at the right end. The board's four M2 holes are **not**
used — it is a clamp, not a screwed mount.

## Printing

* 0.2 mm layers, 3 perimeters, 30 % infill. PETG or ABS if it will sit in
  sunlight; PLA will creep under the screw preload if it gets warm.
* Print both parts **face down** (the large flat face on the bed). No
  supports needed — every overhang is a 2.5 mm rib or a boss.
* The window chamfer prints cleanly face down; it is the top surface.

## Source dimensions

Taken from Waveshare's own drawing, `ESP32-S3-Touch-LCD-1.9-20250411.pdf`,
in [ESP32-S3-Touch-LCD-1.9-DAD.rar](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.9/ESP32-S3-Touch-LCD-1.9-DAD.rar)
(linked from the [wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.9)).

| Feature | Size |
|---|---|
| Cover glass | 55.00 × 31.40, 4 × R1.00 |
| Active area | 43.12 × 23.10, centred on the glass |
| Display module outline | 52.36 × 28.80 |
| PCB | 52.60 × 29.00 × 1.6 (glass overhangs it 1.20 all round) |
| Glass front → back of PCB | 7.80 |
| Glass front → back of rear components | 11.80 |
| Glass front → back of cover glass | 2.50 |
| Mounting holes | 4 × M2, 40.75 × 25.00 pattern (unused here) |
| USB-C | left-hand short edge, on the back face |

The drawing dimensions the active area as centred vertically in the module
outline (2.85 top and bottom) and the outline as centred on the glass; it
does not dimension the active area horizontally, so that is assumed centred
too. The window is cut 0.6 mm oversize all round to absorb it.

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
