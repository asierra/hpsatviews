#!/bin/bash
# Búsqueda de canales hermanos (find_channel_filenames): el ancla tiene que
# cargar los canales de SU escena aunque en el directorio haya otras parecidas.
#
# Hasta 1.2.0 la clave cortaba el sello en la decena de minuto y no miraba
# sector ni satélite, así que en un directorio de mesoescala (una escena por
# minuto) el ancla cargaba sus canales —incluido el propio C01— de cualquiera
# de hasta diez escenas, según el orden de readdir().
#
# Arma un directorio temporal con enlaces a sample_data/ bajo nombres de
# escenas señuelo y revisa en el log qué archivos se cargaron. Todos los
# enlaces apuntan al mismo contenido, así que el criterio es el nombre.
set -e

SRC=$(cd ../sample_data && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

REAL=20242201301171
declare -A SEEN
for f in "$SRC"/OR_ABI-L2-CMIPC-M6C0[123]_G16_s${REAL}_*.nc "$SRC"/OR_ABI-L2-CMIPC-M6C13_G16_s${REAL}_*.nc; do
    b=$(basename "$f")
    ln -s "$f" "$WORK/$b"
    ln -s "$f" "$WORK/${b/_s${REAL}/_s20242201302171}"          # minuto siguiente, misma decena
    ln -s "$f" "$WORK/${b/_s${REAL}/_s20242201301999}"          # mismo minuto, otros segundos
    ln -s "$f" "$WORK/${b/CMIPC-/CMIPM1-}"                      # otro sector
    ln -s "$f" "$WORK/${b/_G16_/_G18_}"                         # otro satélite
done

# Revisa que los cuatro canales cargados lleven exactamente el prefijo esperado.
check_scene() {
    local anchor="$1" expect="$2"
    local log loaded
    log=$(../bin/hpsv rgb -v -s -8 -o "$WORK/out.png" "$WORK/$anchor" 2>&1) || {
        echo "FAIL: hpsv falló con el ancla $anchor" >&2
        echo "$log" | tail -5 >&2
        exit 1
    }
    loaded=$(echo "$log" | grep "Loading channel" | sed 's|.*/||')
    local n_ok n_all
    n_all=$(echo "$loaded" | grep -c . || true)
    n_ok=$(echo "$loaded" | grep -c "^OR_ABI-L2-CMIPC-M6C[0-9][0-9]_G16_s${expect}_" || true)
    if [ "$n_all" -ne 4 ] || [ "$n_ok" -ne 4 ]; then
        echo "FAIL: ancla $anchor cargó canales de otra escena:" >&2
        echo "$loaded" >&2
        exit 1
    fi
    echo "OK: ancla s${expect} carga sus 4 canales"
}

anchor_of() { basename "$(ls "$WORK"/OR_ABI-L2-CMIPC-M6C01_G16_s"$1"_*.nc)"; }

check_scene "$(anchor_of $REAL)" $REAL
check_scene "$(anchor_of 20242201302171)" 20242201302171
# Mismo minuto que la real: el desempate por sello exacto decide.
check_scene "$(anchor_of 20242201301999)" 20242201301999
