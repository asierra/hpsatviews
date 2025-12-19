# Análisis: Estrategia de Recorte con Reproyección

## ✅ SOLUCIÓN IMPLEMENTADA (Versión Final)

**Fecha:** 4 de diciembre de 2025

### El Problema Real

Cuando se recorta y reproyecta (`--clip` + `-r`), aparece un **rectángulo deformado** con el dominio más amplio que el solicitado. Esto ocurre por dos razones:

1. **Recorte en espacio geoestacionario:** Solo evaluar las 4 esquinas crea un bounding box que no captura todos los píxeles
2. **Falta de recorte POST-reproyección:** El bounding box geoestacionario incluye píxeles que, al reproyectarse, caen **fuera** del dominio geográfico solicitado

### Ejemplo del Problema

Usuario solicita: `--clip -107.23 22.72 -93.84 14.94`

Sin la solución:
```
Extensión reproyectada: lat[14.714, 23.196], lon[-109.525, -92.781]
                              ↑↑↑↑↑ MÁS AMPLIO que lo solicitado ↑↑↑↑↑
```

### La Solución: Dos Fases de Recorte

#### Fase 1: Recorte PRE-reproyección (Muestreo Denso de Bordes)

En lugar de evaluar solo 4 esquinas, **muestreamos 20 puntos por cada borde** del dominio geográfico:

```c
const int SAMPLES_PER_EDGE = 20;  // 84 puntos totales

// Para cada borde del dominio geográfico:
//   Borde superior:  lat=lat_max, lon ∈ [lon_min, lon_max]
//   Borde inferior:  lat=lat_min, lon ∈ [lon_min, lon_max]  
//   Borde izquierdo: lon=lon_min, lat ∈ [lat_min, lat_max]
//   Borde derecho:   lon=lon_max, lat ∈ [lat_min, lat_max]

// Bounding box = min/max de TODOS los puntos muestreados
```

**Beneficio:** Captura **todos** los píxeles geoestacionarios que mapean al dominio, no solo las esquinas.

#### Fase 2: Recorte POST-reproyección (Interpolación Lineal)

Después de reproyectar a una malla geográfica regular, aplicamos un recorte final:

```c
// Los datos reproyectados están en malla regular [lon_min, lon_max] × [lat_min, lat_max]
// Podemos usar interpolación lineal para encontrar los límites exactos del clip

ix_start = ((clip_lon_min - lon_min) / lon_range) * width
iy_start = ((lat_max - clip_lat_max) / lat_range) * height
ix_end   = ((clip_lon_max - lon_min) / lon_range) * width
iy_end   = ((lat_max - clip_lat_min) / lat_range) * height

// Recortar imagen y actualizar navegación
```

**Beneficio:** Elimina píxeles que cayeron fuera del dominio solicitado durante la reproyección.

### Flujo Completo

```
1. Usuario solicita: --clip lon_min lat_max lon_max lat_min -r
                                 ↓
2. FASE 1: Recorte PRE-reproyección
   - Muestrear 84 puntos en bordes del dominio geográfico
   - Encontrar píxeles geoestacionarios correspondientes
   - Calcular bounding box que los contiene
   - Recortar canales y navegación
                                 ↓
3. Reproyectar datos recortados a malla geográfica
                                 ↓
4. FASE 2: Recorte POST-reproyección
   - Calcular índices del dominio solicitado en la malla geográfica
   - Recortar canales reproyectados
   - Actualizar navegación al dominio exacto
                                 ↓
5. Generar imagen final (sin píxeles extra)
```

### Archivos Modificados

1. **`rgb.c`**:
   - **Líneas ~332-420**: Recorte PRE-reproyección con muestreo denso
   - **Líneas ~492-542**: Recorte POST-reproyección con interpolación lineal
   - **Líneas ~593-689**: Recorte POST-procesamiento (sin reproyección)
   - Eliminadas funciones obsoletas: `infer_missing_corners()`, `calculate_bounding_box()`

2. **`processing.c`**:
   - **Líneas ~167-256**: Muestreo denso en comando `gray`

### Beneficios

✅ **Elimina el rectángulo deformado** - dominio exacto al solicitado  
✅ **Captura toda la deformación** geométrica del dominio  
✅ **Sin pérdida de datos** - incluye todos los píxeles relevantes  
✅ **Funcionalidad existente intacta** - mejora transparente  
✅ **Maneja dominios parcialmente fuera del disco** (≥4 muestras válidas)  

### Resultado Esperado

Con el comando:
```bash
./hpsatviews rgb --mode ash archivo.nc --clip -107.23 22.72 -93.84 14.94 -r
```

**Antes:**
```
Extensión: lat[14.714, 23.196], lon[-109.525, -92.781]  ← MÁS AMPLIO
```

**Después:**
```
Extensión: lat[14.940, 22.720], lon[-107.230, -93.840]  ← EXACTO
```

---

## Problema Anterior (Ya Resuelto)

Cuando se usa `--clip` con reproyección (`-r`), algunas esquinas del dominio geográfico pueden caer **fuera del disco visible** del satélite. Esto genera un rectángulo deformado en el espacio geoestacionario, con huecos en las esquinas.

### Ejemplo Visual del Problema

```
Espacio Geográfico (lat/lon):        Espacio Geoestacionario (pixels):
┌────────────────┐                   
│ UL          UR │                        ?  [VACÍO]  ?
│                │                    ┌─────────────────┐
│                │          =>        │ Datos Válidos   │
│                │                    │                 │
│ LL          LR │                    └─────────────────┘
└────────────────┘                       ?           ?
```

En el espacio geoestacionario, **solo las esquinas que caen dentro del disco tienen coordenadas válidas**. Las que caen fuera retornan `(-1, -1)` de `reprojection_find_pixel_for_coord()`.

## Solución Implementada

Ya existe la función `infer_missing_corners()` que infiere las esquinas inválidas usando geometría rectangular.

### Principio Geométrico

En proyección geoestacionaria:
- **Líneas de latitud constante** → aproximadamente horizontales en el espacio de píxeles
- **Líneas de longitud constante** → aproximadamente verticales en el espacio de píxeles

Por lo tanto:
- **UL y LL** comparten la misma columna X (misma longitud)
- **UR y LR** comparten la misma columna X (misma longitud)
- **UL y UR** comparten la misma fila Y (misma latitud)
- **LL y LR** comparten la misma fila Y (misma latitud)

## Casos de Inferencia

### Caso 1: Una Esquina Faltante (3 válidas) ✅ ÓPTIMO

Ejemplo: **UL inválida**, LL, UR, LR válidas

```
Inferencia:
  UL.x = LL.x  (misma longitud que LL)
  UL.y = UR.y  (misma latitud que UR)
```

**Precisión: EXACTA** - Usa las propiedades de las líneas de lat/lon

### Caso 2: Dos Esquinas Diagonales Faltantes (2 válidas opuestas) ✅ ÓPTIMO

Ejemplo: **UL y LR inválidas**, LL y UR válidas

```
Inferencia:
  UL.x = LL.x
  UL.y = UR.y
  
  LR.x = UR.x
  LR.y = LL.y
```

**Precisión: EXACTA** - Caso ideal de inferencia

### Caso 3: Dos Esquinas del Mismo Lado Faltantes ⚠️ REQUIERE EXTRAPOLACIÓN

Ejemplo: **UL y UR inválidas** (todo el borde superior fuera), LL y LR válidas

```
Problema: No tenemos información directa sobre lat_max
Solución actual: Extrapolar usando el ancho del borde inferior
  
  width_bottom = |LR.x - LL.x|
  UL.x = LL.x
  UL.y = LL.y - width_bottom  (asume proporción cuadrada)
  
  UR.x = LR.x
  UR.y = LR.y - width_bottom
```

**Precisión: APROXIMADA** - Depende de la geometría del dominio

## Mejoras Sugeridas a la Implementación Actual

### 1. **Optimizar la Lógica de Casos Diagonales**

El código actual verifica `!ll_invalid && !ur_invalid` dos veces para UL. Simplificar:

```c
// Inferir Upper Left (UL)
if (ul_invalid) {
    if (!ll_invalid && !ur_invalid) {
        // CASO DIAGONAL IDEAL
        *ix_tl = *ix_bl;  
        *iy_tl = *iy_tr;  
        inferred++;
    } else if (!ll_invalid) {
        // LL válida: usar su columna X
        *ix_tl = *ix_bl;
        if (!ur_invalid) {
            *iy_tl = *iy_tr;  // UR válida: usar su fila Y
        } else if (!lr_invalid) {
            // Extrapolar Y desde el borde inferior
            int height = abs(*iy_br - *iy_bl);
            *iy_tl = (*iy_bl > height) ? (*iy_bl - height) : 0;
        }
        inferred++;
    } else if (!ur_invalid) {
        // Solo UR válida: usar su fila Y, extrapolar X
        *iy_tl = *iy_tr;
        if (!lr_invalid) {
            int width = abs(*ix_br - *ix_tr);
            *ix_tl = (*ix_tr > width) ? (*ix_tr - width) : 0;
        }
        inferred++;
    }
}
```

### 2. **Validación Post-Inferencia**

Agregar verificación de que las coordenadas inferidas sean razonables:

```c
// Al final de infer_missing_corners()
if (inferred != (4 - valid_count)) {
    LOG_WARN("Algunas esquinas no pudieron ser inferidas (esperadas: %d, inferidas: %d)", 
             4 - valid_count, inferred);
}

// Verificar que las coordenadas inferidas estén en rango razonable
if (*ix_tl < 0) *ix_tl = 0;
if (*iy_tl < 0) *iy_tl = 0;
// ... repetir para todas las esquinas
```

### 3. **Mejorar el Logging**

Diferenciar entre inferencia exacta vs. aproximada:

```c
LOG_INFO("  UL inferida desde LL y UR (EXACTA - diagonal): (%d, %d)", *ix_tl, *iy_tl);
LOG_INFO("  UL inferida desde LL y LR (APROXIMADA - extrapolación): (%d, %d)", *ix_tl, *iy_tl);
```

## Estrategia de Recorte Mejorada: Propuesta Alternativa

### Opción A: Recorte Conservador (actual)

```
Ventajas:
+ Simple de implementar
+ No pierde datos válidos
+ Funciona bien cuando pocas esquinas son inválidas

Desventajas:
- Puede incluir áreas con huecos en las esquinas
- Requiere post-procesamiento para rellenar huecos
```

### Opción B: Recorte por Máscara de Disco

En lugar de inferir esquinas, aplicar una **máscara del disco visible** después de reproyectar:

```c
// Después de reproyectar
for (cada pixel en la imagen reproyectada) {
    // Verificar si la coordenada geográfica cae dentro del disco
    if (!is_coord_inside_disk(lat, lon, navla, navlo)) {
        pixel = TRANSPARENTE;
    }
}
```

```
Ventajas:
+ Resultado visualmente perfecto (sin áreas inválidas)
+ No necesita inferir esquinas
+ Funciona para cualquier dominio

Desventajas:
- Requiere verificación pixel por pixel (más lento)
- Necesita soporte para transparencia en PNG
```

### Opción C: Recorte Adaptativo con Margen

Calcular el bounding box **solo con píxeles válidos**, ignorando las esquinas inferidas:

```c
// En lugar de inferir esquinas faltantes, usar solo las válidas
int min_ix = INT_MAX, max_ix = INT_MIN;
int min_iy = INT_MAX, max_iy = INT_MIN;

if (!ul_invalid) { update_bounds(ix_tl, iy_tl, &min_ix, &max_ix, &min_iy, &max_iy); }
if (!ur_invalid) { update_bounds(ix_tr, iy_tr, &min_ix, &max_ix, &min_iy, &max_iy); }
if (!ll_invalid) { update_bounds(ix_bl, iy_bl, &min_ix, &max_ix, &min_iy, &max_iy); }
if (!lr_invalid) { update_bounds(ix_br, iy_br, &min_ix, &max_ix, &min_iy, &max_iy); }
```

```
Ventajas:
+ Solo incluye áreas con datos válidos garantizados
+ Más simple que inferir esquinas

Desventajas:
- Puede perder partes del dominio solicitado
- El área recortada puede ser más pequeña de lo esperado
```

## Recomendación Final

**Mantener la estrategia actual (Opción A) con las mejoras propuestas:**

1. ✅ La función `infer_missing_corners()` ya está bien implementada
2. ✅ Mejorar el logging para diferenciar casos exactos vs aproximados
3. ✅ Agregar validación post-inferencia
4. 🆕 **Considerar agregar una opción `--clip-mode`**:
   - `--clip-mode=infer` (default): Usa inferencia de esquinas
   - `--clip-mode=strict`: Solo usa esquinas válidas (Opción C)
   - `--clip-mode=mask`: Aplica máscara de disco (Opción B)

## Testing Sugerido

Probar con dominios extremos:

```bash
# Caso 1: Dominio muy amplio (esquinas norte fuera del disco)
./hpsatviews rgb -r --clip -150 60 -50 10 archivo.nc

# Caso 2: Dominio en el borde del disco
./hpsatviews rgb -r --clip -140 50 -100 30 archivo.nc

# Caso 3: Dominio pequeño totalmente dentro del disco
./hpsatviews rgb -r --clip -110 30 -100 20 archivo.nc
```

Verificar:
- ✓ No hay crashes
- ✓ Las esquinas inferidas son razonables
- ✓ La imagen resultante no tiene huecos grandes
- ✓ Los logs indican claramente qué esquinas fueron inferidas

## Conclusión

La estrategia actual de inferencia de esquinas es **correcta y funcional**. Las mejoras sugeridas son principalmente:
- Optimizaciones de código
- Mejor logging y diagnóstico
- Validaciones adicionales

**No se requieren cambios estructurales** a menos que se encuentren casos específicos donde la inferencia falla.
