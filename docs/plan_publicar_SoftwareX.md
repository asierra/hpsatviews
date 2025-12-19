# Plan de Conformidad SoftwareX y Datos de Prueba

**Objetivo:** Preparar el repositorio `hpsatviews` para su publicación académica (SoftwareX), asegurando que los revisores puedan descargar datos de prueba y ejecutar una demostración funcional sin configurar entornos complejos.

**Estrategia:** Implementar un "Kit de Reproducción" que use datos del dominio CONUS (Continental US) del satélite GOES-19, optimizando el tamaño de descarga y manteniendo la validación técnica.

---

## 📂 Fase 1: Reestructuración del Repositorio

Organizar el proyecto para separar código fuente, datos de prueba y scripts de validación.

- [ ] **1.1. Crear estructura de carpetas estándar**
    ```text
    hpsatviews/
    ├── src/                 # Código fuente existente
    ├── include/             # Headers existentes
    ├── sample_data/         # (NUEVO) Destino de descargas .nc (Ignorado en git)
    │   └── .gitignore       # Contenido: "*" excepto ".gitignore"
    ├── reproduction/        # (NUEVO) Scripts para revisores y CI/CD
    │   ├── expected_output/ # Imágenes PNG de referencia (pequeñas)
    │   └── .gitignore       # Ignorar archivos generados (*.tif, *.png)
    └── codemeta.json        # (NUEVO) Metadatos de citación estándar
    ```

- [ ] **1.2. Configurar `.gitignore` global**
    Asegurar que nunca se suban archivos NetCDF (*.nc) ni GeoTIFF grandes al repositorio principal.

---

## 🛰️ Fase 2: Definición del "Golden Set" (Datos de Prueba)

Usaremos una escena **CONUS** específica que contenga transición día/noche o un momento de día claro para validar todos los algoritmos.

**Selección:** GOES-19 (East) - Sector CONUS
**Fecha propuesta:** Día Juliano 280 de 2025 (Ejemplo representativo)
**Hora:** 18:01 UTC (Mediodía local, ideal para VIS y NIR)

**Canales Requeridos (Total ~150 MB vs 3 GB de Full Disk):**
1.  **C01 (Blue), C02 (Red), C03 (Veggie):** Para validar `truecolor` y `sharpening`.
2.  **C13 (Clean IR):** Para validar `night`, máscaras y temperatura base.
3.  **C11, C14, C15:** Para validar algoritmo de ceniza volcánica (`ash`).
4.  **C08, C10, C12:** Para validar masas de aire (`airmass`).

---

## 📜 Fase 3: Scripts de Automatización

Estos scripts vivirán en la carpeta `reproduction/`.

- [ ] **3.1. Script de Descarga (`download_sample.sh`)**
    Este script debe descargar los archivos desde el bucket público de NOAA en Amazon S3.

    ```bash
    #!/bin/bash
    # reproduction/download_sample.sh
    # Descarga datos CONUS de GOES-19 desde NOAA S3
    
    DATA_DIR="../sample_data"
    mkdir -p "$DATA_DIR"
    
    # Base URL para el día 280, hora 18 (Ejemplo estructura NOAA)
    # Nota: Ajustar nombres exactos de archivos según disponibilidad real en S3
    BASE_URL="[https://noaa-goes19.s3.amazonaws.com/ABI-L2-CMIPC/2025/280/18](https://noaa-goes19.s3.amazonaws.com/ABI-L2-CMIPC/2025/280/18)"
    
    echo "--- Iniciando descarga del Golden Set (CONUS) ---"
    
    # Función helper
    download_channel() {
        CH=$1
        # Patrón de archivo (wildcard simulado, en producción usar nombre exacto o llistar bucket)
        FILE="OR_ABI-L2-CMIPC-M6C${CH}_G19_s20252801801172_e20252801803545_c20252801804018.nc"
        
        echo "Descargando Canal $CH..."
        curl -f -o "$DATA_DIR/$FILE" "$BASE_URL/$FILE" || echo "Error descargando C$CH"
    }

    # 1. Canales Visibles (True Color)
    download_channel "01"
    download_channel "02"
    download_channel "03"

    # 2. Canales IR (Ash / Night)
    download_channel "11"
    download_channel "13"
    download_channel "14"
    download_channel "15"

    echo "--- Descarga completada en $DATA_DIR ---"
    ```

- [ ] **3.2. Script de Demostración (`run_demo.sh`)**
    Script que compila y ejecuta los casos de uso principales.

    ```bash
    #!/bin/bash
    # reproduction/run_demo.sh
    
    # 1. Compilación limpia
    echo "[1/3] Compilando proyecto..."
    cd ..
    make clean > /dev/null
    make
    if [ $? -ne 0 ]; then
        echo "❌ Error de compilación."
        exit 1
    fi
    
    # 2. Ejecutar True Color
    echo "[2/3] Generando True Color..."
    ./hpsatviews rgb \
        --mode truecolor \
        --input ./sample_data \
        --output ./reproduction/demo_truecolor.tif \
        --verbose
    
    # 3. Ejecutar Ash (Ceniza)
    echo "[3/3] Generando Producto de Ceniza..."
    ./hpsatviews rgb \
        --mode ash \
        --input ./sample_data \
        --output ./reproduction/demo_ash.tif
        
    echo "✅ Demostración finalizada. Ver resultados en 'reproduction/'"
    ```

---

## 📝 Fase 4: Documentación para Revisores

- [ ] **4.1. Crear `reproduction/README.md`**
    Instrucciones paso a paso:
    1. Requisitos (`libnetcdf-dev`, `gcc`, `make`).
    2. Ejecutar `bash download_sample.sh`.
    3. Ejecutar `bash run_demo.sh`.
    4. Comparar `demo_truecolor.tif` con `expected_output/ref_truecolor.png`.

- [ ] **4.2. Generar `codemeta.json`**
    Archivo estándar JSON-LD que describe el software (autores, afiliación LANOT, licencia). Herramienta recomendada: *CodeMeta generator*.

---

## ✅ Checklist de Entrega

- [ ] Estructura de directorios creada.
- [ ] Datos de prueba descargados y verificados localmente.
- [ ] Scripts de reproducción funcionan en un entorno limpio (ej. un contenedor Docker o VM nueva).
- [ ] Documentación de reproducción escrita.
