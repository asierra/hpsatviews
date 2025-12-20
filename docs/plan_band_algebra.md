# Plan de Implementación: Álgebra de Bandas (Linear Combination)

## Objetivo

Extender el comando `gray` para permitir la generación de imágenes basadas en **operaciones aritméticas lineales entre bandas** (por ejemplo, diferencias de temperatura o índices simples), manteniendo la filosofía de **"un solo archivo ancla de entrada"**. 

Una vez funcionando en gray, se podrá extender a rgb, con las mismas opciones pero con un separador ;
---

## Nueva sintaxis

```bash
hpsatviews gray \
  --expr "2.0*C13 - 4.0*C15 + 0.5" \
  --minmax "-5.0,5.0" \
  ...
```

Estrictamente se admiten solo esos caracteres para números flotantes, bandas, operaciones con *,+,- y ; cuando se extienda el soporte a rgb custom. No se admiten paréntesis ni otros signos aritméticos. Se admiten espacios para legibilidad pero serán eliminados por el parser.

### Casos de prueba (para parser)

**Deben funcionar:**
```
✅ "C13"                     # coef implícito 1.0, bias 0.0
✅ "C13-C15"                 # 1.0*C13 - 1.0*C15
✅ "2*C13"                   # sin .0
✅ "2.0*C13"                 # con .0
✅ "-2.0*C13"                # coef negativo al inicio
✅ "C13 - 4.0*C15 + 0.5"     # expresión completa con bias
✅ "0.5*C01+0.3*C02+0.2*C03" # verde sintético
✅ "C13-273.15"              # conversión Kelvin a Celsius
✅ "  C13  -  C15  "         # espacios ignorados
```

**Deben fallar con error claro:**
```
❌ "C13/C15"                 # división no soportada
❌ "(C13-C15)*2"             # paréntesis no soportados
❌ "C13^2"                   # potencias no soportadas
❌ "2**C13"                  # doble asterisco
❌ "C99"                     # banda inválida (solo C01-C16)
❌ "C13 C15"                 # falta operador
❌ "*C13"                    # comienza con operador
❌ "C13-"                    # termina con operador
❌ "2.0.3*C13"               # número mal formado
```

---

## Fase 1: Estructura de datos y parsing

### Objetivo

Traducir la cadena de texto proporcionada por el usuario a una estructura manejable en C **sin usar bibliotecas externas**.

### Definición de estructuras

(En `parse_expr.h`)

```c
typedef struct {
    uint8_t band_id;  // 1-16 (como band_id en DataNC)
    double coeff;           // Coeficiente: 2.0, -4.0, etc.
} LinearTerm;

typedef struct {
    LinearTerm terms[10];   // Máximo 10 términos (ej: "2*C13 - 4*C15")
    int        num_terms;   // Número de términos usados (0-10)
    double     bias;        // Término independiente (ej: +0.5, -273.15)
} LinearCombo;
```

**Nota**: `band_id` es 1-16 (C01=1, C02=2, ..., C16=16), NO el índice del array.

### Implementación del parser

(En `parse_expr.c`)

```c
int parse_expr_string(const char *input, LinearCombo *out);
```

#### Estrategia

* Usar `strtod` para leer números flotantes.
* Recorrer la cadena mediante punteros.
* Retornar 0 en éxito, -1 en error.

#### Algoritmo detallado

```c
// Pseudocódigo del parser
1. Inicializar: num_terms=0, bias=0.0
2. Eliminar espacios iniciales
3. Mientras (*ptr != '\0'):
   a. Leer signo opcional ('+' o '-')
   b. Si hay dígito o punto:
      - Usar strtod() para leer número
      - Si siguiente es '*':
        * Avanzar ptr
        * Leer 'C' y dos dígitos
        * Validar band_id (1-16)
        * Guardar en terms[num_terms++]
      - Sino:
        * Acumular en bias (con signo)
   c. Sino si hay 'C':
      - Coeficiente implícito 1.0 (o -1.0 si signo fue '-')
      - Leer 'C' y dos dígitos
      - Validar band_id (1-16)
      - Guardar en terms[num_terms++]
   d. Sino:
      - Error de sintaxis
   e. Eliminar espacios
   f. Verificar que sigue '+', '-' o fin de cadena
4. Validar num_terms > 0 (al menos una banda)
5. Retornar 0 si éxito
```

#### Manejo de signos negativos

Distinguir contextos:
- **Número negativo**: `-2.0*C13` (inicio), `C13 + -2.0` (después de operador)
- **Operador resta**: `C13 - C15` (después de banda o número sin `*`)

Regla: Un `-` es número negativo si:
- Está al inicio de la expresión
- Sigue inmediatamente a `+` o `-` anterior
- Está después de `*`

#### Compilación standalone

```bash
# Makefile target
test_parser: parse_expr.c
	gcc -DPARSE_EXPR_STANDALONE -Wall -g parse_expr.c -o test_parser -lm

# Uso
./test_parser "2.0*C13 - 4.0*C15 + 0.5"
```

---

## Fase 2: Lógica de archivos (inferencia)

Esto ya lo solucionamos en parte con channelset.h y channelset.c y con el ancla sabemos el instante (la firma sYYYYJJJhhmm) y la ruta exacta dondé podemos encontrar los archivos.

### Objetivo

Localizar y cargar las bandas necesarias basándose **únicamente** en el archivo *ancla* proporcionado por el usuario.

### Análisis de dependencias

* Tras parsear la fórmula, extraer la lista de bandas únicas requeridas (por ejemplo: `C13`, `C15`).

#### Función auxiliar

```c
// En parse_expr.h
int extract_required_channels(const LinearCombo* combo, char** channels_out);

// Implementación en parse_expr.c
int extract_required_channels(const LinearCombo* combo, char** channels_out) {
    int unique_count = 0;
    bool seen[17] = {false}; // seen[1..16]
    
    for (int i = 0; i < combo->num_terms; i++) {
        unsigned char band_id = combo->terms[i].band_id;
        if (!seen[band_id]) {
            seen[band_id] = true;
            channels_out[unique_count] = malloc(4);
            snprintf(channels_out[unique_count], 4, "C%02d", band_id);
            unique_count++;
        }
    }
    channels_out[unique_count] = NULL; // Terminador
    return unique_count;
}

// Uso:
char* required_channels[10];
int n = extract_required_channels(&combo, required_channels);
// required_channels = ["C13", "C15", NULL, ...]
```

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

* Leer argumentos `--minmax min,max`.
* Calcular el factor de escala para normalización:

```c
double scale = 255.0 / (max - min)
```

Esto ya está resuelto en todas nuestras funciones DataF -> ImageData

### Implementación del kernel

```c
// En parse_expr.c o math_ops.c
DataF evaluate_linear_combo(const LinearCombo* combo, const DataNC* channels) {
    // 1. Obtener dimensiones del primer canal
    int ref_idx = combo->terms[0].band_id;
    size_t width = channels[ref_idx].fdata.width;
    size_t height = channels[ref_idx].fdata.height;
    
    // 2. Crear resultado inicializado con bias
    DataF result = dataf_create(width, height);
    dataf_fill(&result, combo->bias);
    
    // 3. Acumular cada término: result += coeff * channel
    for (int i = 0; i < combo->num_terms; i++) {
        unsigned char band_id = combo->terms[i].band_id;
        double coeff = combo->terms[i].coeff;
        
        // Multiplicar canal por coeficiente
        DataF scaled = dataf_op_scalar(&channels[band_id].fdata, coeff, OP_MUL, false);
        
        // Sumar al resultado
        DataF temp = dataf_op_dataf(&result, &scaled, OP_ADD);
        dataf_destroy(&result);
        dataf_destroy(&scaled);
        result = temp;
    }
    
    return result;
}
```

### Conversión a imagen con rango

```c
// Usar función existente pero con rango customizado
ImageData img = dataf_to_image_with_custom_range(&result, minmax[0], minmax[1]);

// O implementar inline:
for (size_t i = 0; i < result.size; i++) {
    float val = result.data_in[i];
    if (val == NonData) {
        img.data[i] = 0;  // Negro para NonData
    } else {
        // Clamp y escalar a [0, 255]
        if (val < minmax[0]) val = minmax[0];
        if (val > minmax[1]) val = minmax[1];
        img.data[i] = (uint8_t)((val - minmax[0]) * 255.0 / (minmax[1] - minmax[0]));
    }
}
```

---

## Fase 5: Integración en processing.c

### Flujo de ejecución

```c
if (ap_found(parser, "expr")) {
    // 1. Parsear expresión
    const char* expr_str = ap_get_str_value(parser, "expr");
    LinearCombo combo;
    if (parse_expr_string(expr_str, &combo) != 0) {
        LOG_ERROR("Invalid expression syntax: %s", expr_str);
        return -1;
    }
    
    // 2. Parsear rango
    float minmax[2] = {0.0f, 255.0f};  // Default
    if (ap_found(parser, "minmax")) {
        const char* minmax_str = ap_get_str_value(parser, "minmax");
        if (sscanf(minmax_str, "%f,%f", &minmax[0], &minmax[1]) != 2) {
            LOG_ERROR("Invalid --minmax format. Use: min,max");
            return -1;
        }
    }
    
    // 3. Extraer bandas requeridas
    char* required_channels[10];
    int n = extract_required_channels(&combo, required_channels);
    LOG_INFO("Expression requires %d bands", n);
    
    // 4. Crear ChannelSet y buscar archivos
    ChannelSet* cset = channelset_create((const char**)required_channels, n);
    // ... (similar a rgb.c)
    
    // 5. Cargar canales
    DataNC channels[17] = {0};  // índices 1-16
    // ... cargar cada canal requerido
    
    // 6. Validar dimensiones y resamplear
    // ... (similar a rgb.c)
    
    // 7. Evaluar expresión
    DataF result = evaluate_linear_combo(&combo, channels);
    
    // 8. Convertir a imagen
    ImageData img = dataf_to_image_with_custom_range(&result, minmax[0], minmax[1]);
    
    // 9. Aplicar post-procesamiento (gamma, clahe, etc.)
    // ... (flujo normal)
    
    // 10. Guardar
    writer_save_png(output_filename, &img);
}
```

---

## Extensión futura: RGB Custom

### Sintaxis propuesta

```bash
hpsatviews rgb --mode custom \
  --expr "C13-C15;C14-C11;C13" --minmax "-5,5;-3,3;00,300" 
  
```

Parsear separando por `;` y evaluar tres expresiones independientes. Abortar con error si no están las 3 expresiones y los tres mimax. Reutilizar o adaptar el código ya existente. 

