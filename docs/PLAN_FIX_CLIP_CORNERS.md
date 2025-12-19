# Plan: Corrección del Recorte con Esquinas Fuera del Globo

## 📋 Problema Identificado

**Contexto:** Cuando se usa `--clip` sin reproyección (`-r`) para una región amplia cuya esquina Upper Left queda fuera del disco visible del satélite, el recorte falla o se hace incorrectamente.

**Causa raíz:** La función `reprojection_find_pixel_for_coord()` busca el píxel válido más cercano a cada esquina del dominio geográfico solicitado. Si una esquina (e.g., Upper Left) cae completamente fuera del globo (todos sus píxeles cercanos tienen `lat=NonData` o `lon=NonData`), la función retorna `-1,-1` o encuentra un píxel válido lejano e incorrecto en el borde del disco.

**Impacto:** El bounding box calculado por `calculate_bounding_box()` usa coordenadas inválidas/incorrectas, resultando en un recorte que:
- Incluye áreas no deseadas del disco
- Excluye partes válidas del dominio solicitado
- Puede causar dimensiones incorrectas o crashes

**Referencia:** Comentario en `/home/aguilars/lanot/hpsatviews/TODO.txt` (líneas 57-67):
```
Comparando las salidas con y sin proyección, cuando recortamos sin
proyección se mocha una buena parte porque forza la esquina izquierda
superior a que exista y tiene que poner una parte del globo. Un mejor
algoritmo sería calcular las esquinas en coordenadas geoestacionarias,
las que sí están en el mapa, como la inferior izquierda y la superior
derecha y con ellas asignar la esquina superior izquierda.
```

---

## 🎯 Objetivos de la Corrección

1. **Inferir esquinas inválidas** a partir de esquinas válidas usando geometría rectangular.
2. **No perder datos válidos** del dominio solicitado.
3. **Mantener compatibilidad** con dominios completamente dentro del globo (caso común).
4. **Logging claro** para diagnóstico (advertir cuando se infieren esquinas).

---

## 🔍 Análisis Técnico

### Archivos Involucrados

| Archivo | Función Clave | Responsabilidad |
|---------|---------------|-----------------|
| `rgb.c` | líneas 343-390, 541-570 | Lógica de clipping PRE y POST reproyección |
| `rgb.c` | `calculate_bounding_box()` | Calcula min/max de las 4 esquinas |
| `reprojection.c` | `reprojection_find_pixel_for_coord()` | Busca píxel más cercano a coord geográfica |
| `processing.c` | líneas 129-180 | Clipping en comando `gray` |

### Flujo Actual (con falla)

```
1. Usuario solicita: --clip lon_min lat_max lon_max lat_min
2. Se calculan 4 esquinas geográficas:
   - Upper Left  (UL): lat_max, lon_min
   - Upper Right (UR): lat_max, lon_max
   - Lower Left  (LL): lat_min, lon_min
   - Lower Right (LR): lat_min, lon_max

3. Para cada esquina:
   reprojection_find_pixel_for_coord() busca píxel (ix, iy) válido más cercano
   
4. PROBLEMA: Si UL está fuera del globo → retorna (-1,-1) o píxel incorrecto

5. calculate_bounding_box() usa las 4 coordenadas (incluyendo inválidas)
   → Bounding box incorrecto

6. dataf_crop() recorta con coordenadas erróneas
   → Resultado: imagen truncada o mal alineada
```

### Geometría de las Esquinas

En una proyección geoestacionaria, el dominio rectangular en coordenadas geográficas (lat/lon) **NO es rectangular** en el espacio de píxeles geoestacionarios (x/y). Sin embargo, podemos aproximar las esquinas faltantes usando las esquinas válidas:

**Regla de inferencia (basada en alineación de columnas y filas):**

Si tenemos:
- `UR` = Upper Right (válida): `(ix_ur, iy_ur)`
- `LL` = Lower Left (válida): `(ix_ll, iy_ll)`

Podemos inferir:
- `UL` = Upper Left (inválida): **`ix ≈ ix_ll`, `iy ≈ iy_ur`**
  - Razonamiento: UL debe estar en la misma **columna x** que LL (mismo lon_min)
    y en la misma **fila y** que UR (mismo lat_max)

Similarmente:
- `LR` = Lower Right: `ix ≈ ix_ur`, `iy ≈ iy_ll`
- `UL` desde `UR` y `LL`: `ix ≈ ix_ll`, `iy ≈ iy_ur`
- `UR` desde `UL` y `LR`: `ix ≈ ix_lr`, `iy ≈ iy_ul`

**Casos a cubrir:**

| Esquinas Válidas | Esquinas a Inferir | Estrategia |
|------------------|-------------------|------------|
| LL, UR, LR | UL | `UL.x = LL.x; UL.y = UR.y` |
| UL, UR, LR | LL | `LL.x = UL.x; LL.y = LR.y` |
| UL, LL, LR | UR | `UR.x = LR.x; UR.y = UL.y` |
| UL, UR, LL | LR | `LR.x = UR.x; LR.y = LL.y` |
| LL, UR | UL, LR | `UL = (LL.x, UR.y); LR = (UR.x, LL.y)` |
| UL, LR | UR, LL | `UR = (LR.x, UL.y); LL = (UL.x, LR.y)` |

**Nota:** Si solo 1 esquina es válida (o ninguna), el dominio solicitado está completamente fuera del disco → ERROR (no se puede recortar).

---

## 🛠️ Solución Propuesta

### Enfoque: Nueva función helper `infer_missing_corners()`

**Ubicación:** Agregar a `rgb.c` (o crear `clip_utils.c` si crece mucho).

**Firma:**
```c
/**
 * @brief Infiere coordenadas de esquinas inválidas a partir de esquinas válidas.
 * 
 * Cuando una región de clip es muy amplia y alguna esquina queda fuera del disco
 * visible del satélite, esta función usa geometría rectangular para estimar las
 * coordenadas geoestacionarias (píxeles) de las esquinas faltantes a partir de
 * las esquinas válidas.
 * 
 * @param ix_tl, iy_tl [in/out] Coords Upper Left
 * @param ix_tr, iy_tr [in/out] Coords Upper Right
 * @param ix_bl, iy_bl [in/out] Coords Lower Left
 * @param ix_br, iy_br [in/out] Coords Lower Right
 * @return Número de esquinas que fueron inferidas (0-4)
 */
static int infer_missing_corners(int* ix_tl, int* iy_tl,
                                 int* ix_tr, int* iy_tr,
                                 int* ix_bl, int* iy_bl,
                                 int* ix_br, int* iy_br);
```

**Algoritmo:**

1. Identificar qué esquinas son inválidas (coords == -1)
2. Contar válidas: si `valid_count < 2` → ERROR (dominio fuera del globo)
3. Para cada esquina inválida:
   - Caso UL inválido:
     - Si LL válido: `*ix_ul = ix_ll`
     - Si UR válido: `*iy_ul = iy_ur`
     - Si TR válido y LL inválido: `*ix_ul = ?` (usar promedio o extrapolación)
   - Caso UR inválido: `*ix_ur = ix_br` (si BR válido), `*iy_ur = iy_ul` (si UL válido)
   - Caso LL: `*ix_ll = ix_ul`, `*iy_ll = iy_bl` / `iy_lr`
   - Caso LR: `*ix_lr = ix_ur`, `*iy_lr = iy_ll`
   
4. Logging: `LOG_INFO("Esquinas inferidas: UL=%d, UR=%d, LL=%d, LR=%d", ...)`
5. Retornar número de esquinas inferidas

**Pseudo-código (simplificado para caso más común: UL inválido):**

```c
bool ul_invalid = (*ix_tl < 0 || *iy_tl < 0);
bool ur_invalid = (*ix_tr < 0 || *iy_tr < 0);
bool ll_invalid = (*ix_bl < 0 || *iy_bl < 0);
bool lr_invalid = (*ix_br < 0 || *iy_br < 0);

int valid_count = 4 - (ul_invalid + ur_invalid + ll_invalid + lr_invalid);

if (valid_count < 2) {
    LOG_ERROR("Dominio de clip completamente fuera del disco visible (solo %d esquinas válidas)", valid_count);
    return -1; // Error crítico
}

int inferred = 0;

// Inferir UL
if (ul_invalid) {
    if (!ll_invalid && !ur_invalid) {
        *ix_tl = *ix_bl; // Misma columna que LL
        *iy_tl = *iy_tr; // Misma fila que UR
        inferred++;
        LOG_INFO("Esquina UL inferida desde LL y UR: (%d, %d)", *ix_tl, *iy_tl);
    } else if (!ll_invalid && !lr_invalid) {
        *ix_tl = *ix_bl;
        *iy_tl = *iy_bl - (*iy_br - *iy_br); // Usar LL y diferencia vertical de LR (si existe)
        inferred++;
    }
    // ... más casos
}

// Repetir para UR, LL, LR
// ...

return inferred;
```

---

### Modificaciones en `rgb.c`

#### 1. Función `create_rgb_multiband()` — Clipping PRE-reproyección (líneas ~343-390)

**Cambio:**

```c
// Encontrar las 4 esquinas en el espacio geoestacionario
int ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br;
reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_min, &ix_tl, &iy_tl);
reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_max, &ix_tr, &iy_tr);
reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_min, &ix_bl, &iy_bl);
reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_max, &ix_br, &iy_br);

LOG_DEBUG("Píxeles pre-reproj (raw): TL(%d,%d), TR(%d,%d), BL(%d,%d), BR(%d,%d)", 
          ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br);

// NUEVO: Inferir esquinas inválidas
int inferred_count = infer_missing_corners(&ix_tl, &iy_tl, &ix_tr, &iy_tr,
                                           &ix_bl, &iy_bl, &ix_br, &iy_br);
if (inferred_count < 0) {
    LOG_ERROR("Dominio de clip fuera del disco visible. Ignorando --clip.");
    // Continuar sin recortar (o abortar según preferencia)
} else if (inferred_count > 0) {
    LOG_INFO("Inferidas %d esquinas. Píxeles finales: TL(%d,%d), TR(%d,%d), BL(%d,%d), BR(%d,%d)",
             inferred_count, ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br);
}

// Calcular bounding box usando función auxiliar
calculate_bounding_box(ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br,
                       &clip_x_start, &clip_y_start, &clip_width, &clip_height);
```

#### 2. Función `create_rgb_multiband()` — Clipping POST-procesamiento (líneas ~541-570)

**Aplicar el mismo patrón:**

```c
// Datos originales (cuadrícula geoestacionaria) - búsqueda de píxeles
int ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br;
reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_min, &ix_tl, &iy_tl);
// ... (igual que antes)

LOG_DEBUG("Píxeles de las esquinas (raw): TL(%d,%d), TR(%d,%d), BL(%d,%d), BR(%d,%d)", 
          ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br);

// NUEVO: Inferir esquinas inválidas
int inferred_count = infer_missing_corners(&ix_tl, &iy_tl, &ix_tr, &iy_tr,
                                           &ix_bl, &iy_bl, &ix_br, &iy_br);
if (inferred_count < 0) {
    LOG_WARN("Dominio de clip fuera del disco. Ignorando recorte POST-procesamiento.");
    // No recortar
} else if (inferred_count > 0) {
    LOG_INFO("Inferidas %d esquinas para recorte POST. Coords finales: TL(%d,%d), ...",
             inferred_count, ix_tl, iy_tl);
}

// Calcular bounding box
unsigned int x_start, y_start, crop_width, crop_height;
calculate_bounding_box(ix_tl, iy_tl, ix_tr, iy_tr, ix_bl, iy_bl, ix_br, iy_br,
                       &x_start, &y_start, &crop_width, &crop_height);
```

---

### Modificaciones en `processing.c`

**Archivo:** `processing.c`, función `create_single_gray()` (líneas ~129-180)

**Cambio:** Aplicar el mismo patrón si no hay reproyección. Si hay reproyección, el código usa interpolación lineal (no requiere cambio).

```c
if (nav_loaded) {
    // ... parsing de clip coords ...
    
    int ix_start, iy_start, ix_end, iy_end;
    if (do_reprojection) {
        // Caso reproyectado: usar interpolación lineal (OK, no requiere cambio)
        // ...
    } else {
        // Caso original: usar búsqueda de píxeles
        int ix_tl, iy_tl, ix_br, iy_br;
        reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_max, clip_lon_min, &ix_tl, &iy_tl);
        reprojection_find_pixel_for_coord(&navla, &navlo, clip_lat_min, clip_lon_max, &ix_br, &iy_br);
        
        // NUEVO: Inferir si es necesario (caso simplificado: solo 2 esquinas)
        // Para processing.c podríamos usar solo TL y BR; 
        // si alguna es inválida, usar la válida para estimar la otra
        if (ix_tl < 0 || iy_tl < 0) {
            LOG_WARN("Esquina TL fuera del disco. Aproximando con BR.");
            // Estrategia simplificada: usar BR y calcular offset aproximado
            // O llamar a una versión simplificada de infer_missing_corners
        }
        
        ix_start = ix_tl;
        iy_start = iy_tl;
        ix_end = ix_br;
        iy_end = iy_br;
    }
    
    unsigned int crop_width = (ix_end > ix_start) ? (ix_end - ix_start) : 0;
    unsigned int crop_height = (iy_end > iy_start) ? (iy_end - iy_start) : 0;
    // ...
}
```

**Nota:** Para `processing.c` solo se usan 2 esquinas (TL y BR), por lo que la lógica de inferencia puede ser más simple o podríamos calcular las 4 esquinas completas igual que en `rgb.c`.

---

## 📝 Implementación Paso a Paso

### Paso 1: Crear función `infer_missing_corners()` en `rgb.c`

- [ ] Agregar función estática antes de `create_rgb_multiband()`
- [ ] Implementar lógica de inferencia con todos los casos (UL, UR, LL, LR)
- [ ] Agregar logging detallado (DEBUG para cálculos, INFO para resultados)
- [ ] Manejar caso error (< 2 esquinas válidas → retornar -1)

### Paso 2: Integrar en clipping PRE-reproyección (`rgb.c` línea ~360)

- [ ] Llamar a `infer_missing_corners()` después de `reprojection_find_pixel_for_coord()`
- [ ] Agregar manejo de error si retorna -1
- [ ] Actualizar logs para mostrar coords originales vs. inferidas

### Paso 3: Integrar en clipping POST-procesamiento (`rgb.c` línea ~550)

- [ ] Aplicar mismo patrón que Paso 2
- [ ] Verificar que funciona tanto con reproyección como sin ella

### Paso 4: Adaptar `processing.c` (opcional/simplificado)

- [ ] Decidir si usar las 4 esquinas o solo TL/BR con inferencia básica
- [ ] Implementar y probar

### Paso 5: Testing

- [ ] Caso 1: Dominio completamente dentro del disco (no debe cambiar)
- [ ] Caso 2: UL fuera del disco, LL y UR válidas (debe inferir UL)
- [ ] Caso 3: UL y UR fuera, LL y LR válidas (debe inferir ambas)
- [ ] Caso 4: Solo 1 esquina válida (debe fallar con error claro)
- [ ] Caso 5: Dominio completamente fuera del disco (debe fallar con error)

### Paso 6: Documentación

- [ ] Actualizar comentarios de código
- [ ] Actualizar `README.md` con nota sobre regiones amplias
- [ ] Agregar ejemplo de uso con región amplia en ejemplos

---

## 🧪 Casos de Prueba Sugeridos

### Test 1: Región CONUS Ampliada (UL fuera del disco)

```bash
# Dominio muy amplio que incluye parte del Pacífico (UL fuera)
./hpsatviews rgb -m truecolor \
    --clip -135.0 50.0 -60.0 10.0 \
    -o test_wide_domain.png \
    archivo.nc
```

**Expectativa:**
- Mensaje: `Esquina UL inferida desde LL y UR: (xxx, yyy)`
- Imagen recortada incluye toda la región válida sin "mochar" el área útil

### Test 2: Región Centrada (todas las esquinas válidas)

```bash
# Región estándar centrada en México
./hpsatviews rgb -m truecolor \
    --clip -107.23 22.72 -93.84 14.94 \
    -o test_normal_domain.png \
    archivo.nc
```

**Expectativa:**
- Sin mensajes de inferencia
- Resultado idéntico al actual (no debe cambiar)

### Test 3: Dominio Completamente Fuera

```bash
# Región en Europa (fuera del disco de GOES-19)
./hpsatviews rgb -m truecolor \
    --clip 0.0 60.0 20.0 40.0 \
    -o test_invalid_domain.png \
    archivo.nc
```

**Expectativa:**
- Error: `Dominio de clip completamente fuera del disco visible (solo 0 esquinas válidas)`
- Procesamiento continúa sin recortar (o aborta según decisión de diseño)

---

## 📊 Riesgos y Mitigaciones

| Riesgo | Probabilidad | Impacto | Mitigación |
|--------|--------------|---------|------------|
| Inferencia incorrecta en geometrías complejas (bordes del disco) | Media | Medio | Validar con logs DEBUG; comparar con/sin reproyección |
| Regresión en dominios normales (todas esquinas válidas) | Baja | Alto | Test exhaustivo de casos existentes antes de merge |
| Inferencia con solo 2 esquinas diagonales (UL+LR o UR+LL) da resultados imprecisos | Media | Bajo | Documentar limitación; recomendar usar dominios con al menos 3 esquinas válidas |
| Overhead de procesamiento | Muy Baja | Muy Bajo | Función solo se llama 1 vez por clip; impacto despreciable |

---

## ✅ Criterios de Aceptación

1. ✅ Dominios con todas las esquinas dentro del disco funcionan igual que antes (sin regresión)
2. ✅ Dominios con 1-2 esquinas fuera del disco se recortan correctamente usando inferencia
3. ✅ Dominios completamente fuera del disco fallan con error claro y no crashean
4. ✅ Logging DEBUG muestra coords originales, inferidas y finales para diagnóstico
5. ✅ Código compila sin warnings
6. ✅ Documentación actualizada en README y comentarios de código

---

## 📚 Referencias

- **Archivo de problema original:** `/home/aguilars/lanot/hpsatviews/TODO.txt` (líneas 57-67)
- **Código afectado:**
  - `rgb.c`: líneas 193-235 (calculate_bounding_box), 343-390 (clip PRE-reproj), 541-570 (clip POST)
  - `reprojection.c`: líneas 175-265 (reprojection_find_pixel_for_coord)
  - `processing.c`: líneas 129-180 (clip en gray)
- **Documentación:** `README.md` sección "Recorte Geográfico Inteligente"

---

**Autor:** GitHub Copilot (Claude Sonnet 4.5)  
**Fecha:** 1 de diciembre de 2025  
**Estado:** 🟡 Borrador para revisión
