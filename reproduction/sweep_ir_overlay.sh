#!/bin/bash
# Barrido de escenas para decidir los umbrales de la superposición infrarroja
# (rama daynite-ir-overlay). Para cada escena renderiza la salida de producción
# y la del prototipo con varios umbrales, y arma montajes recortados en las dos
# zonas donde se juega la decisión: donde más cambia la imagen (el terminador,
# normalmente) y donde hay color verdadero que perder (tierra o mar a la vista).
#
# Uso:
#   reproduction/sweep_ir_overlay.sh <anchor.nc> [<anchor.nc> ...]
#
# Las anclas las elige pick_scenes.sh, que además comprueba que la escena esté
# completa --el acervo llega canal por canal y una escena a medio bajar falla
# después, con un error que no señala la causa:
#
#   reproduction/sweep_ir_overlay.sh $(reproduction/pick_scenes.sh --hours 12,17,22)
#
# Overrides:
#   HPSV_BASE_BIN   Binario de referencia, la rama que va a producción.
#                   Por omisión bench_bin/hpsv-base.
#   HPSV_PROTO_BIN  Binario del prototipo, con image_overlay_ir().
#                   Por omisión bench_bin/hpsv-overlay.
#   THRESHOLDS      Pares t_opaque,t_clear a probar. Por omisión
#                   "230,255 240,270 250,285".
#   OUTDIR          Dónde dejar tif y png. Por omisión ./sweep_ir_<fecha>.
#   KEEP_TIF        1 para conservar los GeoTIFF (son ~30 MB cada uno y salen
#                   4 por escena; por omisión se borran tras el montaje).
#
# Los dos binarios se construyen así, desde la raíz del repo:
#   mkdir -p bench_bin
#   git checkout terminador-satpy && make clean && make && cp bin/hpsv bench_bin/hpsv-base
#   git checkout daynite-ir-overlay && make clean && make && cp bin/hpsv bench_bin/hpsv-overlay
#
# Qué mirar en cada montaje, por si lo revisa alguien que no siguió la
# discusión: en el recorte "color" la tierra y el mar tienen que salir igual que
# en el primer panel — si se tiñen, el umbral alto está demasiado caliente. En
# el recorte "cambio" la estructura térmica de los topes fríos tiene que estar
# al menos tan legible como en el primer panel. El caso que falta por ver, y la
# razón de barrer varias escenas, es estratocúmulo marino frío: es nube baja
# pero fría, así que un t_clear alto la pintaría de azul como si fuera
# convección.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

BASE_BIN="${HPSV_BASE_BIN:-$ROOT/bench_bin/hpsv-base}"
PROTO_BIN="${HPSV_PROTO_BIN:-$ROOT/bench_bin/hpsv-overlay}"
THRESHOLDS="${THRESHOLDS:-230,255 240,270 250,285}"
OUTDIR="${OUTDIR:-$PWD/sweep_ir_$(date +%Y%m%d_%H%M%S)}"
KEEP_TIF="${KEEP_TIF:-0}"

if [[ $# -lt 1 ]]; then
    sed -n '2,30p' "$0" >&2
    exit 2
fi

for b in "$BASE_BIN" "$PROTO_BIN"; do
    if [[ ! -x "$b" ]]; then
        echo "Error: no encuentro el binario '$b'." >&2
        echo "Constrúyelo como dice la cabecera de este script, o apunta" >&2
        echo "HPSV_BASE_BIN / HPSV_PROTO_BIN a donde estén." >&2
        exit 1
    fi
done
if ! python3 -c "from osgeo import gdal" 2>/dev/null; then
    echo "Error: falta python3-gdal, que es lo que arma los montajes." >&2
    exit 1
fi

mkdir -p "$OUTDIR"
echo "Salida: $OUTDIR"
echo "Umbrales: $THRESHOLDS"
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
    echo -n "  producción... "
    if "$BASE_BIN" rgb -o "$out" "$anchor" >"$OUTDIR/${tag}_base.log" 2>&1; then
        echo "ok"; tifs+=("$out"); labels+=("produccion")
    else
        echo "FALLÓ (ver ${tag}_base.log)"; continue
    fi

    for th in $THRESHOLDS; do
        safe="${th/,/-}"
        out="$OUTDIR/${tag}_ov${safe}.tif"
        echo -n "  overlay $th... "
        if HPSV_IR_OVERLAY="$th" "$PROTO_BIN" rgb -o "$out" "$anchor" \
               >"$OUTDIR/${tag}_ov${safe}.log" 2>&1; then
            echo "ok"; tifs+=("$out"); labels+=("$th K")
        else
            echo "FALLÓ (ver ${tag}_ov${safe}.log)"
        fi
    done

    if [[ ${#tifs[@]} -lt 2 ]]; then
        echo "  sin suficientes salidas para comparar, se salta el montaje"
        continue
    fi

    echo -n "  montajes... "
    if python3 "$SCRIPT_DIR/ir_overlay_montage.py" \
           --out-prefix "$OUTDIR/${tag}" \
           --labels "$(IFS='|'; echo "${labels[*]}")" \
           "${tifs[@]}"; then
        [[ "$KEEP_TIF" == "1" ]] || rm -f "${tifs[@]}"
    fi
    echo
done

echo "Listo. Revisa los *_cambio.png y *_color.png en $OUTDIR"
