// 1. Nipkow disc with integrated diffuser for Mecha-telly 
// 2. DVD motor adapter cup for Mecha-telly
// 
// To process: 
// Load in OpenSCAD, press F6 and save as .3mf. 3mf preserves materials.
// 
// CC-BY 2026 svofski
// 

$fn=64;
outer_d=178;
rim=2;
nholes=32;
thicc=0.8;
hole_r=0.8;
hole_overlap=0.1;
hole_dilate=0.1;
shaft_d=15.4;
spindle_r2=36/2;

spokes_r = 58;

sync_r = 55;
vsync_r = 40;
hsync_hole_r = 1;
spoke_width = 6;

outer_r = outer_d/2 - rim - hole_r/2;

with_diffusers = true;
with_white_spoke = false;

// ---------------
// MAKE THE WHEEL
// --------------
main_wheel();

// -------------------
// MAKE THE MOTOR CUP
// -------------------
//motor_cup();

module main_wheel(thicc=thicc, shaft_d=shaft_d)
{
    color("black")
    intersection() 
    {
        difference()
        {
            cylinder(h=thicc,r=outer_d/2);

            visual_holes();
            translate([0,0,0.21]) sync_holes(h=thicc-0.2);
            translate([0,0,-0.1]) cylinder(h=thicc + 0.2, r=shaft_d/2); // shaft

            spokes_negative();
    
            blablabla();
        }    
    }

    color("magenta")
    {
        if (with_diffusers) visual_holes(h=0.2, zofs=0);
        if (with_white_spoke) translate([0,0,0.2]) sync_holes(h=thicc-0.19, zofs=0);
        blablabla();
    }
}

module visual_holes(h=thicc + 0.2, zofs=-0.1, sync_ofs=1/4)
{
    for(i = [0:1:nholes-1]) {
        rotate([0, 0, (-i - sync_ofs) * 360/nholes]) {
            translate([outer_r - i * (hole_r - hole_overlap), 0, zofs]) 
                cylinder(h=h, r=hole_r + hole_dilate);
        }
    }
}

module trapezoide(length, width1, width2, height)
{
    hull() {
        translate([-length/2, 0, 0]) cube([0.001, width1, height], center=true);
        translate([+length/2, 0, 0]) cube([0.001, width2, height], center=true);
    }
}

module sync_holes(h=thicc + 0.2, zofs=-0.1, square_len=hsync_hole_r*5, hsync=false, vsync=true) 
{
   sector_width1 = 3.141 * 2 * (sync_r - square_len/2)  / 64;
   sector_width2 = 3.141 * 2 * (sync_r + square_len/2)  / 64;
   for(i = [0:1:nholes-1]) {
        rotate([0, 0, -i * 360/nholes]) {
            if (hsync) {
                translate([sync_r, 0, zofs])// cylinder(h=h, r=hsync_hole_r + ((i == 0) ? 0.5 : 0));
                    translate([0, 0, h/2]) //cube([square_len, sector_width, h], center=true);
                        trapezoide(square_len, sector_width1, sector_width2, h);
            }
            if (vsync && i == 0) {
                translate([vsync_r, 0, zofs])
                    translate([0, 0, h/2]) trapezoide(square_len*5, spoke_width, spoke_width, h);
            }
        }
    }
}

module spokes_negative()
{
    difference() {
        translate([0,0,-0.1]) cylinder(h=thicc+0.2, r= spokes_r);
        cylinder(h=100, r= spindle_r2, center=true);
        
        cube([2 * spokes_r, spoke_width, 100], center=true);
        rotate([0,0,90])
                cube([2 * spokes_r, spoke_width, 100], center=true);

        mounting_holes(r=12/2);            
    }    
    mounting_holes(r=2.2/2);
}

module mounting_holes(r=3.2/2,h=thicc+0.2) 
{
    for (i = [0:90:360]) {
        rotate([0,0,i])
        translate([0, 16, -0.1]) cylinder(h=h, r=r);
    }
}

module motor_cup()
{    
    difference() {
        cylinder(r = 38/2, h=5);
        translate([0,0,2]) cylinder(r=27/2+0.2, h=5); // outer rim of the black dvd thingy plate
        translate([0,0,-2]) cylinder(r=shaft_d/2, h=100);
        translate([0,0,-0.01]) main_wheel(thicc=thicc+0.1, shaft_d=shaft_d-0.01);
        mounting_holes(r=12.4/2);
        mounting_holes(h=100);
    }
}


module blablabla() 
{
    translate([0,0,-0.01]) linear_extrude(h=0.2) {
    rotate([0,0,90]) scale([-1.15,1,1]) translate([-50,0,0]) text("NIPKOW D178 32L", size=3.25, valign="center", font="Impact");
    rotate([0,0,90]) scale([-1.2,1,1]) translate([22,0,0]) text("SYNC OFS 1/4", size=3.25, valign="center", font="Impact");
    }
}