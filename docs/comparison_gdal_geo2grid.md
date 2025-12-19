# Comparación técnica: HPSATVIEWS vs GDAL vs geo2grid

Este documento presenta una comparación técnica y conceptual entre 
**HPSATVIEWS**, **GDAL** y **geo2grid**, enfocada específicamente en 
flujos de trabajo para la generación de **vistas y productos visuales** 
a partir de datos satelitales geoestacionarios.

El objetivo no es evaluar herramientas GIS generalistas ni plataformas 
de análisis físico completo, sino contrastar **filosofía de diseño, 
modelo operativo y eficiencia** en contextos de visualización 
satelital.

---

## 1. Enfoque conceptual

### HPSATVIEWS

HPSATVIEWS está diseñado exclusivamente para operar en el dominio de las **vistas** y **productos visuales**. Parte de datos satelitales calibrados (L1b/L2), pero no expone ni manipula directamente campos físicos numéricos. Su objetivo es la generación rápida, reproducible y geométricamente consistente de representaciones visuales orientadas a la interpretación humana.

La herramienta asume un flujo operativo simple: un **archivo ancla** identifica la escena y el instante, y a partir de él se infieren automáticamente las bandas necesarias.

### GDAL

GDAL es una biblioteca GIS de propósito general, orientada a la transformación, reproyección y análisis de datos geoespaciales arbitrarios. Su diseño prioriza flexibilidad y cobertura de formatos, no simplicidad operativa.

En el contexto de datos satelitales geoestacionarios, GDAL trata cada archivo como una entidad independiente, requiriendo pasos explícitos para reproyección, recorte, escalamiento y composición visual.

### geo2grid

geo2grid es una herramienta desarrollada principalmente para el procesamiento y visualización de datos GOES y otros sensores meteorológicos. Está orientada a la generación de productos específicos y sigue flujos de trabajo predefinidos.

Si bien produce resultados científicamente válidos, su arquitectura privilegia la configuración declarativa y el procesamiento por lotes, lo que puede resultar menos flexible para exploración rápida o integración en pipelines ligeros.

---

## 2. Modelo operativo

| Aspecto              | HPSATVIEWS                      | GDAL                           | geo2grid                  |
| -------------------- | ------------------------------- | ------------------------------ | ------------------------- |
| Unidad de trabajo    | Archivo ancla                   | Archivo individual             | Escena configurada        |
| Curva de aprendizaje | Baja                            | Alta                           | Media                     |
| Flujo típico         | Un comando → una vista/producto | Múltiples comandos encadenados | Configuración + ejecución |
| Orientación          | Visual                          | GIS generalista                | Operacional meteorológico |

---

## 3. Reproyección y geolocalización

### HPSATVIEWS

Implementa reproyección directa píxel a píxel desde proyección geoestacionaria a malla lat/lon uniforme (WGS84), optimizada para rendimiento. Incluye relleno inteligente de huecos y muestreo denso de bordes para asegurar continuidad espacial.

### GDAL

Utiliza reproyección basada en transformaciones generales entre sistemas de referencia. Este enfoque es robusto y genérico, pero computacionalmente costoso para escenas geoestacionarias completas y flujos de visualización en tiempo casi real.

### geo2grid

Emplea modelos geométricos específicos por sensor y productos predefinidos. La reproyección es precisa, pero está fuertemente acoplada a configuraciones y plantillas específicas.

---

## 4. Construcción de vistas y productos

### HPSATVIEWS

* Jerarquía explícita: Gray → Pseudocolor → RGB → Producto
* Composición visual como operación de primer nivel
* Nombres de salida determinísticos y trazables

### GDAL

* La visualización es un resultado indirecto de operaciones GIS
* No existe un concepto nativo de “producto visual”
* La composición RGB requiere múltiples pasos explícitos

### geo2grid

* Productos visuales predefinidos
* Menor control fino sobre combinaciones no estándar
* Convenciones rígidas de salida

---

## 5. Rendimiento y escalabilidad

### HPSATVIEWS

* Implementado en C (C11) con paralelización OpenMP
* Diseñado para minimizar lecturas redundantes y copias intermedias
* Adecuado para procesamiento interactivo y por lotes

### GDAL

* Rendimiento dependiente del driver y del flujo de comandos
* Optimizado para flexibilidad, no para visualización de alta frecuencia

### geo2grid

* Buen rendimiento en flujos soportados
* Menor flexibilidad fuera de productos previstos

---

## 6. Cuándo usar cada herramienta

* **HPSATVIEWS**: cuando el objetivo principal es la generación rápida y consistente de vistas y productos visuales para interpretación humana.
* **GDAL**: cuando se requiere análisis GIS general, transformación de formatos o integración con sistemas geoespaciales complejos.
* **geo2grid**: cuando se siguen flujos operacionales estándar y productos meteorológicos predefinidos.

---

## 7. Conclusión

HPSATVIEWS no compite directamente con GDAL ni con geo2grid en términos de alcance funcional. Su fortaleza reside en un **enfoque deliberadamente restringido**, optimizado para visualización satelital, que reduce complejidad operativa y maximiza rendimiento y reproducibilidad en un dominio bien definido.
