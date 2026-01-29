# Plan de Refactorización: Corrección de Rayleigh en hpsatviews

Este documento detalla los cambios necesarios para alinear la física de hpsatviews con geo2grid, eliminando errores geométricos y de escala.

---

## 1. Archivo: reader_nc.c
**Función:** `compute_satellite_view_angles` (Línea ~538)
**Acción:** Invertir la dirección del vector de visión del satélite.

### Cambio:
```c
// Buscar líneas 538-540:
double dx = x_sat - x_pixel; // Antes: x_pixel - x_sat
double dy = y_sat - y_pixel; // Antes: y_pixel - y_sat
double dz = z_sat - z_pixel; // Antes: z_pixel - z_sat
```

**Razón:** Para que el azimut (vaa) sea físicamente correcto (del observador al satélite), el vector debe apuntar hacia el satélite. Esto corrige el ángulo de azimut relativo (raa) que se usa en las LUTs.

## 2. Archivo: rayleigh.c

**Función:** luts_rayleigh_correction (Línea ~147) Acción: Eliminar la multiplicación redundante por tau.

### Cambio:
```c
// Cambiar línea 147:
float r_corr = get_rayleigh_value(&lut, theta_s, nav->vza.data_in[i], nav->raa.data_in[i]);
// Nota: Se elimina el "* tau" final.
```

**Razón:** Las LUTs de pyspectral ya devuelven la reflectancia de camino final. Al multiplicar por tau (0.235), estabas subestimando la corrección en un ~75%.

## 3. Archivo: rayleigh.h

Líneas: ~17 y ~21 Acción: Revertir los valores de Tau a su base física real.

### Cambio:
```c
// Cambiar línea 17:
#define RAYLEIGH_TAU_BLUE 0.167f // Valor físico estándar (Bucholtz) para 0.47um
```

**Razón:** Ya no se requieren valores "inflados" (0.235) para compensar el error de la doble multiplicación. Este cambio es opcional pero recomendado para mantener la coherencia técnica.


### 4. Archivo: rgb.c

Función: `apply_postprocessing` (Línea ~250) Acción: Ajustar el llamado a la función de corrección.

### Cambio:
```c
// Cambiar el llamado a la corrección del canal azul:
luts_rayleigh_correction(&ctx->comp_b, &nav, 1, 1.0f);
```

Razón: Al pasar 1.0f como escala, nos aseguramos de que el valor extraído de la LUT se aplique íntegramente a la imagen.

## 5. Verificación de éxito

1. Compilar con make.

2. Procesar una imagen GOES-16 Full Disk.

3. El Canal 1 (Azul) no debe presentar "niebla" azul sobre tierra.

4. La reflectancia en océanos limpios debe situarse entre 0.02 y 0.05.

### Nota sobre la implementación
Al invertir el vector en `reader_nc.c`, asegúrate de verificar la línea donde calculas `cos_vza`. El producto punto con la normal local debe ser coherente:
* Si el vector apunta del **Píxel al Satélite**, el `cos_vza` debe ser positivo.
* En tu código original tenías un signo negativo (`-(dx * nx + ...)`). Con el vector invertido, es probable que debas quitar ese signo negativo para mantener el ángulo cenital en el rango $[0, 90^\circ]$.
