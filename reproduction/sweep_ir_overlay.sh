#!/bin/bash
# Barrido de umbrales de --ir-overlay (realce IR diurno de daynite). Para cada
# escena renderiza daynite sin realce y con varios pares --ir-range, y arma
# montajes lado a lado: el disco completo, la ventana donde más cambia la
# imagen y las regiones fijas que se le pidan.
#
# Con él se eligieron los umbrales por omisión (220,240). Si alguien propone
# cambiarlos, o mover la penumbra de include/daynight_mask.h, este es el
# estudio que hay que repetir.
#
# Uso:
#   reproduction/sweep_ir_overlay.sh <anchor.nc> [<anchor.nc> ...]
#
# Las anclas las elige pick_scenes.sh, que además comprueba que la escena esté
# completa. En LANOT los días completos están en /depot; /data1 solo guarda
# fragmentos de los últimos días:
#
#   D=/depot/goes-east/l1b/abi/fd/2026
#   reproduction/sweep_ir_overlay.sh \
#       $(reproduction/pick_scenes.sh --dir $D/245 --hours 12,14,16,20,22 --tolerance 10)
#
# Overrides:
#   HPSV_BIN       Binario con el que se renderizan las variantes con realce.
#                  Por omisión bin/hpsv.
#   HPSV_BASE_BIN  Binario del panel de referencia, que se renderiza sin
#                  --ir-overlay. Por omisión el mismo HPSV_BIN. Separarlos sirve
#                  para aislar otro cambio: p.ej. una penumbra distinta
#                  compilada aparte, contra la referencia de producción.
#   THRESHOLDS     Pares T1,T2 a probar. Por omisión "220,240 225,245 230,250".
#   WINDOWS        Regiones fijas, "nombre:LAT,LON" separadas por espacios.
#                  Por omisión las que decidieron los umbrales (ver abajo).
#                  Las que caen fuera de la imagen se omiten con un aviso.
#   WINDOW_SIZE    Lado de cada recorte, en píxeles nativos. Por omisión 600.
#   OUTDIR         Dónde dejar tif y png. Por omisión ./sweep_ir_<fecha>.
#   KEEP_TIF       1 para conservar los GeoTIFF (~30 MB cada uno) y poder
#                  rehacer montajes con otras ventanas; por omisión se borran.
#
# Qué mirar, por si lo revisa alguien que no siguió la discusión original:
#   - weddell: el hielo marino antártico está bajo 260 K, y un T2 de 250 K o
#     más lo pinta de cian. Fue lo que descartó los pares calientes.
#   - scperu, scchile: estratocúmulo marino. Se temía que un T2 alto lo
#     pintara como convección; con los pares probados no ocurrió.
#   - altiplano: superficie alta y fría; solo debe colorearse la convección.
#   - cambio: suele caer en un sistema frío en pleno día. La estructura térmica
#     tiene que leerse igual con todos los pares, y la nube media alrededor no
#     debe quedar bajo un velo azul o lavanda.
#   Las coordenadas por omisión están pensadas para GOES-East (75.2 W).

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

BIN="${HPSV_BIN:-$ROOT/bin/hpsv}"
BASE_BIN="${HPSV_BASE_BIN:-$BIN}"
THRESHOLDS="${THRESHOLDS:-220,240 225,245 230,250}"
WINDOWS="${WINDOWS:-weddell:-65,-45 scperu:-18,-82 scchile:-25,-78 altiplano:-18,-68}"
WINDOW_SIZE="${WINDOW_SIZE:-600}"
OUTDIR="${OUTDIR:-$PWD/sweep_ir_$(date +%Y%m%d_%H%M%S)}"
KEEP_TIF="${KEEP_TIF:-0}"

if [[ $# -lt 1 ]]; then
    sed -n '2,45p' "$0" >&2
    exit 2
fi

for b in "$BIN" "$BASE_BIN"; do
    if [[ ! -x "$b" ]]; then
        echo "Error: no encuentro el binario '$b'. Compila con make o apunta" >&2
        echo "HPSV_BIN / HPSV_BASE_BIN a donde esté." >&2
        exit 1
    fi
done
if ! "$BIN" rgb --help 2>&1 | grep -q -- "--ir-overlay"; then
    echo "Error: '$BIN' no conoce --ir-overlay." >&2
    exit 1
fi
if ! python3 -c "from osgeo import gdal; from PIL import Image" 2>/dev/null; then
    echo "Error: los montajes necesitan las ligaduras de GDAL para Python y Pillow." >&2
    exit 1
fi

win_args=()
for w in $WINDOWS; do win_args+=(--at "$w"); done

mkdir -p "$OUTDIR"
echo "Salida: $OUTDIR"
echo "Umbrales: $THRESHOLDS"
echo "Ventanas: $WINDOWS"
echo

for anchor in "$@"; do
    if [[ ! -f "$anchor" ]]; then
        echo "  aviso: '$anchor' no existe, se salta" >&2
        continue
    fi
    # Firma de la escena, p.ej. s20262582200216 -> 20262582200
    base=$(basename "$anchor")
    if [[ $base =~ _s([0-9]{11}) ]]; then
        tag="${BASH_REMATCH[1]}"
    else
        tag=$(basename "$anchor" .nc)
    fi
    echo "=== escena $tag ==="

    tifs=()
    labels=()

    out="$OUTDIR/${tag}_base.tif"
    echo -n "  sin realce... "
    if "$BASE_BIN" rgb -o "$out" "$anchor" >"$OUTDIR/${tag}_base.log" 2>&1; then
        echo "ok"; tifs+=("$out"); labels+=("sin realce")
    else
        echo "FALLÓ (ver ${tag}_base.log)"; continue
    fi

    for th in $THRESHOLDS; do
        safe="${th/,/-}"
        out="$OUTDIR/${tag}_ir${safe}.tif"
        echo -n "  --ir-range $th... "
        if "$BIN" rgb --ir-overlay --ir-range "$th" -o "$out" "$anchor" \
               >"$OUTDIR/${tag}_ir${safe}.log" 2>&1; then
            echo "ok"; tifs+=("$out"); labels+=("$th K")
        else
            echo "FALLÓ (ver ${tag}_ir${safe}.log)"
        fi
    done

    if [[ ${#tifs[@]} -lt 2 ]]; then
        echo "  sin suficientes salidas para comparar, se salta el montaje"
        continue
    fi

    echo "  montajes..."
    if python3 "$SCRIPT_DIR/ir_overlay_montage.py" \
           --out-prefix "$OUTDIR/${tag}" \
           --labels "$(IFS='|'; echo "${labels[*]}")" \
           --window-size "$WINDOW_SIZE" "${win_args[@]}" "${tifs[@]}"; then
        [[ "$KEEP_TIF" == "1" ]] || rm -f "${tifs[@]}"
    else
        echo "  el montaje falló; se conservan los GeoTIFF" >&2
    fi
    echo
done

echo "Listo. Revisa los *_cambio.png y las ventanas en $OUTDIR"
