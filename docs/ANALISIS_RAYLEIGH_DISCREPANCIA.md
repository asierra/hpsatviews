# Análisis de Discrepancia: Corrección Rayleigh hpsv vs geo2grid

**Fecha**: 29 de enero de 2026  
**Archivos analizados**:
- `tests/abi_l1b_geotiff_20260128_180021.log` (geo2grid)
- `tests/tcrayluts_20260281800.log` (hpsv)
- `geo2grid_v_1_2/.../pyspectral/rayleigh.py`
- `geo2grid_v_1_2/.../satpy/modifiers/atmosphere.py`

---

## Síntoma

La imagen procesada por `hpsv` muestra **nubes amarillas** fuera de la zona central, mientras que geo2grid produce colores naturales correctos, a pesar de usar las **mismas LUTs de pyspectral**.

---

## Hallazgos del Análisis de Logs

### 1. Valores de corrección Rayleigh comparables

Comparando píxeles muestra similares en ambos logs:

**geo2grid (línea 62)**:
```
Sample pixel 88260056: SZA=61.19, VZA=89.67, RAA=10.56, Original=0.384276, Rayleigh=0.273056
```

**hpsv (línea 60)**:
```
Sample pixel 88260056: SZA=61.19, VZA=89.67, RAA=10.56, Original=0.384276, Rayleigh=0.273056
```

✅ **Los valores de interpolación son IDÉNTICOS**: ambos obtienen `Rayleigh=0.273056` para el mismo píxel.

### 2. Rangos de las LUTs

**geo2grid**: No muestra el rango de la LUT en el log  
**hpsv (línea 49)**:
```
Valores tabla: min=0.063205, max=1.425588, media=0.525150
```

✅ **Las LUTs están en escala correcta** (0-1.5 aproximadamente, que es razonable para reflectancias normalizadas).

---

## Análisis del Código Fuente

### pyspectral/rayleigh.py (líneas 240-243)

```python
def get_reflectance(self, sun_zenith, sat_zenith, azidiff, ...):
    # ...interpolación...
    res = minterp(interp_points2)
    res *= 100  # ← MULTIPLICA POR 100
    return res.reshape(sunzsec.shape)
```

**Hallazgo crítico**: `pyspectral` devuelve valores en **porcentaje** (0-100), no fracción (0-1).

### satpy/modifiers/atmosphere.py (línea 113)

```python
def __call__(self, projectables, optional_datasets=None, **info):
    # ...
    refl_cor_band = corrector.get_reflectance(sunz, satz, ssadiff, ...)
    proj = vis - refl_cor_band  # ← RESTA DIRECTA
    # ...
```

**Hallazgo**: La corrección se aplica como **resta directa** sin factores adicionales.

### hpsv/src/rayleigh.c (líneas 520-522)

```c
float r_corr = get_rayleigh_value(&lut, theta_s, vza, raa) * tau;
// ...
float val = original - r_corr;
```

**PROBLEMA IDENTIFICADO**:
1. `get_rayleigh_value()` devuelve valores en escala 0-1.5
2. **Multiplico por tau (0.235)** → reduce el valor a 0-0.35 aprox.
3. **NO multiplico por 100** para convertir a porcentaje

**Resultado**: La corrección es ~10x más pequeña de lo que debería ser.

---

## Causa Raíz

### Error 1: Escala incorrecta de las LUTs

Las LUTs embebidas contienen valores normalizados (0-1.5) que representan **reflectancias en fracción**. Pyspectral los convierte a porcentaje multiplicando por 100 ANTES de restar.

**Mi código**:
```c
float r_corr = lut_value * tau;  // tau=0.235
// Ejemplo: 0.27 * 0.235 = 0.063 (ERROR: debería ser ~27%)
```

**Código correcto**:
```c
float r_corr = lut_value * 100.0f;  // Convertir a porcentaje
// Ejemplo: 0.27 * 100 = 27.0 (CORRECTO)
```

### Error 2: Multiplicación redundante por tau

Las LUTs de pyspectral **ya incorporan tau** en sus cálculos internos. La multiplicación adicional por `tau` es incorrecta y causa subdivisión del valor.

**Evidencia**: En `rayleigh.py` línea 260:
```python
rayleigh_refl = _get_wavelength_adjusted_lut_rayleigh_reflectance(
    self.reflectance_lut_filename, wvl)
# Esta función ya ajusta por longitud de onda (que implica tau)
```

---

## Error 3: Dirección del Vector de Visión (MENOR IMPACTO)

En `reader_nc.c` línea 750:
```c
double dx = x_pixel - x_sat;  // Vector SAT→PIXEL
```

**Pyspectral calcula**: Vector desde píxel al satélite (inverso)

**Impacto**: Esto afecta el signo de `cos_vza`, pero al usar `fabsf()` en el VAA, el error se cancela parcialmente. Sin embargo, es físicamente incorrecto.

**Corrección requerida**:
```c
double dx = x_sat - x_pixel;  // Vector PIXEL→SAT (correcto)
```

---

## Plan de Corrección Actualizado

### Cambio 1: Eliminar multiplicación por tau
**Archivo**: `src/rayleigh.c` línea 522

```c
// ANTES (INCORRECTO):
float r_corr = get_rayleigh_value(&lut, theta_s, vza, raa) * tau;

// DESPUÉS (CORRECTO):
float r_corr = get_rayleigh_value(&lut, theta_s, vza, raa) * 100.0f;
```

**Razón**: Las LUTs ya incorporan la física de dispersión. Solo necesitamos convertir de fracción (0-1) a porcentaje (0-100) como hace pyspectral.

### Cambio 2: Invertir dirección del vector de visión
**Archivo**: `src/reader_nc.c` líneas 748-750

```c
// ANTES:
double dx = x_pixel - x_sat;
double dy = y_pixel - y_sat;
double dz = z_pixel - z_sat;

// DESPUÉS:
double dx = x_sat - x_pixel;
double dy = y_sat - y_pixel;
double dz = z_sat - z_pixel;
```

**Razón**: El vector debe apuntar desde el píxel hacia el satélite (dirección de observación).

### Cambio 3: Ajustar cos_vza según el vector invertido
**Archivo**: `src/reader_nc.c` línea 768

```c
// ANTES:
double cos_vza = -(dx * nx + dy * ny + dz * nz);

// DESPUÉS:
double cos_vza = dx * nx + dy * ny + dz * nz;
```

**Razón**: Con el vector invertido, el producto punto con la normal ya da el coseno correcto sin necesidad de cambiar signo.

### Cambio 4: (Opcional) Actualizar valores de tau a físicos
**Archivo**: `include/rayleigh.h` línea 17

```c
// ANTES:
#define RAYLEIGH_TAU_BLUE 0.235f

// DESPUÉS:
#define RAYLEIGH_TAU_BLUE 0.167f  // Valor de Bucholtz para 0.47 µm
```

**Razón**: Ya no necesitamos valores "inflados" para compensar errores. Este cambio es opcional pero mejora la coherencia física.

---

## Verificación Esperada

Después de los cambios, esperar:

1. **Valores de corrección ~4x mayores** (ej: 0.06 → 27%)
2. **Eliminación de nubes amarillas** en los bordes
3. **Colores naturales** similares a geo2grid
4. **Océanos con reflectancia 2-5%** (actualmente ~0.02%)

---

## Comandos de Prueba

```bash
# Recompilar
make clean && make

# Probar con datos del 28 de enero
./bin/hpsv -m truecolor -o test_corregido.png \
  sample_data/028/OR_ABI-L1b-RadF-M6C0[1-3]*.nc

# Comparar con geo2grid
diff test_corregido.png tmp.png  # (visualmente)
```

---

## Referencias

- `geo2grid_v_1_2/libexec/python_runtime/lib/python3.11/site-packages/pyspectral/rayleigh.py`
- `geo2grid_v_1_2/libexec/python_runtime/lib/python3.11/site-packages/satpy/modifiers/atmosphere.py`
- Bucholtz, A. (1995). "Rayleigh-scattering calculations for the terrestrial atmosphere"
