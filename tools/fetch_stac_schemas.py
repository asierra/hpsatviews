#!/usr/bin/env python3
"""Descarga los esquemas oficiales de STAC que hpsv declara, y su cierre de
referencias, a docs/stac/schemas/.

Se versionan en vez de descargarlos en cada corrida por dos razones: la suite
deja de depender de que un servicio ajeno esté en pie, y las versiones quedan
fijadas de forma visible en el árbol, que es justo lo que el riesgo de «deriva
de versiones» de docs/stac/STAC_PLAN.md pide. Para actualizarlas, cambia las
versiones en src/metadata.c, corre este script y revisa el diff.

Uso:  python3 tools/fetch_stac_schemas.py
"""
import json
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "docs" / "stac" / "schemas"
METADATA = ROOT / "src" / "metadata.c"


def declared_urls():
    """Las URL que el emisor declara, leídas del código, no copiadas a mano."""
    src = METADATA.read_text()
    version = re.search(r'#define STAC_VERSION "([^"]+)"', src).group(1)
    urls = [f"https://schemas.stacspec.org/v{version}/item-spec/json-schema/item.json"]
    urls += re.findall(r'#define STAC_EXT_\w+ "([^"]+)"', src)
    return urls


def local_path(uri):
    parts = urllib.parse.urlsplit(uri)
    return DEST / parts.netloc / parts.path.lstrip("/")


def refs(node):
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "$ref" and isinstance(value, str):
                yield value
            else:
                yield from refs(value)
    elif isinstance(node, list):
        for item in node:
            yield from refs(item)


def main():
    pending = list(declared_urls())
    seen = set()
    while pending:
        uri = pending.pop()
        base, _, _ = uri.partition("#")
        if not base.startswith("http") or base in seen:
            continue
        seen.add(base)
        # Los meta-esquemas de json-schema.org los trae jsonschema de fábrica.
        if urllib.parse.urlsplit(base).netloc == "json-schema.org":
            continue
        print("bajando", base)
        # Con el User-Agent por omisión de urllib, algunos servidores responden 403.
        req = urllib.request.Request(base, headers={"User-Agent": "hpsatviews-schema-fetch"})
        doc = json.loads(urllib.request.urlopen(req, timeout=30).read())
        path = local_path(base)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
        for ref in refs(doc):
            pending.append(urllib.parse.urljoin(base, ref))
    print(f"\n{len(seen)} esquemas en {DEST.relative_to(ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
