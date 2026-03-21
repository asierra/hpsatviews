# Plan: Ratio Sharpening para Truecolor

**Fecha:** 21 de Marzo de 2026  
**Reemplaza:** Sección 8 de PLAN_IMPLEMENTACION_REALCES.md (Unsharp Mask incorrecto)  
**Algoritmo real:** Ratio Sharpening (SelfSharpenedRGB de geo2grid/satpy)

---

## 1. Corrección al plan anterior

| | Plan anterior (Sección 8) | Algoritmo real (geo2grid) |
|---|---|---|
| **Método** | Unsharp Mask (aditivo) | Ratio Sharpening (multiplicativo) |
| **Operación** | `band + (C02 - blur(C02)) × amount` | `band × (C02 / mean_2x2(C02))` |
| **Complejidad** | Necesita upsample + downsample + resta | Solo promedio 2×2 + división |
| **Parámetros** | Factor `amount` arbitrario | Sin parámetros tunables (clip fijo a 1.5) |
| **Robustez** | Puede generar halos y valores negativos | Multiplicativo, preserva rango |

---

## 2. Algoritmo de geo2grid (SelfSharpenedRGB)

```
red_mean = mean_2x2(comp_r)              // promedio por bloques 2×2, mismo tamaño
ratio    = comp_r / red_mean             // variación local de cada pixel
ratio    = sanitize(ratio)               // NaN/inf/negativo → 1.0
ratio    = clip(ratio, 0, 1.5)           // máximo 50% de amplificación
comp_g  *= ratio                         // inyectar contraste local en verde
comp_b  *= ratio                         // inyectar contraste local en azul
```

**¿Por qué funciona?** C02 (rojo) tiene la información de bordes y texturas finas. 
El ratio captura las variaciones locales de contraste y las propaga a verde y azul, 
que tienen menor resolución o son sintéticos.

**Funciona a cualquier resolución de trabajo** (no requiere `--full-res`):
- A **0.5km** (`--full-res`): máximo beneficio, contraste sub-kilométrico
- A **1km**: buen beneficio, contraste kilométrico
- A **2km** (default): beneficio reducido pero presente

---

## 3. Pasos de implementación

### Paso 1: ProcessConfig (`include/config.h`)
Agregar `bool use_sharpen;` junto a `use_piecewise_stretch` en el struct `ProcessConfig`.

### Paso 2: Registrar flag (`src/args.c`)
Agregar `ap_add_flag(parser, "sharpen");` en el bloque rgb del dispatcher.

### Paso 3: Parsear flag (`src/config.c`)
Agregar `cfg->use_sharpen = ap_found(parser, "sharpen");` después de la línea de stretch.

### Paso 4: Texto de ayuda (`include/help_en.h` + `include/help_es.h`)
- EN: `"  --sharpen       Ratio sharpening (truecolor/daynite).\n"`
- ES: `"  --sharpen       Nitidez por ratio (truecolor/daynite).\n"`

### Paso 5: `dataf_mean_2x2` (`include/datanc.h` + `src/datanc.c`)

Declarar en `datanc.h`:
```c
/**
 * @brief Promedio por bloques 2×2, resultado del mismo tamaño que la entrada.
 * Cada bloque de 2×2 pixeles se reemplaza por su promedio.
 * Maneja bordes impares y salta valores NonData.
 */
DataF dataf_mean_2x2(const DataF *input);
```

Implementar en `datanc.c`:
- Crear DataF de mismo tamaño
- Iterar bloques 2×2 con `#pragma omp parallel for`
- Promediar valores válidos (skip `IS_NONDATA()`), escribir promedio en las 4 posiciones
- Bordes (ancho/alto impar): bloques parciales → promediar lo disponible
- Llamar `dataf_find_min_max()` al final

### Paso 6: `dataf_ratio_sharpen_map` (truecolor.h + truecolor.c)

Declarar en `truecolor.h`:
```c
/**
 * @brief Genera un mapa de ratio de sharpening a partir de un canal de referencia.
 * Calcula ratio = channel / mean_2x2(channel), sanitiza valores inválidos (→ 1.0)
 * y limita a [0, 1.5]. El resultado se multiplica sobre otros canales para
 * transferir el contraste local del canal de referencia.
 */
DataF dataf_ratio_sharpen_map(const DataF *channel);
```

Implementar en `truecolor.c`:
- Llamar `dataf_mean_2x2(channel)` → mean
- Loop paralelo con `#pragma omp parallel for`
- `ratio[i] = channel[i] / mean[i]`
- Si `!isfinite(ratio) || ratio < 0.0f` → `1.0f`
- `ratio = fminf(ratio, 1.5f)`
- Destruir mean, retornar ratio_map

### Paso 7: Integrar en pipeline (rgb.c)

En `compose_truecolor()`, insertar DESPUÉS del green boost (×1.05) y ANTES del 
piecewise stretch:

```c
if (ctx->opts.use_sharpen) {
    LOG_INFO("Aplicando ratio sharpening...");
    DataF ratio_map = dataf_ratio_sharpen_map(&ctx->comp_r);
    if (ratio_map.data_in) {
        DataF new_g = dataf_op_dataf(&ctx->comp_g, &ratio_map, OP_MUL);
        dataf_destroy(&ctx->comp_g);
        ctx->comp_g = new_g;
        DataF new_b = dataf_op_dataf(&ctx->comp_b, &ratio_map, OP_MUL);
        dataf_destroy(&ctx->comp_b);
        ctx->comp_b = new_b;
        dataf_destroy(&ratio_map);
    }
}
```

### Paso 8: Verificación

```bash
# Con sharpening
hpsv rgb -m truecolor --rayleigh --sharpen --stretch archivo.nc -o con_sharpen.png

# Sin sharpening (referencia)
hpsv rgb -m truecolor --rayleigh --stretch archivo.nc -o sin_sharpen.png

# Comparar contra geo2grid
# Usar g2g_20260791800.png de testtc/

# Máximo efecto
hpsv rgb -m truecolor --rayleigh --sharpen --stretch --full-res archivo.nc -o full_res.png
```

---

## 4. Archivos a modificar

| Archivo | Cambio | Líneas est. |
|---------|--------|-------------|
| config.h | `bool use_sharpen` en ProcessConfig | +1 |
| args.c | `ap_add_flag(parser, "sharpen")` | +1 |
| config.c | `cfg->use_sharpen = ap_found(...)` | +1 |
| help_en.h | Línea de ayuda EN | +1 |
| help_es.h | Línea de ayuda ES | +1 |
| datanc.h | Declaración `dataf_mean_2x2` | +5 |
| datanc.c | Implementación `dataf_mean_2x2` | +40 |
| truecolor.h | Declaración `dataf_ratio_sharpen_map` | +6 |
| truecolor.c | Implementación `dataf_ratio_sharpen_map` | +35 |
| rgb.c | Invocar sharpening en `compose_truecolor` | +12 |
| **Total** | | **~103** |

---

## 5. Decisiones

- **Flag explícito** `--sharpen`, independiente de `--rayleigh` (separar concerns)
- `dataf_mean_2x2` en `datanc.c` — operación genérica sobre DataF, reutilizable
- `dataf_ratio_sharpen_map` en `truecolor.c` — lógica específica del pipeline visual
- No crear módulo nuevo (`sharpening.c`) — no justificado para ~75 líneas de lógica core
- Clip a **1.5** fijo (igual que geo2grid), no configurable
- Doc comments solo en headers, `.c` limpio
- Destruir intermedios con `dataf_destroy()` antes de reasignar

---

## 6. Criterios de éxito

1. **Visual:** Bordes de nubes y costas visiblemente más nítidos con `--sharpen`
2. **No-regresión:** Sin `--sharpen`, output idéntico al anterior
3. **Performance:** Overhead < 200ms en full disk con OpenMP
4. **Compilación:** Sin warnings con `-Wall -Wextra`
5. **Paridad:** Resultado con `--sharpen --full-res` visualmente comparable a geo2grid
```

Puedes habilitar las herramientas de edición para que lo aplique directamente, o copiar el contenido al archivo.Puedes habilitar las herramientas de edición para que lo aplique directamente, o copiar el contenido al archivo.
