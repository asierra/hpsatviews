Aquí van mis hallazgos. La conclusión principal es que **el análisis fue escrito asumiendo que no teníamos casi nada implementado, pero en realidad ya tenemos la gran mayoría**. Solo hay 2 brechas reales.

---

## Hallazgos: Lo que ya tenemos vs. lo que propone el análisis

### 1. Corrección de Rayleigh — ✅ YA IMPLEMENTADO (igual que geo2grid)

El análisis dice que "probablemente no tenemos corrección de Rayleigh." **Incorrecto.** Ya la tenemos completa:

- LUTs extraídos de **pyspectral** con perfil `us-standard` y tipo `rayleigh_only` — exactamente lo mismo que usa geo2grid por defecto
- Interpolación **trilineal** en tabla 3D (96 × 9 × 19 valores) indexada por SZA, VZA y azimut relativo
- Se aplica a **C01 (azul)** y **C02 (rojo)**, pero **no a C03 (NIR)** — igual que geo2grid
- Incluye suavizado en zona de terminador (70°–88° SZA) para evitar artefactos
- Implementado en rayleigh.c con LUTs embebidos en rayleigh_lut_embedded.c

### 2. Piecewise Linear Stretch — ✅ YA IMPLEMENTADO (idéntico)

El análisis sugiere "extraer los arreglos xp/fp del YAML de geo2grid." **Ya los tenemos exactos:**

| Input (xp) | Output (fp) | hpsatviews | geo2grid |
|-------------|-------------|------------|----------|
| 0 | 0 | ✅ | ✅ |
| 25 | 90 | ✅ | ✅ |
| 55 | 140 | ✅ | ✅ |
| 100 | 175 | ✅ | ✅ |
| 255 | 255 | ✅ | ✅ |

Implementado en truecolor.c como `GEO2GRID_STRETCH_X[]` y `GEO2GRID_STRETCH_Y[]`.

### 3. Corrección SZA — ✅ YA IMPLEMENTADO

`reflectancia / cos(SZA)` con corte en 85°, igual que geo2grid. Implementado en truecolor.c.

---

## Las 2 brechas REALES que causan diferencias visuales

### Brecha 1: Fórmula de verde sintético (impacto moderado)

| | Fórmula | Coef. Blue | Coef. Red | Coef. NIR |
|---|---------|-----------|-----------|-----------|
| **hpsatviews** | EDC + boost | 0.457 | 0.484 | 0.059 → luego ×1.05 |
| **geo2grid default** | Estándar | 0.465 | 0.465 | 0.07 |

Diferencias:
- Nuestros coeficientes son la variante **EDC**, no la default de geo2grid
- Además aplicamos un **boost de ×1.05 al verde** que geo2grid **no aplica** (rgb.c línea 114)
- Resultado: nuestros verdes son ligeramente más intensos y con balance Red/Blue diferente

### Brecha 2: Ratio Sharpening (impacto visual SIGNIFICATIVO) — ❌ NO IMPLEMENTADO

Esta es probablemente **la mayor diferencia visible** entre ambos sistemas. geo2grid usa la resolución nativa de C02 (0.5 km) para mejorar la nitidez de los otros canales:

```
ratio = C02_500m / promedio_2x2(C02_500m)
ratio = clamp(ratio, 0, 1.5)
green_sharpened = green_1km × ratio
blue_sharpened  = blue_1km  × ratio
red = C02_500m  (sin cambio)
```

Esto **propaga el detalle fino de 0.5 km a los 3 canales**, produciendo una imagen significativamente más nítida en bordes de nubes, costas y texturas superficiales. Nosotros simplemente igualamos todas las resoluciones por resampling (boxfilter/bilinear) y perdemos ese detalle.

---

## Plan: Cerrar la brecha visual

### Fase 1: Fórmula verde configurable (cambio pequeño)
- Agregar opción para seleccionar variante de fórmula verde (EDC actual, geo2grid default, CIMSS)
- Evaluar si el boost ×1.05 debe mantenerse, eliminarse, o hacerse configurable
- **Archivos:** truecolor.c, config.h, config.c, args.c

### Fase 2: Ratio Sharpening (cambio mediano, mayor impacto)
- Implementar sharpening basado en ratio usando C02 en resolución nativa
- Solo aplicable cuando se trabaja con resolución completa (`--full-res`)
- Requiere mantener C02 a 0.5 km separado del resampleo general
- **Archivos:** truecolor.c o nuevo módulo, rgb.c

### Verificación
- Comparar outputs con las imágenes de referencia en testtc
- Validar que los coeficientes alternos produzcan resultados más cercanos a geo2grid

---

**¿Qué opinas? ¿Procedemos con ambas fases, solo una, o quieres ajustar el alcance?**
