# Plan de implementación: salida STAC en hpsatviews

## Contexto

STAC (*SpatioTemporal Asset Catalog*) no es un formato de archivo: es una
especificación —modelo de datos, grafo de enlaces y semántica de HTTP, con
esquemas JSON que se validan—. El objetivo es que `hpsv` emita un **`Item` de
STAC** por escena procesada, de modo que el acervo del laboratorio se vuelva
indexable con herramientas estándar (STAC Browser, QGIS, `pystac-client`,
TiTiler) en lugar de con lectores de la casa.

Referencia [STAC SpatioTemporal Asset Catalog](shttps://stacspec.org/en)

La discusión que originó este plan está en el rediseño del portal:
`~/Dropbox/cca/lanot/Portal/spec-portal_lanot.md` §4.4 («STAC, y en qué nivel se
adopta») y la nota `nota_catalogo_django.md`, decisión 4. De ahí viene la
frontera que este plan respeta:

> El `Item` lo emite la herramienta que escribe el producto, en el instante en
> que conoce todo. La `Collection`, el `Catalog` y el índice son de un servicio
> aparte. Un programa que escribe un archivo no puede mantener un grafo de
> enlaces sobre todo el acervo.

**El cambio de fondo no es de formato, es de retención.** Hoy el sidecar es un
subproducto que se tira: se emite con `-j`, lo consume `mapdrawer` y se borra.
Adoptar STAC significa decidir que la descripción del producto **se conserva**,
con su propio ciclo de vida.

## Estado actual (verificado 2026-09-11)

El 90 % del contenido de un `Item` ya existe en `MetadataContext`.

| STAC pide | Dónde está hoy |
|---|---|
| `datetime` ISO 8601 UTC | `ctx->time_iso`, `strftime("%Y-%m-%dT%H:%M:%SZ")` en `metadata_from_nc()` (`src/metadata.c`) |
| `bbox` en orden `[W,S,E,N]` | Ya sale en ese orden: los tres llamadores pasan mínimos primero (`src/processing.c:455`, `:579`, `src/rgb.c:1488`) |
| `proj:wkt2`, `proj:transform`, `proj:shape` | `get_projection_wkt()` arma el PROJ exacto (`+proj=geos +sweep=x +lon_0=… +h=…`) y `GDALSetGeoTransform()` tiene la transformación (`src/writer_geotiff.c:90-110`, `:245`). **Es `static` y no sale del escritor.** |
| `eo:bands`, estadísticas por banda | `ctx->channels[]` con `name`, `quantity`, `min`, `max`, `unit` |
| `platform`, `instruments` | `metadata_sat_name()` → `G16`; son campos del núcleo de STAC, no extensión |
| `processing:software` | `tool` + `HPSV_VERSION` (`include/version.h`) |
| `id` estable | `metadata_build_filename()` ya produce `hpsv_G16_conus_2026254_1800_rgb_…` |
| Activos legibles en la nube | `--cog`, controlador COG de GDAL con `COMPRESS=ZSTD` (`src/writer_geotiff.c:265-276`) |
| Serializador | `src/writer_json.c` |

Y el ciclo de vida actual, que es lo que hay que cambiar:

* El sidecar es **opcional**: sólo con `-j` / `--json` (`include/help_en.h:36`).
* **No se conserva.** `LANOT_tools/GLMconus_png.sh:50` no lo pide siquiera: con
  `-t`, el GeoTIFF lleva su georreferencia y `mapdrawer` la lee con rasterio; el
  intermedio se borra en `:68`. `--metadata` de `mapdrawer` está documentado
  para «imágenes sin georreferencia», que es su único caso real.
* Lo que **sí** sobrevive junto al producto son las etiquetas GDAL del GeoTIFF
  (`tool`, `satellite`, `sector`, `band`, `scan_time`, `product`,
  `colormap_*`) y la convención del nombre de archivo.
* Lo que se pierde en cada corrida: `channels[]` (magnitud física, unidades,
  rango real) y `enhancements` (gamma, CLAHE, Rayleigh). En la línea de PNG no
  queda descripción alguna.

### Correcciones al estado verificado (2026-09-12)

La lectura del código obligó a corregir seis afirmaciones de la tabla de arriba:

1. **El limbo del disco completo deja de ser el riesgo técnico principal.** GDAL
   se enlaza **incondicionalmente** (`Makefile:31-39`, sin `HAVE_GDAL` ni
   `#ifdef`) y `src/writer_geotiff.c` ya usa la API **C** de OSR, así que
   `OCTNewCoordinateTransformation()` está disponible sin tocar el build: basta
   recorrer el borde de la imagen y descartar los puntos que GDAL marque como
   fallidos —la opción (a) de la fase 1—, sin escribir matemática propia. Dos
   cuidados: `OSRSetAxisMappingStrategy(…, OAMS_TRADITIONAL_GIS_ORDER)` sobre el
   SRS 4326, porque GDAL 3 devuelve lat,lon por omisión y voltearía el `bbox`
   en silencio; y reusar la misma cadena PROJ de `writer_geotiff.c:100` para que
   la geometría del ítem case con el CRS del GeoTIFF.
2. **`get_projection_wkt()` exporta WKT1**, no WKT2 (`OSRExportToWkt`,
   `src/writer_geotiff.c:91-119`). `proj:wkt2` exige `OSRExportToWktEx` con
   `FORMAT=WKT2_2019`.
3. **La cadena PROJ no es «el PROJ exacto» que decía la tabla.** `+sweep=x` está
   incrustado y `sweep_angle_axis` **no se lee nunca** del NetCDF; el elipsoide
   es `+ellps=GRS80` fijo e ignora `semi_major`/`semi_minor`, que sí se leen
   (`src/reader_nc.c:196-202`). Correcto para ABI, falso como afirmación
   general, y conviene cerrarlo antes de publicar el WKT como metadato.
4. **`bbox` es `float[4]`** (`src/metadata.c:52`) y se imprime con `%.8g`
   (`src/writer_json.c:138`): sobra en metros, queda al límite en grados (~1 m).
   Para el `Item` hay que pasarlo a `double`.
5. **El consumidor real del sidecar no es `GLMconus_png.sh`** sino
   `LANOT_procesamiento_goes/crea_rgbs_products.sh:177-180,207`, que corre
   `hpsv rgb -j -B` y **espera dos sidecars** (`.json` y `_geo.json`) cuando
   `hpsv` escribe uno solo. Sólo los usa como temporales y los borra, así que
   nada se rompe hoy; es la forma concreta del defecto que resuelve D1.
6. **`channels[]` sólo se llena con un canal, también en `rgb`**:
   `metadata_from_nc()` se llama una vez, con el canal de referencia
   (`src/rgb.c:1275`). `eo:bands` de la fase 3 necesitará los tres.

Pendiente menor detectado: `compute_lalo()` (`src/reader_nc.c:369`) es código
muerto que lee globales nunca inicializadas y devuelve NaN fuera del limbo sin
guarda alguna. Borrarlo o cablearlo antes de que alguien lo tome por la inversa
analítica disponible; la viva es la del bucle de `compute_navigation_nc()`
(`src/reader_nc.c:537-565`), que sí protege el limbo.

**Consecuencia para el barrido retroactivo:** de lo ya producido se puede
reconstruir un `Item` desde el GeoTIFF (geotransformación, WKT, etiquetas,
nombre), pero **sin `channels` ni `enhancements`**; de los PNG no se reconstruye
nada. Recuperarlo completo exige reprocesar desde el NetCDF. Es el argumento
para empezar a emitir pronto.

## Tres decisiones previas al código

Ninguna es técnica; las tres cambian el diseño. **Resolverlas antes de la fase 3.**

### D1. Qué es un `Item` y qué es un activo

El constructor de nombres codifica las operaciones —modo, gamma, CLAHE— en el
identificador. Si cada variante es un `Item`, una búsqueda por fecha devuelve la
misma escena repetida.

**Propuesta:** `Item` = **escena** (satélite, sector, instante, producto); las
variantes —`truecolor`, `ash`, `night`; PNG y COG; realces— son **activos** de
ese ítem, con la operación en la llave del activo. Como el sidecar actual es por
corrida y desechable, **nadie depende de la semántica por corrida**: adoptar la
identidad por escena no rompe nada hoy.

Implica que el ítem se escribe **después** de todas las salidas de la corrida, y
que dos corridas sobre la misma escena deben **fusionar** activos, no
sobrescribir. Definir dónde vive esa fusión: en `hpsv` (releyendo el ítem si
existe) o en el indizador. Lo más simple y honesto es en `hpsv`, leyendo y
reescribiendo el ítem si ya está en el directorio de salida.

**Resuelta el 2026-09-12: `Item` = escena *más* modo/producto.** El `id` lleva
el modo (`hpsv_G16_conus_2024220_1301_ash`) y la `Collection` es el producto,
que es lo que fija el portal §4.4 (`Collection` ↔ `Producto`). Los activos son
las variantes de **formato y rejilla** —COG/PNG, fija/geográfica—, no los modos:
un `Item` pertenece a una sola colección, así que colgar `ash` y `truecolor` del
mismo ítem dejaría la colección sin correspondencia con `Producto`.

Con esta identidad **una corrida emite su ítem completo y no hay que fusionar
nada**, que es la consecuencia que más código ahorra: hoy `src/writer_json.c` es
sólo emisor y no existe ningún lector de JSON en el árbol (el único precedente
de releer lo que uno escribió es `effective_path()` en `src/timing.c:223-250`).

Consecuencias verificadas en el código, y qué hacer con cada una:

1. **`metadata_build_filename()` no sirve como `id`.** Codifica gamma, CLAHE,
   clip e inversión en el nombre vía `build_ops_string()`
   (`src/metadata.c:232-306`). El `id` necesita una función hermana que corte en
   `hpsv_<SAT>[_<SECTOR>]_<YYYYJJJ_hhmm>_<TIPO>`, sin el segmento de
   operaciones. Los realces son propiedad del activo, no de la escena.
2. **El ítem se escribe donde hoy se escribe el sidecar**, `save_sidecar_json()`
   en `src/main.c:99`: es el único punto del programa donde todas las imágenes
   ya están en disco y `meta` y `cfg` siguen vivos.
3. **`MetadataContext` es de un solo valor donde tiene que ser de varios.** Con
   `-B` la geometría se escribe dos veces y la segunda pisa a la primera
   (`processing.c:455` en metros, luego `:579` en grados; `rgb.c:1487` evita el
   doble registro ramificando, y entonces sólo guarda la reproyectada), y
   `output_file`/`output_width`/`output_height` se añaden una sola vez, al final,
   con el nombre `_geo`. El sidecar que resulta **lleva nombre de rejilla fija y
   contenido geográfico**. Para que los dos archivos sean activos del mismo ítem
   hace falta registrar una *lista* de salidas —ruta, ancho, alto, proyección,
   bbox, papel— en vez de las claves `output_*`.
4. **Las rutas no pueden viajar por `extra_fields`.** `metadata_add_str()`
   trunca a 63 caracteres (`src/metadata.c:198-204`, `val_s[64]`) y descarta en
   silencio a partir de la entrada 32 (`MAX_KV`). Un `href` de activo se corta
   sin aviso; la lista del punto 3 necesita almacenamiento propio.

### D2. Retención y ciclo de vida

Preguntas a responder por escrito antes de emitir:

* ¿Dónde viven los ítems: junto al producto, o en un árbol aparte? Junto al producto.
* ¿Quién los purga? El portal fija que la retención es política local del
  receptor (§3 de la especificación del portal). Un cron independiente.
* ¿Viajan con el producto al sitio de publicación? Sí.
* **¿Qué pasa cuando el producto se borra y el ítem sobrevive?** *(resuelta el
  2026-09-12)* **Fuera de `hpsv`.** La herramienta nunca borra: emite el ítem con
  sus activos presentes y ahí termina su responsabilidad. La distinción que se
  quiere ganar —«no hay ítem» → nunca se capturó; «hay ítem sin activo
  alcanzable» → se capturó y se eliminó— se sostiene sola con que el purgador
  conserve el ítem, y **no exige campo alguno del emisor**. Marcar el activo
  como inalcanzable es trabajo del cron de purga y del indizador, y la
  convención (`hpsv:asset_state` o equivalente) se declara a nivel de colección.
  No toca el código de `hpsv`.

Esa última es la más valiosa. Hoy el estado `sin_datos` del portal admite tres
lecturas indistinguibles (§6.7), y la eliminación del acervo L1b de 2018 es
irreversible y muda. Con ítems conservados al purgar, se parte en dos respuestas
distinguibles: **no hay ítem** → nunca se capturó; **hay ítem sin activo
alcanzable** → se capturó y se eliminó.

STAC **no tiene campo estándar** para «el activo ya no existe», así que eso es
una convención propia (`hpsv:asset_state` o equivalente a nivel de colección),
no un `404` esperando interpretación.

### D3. Fusionar el sidecar o emitir dos archivos

**Propuesta: fusionar.** El sidecar no tiene base instalada —es opcional,
efímero y su único consumidor está en `LANOT_tools`, del mismo laboratorio—, así
que sostener dos formatos no protege ningún contrato real. El `Item` sustituye
al sidecar y `-j` pasa a emitirlo. 

No puede ser aditivo: `geometry` ya está ocupada con otro significado
(`{projection, bbox}`) y STAC la reserva para una geometría GeoJSON. `crs` y
`bounds` de la raíz no son campos de STAC.

**Resuelta el 2026-09-12: fusionar**, y corregir `mapdrawer` en la fase 4.

Lo que hay que cambiar en `mapdrawer` (fase 4), con renglón:

| Sitio | Hoy | Con `Item` |
|---|---|---|
| `mapdrawer.py:1180` | `metadata.get('bounds')` | `item["bbox"]` (mismo orden) |
| `mapdrawer.py:1419` | `bounds` para `--lat-south` | idem |
| `mapdrawer.py:1432-1436` | `crs`, con error si falta | `properties["proj:wkt2"]` |
| `mapdrawer.py:1445-1446` | **asigna** `crs` y `bounds` tras reproyectar | trabaja sobre un diccionario propio, no sobre el ítem |

**Beneficio que paga el cambio:** `mapdrawer.py:63-66` mantiene su propia tabla
`GOES_PROJECTIONS` con los PROJ de goes16/17/18/19, y **no coincide** con la de
`hpsv`: fija `+a=6378137.0 +b=6356752.31414` y `+h`/`+lon_0` a mano, mientras
`get_projection_wkt()` los lee del archivo (`proj_info.lon_origin`,
`sat_height`) con `+ellps=GRS80`. Hoy el alias `"goes16"` funciona porque dos
repositorios sincronizan tablas privadas a mano. Con `proj:wkt2` en el ítem, esa
tabla se borra.

## Defectos que se cerraron primero (fase 0, 2026-09-12)

*Diagnóstico original, conservado como registro; los tres están cerrados y
la fase 0 de abajo dice cómo.*

1. **El esquema ya se separó del escritor.** `docs/hpsatviews.schema.json` exige
   `timestamp_iso` y `metadata_save_json()` escribe `timestamp`; el esquema
   describe `satellite` como `"goes-16"` y el código emite `"G16"` (que es lo
   que verifica `tests/test_json.sh:24`); el enum de `command` incluye
   `composite`, que no es subcomando. **Estaba documentado como trampa en
   `CLAUDE.md:142`**. El sidecar no pasaba su propio esquema declarado: se
   comprobó con `/tmp/m.json`, un disco completo de G19 emitido con `-t -j`.
2. **El orden del `bbox` está mal documentado.** `include/metadata.h:51` y la
   descripción del esquema dicen `[lon_min, lat_max, lon_max, lat_min]`; los
   tres llamadores pasan mínimos primero. Los llamadores tienen razón —ese es el
   orden de GeoJSON y de STAC— y lo que está mal es la documentación. Corregir
   antes de que alguien escriba un cliente creyéndole al comentario.
3. **Nada valida contra el esquema.** `tests/test_json.sh` verifica claves con
   `grep`. Lo que STAC compra es que un consumidor ajeno le crea al esquema, y
   eso se sostiene con validación en la suite. Sin cerrar este punto, adoptar
   STAC produce dos esquemas separándose en vez de uno.

## Fases

### Fase 0 — Sanear el esquema y validar (sin STAC todavía) — **HECHA 2026-09-12**

Los tres defectos quedaron cerrados:

1. **El esquema se realineó con el emisor**, no al revés: `timestamp_iso` →
   `timestamp`, fuera `composite` del enum de `command`, `satellite` descrito
   como `G16`…`G19` (el nombre canónico `goes-16` llegará en `platform`),
   declaradas las claves raíz que se escribían sin declarar (`sector`,
   `product`, `crs`, `bounds`), declarado el vocabulario real de `enhancements`
   —quince claves, y `gamma` que puede ser número **o** cadena cuando los tres
   gammas de `rgb` difieren (`rgb.c:1201-1203`)—, y `geometry` bajado a
   opcional: una corrida `gray` sin `-c`, `-t` ni `-G` no carga navegación
   (`processing.c:291`) y legítimamente no emite geometría.
2. **El orden del `bbox` está corregido** en `include/metadata.h` y en el
   README: es `[x_min, y_min, x_max, y_max]` = `[W,S,E,N]`, y las unidades
   siguen a `crs` (grados reproyectado, metros en rejilla fija).
3. **La suite valida de verdad.** `tests/test_json.sh` corre cada sidecar que
   produce contra `docs/hpsatviews.schema.json` con `python3-jsonschema`
   (dependencia dura, no salto silencioso: `CLAUDE.md` documenta cómo un `SKIP`
   contado como aprobado hizo que la suite CUDA mintiera un 9/9). Se añadieron
   casos para las dos ramas de geometría y para `rgb`. El validador se probó
   contra tres sidecars rotos a mano y los rechaza.

Y de paso, dos defectos del emisor descubiertos al verificar:

* **`rgb` nunca registraba su `command`**: `metadata_set_command()` sólo se
  llamaba desde `processing.c:73`, así que los sidecars de `rgb` salían sin la
  clave y `metadata_build_filename()` se quedaba en el literal `"output"` como
  tipo de producto (`hpsv_G16_conus_2024220_1302_output_C13.png` en vez de
  `…_ash.png`). Corregido en `src/rgb.c`, con regresión en `test_json.sh`.
* **El segfault que `crea_rgbs_products.sh:202` atribuye al sidecar no es del
  sidecar.** Es `rgb -B` junto con `-s`: el bloque de rejilla fija escalaba
  `ctx.final_image` *en sitio* y después la reproyección recorría esa imagen ya
  reducida con la geotransformación del canal de referencia, que sigue
  describiendo la rejilla sin escalar → lectura fuera de límites. `gray` y
  `pseudocolor` no lo tienen porque `processing.c:405-412` escala una copia
  local. Corregido con el mismo patrón, y `-s` ahora se aplica a las dos
  salidas, como en `processing.c`. Conviene avisar a producción para que retire
  el comentario y deje de ignorar el código de salida.

### Fase 1 — La huella en EPSG:4326, siempre — **HECHA 2026-09-12**

`src/footprint.c` calcula la huella geográfica de **toda** salida y la emite en
el sidecar como `bbox_4326` (recuadro `[W,S,E,N]`) y `footprint` (GeoJSON
`Polygon`). Son claves **aditivas**: `crs`, `bounds` y `geometry` siguen
significando exactamente lo que significaban, así que `mapdrawer` no se entera
hasta la fase 4, y la fase 3 sólo tiene que mapear `bbox_4326` → `bbox` y
`bounds` → `proj:bbox`.

**Cómo se resolvió el limbo.** No con ninguna de las dos opciones que este
documento proponía. Se camina el borde del ráster y, donde una muestra cae fuera
del disco visible, se lleva por bisección hacia el centro hasta volver a la
Tierra; donde un borde *cruza* el limbo se biseca además **a lo largo del
borde** para clavar esa esquina. Lo segundo no era opcional: la esquina noroeste
de un sector CONUS sí se sale del disco, y redondearla hacia el centro costaba
medio grado de latitud. Una sola rutina sirve para disco completo, sector y
recorte.

**Y no usa PROJ.** La versión con `OCTTransform` de GDAL —la que este documento
recomendaba tras verificar que GDAL ya está enlazado— funcionó y dio las mismas
coordenadas, pero gastaba ~17 ms en construir el *pipeline* de PROJ, la décima
parte de una corrida pequeña, para 260 puntos. Se sustituyó por la misma forma
cerrada que `compute_navigation_nc()` ya ejecuta por píxel
(`src/reader_nc.c:537-570`). Las dos coinciden en 3e-06° (36 cm, contra píxeles
de 500 m) y la analítica cuesta **menos de 1 ms**, medido con `LOG_TIMING` tanto
en CONUS como en disco completo.

Comprobaciones de cordura, las que este plan pedía:

| Caso | `bbox_4326` |
|---|---|
| G19 disco completo (`lon_0 = -75`) | `[-156.2995, -81.3282, 6.2994, 81.3282]`, o sea `lon_0 ± 81.30°` |
| G18 disco completo (`lon_0 = -137`) | `[141.7005, -81.3282, -55.7006, 81.3282]`, **cruza el antimeridiano** |
| G16 CONUS | `[-152.1135, 14.5618, -52.9183, 56.7807]`, 130 vértices, 2 cruces de borde |

**El antimeridiano era una trampa que el plan no había visto.** Las longitudes
se acumulan como diferencias respecto al meridiano subsatelital, no en crudo: un
disco de GOES-West va de −218° a −56°, cuyo mínimo y máximo ingenuos son
`[-179, +176]`, es decir el planeta entero. `ring_bbox()` pliega el envolvimiento
a la convención de STAC —oeste > este— y marca `crosses_antimeridian`.

**Caso sin geometría: ya no existe.** La huella no necesita rejilla de
navegación, sólo los parámetros de proyección del archivo, así que se emite
incluso en el PNG pelado que hoy no lleva `crs` ni `bounds`. La fase 3 ya no
tiene que hacer fallar a `--stac` por falta de geometría, salvo con archivos sin
proyección utilizable.

Trabajo adicional que entró con la fase:

* `src/projection.c` es ahora el único sitio que arma el SRS; `get_projection_wkt()`
  era `static` en `src/writer_geotiff.c`. De paso, `+sweep` se **lee** del archivo
  (`sweep_angle_axis`) en vez de estar incrustado. El GeoTIFF sale byte a byte idéntico.
* `MetadataContext.bbox` pasó de `float` a `double`, como pedía la corrección 4.
* **Bug corregido**: con `-s`, el `bounds` en metros de la rejilla fija salía a un
  cuarto de la extensión real (`processing.c` multiplicaba el ancho ya reducido
  por el tamaño de píxel sin escalar), contradiciendo la geotransformación del
  GeoTIFF escrito al lado. Con prueba de regresión en `tests/test_json.sh`.
* **Desbordamiento latente cerrado** en `src/writer_json.c`: `check_comma()`
  indexaba `needs_comma[depth]` sin tope mientras los `begin_*` incrementaban
  `depth` sin límite. No se alcanzaba con tres niveles de anidamiento, pero el
  polígono le dio a GCC un camino concreto y lo delató.

### Fase 2 — Cablear la proyección hasta el contexto — **HECHA 2026-09-12**

El sidecar lleva ahora cuatro claves más, aditivas como las de la fase 1 y
nombradas ya por la extensión de STAC a la que alimentan: `proj_transform`,
`proj_shape`, `proj_epsg` y `proj_wkt2`. La fase 3 sólo tiene que renombrarlas
con dos puntos.

* **`proj_transform` va en el orden de STAC, que no es el de GDAL**: ancho de
  píxel, rotación de fila, origen x, rotación de columna, alto de píxel, origen
  y. En la rejilla fija se emite en metros, con el recorte y el remuestreo ya
  aplicados; se verifica en la suite contra la geotransformación del GeoTIFF
  escrito al lado, en seis variantes, y coincide al límite del redondeo del JSON.
* **`proj_shape` es `[alto, ancho]`.** En `rgb` hubo que moverlo *después* de
  `apply_scaling()`: el bloque que fija `crs`/`bounds` corre antes del
  remuestreo, así que habría descrito una imagen que nunca se escribió.
* **`proj_epsg`** es 4326 reproyectado y se omite en la rejilla fija, que no
  tiene código de autoridad.
* **`proj_wkt2`** sale de `OSRExportToWktEx` con `FORMAT=WKT2_2019`; lo que
  `projection_wkt_from_nc()` devolvía era WKT1.

**El trabajo de fondo fue quitar una duplicación.** El ajuste de la
geotransformación por recorte y escala estaba copiado dentro de la rama
`is_geotiff` de cada escritor —dos veces en `processing.c`, una en `rgb.c`— y
sólo ahí, que es por lo que una salida PNG no tenía transformación en ninguna
parte y por lo que el `bounds` en metros discrepaba del GeoTIFF bajo `-s`. Ahora
vive en `projection_output_geotransform()`. Los GeoTIFF salen byte a byte
idénticos en las ocho variantes comprobadas.

**Coste.** `-j` llegó a `ProcessConfig` (vivía sólo en `main.c` vía
`ap_found()`), y la huella y la rejilla se calculan únicamente cuando se van a
escribir. El WKT2 es lo único que necesita GDAL: **6 ms** la primera vez que un
proceso lo toca, y **0 ms** escribiendo GeoTIFF, porque el escritor ya creó el
SRS. La cadena de producción, que usa `-t -j`, no paga nada.

**Pendiente menor:** el CRS de la rejilla fija sale como `PROJCRS["unknown"]`,
porque se arma desde una cadena PROJ.4 sin autoridad. Los parámetros están
completos y es de ellos de lo que dependen los clientes, pero un nombre propio
—«GOES-R ABI fixed grid»— mejoraría la ficha del catálogo. Cambiarlo altera los
bytes de los GeoTIFF ya producidos, así que no se tocó.

**Y no confundirlo con esto** (medido el 2026-09-12, al escribir el lector de la
fase 4): `rasterio`/`pyproj` dicen que el CRS del `Item` **no es igual** al del
GeoTIFF en rejilla fija, aunque los parámetros sean idénticos y los puntos de
prueba caigan en el mismo sitio con 0.000000 m de diferencia. La causa no es el
nombre ni los nombres de eje: es que **al leer el GeoTIFF, GDAL reconstruye el
CRS desde las claves TIFF y le añade `REMARK["PROJ CRS string: +proj=geos …"]`**,
que el WKT2 del `Item` no lleva, y `pyproj` cuenta el remark como parte del CRS.
Aislado: quitando sólo el remark, `==` da verdadero; igualando sólo los nombres
de eje, sigue dando falso. No hay nada que arreglar en `hpsv` —el remark lo
sintetiza el lector de GDAL— y ponerle nombre al CRS **no** lo resolvería. Lo
que hay que corregir, si acaso, es la comparación: `CRS.to_dict()` compara los
parámetros e ignora remarks y nombres de eje.

### Fase 3 — Emitir el `Item` — **HECHA 2026-09-12**

`-j` escribe ahora un `Item` de STAC en vez del sidecar propio, que es lo que D3
decidió. Se verificó antes que nada en producción lo lee: `crea_rgbs_products.sh`
lo pide con `-j` pero sólo para borrarlo, y ningún otro guion usa la bandera. Lo
único que queda roto es `mapdrawer --metadata`, opcional y documentado para
imágenes sin georreferencia, que corrige la fase 4.

**Nombre e identidad.** El archivo se nombra por el `id`, **no** por `-o`: el
`id` es la escena más el producto, sin el segmento de realces, así que dos
corridas sobre la misma escena convergen en un solo Item en vez de dispersar uno
por renderización. `metadata_build_id()` y `metadata_build_filename()` comparten
el mismo tronco (`build_stem()`) y se separan justo ahí. El directorio sí viene
de `-o`, porque es donde quedó el producto. Hay prueba de regresión: una corrida
con `--clahe -g 1.5` tiene que caer en el mismo archivo.

**`--stac-collection` es opcional.** Sin ella el Item no lleva `collection`, que
es STAC válido y deja que el indizador la asigne al ingerir. Hacerla obligatoria
habría roto toda invocación de `-j` existente, y derivarla sola habría fijado un
identificador público que el portal §4.4 advierte que no se renombra sin romper a
quien lo citó.

**Los activos llevan su propia rejilla, y eso no estaba previsto.** Una corrida
con `-B` escribe dos rásteres en rejillas distintas, y el `proj:transform` del
Item sólo puede describir uno: sin `proj:transform` por activo, un cliente
georreferenciaría mal el otro — y `-B` es justo lo que usa producción. Cada
activo lleva ahora `proj:shape`, `proj:transform` y, cuando aplica, `proj:epsg`,
y la suite abre cada activo declarado y los compara contra el archivo real.

**La huella de una reproyección ya no es su rectángulo.** El ráster reproyectado
es un rectángulo en lat/lon, pero el dato de dentro no: las esquinas de un disco
completo reproyectado son nodato. Declararlas haría que una búsqueda de catálogo
coincidiera con mar abierto, así que sin `-c` la geometría es el anillo curvo de
la rejilla de origen; con `-c` el recuadro sí es la huella, porque el recorte es
él mismo una caja en lat/lon.

**Decisiones que el plan ya fijaba y se respetaron:** `links` sale vacío, porque
construir `self`/`root`/`parent` exige saber dónde se publicará el archivo; no
hay bandera de URL raíz; los rangos físicos van en `hpsv:channels` y **no** en
`raster:bands` de un activo, porque los activos son de 8 bits y colgarles
kelvin afirmaría algo falso; el tipo de medio sigue a `--cog`.

**`eo:bands` lista los tres canales**, no sólo el de referencia:
`metadata_from_nc()` se llama ahora por cada canal cargado. Un `ash` declara
C11, C13, C14 y C15. Los `common_name` de STAC sólo se emiten en las bandas
reflectivas, donde son inequívocos; inventarle uno a las térmicas de ABI
afirmaría algo falso a quien filtre por ello.

**Sin huella no hay Item**: si el archivo no trae proyección utilizable, `-j`
falla con mensaje claro y el código de salida lo refleja, en vez de escribir un
huérfano sin `geometry`.

**Deuda declarada.** Las versiones del núcleo y de cada extensión están
`#define`adas juntas al principio de `metadata_save_stac_item()` precisamente
para que revalidarlas sea una sola edición; hoy son 1.0.0 y v1.1.0, elegidas por
ser las de despliegue más amplio, y **no se han validado contra los esquemas
vivos** — eso es la fase 5. `docs/hpsatviews.schema.json` se borró, porque
describía un formato que ya no se emite, y lo sustituye
`docs/stac/hpsv-item.schema.json`, que cubre lo que esta herramienta emite y
controla.

### Fase 4 — `mapdrawer` lee el `Item` — **HECHA 2026-09-12**

LANOT_tools `a7c0037` (rama `stac-fase4`) y LANOT_procesamiento_goes `eafc182`
(rama `meso-item-stac`). Sin fusionar en `main` ni desplegar.

**La tabla de D3 estaba mal en lo esencial.** El `bbox` de la raíz del Item es
la huella en 4326 —la curva del limbo—, no la extensión del ráster. Los límites
de la imagen, en su propio CRS (metros en rejilla fija), salen de
`proj:transform` + `proj:shape` **del activo**. Leer `bbox` habría desalineado
todas las capas. Tampoco basta `properties["proj:wkt2"]`: con `-B` describe sólo
la última salida escrita.

**Los cuatro sitios de `mapdrawer` no cambiaron.** Toda la conversión cabe en
`Metadata.from_stac_item()` (`LANOT_tools/metadata.py`), que entrega los mismos
`crs` y `bounds` internos, así que `make_south_room`, `--o_crs` y
`set_projected_bounds` siguen igual. `from_json_file()` se conserva para el
formato plano que aún escriben `geotiff2view --save-metadata` y el sidecar de
`mapdrawer --o_crs`.

* El activo se elige por `href` = nombre de la imagen; el `proj:*` del Item sólo
  se cree cuando hay un solo activo.
* CRS: `proj:wkt2` del activo → `proj:epsg` del activo → los del Item (un solo
  activo). Sin CRS, error: no se inventa.
* `-m` sólo acepta el Item: un JSON plano o un archivo ausente salen con 1, en
  vez de ignorarse en silencio como antes.
* `platform` se lee en forma corta (`goes-19` → `G19`), la convención de CIMSS y
  CIRA y la de los nombres de archivo.

**El orden «primero el emisor» se confirmó en la práctica.** Con `-B` el activo
de rejilla fija no declaraba CRS y el lector no podía georreferenciarlo; lo cerró
`a6e97dd`, y los fixtures de LANOT_tools se regeneraron con ese binario.

**`GOES_PROJECTIONS` se borró** de `mapdrawer.py` y de su copia en
`glm_renderer.py`; `--crs goes16` ya no existe. Una prueba deja constancia de que
el CRS del Item y la tabla vieja ponen los puntos a menos de 1 m.

**Hallazgo lateral: el grosor de `--layer`.** Desde `f50b864` (LANOT_tools,
2026-06) `:0.5` era fracción del ancho —media imagen— y `:1` la imagen entera,
mientras que tren2 corre código de abril, donde son píxeles. Desplegar la fase 4
habría dejado blanca la mesoescala. Ahora hay una sola regla en las tres
herramientas: `< 1` fracción del ancho, `>= 1` píxeles, y un aviso cuando un
grosor relativo pasa del 1 % del ancho.

**Mesoescala.** `crea_rgb_meso.py` buscaba `<-o>.json` y el Item se nombra por su
`id`, así que la ingesta cayó al instalar el hpsv de la fase 3. Ahora hpsv corre
sin `-o` (imagen e Item comparten nombre), la imagen se toma del `href` del Item y
el nombre publicado de siempre (`YYYYMMDD_hhmm_SAT_SECTOR.png`) se arma desde el
Item.

**Verificación.** 265 pruebas en LANOT_tools. Contra los GeoTIFF de un disco
completo G19 con `-B` (21696² y 10000²): límites iguales a los de rasterio y 0 m
de diferencia de posición. Una escena real de mesoescala G19 M1 por la cadena
completa de `crea_rgb_meso.py`: costa de 1 px alineada y `"satellite": "G19"`.

**Pendientes.**

* Desplegar juntos en tren2 el hpsv con `a6e97dd`, LANOT_tools y
  LANOT_procesamiento_goes.
* Grosor en disco completo: `0.0005` da 5 px en 10000² donde tren2 hoy pinta
  1 px. Revisarlo guion por guion.
* Otros guiones podrían dejar de pasarle `-o` a hpsv.
* Lo que mesoescala publica junto a la imagen sigue siendo el sidecar plano de
  `mapdrawer --o_crs`, no el Item.

### Fase 5 — Validación externa y barrido retroactivo — **validación HECHA 2026-09-12; barrido pendiente**

**Validación contra los esquemas oficiales, hecha.** `tests/test_json.sh` valida
cada Item contra el esquema del núcleo y el de cada extensión que declara.

*Decisión explícita sobre la red, que este plan pedía tomar:* **se versionan
copias locales**, en `docs/stac/schemas/`, no se descargan en cada corrida. Dos
razones: la suite deja de depender de que un servicio ajeno esté en pie, y las
versiones quedan fijadas de forma visible en el árbol, que es justo lo que el
riesgo de «deriva de versiones» pedía. `tools/fetch_stac_schemas.py` las trae
—doce archivos, el cierre completo de referencias— y **lee las versiones de
`src/metadata.c`** en vez de repetirlas, de modo que no pueden separarse del
emisor. Mover una versión es editar los `#define`, correr el script y revisar el
diff.

**Y encontró lo que se buscaba.** El `Item` no validaba contra `eo` v1.1.0:
`eo:bands` tiene que ir en el **activo**, y un Item que sólo lo lleve en
`properties` no valida. Al corregirlo apareció algo de fondo: decir que el
activo de un `ash` contiene C11, C13, C14 y C15 es **falso** —ese activo es un
compuesto RGB de tres planos derivados—, la misma falsedad que ya se había
evitado al no colgar `raster:bands` de un activo de 8 bits. Así que `eo:bands`
se emite sólo cuando el activo **es** esa banda (`gray`, `pseudocolor`), y un
compuesto no declara ni el campo ni la extensión; su procedencia vive en
`hpsv:channels`. El núcleo 1.0.0 y `projection`/`processing` v1.1.0 validaron sin
cambios.

*Corrección a una afirmación mía intermedia:* llegué a escribir que eo v1.1.0
«prohíbe» `eo:bands` en `properties`. No es exacto: lo que exige es que esté en
el activo. Un Item que lo tenga en ambos sitios valida.

**Barrido retroactivo, pendiente.** Reconstruir Items de lo ya producido desde
los GeoTIFF. Lo que las etiquetas GDAL y el nombre permiten: satélite, sector,
instante, producto, y la georreferencia completa (geotransformación y CRS), de
donde sale la huella con el mismo código de `src/footprint.c`. Lo que **no** se
puede recuperar: `hpsv:channels` —magnitud física, unidades, rango real— y
`hpsv:enhancements`, porque nunca se escribieron dentro del GeoTIFF. Los PNG sin
georreferencia quedan fuera por completo. Conviene que sea una herramienta
aparte en `tools/`, no parte de `hpsv`: lee archivos que ya existen y no
participa del camino de proceso.

## Mapeo campo por campo

| Sidecar hoy | `Item` de STAC | Nota |
|---|---|---|
| `tool`, `version` | `properties["processing:software"]` | `{"hpsatviews": "<HPSV_VERSION>"}` |
| `satellite` (`G16`) | `properties.platform` (`goes-16`), `constellation` (`goes`), `instruments` (`["abi"]`) | Requiere tabla de nombres canónicos |
| `sector` | `properties["hpsv:sector"]` | No hay campo estándar |
| `timestamp` | `properties.datetime` | Ya en ISO 8601 UTC |
| `product`, `command` | `properties["hpsv:product"]`, `["hpsv:command"]` | |
| `crs` | `properties["proj:wkt2"]`, `proj:epsg` (`null` en rejilla fija) | Fase 2 |
| `bounds` | `proj:transform` + `proj:shape` del activo (y `proj:bbox`) | **No** es `bbox` de la raíz, que es la huella en 4326 (corregido en la fase 4) |
| `geometry.bbox` en metros | `properties["proj:bbox"]` | |
| — | `geometry` (polígono GeoJSON) | Fase 1 |
| `channels[].name` | `properties["eo:bands"][].name` / `common_name` | |
| `channels[].min/max/unit` | **`properties["hpsv:channels"]`** | **No** en `raster:bands.statistics`: eso cuelga del activo, y el activo es de 8 bits. Colgarle estadísticas en kelvin afirma algo falso, y ningún validador lo atrapa |
| `enhancements` | `properties["hpsv:enhancements"]` | Sin hogar estándar; `processing:lineage` admite texto libre |
| (nombre del archivo) | `id` | `metadata_build_filename()` sin extensión, con la identidad por escena de D1 |

**Longitudes de onda centrales de ABI (µm)**, para `eo:bands.center_wavelength`
—verificar contra el ABI PUG antes de fijarlas—:

| C01 | C02 | C03 | C04 | C05 | C06 | C07 | C08 |
|---|---|---|---|---|---|---|---|
| 0.47 | 0.64 | 0.86 | 1.37 | 1.6 | 2.24 | 3.9 | 6.2 |

| C09 | C10 | C11 | C12 | C13 | C14 | C15 | C16 |
|---|---|---|---|---|---|---|---|
| 6.9 | 7.3 | 8.4 | 9.6 | 10.3 | 11.2 | 12.3 | 13.3 |

## Lo que esta herramienta NO hace

* **No genera `Collection` ni `Catalog`.** Eso mantiene un grafo sobre todo el
  acervo; es del servicio de catálogo, no de un programa que escribe un archivo.
* **No resuelve enlaces absolutos** ni conoce la URL donde se publicará.
* **No mantiene el índice.** Emite ítems; alguien más los ingiere.

## Riesgos conocidos

* **El limbo del disco completo** (fase 1) está resuelto y medido; ver la fase 1.
  El riesgo real no era la geometría del limbo sino el **antimeridiano**, que
  este documento no mencionaba: sin tratarlo, un disco de GOES-West declara
  cobertura mundial.
* **`COMPRESS=ZSTD`** (`src/writer_geotiff.c:273`) es COG válido, pero si el
  objetivo es que terceros lean los activos, reduce la audiencia frente a
  DEFLATE o WEBP. Conviene poder elegir la compresión cuando la salida es para
  publicación.
* **Deriva de versiones:** `stac_version` y cada extensión se fijan por URL de
  esquema. Se suman a las dos versiones que ya se cargan (herramienta y esquema
  propio). Sin validación en la suite, la deriva se repite.
* **D1 mal resuelta** produce ítems duplicados por escena, que es la falla más
  visible desde un cliente y la más molesta de corregir después, porque los
  identificadores ya circularon.

## Pendiente aparte: casos de producción en la suite (discutido 2026-09-12)

**Hace falta, pero no replicando los guiones.** La evidencia son los tres
defectos que aparecieron al hacer las fases 0 a 3 —el segfault de `rgb -B` con
`-s`, la extensión en metros bajo `-s`, y `rgb` sin `command`—: los tres estaban
en combinaciones que `LANOT_procesamiento_goes` usa a diario y que la suite no
tocaba. Ninguno era exótico. El hueco entre la suite y producción era
exactamente donde vivían los bugs, y no por casualidad: la suite prueba banderas
de una en una y producción las combina.

Replicar `crea_rgbs_products.sh` sería el error opuesto. Los guiones cambian; lo
estable es **el contrato de la CLI**, y es eso lo que hay que cubrir.

**Barato, sin datos nuevos:**

* `-N "etiqueta:corta"`, la forma con dos puntos. Alimenta `product_short`, que
  alimenta el `mode` de `rgb` y por tanto **el `id` del Item**. Un identificador
  es contrato público y hoy no tiene una sola prueba.
* `--expr "A;B;C"` en `rgb --mode custom`, que es como producción arma varios
  productos.
* `--minmax` invertido (max,min), que el propio guion documenta como truco
  deliberado.

**Caro: el disco completo.** Es donde el código de la fase 1 hace su trabajo más
interesante —128 bisecciones al limbo, el antimeridiano de GOES-West, el pico de
memoria de 10848²— y la suite no lo ha visto nunca. Pero meterlo en
`sample_data/` multiplica por diez la descarga y el tiempo de CI en **cada**
corrida. Tampoco alcanzan los canales: `airmass`, `severestorm` y `so2` no se
prueban porque C05, C07–C10 y C12 no están en la muestra.

Propuesta: una suite de disco completo **fuera** de `run_all_tests.sh`, invocada
a mano con la ruta a un archivo local y deliberadamente **no contabilizada** en
el marcador. La razón está en `CLAUDE.md`: la suite CUDA se salta sola y cuenta
como aprobada, de modo que un 9/9 verde no significa que la GPU se probara.
Añadir una segunda suite que se salta sola repetiría esa mentira; si no entra en
el conteo, no puede mentir.

**Observación incómoda sobre el método.** Lo que cazó los bugs de esta ronda no
fueron las pruebas sino la **comparación diferencial** contra el binario
anterior: tiempos, pico de RSS y `cmp` byte a byte de las salidas. Eso no está
automatizado en ninguna parte. Automatizarlo exige guardar referencias, que es
justo el mantenimiento que se decidió no pagar cuando se puso `check_nonblank`
en vez de una referencia exacta por modo. Conviene revisar esa decisión con lo
aprendido, pero es una discusión aparte de ésta.

## Primer paso de la siguiente sesión

Fases 0 a 4 cerradas: `hpsv` emite `Item`s de STAC con CRS por activo y
`mapdrawer` los lee. Antes de la fase 5 va el despliegue, porque tren2 sigue con
LANOT_tools de abril: hpsv, LANOT_tools (`stac-fase4`) y
LANOT_procesamiento_goes (`meso-item-stac`) tienen que llegar juntos.

**Fase 5, validación externa y barrido retroactivo.** Validar contra los
esquemas oficiales del núcleo y de cada extensión, y decidir explícitamente si
se descargan en CI o se versionan copias locales; ahí se revalidan las versiones
declaradas. El barrido reconstruye Items de lo ya producido desde los GeoTIFF,
documentando que `hpsv:channels` y `hpsv:enhancements` no se pueden recuperar.

El aviso a producción sobre `crea_rgbs_products.sh` ya se atendió: el guion vuelve
a tratar como fallo el código de salida de `hpsv`.
