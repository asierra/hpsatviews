# Auditoría de publicabilidad: hpsatviews — JOSS vs. SoftwareX

**Fecha:** 2026-06-21
**Auditor:** Claude Code (revisión automatizada del repositorio + metadatos de GitHub)
**Repositorio:** https://github.com/asierra/hpsatviews

---

## TL;DR (veredicto rápido)

| Pregunta | Respuesta |
|---|---|
| ¿Cumple HOY los criterios de admisión de **JOSS**? | **Parcialmente** — cumple el núcleo (esfuerzo sustancial, licencia OSI, repo público, software completo), pero faltan: docs en **inglés**, **tests automatizados + CI**, **guías de comunidad** y un **release etiquetado**. |
| ¿Cumple HOY los de **SoftwareX**? | **Parcialmente** — faltan: **DOI archivable (Zenodo)**, manuscrito de 4-6 págs, docs en **inglés** y sección de **comparación con software similar**. |
| ¿El repo estuvo privado? | **No se puede probar desde fuera** (ver §1.1). Pero es **público hoy** y tiene **16 meses de historia**. Para JOSS esto **no es un obstáculo** en ningún escenario. |
| **Recomendación** | **Someter a JOSS primero.** Menor barrera, gratuito, y las brechas son ~2-3 semanas de trabajo. SoftwareX después/como alternativa si quieres el paper largo de benchmark (tiene APC de pago). |

---

## PARTE 1 — Elegibilidad para JOSS

### 1.1 Historial público real (HALLAZGO CRÍTICO)

**Lo que encontré en los datos:**

- Primer commit: **2025-02-07 12:19:27 -0600** (= `2025-02-07T18:19:27Z`).
- `created_at` del repo en GitHub (API pública): **`2025-02-07T18:19:27Z`** — **coincide al segundo** con el primer commit.
- Estado actual (API pública, sin autenticación): `"private": false`, `"visibility": "public"`. El repo **es clonable y navegable sin cuenta**.
- 240 commits, del 2025-02-07 al 2026-06-16. Actividad distribuida en **16 meses**.

**Interpretación honesta:** la coincidencia exacta entre `created_at` y el primer commit indica que el repo se **creó en GitHub y se hizo push de inmediato**. **Sin embargo, esto NO prueba que haya sido público todo el tiempo:** un repo puede crearse privado y volverse público después sin que `created_at` cambie, y la API REST pública **no expone el historial de cambios de visibilidad**. La única fuente autoritativa es **tu propio registro**: GitHub → repo → *Settings → ... → Audit log*, o tu *Security log* personal (`https://github.com/settings/security-log`), buscando el evento `repo.access` / "made public".

> **Acción para ti:** verifica en tu Security log la fecha exacta del cambio a público (si lo hubo). **Dime esa fecha y cierro el punto con certeza.**

**¿Importa para JOSS?** **No.** Aquí hay que corregir una premisa: **JOSS no tiene una regla formal de "6 meses de historial público".** Su criterio real es *"substantial scholarly effort"* (heurística ≈ >3 meses de trabajo o >1000 líneas, software mantenido y completo). Este repo tiene **16 meses de historia real y ~25 000 líneas** → supera el umbral con holgura **incluso en el peor caso** (que se hubiera hecho público ayer). El "miedo" de fondo (que un repo recién creado parezca prototipo) no aplica.

### 1.2 Evidencia de desarrollo abierto (issues / PRs / releases)

| Señal | Estado |
|---|---|
| Issues habilitados | ✅ `has_issues: true`, abiertos al público |
| Issues reales | ⚠️ **1 histórico** (cerrado, del bot Copilot), `open_issues: 0` |
| Pull Requests | ⚠️ **1** (rama `copilot/reorganize-repository-structure`) |
| Releases / tags | ❌ **0 tags, 0 releases** (verificado en git y API) |
| Estrellas / forks | 1 estrella, 0 forks, 0 adoptantes externos |
| Contribuidores | 1 autor real (175+57+6 commits, mismo autor con 2 emails) + bot |

**Lectura:** el desarrollo ha sido esencialmente **mono-autor y sin tráfico público de issues/PRs**. JOSS **no exige** comunidad ni adopción externa, pero sí valora que el proyecto **invite a la participación**. La ausencia total de releases es una **brecha concreta y fácil de cerrar**.

### 1.3 ¿Feature-complete (no prototipo)?

✅ **Sí.** Evidencia verificada:
- **Compila limpio**: `make` → `bin/hpsv`, **0 warnings**, salida `1.0.0`.
- **0 marcadores `TODO`/`FIXME`/`XXX`/`HACK` en el código fuente** (`src/`, `include/`).
- Funcionalidad amplia y no trivial: lectura NetCDF L1b/L2, gray/pseudocolor/RGB, 8 modos RGB, corrección Rayleigh (LUT + analítica), CLAHE, reproyección geoestacionaria→lat/lon, álgebra de bandas, salida PNG y **COG GeoTIFF**, sidecar JSON.
- **En producción desde abril** generando las vistas operativas de GOES en LANOT.

⚠️ **Un pendiente conocido** (anotado por ti en `docs/TODO.txt`, 2026-06-20): la malla de reproyección **rellena con 0 en lugar de `nodata`** ("inventar información que no existe"). Tú mismo lo marcaste como *"preciso corregirlo antes de publicar"*. **Debe cerrarse antes de someter** — es un punto de corrección científica que un revisor podría detectar.

### 1.4 ¿Software orientado a investigación con aplicación científica clara?

✅ **Sí, y fuerte.** No es un wrapper delgado: implementa física real (modelos de dispersión de Rayleigh de Bucholtz 1995 + Hansen & Travis 1974; LUTs de pyspectral/Scheirer 2018; síntesis de canal verde estilo geo2grid/satpy; CLAHE de Zuiderveld). Aplicación de teledetección operativa con referencias bibliográficas en el README. Encaja perfecto en el alcance de JOSS.

### 1.5 Statement of Need (borrador para el paper.md de JOSS)

> **Statement of need.** Geostationary platforms such as the GOES-R series stream
> ABI imagery at high cadence (full-disk every 10 min, CONUS every 5 min, mesoscale
> every 1 min). Operational centers must turn this raw NetCDF (L1b radiances, L2
> products) into human-interpretable views — grayscale, pseudocolor, and RGB
> composites such as true color, air mass, and ash — within the inter-scan interval.
> Established Python toolchains (satpy/geo2grid + GDAL) are flexible but impose
> latency and memory overhead that strain near-real-time operation on commodity
> hardware. `hpsatviews` fills this gap with a single, dependency-light C11/OpenMP
> command-line tool that produces georeferenced, metadata-rich views (PNG and
> Cloud-Optimized GeoTIFF) in seconds, embedding pyspectral-derived Rayleigh
> correction LUTs in the binary to remove external runtime dependencies. It has
> been in continuous operational use since April 2026 at the National Laboratory
> for Earth Observation (LANOT, UNAM), where it generates the laboratory's full
> set of operational GOES views, demonstrating its fitness for unattended,
> time-critical production pipelines on modest infrastructure.

*(Para JOSS basta este nivel: el "near-term significance" se sustenta en el uso operacional real, no en adopción de terceros, que JOSS explícitamente no exige.)*

### 1.6 Checklist de admisión JOSS

| Requisito | Estado |
|---|---|
| Repo público, clonable sin cuenta | ✅ |
| Issue tracker público habilitado | ✅ |
| Licencia OSI-aprobada | ✅ GPL-3.0 |
| Esfuerzo académico sustancial | ✅ 16 meses / 25k LOC |
| Software completo, no prototipo | ✅ (cerrar bug nodata) |
| Documentación: instalación | ✅ (pero en español) |
| Documentación: ejemplo de uso end-to-end | ✅ `reproduction/run_demo.sh` |
| Documentación: API / funcionalidad | ✅ (headers documentados + README) |
| **Docs en inglés** | ❌ README y ayuda principalmente en español |
| **Tests automatizados** | ⚠️ Existen scripts (`tests/`) pero sin aserciones claras de pass/fail ni CI |
| **Integración continua (CI)** | ❌ No hay workflows en `.github/` |
| **Guías de comunidad** (CONTRIBUTING, cómo reportar/pedir soporte) | ❌ Ausentes |
| **Release etiquetado** (vX.Y.Z) | ❌ 0 tags |
| Archivo DOI (Zenodo) | ❌ (JOSS lo genera en aceptación, pero conviene tenerlo) |

---

## PARTE 2 — Elegibilidad / requisitos para SoftwareX

### 2.1 ¿Documentación suficiente para un paper de 4-6 páginas?

✅ **El material existe**, ya estructurado a favor:
- `docs/plan_maestro_SoftwareX.md` y `docs/plan_publicar_SoftwareX.md`: ya tienes el esqueleto Motivation / Software description / Illustrative examples / Impact.
- `docs/presentacion_hpsatviews.md` (18 KB): base para la sección descriptiva.
- README §5 (Detalles técnicos) cubre geometría, Rayleigh, CLAHE, true color, rendimiento — material directo para *Software description*.
- Faltan: el **manuscrito LaTeX** en sí (plantilla Elsevier) y la **tabla Code Metadata** (parcialmente esbozada en el plan maestro §4.1).

### 2.2 Evidencia para la sección "Impact"

Tu uso en producción es el mejor activo. Partes concretas del repo que puedes citar como evidencia:

- **Outputs operativos**: las imágenes en la raíz (`hpsv_G19_2025310_1605_pseudo_*_geo.png`) y `tests/*.png` son ejemplos reales de productos generados.
- **Reproducibilidad verificable**: `reproduction/` (kit completo: `download_sample.sh` baja datos públicos de NOAA S3 sin credenciales, `run_demo.sh`, `expected_output/`). Esto es **oro para SoftwareX** — un revisor reproduce en <5 min.
- **Sidecar JSON de trazabilidad** (README §4.7): documenta exactamente los parámetros de cada salida → argumento de reproducibilidad/auditoría científica.
- **Narrativa de rendimiento**: el ángulo "30-120× más rápido que Python/GDAL" (de tu plan maestro). ⚠️ **Necesitas medirlo y graficarlo** (Figura: hpsv vs `gdal_translate` vs satpy). Hoy **no hay benchmark reproducible** en el repo — es lo que más falta para el *Impact* de SoftwareX.
- **Uso operacional LANOT desde abril 2026**: declaración institucional (idealmente con una frase de respaldo del laboratorio).

### 2.3 ¿Comparación con software similar?

⚠️ **Parcialmente.** Tienes material de comparación técnica (`docs/comparacion_g2g_marzo2026.md`, `docs/comparison_gdal_geo2grid.md`, `docs/INFORME_REALCES_GEO2GRID.md`, `tests/compara_gdal.sh`) que demuestran paridad de calidad con geo2grid/GDAL. **Falta consolidarlo** en una tabla comparativa de paper (hpsatviews vs satpy/geo2grid vs GDAL: lenguaje, dependencias, tiempo, COG nativo, etc.). Es armable en 1-2 días a partir de lo que ya existe.

---

## PARTE 3 — Auditoría técnica común (aplica a ambas)

| Ítem | Estado | Notas |
|---|---|---|
| README | ✅ Completo (516 líneas) | **En español** — bloqueante para ambas revistas |
| Instrucciones de instalación | ✅ | README §6 + `reproduction/README.md` (en inglés) + CLAUDE.md |
| Ejemplo end-to-end | ✅ | `reproduction/run_demo.sh` con datos públicos |
| Docstrings / API docs | ✅ | Headers en `include/` documentan la API (política explícita en TODO) |
| **Tests automatizados** | ⚠️ | `tests/*.sh` + `run_all_tests.sh`: son más *smoke/comparación visual* que aserciones automáticas. No fallan con código de error claro de forma evidente. |
| **CI configurado** | ❌ | `.github/` solo tiene `copilot-instructions.md`. Sin GitHub Actions. |
| Dependencias con versiones | ✅ (adecuado para C) | `codemeta.json` y `reproduction/README.md` fijan mínimos (libnetcdf≥4.6, libpng≥1.6, GDAL≥3.0, GCC≥4.9). No hay `requirements.txt` porque es C; el Makefile + versiones documentadas es la práctica correcta. |
| **LICENSE** | ✅ | GPL-3.0 completo (35 KB), OSI-aprobada |
| **CITATION.cff** | ✅ | Presente y bien formado. ⚠️ `date-released: 2025-01-01` es placeholder; falta DOI |
| **codemeta.json** | ✅ | Presente. ⚠️ `dateCreated: 2025-01-01` placeholder; falta DOI |
| Versionado semántico / releases | ❌ | Versión 1.0.0 en el binario, pero **sin tag ni release en GitHub** |
| **Conexión a Zenodo / DOI** | ❌ | No vinculado. Bloqueante para SoftwareX; recomendado para JOSS |
| Calidad de código | ✅ | 0 TODO/FIXME en fuente, compila con 0 warnings, sin código muerto detectado |
| **Higiene del repo** | ⚠️ | `docs/` mezcla ~25 documentos internos de planificación en español (planes, sprints, análisis). No bloquea, pero da aspecto de "cuaderno de trabajo". Conviene mover a `docs/dev/` o limpiar. PNGs grandes (20 MB c/u) versionados en la raíz: quitarlos. |

---

## PARTE 4 — Recomendación

### 4.1 Veredicto

- **JOSS — Parcialmente listo (cumple el núcleo).** Tiene lo difícil: software completo, en producción, esfuerzo sustancial de 16 meses, licencia OSI, repo público y navegable, aplicación científica clara. Faltan piezas **mecánicas y rápidas**: inglés, tests+CI, guías de comunidad, release. **El historial público NO es un problema** (JOSS no exige 6 meses públicos; tienes 16 meses de historia y el peor caso tampoco bloquea).
- **SoftwareX — Parcialmente listo, con más trabajo.** Requiere además **DOI Zenodo**, **manuscrito de 4-6 págs**, **benchmark reproducible** (hoy ausente) y **tabla comparativa**. Mayor esfuerzo de redacción y **conlleva APC (cargo de pago por publicación)**.

### 4.2 Esfuerzo estimado para cerrar brechas

| Brecha | JOSS | SoftwareX | Esfuerzo |
|---|---|---|---|
| Traducir README + ayuda a inglés | ✅ req. | ✅ req. | 3-5 días |
| Bug `nodata`=0 en reproyección | ✅ debe | ✅ debe | 1-3 días |
| Tests con aserciones + GitHub Actions CI | ✅ req. | recomendado | 2-4 días |
| Guías de comunidad (CONTRIBUTING, issue templates, soporte) | ✅ req. | opcional | 0.5-1 día |
| Release v1.0.0 + tag | ✅ req. | ✅ req. | 0.5 día |
| Vincular Zenodo → DOI | recomendado | ✅ req. | 0.5 día |
| Limpiar `docs/` + quitar PNGs grandes de la raíz | recomendado | recomendado | 0.5 día |
| `paper.md` (Statement of Need, ~250-1000 palabras) | ✅ req. | — | 1-2 días |
| Manuscrito LaTeX 4-6 págs (Elsevier) | — | ✅ req. | 1-2 semanas |
| Benchmark reproducible vs satpy/GDAL + figuras | opcional | ✅ clave | 3-5 días |
| Tabla comparativa con software similar | opcional | ✅ req. | 1-2 días |

- **Tiempo a JOSS-ready:** ≈ **2-3 semanas** a tiempo parcial.
- **Tiempo a SoftwareX-ready:** ≈ **5-7 semanas** a tiempo parcial (sobre todo manuscrito + benchmark).

### 4.3 Recomendación final: **JOSS primero**

Razones:
1. **Barrera más baja y gratuita.** JOSS no cobra APC; SoftwareX sí.
2. **Tus brechas son exactamente el checklist de JOSS** (inglés, tests/CI, comunidad, release) y son semanas, no meses.
3. **JOSS no penaliza la falta de adopción externa** — tu situación (producción interna, sin terceros aún) le cae perfecto; SoftwareX presiona más el *Impact*/novedad.
4. **El historial público no te frena en JOSS** bajo ninguna interpretación.
5. **Reutilizas casi todo para SoftwareX después**: el inglés, el release/DOI, el kit de reproducción y la comparación con geo2grid sirven para ambos. Puedes someter a SoftwareX como segundo paper (más extenso, con benchmark) si quieres la narrativa de rendimiento — JOSS y SoftwareX cubren un software con enfoques distintos, no se excluyen.

> **Matiz:** si tu prioridad es específicamente el paper de **rendimiento "30-120× más rápido"** con benchmark y figuras (y el APC no es problema), SoftwareX es el *venue* más natural para esa historia. Pero como **primera publicación y dado el estado actual**, JOSS es el camino de menor fricción y mayor probabilidad de aceptación rápida.

### 4.4 Lista priorizada de acciones (camino JOSS)

**🔴 CRÍTICO (bloquea la admisión):**
1. **Traducir a inglés** README + texto de ayuda (`include/help_en.h` ya existe — verificar que sea la ruta por defecto y completarlo) y los logs.
2. **Corregir el bug `nodata`=0** en la malla de reproyección (lo marcaste como must-fix).
3. **Tests automatizados con pass/fail real** + **GitHub Actions CI** que compile y corra `tests/run_all_tests.sh` en Ubuntu limpio.
4. **Crear release `v1.0.0`** con tag en GitHub.
5. **`paper.md`** con Statement of Need (usa el borrador de §1.5) + metadatos JOSS.
6. **Guías de comunidad**: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, plantillas de issues, y una sección "Support / Reporting issues" en el README.

**🟡 RECOMENDADO (fortalece la candidatura):**
7. Vincular **Zenodo** y obtener **DOI**; actualizar `CITATION.cff` y `codemeta.json` (corregir fechas placeholder `2025-01-01`).
8. **Limpiar `docs/`** (mover los ~25 planes/sprints internos a `docs/dev/`) y **quitar los PNGs de 20 MB de la raíz** (ya están en `.gitignore` pero presentes en disco; asegurar que no se versionen).
9. Habilitar/abrir algún **issue público** que documente el roadmap, para mostrar desarrollo abierto.

**🟢 OPCIONAL (pulido / útil para SoftwareX después):**
10. **Benchmark reproducible** vs satpy/geo2grid/GDAL con figura (reutilizable en SoftwareX).
11. **Tabla comparativa** con software similar (consolidar `docs/comparison_gdal_geo2grid.md`).
12. Badge de CI y de DOI en el README.

---

## Anexo — Evidencia recopilada

- **GitHub API (pública):** `private: false`, `visibility: public`, `created_at: 2025-02-07T18:19:27Z`, `has_issues: true`, `open_issues: 0`, 1 star, 0 forks, 0 releases.
- **Git:** 240 commits (2025-02-07 → 2026-06-16); 0 tags; ramas `main`, `onlyl2`, `copilot/reorganize-repository-structure`; 1 autor real (2 emails) + bot Copilot.
- **Build:** `make` exitoso, `bin/hpsv 1.0.0`, **0 warnings**.
- **Código:** ~25 160 líneas C/H; **0** `TODO/FIXME/XXX/HACK` en fuente.
- **Licencia:** GPL-3.0 (OSI). **CITATION.cff** y **codemeta.json** presentes (con fechas placeholder, sin DOI).
- **Reproducibilidad:** kit completo en `reproduction/` con descarga de datos públicos NOAA S3.
- **Pendiente técnico declarado:** relleno con 0 en lugar de `nodata` en reproyección (`docs/TODO.txt`, 2026-06-20).
</content>
