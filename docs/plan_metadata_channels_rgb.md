# Plan: Registrar todos los canales RGB en metadatos

## Problema

En el modo `rgb`, `metadata_from_nc()` solo se llama una vez (src/rgb.c ~L872)
con el canal de referencia. El array `channels[]` del JSON queda con una sola
entrada, ocultando el resto de bandas usadas en el compuesto.

`metadata_from_nc()` ya acumula correctamente (incrementa `channel_count`),
así que el mecanismo está listo — solo falta llamarla para cada canal cargado.

---

## Solución (modos no-custom)

Reemplazar la llamada única por un bucle en orden numérico ascendente:

```c
// src/rgb.c ~L872

// 1. Canal de referencia primero (fija satellite, sector, timestamp)
metadata_from_nc(meta, &ctx.channels[ctx.ref_channel_idx]);

// 2. Resto de canales cargados, en orden numérico
for (int i = 1; i <= 16; i++) {
    if (i == ctx.ref_channel_idx) continue;
    if (ctx.channels[i].band_id == 0) continue;  // no cargado
    metadata_from_nc(meta, &ctx.channels[i]);
}
```

**Verificación pendiente:** comprobar si `metadata_from_nc` sobreescribe
`satellite`/`sector`/`timestamp` en cada llamada. Si es así, agregar un guard
en src/metadata.c:

```c
if (ctx->channel_count == 0) {
    // fijar satellite, sector, timestamp
}
// siempre agregar ChannelInfo
```

**Archivo a modificar:** solo `src/rgb.c` (~L872)

**Verificación:**
- `ash` → 4 entradas: C11, C13, C14, C15
- `truecolor` → 3 entradas: C01, C02, C03
- `night` → 1 entrada: C13 (sin regresión)

---

## Caso pendiente: modo `custom`

### Contexto

Con `--mode custom`, los canales se determinan parseando `--expr`:

```bash
hpsv rgb --mode custom --expr "C13-C14; C13; -1.0*C15+300" archivo.nc
```

Cada `DataNC` en `ctx.channels[cn]` contiene los valores **crudos** de
radianza/reflectancia del archivo NetCDF. Los rangos `fdata.fmin`/`fdata.fmax`
reflejan la escala original del canal, **no el resultado de la expresión**.

Los rangos calculados post-composición sí están disponibles en `RgbContext`
como `ctx.min_r`, `ctx.max_r`, `ctx.min_g`, `ctx.max_g`, `ctx.min_b`, `ctx.max_b`.

### Opciones

| | **A — Canales crudos** | **B — Solo expresión** | **C — Componentes R/G/B** |
|---|---|---|---|
| **Qué registra en `channels[]`** | Una entrada por canal único (C13, C14, C15…) con min/max crudos | Nada | Tres entradas (`R: C13-C14`, `G: C13`, `B: -1.0*C15+300`) con min/max calculados |
| **Expresión en `enhancements`** | Sí, como `"expr"` | Sí, como `"expr"` | Sí, como `"expr"` |
| **Ventaja** | Simple, consistente con modos fijos | Evita rangos engañosos | Documenta exactamente lo que se visualizó |
| **Desventaja** | min/max no corresponden al compuesto final | No documenta qué bandas se usaron | Más complejo; requiere acceso a `ctx.min_r/max_r` post-`composer_func()` |
| **Complejidad** | Mínima | Mínima | Media |
| **Segunda iteración** | No | No | Recomendado para después |

### Recomendación tentativa

**A o B** en esta iteración. **C** como mejora posterior.

---

## Estado

- [ ] Decidir tratamiento para modo `custom` (tabla anterior)
- [ ] Verificar guard en `metadata_from_nc` (sobreescritura de satellite/sector/timestamp)
- [ ] Implementar bucle en `src/rgb.c`
- [ ] Pruebas con `ash`, `truecolor`, `night`, `custom`
