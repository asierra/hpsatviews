# Plan de Implementación: Álgebra de Bandas (Linear Combination)

## Objetivo

Extender el comando `gray` para permitir la generación de imágenes basadas en **operaciones aritméticas lineales entre bandas** (por ejemplo, diferencias de temperatura o índices simples), manteniendo la filosofía de **"un solo archivo ancla de entrada"**. Una vez funcionando en gray, se podrá extender a rgb, permitiendo simplemente tres --calc - tres --minmax (o --range)

---

## Nueva sintaxis

```bash
hpsatviews gray \
  --calc "2.0*C13 - 4.0*C15 + 0.5" \
  --range "-5.0,5.0" \
  ...
```

Discutir la conveniencia de usar --calc o buscar alguna alternativa, pues es algebra muy simple, no una calculadora complicada. Y tal vez --minmax es más intuitivo que --range.

---

## Fase 1: Estructura de datos y parsing

### Objetivo

Traducir la cadena de texto proporcionada por el usuario a una estructura manejable en C **sin usar bibliotecas externas**.

### Definición de estructuras

(En `math_ops.h`)

```c
typedef struct {
    char   band_id;  // Como en DataNC
    double coeff;         // Ejemplo: 2.0
} LinearTerm;

typedef struct {
    LinearTerm terms[8]; // Máximo 8 términos
    int        num_terms;
    double     bias;      // Término independiente
} LinearCombo;
```

### Implementación del parser

(En `math_ops.c`)

```c
int parse_calc_string(const char *input, LinearCombo *out);
```

#### Estrategia

* Usar `strtod` para leer números flotantes.
* Recorrer la cadena mediante punteros.

#### Lógica

1. Leer número.
2. Leer delimitador (`*`).
3. Leer nombre de banda (alfanumérico).
4. Si no hay banda después del número, acumular en `bias`.
5. Manejar espacios en blanco y signos negativos.

---

## Fase 2: Lógica de archivos (inferencia)

Esto ya lo solucionamos en parte con channelset.h y channelset.c y con el ancla sabemos el instante (la firma sYYYYJJJhhmm) y la ruta exacta dondé podemos encontrar los archivos.

### Objetivo

Localizar y cargar las bandas necesarias basándose **únicamente** en el archivo *ancla* proporcionado por el usuario.

### Análisis de dependencias

* Tras parsear la fórmula, extraer la lista de bandas únicas requeridas (por ejemplo: `C13`, `C15`).

### Generación de rutas (path construction)

Para cada banda en la fórmula: (ya resuelto)

1. Extraer *timestamp* y prefijos del nombre del archivo ancla.
2. Sustituir el identificador de canal (ejemplo: reemplazar `C13` por `C15`).
3. Verificar la existencia del archivo (`access()` o `stat()`).

**Manejo de errores**:

* Si falta un archivo, abortar con un mensaje claro:

```text
Missing companion file for band {CH}
```

---

## Fase 3: Gestión de memoria y carga

### Objetivo

Cargar múltiples imágenes en memoria de forma eficiente.

### Estrategia de carga

* Carga secuencial o paralela (opcional).
* Cargar los datos de todas las bandas requeridas en arreglos DataF.

### Consideración crítica

* El *loader* **debe devolver valores físicos calibrados**:

  * Kelvin para IR.
  * Reflectancia para VIS.
* Aplicar `scale_factor` y `add_offset` del NetCDF. 
* **No** operar con cuentas crudas (`uint16`).
* Todo esto ya lo hacemos.

### Validación de dimensiones

* Verificar que todas las matrices tengan exactamente el mismo `width` y `height`.
* Si hace falta, aplicar downsampling por defecto o upsampling si --full-res como ya hacemos en rgb.c

---

## Fase 4: Núcleo de procesamiento (Math Kernel)

### Objetivo

Ejecutar la operación matemática píxel a píxel usando **OpenMP**.
Usar las funciones dataf_op_dataf y dataf_op_scalar en datanc.c 

### Preparación de parámetros

* Leer argumentos `--range min,max`.
* Calcular el factor de escala para normalización:

```c
double scale = 255.0 / (max - min)
```

Tal vez solo hace falta crear math_ops.c/h y aprovechar código en channelset.c/h, rgb.c/h y datanc.h/c.

Discutir nombres apropiados.
