# Guía de Implementación: CLAHE para hpsatviews

**Autor:** Asistente Gemini  
**Fecha:** 2025  
**Contexto:** Implementación de *Contrast Limited Adaptive Histogram Equalization* en C para imágenes de satélite (GOES/hpsatviews).

---

## 1. Conceptos Fundamentales

A diferencia de la ecualización global, CLAHE opera en pequeñas regiones llamadas "tiles" (bloques).

1.  **Tile Grid:** La imagen se divide en una cuadrícula (ej. $8 \times 8$).
2.  **Clip Limit:** Se limita la altura del histograma local para no amplificar ruido.
3.  **Interpolación Bilineal:** Se usa para eliminar los bordes visibles entre bloques.

---

## 2. Estructuras de Datos Sugeridas

No es necesario modificar `ImageData` en `image.h`, pero necesitarás estructuras temporales dentro de `image.c`.

### Definiciones
Define esto al inicio de tu implementación en `image.c` o como constantes:

```c
#define CLAHE_NUM_TILES_X 8
#define CLAHE_NUM_TILES_Y 8
#define CLAHE_NUM_BINS 256
// Clip limit normalizado (ej. 4.0 es estándar, valores más altos = más contraste y más ruido)
#define CLAHE_CLIP_LIMIT_FACTOR 4.0
```

## 3\. Fase Lógica (Funciones Auxiliares)

Estas funciones deben ser `static` en `image.c`.

### 3.1 Recorte y Redistribución del Histograma

Esta es la diferencia clave con la ecualización normal.

**Pseudocódigo de Implementación:**

```c
/**
 * @brief Recorta el histograma y redistribuye el exceso.
 * @param hist Apuntador al histograma (array de 256 enteros).
 * @param limit Límite máximo de pixeles permitidos por bin.
 */
static void clip_histogram(unsigned int* hist, unsigned int limit) {
    unsigned int excess = 0;
    
    // Paso 1: Calcular exceso y recortar
    for (int i = 0; i < CLAHE_NUM_BINS; i++) {
        if (hist[i] > limit) {
            excess += (hist[i] - limit);
            hist[i] = limit;
        }
    }
    
    // Paso 2: Redistribución uniforme
    unsigned int avg_inc = excess / CLAHE_NUM_BINS;
    unsigned int remainder = excess % CLAHE_NUM_BINS;

    if (avg_inc > 0) {
        for (int i = 0; i < CLAHE_NUM_BINS; i++) {
            hist[i] += avg_inc;
        }
    }
    
    // Paso 3: Redistribuir el remanente aleatoriamente o secuencialmente
    // (Secuencial es más rápido y suficiente para este caso)
    for (int i = 0; i < remainder; i++) {
        hist[i]++; 
    }
}
```

### 3.2 Mapeo CDF (Función de Transferencia)

Calcula la CDF (Cumulative Distribution Function) para un tile específico.

```c
static void calculate_cdf_mapping(unsigned int* hist, unsigned char* map_lut, int pixels_per_tile) {
    unsigned int sum = 0;
    float scale = 255.0f / pixels_per_tile;
    
    for (int i = 0; i < CLAHE_NUM_BINS; i++) {
        sum += hist[i];
        map_lut[i] = (unsigned char)(sum * scale + 0.5f);
    }
}
```

-----

## 4\. Flujo Principal: `image_apply_clahe`

Esta es la función pública.

### Pasos Detallados

1.  **Validación:** Verificar `bpp`. Si es RGB (3), decidir si aplicar por canal o convertir a LAB/HSL (recomendado: aplicar a RGB independientemente es más fácil para empezar).
2.  **Configuración de Tiles:**
      * Calcular `tile_width = width / CLAHE_NUM_TILES_X`
      * Calcular `tile_height = height / CLAHE_NUM_TILES_Y`
      * Calcular `clip_limit_pixels = (clip_factor * tile_width * tile_height) / CLAHE_NUM_BINS`
3.  **Memoria LUT (Look Up Table):**
      * Asignar un array gigante para guardar los mapas de todos los tiles:
      * `unsigned char lut[CLAHE_NUM_TILES_Y][CLAHE_NUM_TILES_X][CLAHE_NUM_BINS]`

### 4.1 Cálculo Paralelo de LUTs (OpenMP)

```c
#pragma omp parallel for collapse(2)
for (int ty = 0; ty < CLAHE_NUM_TILES_Y; ty++) {
    for (int tx = 0; tx < CLAHE_NUM_TILES_X; tx++) {
        // 1. Definir los límites del tile en la imagen (x_start, y_start, etc)
        // 2. Calcular histograma local recorriendo los pixeles de ese tile
        // 3. clip_histogram(...)
        // 4. calculate_cdf_mapping(...) y guardar en lut[ty][tx]
    }
}
```

-----

## 5\. Interpolación Bilineal (Reconstrucción)

Esta es la parte más delicada. No se aplica el mapa del tile donde está el pixel, sino una mezcla de los mapas de los centros de tiles cercanos.

**Lógica de Iteración:**
Recorre pixel por pixel $(x, y)$ de la imagen.

1.  **Encontrar vecinos:** Determina los 4 tiles cuyos centros rodean al pixel actual.
      * *Nota:* En los bordes de la imagen solo tendrás 2 vecinos, y en las esquinas 1.
2.  **Calcular coeficientes:**
      * `tx = (x - centro_tile_izq_x) / distancia_entre_centros_x`
      * `ty = (y - centro_tile_arr_y) / distancia_entre_centros_y`
3.  **Obtener valores mapeados:**
      * `val_TL = lut[tile_arr_izq][pixel_val]` (Top-Left)
      * `val_TR = lut[tile_arr_der][pixel_val]` (Top-Right)
      * `val_BL = lut[tile_abj_izq][pixel_val]` (Bottom-Left)
      * `val_BR = lut[tile_abj_der][pixel_val]` (Bottom-Right)
4.  **Interpolar:**
      * `val = (1-tx)*(1-ty)*val_TL + tx*(1-ty)*val_TR + ...`

-----

## 6\. Estrategia de Color (RGB)

Para imágenes de satélite (GOES):

**Opción A (Simple - Por Canal):**
Llamar a la lógica de CLAHE para R, luego para G, luego para B.

  * *Pros:* Muy fácil de implementar.
  * *Contras:* Puede saturar colores artificialmente.

**Opción B (Luminosidad - Recomendada):**

1.  Convertir pixel RGB a YCbCr o HSL.
2.  Aplicar CLAHE solo al canal Y (Luminancia) o L.
3.  Convertir de vuelta a RGB.

> **Recomendación:** Empieza con la **Opción A** para verificar que el algoritmo funciona y luego refina a la Opción B si los colores se ven extraños.

-----

## 7\. Checklist para `image.c`

  - [ ] Incluir `<math.h>` y `<omp.h>`.
  - [ ] Asegurarse de manejar el `malloc` y `free` de la LUT gigante (son $8 \times 8 \times 256$ bytes, es pequeño, puede ir en el stack o heap).
  - [ ] Verificar que los bucles `for` que recorren la imagen respeten el `bpp` (saltos de 3 en 3 para RGB).

<!-- end list -->

```
```