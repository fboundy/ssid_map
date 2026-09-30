// Finger guard for a 40 mm fan
// ============================
// Flat concentric-ring grille: an outer frame carrying three rings and six
// radial spokes onto a central hub. Fits the standard 40 mm pattern —
// 40 mm square, four Ø3 mm holes on 32 mm centres.
//
// Render:  openscad -o fan_grille_40.stl fan_grille.scad
//
// Sizing it for another fan is just the first four numbers: an 80 mm fan is
// size 80, hole_pitch 71.5, open_d 76, and rings 5 looks about right.

/* [Fan] */
size        = 40.0;    // fan is this square
hole_pitch  = 32.0;    // mounting-hole centres
hole_d      = 3.2;     // the fan's holes are 3.0; this is M3 clearance
corner_r    = 2.5;     // to match the fan's rounded corners

/* [Grille] */
thickness   = 2.5;     // plate thickness
open_d      = 36.0;    // opening over the blades
hub_d       = 8.0;     // solid disc at the centre
rings       = 3;       // concentric rings between hub and frame
ring_w      = 1.6;     // width of each ring
spokes      = 6;       // radial bars
spoke_w     = 1.8;

/* [Fit] */
countersink = false;   // true to countersink the mounting holes
csk_d       = 6.0;

$fn = 96;
eps = 0.01;

// ---- derived -------------------------------------------------------------
open_r = open_d / 2;
hub_r  = hub_d / 2;
// Spread the rings evenly across the gap between the hub and the opening, so
// changing `rings` never needs the spacing adjusted by hand.
span     = open_r - hub_r;
ring_gap = (span - rings * ring_w) / (rings + 1);
assert(ring_gap > 0.8, "no room for that many rings: reduce `rings` or `ring_w`");
function ring_r(i) = hub_r + ring_gap * (i + 1) + ring_w * (i + 0.5);

// ---- helpers -------------------------------------------------------------
module rrect(w, r) {
    offset(r = r) offset(r = -r) square([w, w], center = true);
}

module annulus(r, w) {
    difference() {
        circle(r = r + w / 2);
        circle(r = r - w / 2);
    }
}

module at_holes() {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * hole_pitch / 2, sy * hole_pitch / 2, 0]) children();
}

// ---- the grille ----------------------------------------------------------
module grille_2d() {
    // frame
    difference() {
        rrect(size, corner_r);
        circle(r = open_r);
    }
    // hub and rings
    circle(r = hub_r);
    for (i = [0 : rings - 1]) annulus(ring_r(i), ring_w);
    // spokes, run into the frame so everything is tied together
    for (i = [0 : spokes - 1])
        rotate([0, 0, i * 360 / spokes])
            translate([0, -spoke_w / 2]) square([open_r + 0.5, spoke_w]);
}

module fan_grille() {
    difference() {
        linear_extrude(thickness) grille_2d();
        at_holes() {
            translate([0, 0, -eps]) cylinder(d = hole_d, h = thickness + 2 * eps);
            if (countersink)
                translate([0, 0, thickness - (csk_d - hole_d) / 2])
                    cylinder(d1 = hole_d, d2 = csk_d, h = (csk_d - hole_d) / 2 + eps);
        }
    }
}

fan_grille();

echo(str("outside        : ", size, " x ", size, " x ", thickness, " mm"));
echo(str("mounting holes : 4 x ", hole_d, " at +/-", hole_pitch / 2));
echo(str("opening        : ", open_d, " mm, ", rings, " rings ", ring_w,
         " wide, ", ring_gap, " mm gaps"));
