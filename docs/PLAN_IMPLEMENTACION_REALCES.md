# PLAN DE IMPLEMENTACIÓN: REALCES TRUE COLOR Y CORRECCIONES FÍSICAS (V2)

**Fecha:** 31 de Enero de 2026
**Objetivo:** Refactorizar la cadena de procesamiento de hpsatviews para igualar la calidad visual de geo2grid, corrigiendo errores físicos (Rayleigh/Geometría) e implementando realces de contraste (SZA/Stretch).

---

## 1. DIAGNÓSTICO Y SOLUCIONES

Para que hpsatviews genere imágenes true_color similares a geo2grid, necesitamos implementar:

### Cambios Obligatorios (Prioridad Alta)
1. ✅ **Corrección Rayleigh** - YA IMPLEMENTADO (con issues menores)
2. ✅ **Corrección Solar Zenith (SZA)** - IMPLEMENTADO
3. ⚠️ **Orden de Operaciones Correcto** - EN PROGRESO
4. ✅ **Piecewise Linear Stretch** - IMPLEMENTADO (realce principal)

| Problema Actual | Causa Raíz Técnica | Solución Propuesta |
| :--- | :--- | :--- |
| **Imágenes "lavadas" / azuladas** | Multiplicación redundante de `tau` en LUTs (subestima corrección). | Eliminar factor `* tau` en `rayleigh.c`. |
| **Bordes oscuros (Viñeteado)** | Falta de corrección Cenital Solar (SZA). | Implementar división por `cos(sza)`. |
| **"Amarillos" o colores sucios** | Generación del Canal Verde usando datos sin corregir. | Mover generación del verde *después* de Rayleigh y SZA. |
| **Contraste pobre en sombras** | Falta de curva de estiramiento (Stretch). | Implementar `Piecewise Linear Stretch` de geo2grid. |
| **Errores geométricos** | Vector de visión del satélite invertido. | Invertir cálculo `dx, dy, dz` en `reader_nc.c`. |

---

## 2. ARQUITECTURA DEL PIPELINE (ORDEN ESTRICTO)

El orden de las operaciones es crítico. El flujo debe ser condicional según el tipo de producto (L1b vs L2).

### Algoritmo General (pseudocódigo en `rgb.c`)

Nota: Resolver la contradicción con SZA -> Rayleigh -> Verde. ¿Cómo trabajamos con los pixeles negativos? Si hacemos primero Rayleigh, luego SZA, y luego Verde, ¿qué pasa con los pixeles negativos?

1.  **Entrada:** Cargar canales R, B, NIR (Normalizados 0.0 - 1.0).
2.  **Paso A: Corrección Atmosférica**
    * Aplicar `luts_rayleigh_correction` al canal Azul (y Rojo opcional).
    * *Nota:* La función debe tener clamping interno (`val < 0 ? 0 : val`).
3.  **Paso B: Corrección Cenital Solar (Solo L1b)**
    * *Condición:* `if (is_L1b && sza_pixel < 85.0)`
    * *Operación:* `Pixel = Pixel / cos(deg2rad(sza))`
    * *Razón:* L2 (MCMIP) ya viene corregido; aplicar esto quemaría la imagen.
4.  **Paso C: Generación de Verde Sintético**
    * Usar bandas R, B, NIR *ya corregidas* en los pasos A y B.
    * Fórmula: $G = 0.45 \times R + 0.10 \times NIR + 0.45 \times B$
5.  **Paso D: Realces de Contraste (Enhancements)**
    * Aplicar `apply_piecewise_stretch` (Curva de contraste de geo2grid).
    * Aplicar Gamma (si se requiere).

---

## 3. DETALLE DE IMPLEMENTACIÓN POR MÓDULO

### 3.1 Nuevo Módulo: `src/enhancements.c`

Debe implementar dos funciones clave:

**A. Corrección Cenital Solar:**
```c
// Divide la reflectancia por el coseno del ángulo cenital solar.
// Protege contra ángulos extremos (>85°) para evitar infinito/ruido.
void apply_solar_zenith_correction(DataF *band, const DataF *sza, float limit_deg);
```

**B. Piecewise Linear Stretch (Geo2grid Standard):** Interpolación lineal basada en los siguientes puntos de control (normalizados a float):

| Punto Entrada (x)|	Valor Float |	Punto Salida (y) |	Valor Float |	Efecto Visual |
| :--- | :--- | :--- | :--- | :--- |
| 0	|0.000	|0	|0.000	|Punto Negro|
| 25	| 0.098	| 90	| 0.353	| Shadow Boost (Aclara tierras) |
| 55	| 0.216	| 140	| 0.549	| Medios tonos|
| 100	| 0.392	| 175	| 0.686	| Nubes medias|
| 255	| 1.000	| 255	| 1.000	| Punto Blanco|

---

### 3.2 Implementación

Usaremos un corte conservador (`MAX_SZA = 85.0f`) para evitar amplificación de ruido en el terminador.

```c
/**
 * @brief Aplica corrección de ángulo cenital solar a datos de reflectancia.
 * 
 * Formula: reflectance_corrected = reflectance_TOA / cos(solar_zenith_angle)
 * 
 * @param data Datos de reflectancia (in-place modification)
 * @param sza Ángulos cenitales solares en grados
 */
void apply_solar_zenith_correction(DataF *data, const DataF *sza) {
    if (!data || !sza || !data->data_in || !sza->data_in) return;
    
    const float MAX_SZA = 85.0f; // Corte conservador para evitar ruido extremo
    const float RAD_PER_DEG = M_PI / 180.0f;
    
    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        float refl = data->data_in[i];
        float sza_deg = sza->data_in[i];
        
        if (IS_NONDATA(refl) || IS_NONDATA(sza_deg) || sza_deg > MAX_SZA) {
            data->data_in[i] = 0.0f; // Clamping a negro en noche/terminador
            continue;
        }
        
        float cos_sza = cosf(sza_deg * RAD_PER_DEG);
        // Evitar división por cero (aunque MAX_SZA ya protege)
        if (cos_sza > 0.087f) { // cos(85) approx 0.087
            data->data_in[i] = refl / cos_sza;
        } else {
            data->data_in[i] = 0.0f;
        }
    }
    dataf_find_min_max(data);
}
```

### 3.3 Integración en Pipeline

**Archivo:** `src/rgb.c`  
**Función:** `compose_truecolor()`  
**Ubicación:** Después de cargar canales, ANTES de corrección Rayleigh

Nota:
- La corrección del cenit solar también se aplica al ch_nir que se va a usar para el verde sintético.
- Asegurarse de que el orden de las operaciones sea exactamente:
    1. Cargar C01, C02, C03
    2. cargar navegación (SZA, etc.)
    3. Aplicar corrección solar a los 3 canales
    4. Sintetizar el verde (que usa un C03 ya corregido por cenit)
    5. Aplicar corrección Rayleigh a C01 y C02.
 
```c
static bool compose_truecolor(RgbContext *ctx) {
    // ... cargar canales C01, C02, C03 ...
    
    // NUEVO: Generar SZA si vamos a hacer corrección solar zenith
    if (ctx->opts.rayleigh_on || ctx->opts.rayleigh_analytic) {
        // Cargar navegación (necesaria para SZA)
        char nav_file[512];
        get_navigation_filename(/* ... */);
        
        RayleighNav nav = {0};
        if (rayleigh_load_navigation(nav_file, &nav, width, height)) {
            // Aplicar corrección solar zenith a canales visibles
            LOG_INFO("Aplicando corrección solar zenith...");
            apply_solar_zenith_correction(&ctx->comp_b, &nav.sza);
            apply_solar_zenith_correction(&ctx->comp_r, &nav.sza);
            apply_solar_zenith_correction(ch_nir, &nav.sza);
            
            // Ahora aplicar Rayleigh
            if (ctx->opts.rayleigh_analytic) {
                // ...
            } else {
                luts_rayleigh_correction(&ctx->comp_b, &nav, 1);
                luts_rayleigh_correction(&ctx->comp_r, &nav, 2);
            }
            
            rayleigh_free_navigation(&nav);
        }
    }
    
    // Continuar con verde sintético, etc.
    // ...
}
```

### 3.4 Impacto Esperado

**Antes (sin corrección solar zenith):**
```
C01 media: 0.152 → Rayleigh corrige 0.113 → Resultado: 0.056 (21.9% negativos)
C02 media: 0.124 → Rayleigh corrige 0.032 → Resultado: 0.094 (9.5% negativos)
```

**Después (con corrección solar zenith):**
```
C01 media: 0.152 → Corr. SZA: 0.200 → Rayleigh corrige 0.113 → Resultado: 0.087 (<5% negativos)
C02 media: 0.124 → Corr. SZA: 0.162 → Rayleigh corrige 0.032 → Resultado: 0.130 (<2% negativos)
```

**Mejora:**
- Reduce píxeles negativos de 21.9% a ~5%
- Aumenta niveles de reflectancia corregida
- Balance de color más correcto

---

## 4. FASE 2: PIECEWISE LINEAR STRETCH (Realce de Contraste)

### 4.1 Teoría

Geo2grid aplica una curva de realce no lineal que expande fuertemente las sombras y comprime los tonos claros:

| Rango | Expansión | Propósito |
|-------|-----------|-----------|
| 0-25% | ×3.6 | Realzar océanos, sombras |
| 25-55% | ×2.5 | Realzar vegetación, tierra |
| 55-100% | ×1.75 | Preservar nubes brillantes |

### 4.2 Implementación

**Archivo:** `src/enhancements.c` (o `src/image.c`)

**CORRECCIÓN IMPORTANTE:** El realce debe aplicarse sobre los datos en punto flotante (`DataF`) antes de la conversión a imagen de 8 bits, para preservar la precisión y el rango dinámico durante la transformación no lineal.

```c
/**
 * @brief Aplica stretch lineal por tramos (piecewise linear).
 * 
 * Implementa una curva de realce de contraste definida por puntos de control.
 * Compatible con el enhancement de geo2grid/satpy para true_color.
 * 
 * @param data Datos en punto flotante a realzar (modificada in-place)
 * @param xp Puntos de control de entrada (valores normalizados 0-1)
 * @param fp Puntos de control de salida (valores normalizados 0-1)
 * @param n_points Número de puntos de control (debe ser >= 2)
 */
void apply_piecewise_stretch(DataF *data, 
                             const float *xp, 
                             const float *fp, 
                             int n_points) {
    if (!data || !data->data_in || !xp || !fp || n_points < 2) {
        LOG_ERROR("Invalid parameters for piecewise linear stretch");
        return;
    }
    
    LOG_INFO("Aplicando piecewise linear stretch con %d puntos de control", n_points);
    
    #pragma omp parallel for
    for (size_t i = 0; i < data->size; i++) {
        float value = data->data_in[i];
        
        // Ignorar NonData
        if (IS_NONDATA(value)) continue;
        
        // Interpolar linealmente en la curva definida
        float result = piecewise_interp(value, xp, fp, n_points);
        
        // Guardar resultado (manteniendo float)
        data->data_in[i] = result;
    }
}

/**
 * @brief Interpolación lineal por tramos entre puntos de control.
 * 
 * Equivalente a numpy.interp(x, xp, fp)
 * 
 * @param x Valor de entrada
 * @param xp Array de puntos de control x (debe estar ordenado)
 * @param fp Array de puntos de control y
 * @param n Número de puntos
 * @return Valor interpolado
 */
static float piecewise_interp(float x, const float *xp, const float *fp, int n) {
    // Clamping: si x está fuera del rango, devolver valor extremo
    if (x <= xp[0]) return fp[0];
    if (x >= xp[n-1]) return fp[n-1];
    
    // Buscar segmento donde se encuentra x
    for (int i = 0; i < n - 1; i++) {
        if (x >= xp[i] && x <= xp[i+1]) {
            // Interpolación lineal entre xp[i] y xp[i+1]
            float t = (x - xp[i]) / (xp[i+1] - xp[i]);
            return fp[i] + t * (fp[i+1] - fp[i]);
        }
    }
    
    // No debería llegar aquí
    return fp[n-1];
}
```

### 4.3 Curvas Predefinidas

**Archivo:** `include/enhancements.h`

Nota: es necesario poner estas curvas en un .h cuando podrían estar en el .c si solamente van a ser usadas en enhacements y no en ningún otro lugar?
Si van a ser usadas en más de un .c. no ponerlas como static sino en un .c y en el .h declararlas como extern.

```c
#ifndef HPSATVIEWS_ENHANCEMENTS_H_
#define HPSATVIEWS_ENHANCEMENTS_H_

#include "image.h"

// Curvas de realce predefinidas

// Curva geo2grid/satpy para true_color
// Optimizada para imágenes de satélite visible
#define TRUECOLOR_STRETCH_POINTS 5
static const float TRUECOLOR_XP[TRUECOLOR_STRETCH_POINTS] = {
    0.0f / 255.0f,   // 0.0
    25.0f / 255.0f,  // 0.098
    55.0f / 255.0f,  // 0.216
    100.0f / 255.0f, // 0.392
    255.0f / 255.0f  // 1.0
};
static const float TRUECOLOR_FP[TRUECOLOR_STRETCH_POINTS] = {
    0.0f / 255.0f,   // 0.0
    90.0f / 255.0f,  // 0.353
    140.0f / 255.0f, // 0.549
    175.0f / 255.0f, // 0.686
    255.0f / 255.0f  // 1.0
};

// Curva lineal (identidad, sin realce)
#define LINEAR_STRETCH_POINTS 2
static const float LINEAR_XP[LINEAR_STRETCH_POINTS] = {0.0f, 1.0f};
static const float LINEAR_FP[LINEAR_STRETCH_POINTS] = {0.0f, 1.0f};

// Funciones públicas
void apply_piecewise_stretch(DataF *data, 
                                          const float *xp, 
                                          const float *fp, 
                                          int n_points);

#endif // HPSATVIEWS_ENHANCEMENTS_H_
```

### 4.4 Integración en Pipeline

**Opción A: Flag explícito `--enhance-contrast`**

```c
// En args.h
typedef struct {
    // ... campos existentes ...
    bool use_piecewise_stretch;  // --stretch
} Args;

// En rgb.c, ANTES de generar ImageData (sobre los componentes DataF)
if (ctx->opts.use_piecewise_stretch) {
    LOG_INFO("Aplicando realce de contraste true_color...");
    
    // Aplicar a cada canal individualmente
    apply_piecewise_stretch(&ctx->comp_r, TRUECOLOR_XP, TRUECOLOR_FP, TRUECOLOR_STRETCH_POINTS);
    apply_piecewise_stretch(&ctx->comp_g, TRUECOLOR_XP, TRUECOLOR_FP, TRUECOLOR_STRETCH_POINTS);
    apply_piecewise_stretch(&ctx->comp_b, TRUECOLOR_XP, TRUECOLOR_FP, TRUECOLOR_STRETCH_POINTS);
}
```

**Opción B: Automático para true_color con Rayleigh**

```c
// En rgb.c, después de generar ImageData
if (ctx->mode == RGB_MODE_TRUECOLOR && 
    (ctx->opts.rayleigh_on || ctx->opts.rayleigh_analytic)) {
    // Aplicar realce de contraste automáticamente cuando se usa Rayleigh
    LOG_INFO("Aplicando realce de contraste optimizado para true_color...");
    image_apply_piecewise_linear_stretch(&ctx->final_image, 
                                        TRUECOLOR_XP, 
                                        TRUECOLOR_FP, 
                                        TRUECOLOR_STRETCH_POINTS);
}
```

**Recomendación:** Usar Opción B (automático) para mantener simplicidad de uso.

---

## 5. FASE 3: AJUSTE DE ESCALA (Menor Prioridad)

### 5.1 Problema de Escala

**geo2grid:** Trabaja con reflectancia en **porcentaje (0-100)**  
**hpsatviews:** Trabaja con reflectancia en **fracción (0-1)**

### 5.2 Solución: No Cambiar Escala Interna

Mantener escala 0-1 internamente es más estándar y evita confusión. El "crude stretch" de geo2grid no es necesario si ya tenemos datos en la escala correcta.

**Ajuste necesario:** Adaptar las curvas de realce para trabajar con escala 0-1:

```c
// Ya implementado en la sección anterior:
// Los puntos xp y fp ya están normalizados diviendo por 255.0
```

---

## 6. ORDEN DE IMPLEMENTACIÓN RECOMENDADO

### Sprint 1: Corrección Solar Zenith (Crítico)
**Estimación:** 4-6 horas  
**Archivos:** `reader_nc.c`, `rgb.c`

1. Implementar `apply_solar_zenith_correction()` en `reader_nc.c`
2. Integrar en pipeline de `compose_truecolor()`
3. Probar con imágenes de test
4. Validar reducción de píxeles negativos

**Criterio de éxito:**
- Píxeles negativos < 5% (desde 21.9%)
- Balance de color mejorado
- Estadísticas de reflectancia más altas

### Sprint 2: Piecewise Linear Stretch (Alto Impacto Visual)
**Estimación:** 6-8 horas  
**Archivos:** `image.c` (o nuevo `enhancements.c`), `enhancements.h`, `rgb.c`

1. Implementar `piecewise_interp()` y `image_apply_piecewise_linear_stretch()`
2. Definir curvas en `enhancements.h`
3. Integrar automáticamente para true_color con Rayleigh
4. Comparar visualmente con geo2grid

**Criterio de éxito:**
- Imagen visualmente similar a geo2grid
- Océanos con mejor contraste
- Nubes preservadas (no quemadas)

### Sprint 3: Validación y Ajustes Finos
**Estimación:** 4 horas  
**Archivos:** Tests, documentación

1. Crear test comparativo con múltiples escenas
2. Ajustar parámetros de curva si es necesario
3. Documentar cambios en README.md
4. Actualizar plan_rayleigh_cor.md

---

## 7. VALIDACIÓN Y TESTING

### 7.1 Tests Automatizados

```bash
#!/bin/bash
# tests/test_enhancements.sh

set -e

echo "=== Test: Solar Zenith Correction + Enhancements ==="

# Imagen de referencia (sin correcciones)
../bin/hpsv rgb -m truecolor -g 2 -s -4 \
  ../sample_data/028/OR_ABI-L1b-RadF-M6C01_G19_s20260281200213_e20260281209522_c20260281209563.nc \
  -o tc_ref.png

# Con Rayleigh y nueva corrección solar zenith
../bin/hpsv rgb -m truecolor --rayleigh -g 2 -s -4 \
  ../sample_data/028/OR_ABI-L1b-RadF-M6C01_G19_s20260281200213_e20260281209522_c20260281209563.nc \
  -o tc_ray_sunz.png \
  > tc_ray_sunz.log 2>&1

# Verificar estadísticas en log
echo "Verificando píxeles negativos..."
NEG_BLUE=$(grep "Píxeles negativos clamped" tc_ray_sunz.log | head -1 | grep -oP '\d+\.\d+(?=%)')
echo "Canal azul: $NEG_BLUE% negativos (objetivo: < 5%)"

if (( $(echo "$NEG_BLUE < 5.0" | bc -l) )); then
    echo "✅ Test PASSED: Píxeles negativos dentro del rango aceptable"
else
    echo "❌ Test FAILED: Demasiados píxeles negativos"
    exit 1
fi
```

### 7.2 Comparación Visual

**Método:** Crear composiciones side-by-side

```bash
# Generar todas las versiones
./tests/test_enhancements.sh

# Crear comparación con ImageMagick
montage tc_ref.png tc_ray_sunz.png tcgeo2grid.png \
  -tile 3x1 -geometry +2+2 -label '%f' comparison.png
```

**Métricas visuales a evaluar:**
1. Balance de color (océanos, vegetación, nubes)
2. Contraste general
3. Detalle en sombras vs. altas luces
4. Artefactos en bordes del disco

---

## 8. CONSIDERACIONES ADICIONALES

### 8.1 Interacción con Gamma

**Problema:** Actualmente se aplica gamma 2.0 con `-g 2`

**Recomendación:** 
- El piecewise linear stretch **reemplaza** la necesidad de gamma para true_color
- Mantener gamma como opción pero **NO aplicarlo por defecto** cuando se usa stretch
- O reducir gamma a 1.2-1.5 si se combina con stretch

```c
// En rgb.c
if (ctx->opts.enhance_contrast && ctx->opts.gamma > 1.9) {
    LOG_WARN("Gamma alto detectado con realce de contraste. Reduciendo a 1.5 para evitar sobre-realce.");
    ctx->opts.gamma = 1.5f;
}
```

### 8.2 Modos RGB Diferentes

**Pregunta:** ¿Aplicar piecewise stretch solo a true_color o también a otros modos?

**Respuesta:**
- **true_color**: Sí, siempre (curva optimizada para visible)
- **ash, dust, etc.**: No, usar stretch lineal o curvas específicas
- **night**: No aplica (usa paleta diferente)

```c
// En rgb.c
if (ctx->mode == RGB_MODE_TRUECOLOR && ctx->rayleigh_applied) {
    // Aplicar curva true_color
} else if (ctx->mode == RGB_MODE_ASH) {
    // Aplicar curva diferente o ninguna
}
```

### 8.3 Performance

**Impacto esperado:**
- `apply_solar_zenith_correction()`: ~50-100ms (paralelizado)
- `image_apply_piecewise_linear_stretch()`: ~30-50ms (paralelizado)
- **Total overhead:** <150ms adicionales

**Optimización:** Ambas funciones están paralelizadas con OpenMP.

---

## 9. ALTERNATIVAS CONSIDERADAS

### Alternativa 1: Usar Gamma Ajustado en Lugar de Piecewise

**Pros:**
- Ya implementado
- Más simple

**Contras:**
- Gamma es una curva de potencia global, no permite control fino por rangos
- No puede replicar la expansión agresiva en sombras de geo2grid
- Requeriría gamma ~0.4-0.5 que produce artefactos

**Decisión:** No recomendado.

### Alternativa 2: CLAHE en Lugar de Piecewise

**Pros:**
- Ya implementado
- Adaptativo

**Contras:**
- CLAHE es espacialmente adaptativo (tile-based), no global
- Resultados inconsistentes entre imágenes
- Más lento
- No coincide con comportamiento de geo2grid

**Decisión:** No recomendado para replicar geo2grid, pero útil como opción adicional.

---

## 10. RESUMEN DE ARCHIVOS A MODIFICAR

| Archivo | Cambios | Líneas Est. | Prioridad |
|---------|---------|-------------|-----------|
| `src/reader_nc.c` | Agregar `apply_solar_zenith_correction()` | +60 | Alta |
| `src/rgb.c` | Integrar corrección solar zenith | +15 | Alta |
| `src/image.c` o nuevo `src/enhancements.c` | Implementar piecewise stretch | +80 | Alta |
| `include/enhancements.h` | Definir curvas y prototipos | +30 | Alta |
| `src/rgb.c` | Integrar piecewise stretch | +10 | Alta |
| `Makefile` | Agregar `enhancements.o` si se crea nuevo archivo | +2 | Media |
| `tests/test_enhancements.sh` | Crear test automatizado | +50 | Media |
| `README.md` | Documentar nuevas features | +20 | Baja |

**Total estimado:** ~270 líneas de código nuevo

---

## 11. CRITERIOS DE ÉXITO FINAL

### Objetivos Cuantitativos

1. **Píxeles negativos:** < 5% (desde 21.9%)
2. **Diferencia visual con geo2grid:** RMSE < 10% en histogramas RGB
3. **Performance:** Overhead < 200ms por imagen Full Disk

### Objetivos Cualitativos

1. **Balance de color:** Similar a geo2grid (sin amarillos excesivos)
2. **Contraste:** Océanos visibles, nubes brillantes preservadas
3. **Artefactos:** Sin halos o banding en bordes

### Validación con Usuario

```bash
# Generar comparación final
hpsv rgb -m truecolor --rayleigh -g 2 -s -4 archivo.nc -o hpsv_final.png
# vs
# tcgeo2grid.png (ya generado)

# El usuario debe confirmar:
# ✅ Colores naturales
# ✅ Sin amarillos excesivos
# ✅ Detalles visibles en océanos
# ✅ Nubes no quemadas
```

---

**Fin del Plan de Implementación**

**Siguiente paso:** Comenzar con Sprint 1 (Corrección Solar Zenith)
