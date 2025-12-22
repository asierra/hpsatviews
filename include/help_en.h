#ifndef HPSATVIEWS_HELP_EN_H
#define HPSATVIEWS_HELP_EN_H

/* =========================
 * General help
 * ========================= */
static const char *HPSATVIEWS_HELP =
"Usage: hpsatviews <command> [options]\n"
"\n"
"Commands:\n"
"  rgb          Generate an RGB image (e.g., true color or day/night composites).\n"
"  pseudo		Generate a color-mapped image from a single channel.\n"
"  gray         Generate a grayscale image from a single channel.\n"
"\n"
"Global options:\n"
"  --list-clips            List available predefined geographic clips.\n"
"\n"
"Common options (available for all commands):\n"
"  -o, --out <file>        Output file. If not specified, it is generated\n"
"                          automatically from the view content.\n"
"  -t, --geotiff           Generate GeoTIFF output (instead of PNG).\n"
"  -c, --clip <value>      Geographic clipping (key or coordinates).\n"
"  -g, --gamma <value>     Gamma correction (default: 1.0).\n"
"  -h, --histo             Apply global histogram equalization.\n"
"  --clahe                 Apply CLAHE (adaptive histogram equalization).\n"
"  --clahe-params <params> CLAHE parameters: \"tiles_x,tiles_y,clip_limit\".\n"
"  -s, --scale <factor>    Scale factor (default: 1).\n"
"  -a, --alpha             Add alpha channel.\n"
"  -r, --geographics       Reproject to geographic coordinates (lat/lon).\n"
"  -v, --verbose           Verbose mode.\n"
"\n"
"Automatic naming:\n"
"  If -o/--out is not specified, the output filename is generated automatically\n"
"  from the view content.\n"
"\n"
"  Form:\n"
"    hpsv_<SAT>_<INST>_<TYPE>_<BANDS>[_<OPS>].<ext>\n"
"\n"
"  Example:\n"
"    hpsatviews rgb anchor.nc --clahe --geographics\n"
"      -> hpsv_G16_2024223_183012_rgb_auto_clahe__geo.png\n"
"\n"
"Use 'hpsatviews help <command>' for command-specific help.\n";

/* =========================
 * Command help: rgb
 * ========================= */
static const char *HPSATVIEWS_HELP_RGB =
"Usage:\n"
"  hpsatviews rgb <anchor> [options]\n"
"\n"
"Description:\n"
"  Generates an RGB view from multiple channels, either as an explicit\n"
"  composition or as a predefined semantic RGB product.\n"
"\n"
"Command-specific options:\n"
"  --product <name>        RGB product (truecolor, airmass, ash, etc.).\n"
"\n";

/* =========================
 * Command help: pseudocolor
 * ========================= */
static const char *HPSATVIEWS_HELP_PSEUDOCOLOR =
"Usage:\n"
"  hpsatviews pseudocolor <anchor> [options]\n"
"\n"
"Description:\n"
"  Generates a pseudocolor view from a single channel using a color palette.\n"
"\n"
"Command-specific options:\n"
"  --palette <name>        Color palette to use.\n"
"\n";

/* =========================
 * Command help: gray
 * ========================= */
static const char *HPSATVIEWS_HELP_GRAY =
"Usage:\n"
"  hpsatviews gray <anchor> [options]\n"
"\n"
"Description:\n"
"  Generates a grayscale view from a single channel.\n"
"\n";

#endif /* HPSATVIEWS_HELP_EN_H */
