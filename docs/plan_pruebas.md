# Plan de pruebas de funcionalidad de hpsv

## Preparación

Ejecutar el script `download_sample.sh` para poblar el directorio `sample_data/` con las imágenes NetCDF. Los comandos a continuación usan los archivos disponibles en `sample_data/` tras la descarga. El archivo ancla de referencia es la banda 13 (`C13`) del timestamp `s20242201301171`, que permite al programa inferir automáticamente las demás bandas del mismo instante.

## Plan de Pruebas de Validación

**Prueba 1: Comandos base y renderizado de un solo canal**

* **Objetivo:** Verificar que el motor lee correctamente el archivo ancla y genera las salidas estándar por defecto.
* **Ejecución:**
`hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -v`
`hpsv pseudocolor sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -v`
* 
**Validación:** Se deben generar archivos PNG en escala de grises y con una paleta de color, respectivamente.


**Prueba 2: Composición Multicanal (True Color)**

* 
**Objetivo:** Confirmar que la función de compuestos multicanal opera adecuadamente.


* **Ejecución:**
`hpsv rgb sample_data/OR_ABI-L2-CMIPC-M6C01_G16_s20242201301171_e20242201303543_c20242201304004.nc --mode truecolor -v`
* 
**Validación:** El programa debe ubicar las bandas complementarias a partir del ancla y ensamblar una imagen RGB a color.


**Prueba 3: Patrones de salida y Metadatos**

* 
**Objetivo:** Probar el parseo de patrones dinámicos en los nombres de salida  y la generación del sidecar JSON.


* **Ejecución:**
`hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -t -j -o "{SAT}_prueba_{CH}_{TS}.tif" -v`
* 
**Validación:** Debe existir un archivo GeoTIFF cuyo nombre contenga los identificadores del satélite, canal y timestamp sustituidos correctamente. Junto a él, debe aparecer un archivo `.json` con los metadatos de la imagen.



**Prueba 4: Geometría, Reproyección y Recorte**

* **Objetivo:** Evaluar las transformaciones espaciales del programa.
* **Ejecución:**
`hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc -c conus -G -B  -v`
* 
**Validación:** El parámetro `-c` debe efectuar el recorte por clave. Al incluir la bandera `-B` , debes obtener dos imágenes: una en la proyección nativa y otra re-proyectada a Lat/Lon con el sufijo `_geo` en el nombre.



**Prueba 5: Procesamiento y Ajuste Visual**

* **Objetivo:** Asegurar que los filtros integrados modifican la imagen sin corromper la memoria ni la salida.
* **Ejecución:**
`hpsv gray sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc --clahe -s -2 -g 1.5 -v`
* 
**Validación:** La imagen resultante debe tener una resolución menor debido al factor de escala negativo. Visualmente, debe aplicar una ecualización de histograma adaptativa sin lucir sobresaturada y reflejar la corrección gamma.



**Prueba 6: Motor de Álgebra de Bandas**

* **Objetivo:** Estresar el evaluador de expresiones matemáticas. El script descarga bandas infrarrojas ideales (C11, C13, C14, C15) para probar esto.
* **Ejecución:**
`hpsv pseudocolor sample_data/OR_ABI-L2-CMIPC-M6C13_G16_s20242201301171_e20242201303555_c20242201304066.nc --expr "C13-C15" --minmax "-5,5" -v`
* 
**Validación:** El programa debe calcular correctamente la diferencia lineal entre los términos C13 y C15 y ajustar el contraste de la imagen basándose en el rango opcional indicado.
