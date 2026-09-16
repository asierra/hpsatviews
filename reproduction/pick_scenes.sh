#!/bin/bash
# Elige escenas completas del acervo rotativo de GOES y escribe la ruta del
# ancla (C01) de cada una, una por línea, para pasárselas a otro script:
#
#   reproduction/sweep_ir_overlay.sh $(reproduction/pick_scenes.sh --hours 12,17,22)
#
# El acervo rotativo de /data1 guarda unos 4 días con cadencia de 10 minutos,
# pero no completos: en septiembre de 2026 los días anteriores solo conservaban
# las 20-23 UTC. Los días completos están en /depot, un directorio por día:
#
#   reproduction/pick_scenes.sh --dir /depot/goes-east/l1b/abi/fd/2026/245 --hours 12,20
#
# A mano son cientos de archivos por canal y hay que cruzarlos para saber cuáles
# forman una escena utilizable.
#
# Uso:
#   reproduction/pick_scenes.sh [opciones]
#
# Opciones:
#   --dir DIR       Dónde buscar. Por omisión /data1/input/abi/l1b/fd.
#   --hours LISTA   Una escena por cada hora UTC de la lista, por cada día
#                   disponible: --hours 12,17,22. La elegida es la más cercana
#                   al minuto 00 de esa hora.
#   --at HHMM       Una escena por día, la más cercana a esa hora UTC exacta.
#   --day YYYYJJJ   Limita a ese día juliano (p. ej. 2026259). Repetible.
#   --last N        Sólo las N escenas más recientes, sin criterio de hora.
#   --tolerance MIN Cuánto puede alejarse una escena de la hora pedida antes de
#                   descartarla, en minutos. Por omisión 30. Con cadencia de 10
#                   minutos el error real nunca pasa de 5, así que rebasar 30
#                   significa que hay un hueco en el acervo: sin este límite la
#                   escena más cercana podría ser de horas después y saldría
#                   como si fuera la pedida.
#   --channels LIS  Canales que deben estar presentes para considerar la escena
#                   completa. Por omisión 01,02,03,13, que es lo que consumen
#                   truecolor y daynite.
#   --copy DIR      Copia ahí los canales de cada escena elegida, además de
#                   imprimir la ruta (que entonces apunta a la copia). Para que
#                   el barrido siga siendo repetible cuando /data1 purgue; con
#                   /depot no hace falta.
#   --lit           Ordena por tamaño de C01 en vez de por fecha. El tamaño es
#                   buen proxy de cuánto disco está iluminado, porque C01 es
#                   visible y el lado nocturno comprime a casi nada.
#   --verbose       Explica en stderr qué descartó y por qué.
#
# Solo comprueba que los canales existan, no que estén sanos: un archivo
# truncado (en /data1 hubo uno de 4.6 MB con inicio igual a fin) cuenta como
# presente. --hours lo esquiva en la práctica porque elige el minuto 00.
#
# Por qué comprueba los canales: hpsv infiere los hermanos del ancla por el
# sello de tiempo, así que si la escena está a medio llegar --pasa, porque los
# archivos aparecen uno a uno cada 10 minutos-- falla más adelante y con un
# mensaje que no señala la causa. Más vale descartarla aquí.

set -u

DIR="/data1/input/abi/l1b/fd"
HOURS=""
AT=""
DAYS=()
LAST=0
TOLERANCE=30
CHANNELS="01,02,03,13"
COPY=""
LIT=0
VERBOSE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir)      DIR="$2"; shift 2 ;;
        --hours)    HOURS="$2"; shift 2 ;;
        --at)       AT="$2"; shift 2 ;;
        --day)      DAYS+=("$2"); shift 2 ;;
        --last)     LAST="$2"; shift 2 ;;
        --tolerance) TOLERANCE="$2"; shift 2 ;;
        --channels) CHANNELS="$2"; shift 2 ;;
        --copy)     COPY="$2"; shift 2 ;;
        --lit)      LIT=1; shift ;;
        --verbose)  VERBOSE=1; shift ;;
        -h|--help)  sed -n '2,52p' "$0"; exit 0 ;;
        *) echo "opción desconocida: $1" >&2; exit 2 ;;
    esac
done

[[ -d "$DIR" ]] || { echo "no existe el directorio '$DIR'" >&2; exit 1; }

note() { [[ $VERBOSE == 1 ]] && echo "  $*" >&2; return 0; }

IFS=',' read -r -a CHANS <<< "$CHANNELS"
FIRST="${CHANS[0]}"

# Índice de escenas completas: clave YYYYJJJHHMM -> ruta del ancla.
declare -A ANCHOR
found=0
for f in "$DIR"/*C${FIRST}_*.nc; do
    [[ -e "$f" ]] || continue
    base=$(basename "$f")
    [[ $base =~ _s([0-9]{11}) ]] || { note "sin sello de tiempo: $base"; continue; }
    key="${BASH_REMATCH[1]}"
    complete=1
    for ch in "${CHANS[@]}"; do
        compgen -G "$DIR/*C${ch}_*_s${key}*.nc" >/dev/null || { complete=0; break; }
    done
    if [[ $complete == 0 ]]; then
        note "escena $key incompleta (falta C${ch}), se descarta"
        continue
    fi
    ANCHOR[$key]="$f"
    found=$((found + 1))
done

if [[ $found -eq 0 ]]; then
    echo "no hay escenas completas en '$DIR' con canales $CHANNELS" >&2
    exit 1
fi

keys=$(printf '%s\n' "${!ANCHOR[@]}" | sort)

# Filtro por día juliano.
if [[ ${#DAYS[@]} -gt 0 ]]; then
    filtered=""
    for k in $keys; do
        for d in "${DAYS[@]}"; do
            [[ "${k:0:7}" == "$d" ]] && filtered+="$k"$'\n'
        done
    done
    keys=$(printf '%s' "$filtered" | sed '/^$/d')
fi
[[ -n "$keys" ]] || { echo "ningún día coincide" >&2; exit 1; }

# Selección.
chosen=""
if [[ -n "$HOURS" || -n "$AT" ]]; then
    targets=()
    if [[ -n "$AT" ]]; then
        targets+=("$AT")
    else
        IFS=',' read -r -a hh <<< "$HOURS"
        for h in "${hh[@]}"; do targets+=("$(printf '%02d00' "$((10#$h))")"); done
    fi
    # Para cada día y cada hora objetivo, la escena de minuto más cercano.
    for day in $(printf '%s\n' $keys | cut -c1-7 | sort -u); do
        for t in "${targets[@]}"; do
            tmin=$((10#${t:0:2} * 60 + 10#${t:2:2}))
            best=""; bestd=99999
            for k in $keys; do
                [[ "${k:0:7}" == "$day" ]] || continue
                kmin=$((10#${k:7:2} * 60 + 10#${k:9:2}))
                d=$(( kmin > tmin ? kmin - tmin : tmin - kmin ))
                (( d < bestd )) && { bestd=$d; best=$k; }
            done
            if [[ -z "$best" ]]; then
                continue
            elif (( bestd > TOLERANCE )); then
                echo "aviso: día $day hora $t: la escena más cercana ($best) está a" \
                     "$bestd min, más de los $TOLERANCE de tolerancia; se descarta" >&2
            else
                chosen+="$best"$'\n'
                note "día $day hora $t -> $best (a $bestd min)"
            fi
        done
    done
    chosen=$(printf '%s' "$chosen" | sed '/^$/d' | sort -u)
else
    chosen="$keys"
fi

# Orden y recorte.
if [[ $LIT == 1 ]]; then
    chosen=$(for k in $chosen; do printf '%s %s\n' "$(stat -c%s "${ANCHOR[$k]}")" "$k"; done \
             | sort -rn | awk '{print $2}')
else
    chosen=$(printf '%s\n' $chosen | sort -r)
fi
if [[ "$LAST" -gt 0 ]]; then
    chosen=$(printf '%s\n' $chosen | head -n "$LAST")
fi

[[ -n "$chosen" ]] || { echo "ninguna escena cumple el criterio" >&2; exit 1; }

# Salida, copiando si se pidió.
[[ -n "$COPY" ]] && mkdir -p "$COPY"
for k in $chosen; do
    if [[ -n "$COPY" ]]; then
        for ch in "${CHANS[@]}"; do
            for src in "$DIR"/*C${ch}_*_s${k}*.nc; do
                dst="$COPY/$(basename "$src")"
                [[ -e "$dst" ]] || cp -- "$src" "$dst"
            done
        done
        echo "$COPY/$(basename "${ANCHOR[$k]}")"
    else
        echo "${ANCHOR[$k]}"
    fi
done
