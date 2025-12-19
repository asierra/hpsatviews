# Plan Maestro de Publicación: hpsatviews en SoftwareX

**Objetivo:** Lograr la aceptación del artículo en *SoftwareX* (Elsevier) demostrando que `hpsatviews` es una herramienta de alto rendimiento, reproducible y científicamente relevante.

**Estado Actual:**
- [x] Código fuente actualizado (`image.c`, `image.h`) con CLAHE y OpenMP.
- [x] Documentación (`README.md`) alineada con las funcionalidades reales.
- [ ] Repositorio público "Clean Slate" creado.
- [ ] DOI generado.
- [ ] Manuscrito redactado.

---

## 🏗️ Fase 1: Higiene del Repositorio (Clean Slate)

El repositorio debe verse profesional y libre de archivos basura.

- [ ] **1.1. Crear nuevo repositorio público**
    - Nombre sugerido: `hpsatviews` (si el anterior se renombra/elimina) o `hpsatviews-public`.
    - **NO** importar historial de commits antiguos "sucios". Hacer un *fresh init*.

- [ ] **1.2. Estructura de Archivos Final**
    Asegurar que la raíz contenga estrictamente:
    ```text
    ├── src/                 # (Opcional, si decides mover .c/.h aquí, o dejarlos en raíz)
    ├── .gitignore           # Vital para no subir binarios/logs
    ├── CITATION.cff         # Metadatos académicos (YAML)
    ├── LICENSE              # GPL v3 completo
    ├── Makefile             # Probado en Linux limpio
    ├── README.md            # Con badges y documentación completa
    ├── image.c              # Con CLAHE implementado
    ├── image.h              # Con declaración de CLAHE
    └── (otros .c/.h)
    ```

- [ ] **1.3. Archivo `.gitignore` Robusto**
    ```gitignore
    # Compilados
    *.o
    *.a
    hpsatviews
    
    # Datos y Logs
    *.nc
    *.tif
    *.png
    *.log
    
    # Excepciones para documentación/demo
    !sample_data/
    !assets/
    ```

- [ ] **1.4. Archivo `CITATION.cff`**
    Crear este archivo en la raíz para garantizar citas correctas.
    *(Ver contenido generado en la conversación previa)*.

---

## 🛰️ Fase 2: Kit de Reproducibilidad (Datos y Demo)

Los revisores deben poder ejecutar el código en < 5 minutos.

- [ ] **2.1. Carpeta `sample_data/`**
    - No subir GBs. Incluir un script de descarga o un archivo NetCDF pequeño recortado.
    - **Script recomendado:** `download_sample.sh` (descarga un archivo CONUS del bucket S3 de NOAA).

- [ ] **2.2. Script de Demostración (`run_demo.sh`)**
    Script "Botonazo" para el revisor:
    ```bash
    #!/bin/bash
    set -e
    echo "1. Compilando..."
    make clean && make
    
    echo "2. Descargando datos de prueba..."
    ./download_sample.sh
    
    echo "3. Ejecutando CLAHE demo..."
    ./hpsatviews rgb --mode truecolor --clahe "8,8,4.0" -o demo_clahe.png sample_data/test_file.nc
    
    echo "✅ Éxito. Revisa demo_clahe.png"
    ```

---

## 🏷️ Fase 3: Identificador Persistente (DOI)

SoftwareX **exige** un DOI del código (versión específica).

- [ ] **3.1. Vincular Zenodo**
    - Ir a [Zenodo.org](https://zenodo.org) -> Log in with GitHub.
    - Activar el switch para el repositorio `hpsatviews`.

- [ ] **3.2. Crear Release v1.0.0**
    - En GitHub: Releases -> "Create a new release".
    - Tag: `v1.0.0`.
    - Título: "Initial Release - High Performance Satellite Views".
    - **Acción:** Esto disparará a Zenodo para generar el DOI.

- [ ] **3.3. Verificar DOI**
    - Copiar el DOI de Zenodo y el "badge" Markdown.
    - Pegar el badge en el `README.md` y hacer un commit `v1.0.1` (opcional, para que se vea bonito).

---

## 📝 Fase 4: Redacción del Manuscrito

Usar la plantilla LaTeX de Elsevier. Extensión: 3-6 páginas.

- [ ] **4.1. Tabla de Metadatos (Obligatoria)**
    Llenar la tabla "Code Metadata" con:
    - **Current code version:** v1.0.0
    - **Permanent link:** (URL de GitHub)
    - **Legal Software License:** GPL-3.0
    - **Code versioning system:** git
    - **Software code languages:** C11, OpenMP

- [ ] **4.2. Abstract**
    Usar el texto redactado previamente, enfocándose en: "30-120x más rápido que Python/GDAL".

- [ ] **4.3. Motivation and Significance**
    - Problema: Latencia en Python para datos GOES de alta frecuencia.
    - Solución: C11 + OpenMP + Gestión de memoria manual.
    - Impacto: Permite operación en tiempo real en hardware modesto (LANOT/Universidades).

- [ ] **4.4. Software Description**
    - Describir la arquitectura (`ImageData` struct).
    - Describir **CLAHE**: Explicar la implementación paralela y la interpolación bilineal.
    - Describir **Rayleigh**: Mencionar las LUTs embebidas para velocidad.

- [ ] **4.5. Illustrative Examples**
    - Figura 1: Comparativa Visual (Original vs CLAHE).
    - Figura 2: Gráfica de Barras (Tiempo de ejecución: hpsatviews vs gdal_translate vs satpy).

---

## ✅ Fase 5: Lista de Verificación de Envío

Antes de subir el PDF a Editorial Manager:

- [ ] **Consistencia:** ¿El código en GitHub tiene la función `image_apply_clahe`? (Crucial).
- [ ] **Reproducibilidad:** ¿Alguien externo probó el `run_demo.sh`?
- [ ] **Licencia:** ¿Está el archivo `LICENSE` en el repo?
- [ ] **DOI:** ¿El enlace al DOI en el paper funciona?
