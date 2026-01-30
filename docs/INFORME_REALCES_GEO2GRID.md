# Informe: Realces Aplicados por geo2grid al True Color RGB

**Fecha:** 30 de enero de 2026  
**Autor:** Análisis comparativo geo2grid vs hpsatviews  
**Objetivo:** Documentar todos los realces que aplica geo2grid para reproducirlos en hpsatviews

---

## 1. RESUMEN EJECUTIVO

Geo2grid aplica **dos realces principales** al producto true_color que NO están implementados en hpsatviews:

1. **Stretch "crude" (0-100%)**: Normalización del rango de reflectancia
2. **Piecewise Linear Stretch**: Curva de realce no lineal con 5 puntos de control

Estos realces explican la diferencia visual significativa entre las imágenes generadas por ambos sistemas, especialmente los "amarillos y verdes fuertes" observados en hpsatviews.

---

## 2. ANÁLISIS DE LOGS DE GEO2GRID

### 2.1 Configuración de Enhancements Aplicada

Del log `tcgeo2grid_20260281200.log` (líneas 112-115):

```log
[2026-01-30 09:55:02,442] : DEBUG : satpy.writers : apply : Data for DataID(name='true_color', resolution=500) will be enhanced with options:
	[{'name': 'reflectance_range', 'method': <function stretch at 0x7f1a9ec18fe0>, 'kwargs': {'stretch': 'crude', 'min_stretch': 0.0, 'max_stretch': 100.0}}, 
	 {'name': 'Linear interpolation', 'method': <function piecewise_linear_stretch at 0x7f1a9ec868e0>, 'kwargs': {'xp': [0.0, 25.0, 55.0, 100.0, 255.0], 'fp': [0.0, 90.0, 140.0, 175.0, 255.0], 'reference_scale_factor': 255}}]
```

### 2.2 Archivos de Configuración

**Archivo:** `/home/aguilars/lanot/hpsatviews/geo2grid_v_1_2/etc/polar2grid/enhancements/abi.yaml`

```yaml
enhancements:
  true_color_crefl:
    sensor: abi
    standard_name: true_color
    operations:
      - name: reflectance_range
        method: !!python/name:satpy.enhancements.stretch
        kwargs: {stretch: 'crude', min_stretch: 0., max_stretch: 100.}
      - name: Linear interpolation
        method: !!python/name:satpy.enhancements.piecewise_linear_stretch
        kwargs:
         xp: [0., 25., 55., 100., 255.]
         fp: [0., 90., 140., 175., 255.]
         reference_scale_factor: 255
```

---

## 3. DESCRIPCIÓN DETALLADA DE REALCES

### 3.1 Realce 1: "Crude Stretch" (Normalización de Rango)

**Tipo:** Stretch lineal simple  
**Parámetros:**
- `min_stretch`: 0.0 (%)
- `max_stretch`: 100.0 (%)
- `stretch`: "crude"

**Función:**
```python
# Pseudocódigo conceptual
def crude_stretch(data, min_val=0.0, max_val=100.0):
    """
    Normaliza linealmente el rango de datos.
    Asume que los datos de entrada están en escala 0-100 (reflectancia en %)
    """
    # Clamping
    data = np.clip(data, min_val, max_val)
    
    # Normalización lineal a rango 0-1
    data_normalized = (data - min_val) / (max_val - min_val)
    
    return data_normalized
```

**Efecto:**
- Asegura que los datos estén en el rango esperado (0-100%)
- Elimina valores outliers por encima de 100% o debajo de 0%
- Normaliza a rango 0-1 para el siguiente paso

**Nota importante:** Este stretch asume que los datos de entrada están en **porcentaje (0-100)**, no en fracción (0-1). 

**Implicación para hpsatviews:** Nuestros datos están en fracción (0-1), por lo que deberíamos ajustar este paso o convertir nuestra escala.

### 3.2 Realce 2: "Piecewise Linear Stretch" (Curva de Realce)

**Tipo:** Interpolación lineal por tramos  
**Parámetros:**
- `xp`: [0.0, 25.0, 55.0, 100.0, 255.0] - Puntos de entrada
- `fp`: [0.0, 90.0, 140.0, 175.0, 255.0] - Puntos de salida
- `reference_scale_factor`: 255

**Función:**
```python
# Pseudocódigo conceptual basado en numpy.interp
def piecewise_linear_stretch(data, xp, fp, reference_scale=255):
    """
    Aplica una curva de realce no lineal definida por puntos de control.
    
    Args:
        data: Array de datos normalizados (0-1) desde el paso anterior
        xp: Puntos de control de entrada (en escala 0-255)
        fp: Puntos de control de salida (en escala 0-255)
        reference_scale: Factor de escala (255 para 8-bit)
    
    Returns:
        Array con valores realzados en rango 0-255
    """
    # Escalar datos de 0-1 a 0-255
    data_scaled = data * reference_scale
    
    # Aplicar interpolación lineal por tramos
    result = np.interp(data_scaled, xp, fp)
    
    return result  # Salida en rango 0-255 para uint8
```

**Interpretación de la Curva:**

Los puntos de control definen una función de transferencia no lineal:

| Entrada (xp) | Salida (fp) | % de Compresión/Expansión |
|--------------|-------------|---------------------------|
| 0.0          | 0.0         | Base (negro puro)         |
| 25.0         | 90.0        | **Expansión × 3.6**       |
| 55.0         | 140.0       | Expansión × 2.5           |
| 100.0        | 175.0       | Compresión × 1.75         |
| 255.0        | 255.0       | Tope (blanco puro)        |

**Efecto Visual:**

1. **Rango 0-25% (sombras/oscuros):**
   - Expansión muy agresiva (×3.6)
   - Mejora detalles en áreas oscuras (océanos, sombras)
   - **Este es el realce más importante**

2. **Rango 25-55% (tonos medios):**
   - Expansión moderada (×2.5)
   - Mejora contraste en vegetación y tierra

3. **Rango 55-100% (tonos claros):**
   - Expansión menor (×1.75)
   - Mantiene detalles en nubes brillantes

4. **Rango 100-255% (sobresaturación):**
   - Mapeo directo
   - Preserva el tope blanco

**Curva Visualizada:**

```
Salida (fp)
255 |                                            *
    |                                       *
    |                                  *
175 |                            *
    |                       *
140 |                  *
    |              *
90  |         *
    |      *
    |   *
0   |*
    +-----|-----|-----|-----|-----|-----|-----|---->
    0    25    55   100   125   175   215   255
                  Entrada (xp)
```

La curva muestra una **expansión agresiva en sombras** que disminuye progresivamente hacia los tonos claros.

---

## 4. PIPELINE COMPLETO DE GEO2GRID vs HPSATVIEWS

### 4.1 Pipeline de geo2grid (True Color)

```
1. Lectura de datos L1b (Rad)
2. Calibración a reflectancia (%)
   → C01, C02, C03 en escala 0-100%
3. Corrección solar zenith
   → reflectance_corrected = reflectance / cos(sza)
4. Corrección Rayleigh
   → C01_corr = C01 - rayleigh_lut(sza, vza, raa)
   → C02_corr = C02 - rayleigh_lut(sza, vza, raa)
5. Síntesis de verde
   → Green = hybrid_green(C01_corr, C02_corr, C03)
6. Remuestreo a resolución común (500m)
   → Sharpen con canal rojo de alta resolución
7. **Realce 1: Crude Stretch**
   → Normalización 0-100% → 0-1
8. **Realce 2: Piecewise Linear Stretch**
   → Curva no lineal con expansión en sombras
9. Conversión a uint8 (0-255)
10. Escritura GeoTIFF
```

### 4.2 Pipeline actual de hpsatviews (True Color con Rayleigh LUTs)

```
1. Lectura de datos L1b (Rad)
2. Calibración a reflectancia (fracción)
   → C01, C02, C03 en escala 0-1
3. ❌ NO aplica corrección solar zenith
4. Corrección Rayleigh
   → C01_corr = C01 - rayleigh_lut(sza, vza, raa) / 100
   → C02_corr = C02 - rayleigh_lut(sza, vza, raa) / 100
5. Síntesis de verde
   → Green = hybrid_green(C01_corr, C02_corr, C03)
6. Downsampling a resolución común
   → Box filter simple
7. ❌ NO aplica realces de contraste
8. Gamma 2.0 (opcional, con flag -g 2)
9. Conversión a uint8 (0-255) con escalado lineal
10. Escritura PNG
```

### 4.3 Diferencias Clave

| Paso | geo2grid | hpsatviews | Impacto |
|------|----------|------------|---------|
| Corrección solar zenith | ✅ Sí (1/cos(sza)) | ❌ No | **Alto** - Afecta niveles absolutos |
| Realce de sombras | ✅ Expansión ×3.6 | ❌ No | **Muy Alto** - Océanos se ven más oscuros en hpsv |
| Curva de realce | ✅ Piecewise linear | ❌ No (lineal) | **Alto** - Menor contraste general |
| Sharpening | ✅ Con C02 @ 500m | ✅ Pero con box filter | Medio |
| Escala datos | 0-100% | 0-1 | Medio - Requiere ajustes |

---

## 5. ESTADÍSTICAS COMPARATIVAS

### 5.1 hpsatviews (20260281200) - Con Rayleigh LUTs

**Canal Azul (C01):**
```
Reflectancia original:  min=-0.001136, max=0.866589, media=0.152182
Corrección Rayleigh:    max=0.266980, media=0.112677
Reflectancia corregida: media=0.056415
Píxeles negativos:      25768178 (21.9%)  ⚠️ PROBLEMA
```

**Canal Rojo (C02):**
```
Reflectancia original:  min=-0.001545, max=0.939794, media=0.123856
Corrección Rayleigh:    max=0.076294, media=0.031916
Reflectancia corregida: media=0.093764
Píxeles negativos:      11148708 (9.5%)   ⚠️ PROBLEMA
```

**Problemas identificados:**
1. **21.9% de píxeles negativos en azul** - La corrección Rayleigh es demasiado agresiva
2. **Media de azul corregido muy baja** (0.056) - Imagen resultante oscura
3. **Clamping a 0** está ocultando el problema

### 5.2 Análisis de Píxeles Negativos

La gran cantidad de píxeles negativos sugiere uno de estos problemas:

1. **Vector de visión aún incorrecto** - El RAA podría estar mal calculado
2. **LUTs sobrecorrigiendo** - Valores de LUT demasiado altos para ciertos ángulos
3. **Falta corrección solar zenith** - Sin dividir por cos(sza), los valores son más bajos

---

## 6. CÁLCULO ESPERADO DE PÍXELES NEGATIVOS EN GEO2GRID

Geo2grid **también genera píxeles negativos** después de la corrección Rayleigh, pero los maneja de forma diferente:

1. **Permite valores negativos temporalmente**
2. **Aplica el crude stretch** que los clampea a 0
3. **Aplica la curva de realce** que expande las sombras

El resultado es que los píxeles que quedarían "demasiado oscuros" se realzan fuertemente en la curva de contraste.

---

## 7. EXPERIMENTOS SUGERIDOS PARA DIAGNÓSTICO

### 7.1 Verificar Escala de Datos

```bash
# Generar imagen sin corrección Rayleigh
hpsv rgb -m truecolor -g 2 -s -4 archivo.nc -o ref_sin_ray.png

# Generar con Rayleigh
hpsv rgb -m truecolor --rayleigh -g 2 -s -4 archivo.nc -o test_ray.png

# Comparar estadísticas en logs
```

**Esperable:**
- Sin Rayleigh: Media azul ~0.15-0.25
- Con Rayleigh: Media azul ~0.05-0.10 (después de corrección)

### 7.2 Verificar Corrección Solar Zenith

```c
// En reader_nc.c, después de calibración
// Agregar temporalmente:
reflectance = reflectance / cos(sza * M_PI / 180.0);
```

**Esperable:** Valores más altos, especialmente en bordes del disco (alto SZA).

### 7.3 Probar Curva de Realce Manual

```bash
# Aplicar gamma muy agresivo para simular expansión
hpsv rgb -m truecolor --rayleigh -g 0.5 -s -4 archivo.nc -o test_gamma_low.png
```

---

## 8. PRÓXIMOS PASOS

Ver documento complementario: `PLAN_IMPLEMENTACION_REALCES.md`

---

## APÉNDICE A: Fórmulas Matemáticas

### A.1 Crude Stretch

$$\text{stretched} = \frac{\text{clamp}(\text{data}, \text{min}, \text{max}) - \text{min}}{\text{max} - \text{min}}$$

### A.2 Piecewise Linear Stretch

$$\text{output}(x) = \begin{cases}
\text{interp}(x \cdot 255, \mathbf{xp}, \mathbf{fp}) & \text{si } 0 \leq x \leq 1 \\
0 & \text{si } x < 0 \\
255 & \text{si } x > 1
\end{cases}$$

Donde $\text{interp}$ es interpolación lineal entre puntos adyacentes en $\mathbf{xp}$ y $\mathbf{fp}$.

### A.3 Corrección Solar Zenith

$$R_{\text{corrected}} = \frac{R_{\text{TOA}}}{\cos(\theta_s)}$$

Donde:
- $R_{\text{TOA}}$ = Reflectancia observada en tope de atmósfera
- $\theta_s$ = Ángulo cenital solar (SZA)

**Nota:** Esto corrige por el camino óptico más largo cuando el sol está bajo en el horizonte.

---

## APÉNDICE B: Referencias Técnicas

1. **Satpy Documentation**: https://satpy.readthedocs.io/en/stable/enhancements.html
2. **Pyspectral Rayleigh Correction**: https://pyspectral.readthedocs.io/en/latest/rayleigh_correction.html
3. **NOAA ABI True Color Recipe**: https://www.star.nesdis.noaa.gov/GOES/documents/ABIQuickGuide_CustomRGBs.pdf
4. **Archivo de configuración geo2grid**: `geo2grid_v_1_2/etc/polar2grid/enhancements/abi.yaml`

---

## APÉNDICE C: Comandos de Comparación Usados

```bash
# Geo2grid
geo2grid_v_1_2/bin/geo2grid.sh -r abi_l1b -w geotiff -p true_color \
  --output-filename tmp.tif \
  -f sample_data/028/*s20260281200* \
  > tcgeo2grid_20260281200.log 2>&1

# Conversión a PNG
gdal_translate -of PNG -outsize 12.5% 12.5% tmp.tif tcgeo2grid_20260281200.png

# hpsatviews
bin/hpsv rgb -m truecolor --rayleigh -g 2 -s -4 \
  sample_data/028/OR_ABI-L1b-RadF-M6C01_G19_s20260281200213_e20260281209522_c20260281209563.nc \
  -o tcrayluts_20260281200.png \
  > tcrayluts_20260281200.log 2>&1
```

---

**Fin del Informe**
