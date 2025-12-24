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
* Recorrer la cadena mediante apuntadores.
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

## Extensión RGB modo Custom

### Objetivo

Permitir composiciones RGB personalizadas usando expresiones algebraicas en cada canal, reutilizando toda la infraestructura ya implementada para `gray`.

### Sintaxis propuesta

```bash
hpsatviews rgb --mode custom \
  --expr "C13-C15; C14-C11; C13" \
  --minmax "-5,5; -3,3; 200,300" \
  archivo_C13.nc
```

**Formato:**
- `--expr`: 3 expresiones separadas por `;` (una para R, G, B)
- `--minmax`: 3 rangos separados por `;` (formato: `min,max`)
- Espacios alrededor de `;` son opcionales e ignorados

### Casos de prueba (para validación)

**Deben funcionar:**
```bash
✅ --expr "C13-C15;C14-C11;C13" --minmax "-5,5;-3,3;200,300"
✅ --expr "C13; C14; C15" --minmax "200,300; 200,300; 200,300"
✅ --expr "2.0*C13-273.15; C14; 0.5*C15" --minmax "-80,50; 200,300; 100,200"
✅ --expr " C13 - C15 ; C14 - C11 ; C13 " --minmax " -5 , 5 ; -3 , 3 ; 200 , 300 "
✅ --expr "C15-C13;C14-C11;C13" --minmax "-6.7,2.6;-6.0,6.3;243.6,302.4" # ash
✅ --expr "C09-C10;C13-C11;C13" --minmax "-4.0,2.0;-4.0,5.0;233.0,300.0" # so2
✅ --expr "C08-C10;C12-C13;C08-275.15" --minmax "-26.2,0.6;-43.2, 6.7;29.25,64.65" # airmass
```

**Deben fallar con error claro:**
```bash
❌ --expr "C13-C15;C14-C11" --minmax "-5,5;-3,3;200,300"  # Solo 2 expresiones
❌ --expr "C13-C15;C14-C11;C13" --minmax "-5,5;-3,3"      # Solo 2 rangos
❌ --expr "C13-C15;C99;C13" --minmax "-5,5;-3,3;200,300"  # Banda inválida en G
❌ --expr "C13;C14;C15" --minmax "-5,5;-3,3;invalid"      # Rango malformado
❌ --expr "C13;C14;C15" --minmax "5,-5;-3,3;200,300"      # min > max
```

---

## Plan de Implementación: RGB Custom

### Fase 1: Parser de múltiples expresiones

**Objetivo**: Extender el parser para manejar 3 expresiones y 3 rangos.

#### Nuevas funciones en `parse_expr.c`

```c
/**
 * @brief Parsea una cadena con 3 expresiones separadas por ';'
 * @param input Cadena como "C13-C15; C14-C11; C13"
 * @param combos Array de 3 LinearCombo para almacenar las expresiones
 * @return 0 si éxito, -1 si error
 */
int parse_expr_rgb(const char *input, LinearCombo combos[3]);

/**
 * @brief Parsea una cadena con 3 rangos separados por ';'
 * @param input Cadena como "-5,5; -3,3; 200,300"
 * @param ranges Array de 6 floats: [r_min, r_max, g_min, g_max, b_min, b_max]
 * @return 0 si éxito, -1 si error
 */
int parse_minmax_rgb(const char *input, float ranges[6]);

/**
 * @brief Extrae todas las bandas únicas requeridas por las 3 expresiones
 * @param combos Array de 3 LinearCombo
 * @param channels_out Array de strings para nombres de canales (ej: "C13", "C15")
 * @return Número de bandas únicas (sin duplicados entre R, G, B)
 */
int extract_required_channels_rgb(const LinearCombo combos[3], char** channels_out);
```

#### Estrategia de implementación

```c
int parse_expr_rgb(const char *input, LinearCombo combos[3]) {
    char *input_copy = strdup(input);
    if (!input_copy) return -1;
    
    // Tokenizar por ';'
    char *saveptr;
    char *token = strtok_r(input_copy, ";", &saveptr);
    
    for (int i = 0; i < 3; i++) {
        if (!token) {
            LOG_ERROR("Se esperan exactamente 3 expresiones separadas por ';', encontradas %d", i);
            free(input_copy);
            return -1;
        }
        
        // Parsear expresión individual (reutilizar parse_expr_string)
        if (parse_expr_string(token, &combos[i]) != 0) {
            LOG_ERROR("Error en expresión %d: %s", i+1, token);
            free(input_copy);
            return -1;
        }
        
        token = strtok_r(NULL, ";", &saveptr);
    }
    
    // Verificar que no hay más expresiones
    if (token != NULL) {
        LOG_ERROR("Se esperan exactamente 3 expresiones, se encontraron más");
        free(input_copy);
        return -1;
    }
    
    free(input_copy);
    return 0;
}

int parse_minmax_rgb(const char *input, float ranges[6]) {
    char *input_copy = strdup(input);
    if (!input_copy) return -1;
    
    char *saveptr;
    char *token = strtok_r(input_copy, ";", &saveptr);
    
    for (int i = 0; i < 3; i++) {
        if (!token) {
            LOG_ERROR("Se esperan exactamente 3 rangos separados por ';', encontrados %d", i);
            free(input_copy);
            return -1;
        }
        
        // Parsear par min,max
        float min_val, max_val;
        if (sscanf(token, "%f,%f", &min_val, &max_val) != 2) {
            LOG_ERROR("Rango %d malformado: %s (formato esperado: min,max)", i+1, token);
            free(input_copy);
            return -1;
        }
        
        // Validar que min < max
        if (min_val >= max_val) {
            LOG_ERROR("Rango %d inválido: min (%.2f) >= max (%.2f)", i+1, min_val, max_val);
            free(input_copy);
            return -1;
        }
        
        ranges[i*2] = min_val;
        ranges[i*2 + 1] = max_val;
        
        token = strtok_r(NULL, ";", &saveptr);
    }
    
    if (token != NULL) {
        LOG_ERROR("Se esperan exactamente 3 rangos, se encontraron más");
        free(input_copy);
        return -1;
    }
    
    free(input_copy);
    return 0;
}

int extract_required_channels_rgb(const LinearCombo combos[3], char** channels_out) {
    bool seen[17] = {false};  // seen[1..16]
    int unique_count = 0;
    
    // Iterar sobre las 3 expresiones
    for (int expr = 0; expr < 3; expr++) {
        for (int term = 0; term < combos[expr].num_terms; term++) {
            uint8_t band_id = combos[expr].terms[term].band_id;
            if (!seen[band_id]) {
                seen[band_id] = true;
                channels_out[unique_count] = malloc(4);
                snprintf(channels_out[unique_count], 4, "C%02d", band_id);
                unique_count++;
            }
        }
    }
    
    channels_out[unique_count] = NULL;
    return unique_count;
}
```

---

### Fase 2: Nueva estrategia RGB "custom"

**Objetivo**: Integrar el modo "custom" en el sistema de estrategias de `rgb.c`.

#### Modificaciones en `rgb.c`

1. **Agregar estrategia "custom" al array STRATEGIES**:

```c
// En rgb.c, línea ~330
static const RgbStrategy STRATEGIES[] = {
    // ... estrategias existentes ...
    
    { "custom", {NULL}, compose_custom, 
      "Combinación personalizada usando expresiones algebraicas (requiere --expr y --minmax)", 
      false },
    
    { NULL, {NULL}, NULL, NULL, false }  // Centinela
};
```

**Nota**: `req_channels` es `{NULL}` porque los canales se determinan dinámicamente según las expresiones.

2. **Implementar `compose_custom`**:

```c
static ImageData compose_custom(RgbContext *ctx) {
    // Las expresiones ya fueron parseadas en rgb_parse_options
    // y los canales cargados en load_channels_custom
    
    // Evaluar las 3 expresiones
    DataF r_result = evaluate_linear_combo(&ctx->custom_combos[0], ctx->channels);
    DataF g_result = evaluate_linear_combo(&ctx->custom_combos[1], ctx->channels);
    DataF b_result = evaluate_linear_combo(&ctx->custom_combos[2], ctx->channels);
    
    if (!r_result.data_in || !g_result.data_in || !b_result.data_in) {
        LOG_ERROR("Fallo al evaluar una o más expresiones");
        dataf_destroy(&r_result);
        dataf_destroy(&g_result);
        dataf_destroy(&b_result);
        return image_create(0, 0, 0);
    }
    
    // Usar los rangos personalizados de --minmax
    ImageData result = create_multiband_rgb(
        &r_result, &g_result, &b_result,
        ctx->custom_ranges[0], ctx->custom_ranges[1],  // R: min, max
        ctx->custom_ranges[2], ctx->custom_ranges[3],  // G: min, max
        ctx->custom_ranges[4], ctx->custom_ranges[5]   // B: min, max
    );
    
    dataf_destroy(&r_result);
    dataf_destroy(&g_result);
    dataf_destroy(&b_result);
    
    return result;
}
```

---

### Fase 3: Extender RgbContext

**Objetivo**: Agregar campos para expresiones y rangos personalizados.

#### Modificaciones en `rgb.h`

```c
typedef struct {
    // ... campos existentes ...
    
    // Modo custom (álgebra de bandas)
    bool is_custom_mode;              // true si mode == "custom"
    LinearCombo custom_combos[3];     // Expresiones para R, G, B
    float custom_ranges[6];           // [r_min, r_max, g_min, g_max, b_min, b_max]
    char* custom_channels[17];        // Lista de canales requeridos (para free)
    
    // ... resto de campos ...
} RgbContext;
```

---

### Fase 4: Integrar parsing en `rgb_parse_options`

**Objetivo**: Detectar modo "custom" y parsear `--expr` y `--minmax`.

#### Modificaciones en `rgb.c`

```c
bool rgb_parse_options(ArgParser *parser, RgbContext *ctx) {
    // ... parsing existente ...
    
    ctx->opts.mode = ap_get_str_value(parser, "mode");
    
    // Detectar modo custom
    ctx->is_custom_mode = (strcmp(ctx->opts.mode, "custom") == 0);
    
    if (ctx->is_custom_mode) {
        // Validar que --expr está presente
        if (!ap_found(parser, "expr")) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "El modo 'custom' requiere la opción --expr");
            return false;
        }
        
        const char* expr_str = ap_get_str_value(parser, "expr");
        if (parse_expr_rgb(expr_str, ctx->custom_combos) != 0) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "Error al parsear --expr: %s", expr_str);
            return false;
        }
        
        // Parsear --minmax (obligatorio para custom)
        if (!ap_found(parser, "minmax")) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "El modo 'custom' requiere la opción --minmax");
            return false;
        }
        
        const char* minmax_str = ap_get_str_value(parser, "minmax");
        if (parse_minmax_rgb(minmax_str, ctx->custom_ranges) != 0) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "Error al parsear --minmax: %s", minmax_str);
            return false;
        }
        
        LOG_INFO("Modo custom detectado:");
        LOG_INFO("  R: Rango [%.2f, %.2f]", ctx->custom_ranges[0], ctx->custom_ranges[1]);
        LOG_INFO("  G: Rango [%.2f, %.2f]", ctx->custom_ranges[2], ctx->custom_ranges[3]);
        LOG_INFO("  B: Rango [%.2f, %.2f]", ctx->custom_ranges[4], ctx->custom_ranges[5]);
    }
    
    return true;
}
```

---

### Fase 5: Carga dinámica de canales en modo custom

**Objetivo**: Cargar solo los canales requeridos por las 3 expresiones.

#### Modificaciones en `load_channels`

```c
static bool load_channels(RgbContext *ctx, const RgbStrategy *strategy) {
    int count;
    const char** channels_to_load;
    
    if (ctx->is_custom_mode) {
        // Modo custom: extraer canales de las expresiones
        count = extract_required_channels_rgb(ctx->custom_combos, ctx->custom_channels);
        if (count == 0) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "No se encontraron bandas válidas en las expresiones");
            return false;
        }
        channels_to_load = (const char**)ctx->custom_channels;
        LOG_INFO("Modo custom requiere %d canales únicos", count);
    } else {
        // Modo normal: usar strategy->req_channels
        count = 0;
        while (strategy->req_channels[count] != NULL) count++;
        channels_to_load = strategy->req_channels;
    }
    
    // Crear ChannelSet
    ctx->channel_set = channelset_create(channels_to_load, count);
    if (!ctx->channel_set) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), 
                 "Falla de memoria al crear ChannelSet.");
        return false;
    }
    
    // ... resto del código de carga (igual que antes) ...
}
```

---

### Fase 6: Limpieza de recursos en modo custom

**Objetivo**: Liberar memoria de las expresiones y canales dinámicos.

#### Modificaciones en `rgb_context_destroy`

```c
void rgb_context_destroy(RgbContext *ctx) {
    if (!ctx) return;

    // Liberar ChannelSet
    channelset_destroy(ctx->channel_set);
    
    // Liberar canales custom (si fueron malloc'd)
    if (ctx->is_custom_mode) {
        for (int i = 0; ctx->custom_channels[i] != NULL; i++) {
            free(ctx->custom_channels[i]);
        }
    }
    
    // ... resto de limpieza (igual que antes) ...
}
```

---

### Ejemplos de uso

#### Ash RGB (equivalente a --mode ash pero personalizado)
```bash
hpsatviews rgb --mode custom \
  --expr "C15-C13; C14-C11; C13" \
  --minmax "-6.7,2.6; -6.0,6.3; 243.6,302.4" \
  -o ash_custom.png archivo_C13.nc
```

#### Diferencial de temperatura en falso color
```bash
hpsatviews rgb --mode custom \
  --expr "C13-C15; C14-C15; C13" \
  --minmax "-5,5; -3,3; 200,300" \
  --clahe --gamma 1.2 \
  -o temp_diff.png archivo_C13.nc
```

#### Conversión Kelvin a Celsius en RGB
```bash
hpsatviews rgb --mode custom \
  --expr "C13-273.15; C14-273.15; C15-273.15" \
  --minmax "-80,50; -80,50; -80,50" \
  -o celsius_rgb.png archivo_C13.nc
```

---

### Notas de implementación

1. **Reutilización**: El 90% del código ya existe (parser, evaluate_linear_combo, create_multiband_rgb).

2. **Validación robusta**: Cada fase debe validar:
   - Número exacto de expresiones (3)
   - Número exacto de rangos (3)
   - Sintaxis correcta en cada expresión
   - Bandas válidas (C01-C16)
   - min < max en cada rango

3. **Compatibilidad**: Los modos existentes no se modifican, solo se añade "custom".

4. **Mensajes de error**: Deben ser claros y específicos:
   - "Se esperan 3 expresiones, encontradas 2"
   - "Rango 2 inválido: min (5.0) >= max (3.0)"
   - "Error en expresión 1: banda C99 no válida"

5. **Testing sugerido**:
   ```bash
   # Caso básico
   --expr "C13;C14;C15" --minmax "200,300;200,300;200,300"
   
   # Con álgebra
   --expr "C13-C15;C14-C11;C13" --minmax "-5,5;-3,3;200,300"
   
   # Con espacios (deben ignorarse)
   --expr " C13 - C15 ; C14 ; C15 " --minmax " -5 , 5 ; 200 , 300 ; 200 , 300 "
   
   # Errores
   --expr "C13;C14" --minmax "-5,5;-3,3;200,300"  # Faltan expresiones
   --expr "C13;C14;C99" --minmax "-5,5;-3,3;200,300"  # Banda inválida
   ```

---

### Estimación de trabajo

- **Fase 1** (Parser múltiple): ~2 horas - Reutiliza parse_expr_string existente
- **Fase 2** (Estrategia custom): ~1 hora - Trivial, solo compose_custom
- **Fase 3** (Extender RgbContext): ~30 min - Solo agregar campos
- **Fase 4** (Parsing en opciones): ~1 hora - Lógica de validación
- **Fase 5** (Carga dinámica): ~1 hora - Modificar load_channels
- **Fase 6** (Limpieza): ~30 min - Agregar frees

**Total estimado: 6 horas** de implementación limpia reutilizando todo lo existente. 

