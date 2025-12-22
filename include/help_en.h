#ifndef HPSATVIEWS_HELP_EN_H
#define HPSATVIEWS_HELP_EN_H

/* =========================
 * General help
 * ========================= */
static const char *HPSATVIEWS_HELP =
"Usage: hpsatviews <command> <anchor> [options]\n"
"\n"
"Commands:\n"
"  rgb                Multichannel composites (True Color, AirMass, etc.).\n"
"  pseudocolor, pseudo  Single channel image with color palette (CPT).\n"
"  gray               Grayscale image.\n"
"\n"
"Common Output and Geometry Options:\n"
"  -o, --out <f>       Output file. Accepts patterns (see below).\n"
"  -t, --geotiff       GeoTIFF output (default: PNG).\n"
"  -c, --clip <val>    Crop by key name or coordinates.\n"
"  -r, --geographics   Reprojection to Lat/Lon.\n"
"  -s, --scale <n>     Scale factor.\n"
"\n"
"Patterns for --out and Automatic Naming:\n"
"  If -o is omitted, the name follows: hpsv_{SAT}_{TS}_{CH}[_{OPS}].{EXT}\n"
"  Available patterns:\n"
"    {SAT}  Satellite (G16, G19)     {CH} channel or band (C01, C02, etc.)\n"
"    {TS}   Instant (YYYYJJJhhmm)    {YYYY} year {MM} month {DD} day ...\n"
"    {OPS}  Operations performed (-h, --clahe, -r, s)\n"
"\n"
"Example:\n"
"  hpsatviews pseudo file.nc -o \"{SAT}_{CLIP}.png\" -c mexico\n"
"  -> G16_mexico.png\n"
"\n"
"Band Algebra Options:\n"
"  --expr <f>      Formula to calculate pixel value.\n"
"                  Supports: +, -, *, numeric constants, and bands (C01-C16).\n"
"                  Ex: \"C13-C14\" (Ash detection).\n"
"  --minmax <m>    Range [min,max] to adjust the palette.\n"
"                  If omitted, defaults to 0,255.\n"
"\n"
"Use 'hpsatviews help <command>' for command-specific help.\n";


/* =========================
 * Command help: rgb
 * ========================= */
static const char *HPSATVIEWS_HELP_RGB =
"Usage: hpsatviews rgb <anchor> [options]\n"
"\n"
"Modes (--mode):\n"
"  daynite (def), truecolor, night, airmass, ash, so2, custom.\n"
"\n"
"General RGB Options:\n"
"  --rayleigh      Atmospheric correction (for truecolor/daynite).\n"
"  -f, --full-res  Use native resolution of the finest channel.\n"
"\n"
"Custom mode uses band algebra for each channel. The options --expr and\n"
"minmax are required but components must be separated by semicolons (;).\n"
"Ex: \"C13-C14; C13; -1.0*C15 + 300\".\n";


/* =========================
 * Command help: pseudocolor
 * ========================= */
static const char *HPSATVIEWS_HELP_PSEUDOCOLOR =
"Usage: hpsatviews pseudocolor <anchor> [options]\n"
"\n"
"Generates an image applying a color palette (CPT).\n"
"\n"
"Options:\n"
"  -p, --cpt <f>   Palette file .cpt (or internal name).\n"
"  -i, --invert    Invert palette order.\n";


/* =========================
 * Command help: gray
 * ========================= */
static const char *HPSATVIEWS_HELP_GRAY =
"Usage: hpsatviews gray <anchor> [options]\n"
"\n"
"Generates a grayscale image.\n"
"\n"
"Options:\n"
"  -i, --invert    Invert scale (White <-> Black).\n";

#endif /* HPSATVIEWS_HELP_EN_H */
