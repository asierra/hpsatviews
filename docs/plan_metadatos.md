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

Crear el contenedor interno que *viajará* a través del pipeline recolectando información relevante. Tal vez ya lo tenemos con estructuras como RgbContext en rgb.c y su equivalente en processing.c .

### Contenedor de Metadatos (estructura global)

* Debe persistir durante toda la ejecución del programa.
* Actúa como repositorio único de metadatos científicos.

#### Campos requeridos

##### 1. Identidad

* Satélite
* Sensor
* Canal / Banda(s)
* Fecha y hora (formato **ISO 8601**)

##### 2. Física (Radiometría)

* Unidad física (por ejemplo: `Kelvin`, `%`, `W/m²`)
* Valor mínimo real (antes del escalado)
* Valor máximo real (antes del escalado)

##### 3. Geografía

* *Bounding Box* final del producto:

  * Latitud mínima
  * Latitud máxima
  * Longitud mínima
  * Longitud máxima
  * Proyección y geotransform. Todo eso ya lo manejamos.

##### 4. Procesamiento

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

  * `--save-metadata` *(alternativa: `--export-stats`)*
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

* Registrar:

  * Algoritmo de mejora aplicado (`CLAHE`, histograma, lineal, etc.).
  * Parámetros utilizados (gamma, tiles, clip limit, etc.).

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

#### Organización interna del JSON

* `file_info`
* `spatial`
* `radiometry`
* `processing`

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

