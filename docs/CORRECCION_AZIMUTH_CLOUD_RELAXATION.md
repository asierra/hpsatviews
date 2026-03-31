# Corrección de convención de azimuth y relajación en nubes

**Fecha**: 31 de marzo de 2026  
**Archivos modificados**: `src/rayleigh.c`, `include/rayleigh.h`, `src/rgb.c`  
**Estado**: ✅ Implementado y validado

---

## TL;DR

**Problema**: Imágenes true color de hpsv menos vívidas que geo2grid, con
gap estadístico persistente en los tres canales RGB.  
**Causa raíz 1**: La LUT de Rayleigh se indexaba con `Δφ` directo, pero
pyspectral usa `180° − Δφ`. Para valores típicos de GOES (RAA 0–25°),
esto causaba ~30–35% de sub-corrección.  
**Causa raíz 2**: Faltaba la relajación de corrección en zonas nubosas
que pyspectral aplica usando el canal rojo (C02) como indicador.  
**Resultado**: Gap medio con geo2grid reducido de +3.9/+6.7/+9.9 a
−0.8/+0.8/+2.0 (R/G/B).

---

## 1. Contexto

Después de corregir la escala de las LUTs (`×100` en lugar de `×tau`),
la fórmula del verde sintético (coeficientes `0.465/0.465/0.07`) y el
rango de normalización (`1.0` con stretch), persistía una brecha
cuantitativa con las imágenes de geo2grid. El análisis píxel a píxel
reveló que la corrección Rayleigh de hpsv era sistemáticamente menor
que la de pyspectral.

---

## 2. Bug de convención de azimuth

### Descubrimiento

La LUT de pyspectral es un arreglo 3D indexado por:

```
[secante_zenith_solar × diferencia_azimutal × secante_zenith_satélite]
```

En `pyspectral/rayleigh.py` (línea 240), el eje de azimuth se indexa con:

```python
interp_points2 = np.array([sunzsec.ravel(),
                           180 - azidiff.ravel(),   # ← 180 − Δφ
                           satzsec.ravel()]).T
```

hpsv pasaba `azidiff` directamente, sin la transformación `180 − Δφ`.

### Impacto cuantitativo

Para un píxel representativo (SZA=24.15°, VZA=42.56°, RAA=23.79°):

| Convención | Valor LUT C01 | Corrección aplicada |
|-----------|--------------|-------------------|
| `Δφ` directo (hpsv antes) | 0.0677 | Sub-corregido |
| `180° − Δφ` (pyspectral) | 0.1061 | Correcto |

Diferencia: **57% más corrección** con la convención correcta.

Para GOES geoestacionario, la diferencia de azimut sol–satélite típica
es 0–25°. Al indexar con `Δφ = 23°` en lugar de `180° − 23° = 157°`,
caemos en una región de la LUT con valores de corrección mucho menores.

### Corrección

En `src/rayleigh.c`, función `get_rayleigh_value()`:

```c
// Normalizar azimuth al rango [0, 180]
if (a > 180.0f) a = 360.0f - a;

// Convención pyspectral: LUT indexada con 180 − azidiff
a = 180.0f - a;
```

---

## 3. Relajación en zonas nubosas (cloud relaxation)

### Origen

pyspectral incluye la función `_relax_rayleigh_refl_correction_where_cloudy`
que reduce la corrección Rayleigh donde la reflectancia del canal rojo
(C02, 0.64 µm) supera un umbral, bajo la premisa de que en nubes densas
la dispersión molecular es despreciable frente a la dispersión de las
partículas de nube.

Código de referencia (`pyspectral/rayleigh.py`):

```python
def _relax_rayleigh_refl_correction_where_cloudy(refl, cl_idx):
    """Reduce Rayleigh correction where the redband indicates clouds."""
    CLEAR_SKY_LIMIT = 20   # porcentaje
    UPPER_LIMIT = 100
    refl_copy = refl.copy()
    mask = cl_idx > CLEAR_SKY_LIMIT
    refl_copy[mask] *= (1 - (cl_idx[mask] - CLEAR_SKY_LIMIT) /
                             (UPPER_LIMIT - CLEAR_SKY_LIMIT))
    return refl_copy
```

### Implementación en hpsv

En `src/rayleigh.c`, dentro del bucle principal de `luts_rayleigh_correction()`:

```c
// Cloud relaxation: reducir corrección donde redband >= 0.20 (fracción)
if (redband_data) {
    float rb = redband_data[idx];
    if (rb >= 0.20f) {
        r_corr *= 1.0f - (rb - 0.20f) / 0.80f;
    }
}
```

La función recibe un parámetro adicional `const DataF *redband`:
- Para C01 (azul): se pasa `&ctx->comp_r` (C02) como redband
- Para C02 (rojo): se pasa `NULL` (sin relajación, igual que pyspectral)

Se agregó validación de dimensiones: si `redband` no coincide en tamaño
con la imagen a corregir, se desactiva silenciosamente con un warning.

### Firma actualizada

```c
// Antes:
int luts_rayleigh_correction(DataF *img, const RayleighNav *nav, 
                             const uint8_t channel);

// Después:
int luts_rayleigh_correction(DataF *img, const RayleighNav *nav,
                             const uint8_t channel, const DataF *redband);
```

---

## 4. Validación cuantitativa

Comparación de estadísticas RGB (medias de 8-bit) entre hpsv y geo2grid
para la escena G19 2026-079 18:00 UTC, disco completo a 1 km:

### Antes de las correcciones

| Canal | hpsv | geo2grid | Δ medio | Δ std |
|-------|------|----------|---------|-------|
| R | 71.0 | 67.1 | +3.9 | −9.1 |
| G | 73.1 | 66.4 | +6.7 | −12.2 |
| B | 74.1 | 64.2 | +9.9 | −14.5 |

### Después de las correcciones

| Canal | hpsv | geo2grid | Δ medio | Δ std |
|-------|------|----------|---------|-------|
| R | 66.3 | 67.1 | −0.8 | −2.9 |
| G | 67.2 | 66.4 | +0.8 | −2.7 |
| B | 66.2 | 64.2 | +2.0 | −1.2 |

El gap residual de ~3 unidades en std se atribuye a la diferencia
en método de downsampling (box filter en hpsv vs nearest neighbor en
geo2grid), inherente al diseño y no planificada para corrección.

### Test multi-horario

Se validaron 7 instantes (12:00, 14:00, 14:40, 14:50, 16:00, 18:00,
20:00 UTC) con resultados aceptables en todos los casos, incluyendo
escenas con alto ángulo zenital solar (bordes del disco).

---

## 5. Simplificación de `--sharpen`

Como cambio asociado, se eliminó la lógica que forzaba resolución
completa (0.5 km) cuando se usaba `--sharpen`. El ratio sharpening
ahora opera a la resolución que el usuario elija, ya que su efecto
es imperceptible en disco completo a resolución reducida.

Código eliminado de `src/rgb.c`:
- Variable `want_max_res` que combinaba `use_full_res || use_sharpen`
- Bloque de auto-downsample (~25 líneas) que compensaba el forzado

---

## 6. Resumen de cambios por archivo

| Archivo | Cambio |
|---------|--------|
| `src/rayleigh.c` | `a = 180.0f - a` en `get_rayleigh_value()`; parámetro `redband` y cloud relaxation en `luts_rayleigh_correction()` |
| `include/rayleigh.h` | Firma actualizada con `const DataF *redband` |
| `src/rgb.c` | Paso de redband en llamadas a Rayleigh; eliminación de `want_max_res` y auto-downsample |

---

## 7. Lecciones aprendidas

1. **Convenciones implícitas son peligrosas.** La transformación
   `180 − azidiff` no está documentada en la docstring de pyspectral,
   solo aparece en el código. Siempre verificar píxel a píxel contra
   la referencia.

2. **Validación cuantitativa temprana.** Comparar estadísticas globales
   (media, std) entre implementaciones es más eficiente que inspección
   visual para detectar errores sistemáticos.

3. **Los efectos se combinan de forma no obvia.** El azimuth fix
   *aumenta* la corrección, mientras que cloud relaxation la *reduce*
   en nubes. El efecto neto depende de la cobertura nubosa de la escena.
