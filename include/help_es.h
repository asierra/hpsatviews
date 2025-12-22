#ifndef HPSATVIEWS_HELP_ES_H
#define HPSATVIEWS_HELP_ES_H

/* =========================
 * Help general
 * ========================= */
static const char *HPSATVIEWS_HELP =
"Uso: hpsatviews <comando> [opciones]\n"
"\n"
"Comandos:\n"
"  rgb          Genera una imagen RGB (p. ej. color verdadero).\n"
"  pseudo       Genera una imagen con mapa de colores.\n"
"  gray         Genera una imagen en escala de grises.\n"
"\n"
"Opciones globales:\n"
"  --list-clips            Muestra los recortes geográficos predefinidos disponibles.\n"
"\n"
"Opciones comunes (disponibles en todos los comandos):\n"
"  -o, --out <archivo>     Archivo de salida. Si no se especifica, se genera automáticamente.\n"
"  -t, --geotiff           Generar salida en formato GeoTIFF (en vez de PNG).\n"
"  -c, --clip <valor>      Recortar imagen (clave o coordenadas geográficas).\n"
"  -g, --gamma <valor>     Corrección gamma (defecto: 1.0).\n"
"  -h, --histo             Aplica ecualización de histograma global.\n"
"  --clahe                 Aplica CLAHE (ecualización adaptativa).\n"
"  --clahe-params <params> Parámetros CLAHE: \"tiles_x,tiles_y,clip_limit\".\n"
"  -s, --scale <factor>    Factor de escala (defecto: 1).\n"
"  -a, --alpha             Añade canal alfa.\n"
"  -r, --geographics       Reproyecta a coordenadas geográficas (lat/lon).\n"
"  -v, --verbose           Modo verboso.\n"
"\n"
"Nombrado automático:\n"
"  Si no se especifica -o/--out, el nombre del archivo se genera automáticamente\n"
"  a partir del contenido de la vista.\n"
"\n"
"  Forma:\n"
"    hpsv_<SAT>_<INST>_<TIPO>_<BANDAS>[_<OPS>].<ext>\n"
"\n"
"  Ejemplo:\n"
"    hpsatviews rgb ancla.nc --clahe --geographics\n"
"      -> hpsv_G16_2024223_183012_rgb_auto_clahe__geo.png\n"
"\n"
"Use 'hpsatviews help <comando>' para ayuda específica de un comando.\n";

/* =========================
 * Help comando: rgb
 * ========================= */
static const char *HPSATVIEWS_HELP_RGB =
"Uso:\n"
"  hpsatviews rgb <ancla> [opciones]\n"
"\n"
"Descripción:\n"
"  Genera una vista RGB a partir de múltiples canales. Puede producir composiciones\n"
"  explícitas o productos RGB semánticos predefinidos.\n"
"\n"
"Opciones específicas:\n"
"  --mode <nombre>      Producto RGB: daynite (defecto), truecolor, night, airmass, ash, so2.\n"
"  --rayleigh           Aplica corrección atmosférica de Rayleigh (solo modos truecolor/daynite).\n"
"  -f, --full-res       Usa el canal de mayor resolución como referencia. Por defecto, usa el menor.\n"
"\n";

/* =========================
 * Help comando: pseudocolor
 * ========================= */
static const char *HPSATVIEWS_HELP_PSEUDOCOLOR =
"Uso:\n"
"  hpsatviews pseudocolor <ancla> [opciones]\n"
"\n"
"Descripción:\n"
"  Genera una vista en pseudocolor a partir de un solo canal utilizando una\n"
"  paleta de colores.\n"
"\n"
"Opciones específicas:\n"
"  -p, --cpt <nombre>      Mapa de color a utilizar (defecto, arcoiris predefinido).\n"
"  -i, --invert            Invierte los valores (mínimo a máximo).\n"
"\n";

/* =========================
 * Help comando: gray
 * ========================= */
static const char *HPSATVIEWS_HELP_GRAY =
"Uso:\n"
"  hpsatviews gray <ancla> [opciones]\n"
"\n"
"Descripción:\n"
"  Genera una vista en escala de grises de un solo canal.\n"
"  -i, --invert            Invierte los valores (blanco a negro).\n"
"\n";

#endif /* HPSATVIEWS_HELP_ES_H */
