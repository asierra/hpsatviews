#ifndef HPSATVIEWS_HELP_ES_H
#define HPSATVIEWS_HELP_ES_H


/* =========================
 * Help general
 * ========================= */
static const char *HPSATVIEWS_HELP =
"Uso: hpsv <comando> <ancla> [opciones]\n"
"\n"
"El archivo netcdf <ancla> permite inferir la ruta y el instante de las "
"bandas necesarias.\n\n"
"Comandos:\n"
"  rgb                Composiciones multicanal (Color verdadero, AirMass, etc.).\n"
"  pseudocolor        Imagen con paleta de colores (CPT).\n"
"  gray               Imagen en escala de grises.\n"
"\n"
"Opciones comunes de salida y geometría:\n"
"  -o, --out <f>       Archivo de salida. Acepta patrones (ver abajo).\n"
"  -t, --geotiff       Salida en GeoTIFF (PNG por omisión).\n"
"  -c, --clip <val>    Recorte por clave o coordenadas.\n"
"  -r, --geographics   Reproyección a Lat/Lon.\n"
"  -s, --scale <n>     Factor de escala entero (negativo reduce).\n"
"  -a, --alpha         Añade canal alfa para regiones sin datos o umbral.\n"
"  -g, --gamma <valor> Corrección gamma.\n"
"  -h, --histo         Ecualización de histograma global. Si satura el\n"
"                      contraste, usar CLAHE.\n"
"  --clahe             Ecualización adaptativa de histograma (CLAHE) con\n"
"                      parámetros (8,8,4.0) por omisión.\n"
"  --clahe-params <params> Implica CLAHE con params. tiles_x,tiles_y,clip_limit.\n"
"\n"
"Patrones para --out y nombrado automático:\n"
"  Si se omite -o, el nombre sigue el patrón: hpsv_{SAT}_{TS}_{CH}[_{OPS}].{EXT}\n"
"  Patrones disponibles con -o:\n"
"    {SAT}  Satélite (G16, G19)     {CH} canal o banda (C01, C02, etc.)\n"
"    {TS}   Instante (YYYYJJJhhmm)  {YYYY} año, {MM} mes, {DD} día, {hh} hora,\n" 
"                                   {mm} minuto, {ss} segundo, {JJJ} día del año.\n"
"    {OPS}  Operaciones realizadas (-h, --clahe, -s, -r)\n"
"\n"
"Ejemplo:\n"
"  hpsv pseudo archivo.nc -o \"{SAT}_{CLIP}.png\" -c mexico\n"
"  -> G16_mexico.png\n"
"\n"
"Álgebra de Bandas (combinación lineal de bandas):\n"
"  --expr <f>      Suma y resta de términos \"a*CH1+b*CH2...\".\n"
"                  Soporta: +, - constantes numéricas y bandas (C01-C16).\n"
"                  Ej: \"2.0*C13-C15-200\" .\n"
"  --minmax <m>    Rango opcional [min,max] para ajustar el contraste.\n\n"
"Use 'hpsv help <comando>' para ayuda específica de un comando.\n";


/* =========================
 * Help comando: rgb
 * ========================= */
static const char *HPSATVIEWS_HELP_RGB =
"Uso: hpsv rgb <ancla> [opciones]\n"
"\n"
"Modos (--mode):\n"
"  daynite (def), truecolor, night, airmass, ash, so2, custom.\n"
"\n"
"Opciones RGB:\n"
"  --rayleigh      Corrección atmosférica (para truecolor/daynite).\n"
"  --ray-analytic  Usa corrección Rayleigh analítica (en lugar de LUTs).\n"
"  -f, --full-res  Usa la resolución nativa del canal más fino.\n"
"\n"
"El modo custom utiliza álgebra de bandas para cada canal. Se requieren\n"
"las opciones --expr y --minmax pero es preciso separarlos con punto y coma (;).\n"
"Ej: \"C13-C14; C13; -1.0*C15 + 300\".\n";

/* =========================
 * Help comando: pseudocolor / gray
 * ========================= */
static const char *HPSATVIEWS_HELP_PSEUDOCOLOR =
"Uso: hpsv pseudocolor <ancla> -p <paleta> [opciones]\n"
"\n"
"Opciones:\n"
"  -p, --cpt <nombre>  Paleta de colores (def. rainbow interno).\n"
"  -i, --invert        Invierte la polaridad de los datos.\n";

static const char *HPSATVIEWS_HELP_GRAY =
"Uso: hpsv gray <ancla> [opciones]\n"
"\n"
"Opciones:\n"
"  -i, --invert        Invierte escala (blanco es negro).\n";

#endif /* HPSATVIEWS_HELP_ES_H */
