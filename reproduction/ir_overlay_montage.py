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

Ventanas fijas, repetibles, cada una en <prefijo>_<nombre>.png:

  --window [nombre:]X,Y      esquina superior izquierda en píxeles nativos
  --at [nombre:]LAT,LON      centro en grados; se proyecta con el CRS del
                             primer GeoTIFF, así que sirve igual en rejilla
                             fija que en geográficas

Son las que hay que usar para juzgar un caso concreto --estratocúmulo marino,
hielo polar, el Altiplano-- porque una búsqueda automática de "dónde hay color
verdadero en juego" resultó poco fiable: la paleta infrarroja es mucho más
saturada que el color verdadero, así que cualquier puntuación por saturación
se va al lado nocturno o a la penumbra.

--probe I,J elige qué par compara la búsqueda automática (por omisión la
referencia contra la última). Sirve para aislar un solo cambio entre variantes:
por ejemplo, el mismo umbral con dos penumbras distintas.
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


def parse_named(spec, n, kind):
    """'[nombre:]A,B' -> (nombre, a, b)."""
    name, _, coords = spec.rpartition(":")
    try:
        a, b = (float(v) for v in coords.split(","))
    except ValueError:
        sys.exit(f"{kind} espera [nombre:]A,B, no '{spec}'")
    return (name or f"v{n}"), a, b


def latlon_to_pixel(ds, lat, lon):
    from osgeo import osr
    src = osr.SpatialReference()
    src.ImportFromEPSG(4326)
    src.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
    dst = osr.SpatialReference(wkt=ds.GetProjection())
    dst.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
    try:
        x, y, _ = osr.CoordinateTransformation(src, dst).TransformPoint(lon, lat)
    except RuntimeError:
        x = y = float("inf")
    gt = ds.GetGeoTransform()
    if not (np.isfinite(x) and np.isfinite(y)):
        return None
    px, py = int((x - gt[0]) / gt[1]), int((y - gt[3]) / gt[5])
    if not (0 <= px < ds.RasterXSize and 0 <= py < ds.RasterYSize):
        return None
    return px, py


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tifs", nargs="+", help="el primero es la referencia")
    ap.add_argument("--out-prefix", required=True)
    ap.add_argument("--labels", default="", help="etiquetas separadas por |")
    ap.add_argument("--window", action="append", default=[],
                    help="[nombre:]X,Y en píxeles nativos (esquina); repetible")
    ap.add_argument("--at", action="append", default=[],
                    help="[nombre:]LAT,LON en grados (centro); repetible")
    ap.add_argument("--window-size", type=int, default=None,
                    help="lado del recorte en píxeles nativos")
    ap.add_argument("--probe", default=None,
                    help="I,J: par de imágenes que compara la búsqueda automática")
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
    side = args.window_size or WIN * ratio

    # Vista de conjunto.
    per = max(240, MONTAGE_W // len(args.tifs) - 6)
    montage([read(p, bw=per, bh=per) for p in args.tifs], labels,
            f"{args.out_prefix}_disco.png", f"disco completo a {per}px")

    def crop(X, Y, suffix, note):
        X = max(0, min(X, native - side))
        Y = max(0, min(Y, ds.RasterYSize - side))
        montage([read(p, X, Y, side, side) for p in args.tifs], labels,
                f"{args.out_prefix}_{suffix}.png", f"{note} x={X} y={Y} {side}x{side}")

    # Ventana de mayor cambio.
    i, j = 0, len(args.tifs) - 1
    if args.probe:
        try:
            i, j = (int(v) for v in args.probe.split(","))
            args.tifs[i], args.tifs[j]
        except (ValueError, IndexError):
            sys.exit(f"--probe espera I,J entre 0 y {len(args.tifs) - 1}")
    ref = read(args.tifs[i], bw=thumb, bh=thumb).astype(int)
    probe = read(args.tifs[j], bw=thumb, bh=thumb).astype(int)
    delta = np.abs(ref - probe).max(axis=2) * (ref.max(axis=2) > 0)
    _, y, x = best_window(delta)
    crop(x * ratio, y * ratio, "cambio", f"mayor cambio entre {labels[i]} y {labels[j]},")

    # Ventanas pedidas.
    n = 0
    for spec in args.window:
        n += 1
        name, X, Y = parse_named(spec, n, "--window")
        crop(int(X), int(Y), name, "ventana pedida,")
    for spec in args.at:
        n += 1
        name, lat, lon = parse_named(spec, n, "--at")
        pix = latlon_to_pixel(ds, lat, lon)
        if pix is None:
            # Un sector o el otro satélite no ven todas las regiones: se avisa y
            # se sigue con las demás.
            print(f"aviso: {name} ({lat}, {lon}) cae fuera de la imagen; se omite",
                  file=sys.stderr)
            continue
        cx, cy = pix
        crop(cx - side // 2, cy - side // 2, name, f"centrada en {lat},{lon},")


if __name__ == "__main__":
    main()
