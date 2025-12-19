# Plan de Implementación: Álgebra de Bandas (Linear Combination)

## Objetivo

Extender el comando `gray` para permitir la generación de imágenes basadas en **operaciones aritméticas lineales entre bandas** (por ejemplo, diferencias de temperatura o índices simples), manteniendo la filosofía de **"un solo archivo de entrada"**. Una vez funcionando en gray, se podrá extender a rgb, permitiendo simplemente tres --calc - tres --minmax (o --range)

---

## Nueva sintaxis

```bash
hpsatviews gray \
  --calc "1.0*C13 - 1.0*C15 + 0.5" \
  --range "-5.0,5.0" \
  ...
```

---

## Fase 1: Estructura de datos y parsing

### Objetivo

Traducir la cadena de texto proporcionada por el usuario a una estructura manejable en C **sin usar bibliotecas externas**.

### Definición de estructuras

(En `types.h` o `math_ops.h`)

```c
typedef struct {
    char   band_name[8];  // Ejemplo: "C13"
    double coeff;         // Ejemplo: 1.0
} LinearTerm;

typedef struct {
    LinearTerm terms[10]; // Máximo 10 términos
    int        num_terms;
    double     bias;      // Término independiente
} LinearCombo;
```

### Implementación del parser

(En `utils.c`)

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

### Objetivo

Localizar y cargar las bandas necesarias basándose **únicamente** en el archivo *ancla* proporcionado por el usuario.

### Análisis de dependencias

* Tras parsear la fórmula, extraer la lista de bandas únicas requeridas (por ejemplo: `C13`, `C15`).

### Identificación del ancla

* Detectar qué banda corresponde al archivo recibido en `argv`.

### Generación de rutas (path construction)

Para cada banda faltante en la fórmula:

1. Extraer *timestamp* y prefijos del nombre del archivo ancla.
2. Sustituir el identificador de canal (ejemplo: reemplazar `C13` por `C15`).
3. Verificar la existencia del archivo (`access()` o `stat()`).

**Manejo de errores**:

* Si falta un archivo, abortar con un mensaje claro:

```text
Missing companion file for band C15
```

---

## Fase 3: Gestión de memoria y carga

### Objetivo

Cargar múltiples imágenes en memoria de forma eficiente.

### Estrategia de carga

* Carga secuencial o paralela (opcional).
* Cargar los datos de todas las bandas requeridas en arreglos de `float`.

### Consideración crítica

* El *loader* **debe devolver valores físicos calibrados**:

  * Kelvin para IR.
  * Reflectancia para VIS.
* Aplicar `scale_factor` y `add_offset` del NetCDF.
* **No** operar con cuentas crudas (`uint16`).

### Validación de dimensiones

* Verificar que todas las matrices tengan exactamente el mismo `width` y `height`.

---

## Fase 4: Núcleo de procesamiento (Math Kernel)

### Objetivo

Ejecutar la operación matemática píxel a píxel usando **OpenMP**.

### Preparación de parámetros

* Leer argumentos `--range min,max`.
* Calcular el factor de escala para normalización:

```c
double scale = 255.0 / (max - min)
```
