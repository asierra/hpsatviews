# Plan de Implementación: Sistema de Metadatos y Arquitectura Unificada v2.0

## Objetivo General

Refactorizar **hpsatviews** para exportar un archivo *sidecar* (`.json`) con información **radiométrica** y **geoespacial** crítica. Para lograrlo de manera sostenible, se unificará la gestión del estado del programa eliminando estructuras redundantes y adoptando patrones de diseño modernos.

---

## Diagrama de Flujo de Datos

### Estado Actual (Legacy)
```
main.c
  └─> ArgParser parsed
      ├─> cmd_rgb(parser)
      │    └─> run_rgb(parser)
      │         ├─> rgb_parse_options(parser, &ctx)  // Crea RgbOptions dentro de ctx
      │         ├─> Cargar canales (DataNC)
      │         ├─> Procesar RGB (composer)
      │         ├─> generate_hpsv_filename(FilenameGeneratorInfo)  // Estructura temporal
      │         └─> save_png(filename)
      │
      └─> cmd_gray(parser)
           └─> run_processing(parser, false)
                ├─> Parsear opciones manualmente (variables locales)
                ├─> Cargar canal NetCDF
                ├─> Procesar escala de grises
                ├─> generate_hpsv_filename(FilenameGeneratorInfo)
                └─> save_png(filename)

PROBLEMA: 
- Cada comando parsea opciones de forma diferente
- FilenameGeneratorInfo es una estructura "throw-away"
- No hay metadatos exportables
- RgbOptions y parsing manual en processing.c son redundantes
```

### Estado Objetivo (v2.0)
```
main.c
  └─> ArgParser parsed
      ├─> config_from_argparser(parser, &config)  // ProcessConfig inmutable
      ├─> meta = metadata_create()                 // MetadataContext opaco
      │
      ├─> cmd_rgb()
      │    └─> run_rgb(&config, meta)              // Inyección de dependencias
      │         ├─> metadata_add(meta, "satellite", "goes-16")
      │         ├─> metadata_add(meta, "mode", "truecolor")
      │         ├─> Cargar canales
      │         ├─> metadata_add(meta, "radiometry", ...)
      │         ├─> Procesar RGB
      │         └─> metadata_add(meta, "geometry", ...)
      │
      └─> cmd_gray()
           └─> run_processing(&config, meta)       // Misma firma que RGB
                └─> (Flujo análogo)
      
      // FASE COMÚN DE CIERRE
      ├─> output_filename = metadata_build_filename(meta, ".png")
      ├─> save_png(output_filename, image)
      ├─> json_filename = metadata_build_filename(meta, ".json")
      ├─> metadata_write_json(meta, json_filename)
      └─> metadata_destroy(meta)

MEJORA:
✓ Interfaz unificada (config + meta)
✓ Separación clara: config = input, meta = output
✓ Metadata acumula TODO lo necesario (nombre archivo + JSON)
✓ FilenameGeneratorInfo eliminado
✓ RgbOptions eliminado (reemplazado por ProcessConfig)
```

---

---

## Resumen de Archivos Impactados

### Archivos Nuevos (6)
| Archivo | Propósito | Sprint |
|---------|-----------|--------|
| `include/config.h` | Define `ProcessConfig` y feature flag | 1 |
| `src/config_loader.c` | Parser ArgParser → ProcessConfig | 1 |
| `include/metadatos.h` | API opaca para MetadataContext | 1 |
| `src/metadatos.c` | Implementación + generación JSON | 1-2 |
| `tests/test_metadata.c` | Tests unitarios metadatos | 6 |
| `tests/test_config.c` | Tests unitarios configuración | 6 |

### Archivos a Modificar (7)
| Archivo | Cambios Principales | Sprint |
|---------|---------------------|--------|
| `src/main.c` | Agregar dispatching con feature flag | 3-4 |
| `include/rgb.h` | Adelgazar RgbContext, eliminar RgbOptions | 3 |
| `src/rgb.c` | Implementar run_rgb_v2(), inyectar dependencias | 3 |
| `include/processing.h` | Nueva firma run_processing_v2() | 4 |
| `src/processing.c` | Refactorizar con nuevos contextos | 4 |
| `Makefile` | Agregar nuevos .c a SRCS, remover filename_utils.c | 1, 5 |
| `README.md` | Documentar JSON sidecar | 6 |

### Archivos a Eliminar (2)
| Archivo | Motivo | Sprint |
|---------|--------|--------|
| `src/filename_utils.c` | Lógica absorbida en metadatos.c | 5 |
| `include/filename_utils.h` | Header obsoleto | 5 |

### Archivos No Modificados (Reutilizables sin cambios)
- `src/channelset.c` - Gestión de sets de canales
- `src/datanc.c` - Estructura de datos NetCDF y arrays de floats y de bytes
- `src/reader_nc.c` - Lectura NetCDF
- `src/image.c` - Manipulación de imágenes y estructura de datos ubytes
- `src/reprojection.c` - Reproyección geográfica
- `src/truecolor.c` - Algoritmos de true color
- `src/rayleigh.c` - Corrección Rayleigh
- `src/gray.c` - Generación escala de grises
- `src/palette.c` - Manejo de paletas CPT
- `src/writer_png.c` / `writer_geotiff.c` - Escritura de imágenes

**Total:** 6 nuevos + 7 modificados + 2 eliminados = **15 archivos afectados** (de ~40 totales)

---

## Fase 1: Definición de la Arquitectura Core (Modern C)

### Objetivo
Crear dos estructuras fundamentales que reemplazarán el paso de múltiples argumentos sueltos y eliminarán la necesidad de `FilenameGeneratorInfo` y variables globales.

### 1. `ProcessConfig` (Pattern: Data Aggregate / Configuration Object)
**Ubicación:** `config.h`
Estructura **inmutable** que contiene *lo que el usuario pidió* (Input).
* **Propósito:** Eliminar listas largas de argumentos en funciones y centralizar la configuración.
* **Modern C (C99/C11):** Se inicializará usando **Designated Initializers** para claridad y valores por defecto seguros.

```c
typedef struct {
    const char *command_mode;   // "rgb", "gray", "pseudocolor"
    const char *strategy_name;  // "truecolor", "ch13", "ash"
    
    // Parámetros físicos
    float gamma;
    bool apply_clahe;
    float clahe_clip_limit;
    int clahe_tiles_x;          // [NUEVO]
    int clahe_tiles_y;          // [NUEVO]
    bool apply_histogram;
    bool apply_rayleigh;        // [NUEVO]
    
    // Opciones de composición
    int scale;                  // [NUEVO]
    bool use_alpha;             // [NUEVO]
    bool use_citylights;        // [NUEVO]

    // Álgebra de Bandas (Custom)
    bool is_custom_mode;        // [NUEVO]
    void *custom_combos;        // [NUEVO] Puntero a LinearCombo[3]
    float *custom_ranges;       // [NUEVO] Puntero a float[6]
    
    // Geometría solicitada
    bool has_clip;
    float clip_coords[4];       // [lon_min, lat_max, lon_max, lat_min]
    bool do_reprojection;
    
    // Salida
    bool force_geotiff;
    const char *output_path_override;
} ProcessConfig;
```

### 2. `MetadataContext` (Pattern: Opaque Handle)
**Ubicación:** `metadatos.h` (declaración) y `metadatos.c` (definición) Estructura opaca que contiene lo que realmente sucedió y los resultados acumulados.
* **Propósito:** Desacoplar la lógica científica de la librería de serialización (cJSON) y actuar como repositorio único para generar el nombre de archivo final.
* **Fluent C:** Implementa el patrón Handle (typedef struct MetadataContext MetadataContext; en el header).
* **Modern C (C11):** Utiliza macros _Generic para una API polimórfica simple que evita la proliferación de funciones con nombres distintos.


```c
// En metadatos.h
#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        int:    metadata_add_int, \
        double: metadata_add_double, \
        char*:  metadata_add_string, \
        const char*: metadata_add_string \
    )(CTX, KEY, VAL)
```

## Fase 2: Implementación de Módulos Base

### A. Módulo de Configuración (`config_loader.c`)
Crear un cargador centralizado que convierta `ArgParser` en `ProcessConfig`.
* **Acción:** Mover la lógica de parseo dispersa en `rgb.c` y `processing.c` a este nuevo módulo.
* **Validación:** Usar `static_assert` (C11) para validar tamaños de buffers y asunciones de tipos en tiempo de compilación.

### B. Módulo de Escritura JSON (`writer_json.c`)
Implementar un escritor JSON minimalista ("Write-Only") optimizado para C17.
* **Objetivo:** Reemplazar librerías pesadas para facilitar la compilación en clústeres.
* **Características:**
    * Cero dependencias (solo `<stdio.h>`).
    * **Modern C (C11/C17):** Implementación de una API polimórfica mediante macros `_Generic`.
        * Permite usar `json_write(w, "key", valor)` indistintamente para `int`, `double` o `string`.
    * Escritura directa a disco (Stream) para consumo de memoria insignificante.

### C. Módulo de Metadatos (`metadatos.c`)
Implementar el contenedor opaco y la lógica de negocio.
* **Estrategia de Datos:** El `MetadataContext` acumulará estadísticas en estructuras C nativas (`struct`) durante el procesamiento.
* **Serialización:** La función `metadata_save_json` usará la API simplificada `json_write` para volcar los datos al finalizar.
* **Generación de Nombres:** Absorbe la lógica de `filename_utils.c`.

---

## Fase 3: Refactorización de Pipelines (Inyección de Dependencias)

### Refactorización de `rgb.c` y `processing.c`
Modificar las funciones principales (`run_rgb`, `processing_loop`) para aceptar los nuevos contextos en lugar de argumentos sueltos o estructuras ad-hoc.

#### Patrón: Caller-Owned Instance
El flujo en `main` o el despachador de comandos será:
1.  **Crear:** `main` instancia `ProcessConfig` (desde args) y `MetadataContext` (vacío).
2.  **Inyectar:** `main` pasa los punteros a `run_rgb(&config, meta)`.
3.  **Procesar:** `run_rgb` lee la configuración (solo lectura) y escribe estadísticas/geometría en `meta`.
4.  **Guardar:** `main` solicita a `meta` generar el nombre de archivo y guardar el JSON.
5.  **Destruir:** `main` es responsable de limpiar la memoria al finalizar.

#### Patrón: Goto Error Handling
Asegurar que las funciones de procesamiento utilicen etiquetas `goto cleanup` para liberar recursos intermedios (imágenes, buffers grandes) de forma determinista ante cualquier error, sin destruir prematuramente el `MetadataContext` (permitiendo guardar logs de error en el JSON si fuera necesario).

### Migración de Estructuras Legadas y Estrategia de Pipeline

#### 1. Eliminación de `FilenameGeneratorInfo`
**Estado Actual:** Estructura temporal en `filename_utils.h` usada solo para pasar datos a la función de nombres.
**Acción:** **ELIMINAR**.
**Reemplazo:**
* La lógica de generación de nombres se mueve dentro de `MetadataContext`.
* Al tener acceso a `ProcessConfig` (para el modo/estrategia) y a los datos recolectados (satélite, fecha), el `MetadataContext` tiene toda la información necesaria sin requerir una estructura intermedia.

#### 2. Refactorización de `RgbContext`
**Estado Actual:** "God Object" que mezcla configuración (gamma, clahe), estado de error y buffers de imagen.
**Acción:** **ADELGAZAR (Slim Down)** mediante composición.
**Nueva Definición en `rgb.h`:**
```c
typedef struct {
    // --- INYECCIÓN DE DEPENDENCIAS ---
    const ProcessConfig *config;  // Configuración (Solo lectura, reemplaza a ctx->opts)
    MetadataContext *meta;        // Salida de metadatos (Escritura)

    // --- ESTADO DE EJECUCIÓN (Lo que queda) ---
    DataNC *channels[16];         // Canales cargados
    ImageData final_image;        // Buffer de imagen resultante
    
    // --- ESTADO INTERNO ---
    // Variables temporales exclusivas del algoritmo RGB
    // (ej. máscaras intermedias, buffers de reproyección)
} RgbContext;
```

#### 3. Estrategia de Pipeline: "Setup Unificado, Ejecución Especializada"
No fusionaremos rgb.c y processing.c en una sola función gigante (para mantener Separation of Concerns), pero unificaremos su Interfaz de Llamada.

Nuevo Flujo de Control (en `main.c` o `hpsatviews.c`):

1. Fase de Setup (Común):
* ArgParser -> config_loader -> ProcessConfig (Llenado automático).
* Creación de MetadataContext.

2. Fase de Despacho (Branching):
```c
if (strcmp(config.command_mode, "rgb") == 0) {
    // run_rgb ahora acepta la firma estandarizada
    run_rgb(&config, meta); 
} 
else {
    // processing_loop (monocanal) se adapta a la misma firma
    run_monochannel(&config, meta); 
}
```

3. Fase de Cierre (Común):
* Generación de nombre de archivo (vía meta).
* Guardado de JSON (vía meta).
* Limpieza de memoria (destroy).

---

## Fase 4: Generación de Salidas

### 1. Nombre de Archivo Automático
El `MetadataContext` tendrá un método `metadata_build_filename(ctx, extension)` que reemplaza a `generate_hpsv_filename`. Usará internamente:
* Satélite (detectado del input file).
* Estrategia/Modo (del Config).
* Fecha/Hora (leída del NetCDF y normalizada).

### 2. JSON Sidecar
Estructura final del JSON a generar:

#### 1. Caso: Escala de Grises Invertida (Canal IR)
**Escenario:** Canal 13 (Infrarrojo limpio). En meteorología, solemos invertirlo: las nubes frías (valores bajos de Kelvin) se ven blancas (píxel 255), y el suelo caliente (valores altos) se ve negro.

* **Punto Clave:** El `campo invert_values: true`. Esto le dice al visualizador que la barra de colores debe dibujarse al revés (ej. de 320K abajo a 180K arriba).

```json
{
  "processing_tool": "hpsatviews",
  "version": "1.0",
  "command": "gray",
  "input_file": "OR_ABI-L2-CMIPF-...",
  "mode": "channel_13",
  "satellite": "goes-16",
  "time_iso": "2025-01-04T16:30:00Z",
  "geometry": {
    "projection": "geographics",
    "bbox": [-115.0, 32.0, -85.0, 14.0]
  },
  "radiometry": [
    {
      "name": "C13",
      "description": "Clean IR Window",
      "min_radiance": 180.5,
      "max_radiance": 320.0,
      "unit": "K"
    }
  ],
  "enhancements": {
    "gamma": 1.0,
    "invert_values": true, 
    "apply_clahe": false
  }
}
```

#### 2. Caso: Pseudocolor (Vapor de Agua)
**Escenario:** Canal 09 (Mid-level Water Vapor) aplicado con una paleta de colores (ej. wv_noaa).

* **Punto Clave:** Aparece el campo `palette`. El campo `radiometry` sigue mostrando los valores físicos (Kelvin), pero la imagen PNG ya tiene colores "quemados". El visualizador usará `min_radiance` y `max_radiance` para poner las etiquetas numéricas a los extremos de la paleta.

```json
{
  "processing_tool": "hpsatviews",
  "version": "1.0",
  "command": "pseudocolor",
  "mode": "channel_09",
  "palette": "wv_noaa", 
  "satellite": "goes-16",
  "time_iso": "2025-01-04T16:30:00Z",
  "geometry": {
    "projection": "geographics",
    "bbox": [-120.0, 40.0, -70.0, 0.0]
  },
  "radiometry": [
    {
      "name": "C09",
      "description": "Mid-level Water Vapor",
      "min_radiance": 230.0,
      "max_radiance": 290.0,
      "unit": "K"
    }
  ],
  "enhancements": {
    "gamma": 1.0,
    "invert_values": false
  }
}
```

#### 3. Caso: Álgebra de Bandas (RGB Ceniza Volcánica)
**Escenario:** El producto "Ash" es una composición matemática compleja (Algebra):

* Rojo: Diferencia (C15 - C13). Rango físico: -4.0 a 2.0 K.

* Verde: Diferencia (C14 - C11). Rango físico: -4.0 a 5.0 K.

* Azul: Canal C13 invertido. Rango físico: 243 a 303 K.

* Punto Clave: El array `radiometry` tiene 3 elementos. Aquí el JSON brilla, porque describe qué significa cada color físicamente. Un científico puede leer esto y saber exactamente qué rango de temperatura representa el canal rojo, sin adivinar.

```json
{
  "processing_tool": "hpsatviews",
  "version": "1.0",
  "command": "rgb",
  "mode": "ash",
  "satellite": "goes-16",
  "time_iso": "2025-01-04T16:30:00Z",
  "geometry": {
    "projection": "geographics",
    "bbox": [-100.0, 25.0, -90.0, 15.0]
  },
  "radiometry": [
    {
      "component": "red",
      "formula": "T15 - T13",
      "min_radiance": -4.0,
      "max_radiance": 2.0,
      "unit": "K (diff)"
    },
    {
      "component": "green",
      "formula": "T14 - T11",
      "min_radiance": -4.0,
      "max_radiance": 5.0,
      "unit": "K (diff)"
    },
    {
      "component": "blue",
      "formula": "T13",
      "min_radiance": 243.0,
      "max_radiance": 303.0,
      "unit": "K"
    }
  ],
  "enhancements": {
    "gamma": 1.0,
    "description": "EUMETSAT Ash Recipe"
  }
}
```

## Fase 5: Consumo en Mapdrawer (Visualización)
Adaptar las herramientas externas para usar este JSON.

### Barra de color virtual
* Ignorar los valores de píxel (0–255) para la leyenda.

* Construir la escala de colores usando min_radiance y max_radiance del JSON.

### Etiquetado automático
* Generar etiquetas dinámicamente: Temperature [K] (CLAHE enhanced).

## Fase 6: Estrategia de Migración Incremental ("Strangler Fig Pattern")

### Objetivo
Realizar la refactorización sin romper la funcionalidad existente, permitiendo reversión rápida y validación continua durante el desarrollo.

### 6.1 Preparación (Branch y Feature Flags)

**Acción:**
1. Crear rama de desarrollo: `git checkout -b feature/metadata-refactor`
2. Agregar feature flag en `include/config.h` (nuevo archivo):
```c
// Feature flag: cuando esté listo, cambiar a 1
#ifndef HPSV_USE_NEW_PIPELINE
#define HPSV_USE_NEW_PIPELINE 0
#endif
```

### 6.2 Orden de Implementación (Bottom-Up)

#### SPRINT 1: Fundamentos (Semana 1-2)
**Archivos a CREAR:**
1. `include/config.h` + `src/config_loader.c`
   - Implementar `ProcessConfig` struct
   - Función: `config_from_argparser(ArgParser *parser, ProcessConfig *cfg)`
   - **Validación:** Tests unitarios con valores conocidos

2. `include/metadatos.h` + `src/metadatos.c`
   - Implementar `MetadataContext` (opaque handle)
   - API básica: `metadata_create()`, `metadata_destroy()`, `metadata_add_string/int/double()`
   - Stub para generación de JSON (sin cJSON todavía)
   - **Validación:** Compilar sin warnings, tests de memoria (valgrind)

**Dependencias del Makefile:**
```makefile
SRCS += src/config_loader.c src/metadatos.c
```

#### SPRINT 2: Integración con JSON (Semana 3)
**Archivos a MODIFICAR:**
1. `src/metadatos.c`
   - Integrar cJSON para generación real del JSON
   - Implementar `metadata_write_json(ctx, filename)`
   - Implementar `metadata_build_filename(ctx, extension)`
   
2. **Absorber lógica de `filename_utils.c`:**
   - Mover funciones estáticas a `metadatos.c`:
     - `extract_satellite_from_filename()`
     - `format_instant_from_datanc()`
     - `date_to_julian()`
   - `FilenameGeneratorInfo` → **DEPRECATED** (marcar con comentario)
   
**Validación:**
- Crear programa de prueba standalone que genere un JSON sin tocar el pipeline principal
- Verificar schema con validador JSON externo

#### SPRINT 3: Refactorización de RGB (Semana 4-5)
**Archivos a MODIFICAR:**

1. `include/rgb.h`:
   - Adelgazar `RgbContext`:
```c
typedef struct {
    // INYECCIÓN DE DEPENDENCIAS (NUEVO)
    const ProcessConfig *config;
    MetadataContext *meta;
    
    // MANTENER:
    ChannelSet *channel_set;
    DataNC channels[17];
    DataF nav_lat, nav_lon;
    DataF comp_r, comp_g, comp_b;
    ImageData final_image, alpha_mask;
    
    // REMOVER:
    // RgbOptions opts;  ← Ya no es necesario
} RgbContext;
```

2. `src/rgb.c`:
   - Cambiar firma de `run_rgb()`:
```c
#if HPSV_USE_NEW_PIPELINE
int run_rgb_v2(const ProcessConfig *config, MetadataContext *meta);
#else
int run_rgb(ArgParser *parser);  // Versión legacy
#endif
```
   - Implementar `run_rgb_v2()` duplicando lógica pero usando nuevos contextos
   - Durante procesamiento, llamar `metadata_add()` para estadísticas

3. `src/main.c`:
   - En `cmd_rgb()`, agregar switch:
```c
int cmd_rgb(char* cmd_name, ArgParser* cmd_parser) {
#if HPSV_USE_NEW_PIPELINE
    ProcessConfig cfg = {0};
    config_from_argparser(cmd_parser, &cfg);
    MetadataContext *meta = metadata_create();
    int result = run_rgb_v2(&cfg, meta);
    metadata_write_json(meta, "output.json");
    metadata_destroy(meta);
    return result;
#else
    return run_rgb(cmd_parser);  // Legacy
#endif
}
```

**Validación:**
- Compilar con `HPSV_USE_NEW_PIPELINE=0` (debe funcionar igual que antes)
- Compilar con `HPSV_USE_NEW_PIPELINE=1` (probar nueva ruta)
- Comparar salidas PNG bit-a-bit: `cmp old.png new.png`

#### SPRINT 4: Refactorización de Processing (Semana 6)
**Archivos a MODIFICAR:**

1. `include/processing.h`:
```c
#if HPSV_USE_NEW_PIPELINE
int run_processing_v2(const ProcessConfig *config, MetadataContext *meta);
#else
int run_processing(ArgParser *parser, bool is_pseudocolor);
#endif
```

2. `src/processing.c`:
   - Crear `run_processing_v2()` análogo al refactor de RGB
   - Las funciones `process_clip_coords()`, `strinstr()` pueden mantenerse sin cambios

3. `src/main.c`:
   - Actualizar `cmd_gray()` y `cmd_pseudocolor()` con feature flag

**Validación:**
- Tests end-to-end con sample_data/:
  - `hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_*.nc`
  - `hpsv pseudocolor --cpt phase.cpt sample_data/OR_ABI-L2-CMIPC-M6C13_*.nc`

#### SPRINT 5: Eliminación de Legacy (Semana 7)
**Archivos a ELIMINAR/MODIFICAR:**

1. `src/filename_utils.c` + `include/filename_utils.h`:
   - **ELIMINAR** (funcionalidad absorbida en `metadatos.c`)
   - Actualizar Makefile para remover de SRCS

2. `include/rgb.h`:
   - **ELIMINAR** `RgbOptions` struct (ahora usa `ProcessConfig`)

3. `src/main.c`, `src/rgb.c`, `src/processing.c`:
   - Remover todos los `#if HPSV_USE_NEW_PIPELINE` blocks
   - Eliminar funciones `_v2` (renombrar a nombres originales)
   - Eliminar funciones legacy

**Validación:**
- Ejecutar suite completa de tests
- Probar con todos los modos: rgb, gray, pseudocolor
- Probar con todas las opciones: --clip, --gamma, --clahe, --geotiff

#### SPRINT 6: Testing y Documentación (Semana 8)
**Archivos a CREAR/MODIFICAR:**

1. `tests/test_metadata.c` (nuevo):
   - Tests unitarios para `MetadataContext`
   - Tests de serialización JSON

2. `tests/test_config.c` (nuevo):
   - Tests de `ProcessConfig` parsing

3. `docs/MIGRATION_V2.md` (nuevo):
   - Documentar cambios en la API interna
   - Ejemplos de código viejo vs nuevo

4. `README.md`:
   - Agregar sección sobre archivo JSON sidecar
   - Ejemplos de uso con mapdrawer

5. `CHANGELOG.md` (nuevo):
   - Documentar breaking changes (si los hay)

### 6.3 Verificación y Rollback

**Punto de No-Retorno:** Sprint 5 (eliminación de legacy)

**Criterios para Avanzar a Sprint 5:**
- [ ] Todos los tests pasan con `HPSV_USE_NEW_PIPELINE=1`
- [ ] Validación visual: 10 productos generados idénticos a versión legacy
- [ ] Validación cuantitativa: JSON contiene metadatos correctos
- [ ] Performance: No hay degradación >5% en tiempo de ejecución
- [ ] Memoria: No hay leaks detectados con valgrind

**Plan de Rollback:**
```bash
# Si algo sale mal en Sprint 3-4:
git stash  # Guardar cambios locales
git checkout main
make clean && make

# Si hay que revertir después de merge:
git revert <commit-hash-del-merge>
```

### 6.4 Checklist de Validación por Sprint

#### Sprint 1: Fundamentos
```bash
# Compilación
make clean && make
./bin/hpsv --version  # Debe funcionar sin cambios

# Tests unitarios (crear con Check framework o similar)
./tests/test_config_basic
./tests/test_metadata_lifecycle

# Validación de memoria
valgrind --leak-check=full ./tests/test_metadata_lifecycle
```

#### Sprint 2: JSON
```bash
# Generar JSON de prueba sin pipeline
./tests/test_json_generation
cat test_output.json | jq .  # Validar sintaxis

# Validar contra schema
jsonschema -i test_output.json docs/hpsatviews.schema.json
```

#### Sprint 3: RGB
```bash
# Probar ambas rutas (legacy y nueva)
make clean && make  # HPSV_USE_NEW_PIPELINE=0
./bin/hpsv rgb sample_data/OR_ABI-L2-CMIPC-M6C02_*.nc -o legacy.png

make clean && make DEBUG=1 CFLAGS+=-DHPSV_USE_NEW_PIPELINE=1
./bin/hpsv rgb sample_data/OR_ABI-L2-CMIPC-M6C02_*.nc -o new.png

# Comparar bit a bit
cmp legacy.png new.png && echo "✓ IDENTICOS" || echo "✗ DIFIEREN"
md5sum legacy.png new.png

# Validar JSON generado
test -f new.json && jq . new.json || echo "✗ JSON no generado"
```

#### Sprint 4: Processing
```bash
# Gray
./bin/hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_*.nc -o test_gray.png
test -f test_gray.json && echo "✓ JSON creado"

# Pseudocolor
./bin/hpsv pseudocolor --cpt assets/phase.cpt sample_data/OR_ABI-L2-CMIPC-M6C13_*.nc -o test_pseudo.png
test -f test_pseudo.json && echo "✓ JSON creado"

# Con opciones complejas
./bin/hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_*.nc \
  --clip -100,25,-90,15 --gamma 1.5 --clahe --geotiff -o complex.tif
jq '.enhancements.gamma, .enhancements.clahe, .geometry.bbox' complex.json
```

#### Sprint 5: Eliminación Legacy
```bash
# Debe compilar SIN warnings
make clean && make 2>&1 | tee build.log
grep -i "warning\|error" build.log && echo "✗ HAY WARNINGS" || echo "✓ LIMPIO"

# Test de regresión completo
./reproduction/run_demo.sh  # Script que prueba todos los modos
diff -r output/ reproduction/expected_output/ && echo "✓ REGRESION PASS"
```

#### Sprint 6: Testing Final
```bash
# Suite completa
cd tests && ./run_all_tests.sh

# Performance benchmark
time ./bin/hpsv rgb sample_data/OR_ABI-L2-CMIPC-M6C02_*.nc -o perf.png
# Comparar con versión legacy (debe ser similar ±5%)

# Integración con mapdrawer (si disponible)
./bin/hpsv rgb sample_data/*.nc -o map.png
python3 mapdrawer.py map.json  # Generar mapa con barra de color
```

### 6.5 Matriz de Riesgos y Contingencias

| Riesgo | Probabilidad | Impacto | Mitigación | Contingencia |
|--------|--------------|---------|------------|--------------|
| Romper compatibilidad PNG | Media | Alto | Comparación bit-a-bit en Sprint 3 | Revertir sprint, debugging detallado |
| Memory leaks en MetadataContext | Media | Medio | Valgrind continuo, tests de stress | Agregar reference counting |
| Performance degradation | Baja | Medio | Profiling con gprof/perf | Optimizar hot paths, cache config |
| cJSON parsing errors | Baja | Alto | Tests con datos malformados | Agregar validación defensiva |
| Filename generation bugs | Alta | Bajo | Tests unitarios exhaustivos Sprint 2 | Mantener fallback a timestamp simple |
| Makefile dependency hell | Media | Medio | Clean builds frecuentes | Regenerar .d files con make clean |

---

## Fase 7: Verificación y Testing

### Prueba de Consistencia (Modern C)

* Verificar que static_assert protege contra cambios inesperados en la estructura de canales o tamaños de tipos.

* Procesar una imagen conocida y validar que el JSON coincida con los valores físicos esperados.

### Prueba de Recorte
* Ejecutar --clip mexico.

* Confirmar en el JSON que geometry.bbox corresponde a las coordenadas de México y no al disco completo.

### Prueba de Integración

* Flujo completo: hpsatviews → JSON → mapdrawer.

* Validar visualmente la barra de colores y la leyenda generada.

---

## Apéndice A: Cronograma Visual

```
Sprint 1: Fundamentos [████████░░░░░░░░] Semana 1-2
  ↳ config.h, metadatos.h (estructuras base)
  
Sprint 2: JSON [░░░░░░██████░░░░░░] Semana 3
  ↳ Integración cJSON, absorber filename_utils
  
Sprint 3: RGB [░░░░░░░░░░████████] Semana 4-5
  ↳ Refactor rgb.c, feature flags
  
Sprint 4: Processing [░░░░░░░░░░░░░███] Semana 6
  ↳ Refactor processing.c (gray/pseudo)
  
Sprint 5: Cleanup [░░░░░░░░░░░░░░██] Semana 7
  ↳ Eliminar legacy, unificar código
  
Sprint 6: Testing [░░░░░░░░░░░░░░░█] Semana 8
  ↳ Documentación, tests finales
```

## Apéndice B: Guía Rápida de Comandos

### Durante Desarrollo
```bash
# Compilar con pipeline antiguo (seguro)
make clean && make

# Compilar con pipeline nuevo (experimental)
make clean && make CFLAGS+=-DHPSV_USE_NEW_PIPELINE=1

# Tests de memoria
valgrind --leak-check=full --show-leak-kinds=all ./bin/hpsv rgb sample_data/*.nc

# Comparar outputs
diff <(md5sum legacy.png) <(md5sum new.png)

# Validar JSON
jq empty output.json && echo "✓ JSON válido" || echo "✗ JSON inválido"
```

### Después de Sprint 5 (Legacy Eliminado)
```bash
# Build normal
make clean && make

# Tests completos
make test  # (agregar target al Makefile)

# Instalar versión nueva
sudo make install
```

## Apéndice C: Patrones de Código C Moderno Utilizados

### 1. Designated Initializers (C99)
```c
ProcessConfig cfg = {
    .gamma = 1.0,
    .do_reprojection = false,
    .clip_coords = {-100.0, 25.0, -90.0, 15.0},
};
```

### 2. Generic Macros (C11)
```c
#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        int: metadata_add_int, \
        double: metadata_add_double, \
        char*: metadata_add_string \
    )(CTX, KEY, VAL)
```

### 3. Opaque Pointers
```c
// Header (metadatos.h)
typedef struct MetadataContext MetadataContext;

// Implementation (metadatos.c)
struct MetadataContext {
    cJSON *root;
    char buffer[1024];
};
```

### 4. Goto Error Handling
```c
int process_image(const char *file) {
    DataNC *data = NULL;
    ImageData *img = NULL;
    int status = -1;
    
    data = load_nc(file);
    if (!data) goto cleanup;
    
    img = create_image(data);
    if (!img) goto cleanup;
    
    status = 0;  // Success
    
cleanup:
    if (data) datanc_destroy(data);
    if (img) image_destroy(img);
    return status;
}
```

### 5. Static Assertions (C11)
```c
_Static_assert(sizeof(float) == 4, "Require 32-bit floats");
_Static_assert(MAX_CHANNELS == 16, "Channel array size mismatch");
```

---

## Resumen Ejecutivo

**Objetivo:** Modernizar hpsatviews para generar JSON sidecar con metadatos radiométricos/geoespaciales.

**Estrategia:** Strangler Fig Pattern con feature flags (desarrollo paralelo, eliminación gradual de legacy).

**Impacto:** 15 archivos afectados de ~40 totales (37.5% del código base).

**Duración:** 8 semanas (2 meses).

**Riesgos Principales:** Compatibilidad PNG, memory leaks, performance.

**Punto de No-Retorno:** Sprint 5 (después de validar Sprints 3-4).

**Beneficios:**
- ✅ Exportación automática de metadatos científicos
- ✅ Arquitectura más mantenible (separación config/metadata)
- ✅ Eliminación de estructuras redundantes (RgbOptions, FilenameGeneratorInfo)
- ✅ Base para futuras extensiones (más formatos, validaciones)
- ✅ Integración con herramientas de visualización (mapdrawer)
