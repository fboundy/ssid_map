// 40 mm fan grille - Penrith Mountain Rescue Team badge
// =====================================================
// The MREW roundel worked as a cut-through grille: solid where the badge is
// purple, open where it is white. The ring band carries MOUNTAIN RESCUE over
// the top and PENRITH across the bottom as cut letters, the mountain is the
// solid centre, and the cross is an opening in it.
//
// The mountain outline and the cross geometry were traced from the team's own
// artwork (penrithmrt.org.uk), normalised to the radius of the badge's inner
// white disc, so the whole thing scales from `disc_r`.
//
// Render:  openscad -o fan_grille_40_pmrt.stl fan_grille_pmrt.scad
//
// This is the decorative alternative to fan_grille.scad - see the README for
// the airflow difference.

/* [Fan] */
size        = 40.0;    // fan is this square
hole_pitch  = 32.0;    // mounting-hole centres
hole_d      = 3.2;     // M3 clearance for the fan's 3.0 mm holes
corner_r    = 2.5;

/* [Grille] */
thickness   = 2.5;
open_d      = 36.0;    // aperture over the blades
disc_r      = 13.0;    // the badge's inner disc; the band is what is left
text_size   = 3.0;     // lettering in the band
engrave_d   = 0.8;     // the text is engraved, not cut: the counters of O, A,
                       // R and P would otherwise drop out as loose islands.
                       // Set text_through for a stencil font.
text_through = false;
band_slots  = true;    // airflow slots in the plain parts of the band
slot_ang    = 110;     // from 12 o'clock, mirrored; clear of both text runs
slot_sweep  = 42;

/* [Badge, traced from the artwork - all in units of disc_r] */
MOUNTAIN = [[-0.9527,-0.1459], [-0.8046,+0.1801], [-0.6405,-0.1481],
            [-0.2986,+0.5197], [+0.0570,+0.1527], [+0.3852,+0.6587],
            [+0.9573,-0.1276]];
cross_c   = [+0.0023, -0.4980];
cross_r   = 0.4308;
cross_arm = 0.1254;    // half-width of the arms

$fn = 128;
eps = 0.01;

// ---- derived -------------------------------------------------------------
open_r  = open_d / 2;
band_w  = open_r - disc_r;
assert(band_w > text_size + 1.4, "band too narrow for the lettering: lower disc_r or text_size");
text_r  = disc_r + band_w / 2;                 // centreline of the band

// Arc occupied per character, as an angle at the badge centre.
char_step = 360 * (text_size * 0.95) / (2 * PI * text_r);

// ---- helpers -------------------------------------------------------------
module rrect(w, r) { offset(r = r) offset(r = -r) square([w, w], center = true); }

module at_holes() {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * hole_pitch / 2, sy * hole_pitch / 2, 0]) children();
}

// Text set around an arc, letters upright to the centre. `centre_ang` is
// measured clockwise from 12 o'clock; `flip` sets it the other way up so the
// bottom run reads correctly.
module arc_text(txt, centre_ang, flip = false) {
    n = len(txt);
    span = (n - 1) * char_step;
    for (i = [0 : n - 1]) {
        // `a` is clockwise from 12 o'clock. The bottom run walks the other way
        // and each glyph is turned over, so it still reads left to right.
        a = centre_ang + (flip ? span / 2 - i * char_step
                               : i * char_step - span / 2);
        rotate([0, 0, -a])
            translate([0, text_r])
                rotate([0, 0, flip ? 180 : 0])
                    text(txt[i], size = text_size, halign = "center",
                         valign = "center", font = "Liberation Sans:style=Bold");
    }
}

// An arc-shaped slot in the band, for airflow through the plain sections.
module band_slot(centre_ang, sweep) {
    difference() {
        intersection() {
            circle(r = open_r - 1.2);
            rotate([0, 0, -centre_ang])
                polygon([[0, 0],
                         for (a = [-sweep/2 : 2 : sweep/2])
                             [open_r * sin(a), open_r * cos(a)],
                         [open_r * sin(sweep/2), open_r * cos(sweep/2)]]);
        }
        circle(r = disc_r + 1.2);
    }
}

// ---- the badge -----------------------------------------------------------
// Mountain: the traced skyline, dropped below the disc and clipped back to a
// circle slightly larger than the disc so it merges into the band.
module mountain() {
    intersection() {
        polygon(concat(MOUNTAIN * disc_r,
                       [[MOUNTAIN[len(MOUNTAIN)-1][0] * disc_r, -open_r],
                        [MOUNTAIN[0][0] * disc_r, -open_r]]));
        circle(r = disc_r + 0.3);
    }
}

module cross_opening() {
    intersection() {
        translate(cross_c * disc_r) circle(r = cross_r * disc_r);
        translate(cross_c * disc_r)
            union() {
                square([cross_arm * 2 * disc_r, cross_r * 2 * disc_r + 1], center = true);
                square([cross_r * 2 * disc_r + 1, cross_arm * 2 * disc_r], center = true);
            }
    }
}

module lettering() {
    arc_text("MOUNTAIN RESCUE", 0);
    arc_text("PENRITH", 180, true);
}

module grille_2d() {
    difference() {
        union() {
            // frame and the solid ring band
            difference() { rrect(size, corner_r); circle(r = open_r); }
            difference() { circle(r = open_r); circle(r = disc_r); }
            mountain();
        }
        cross_opening();
        if (band_slots) {
            band_slot(slot_ang, slot_sweep);
            band_slot(360 - slot_ang, slot_sweep);
        }
        if (text_through) lettering();
    }
}

module fan_grille_pmrt() {
    difference() {
        linear_extrude(thickness) grille_2d();
        if (!text_through)
            translate([0, 0, thickness - engrave_d])
                linear_extrude(engrave_d + eps) lettering();
        at_holes() translate([0, 0, -eps]) cylinder(d = hole_d, h = thickness + 2 * eps);
    }
}

fan_grille_pmrt();

echo(str("outside  : ", size, " x ", size, " x ", thickness, " mm"));
echo(str("aperture : ", open_d, " mm; badge disc ", disc_r * 2, " mm, band ", band_w, " mm"));
echo(str("lettering: ", text_size, " mm on r", text_r, ", ", char_step, " deg per character"));
