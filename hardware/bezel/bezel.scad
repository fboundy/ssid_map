// Panel-mount bezel for the Waveshare ESP32-S3-Touch-LCD-1.9
// ==========================================================
// Two printed parts that sandwich the display module and the panel:
//
//   front - sits on the OUTSIDE of the panel. Holds a 2 mm protective
//           sheet and the module's cover glass in a pocket, and has a
//           spigot that locates in the panel cutout.
//   back  - sits INSIDE the panel. Two L-shaped ribs reach down the sides
//           of the module and turn in behind the PCB, pressing on the bare
//           strips along its long edges to push the module forward against
//           the sheet. Open at the rear, so the USB-C socket, the pin
//           headers and the battery lead are all reachable from behind.
//
// Four M3 countersunk screws pass from the front, through the panel, into
// bosses on the back part.
//
// Module dimensions are from Waveshare's own drawing
// ESP32-S3-Touch-LCD-1.9-20250411.pdf (in the -DAD.rar download).
// See README.md for the panel cutout and the assembly order.
//
//   Z datum is the front face of the bezel; +Z runs back into the panel.
//
// Render:  openscad -o front.stl -D 'part="front"' bezel.scad
//          openscad -o back.stl  -D 'part="back"'  bezel.scad

part = "both";  // "front" | "back" | "both" | "assembly" | "section" | "cutout"

/* [Your installation] */
// Which way round the board sits, looking at the screen from the front.
// Flip this and the firmware's LCD_ROTATE_180 together.
usb_left     = true;
panel_t      = 3.0;     // panel thickness (see max_panel_t below)
sheet_t      = 1.0;     // protective sheet thickness
flange       = 10.0;    // overlap onto the panel, all round the cutout

/* [Module: Waveshare ESP32-S3-Touch-LCD-1.9] */
// Measured on the board where noted; the rest from Waveshare's drawing.
glass_w      = 55.10;   // cover glass, 4x R1.00                      (measured)
glass_h      = 31.40;
glass_r      = 1.00;
glass_t      = 2.54;    // glass front face -> back of cover glass    (measured)
lcd_w        = 52.36;   // display module body behind the glass
lcd_h        = 28.80;
pcb_w        = 52.60;
pcb_h        = 29.00;
pcb_t        = 1.60;
d_pcb_back   = 7.83;    // glass front face -> back of PCB            (measured)
d_comp_back  = 11.80;   // glass front face -> back of rear components
d_pins       = 17.50;   // glass front face -> tips of the header pins (measured)

// Clear aperture in the glass mask, NOT the active pixel area (43.12 x 23.10):
// the window is cut to the aperture so the bezel edge lands on the mask edge.
va_w         = 44.70;   // (measured)
va_h         = 24.30;   // (measured)
// The aperture is not centred on the glass: it sits 1.9 mm towards the end
// AWAY from the USB-C socket (+x here), so the wide black border is at the
// USB end. Confirmed against the board.
va_off_x     =  1.90;
va_off_y     =  0.00;

/* [Fits and clearances] */
glass_fit    = 0.25;    // per side, around the cover glass
sheet_fit    = 0.25;    // per side, around the protective sheet
sheet_over   = 0.50;    // sheet pocket larger than the glass pocket, per side
cut_clr      = 0.25;    // per side, spigot to panel cutout
win_margin   = 0.20;    // window opening beyond the clear aperture, per side
win_chamfer  = 1.20;    // 45 deg chamfer round the window on the front face
side_clr     = 0.50;    // per side, rib legs to the PCB edge
preload      = 0.30;    // ribs run this much proud, to take up the stack-up

/* [Screws] */
screw_clr    = 3.4;     // M3 clearance, front part
screw_head_d = 6.2;     // countersunk head
screw_pilot  = 2.6;     // self-tapping pilot, back part
screw_depth  = 7.0;     // pilot depth into the back part

/* [Printed wall thicknesses] */
face_t       = 3.0;     // front plate
back_t       = 3.0;     // back plate
wall         = 2.0;     // spigot wall around the glass pocket
rib_t        = 2.5;     // rib thickness behind the PCB

/* [Back box, covering the pin headers] */
box_wall     = 2.5;     // wall of the box behind the panel
lid_t        = 2.0;     // its back face
pin_gap      = 1.5;     // clearance behind the pin tips
usb_open_w   = 16.0;    // USB-C opening in the side wall (across the board)
usb_open_h   = 8.0;
usb_open_z   = 10.5;    // from the bezel front face
vents        = true;    // slots in the lid

$fn = 64;
eps = 0.01;

// Everything below is laid out with the USB-C end at -x; the parts are
// mirrored on output when the socket is wanted on the other side.
xflip = usb_left ? -1 : 1;

// ---- derived -------------------------------------------------------------
pocket_w  = glass_w + 2*glass_fit;         // 55.50
pocket_h  = glass_h + 2*glass_fit;         // 31.90
pocket_r  = glass_r + glass_fit;

sheet_pk_w  = pocket_w + 2*sheet_over;     // 56.50
sheet_pk_h  = pocket_h + 2*sheet_over;     // 32.90
sheet_cut_w = sheet_pk_w - 2*sheet_fit;    // cut the sheet to this
sheet_cut_h = sheet_pk_h - 2*sheet_fit;

cutout_w = pocket_w + 2*wall;              // panel cutout
cutout_h = pocket_h + 2*wall;
spigot_w = cutout_w - 2*cut_clr;
spigot_h = cutout_h - 2*cut_clr;

body_w = cutout_w + 2*flange;              // outer size of both parts
body_h = cutout_h + 2*flange;
body_r = 4.0;

screw_x = cutout_w/2 + flange/2;
screw_y = cutout_h/2 + flange/2;

z_sheet   = face_t;                        // front face of the sheet
z_glass   = face_t + sheet_t;              // front face of the glass = module datum
z_pcb_bk  = z_glass + d_pcb_back;          // back face of the PCB
z_comp_bk = z_glass + d_comp_back;
z_panel_bk = face_t + panel_t;             // back face of the panel
z_plate_bk = z_panel_bk + back_t;          // back face of the back plate

// The back plate has to sit in front of the PCB's back face for the ribs to
// have anything to stand on.
max_panel_t = d_pcb_back + sheet_t - back_t;      // 6.8 mm with the defaults
assert(panel_t <= max_panel_t, "panel_t too thick: the ribs cannot reach the PCB");

// Bearing ribs. The PCB's outer strips are clear of components between the
// buttons/standoff at the left end and the standoff at the right end; the
// pin-header bodies reach 10.14 mm out from the centreline.
rib_y_in  = 11.0;                          // inner edge of the rib foot
rib_y_leg = pcb_h/2 + side_clr;            // legs must clear the PCB edge
rib_y_out = pocket_h/2 + 2.5;              // and overlap the plate to bond to it
rib_x_lo  =  -8.0;                         // well clear of the BOOT/RST buttons
rib_x_hi  =  21.5;                         // clear of the right standoff
rib_z0    = z_pcb_bk - preload;            // bearing face
rib_z1    = rib_z0 + rib_t;                // back of the ribs

aperture_w = pocket_w + 0.6;               // module passes through the plate here
aperture_h = pocket_h + 0.6;

// The back part closes into a box deep enough to cover the header pins. It
// flares out to the flange at 45 degrees so it prints on its lid, unsupported.
box_w      = aperture_w + 2*box_wall;
box_h      = aperture_h + 2*box_wall;
box_r      = pocket_r + box_wall;
z_box_in   = z_glass + d_pins + pin_gap;   // inner face of the lid
z_box_out  = z_box_in + lid_t;
flare_h    = max((body_w - box_w)/2, (body_h - box_h)/2);
z_flare_end = z_plate_bk + flare_h;
assert(z_flare_end < z_box_in, "no room for the 45 degree flare: the box is too shallow");

// ---- helpers -------------------------------------------------------------
module rrect(w, h, r, t) {
    linear_extrude(t) offset(r=r) offset(r=-r) square([w, h], center=true);
}

module csk(z0, z1) {                       // countersunk hole, head at z0
    translate([0, 0, z0 - eps]) cylinder(d=screw_clr, h=z1 - z0 + 2*eps);
    translate([0, 0, z0 - eps])
        cylinder(d1=screw_head_d, d2=screw_clr, h=(screw_head_d - screw_clr)/2 + eps);
}

module at_screws() {
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx*screw_x, sy*screw_y, 0]) children();
}

// Both parts are handed - the window is offset and the ribs are not
// symmetric - so mark the end the USB-C socket goes to. Debossed into the
// back face, where it is hidden once assembled.
mark_depth = 0.6;
module usb_mark(z_face) {
    translate([-screw_x, 0, z_face - mark_depth])
        scale([xflip, 1, 1])               // cancel the part-level mirror
            linear_extrude(mark_depth + eps)
                rotate([0, 0, 90])
                    text("USB", size=4.5, halign="center", valign="center");
}

// ---- front part ----------------------------------------------------------
module front() { scale([xflip, 1, 1]) front_body(); }
module front_body() {
    difference() {
        union() {
            rrect(body_w, body_h, body_r, face_t);
            translate([0, 0, face_t - eps])
                rrect(spigot_w, spigot_h, pocket_r + wall, panel_t + eps);
        }

        // viewing window, chamfered on the front face
        translate([va_off_x, va_off_y, -eps])
            rrect(va_w + 2*win_margin, va_h + 2*win_margin, 0.8, face_t + 2*eps);
        translate([va_off_x, va_off_y, -eps])
            linear_extrude(win_chamfer + eps, scale=[
                (va_w + 2*win_margin) / (va_w + 2*win_margin + 2*win_chamfer),
                (va_h + 2*win_margin) / (va_h + 2*win_margin + 2*win_chamfer)])
                offset(r=0.8) offset(r=-0.8)
                    square([va_w + 2*win_margin + 2*win_chamfer,
                            va_h + 2*win_margin + 2*win_chamfer], center=true);

        // pocket for the protective sheet, then the glass pocket out the back
        translate([0, 0, z_sheet]) rrect(sheet_pk_w, sheet_pk_h, pocket_r, sheet_t + eps);
        translate([0, 0, z_glass - eps])
            rrect(pocket_w, pocket_h, pocket_r, face_t + panel_t - z_glass + 2*eps);

        at_screws() csk(0, face_t);
        usb_mark(face_t);
    }
}

// ---- back part -----------------------------------------------------------
// A box that covers the pin headers, flaring out at 45 degrees to the flange
// that clamps the panel. Two L-section ribs inside it press on the PCB: a leg
// that runs back alongside the module, outside the PCB edge, and a foot that
// turns in behind the PCB to bear on the clear strip along its long edge.
// Open on one side for the USB-C socket.
module back() { scale([xflip, 1, 1]) back_body(); }
module back_body() {
    assert(rib_z0 > z_plate_bk, "no room for the rib legs: reduce panel_t or back_t");
    difference() {
        union() {
            difference() {
                union() {
                    hull() {
                        translate([0, 0, z_panel_bk]) rrect(body_w, body_h, body_r, back_t);
                        translate([0, 0, z_flare_end - eps]) rrect(box_w, box_h, box_r, eps);
                    }
                    translate([0, 0, z_flare_end - eps])
                        rrect(box_w, box_h, box_r, z_box_out - z_flare_end + eps);
                }
                // cavity: the module and its pins live in here
                translate([0, 0, z_panel_bk - eps])
                    rrect(aperture_w, aperture_h, pocket_r, z_box_in - z_panel_bk + eps);
            }

            for (sy = [-1, 1]) scale([1, sy, 1]) {
                // leg: runs back alongside the module, clear of the PCB edge.
                // Full width, so it is carried by the wall along its length.
                translate([-aperture_w/2, rib_y_leg, z_plate_bk - eps])
                    cube([aperture_w, rib_y_out - rib_y_leg,
                          rib_z1 - z_plate_bk + eps]);
                // foot: turns in behind the PCB and bears on the clear strip.
                // Bonded to the leg over the full rib thickness at rib_y_leg.
                translate([rib_x_lo, rib_y_in, rib_z0])
                    cube([rib_x_hi - rib_x_lo, rib_y_leg - rib_y_in, rib_t]);
            }
        }
        // USB-C opening, through the side wall at the marked end
        translate([-body_w, -usb_open_w/2, usb_open_z])
            cube([body_w - aperture_w/2 + 3, usb_open_w, usb_open_h]);
        // ventilation slots in the lid
        if (vents)
            for (i = [-2 : 2])
                translate([i*8, 0, z_box_in + lid_t/2])
                    cube([3, aperture_h - 12, lid_t + 2*eps], center=true);
        at_screws() translate([0, 0, z_panel_bk - eps])
            cylinder(d=screw_pilot, h=screw_depth + eps);
        usb_mark(z_plate_bk);
    }
}

// ---- reference model of the module, for the assembly view ----------------
module module_ref() { scale([xflip, 1, 1]) module_ref_body(); }
module module_ref_body() {
    color("black")     translate([0, 0, z_glass]) rrect(glass_w, glass_h, glass_r, glass_t);
    color("#3060a0")   translate([va_off_x, va_off_y, z_glass - 0.05])
                           rrect(va_w, va_h, 0.5, 0.1);
    color("dimgray")   translate([0, 0, z_glass + glass_t])
                           rrect(lcd_w, lcd_h, 0.5, d_pcb_back - pcb_t - glass_t);
    color("darkgreen") translate([0, 0, z_pcb_bk - pcb_t]) rrect(pcb_w, pcb_h, 0.5, pcb_t);
    // pin-header bodies, then the pins at their real length
    color("#222")      translate([0, 0, z_pcb_bk])
                           rrect(48.3, 20.0, 0.5, d_comp_back - d_pcb_back);
    color("goldenrod") for (sy = [-1, 1]) translate([0, sy*8.89, z_pcb_bk])
                           rrect(48.3, 0.8, 0.1, d_pins - d_pcb_back);
    color("silver")    translate([-pcb_w/2 - 1, -4.5, z_pcb_bk]) cube([10, 9, 3.2]);
}

module panel_ref() {
    color("lightgray", 0.30) translate([0, 0, face_t]) difference() {
        translate([-body_w/2 - 15, -body_h/2 - 10, 0])
            cube([body_w + 30, body_h + 20, panel_t]);
        translate([0, 0, -eps]) rrect(cutout_w, cutout_h, 2, panel_t + 2*eps);
        at_screws() translate([0, 0, -eps]) cylinder(d=screw_clr, h=panel_t + 2*eps);
    }
}

// ---- output --------------------------------------------------------------
if (part == "front") front();
else if (part == "back") back();
else if (part == "assembly") {
    color("steelblue", 0.45) front();
    color("indianred", 0.45) back();
    module_ref();
    panel_ref();
} else if (part == "cutout") {
    // 2D machining outline for the panel: the rectangular cutout and the four
    // screw holes. Export with:  openscad -o panel_cutout.dxf -D 'part="cutout"'
    offset(r=pocket_r + wall) offset(r=-(pocket_r + wall))
        square([cutout_w, cutout_h], center=true);
    at_screws() circle(d=screw_clr);
} else if (part == "section") {
    // Cut the assembly along its length to check the stack-up and clearances.
    difference() {
        union() {
            color("steelblue") front();
            color("indianred") back();
            module_ref();
            panel_ref();
        }
        translate([-100, 0, -10]) cube([200, 120, 80]);   // keep y<0
    }
} else {
    translate([0, -body_h/2 - 4, 0]) front();
    translate([0,  body_h/2 + 4, 0]) back();
}

echo(str("panel cutout       : ", cutout_w, " x ", cutout_h, " mm, R", pocket_r + wall));
echo(str("panel screw holes  : 4 x 3.4 mm at +/-", screw_x, ", +/-", screw_y));
echo(str("bezel outer size   : ", body_w, " x ", body_h, " mm"));
echo(str("cut the sheet to   : ", sheet_cut_w, " x ", sheet_cut_h, " mm"));
echo(str("front part depth   : ", face_t + panel_t, " mm"));
echo(str("depth behind panel : ", z_box_out - z_panel_bk, " mm overall (pins end at ",
         z_glass + d_pins - z_panel_bk, " mm)"));
echo(str("window             : ", va_w + 2*win_margin, " x ", va_h + 2*win_margin,
         " mm, offset ", va_off_x, ", ", va_off_y));
echo(str("max panel_t        : ", max_panel_t, " mm"));
