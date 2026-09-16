#!/usr/bin/env python3
"""Reconstruye Items de STAC a partir de GeoTIFF ya producidos por hpsv.

El barrido retroactivo de la fase 5 de docs/stac/STAC_PLAN.md. Sirve para el
acervo escrito antes de que `hpsv -j` emitiera Items; para lo nuevo, el emisor
es la herramienta y esto no hace falta.

QUÉ SE RECUPERA, y qué no:

  * Del archivo: satélite, sector, instante, producto y banda (etiquetas GDAL),
    y la georreferencia completa -- geotransformación, CRS y tamaño --, de donde
    salen proj:* y la huella.
  * NO se recupera `hpsv:channels` (magnitud física, unidades, rango real) ni
    `hpsv:enhancements`: nunca se escribieron dentro del GeoTIFF. Los Items que
    salen de aquí son, por tanto, más pobres que los que emite hpsv, y lo
    declaran con `hpsv:reconstructed`.
  * Los PNG quedan fuera: sin georreferencia no hay nada que reconstruir.

Uso:
    python3 tools/stac_sweep.py RUTA [RUTA...] [--out-dir DIR] [--collection ID]
                                    [--dry-run]
"""
import argparse
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path

def _install_hint(deb, rpm, probe=None, pip=None):
    """Cómo instalar, según el gestor de paquetes que haya en la máquina.

    El proyecto se compila en Debian/Ubuntu y en RHEL/Rocky (ver CLAUDE.md), así
    que un mensaje que sólo sepa de apt manda a media flota a buscar un paquete
    con un nombre que ahí no existe.
    """
    import shutil

    if shutil.which("apt-get"):
        lines = [f"  sudo apt-get install {deb}"]
    elif shutil.which("dnf"):
        lines = [f"  sudo dnf install {rpm}"]
        if probe:
            lines.append(f"  (si ese nombre no existe: dnf provides '{probe}')")
    elif shutil.which("zypper"):
        lines = [f"  sudo zypper install {rpm}"]
    else:
        lines = [f"  {deb} en Debian/Ubuntu, {rpm} en RHEL/Rocky/Fedora"]
    if pip:
        lines.append(f"  o bien: python3 -m pip install {pip}")
    return "\n".join(lines)


try:
    from osgeo import gdal, osr
except ImportError as exc:  # pragma: no cover
    # Las ligaduras de GDAL no se instalan con pip de forma fiable: la rueda
    # tiene que casar con la libgdal del sistema, así que aquí no se ofrece.
    sys.exit(f"falta el módulo de GDAL para Python ({exc}).\n"
             + _install_hint("python3-gdal", "python3-gdal", "*/osgeo/gdal.py"))

gdal.UseExceptions()


class _Transformer:
    """Transforma del CRS del GeoTIFF a EPSG:4326, en orden lon,lat.

    Se apoya en osr y no en pyproj, aunque pyproj tiene la API más cómoda,
    porque GDAL ya es dependencia dura de este script y pyproj no está
    empaquetado para EPEL 10, que es lo que corre uno de los servidores de
    producción: era una dependencia más que instalar a mano para ganar una
    llamada. Las dos hablan con el mismo PROJ por debajo.
    """

    def __init__(self, wkt):
        src = osr.SpatialReference()
        src.ImportFromWkt(wkt)
        dst = osr.SpatialReference()
        dst.ImportFromEPSG(4326)
        # Orden lon,lat en los dos extremos, que es lo que pedía always_xy=True
        # en pyproj. Sin esto GDAL 3 respeta el orden de la autoridad y devuelve
        # EPSG:4326 como lat,lon, con lo que la huella sale con las coordenadas
        # intercambiadas -- y en silencio, porque ambas son números plausibles.
        src.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
        dst.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
        self._ct = osr.CoordinateTransformation(src, dst)

    def transform(self, x, y):
        # Fuera del disco la transformación falla; con gdal.UseExceptions() eso
        # llega como excepción en vez de como el infinito que devolvía pyproj.
        # _project() espera lo segundo, así que se traduce aquí.
        try:
            lon, lat, _ = self._ct.TransformPoint(x, y)
        except Exception:
            return float("inf"), float("inf")
        return lon, lat

STAC_VERSION = "1.0.0"
EXT_PROJ = "https://stac-extensions.github.io/projection/v1.1.0/schema.json"
EXT_EO = "https://stac-extensions.github.io/eo/v1.1.0/schema.json"
EXT_PROCESSING = "https://stac-extensions.github.io/processing/v1.1.0/schema.json"

PLATFORMS = {"G16": "goes-16", "G17": "goes-17", "G18": "goes-18", "G19": "goes-19"}
BAND_CENTRE = [None, 0.47, 0.64, 0.86, 1.37, 1.6, 2.24, 3.9, 6.2,
               6.9, 7.3, 8.4, 9.6, 10.3, 11.2, 12.3, 13.3]
COMMON_NAME = {1: "blue", 2: "red", 3: "nir08", 4: "cirrus", 5: "swir16", 6: "swir22"}

EDGE_SAMPLES = 32          # igual que FOOTPRINT_EDGE_SAMPLES en include/footprint.h
LIMB_BISECTIONS = 40


# --------------------------------------------------------------------------
# Huella. Port fiel de src/footprint.c: se camina el borde del ráster y, donde
# una muestra cae fuera del disco visible, se lleva por bisección hacia el
# centro hasta volver a la Tierra; donde un borde cruza el limbo se biseca
# además a lo largo del borde para clavar esa esquina. Que sean dos
# implementaciones es una deuda consciente: tests/test_sweep.sh compara este
# resultado contra el Item que emite hpsv para el mismo archivo, que es lo que
# impide que se separen.
# --------------------------------------------------------------------------
def _wrap(lon):
    while lon < -180.0:
        lon += 360.0
    while lon > 180.0:
        lon -= 360.0
    return lon


def _delta(lon, ref):
    d = lon - ref
    while d < -180.0:
        d += 360.0
    while d >= 180.0:
        d -= 360.0
    return d


def _project(tr, x, y):
    lon, lat = tr.transform(x, y)
    if not (math.isfinite(lon) and math.isfinite(lat)):
        return None
    if abs(lat) > 90.0 or abs(lon) > 180.0:
        return None
    return _wrap(lon), lat


def _bisect(tr, ax, ay, bx, by):
    """Último punto sobre la Tierra en el segmento de a (válido) hacia b."""
    lo, hi = 0.0, 1.0
    for _ in range(LIMB_BISECTIONS):
        mid = 0.5 * (lo + hi)
        if _project(tr, ax + (bx - ax) * mid, ay + (by - ay) * mid):
            lo = mid
        else:
            hi = mid
    return _project(tr, ax + (bx - ax) * lo, ay + (by - ay) * lo)


def footprint(crs_wkt, gt, width, height, lon_ref):
    tr = _Transformer(crs_wkt)
    ax, ay = gt[0], gt[3]
    bx = ax + width * gt[1]
    by = ay + height * gt[5]
    x0, x1 = min(ax, bx), max(ax, bx)
    y0, y1 = min(ay, by), max(ay, by)
    cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
    if not _project(tr, cx, cy):
        return None

    # Mismo orden de esquinas que src/footprint.c: desde la inferior izquierda
    # y en el mismo sentido. No es cosmético -- es lo que permite que
    # tests/test_sweep.sh compare los anillos vértice a vértice y detecte que
    # las dos implementaciones se separaron.
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)]
    samples = []
    for i in range(4):
        ax, ay = corners[i]
        bx, by = corners[i + 1]
        for k in range(EDGE_SAMPLES):
            t = k / EDGE_SAMPLES
            samples.append((ax + (bx - ax) * t, ay + (by - ay) * t))

    ring = []
    n = len(samples)
    for i, (px, py) in enumerate(samples):
        qx, qy = samples[i - 1]
        here = _project(tr, px, py)
        prev = _project(tr, qx, qy)
        if (here is None) != (prev is None):
            cut = (_bisect(tr, px, py, qx, qy) if here else _bisect(tr, qx, qy, px, py))
            if cut:
                ring.append(cut)
        ring.append(here if here else _bisect(tr, cx, cy, px, py))
    ring = [p for p in ring if p]
    if len(ring) < 4:
        return None

    dmin = min(_delta(p[0], lon_ref) for p in ring)
    dmax = max(_delta(p[0], lon_ref) for p in ring)
    bbox = [_wrap(lon_ref + dmin), min(p[1] for p in ring),
            _wrap(lon_ref + dmax), max(p[1] for p in ring)]
    return ring, bbox


# --------------------------------------------------------------------------
# Lectura de un GeoTIFF de hpsv
# --------------------------------------------------------------------------
def read_raster(path):
    ds = gdal.Open(str(path))
    tags = ds.GetMetadata()
    if tags.get("tool") != "hpsatviews":
        return None                      # no lo escribió hpsv: no se toca

    srs = osr.SpatialReference()
    srs.ImportFromWkt(ds.GetProjection())
    epsg = 4326 if srs.IsGeographic() else None

    # gray, pseudocolor y rgb no se distinguen por las etiquetas, pero sí por la
    # forma del ráster: una banda con paleta es pseudocolor, sin paleta gray, y
    # tres bandas son un compuesto, cuyo modo sí viaja en `product`.
    if ds.RasterCount >= 3:
        kind, band_seg = tags.get("product", "rgb"), None
    else:
        kind = "pseudo" if ds.GetRasterBand(1).GetColorTable() else "gray"
        band_seg = tags.get("band")

    return {
        "path": Path(path),
        "tags": tags,
        "gt": ds.GetGeoTransform(),
        "width": ds.RasterXSize,
        "height": ds.RasterYSize,
        "wkt2": srs.ExportToWkt(["FORMAT=WKT2_2019", "MULTILINE=NO"]),
        "wkt": ds.GetProjection(),
        "epsg": epsg,
        "kind": kind,
        "band": band_seg,
        "lon_ref": srs.GetProjParm("central_meridian", 0.0),
    }


def item_id(r):
    tags = r["tags"]
    sat = tags.get("satellite", "GXX")
    sector = tags.get("sector")
    scan = tags.get("scan_time", "")
    try:
        stamp = datetime.strptime(scan, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        instant = stamp.strftime("%Y%j_%H%M")
    except ValueError:
        return None, None
    prefix = f"{sat}_{sector}" if sector else sat
    parts = ["hpsv", prefix, instant, r["kind"]]
    if r["band"]:
        parts.append(r["band"])
    return "_".join(parts), stamp


def build_item(rasters, collection):
    """Un Item por identidad de escena; los rásteres son sus activos."""
    iid, stamp = item_id(rasters[0])
    fixed = next((r for r in rasters if r["epsg"] != 4326), None)
    geographic = next((r for r in rasters if r["epsg"] == 4326), None)
    # La huella sale de la rejilla fija, donde sigue el limbo; el rectángulo del
    # ráster reproyectado reclamaría esquinas que son nodato. El proj:* del Item
    # describe la última salida escrita, que es la geográfica: así un lector ve
    # la misma forma venga el Item del emisor o de aquí. De todos modos lo que
    # hay que leer es el proj:* del ACTIVO.
    source = fixed or rasters[0]
    grid = geographic or source

    fp = footprint(source["wkt"], source["gt"], source["width"], source["height"],
                   source["lon_ref"])
    if fp is None:
        return None, "no se pudo calcular la huella"
    ring, bbox = fp

    tags = source["tags"]
    exts = [EXT_PROJ, EXT_PROCESSING]
    band_no = int(source["band"][1:]) if source["band"] and source["band"][0] == "C" else None
    if band_no:
        exts.insert(1, EXT_EO)

    props = {
        "datetime": stamp.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "processing:software": {"hpsatviews": "desconocida"},
        "proj:transform": [grid["gt"][1], grid["gt"][2], grid["gt"][0],
                           grid["gt"][4], grid["gt"][5], grid["gt"][3]],
        "proj:shape": [grid["height"], grid["width"]],
        "proj:wkt2": grid["wkt2"],
        # Reconstruido: lo que no estaba en el archivo no se inventa.
        "hpsv:reconstructed": True,
    }
    if grid["epsg"]:
        props["proj:epsg"] = grid["epsg"]
    platform = PLATFORMS.get(tags.get("satellite"))
    if platform:
        props["platform"] = platform
        props["constellation"] = "goes"
        props["instruments"] = ["abi"]
    if tags.get("sector"):
        props["hpsv:sector"] = tags["sector"]
    if tags.get("product"):
        props["hpsv:product"] = tags["product"]

    assets = {}
    for r in rasters:
        key = "image_geographic" if r["epsg"] == 4326 else "image"
        asset = {
            "href": r["path"].name,
            "type": "image/tiff; application=geotiff",
            "roles": ["data"],
            "proj:shape": [r["height"], r["width"]],
            "proj:transform": [r["gt"][1], r["gt"][2], r["gt"][0],
                               r["gt"][4], r["gt"][5], r["gt"][3]],
            "proj:wkt2": r["wkt2"],
        }
        if r["epsg"]:
            asset["proj:epsg"] = r["epsg"]
        if band_no:
            band = {"name": r["band"] or source["band"],
                    "center_wavelength": BAND_CENTRE[band_no]}
            if band_no in COMMON_NAME:
                band["common_name"] = COMMON_NAME[band_no]
            asset["eo:bands"] = [band]
        assets[key] = asset

    item = {
        "type": "Feature",
        "stac_version": STAC_VERSION,
        "stac_extensions": exts,
        "id": iid,
        "geometry": {"type": "Polygon",
                     "coordinates": [[list(p) for p in ring] + [list(ring[0])]]},
        "bbox": bbox,
        "properties": props,
        "links": [],
        "assets": assets,
    }
    if collection:
        item["collection"] = collection
    return item, None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="GeoTIFF o directorios con ellos")
    ap.add_argument("--out-dir", help="dónde escribir (por omisión, junto al archivo)")
    ap.add_argument("--collection", help="identificador de colección")
    ap.add_argument("--dry-run", action="store_true", help="no escribe, sólo informa")
    args = ap.parse_args()

    files = []
    for p in args.paths:
        path = Path(p)
        files.extend(sorted(path.rglob("*.tif")) + sorted(path.rglob("*.tiff"))
                     if path.is_dir() else [path])

    groups, skipped = {}, []
    for f in files:
        try:
            r = read_raster(f)
        except Exception as exc:
            skipped.append((f, str(exc)[:70]))
            continue
        if r is None:
            skipped.append((f, "no lo escribió hpsv"))
            continue
        iid, _ = item_id(r)
        if not iid:
            skipped.append((f, "sin scan_time utilizable"))
            continue
        groups.setdefault(iid, []).append(r)

    written = 0
    for iid, rasters in sorted(groups.items()):
        item, err = build_item(rasters, args.collection)
        if err:
            skipped.append((rasters[0]["path"], err))
            continue
        out_dir = Path(args.out_dir) if args.out_dir else rasters[0]["path"].parent
        dest = out_dir / f"{iid}.json"
        names = ", ".join(sorted(a["href"] for a in item["assets"].values()))
        if args.dry_run:
            print(f"[seco] {dest}  <- {names}")
        else:
            out_dir.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(item, indent=2, ensure_ascii=False) + "\n")
            print(f"{dest}  <- {names}")
        written += 1

    for path, why in skipped:
        print(f"omitido {path}: {why}", file=sys.stderr)
    print(f"\n{written} Items, {len(skipped)} archivos omitidos")
    print("Recuerda: hpsv:channels y hpsv:enhancements no se pueden reconstruir "
          "desde un GeoTIFF; estos Items los declaran ausentes con hpsv:reconstructed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
