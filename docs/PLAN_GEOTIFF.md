# Plan de Implementación para Escritura de GeoTIFF

Documentación de la implementación completada para generar imágenes GeoTIFF georreferenciadas como alternativa a PNG.

---

## 📋 Contexto

### Tipos de Imágenes Generadas
- **RGB** (`bpp=3`): Truecolor, Ash, Nocturnal, composites
- **Indexadas** (`bpp=1` + palette): Pseudocolor con paletas CPT
- **Grayscale** (`bpp=1`): Singlegray

### Proyecciones Soportadas
- **PROJ_GEOS**: Geoestacionaria nativa (GOES-R Fixed Grid)
- **PROJ_LATLON**: Geográfica ecuirectangular (tras reproyección con `-r`)

---

## ✅ Implementación Completada

### 1. Integración de GDAL

**Archivo**: `Makefile`

Agregado soporte GDAL:
```makefile
CFLAGS=-g -I. -Wall -std=c11 -fopenmp $(shell gdal-config --cflags)
LDFLAGS=-lm -lnetcdf -lpng -fopenmp $(shell gdal-config --libs)
```

Agregado `writer_geotiff.h` a DEPS y `writer_geotiff.o` a OBJS.

---

### 2. Extensión de DataNC con Metadatos de Proyección

**Archivo**: `datanc.h`

Agregada estructura para almacenar georreferenciación:

```c
typedef struct {
  // ... campos existentes ...
  
  double geotransform[6];   // [TopLeftX, PixelW, RotX, TopLeftY, RotY, PixelH]
  ProjectionCode proj_code; // PROJ_GEOS o PROJ_LATLON
  
  struct {
      double sat_height;     // perspective_point_height
      double semi_major;     // semi_major_axis
      double semi_minor;     // semi_minor_axis
      double lon_origin;     // longitude_of_projection_origin
      double inv_flat;       // inverse_flattening
      bool valid;
  } proj_info;
} DataNC;
```

**Propósito**: Los metadatos viajan con los datos, eliminando relecturas del NetCDF.

---

### 3. Lectura de Metadatos en reader_nc.c

**Líneas 162-235**: Al leer el NetCDF, se calcula y guarda:

1. **Parámetros de proyección GOES** desde `goes_imager_projection`
2. **GeoTransform** desde arrays `x[]` y `y[]` (en radianes)
3. **Detección automática** de `proj_code`

**Cálculo del GeoTransform**:
```c
// x[] y y[] están en radianes desde subsatellite point
double x_scale = (x_coords[x_len - 1] - x_coords[0]) / (x_len - 1);
double y_scale = (y_coords[0] - y_coords[y_len - 1]) / (y_len - 1);

// Ajustar a esquina (pixel-as-area convention)
datanc->geotransform[0] = x0_rad - (x_scale / 2.0);
datanc->geotransform[1] = x_scale;
datanc->geotransform[3] = y0_rad - (y_scale / 2.0);
datanc->geotransform[5] = y_scale;
```

---

### 4. Módulo writer_geotiff.c

**Función principal**: `write_image_geotiff()`

**Flujo**:
1. Construye WKT desde `meta->proj_info`
2. Copia y convierte `geotransform` (radianes→metros para GEOS)
3. Crea dataset GDAL (GTiff driver)
4. Configura proyección y geotransform
5. Escribe bandas de datos
6. Opcionalmente escribe color table

**Conversión crítica para GEOS**:
```c
if (meta->proj_code == PROJ_GEOS && meta->proj_info.valid) {
    double h = meta->proj_info.sat_height;
    // reader_nc.c guardó en radianes, GDAL espera metros
    gt[0] *= h;  // Origin X
    gt[1] *= h;  // Pixel width
    gt[3] *= h;  // Origin Y
    gt[5] *= h;  // Pixel height
}
```

**WKT para GEOS**:
```c
// Formato PROJ4 (más robusto que WKT completo)
snprintf(wkt, 512,
    "+proj=geos +lon_0=%.6f +h=%.1f +x_0=0 +y_0=0 "
    "+ellps=GRS80 +units=m +no_defs +sweep=x",
    meta->proj_info.lon_origin,
    meta->proj_info.sat_height);
```

---

### 5. Integración en Pipeline

**Archivos**: `processing.c` (~línea 310), `rgb.c` (~líneas 785, 890)

Detección automática por extensión:
```c
char *ext = strrchr(output_filename, '.');
bool is_geotiff = (ext && strcmp(ext, ".tif") == 0);

if (is_geotiff) {
    write_image_geotiff(output_filename, &imout, datanc, palette);
} else {
    write_image(output_filename, &imout, palette);
}
```

---

### 6. Actualización de Reproyección

**Archivo**: `reprojection.c`

Al reproyectar a geográfico, se actualiza automáticamente:
```c
// Cambiar código de proyección
datanc->proj_code = PROJ_LATLON;

// Calcular nuevo geotransform para grid geográfico
datanc->geotransform[0] = navlo->fmin - pixel_width / 2.0;
datanc->geotransform[3] = navla->fmax + pixel_height / 2.0;
datanc->geotransform[1] = pixel_width;
datanc->geotransform[5] = -pixel_height;
```

---

## 🎯 Uso

Usar extensión `.tif` en lugar de `.png`:

```bash
# RGB truecolor geoestacionario
./hpsatviews rgb -m truecolor -o salida.tif archivo.nc

# RGB reproyectado a geográfico
./hpsatviews rgb -m truecolor -r -o salida.tif archivo.nc

# Con clipping
./hpsatviews rgb -m ash --clip -107.23 22.72 -93.84 14.94 -o salida.tif archivo.nc
```

---

## 📊 Arquitectura Final

```
reader_nc.c → Lee NetCDF + calcula geotransform + lee proj_info
              ↓
DataNC      → Almacena: proj_info, geotransform, proj_code
              ↓
processing.c → Detecta .tif → Llama write_image_geotiff()
rgb.c          ↓
writer_geotiff.c → Usa DataNC directamente:
                   - Construye WKT desde proj_info
                   - Convierte geotransform radianes→metros
                   - Escribe GeoTIFF con GDAL
```

**Ventajas**:
- ✅ Sin relectura de NetCDF
- ✅ Metadatos viajan con los datos
- ✅ Conversión automática de coordenadas
- ✅ Compatible con clipping y reproyección

---

## 🧪 Validación

```bash
# Verificar proyección
gdalinfo salida.tif

# Comparar con referencia
python3 valida_geotiff.py salida.tif referencia.tif
```

---

## 📝 Notas Técnicas

### Pixel-as-Area Convention
GDAL usa esquina superior izquierda (no centro del píxel), por eso:
```c
geotransform[0] = x_min - (pixel_width / 2.0);
geotransform[3] = y_max + (pixel_height / 2.0);
```

### Coordenadas Geoestacionarias
- NetCDF GOES: radianes desde subsatellite point
- GDAL `proj=geos`: metros desde subsatellite point
- Conversión: multiplicar por `satellite_height`

### WKT vs PROJ4
Se usa PROJ4 por:
- Más compacto
- Mejor soporte en GDAL moderno
- Parámetro `+sweep=x` crítico para GOES-R

---

## ✅ Estado: COMPLETO

- [x] Integración GDAL en Makefile
- [x] Extensión de DataNC
- [x] Lectura de metadatos en reader_nc.c
- [x] Módulo writer_geotiff.c funcional
- [x] Integración en processing.c y rgb.c
- [x] Soporte reproyección y clipping
- [x] Testing y validación con GOES-19
- [x] Limpieza de código obsoleto

**Fecha**: Diciembre 5, 2025
