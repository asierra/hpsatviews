# Plan de escritura — Earth Science Informatics

Sustituye al plan de JTECH, que quedó como `lanot/tmp/plan_jtech.md` en bucéfalo.
Este vive en el repo a propósito: para que no vuelva a quedarse varado en un host.

Manuscrito base: `hpsatviews_jtech.tex`. Se reencuadra, no se reescribe: el grueso
del texto sirve tal cual.

Datos de revista verificados el 2026-09-02 contra las páginas oficiales de
*Submission guidelines* y *Aims and scope*.

---

## 0. Decisiones ya tomadas

- **Destino: Earth Science Informatics** (Springer, híbrida). Descartado JTECH
  por riesgo de rechazo por alcance: ninguna de las cinco contribuciones del
  manuscrito es un hallazgo atmosférico, y ya hubo un rechazo por ese flanco en
  JOSS.
- **SoftwareX sigue vivo**, así que los dos manuscritos deben quedar sin
  traslape. La §4.2 de `hpsatviews_softwarex.tex` («Measured performance») es
  hoy el paper de ESIN comprimido y hay que recortarla.
- **ESIN sale primero.** SoftwareX cita a ESIN para las cifras, así que ESIN
  necesita existir al menos como preprint con DOI antes de enviar SoftwareX.
- **JOSS queda como plan B del paper de software, no como paper adicional.**
  Publicar en ESIN cura el motivo del rechazo: JOSS trata ese rechazo de
  escritorio como «not yet, rather than never» y admite reenvío a los seis meses
  o más, y su política permite expresamente la co-publicación junto a un trabajo
  que describa «details of algorithm development, and/or methods assessment»,
  que es justo lo que es el paper de ESIN. Pero JOSS y SoftwareX son el mismo
  tipo de artículo —la descripción del software—, así que enviar ambos sería
  publicación duplicada: hay que elegir uno. Se mantiene SoftwareX porque ya
  está escrito y sin TODOs, mientras JOSS exigiría recortar a 250–1000 palabras
  y esperar. Si SoftwareX rechaza por la misma política que JOSS, entonces sí:
  para ese momento ESIN ya existe y el motivo original está curado.

## 1. La advertencia de alcance — leer antes que nada

El alcance de ESIN cubre «todos los aspectos de las aplicaciones de cómputo a la
adquisición, almacenamiento, **procesamiento** e **visualización** de datos […]
sobre los cinco componentes del sistema terrestre (**atmósfera**, …)». Hasta ahí,
encaje directo. Pero la misma página cierra con un párrafo restrictivo:

> ESIN *primarily publishes research whose principal contribution is to the field
> of Earth science informatics*. Manuscripts focused predominantly on application
> domains that are well served by specialized disciplinary journals — including
> agriculture, **remote sensing**, geography, geotechnical engineering, petroleum
> engineering, **meteorology**, wildfire research, **image-classification
> applications**, and related areas — may fall outside the journal's scope.

Percepción remota, meteorología y aplicaciones de imagen están **nombradas en la
lista de exclusión**. Esto no descarta el artículo, pero sí eleva el reencuadre de
la §2 de retoque a condición de supervivencia:

- Si el manuscrito se lee como «hicimos más rápida una herramienta de imágenes
  GOES», cae en el saco excluido y es rechazo de escritorio.
- Si se lee como «caracterizamos el costo del índice de chunks en almacenamiento
  científico, la organización residente en dispositivo, y el protocolo para medir
  ambos», la contribución principal *es* a la informática y está dentro.

La diferencia es de encuadre, no de contenido: las cuatro contribuciones ya son
computacionales. Pero hay que escribirlas como tales desde el título.

**Plan B:** si aun así hay rechazo por alcance, el destino siguiente es
*Computers & Geosciences*, que no exige que la contribución sea a la informática
*como campo* y tolera mejor «métodos computacionales aplicados a geociencias».

## 2. El reencuadre

La tesis de JTECH era operativa: *el plazo manda, y el intervalo entre archivo e
imagen decide si el producto informa una decisión*. Para ESIN el plazo sigue
sirviendo como motivación, pero deja de ser la tesis.

**Tesis para ESIN:** en las tuberías de producto geoestacionario el cuello de
botella no está en la aritmética sino en el acceso a almacenamiento por chunks y
en la precisión que exige la geolocalización; los dos son caracterizables, los
dos son removibles sin tocar los algoritmos, y medirlos correctamente es más
difícil de lo que parece.

Movimientos concretos:

1. Subir en jerarquía el hallazgo del índice de chunks de HDF5. Hoy es la
   contribución 1 de 5; para esta audiencia es *el* resultado, porque aplica a
   cualquier consumidor de NetCDF-4 por chunks, no solo a GOES.
2. Bajar en jerarquía la comparación contra geo2grid. Sigue siendo necesaria como
   validación, pero deja de ser el titular: un lector de informática quiere el
   mecanismo, no el marcador. Además, un titular de comparación de herramientas
   se parece peligrosamente a un paper de percepción remota.
3. Añadir exposición de dominio. Un revisor de JTECH sabe qué es el ABI; uno de
   ESIN no necesariamente. Esto agranda la sección de algoritmos, no la achica.
4. Vigilar el tono sobre satpy/geo2grid. Son la referencia de la comunidad
   pytroll y sus desarrolladores son revisores plausibles. El borrador ya se
   porta bien —barre `--num-workers` y cita su mejor tiempo, no el default—;
   conservar ese cuidado y evitar que «esa amplitud es justo lo que una tubería
   operativa no necesita y sí paga» suene a descalificación.

### Título

El actual lidera con lo operativo y con GOES, justo lo que la advertencia de
alcance castiga. Opciones que ponen la informática al frente:

- «Quadratic chunk-index cost in chunked scientific storage: characterization,
  removal, and a reproducible benchmarking protocol»
- «Where the time goes in geostationary product pipelines: chunk-index cost,
  device-resident processing, and a measured protocol»
- «Characterizing I/O and precision bottlenecks in chunked-HDF5 Earth
  observation pipelines»

Decidir al final, junto con el abstract.

## 3. Tipo de artículo y estructura destino

**Enviar como *methodology article*, subtipo computational.** Es la decisión
estructural clave, y la revista prescribe subsecciones concretas:

> El *Method section* de un methodology article computacional «may be divided
> into the **'Algorithm', 'Testing', and 'Implementation'** subsections», con
> detalle suficiente para que otros reproduzcan el método.

Eso mapea limpiamente sobre lo que ya existe, y de paso resuelve el hueco de la
§2 vacía: deja de ser sección suelta y se vuelve *Methods/Algorithm*.

| Sección ESIN | Fuente en `hpsatviews_jtech.tex` | Estado |
|---|---|---|
| Title page (+ ORCID, autor de correspondencia) | l. 26–35 | Ajustar formato |
| Abstract, 150–250 palabras | l. 38 | Reescribir **al final** |
| Keywords, 4–6 | — | **NUEVO**, trivial |
| 1. Introduction (incluye estado del arte) | §1, l. 77–155 | Adaptar: reencuadre + citas |
| 2. Methods / **Algorithm** | §2, l. 156 (vacía) | **ESCRIBIR** |
| 3. Methods / **Implementation** | §3, l. 167–300 | Reutilizable casi literal |
| 4. Methods / **Testing** | §4, l. 301–385 | Reutilizable |
| 5. Results | §5, l. 386–487 | Reutilizable |
| 6. Discussion | §6, l. 488–603 | Reutilizable |
| 7. Conclusions | §7, l. 604–637 | Ajustar al nuevo encuadre |
| List of abbreviations | — | **NUEVO**, barato |
| Statements and Declarations | `\datastatement`, l. 649 | **ESCRIBIR**, ver §5 |

**Corrección respecto a la versión previa de este plan:** no hace falta una
sección de *Related work* aparte. La estructura recomendada por la revista no la
lista, y el estado del arte puede vivir en la Introducción, como ya está. Lo que
sí hace falta es engrosar la bibliografía (§6). Tampoco hay límite de palabras ni
de páginas publicado para research o methodology articles — el único tope es de
1000 palabras, y aplica solo a los *Comments*.

Máximo **tres niveles** de encabezado. Nada está bloqueado por mediciones: los
números están cerrados. Esto es trabajo de escritura.

## 4. Huecos de escritura, en orden de esfuerzo

### 4.1 Methods / Algorithm (la §2 vacía, l. 156)

Doble trabajo: fijar la línea base de equivalencia para que los Resultados puedan
afirmar que se compara *el mismo producto*, y dar el contexto de dominio que la
audiencia no trae. Guion ya anotado en el `.tex`:

- Verde sintético `G = 0.465 B + 0.465 R + 0.07 NIR`, coeficientes CIMSS.
- Corrección de Rayleigh por LUT derivada de pyspectral; relajación por nubes.
- Estiramiento por tramos y realce por razón, los de geo2grid.

Añadir para ESIN: qué es el ABI y su cadencia, qué es un compuesto de color
verdadero y por qué hay que sintetizar el verde, qué significa reproyectar de
malla fija a geográfica. Insistir en la tesis: **los algoritmos son los
publicados; lo que cambia es el sustrato.**

### 4.2 Reencuadre de la Introducción

Reordenar las cinco contribuciones para que la del índice de chunks encabece, y
declarar explícitamente que la contribución principal es a la informática, no al
dominio. Es la defensa directa contra el párrafo de la §1 de este plan.

### 4.3 Abstract, keywords y título

Al final. El abstract actual está construido sobre el encuadre operativo y hay
que rehacerlo sobre la tesis de la §2. Verificar que caiga en 150–250 palabras y
que no tenga abreviaturas sin definir ni referencias — el actual usa «ABI» y
«GPU» sin expandir. Keywords: elegir 4–6 que apunten a informática, no a
percepción remota.

## 5. Statements and Declarations

Obligatorias: **«submissions that do not include relevant declarations will be
returned as incomplete»**. Van bajo ese encabezado, antes de la lista de
referencias.

- **Data availability:** GOES-R ABI L1b del bucket público de NOAA en S3, sin
  credenciales.
- **Code availability:** hpsatviews v1.1.0 archivado, DOI
  `10.5281/zenodo.21893553`; DOI de concepto `10.5281/zenodo.20817973`.
  `reproduction/bench_geo2grid.sh` y `reproduction/compare_g2g_product.sh`
  reproducen las tablas.
- **Funding:** Laboratorio Nacional SECIHTI 2025–2027, LN-2025-C-102.
- **Competing interests.**
- **Author contributions**, en formato CRediT (Conceptualization, Methodology,
  Formal analysis and investigation, Writing – original draft, Writing – review
  and editing, Funding acquisition, Resources, Supervision). Bloqueado por la
  decisión de coautores.

**Ojo con esto:** *Author Contributions* y *Competing Interests* hay que
capturarlas además en la interfaz de envío, y **solo lo capturado ahí sale en la
versión publicada**. Ponerlas únicamente en el `.tex` no basta.

### Política de LLM — decisión pendiente tuya

La revista es explícita: un LLM no puede ser autor, y «el uso de un LLM debe
documentarse apropiadamente en la sección de Methods». Exime únicamente la
«edición de copia asistida por IA», que define como mejoras de legibilidad,
estilo, gramática y tono sobre texto generado por humanos — y que **excluye
expresamente el trabajo editorial generativo y la creación autónoma de
contenido**.

Parte del borrador se produjo con asistencia que va más allá de corrección de
estilo. Es tu decisión cómo declararlo, pero conviene resolverlo antes de enviar
y no después: la exigencia de rendición de cuentas humana sobre el texto final
recae en los autores.

## 6. La bibliografía es el hueco mayor

`paper.bib` tiene 11 entradas y el borrador de JTECH cita **cuatro**. Para un
methodology article en Springer eso no pasa revisión.

Faltan además dos citas que el texto ya está pidiendo a gritos:

- **El ABI no está citado.** La introducción abre con «The Advanced Baseline
  Imager aboard the GOES-R series» sin referencia. Falta Schmit et al.
- **satpy y dask no están citados**, y el barrido de `--num-workers` de la
  sección de Testing es literalmente un argumento sobre el planificador de dask.

Cuatro bloques a cubrir, y el orden importa porque el segundo y el cuarto son los
que sostienen el encuadre informático:

1. Acceso y rendimiento de almacenamiento por chunks: HDF5, NetCDF-4, y los
   formatos orientados a nube (COG, Zarr) que motivan omitir la pirámide.
2. Reproducibilidad y metodología de benchmarking en ciencia computacional — hoy
   sin ninguna cita, y es el anclaje del protocolo.
3. Aceleración en GPU aplicada a geociencias.
4. Software de procesamiento geoestacionario: satpy/pytroll, geo2grid.

Verificar cada entrada contra la fuente real; nada reconstruido de memoria.
Arrancar en paralelo con la escritura del Algorithm: es la tarea más larga.

## 7. Mediciones preventivas, opcionales

No bloquean, pero un revisor de informática las va a pedir:

- **Escalamiento fuerte de la implementación OpenMP** (tiempo contra número de
  hilos). Hay barrido de workers de geo2grid pero ninguna curva propia. Es barato
  con los scripts de `reproduction/` que ya existen.
- **Más de una escena o producto.** Hoy todo es un disco completo de color
  verdadero. Si no se mide, dejarlo explícito en Limitaciones.

## 8. Formato y envío

- **LaTeX sí se acepta** para manuscritos con contenido matemático; recomiendan
  la plantilla LaTeX de Springer Nature (`sn-jnl`). Word es la vía por defecto.
- Hay que entregar **fuentes editables** en cada envío y cada revisión; no
  hacerlo impide que el artículo pase a revisión.
- Se suelta `ametsocV6.1.cls`, que hoy impide compilar el manuscrito en lanot7.
- Figuras a color **sin costo**, ni en línea ni en impreso.
- **Costo:** la revista es híbrida, así que la vía de suscripción no cuesta. Y la
  página detecta afiliación UNAM y anuncia que **hay fondos disponibles para
  publicación en acceso abierto** (acuerdo vía CONRICYT). Confirmar cobertura
  concreta para ESIN con la biblioteca antes de elegir Open Choice.

## 9. Orden de trabajo propuesto

1. **Retargeting**: renombrar a `hpsatviews_esin.tex`, pasar a `sn-jnl`,
   reorganizar en Algorithm/Testing/Implementation. Desbloquea compilar aquí.
2. **Methods / Algorithm** — el hueco real de escritura.
3. **Bibliografía** — en paralelo desde el inicio, es lo más largo.
4. **Reencuadre de Introducción** y ajuste de Conclusiones.
5. **Declarations**, keywords y lista de abreviaturas — mecánicos.
6. **Abstract y título** — al final.
7. **Recorte de §4.2 de SoftwareX** — cuando ESIN tenga DOI de preprint.

## 10. Bloqueos y pendientes

- **Coautores sin decidir** (`hpsatviews_jtech.tex`, l. 29). Pendiente desde el
  resumen de la RAUGM. Bloquea *Author contributions* y la portada.
- **Declaración de uso de LLM** — decisión de los autores, ver §5.
- **ORCID** de los autores para la portada.
- **Respuesta de la RAUGM** todavía sin llegar. No bloquea nada de este plan.
- **`plan_jtech.md` en bucéfalo**: revisar si tiene pendientes que este plan no
  recoja.
- **Cobertura del APC por el acuerdo UNAM**, si se opta por acceso abierto.

---

## 11. Estado — retomar aquí

Última sesión: **2026-09-02**, en lanot7. Todo empujado a `origin/main` en el
commit `d71c032`.

### Hecho

Paso 1 del orden de trabajo, completo:

- `git mv hpsatviews_jtech.tex hpsatviews_esin.tex` — la historia sigue al
  archivo, y la versión de JTECH se recupera del repo si hace falta.
- Suelta la clase de AMS. Compila con `article`, **sin una sola macro propia de
  clase en el cuerpo**, para que el paso a `sn-jnl` sea nada más el preámbulo.
- Reorganizado en la estructura que ESIN prescribe para methodology article:
  `Methods` con `Algorithm` / `Implementation` / `Testing`. Tres niveles de
  encabezado, que es el tope de la revista.
- Andamiaje con TODOs: bloque completo de *Statements and Declarations* (Funding
  ya redactado, el resto pendiente), *List of abbreviations*, línea de keywords,
  y las tres alternativas de título comentadas sobre el título actual.
- El *significance statement* de AMS quedó comentado, no borrado: sirve casi tal
  cual para la carta de presentación.
- `.gitignore` al día: fuera AMS, dentro `sn-jnl` y los artefactos de LaTeX.

Verificado: `pdflatex` + `bibtex` compilan limpio, 15 páginas, sin referencias ni
citas indefinidas. Toda la prosa referencia secciones por `\ref`, ninguna por
número escrito a mano, así que la reorganización no rompió ninguna referencia
cruzada.

### Lo primero al llegar a bucéfalo

**Rescatar `lanot/tmp/plan_jtech.md`**, que vive en esa máquina y no en el repo.
Revisar si tiene pendientes que este plan no recoja e incorporarlos aquí. Fue
justo por estar fuera del repo que se perdió de vista; no repetir el patrón.

Si quieres el build de envío en bucéfalo: `sudo apt install texlive-publishers`,
y cambiar la línea de `\documentclass` según la cabecera del `.tex`.

### Siguiente paso

Paso 2, **Methods/Algorithm** — la sección vacía, el único hueco de escritura
real. En paralelo conviene arrancar el paso 3, la bibliografía, que es lo más
largo.

### Nota sobre el trabajo que venía de bucéfalo

Los commits `a3cf22b`, `76e146a` y `3d68276` añadieron `--timing-csv`, que
acumula tiempos en 11 etapas y los escribe en CSV para comparar el build de
OpenMP contra el de CUDA columna por columna. Es la instrumentación que alimenta
la tabla de tiempo por etapa de Resultados, y abarata la curva de escalamiento
fuerte de OpenMP que la §7 deja como medición preventiva opcional.

`CLAUDE.md` trae ahora una advertencia asociada: una etapa cronometrada en un
build y no en el otro sesga esa columna en silencio, y ya ocurrió una vez. Si se
añaden etapas para el paper, etiquetar los sitios de CPU y GPU juntos.
