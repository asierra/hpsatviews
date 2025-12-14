#!/bin/bash
# Descarga datos de prueba GOES-16 (CONUS) dinámicamente
# Estrategia: Listar el bucket S3 para una hora específica y bajar el primer match.

DATA_DIR="../sample_data"
mkdir -p "$DATA_DIR"

# Configuración del Bucket (NOAA GOES-16)
# Usamos Día 220 (7 Agosto 2024) a las 18:00 UTC (Día claro en CONUS)
# O 22:00 Z para mostrar parte de la noche.
BUCKET_URL="https://noaa-goes16.s3.amazonaws.com"
PRODUCT_PREFIX="ABI-L2-CMIPC/2024/220/18" # CONUS, Año, Día, Hora

echo "--- Iniciando descarga inteligente (List & Fetch) ---"
echo "Directorio destino: $DATA_DIR"

# 1. Obtener la lista de archivos disponibles para esa hora (Formato XML)
echo "Consultando índice de archivos en NOAA S3..."
FILE_LIST=$(curl -s "${BUCKET_URL}?list-type=2&prefix=${PRODUCT_PREFIX}")

# Función para extraer nombre y descargar
download_channel() {
    CH=$1
    echo "Buscando archivo para Canal $CH..."

    # Buscamos en el XML una key que contenga el canal (ej: M6C01)
    # grep -m 1 obtiene solo el primer resultado (la primera imagen de esa hora)
    FILE_KEY=$(echo "$FILE_LIST" | grep -o "<Key>[^<]*M6C${CH}_[^<]*</Key>" | head -n 1 | sed 's/<\/\?Key>//g')

    if [ -z "$FILE_KEY" ]; then
        echo "❌ Error: No se encontró archivo para el canal $CH en el índice."
        return
    fi

    # El nombre local será limpio (sin rutas)
    LOCAL_NAME=$(basename "$FILE_KEY")
    
    # URL Final
    FULL_URL="${BUCKET_URL}/${FILE_KEY}"

    echo "   ⬇️  Descargando: $LOCAL_NAME"
    curl -f -s -S -o "$DATA_DIR/$LOCAL_NAME" "$FULL_URL"

    if [ $? -eq 0 ]; then
        echo "   ✅ Completo."
    else
        echo "   ❌ Fallo descarga."
    fi
}

# --- Ejecución ---

# 1. Canales Visibles (True Color)
download_channel "01" # Blue
download_channel "02" # Red
download_channel "03" # Veggie

# 2. Canales IR (Ash / Night / Airmass)
download_channel "08" # Water Vapor High
download_channel "10" # Water Vapor Low
download_channel "11" # Cloud Phase
download_channel "12" # Ozone
download_channel "13" # Clean IR (Reference)
download_channel "14" # IR
download_channel "15" # Dirty IR

echo "--- Proceso finalizado ---"
ls -lh $DATA_DIR