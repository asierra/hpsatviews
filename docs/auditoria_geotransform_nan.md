# Auditoría — contaminación de `geotransform[6]` con `-nan`

> Auditoría de `src/reader_nc.c` (y archivos relacionados `datanc.c`, `reprojection.c`,
> `processing.c`, `rgb.c`) buscando fallas silenciosas de NetCDF-C, fugas de alcance
> en OpenMP y rutas de propagación de NaN hacia `geotransform[]`.
> No se modificó código; este documento es solo el reporte de hallazgos.

## Corrección al diagnóstico previo

El análisis anterior afirmó que *"las variables globales de archivo son solo-lectura en
zonas paralelas"*. **Esto es incorrecto.** En `reader_nc.c:311` hay globales **mutables**
de proyección (`hsat, sm_maj, sm_min, lambda_0, H`) que se **escriben** en
`compute_navigation_nc` y se **leen** en `compute_lalo`. No son solo-lectura.
(Ver Hallazgo #3.)

Se verificó además que `load_nc_sf` y `compute_navigation_nc` se invocan en **bucles
secuenciales** (sin `#pragma omp`, en `processing.c:156` y `rgb.c:413`), de modo que la
causa raíz del `-nan` **no es una data race**, sino **lectura de variables locales sin
inicializar cuyo `retval` de NetCDF se ignora**. Eso explica perfectamente la
intermitencia (la pila contiene basura distinta en cada ejecución).

---

## 🔴 Hallazgo #1 — CRÍTICO — Causa raíz del `-nan` en `geotransform[]`

**`src/reader_nc.c:187-198`, `datanc_read_metadata()`**

Tres defectos compuestos:

1. `x_sf, x_ao, y_sf, y_ao, x0, y0` son **locales sin inicializar**.
2. Los 6 `nc_get_*` **ignoran el retval** → si fallan, las locales retienen basura/`-nan`.
3. **Se usan `xid`/`yid` que vienen de `nc_inq_dimid` (línea 144) como si fueran
   *variable ids*.** Atributo/variable y dimensión son espacios de nombres distintos en
   NetCDF; pasar un *dimid* donde se espera un *varid* falla (o lee otra variable) de
   forma dependiente del archivo → **intermitente**.

La basura entra directo a `geotransform[]`, que luego hace que la reproyección descarte
millones de píxeles.

```c
// ORIGINAL (líneas 187-198)
        double x_sf, x_ao, y_sf, y_ao;
        short x0, y0; size_t iz = 0;
        nc_get_att_double(ncid, xid, "scale_factor", &x_sf); nc_get_att_double(ncid, xid, "add_offset", &x_ao);
        nc_get_att_double(ncid, yid, "scale_factor", &y_sf); nc_get_att_double(ncid, yid, "add_offset", &y_ao);
        nc_get_var1_short(ncid, xid, &iz, &x0); nc_get_var1_short(ncid, yid, &iz, &y0);
        datanc->geotransform[0] = ((double)x0 * x_sf + x_ao) - (x_sf / 2.0);
        datanc->geotransform[1] = x_sf;
        datanc->geotransform[2] = 0.0;
        datanc->geotransform[3] = ((double)y0 * y_sf + y_ao) - (y_sf / 2.0);
        datanc->geotransform[4] = 0.0;
        datanc->geotransform[5] = y_sf;
```

```c
// CORREGIDO
        // x/y son VARIABLES de coordenadas: hay que pedir sus variable ids,
        // no reutilizar los dimension ids (xid/yid vienen de nc_inq_dimid).
        int xvar, yvar;
        double x_sf = 1.0, x_ao = 0.0, y_sf = 1.0, y_ao = 0.0;  // defensivo
        short  x0 = 0, y0 = 0;
        size_t iz = 0;
        bool gt_ok =
            nc_inq_varid(ncid, "x", &xvar)                         == NC_NOERR &&
            nc_inq_varid(ncid, "y", &yvar)                         == NC_NOERR &&
            nc_get_att_double(ncid, xvar, "scale_factor", &x_sf)   == NC_NOERR &&
            nc_get_att_double(ncid, xvar, "add_offset",   &x_ao)   == NC_NOERR &&
            nc_get_att_double(ncid, yvar, "scale_factor", &y_sf)   == NC_NOERR &&
            nc_get_att_double(ncid, yvar, "add_offset",   &y_ao)   == NC_NOERR &&
            nc_get_var1_short(ncid, xvar, &iz, &x0)                == NC_NOERR &&
            nc_get_var1_short(ncid, yvar, &iz, &y0)                == NC_NOERR;

        if (gt_ok) {
            datanc->geotransform[0] = ((double)x0 * x_sf + x_ao) - (x_sf / 2.0);
            datanc->geotransform[1] = x_sf;
            datanc->geotransform[2] = 0.0;
            datanc->geotransform[3] = ((double)y0 * y_sf + y_ao) - (y_sf / 2.0);
            datanc->geotransform[4] = 0.0;
            datanc->geotransform[5] = y_sf;
        } else {
            LOG_WARN("Geotransform no legible; se marca proyección inválida.");
            datanc->proj_info.valid = false;   // impide reproyectar con basura
        }
```

> Nota: el `memset(datanc, 0, sizeof(DataNC))` en línea 278 **sí** deja `geotransform` en
> ceros cuando *no* hay proyección (por eso el `for` comentado en 280-282 es inocuo). El
> daño ocurre **solo en la ruta que sobrescribe** `geotransform` con locales corruptas.

---

## 🔴 Hallazgo #2 — CRÍTICO — `-nan` en la malla de navegación (imagen vacía)

**`src/reader_nc.c:375-393`, `compute_navigation_nc()`**

Mismo patrón uninit + retval ignorado. Aquí `x_sf/y_sf/x_ao/y_ao` alimentan
`x = x_vals_raw[i]*x_sf + x_ao` → `sin/cos` → toda la malla lat/lon. Basura ⇒ `NaN` ⇒
**cero píxeles válidos ⇒ imagen plana/vacía**. Además `lo_proj_orig` (línea 375) está sin
inicializar y solo usa `WRN` (advierte pero **continúa con basura** en `lambda_0`).

```c
// ORIGINAL (líneas 375-393, extracto)
    double lo_proj_orig;
    if ((retval = nc_get_att_double(ncid, varid, "longitude_of_projection_origin", &lo_proj_orig)))
        WRN(retval);                       // ⚠ continúa con lo_proj_orig basura
    ...
    double x_sf, y_sf, x_ao, y_ao;         // sin inicializar
    ...
    nc_get_att_double(ncid, xid, "scale_factor", &x_sf);   // retval ignorado
    nc_get_att_double(ncid, xid, "add_offset",  &x_ao);
    nc_get_att_double(ncid, yid, "scale_factor", &y_sf);
    nc_get_att_double(ncid, yid, "add_offset",  &y_ao);
```

```c
// CORREGIDO
    double lo_proj_orig = 0.0;
    if ((retval = nc_get_att_double(ncid, varid, "longitude_of_projection_origin", &lo_proj_orig)))
        ERR(retval);                       // abortar; no navegar con λ0 basura
    ...
    double x_sf = 1.0, y_sf = 1.0, x_ao = 0.0, y_ao = 0.0;   // defensivo
    ...
    // aquí xid/yid SÍ son variable ids (nc_inq_varid en líneas 385-388), bien.
    if ((retval = nc_get_att_double(ncid, xid, "scale_factor", &x_sf))) ERR(retval);
    if ((retval = nc_get_att_double(ncid, xid, "add_offset",  &x_ao))) ERR(retval);
    if ((retval = nc_get_att_double(ncid, yid, "scale_factor", &y_sf))) ERR(retval);
    if ((retval = nc_get_att_double(ncid, yid, "add_offset",  &y_ao))) ERR(retval);
```

---

## 🟢 Hallazgo #3 — DESCARTADO — Globales de proyección (arquitectura aclarada)

**`src/reader_nc.c:311`**

```c
double hsat, sm_maj, sm_min, lambda_0, H;   // estado global mutable
```

Evaluación inicial: *data race latente* si `compute_navigation_nc` se invocara en paralelo.

**Aclaración del autor:** `compute_navigation_nc` se llama **exactamente una vez por
escena**, antes de cargar cualquier canal, y su resultado se reutiliza para todos ellos.
Los globales se escriben una sola vez en fase secuencial y luego son de solo lectura →
**no hay carrera, ni latente ni activa**. El TSan report apuntaba a los Hallazgos #1 y #2,
no a estos globales.

Riesgo real: **NULO** con el modelo de uso actual.

---

## 🟡 Hallazgo #4 — MEDIO — Retval ignorado + locales uninit (no alimentan geotransform)

**`src/reader_nc.c:155-165`**

```c
// ORIGINAL
    double tiempo;                                  // sin init
    nc_get_var_double(ncid, time_varid, &tiempo);   // retval ignorado → timestamp basura
    ...
    int bid; size_t idx = 0;                        // bid sin init
    nc_get_var1_int(ncid, band_varid, &idx, &bid);  // retval ignorado → band_id erróneo → calibración errónea
```

```c
// CORREGIDO
    double tiempo = 0.0;
    if (nc_get_var_double(ncid, time_varid, &tiempo) == NC_NOERR)
        datanc->timestamp = (time_t)(946728000 + (long)tiempo);
    ...
    int bid = 0; size_t idx = 0;
    if (nc_get_var1_int(ncid, band_varid, &idx, &bid) == NC_NOERR)
        datanc->band_id = (uint8_t)bid;
```

Severidad media: un `band_id` corrupto desvía la rama de calibración (Planck vs. kappa0 en
`datanc_unpack_grid`), produciendo radiancias absurdas (no `-nan` en geotransform, pero sí
imagen incorrecta).

---

## 🟢 Hallazgo #5 — BAJO — Retval ignorado pero protegido por `memset`

**`src/reader_nc.c:181-185`** (`proj_info.sat_height`, etc.)

Los `nc_get_att_double` ignoran retval, pero como `datanc` fue puesto a cero con `memset`
(línea 278), un fallo deja `0.0` (degradado, no basura/`-nan`). Conviene verificar
`NC_NOERR` y marcar `proj_info.valid = false` ante fallo, pero no causa el heisenbug. Igual
para los `planck_*`/`kappa0` (líneas 170-175): `cfg` tiene init designado (línea 275) ⇒
campos no mencionados quedan en `0` por C11, no en basura.

---

## Resumen ordenado por severidad

| # | Ubicación | Defecto | Severidad | Causa del heisenbug |
|---|---|---|---|---|
| 1 | `reader_nc.c:187-198` | uninit + retval ignorado + **dimid usado como varid** → geotransform | 🔴 CRÍTICO | **Sí (raíz directa)** |
| 2 | `reader_nc.c:375-393` | uninit + retval ignorado → malla nav NaN | 🔴 CRÍTICO | **Sí (imagen vacía)** |
| 3 | `reader_nc.c:311` | globales de proyección (solo-lectura tras init única) | 🟢 DESCARTADO | No (arquitectura de un solo llamador) |
| 4 | `reader_nc.c:155-165` | uninit + retval ignorado (`tiempo`, `bid`) | 🟡 MEDIO | No (calibración/timestamp) |
| 5 | `reader_nc.c:181-185` | retval ignorado (protegido por memset) | 🟢 BAJO | No |

**Conclusión:** la combinación de los Hallazgos #1 y #2 (locales sin inicializar + `retval`
de NetCDF ignorado, agravado por el uso de *dimension id* como *variable id* en #1) es la
causa raíz del `geotransform` contaminado con `-nan` y de la imagen vacía intermitente. El
Hallazgo #3 es la fuente real del *data race* que reporta TSan y debe corregirse para
eliminar el riesgo a futuro.
