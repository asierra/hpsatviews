#!/usr/bin/env python3
"""Montajes lado a lado para el barrido de la superposición infrarroja.

Lo llama reproduction/sweep_ir_overlay.sh. Recibe N GeoTIFF del mismo tamaño
--el primero es la referencia-- y escribe:

  <prefijo>_disco.png   el disco completo de cada variante, reducido. Es la
                        vista de conjunto: de un golpe se ve dónde pinta cada
                        umbral y dónde no toca nada.
  <prefijo>_cambio.png  la ventana donde la última variante más se aparta de la
                        referencia, recortada a resolución nativa. Suele caer en
                        el terminador o sobre convección, que es justo donde hay
                        que comprobar que la estructura térmica aparece sin que
                        el borde se vuelva un contorno.

Con --window X,Y se recorta donde uno diga en vez de donde caiga la búsqueda.
Es lo que hay que usar para juzgar un caso concreto --estratocúmulo marino
frío, por ejemplo-- porque una búsqueda automática de "dónde hay color
verdadero en juego" resultó poco fiable: la paleta infrarroja es mucho más
saturada que el color verdadero, así que cualquier puntuación por saturación
se va al lado nocturno o a la penumbra. Las coordenadas se sacan del montaje
del disco: son píxeles de la imagen nativa, esquina superior izquierda.
"""
import argparse
import sys

import numpy as np

try:
    from osgeo import gdal
except ImportError:
    sys.exit("falta python3-gdal")
try:
    from PIL import Image
except ImportError:
    sys.exit("falta python3-pil")

gdal.UseExceptions()

THUMB = 1356          # lado de la miniatura de búsqueda
WIN = 110             # lado de la ventana, en píxeles de miniatura
STEP = 20
MONTAGE_W = 1240      # ancho final del montaje


def read(path, x=0, y=0, w=None, h=None, bw=None, bh=None):
    ds = gdal.Open(path)
    w = w or ds.RasterXSize
    h = h or ds.RasterYSize
    bands = [ds.GetRasterBand(i + 1).ReadAsArray(x, y, w, h, bw, bh) for i in range(3)]
    return np.dstack(bands)


def best_window(score, win=WIN, step=STEP):
    """Ventana de lado @win con mayor @score medio."""
    best = (-1.0, 0, 0)
    n = score.shape[0]
    for y in range(0, n - win, step):
        for x in range(0, n - win, step):
            v = float(score[y:y + win, x:x + win].mean())
            if v > best[0]:
                best = (v, y, x)
    return best


def montage(tiles, labels, out, note=""):
    gap = 6
    h = tiles[0].shape[0]
    w = tiles[0].shape[1]
    img = Image.new("RGB", (w * len(tiles) + gap * (len(tiles) - 1), h), (255, 0, 0))
    for i, t in enumerate(tiles):
        img.paste(Image.fromarray(t.astype(np.uint8)), (i * (w + gap), 0))
    scale = MONTAGE_W / img.width
    if scale < 1:
        img = img.resize((int(img.width * scale), int(img.height * scale)), Image.LANCZOS)
    img.save(out)
    print(f"{out}  [{' | '.join(labels)}]  {note}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tifs", nargs="+", help="el primero es la referencia")
    ap.add_argument("--out-prefix", required=True)
    ap.add_argument("--labels", default="", help="etiquetas separadas por |")
    ap.add_argument("--window", default=None,
                    help="X,Y en píxeles nativos: recorta ahí en vez de buscar")
    ap.add_argument("--window-size", type=int, default=None,
                    help="lado del recorte en píxeles nativos")
    args = ap.parse_args()

    if len(args.tifs) < 2:
        sys.exit("hacen falta al menos dos imágenes")

    labels = args.labels.split("|") if args.labels else [str(i) for i in range(len(args.tifs))]
    if len(labels) != len(args.tifs):
        sys.exit(f"{len(labels)} etiquetas para {len(args.tifs)} imágenes")

    ds = gdal.Open(args.tifs[0])
    native = ds.RasterXSize
    thumb = min(THUMB, native)
    ratio = max(1, native // thumb)

    # Vista de conjunto.
    per = max(240, MONTAGE_W // len(args.tifs) - 6)
    montage([read(p, bw=per, bh=per) for p in args.tifs], labels,
            f"{args.out_prefix}_disco.png", f"disco completo a {per}px")

    # Ventana de detalle.
    side = args.window_size or WIN * ratio
    if args.window:
        try:
            X, Y = (int(v) for v in args.window.split(","))
        except ValueError:
            sys.exit("--window espera X,Y en enteros")
        note = f"ventana pedida x={X} y={Y} {side}x{side}"
    else:
        ref = read(args.tifs[0], bw=thumb, bh=thumb).astype(int)
        probe = read(args.tifs[-1], bw=thumb, bh=thumb).astype(int)
        delta = np.abs(ref - probe).max(axis=2) * (ref.max(axis=2) > 0)
        _, y, x = best_window(delta)
        X, Y = x * ratio, y * ratio
        note = f"mayor cambio, x={X} y={Y} {side}x{side}"
    X = max(0, min(X, native - side))
    Y = max(0, min(Y, native - side))
    montage([read(p, X, Y, side, side) for p in args.tifs], labels,
            f"{args.out_prefix}_cambio.png", note)


if __name__ == "__main__":
    main()
