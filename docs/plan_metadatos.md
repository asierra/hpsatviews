# Plan de Implementación: Sistema de Metadatos y Arquitectura Unificada v2.0

## Objetivo General

Refactorizar **hpsatviews** para exportar un archivo *sidecar* (`.json`) con información **radiométrica** y **geoespacial** crítica. Para lograrlo de manera sostenible, se unificará la gestión del estado del programa eliminando estructuras redundantes y adoptando patrones de diseño modernos.

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
    bool apply_histogram;
    
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
* **Validación:** Usar `static_assert` (C11) para validar tamaños de buffers, estructuras de canales y asunciones de tipos en tiempo de compilación ("Fail Fast").

### B. Módulo de Metadatos (`metadatos.c`)
Implementar el contenedor opaco y la lógica de JSON.
* **Absorción de Lógica:** Integrar la lógica de `filename_utils.c` dentro de este módulo. El `MetadataContext` será responsable de generar el nombre de archivo basado en los datos acumulados (satélite detectado, hora real del scan, etc.).
* **Lifetimes:** Implementar constructores y destructores claros (`metadata_create`, `metadata_destroy`).

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

```json
{
  "processing_tool": "hpsatviews",
  "version": "1.0",
  "command": "rgb",
  "mode": "truecolor",
  "satellite": "goes-16",
  "input_file": "OR_ABI-L2-CMIPF-...",
  "time_iso": "2024-05-10T12:00:00Z",
  "geometry": {
    "projection": "geographics",
    "bbox": [-110.0, 30.0, -80.0, 10.0] 
  },
  "radiometry": [
    {
      "name": "C01",
      "min_radiance": 0.0,
      "max_radiance": 612.5,
      "unit": "W/m^2/sr/um"
    }
  ],
  "enhancements": {
    "gamma": 1.4,
    "clahe": true
  }
}
```

#### 1. Caso: Escala de Grises Invertida (Canal IR)
**Escenario:** Canal 13 (Infrarrojo limpio). En meteorología, solemos invertirlo: las nubes frías (valores bajos de Kelvin) se ven blancas (píxel 255), y el suelo caliente (valores altos) se ve negro.

* **Punto Clave:** El `campo invert_values: true`. Esto le dice al visualizador que la barra de colores debe dibujarse al revés (ej. de 320K abajo a 180K arriba).

```json
{
  "processing_tool": "hpsatviews",
  "version": "1.0",
  "command": "gray",
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

## Fase 6: Verificación y Testing

### Prueba de Consistencia (Modern C)

* Verificar que static_assert protege contra cambios inesperados en la estructura de canales o tamaños de tipos.

* Procesar una imagen conocida y validar que el JSON coincida con los valores físicos esperados.

### Prueba de Recorte
* Ejecutar --clip mexico.

* Confirmar en el JSON que geometry.bbox corresponde a las coordenadas de México y no al disco completo.

## Prueba de Integración

* Flujo completo: hpsatviews → JSON → mapdrawer.

* Validar visualmente la barra de colores y la leyenda generada.

