# Plan de Corrección Rayleigh - Estado Actual

**ACTUALIZADO**: 29 de enero de 2026, 17:05  
**Estado**: PARCIALMENTE RESUELTO - Zona central correcta, bordes con amarillo/verde residual

---

## RESUMEN DEL PROGRESO

### ✅ Problemas Resueltos

1. **Interpolación correcta con secantes**
   - Implementado: Conversión ángulos → secantes antes de interpolar
   - Los ejes de la LUT son en secantes (1.0-24.75 para SZA, 1.0-3.0 para VZA)
   - Clipping de ángulos: SZA max=87.68°, VZA max=70.53°
   - Archivo: `src/rayleigh.c` líneas 520-537

2. **Reducción en ángulos solares altos**
   - Implementado: Reducción lineal para SZA > 70°
   - Factor = 1.0 - (SZA - 70°) / (88° - 70°)
   - Elimina valores extremos (>1.0) en el terminador
   - Archivo: `src/rayleigh.c` líneas 544-550

3. **LUTs regeneradas correctamente**
   - Script: `assets/extract_rayleigh_lut.py`
   - Ejes en secantes (no convertidos a ángulos)
   - Embebidas en: `src/rayleigh_lut_embedded.c`
   - Generador: `assets/embed_luts.py`

### 🟡 Problema Pendiente

**Síntoma**: Amarillo/verde residual en bordes de la imagen  
**Zona afectada**: Bordes con ángulos extremos (VZA > 80°, zonas de transición día/noche)  
**Zona correcta**: Centro de la imagen (colores naturales)

**Valores actuales vs geo2grid**:
- Centro (SZA=24.72°): hpsv=0.074884 vs geo2grid=0.148217 → **0.5x** (subcorrección)
- Borde (SZA=61.19°): hpsv=0.259447 vs geo2grid=0.273056 → **0.95x** (casi correcto)

**Observación**: El factor de discrepancia varía con el ángulo solar, sugiriendo que hay una transformación no lineal faltante.

---

## ARQUITECTURA ACTUAL

### Flujo de Corrección Rayleigh

```
1. Calcular geometría (SZA, VZA, RAA)
   └─> reader_nc.c: compute_satellite_view_angles()
   
2. Clipear ángulos al rango de la LUT
   ├─> SZA: 0° - 87.68° (secante max = 24.75)
   └─> VZA: 0° - 70.53° (secante max = 3.0)
   
3. Convertir a secantes
   ├─> SZA_sec = 1 / cos(SZA * π/180)
   └─> VZA_sec = 1 / cos(VZA * π/180)
   
4. Interpolar en LUT (trilineal)
   └─> get_rayleigh_value(lut, SZA_sec, VZA_sec, RAA)
   
5. Reducir corrección en SZA alto
   └─> if SZA > 70°: factor = 1 - (SZA-70)/(88-70)
   
6. Aplicar corrección
   └─> reflectancia_corregida = original - r_corr
```

### Archivos Modificados

1. **src/rayleigh.c**
   - Línea 273: `get_rayleigh_value()` - Interpolación trilineal
   - Línea 520-537: Clipping de ángulos y conversión a secantes
   - Línea 544-550: Reducción en ángulos altos
   
2. **assets/extract_rayleigh_lut.py**
   - Modificado para mantener ejes en secantes
   - NO convierte secantes a ángulos
   - Path actualizado: `/home/aguilars/cspp/geo2grid_v_1_2/`

3. **assets/embed_luts.py** (NUEVO)
   - Convierte archivos .bin a arrays C embebidos
   - Genera: `src/rayleigh_lut_embedded.c` y `include/rayleigh_lut_embedded.h`

---

## PRÓXIMOS PASOS (Para Sesión Futura)

### Opción 1: Investigar Factor de Escala No Lineal

**Hipótesis**: Los valores de la LUT necesitan una transformación adicional que depende del ángulo solar.

**Pasos**:
1. Extraer múltiples valores de la LUT original para diferentes SZA
2. Comparar con valores de geo2grid en los mismos píxeles
3. Buscar patrón en el factor de corrección vs SZA
4. Implementar transformación si se identifica

**Comando para extraer valores**:
```python
# En assets/, ejecutar script de análisis comparativo
python3 compare_lut_values.py
```

### Opción 2: Verificar Banda Roja en Truecolor

**Hipótesis**: El problema puede estar en cómo se procesa la banda C02 (roja) antes/después de Rayleigh.

**Pasos**:
1. Verificar que C02 también recibe corrección Rayleigh (pyspectral lo hace)
2. Comparar procesamiento de C02 en geo2grid vs hpsv
3. Revisar función `_relax_rayleigh_refl_correction_where_cloudy` en pyspectral

### Opción 3: Análisis Detallado de Pyspectral

**Buscar en código fuente**:
```bash
cd geo2grid_v_1_2/libexec/python_runtime/lib/python3.11/site-packages/pyspectral/
grep -r "reflectance.*\*" *.py  # Buscar multiplicaciones adicionales
grep -r "def.*correction" *.py  # Buscar funciones de corrección
```

### Opción 4: Ajuste Empírico Regional

**Si no se encuentra causa raíz**:
1. Dividir imagen en regiones por SZA
2. Aplicar factor de corrección específico por región
3. Interpolar suavemente entre regiones

```c
// Ejemplo de ajuste regional
float correction_factor = 1.0f;
if (theta_s < 30.0f) {
    correction_factor = 2.0f;  // Centro necesita más corrección
} else if (theta_s < 60.0f) {
    // Interpolación lineal
    correction_factor = 2.0f - (theta_s - 30.0f) / 30.0f;
}
r_corr *= correction_factor;
```

---

## COMANDOS DE PRUEBA

### Regenerar LUTs
```bash
cd assets/
python3 extract_rayleigh_lut.py
python3 embed_luts.py
cd ..
make clean && make -j4
```

### Ejecutar y Comparar
```bash
./bin/hpsv rgb -m truecolor --rayleigh -g 2 -s -4 -v sample_data/028/*.nc -o test_rayleigh.png
# Comparar con geo2grid output
```

### Ver Valores Debug
```bash
grep "Sample pixel 88260056\|Sample pixel 102984753" tests/*.log
```

---

## REFERENCIAS

### Código Fuente Consultado
- `geo2grid_v_1_2/libexec/python_runtime/lib/python3.11/site-packages/pyspectral/rayleigh.py`
  - Línea 242: `res *= 100` - Conversión a porcentaje
  - Línea 227-230: `_clip_angles_inside_coordinate_range()` - Clipping de ángulos
  - Línea 290-302: `reduce_rayleigh_highzenith()` - Reducción en ángulos altos
  
- `geo2grid_v_1_2/libexec/python_runtime/lib/python3.11/site-packages/satpy/modifiers/atmosphere.py`
  - Línea 98-100: Parámetros de reducción por defecto
  - Línea 120-123: Aplicación de reducción si `reduce_strength > 0`

### Documentos Generados
- `docs/ANALISIS_RAYLEIGH_DISCREPANCIA.md` - Análisis inicial del problema
- `docs/RESUMEN_ANALISIS_RAYLEIGH.md` - Hallazgos y conclusiones
- Este archivo (`docs/plan_rayleigh_cor.md`)

---

## NOTAS TÉCNICAS

### Por Qué Secantes, No Ángulos

La dispersión de Rayleigh depende de la **masa de aire** atravesada por la luz, que es proporcional a `1/cos(θ)` (la secante del ángulo cenital). Por eso las LUTs usan secantes como ejes de interpolación en lugar de ángulos directos.

### Valores Típicos de la LUT

- **Rango**: 0.063 - 1.426 (fracción de reflectancia)
- **Media**: 0.525
- **Interpretación**: Fracción de la reflectancia observada que proviene de dispersión Rayleigh

### Límites Físicos

- **SZA max**: 87.68° - Más allá, el sol está demasiado bajo para corrección confiable
- **VZA max**: 70.53° - Más allá, el ángulo de visión es demasiado oblicuo
- **Corrección max razonable**: ~0.8 (80% de la reflectancia original)

---

## PROBLEMA IDENTIFICADO

Después de analizar los logs y código fuente de geo2grid/pyspectral:

1. **Las LUTs están en escala 0-1 (fracción), NO en porcentaje**
2. **Pyspectral multiplica por 100 antes de restar**: `res *= 100`
3. **Mi código multiplica por tau (0.235)**, resultando en correcciones 10x más pequeñas
4. **Las LUTs YA incorporan la física completa** - no necesitan tau adicional

---

## 1. Archivo: rayleigh.c
**Función:** `luts_rayleigh_correction` (Línea ~522)  
**Acción:** Cambiar multiplicación por tau a multiplicación por 100

### Cambio CRÍTICO:
```c
// ANTES (INCORRECTO - línea 522):
float r_corr = get_rayleigh_value(&lut, theta_s, nav->vza.data_in[i], nav->raa.data_in[i]) * tau;

// DESPUÉS (CORRECTO):
float r_corr = get_rayleigh_value(&lut, theta_s, nav->vza.data_in[i], nav->raa.data_in[i]) * 100.0f;
```

**Razón:** Las LUTs de pyspectral contienen valores normalizados (0-1.5) que representan reflectancias en fracción. Pyspectral los convierte a porcentaje multiplicando por 100. La multiplicación por tau es incorrecta y causa que la corrección sea ~10x más pequeña de lo que debería ser.

**Evidencia de los logs:**
- Valores LUT: 0.063-1.425 (escala fraccionaria)
- geo2grid aplica: `res *= 100` en `rayleigh.py:243`
- hpsv aplicaba: `* 0.235` → ERROR

---

## 2. Archivo: reader_nc.c
**Función:** `compute_satellite_view_angles` (Líneas ~748-750)  
**Acción:** Invertir la dirección del vector de visión del satélite.

### Cambio:
```c
// ANTES (líneas 748-750):
double dx = x_pixel - x_sat;
double dy = y_pixel - y_sat;
double dz = z_pixel - z_sat;

// DESPUÉS (CORRECTO):
double dx = x_sat - x_pixel;
double dy = y_sat - y_pixel;
double dz = z_sat - z_pixel;
```

**Razón:** El vector debe apuntar desde el píxel hacia el satélite (dirección de observación). Esto es consistente con la definición física del azimut de visión (VAA).

---

## 3. Archivo: reader_nc.c
**Función:** `compute_satellite_view_angles` (Línea ~768)  
**Acción:** Ajustar cálculo de cos_vza según vector invertido.

### Cambio:
```c
// ANTES (línea 768):
double cos_vza = -(dx * nx + dy * ny + dz * nz);

// DESPUÉS (CORRECTO):
double cos_vza = dx * nx + dy * ny + dz * nz;
```

**Razón:** Con el vector invertido (apuntando del píxel al satélite), el producto punto con la normal local ya da directamente el coseno del ángulo cenital de visión, sin necesidad de cambiar el signo.

---

## 4. Archivo: rayleigh.h (OPCIONAL)
**Línea:** ~17  
**Acción:** Actualizar valor de Tau a físico estándar.

### Cambio:
```c
// ANTES (línea 17):
#define RAYLEIGH_TAU_BLUE 0.235f

// DESPUÉS (recomendado):
#define RAYLEIGH_TAU_BLUE 0.167f  // Valor de Bucholtz (1995) para 0.47 µm
```

**Razón:** Ya no necesitamos valores "inflados" para compensar errores de escala. Este valor es físicamente correcto según Bucholtz (1995). Este cambio es opcional ya que las LUTs no usan tau directamente.

---

## 5. Archivo: rgb.c
**Función:** `apply_postprocessing` (Línea ~250)  
**Acción:** Eliminar parámetro tau del llamado.

### Cambio:
```c
// ANTES:
luts_rayleigh_correction(&ctx->comp_b, &nav, 1, RAYLEIGH_TAU_BLUE);

// DESPUÉS:
// El cuarto parámetro (tau) ya no se usa - la función lo ignora
luts_rayleigh_correction(&ctx->comp_b, &nav, 1, 1.0f);
```

**Razón:** El parámetro tau ya no se usa en la corrección. Las LUTs contienen toda la física necesaria.

## Verificación de Éxito

Después de aplicar los cambios, compilar y ejecutar:

```bash
make clean && make
./bin/hpsv -m truecolor -o test_corregido.png sample_data/028/OR_ABI-L1b-RadF-M6C0[1-3]*.nc
```

### Indicadores de corrección exitosa:

1. **Valores de corrección aumentados ~4x**: 
   - ANTES: Rayleigh media ~0.06 (6%)
   - DESPUÉS: Rayleigh media ~27% (como geo2grid)

2. **Canal Azul (C01)**: No debe presentar "nubes amarillas" ni "niebla azul" sobre tierra
   
3. **Océanos limpios**: Reflectancia entre 0.02 y 0.05 (2-5%)

4. **Comparación visual**: Imagen debe verse similar a la producida por geo2grid

### Nota sobre los cambios

Los cambios en `reader_nc.c` (vector de visión) son importantes para la corrección física, pero el cambio CRÍTICO que resuelve las "nubes amarillas" es el de `rayleigh.c` (multiplicar por 100 en vez de por tau).

Si después de aplicar solo el cambio de rayleigh.c el problema persiste, revisar:
- Que las LUTs embebidas sean las correctas de pyspectral
- Que el cálculo del azimut relativo sea simétrico (0-180°)
- Logs de valores sample para detectar outliers

---

## Referencias

- Análisis detallado: `docs/ANALISIS_RAYLEIGH_DISCREPANCIA.md`
- Logs comparativos:
  - `tests/abi_l1b_geotiff_20260128_180021.log` (geo2grid)
  - `tests/tcrayluts_20260281800.log` (hpsv)
- Código fuente pyspectral:
  - `geo2grid_v_1_2/.../pyspectral/rayleigh.py`
  - `geo2grid_v_1_2/.../satpy/modifiers/atmosphere.py`

