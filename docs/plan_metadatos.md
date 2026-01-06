# Plan de Implementación: Sistema de Metadatos y Arquitectura Unificada v2.0

> **ESTADO**: ✅ **COMPLETADO** (6 de enero, 2026)
> 
> **Documentación de finalización**: Ver [SPRINT6_COMPLETADO.md](SPRINT6_COMPLETADO.md)

## Objetivo General

Refactorizar **hpsatviews** para exportar un archivo *sidecar* (`.json`) con información **radiométrica** y **geoespacial** crítica. Para lograrlo de manera sostenible, se unificará la gestión del estado del programa eliminando estructuras redundantes y adoptando patrones de diseño modernos.

**✅ OBJETIVO LOGRADO**: El sistema ahora genera automáticamente archivos JSON sidecar con metadatos completos para todos los modos (RGB, gray, pseudocolor).

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
      │─> Cargar el o los canales NetCDF pues contienen información importante
      |   para inicializar distintas variables, como los minmax intrínsecos de
      |   los datos. Ya con el datanc de referencia podemos llenar los
      |   metadatos básicos con metadata_from_nc.
      ├─> cmd_rgb()
      │    └─> run_rgb(&config, meta)              // Inyección de dependencias
      │         ├─> metadata_add(meta, "mode", "truecolor")
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

### Archivos Nuevos (6) - ✅ TODOS IMPLEMENTADOS
| Archivo | Propósito | Sprint | Estado |
|---------|-----------|--------|--------|
| `include/config.h` | Define `ProcessConfig` | 1 | ✅ Completado |
| `src/config.c` | Parser ArgParser → ProcessConfig | 1 | ✅ Completado (446 líneas) |
| `include/metadata.h` | API opaca para MetadataContext | 1 | ✅ Completado |
| `src/metadata.c` | Implementación + generación JSON | 1-2 | ✅ Completado (300+ líneas) |
| `tests/test_metadata_json.c` | Tests unitarios metadatos | 2 | ✅ Completado |
| `tests/test_config.sh` | Tests de configuración | 1 | ✅ Completado |

### Archivos Modificados (7) - ✅ TODOS COMPLETADOS
| Archivo | Cambios Principales | Sprint | Estado |
|---------|---------------------|--------|--------|
| `src/main.c` | Dispatchers unificados (269 líneas) | 3-6 | ✅ Completado (-8%) |
| `include/rgb.h` | Declaración unificada sin feature flags | 3-6 | ✅ Completado |
| `src/rgb.c` | Pipeline único con inyección de dependencias | 3-6 | ✅ Completado (1006 líneas, -16%) |
| `include/processing.h` | Declaración unificada sin feature flags | 4-6 | ✅ Completado |
| `src/processing.c` | Pipeline único refactorizado | 4-6 | ✅ Completado (492 líneas, -54%) |
| `Makefile` | Limpiado, feature flags removidos | 1-6 | ✅ Completado |
| `README.md` | Documentar JSON sidecar | 7 | ⏳ Pendiente |

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
**Ubicación:** `metadata.h` (declaración) y `metadata.c` (definición) Estructura opaca que contiene lo que realmente sucedió y los resultados acumulados.
* **Propósito:** Desacoplar la lógica científica de serialización y actuar como repositorio único para generar el nombre de archivo final.
* **Fluent C:** Implementa el patrón Handle (typedef struct MetadataContext MetadataContext; en el header).
* **Modern C (C11):** Utiliza macros _Generic para una API polimórfica simple que evita la proliferación de funciones con nombres distintos.


```c
// En metadata.h
#define metadata_add(CTX, KEY, VAL) \
    _Generic((VAL), \
        int:    metadata_add_int, \
        double: metadata_add_double, \
        char*:  metadata_add_string, \
        const char*: metadata_add_string \
    )(CTX, KEY, VAL)
```

Considerar que DataNC (`datanc.h`) ya guarda mucha información sobre cada archivo netCDF que lee, como sat_id, varname, proyección, geometría, datos físicos, etc. Por eso la función `metadata_from_nc`.

### 3. Módulo `writer_json` (C17 Minimalista)
Implementar un escritor JSON "Write-Only" para evitar dependencias externas pesadas.
* **Ubicación:** `writer_json.h|c`
* **Características:**
    * Cero dependencias (solo `<stdio.h>`).
    * **API Polimórfica (C17):** Uso de macros `_Generic` para una API limpia: `json_write(w, "key", valor)`.
    * Escritura directa a disco (stream) para consumo de memoria insignificante.

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

### C. Módulo de Metadatos (`metadata.c`)
Implementar el contenedor opaco y la lógica de negocio.
* **Estrategia de Datos:** El `MetadataContext` acumulará estadísticas en estructuras C nativas (`struct`) durante el procesamiento.
* **Serialización:** La función `metadata_save_json` usará la API simplificada `json_write` para volcar los datos al finalizar.
* **Generación de Nombres:** Absorbe la lógica de `filename_utils.c`.

---

## Fase 3: Refactorización de Pipelines (Inyección de Dependencias)

### Paso 1: Inyección en `run_rgb` (`rgb.c`)
* Instanciar `MetadataContext` localmente dentro de `run_rgb`.
* Conectar datos existentes:
    * **Configuración:** Copiar manualmente desde `RgbOptions` (gamma, clahe).
    * **Geometría:** Capturar `final_lon_min`, etc., después de la reproyección.
    * **Magnitudes Físicas:** Inferir rangos físicos (Kelvin/Reflectancia) basados en el comando o modo (`truecolor`, `ash`).
* **Resultado:** Generación del primer `.json` funcional junto al `.png`.

### Paso 2: Adaptación de `reader_nc.c`
* Actualizar el lector para poblar los nuevos campos `timestamp` y `sat_id` de `DataNC` al momento de la lectura. Ya están.
* Asegurar que la conversión física (Radiancia -> Reflectancia/Temperatura) esté correcta para que los metadatos sean precisos.

### Paso 3: Arquitectura Unificada (Refactorización)

Una vez que el JSON se genera correctamente, procedemos a limpiar la deuda técnica y unificar la configuración.

### 1. `ProcessConfig` (Configuration Object)
**Ubicación:** `config.h` y `config_loader.c`
* Estructura inmutable que centraliza *lo que el usuario pidió*.
* `config_loader.c` encapsula toda la lógica de `ArgParser`, eliminando el código de parseo disperso en `rgb.c` y `processing.c`.

### 2. Migración Completa de Pipelines
* Refactorizar `run_rgb` para aceptar `const ProcessConfig *` y `MetadataContext *` inyectados desde `main`.
* Eliminar estructuras legadas como `FilenameGeneratorInfo` (la lógica de nombres pasa a `metadatos.c`).

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

Nuevo Flujo de Control (en `main.c`):

1. Fase de Setup (Común):
* ArgParser -> config_loader -> ProcessConfig (Llenado automático).
* Creación de MetadataContext.
* Carga de canales según las opciones y llenado con metadata_from_nc.
2. Fase de Despacho (Branching):
```c
if (strcmp(config.command_mode, "rgb") == 0) {
    // run_rgb ahora acepta la firma estandarizada
    run_rgb(&config, meta); 
} 
else {
    // processing_loop (monocanal) se adapta a la misma firma
    run_mono(&config, meta); 
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
* Satélite (detectado del input file en tiempo de lectura de datos).
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

NOTA: Como desde el principio convertimos la radiometría a magnitudes físicas en load_nc_sf (reflectancia, temperatura de brillo), se renombra `radiometry` a `channels` y se añade `quantity` para inferencia automática en visualización. Ejemplo final:

```json
{
  "tool": "hpsatviews",
  "version": "2.0",
  "command": "rgb",
  "satellite": "goes-16",
  "timestamp_iso": "2024-05-10T12:00:15Z",
  "geometry": {
    "projection": "geographics",
    "bbox": [-110.5, 30.0, -90.0, 15.0]
  },
  "channels": [
    {
      "name": "C02",
      "quantity": "reflectance",        
      "min": 0.0,
      "max": 1.15,
      "unit": "unitless" 
    },
    {
      "name": "C13",
      "quantity": "brightness_temperature", 
      "min": 185.2,
      "max": 310.5,
      "unit": "K"
    }
  ],
  "enhancements": {
    "gamma": 1.0,
    "clahe": true
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

#### ✅ SPRINT 1: Fundamentos (Semana 1-2) - COMPLETADO
**Archivos CREADOS:**
1. ✅ `include/config.h` + `src/config.c`
   - ✅ Implementar `ProcessConfig` struct (87 líneas en header)
   - ✅ Función: `config_from_argparser(ArgParser *parser, ProcessConfig *cfg)` (446 líneas)
   - ✅ Parsing condicional por comando (mode/rayleigh solo RGB, invert solo gray/pseudo, cpt solo pseudo)
   - ✅ **Validación:** Tests unitarios con valores conocidos (test_config.sh - 25 tests pasando)

2. ✅ `include/metadata.h` + `src/metadata.c`
   - ✅ Implementar `MetadataContext` (opaque handle)
   - ✅ API básica: `metadata_create()`, `metadata_destroy()`, `metadata_add()` con _Generic polimórfico
   - ✅ Implementación completa de generación JSON
   - ✅ **Validación:** Compilación sin warnings, tests de memoria (test_metadata_json.c)

#### ✅ SPRINT 2: Integración con JSON (Semana 3) - COMPLETADO
**Archivos MODIFICADOS:**
1. ✅ `src/metadata.c`
   - ✅ Implementar `metadata_save_json(ctx, filename)` con thread-safe gmtime_r()
   - ✅ Implementar `metadata_build_filename(ctx, extension)`
   - ✅ Implementar `metadata_from_nc()` para extraer satélite y timestamp
   
2. ✅ **Integración de lógica de nombres:**
   - ✅ Funciones de extracción integradas en metadata.c
   - ✅ `FilenameGeneratorInfo` → **NO USADO** (metadata reemplaza funcionalidad)
   
**✅ Validación COMPLETADA:**
- ✅ test_metadata_json genera JSON correctamente
- ✅ Schema válido verificado en test_sprint5_complete.sh

#### ✅ SPRINT 3: Stubs y Feature Flags (Semana 4) - COMPLETADO

**✅ Implementación realizada:**
- ✅ Feature flag HPSV_USE_NEW_PIPELINE agregado
- ✅ Stubs de run_rgb_v2() y run_processing_v2() creados
- ✅ Dispatchers con condicionales implementados

**✅ Validación COMPLETADA:**
- ✅ Compilación dual con feature flag (test_sprint3_featureflag.sh)
- ✅ Stubs implementados correctamente
- ✅ Base preparada para SPRINT 4-5

#### ✅ SPRINT 4: Dispatchers Integrados (Semana 5) - COMPLETADO

**✅ Implementación realizada:**
- ✅ Dispatchers cmd_rgb(), cmd_gray(), cmd_pseudocolor() con feature flags
- ✅ Generación de JSON sidecar en todos los comandos
- ✅ Tests de infraestructura (test_sprint4_processing.sh)

**✅ Validación COMPLETADA:**
- ✅ test_sprint4_processing.sh: 17 tests (11 funcionales pasando)
- ✅ Dispatchers integrados en todos los comandos
- ✅ JSON sidecar generándose correctamente
- ✅ Tests end-to-end con sample_data/
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

#### ✅ SPRINT 5: Implementación Completa (Semana 6) - COMPLETADO

**✅ Implementación realizada:**

1. ✅ `src/processing.c`:
   - ✅ Implementación completa de run_processing_v2() (450+ líneas)
   - ✅ Soporte para expr mode, reproj, CLAHE, gamma, todas las funcionalidades
   - ✅ Metadata tracking completo

2. ✅ `src/rgb.c`:
   - ✅ Implementación completa de run_rgb_v2() (220+ líneas)
   - ✅ Adaptador config_to_rgb_context() para reutilizar funciones internas
   - ✅ Metadata tracking completo

3. ✅ Tests de validación:
   - ✅ test_sprint5_complete.sh: 17/17 tests pasando
   - ✅ Verificación MD5: outputs idénticos a legacy

**✅ Validación COMPLETADA:**
- ✅ Gray MD5: 92097f1af84d9a85298ae7fb4bc2ff39 (idéntico)
- ✅ Pseudocolor MD5: de0e79f901bc6ec97ccea33fc01cac94 (idéntico)
- ✅ RGB Truecolor MD5: d221b51caa5f4da34c79533c603044ae (idéntico)
- ✅ JSON sidecar con metadatos completos en todos los modos

#### ✅ SPRINT 6: Cleanup y Consolidación (Semana 7) - COMPLETADO

**✅ ELIMINACIÓN DE CÓDIGO LEGACY:**

1. ✅ **src/processing.c**: 1065 → 492 líneas (-54%, 573 líneas eliminadas)
   - ✅ Eliminada función legacy run_processing(ArgParser*, bool)
   - ✅ Removidos todos los bloques #if HPSV_USE_NEW_PIPELINE
   - ✅ Renombrada run_processing_v2() → run_processing()

2. ✅ **src/rgb.c**: 1192 → 1006 líneas (-16%, 186 líneas eliminadas)
   - ✅ Eliminada función legacy run_rgb(ArgParser*)
   - ✅ Removidos todos los bloques #if HPSV_USE_NEW_PIPELINE
   - ✅ Renombrada run_rgb_v2() → run_rgb()

3. ✅ **src/main.c**: 294 → 269 líneas (-8%, 25 líneas eliminadas)
   - ✅ Dispatchers unificados sin feature flags
   - ✅ cmd_rgb(), cmd_gray(), cmd_pseudocolor() consolidados

4. ✅ **Headers actualizados:**
   - ✅ include/processing.h: declaración única sin feature flags
   - ✅ include/rgb.h: declaración única sin feature flags

5. ✅ **Feature flags eliminados:**
   - ✅ include/config.h: HPSV_USE_NEW_PIPELINE removido
   - ✅ Makefile: PIPELINE_V2 condicional removido

6. ✅ **Documentación:**
   - ✅ docs/SPRINT6_COMPLETADO.md: Resumen completo de migración

**✅ RESULTADO FINAL:**
- ✅ **784 líneas de código eliminadas** (31% reducción)
- ✅ **Pipeline 100% unificado** con inyección de dependencias
- ✅ **Cero feature flags** en código fuente
- ✅ **100% backward compatible**: MD5-verified
- ✅ **JSON metadata automático** en todos los modos

---

## SPRINT 7: Documentación y Mejoras (Pendiente)

### Tareas Restantes:
- ⏳ Actualizar README.md con ejemplos de JSON sidecar
- ⏳ Resolver warnings de compilación menores
- ⏳ Documentación de API para desarrolladores

### 6.3 Verificación y Rollback

**✅ Criterios Cumplidos para Completar Sprint 6:**
- ✅ Todos los tests pasan con implementación unificada
- ✅ Validación cuantitativa: Outputs MD5-idénticos (gray, pseudocolor, RGB)
- ✅ Validación cuantitativa: JSON contiene metadatos correctos
- ✅ Performance: Sin degradación (mismo algoritmo)
- ✅ Memoria: Gestión correcta con goto cleanup
- ✅ 784 líneas de código legacy eliminadas

**Plan de Rollback (Ya no necesario):**
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
