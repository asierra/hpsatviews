# Plan de Implementación

## Sistema de Metadatos Científicos (JSON Sidecar)

---

## Objetivo General

Permitir que **hpsatviews** exporte un archivo *sidecar* (`.json`) con información **radiométrica** y **geoespacial** crítica, con el fin de:

* Asegurar la **reproducibilidad científica**.
* Facilitar la **graficación precisa** en herramientas externas (por ejemplo, *mapdrawer*).

---

## Fase 1: Definición de la Estructura de Datos

### Objetivo

Crear el contenedor interno que *viajará* a través del pipeline recolectando información relevante. Ya tenemos estructuras útiles como RgbContext en rgb.c, FilenameGeneratorInfo en filename_utils.c y ArgParser como se usa en processing.c para ejecutar el pipeline. Quizá todas estas estructuras se pueden integrar en un MetadataContext que el pipeline pueda usar para todas estas operaciones: pipeline monocanal (processing), pipeline rgb, generación automática de nombre y generación de metadatos.

### Contenedor de Metadatos (estructura global)

* Debe persistir durante toda la ejecución del programa.
* Actúa como repositorio único de metadatos científicos.

#### Principio de diseño

El contenedor de metadatos debe describir el **producto científico final**, 
no el archivo de entrada ni el formato de salida.

En particular:
* La geometría corresponde a la imagen final (post-recorte y reproyección).
* Las estadísticas radiométricas corresponden a los datos físicos reales,
  antes de cualquier cuantización a 8 bits.
* El procesamiento se describe como un **pipeline ordenado de pasos**,
  no como una sola operación.

### Campos requeridos del contenedor de metadatos

El contenedor se organiza en bloques semánticos estables:

1. file_info  
2. identity  
3. spatial  
4. radiometry  
5. processing

#### 2. identity – Identidad de la escena

* Satélite
* Sensor
* Tipo de producto (`gray`, `pseudocolor`, `rgb`)
* Modo (por ejemplo: `truecolor`, `ash`, `airmass`)
* Canal o lista de canales
* Intervalo temporal de observación (ISO 8601)


#### 3. Geografía

* *Bounding Box* final del producto:

  * Latitud mínima
  * Latitud máxima
  * Longitud mínima
  * Longitud máxima
  * Proyección y geotransform (por ejemplo EPSG:4326)
  * Resolución espacial del producto final
  * Información del recorte aplicado (preset, explícito o disco completo)

Nota: el bounding box y la resolución corresponden siempre
al producto final generado, no al archivo satelital original.


#### 4. radiometry – Información radiométrica

Debe distinguirse explícitamente entre:

* Rango físico teórico de la magnitud
* Estadísticas reales de la escena
* Cuantización aplicada al producto visual

Campos mínimos:

* Magnitud física (ej. reflectance, brightness_temperature)
* Unidad (ej. %, K)
* input_range:
  * valor mínimo físico teórico
  * valor máximo físico teórico
* scene_statistics:
  * mínimo real de la escena (antes del escalado)
  * máximo real de la escena (antes del escalado)
* quantization:
  * tipo (ej. uint8)
  * rango final (ej. 0–255)

#### 5. processing

* Nombre del algoritmo(s) utilizado(s) (ej. `CLAHE`, `Linear`)
* Parámetros clave:

  * Gamma
  * Clip limit
  * Otros relevantes según el algoritmo
  
---

## Fase 2: Interfaz de Usuario (CLI)

### Objetivo

Permitir que el usuario solicite explícitamente la generación del archivo de metadatos.

### Nueva bandera (flag)

* Implementar:

  * `--save-metadata` 
* Esta bandera activa un **booleano** en la configuración global.

### Validación de dependencias

* La bandera **no debe interferir** con los modos de salida actuales.
* La generación de la imagen principal **no debe bloquearse**.

---

## Fase 3: Instrumentación del Pipeline (Hooks)

### Objetivo

Interceptar la información en los puntos críticos del procesamiento, antes de que se pierda precisión.

### Hook de Lectura (*Loader*)

* Al leer el archivo original (`NetCDF` / `HDF`):

  * Satélite
  * Canal
  * *Timestamp*

### Hook de Geometría (*Clipper*)

* Si se aplica un recorte (`--clip`):

  * Calcular y actualizar las coordenadas extremas Lat/Lon del área resultante.
* Si es disco completo:

  * Usar los límites teóricos del satélite.

### Hook de Análisis (*Calculator*)

> **Punto crítico**

* Justo después de leer la matriz de datos flotantes (temperaturas / reflectancias).
* Antes de la conversión a 8-bit (`0–255`).

Acciones:

* Calcular el **mínimo y máximo estadístico real** de la escena.
* Guardarlos en el contenedor de metadatos.

> Estos valores son los que Python utilizará para construir una barra de color científicamente correcta.

### Hook de Algoritmo (*Processor*)

Registrar el pipeline completo de procesamiento como una secuencia ordenada.

Cada paso debe incluir:
* Nombre del paso
* Parámetros relevantes
* (Opcional) descripción o expresión algebraica

---

## Fase 4: Serialización y Salida (Exportador)

### Objetivo

Escribir el archivo físico de metadatos en disco usando un formato estándar.

### Generación del nombre del archivo

* Regla:

  * El nombre debe coincidir con el archivo de salida de la imagen.
  * Solo cambia la extensión.

Ejemplo:

```
output.tif  →  output.json
```

### Escritura del JSON (*Serializer*)

* Implementar una función que:

  * Reciba el contenedor completamente poblado.
  * Escriba el archivo JSON en disco.
  
### Schema JSON

El archivo de metadatos debe seguir un schema estable con los bloques:

* file_info
* identity
* spatial
* radiometry
* processing

Este schema debe documentarse con un ejemplo real
y considerarse parte de la interfaz pública de hpsatviews.

#### Consideraciones

* Usar **punto decimal** para números de punto flotante.
* Mantener consistencia en nombres de campos.

---

## Fase 5: Integración con el Cliente (Python / Mapdrawer)

### Objetivo

Consumir los metadatos generados para crear figuras científicas precisas.

### Adaptación de *mapdrawer*

* Aceptar un argumento opcional de metadatos.
* Alternativamente:

  * Buscar automáticamente un JSON con el mismo nombre base que la imagen.

La reconstrucción de la barra de color debe basarse exclusivamente
en los campos radiométricos del JSON, y no en los valores cuantizados
de la imagen (0–255).

### Barra de color virtual

* Ignorar los valores de píxel (`0–255`) para la leyenda.
* Construir la escala de colores usando:

  * `min_val_input`
  * `max_val_input`

(leídos directamente del JSON).

### Etiquetado automático

* Generar etiquetas dinámicamente usando:

  * `unit`
  * `algorithm`

Ejemplo:

```
Temperature [K] (CLAHE enhanced)
```

---

## Fase 6: Verificación y Testing

### Prueba de consistencia

* Procesar una imagen conocida.
* Verificar que el JSON contenga los valores mínimo y máximo esperados.

### Prueba de recorte

* Ejecutar un recorte:

  ```
  --clip mexico
  ```
* Confirmar que el *Bounding Box* corresponda a México y no al disco completo.

### Prueba de integración

* Ejecutar el flujo completo:

  ```
  hpsatviews → JSON → mapdrawer
  ```
* Validar que la barra de colores generada coincida visualmente con la realidad científica.

---

## Resultado Esperado

Un sistema robusto de metadatos que:

* Preserve información científica crítica.
* Desacople visualización y física.
* Permita reproducibilidad y validación independiente del producto final.

