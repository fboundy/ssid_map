# 40 mm fan grille

Flat concentric-ring finger guard for a standard 40 mm fan.

| File | What it is |
|---|---|
| `fan_grille.scad` | parametric source |
| `fan_grille_40.stl` | ready to print |

```
40.0 × 40.0 × 2.5 mm, corners R2.5
4 × Ø3.2 mm holes on 32 mm centres (±16, ±16)
36 mm opening, 3 rings 1.6 mm wide with 2.3 mm gaps, 6 spokes, 8 mm hub
```

The mounting holes are **Ø3.2**, clearance for M3 through the fan's own 3 mm
holes. Fan screws are often self-tapping and slightly over 3 mm — if yours
bind, raise `hole_d`. Set `countersink = true` if you want the heads flush.

## Printing

0.2 mm layers, 3 perimeters, 30 % infill — at 2.5 mm thick it is essentially
all perimeter. No supports, no brim needed. Print it flat, either way up.

The thinnest web is **2.0 mm**, where the frame passes the middle of each
edge; the ribs are 1.6 mm. That is comfortable in PLA or PETG, but do not
thin `thickness` much below 2 mm or the frame gets whippy.

## Airflow

About **52 % open** across the 36 mm aperture, measured off the rendered
outline. That is normal for a printed guard and finer than a wire one, which
matters on a small fan: if you would rather have the airflow than the fine
mesh, set `rings = 2`, which widens the gaps to 3.6 mm and opens it to roughly
**62 %**. Anything down to one ring still guards a finger.

## Other fan sizes

Four numbers, then re-export:

| Fan | `size` | `hole_pitch` | `open_d` | `rings` |
|---|---|---|---|---|
| 40 mm | 40 | 32 | 36 | 3 |
| 60 mm | 60 | 50 | 56 | 4 |
| 80 mm | 80 | 71.5 | 76 | 5 |
| 120 mm | 120 | 105 | 115 | 7 |

The ring spacing works itself out — the rings are always spread evenly
between the hub and the opening, and it asserts if you ask for more than fit.

```
openscad -o fan_grille_40.stl fan_grille.scad
openscad -o fan_grille_80.stl -D size=80 -D hole_pitch=71.5 -D open_d=76 -D rings=5 fan_grille.scad
```
