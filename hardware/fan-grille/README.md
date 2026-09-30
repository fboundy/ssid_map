# 40 mm fan grille

Two finger guards for a standard 40 mm fan: a plain concentric-ring one, and
one cut as the Penrith Mountain Rescue Team badge.

| File | What it is |
|---|---|
| `fan_grille.scad` / `fan_grille_40.stl` | plain rings — the one to use if airflow matters |
| `fan_grille_pmrt.scad` / `fan_grille_40_pmrt.stl` | PMRT badge |

Both share the same mounting: 40 mm square, 2.5 mm thick, four Ø3.2 mm holes
on 32 mm centres, 36 mm aperture.

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

Measured off the rendered outlines, across the 36 mm aperture:

| | open area |
|---|---|
| plain rings | **52 %** |
| PMRT badge | **30 %** |

53 % is normal for a printed guard and finer than a wire one. If you would
rather have the airflow, `rings = 2` widens the gaps to 3.6 mm and opens the
plain grille to roughly **62 %**; anything down to one ring still guards a
finger.

The badge is inherently more restrictive — the logo is mostly solid, and no
amount of tuning changes that. It is the decorative option; use the plain one
where the fan is actually working hard.

## The badge version

The roundel read as a cut-through grille: solid where the badge is purple,
open where it is white. The mountain outline and the cross were traced from
the team's own artwork on [penrithmrt.org.uk](https://penrithmrt.org.uk/),
normalised to the radius of the badge's inner disc, so the whole thing scales
from `disc_r`.

Two deliberate departures from the badge:

* **The lettering is engraved 0.8 mm, not cut through.** Cutting it would drop
  the counters of O, A, R and P out as loose islands. Set `text_through = true`
  if you have a stencil font installed.
* **Two arc slots** break the plain lower quarters of the ring, for airflow.
  `band_slots = false` removes them and gives you the ring unbroken.

The white outline that separates the cross roundel from the mountain is left
out — it would have isolated the four quadrants. The cross itself is a through
opening, so it still reads.

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
