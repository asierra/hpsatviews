# Plan de escritura — Earth Science Informatics

Sustituye al plan de JTECH, que quedó como `lanot/tmp/plan_jtech.md` en bucéfalo.

**Ubicación (desde el 2026-09-02):** este plan, `hpsatviews_esin.tex` y la copia
de trabajo de `paper.bib` viven en `~/Dropbox/cca/lanot/vistas/paper/`, fuera del
repo, para no meter ruido en él. Antes vivían en `hpsatviews/paper/` justo para
que no se quedaran varados en un host; Dropbox cumple ese propósito igual en las
máquinas de trabajo —**bucéfalo, pegaso y lanot7**— y deliberadamente no en los
servidores de medición (tahan, tsom04, kawak), donde no hace falta.

**El `paper.bib` de Dropbox es el que manda.** El del repo se queda porque
`hpsatviews_softwarex.tex` y el `paper.md` de JOSS aún lo usan; cuando toque
SoftwareX se porta lo que haga falta. No editar el del repo pensando en ESIN.

En `referencias/` hay artículos de contexto. El del **HDCRS** (grupo de trabajo
de HPC y percepción remota del Earth Science Informatics Technical Committee del
GRSS) sirve para el bloque 3 de la bibliografía del §6.

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

**Plan B — corregido el 2026-09-02.** La versión previa de este plan decía que
el destino siguiente era *Computers & Geosciences*, «que no exige que la
contribución sea a la informática *como campo*». **Eso era falso, escrito sin
verificar.** C&G tiene su propia lista de exclusión de nueve puntos, y dos caen
encima de la tesis de este artículo:

> 5. «Manuscripts aiming at solving a geoscientific *engineering problem rather
>    than answering a scientific question*»
> 6. «*Standard code of already well-established, or previously published
>    methods*»

El punto 6 es casi una cita literal de nuestra frase de encuadre —«los algoritmos
son los publicados; lo que cambia es el sustrato»— y el 5 describe una
caracterización de rendimiento. Su punto 1 exige además «a significant computer
science innovation». O sea que **C&G exige las dos cosas a la vez** (innovación en
cómputo *y* pregunta científica), mientras ESIN solo exige la primera: para este
manuscrito C&G es **más** restrictivo, no menos. A favor solo juega su punto 9,
rechazo de escritorio para código cerrado, que no nos toca.

**Plan B real: *Applied Computing & Geosciences*** (Elsevier, OA, la revista
hermana a la que el propio C&G remite). Su alcance nombra explícitamente *Remote
Sensing*, *Near and Remote Sensing Data Analysis*, *Parallel Systems*, *Data
Processing* y *Software Engineering* — justo lo que ESIN pone en su lista de
exclusión. Verificado contra la *Guide for authors* oficial el 2026-09-02; los detalles
están abajo.

Orden de destinos, entonces: **ESIN → Applied Computing & Geosciences → C&G**,
y C&G solo si se reescribe alrededor de una pregunta científica, no de una
medición.

### Requisitos de ACAG, verificados contra la guía oficial

Leída del HTML descargado por el usuario el 2026-09-02, porque sciencedirect.com
bloquea el acceso automatizado (HTTP 403), igual que el sitio de AMS. Las citas de
**C&G** de más arriba siguen siendo de segunda mano —un espejo del alcance y una
búsqueda que coinciden—; eso no se ha verificado contra la página oficial.

**Corrección:** una versión previa de este plan le atribuyó a ACAG un tipo
*Original Software Publication*. **No existe**; salió de la lista genérica de
Elsevier, no de la revista. Los tipos reales son tres:

| Tipo | Tope | Encaje |
|---|---|---|
| Original research article | **5 000 palabras** | **el nuestro** |
| Application article | 5 000 | no: somos método, no caso de uso |
| Scientific review article | 10 000 | no |

Desaparece con eso el riesgo de traslape con SoftwareX que este plan temía: no
hay género de «paper de software» que colisione.

**El límite de 5 000 palabras es la restricción operativa.** El borrador iba en
4 231 y este plan empuja en la dirección contraria: exposición de dominio (§4.1),
introducción reencuadrada, bibliografía mayor. ESIN no publica tope; ACAG sí. Si
ACAG sigue vivo como plan B, **contar palabras desde ahora**, no al final. (La
guía no aclara si el tope incluye referencias y resumen.)

**Requisito nuevo, y más duro que el de ESIN — datos, *Option C*:** obligatorio
*depositar* los datos en un repositorio y *citarlos y enlazarlos* en el artículo,
o explicar por qué no se puede. No basta la declaración de disponibilidad del §5.
El código ya está en Zenodo, pero **los CSV de `--timing-csv` y los resultados de
`reproduction/` que sostienen las tablas habría que depositarlos**. Tarea concreta
que este plan no tenía. La guía trae ejemplo de *reference to software* con DOI de
Zenodo, así que nuestro propio archivo se cita como referencia normal.

**Tres cosas a favor:**

- **Preprint gratuito en SSRN con DOI** al enviar, publicado en cuanto pasa el
  filtro de escritorio y sin efecto sobre la decisión editorial. Resuelve tal cual
  el requisito del §0: que este paper exista como preprint con DOI antes de mandar
  SoftwareX.
- **La declaración de LLM deja de ser decisión abierta y pasa a ser formulario.**
  Elsevier da la frase textual, en sección propia titulada «Declaration of
  generative AI and AI-assisted technologies in the manuscript preparation
  process», antes de las referencias, y **exime explícitamente** gramática,
  ortografía y gestión de referencias. Mucho más accionable que el «documéntese en
  Methods» de ESIN; ver §5.
- **Highlights** (3–5 viñetas, ≤85 caracteres) y **resumen gráfico**
  (531×1328 px), ambos opcionales. La figura de presupuesto de tiempo del §7.5
  sirve de resumen gráfico sin trabajo extra.

**Compatibles sin cambios:** resumen ≤250 palabras (ESIN pide 150–250), 1–7
palabras clave evitando las de varias palabras (ESIN pide 4–6), LaTeX aceptado con
plantilla de Elsevier, CRediT obligatorio, secciones numeradas 1 / 1.1 / 1.1.1.

**Lo que sí cambia:** estilo de referencias **numérico `[1]` por orden de
aparición**, contra el autor-año de Springer. El trabajo de bibliografía del §6 es
portable; el estilo no.

**Costo: 2 230 USD de APC**, confirmado el 2026-09-02 contra la página de la
revista (la *Guide for authors* no lo trae; remite a una página aparte). ACAG es
OA **obligatorio**, así que aquí no existe la vía de suscripción gratuita que sí
tiene ESIN por ser híbrida: **el plan B cuesta 2 230 USD salvo que lo cubra el
acuerdo UNAM/CONRICYT o una exención**. Eso convierte la consulta a biblioteca
del §8 en condición para el plan B, no en trámite opcional — y conviene
preguntarlo de una vez por las dos revistas, no cuando haga falta.

Confirmado también: el *Article Transfer Service* desde C&G hacia ACAG existe y
está descrito en la guía.

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

## 7.5 Figuras — el manuscrito hoy no tiene ninguna

Recogido de `plan_jtech.md`. El `.tex` tiene tres tablas y **cero
`\includegraphics`**. Es un hueco de trabajo real, no un detalle de formato: dos
de las candidatas sostienen justamente la tesis informática.

**Lo que NO va, y ya estaba decidido:** `processing_workflow` describe lo que
hace el software, no sostiene ningún argumento del artículo, y el texto más la
tabla de etapas ya orientan al lector. El panel de productos (`radiometric.jpg`)
es catálogo, no evidencia. Ambas son las figuras correctas para SoftwareX.

Candidatas, por valor:

- [ ] **Presupuesto de tiempo**: barras apiladas de decodificación / cómputo /
      escritura, para CPU y GPU. Sostiene «el problema dejó de estar limitado por
      cómputo», que hoy vive solo en prosa. Para ESIN es *la* figura: es el
      argumento de que el cuello de botella es de acceso a almacenamiento.
- [ ] **Desglose por etapa T4 vs A30**, agrupado por precisión. El resultado más
      fuerte, y en tabla se lee peor de lo que se vería en barras: el contraste
      entre el grupo `double` y el grupo `float` es visual.
- [ ] **Barrido de workers**, si sobra espacio. Hoy funciona bien como tabla;
      solo vale la pena si se quiere subrayar la meseta. En ESIN, además, es
      material del planificador de dask, así que se apoya en la cita nueva.
- [ ] **Escena de caso**, no catálogo: una sola escena operativa real (Otis o el
      Popocatépetl) para la motivación de la Introducción. Prescindible aquí,
      donde lo operativo dejó de ser la tesis.

Figuras a color sin costo (§8), así que nada empuja a recortarlas.

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
  concreta con la biblioteca antes de elegir Open Choice — y preguntar **por las
  dos revistas de una sola vez**: en ESIN el acceso abierto es opcional, pero en
  el plan B (ACAG, Elsevier) es obligatorio y son 2 230 USD, así que ahí la
  respuesta decide si el plan B es viable. Convenios distintos: Springer vs
  Elsevier.

## 9. Orden de trabajo propuesto

1. **Retargeting**: renombrar a `hpsatviews_esin.tex`, pasar a `sn-jnl`,
   reorganizar en Algorithm/Testing/Implementation. Desbloquea compilar aquí.
2. **Methods / Algorithm** — el hueco real de escritura.
3. **Bibliografía** — en paralelo desde el inicio, es lo más largo.
4. **Reencuadre de Introducción** y ajuste de Conclusiones.
5. **Declarations**, keywords y lista de abreviaturas — mecánicos.
6. **Figuras** (§7.5) — hay que producirlas; las dos primeras son argumento,
   no adorno.
7. **Abstract y título** — al final.
8. **Relectura sección por sección con la prueba de encuadre en la mano:** *¿la
   contribución principal de este párrafo es a la informática, o suena a
   percepción remota?* Es el equivalente para ESIN de la prueba que traía el
   plan de JTECH, y la defensa última contra el rechazo de la §1.
9. **Recorte de §4.2 de SoftwareX** — cuando ESIN tenga DOI de preprint.

## 10. Bloqueos y pendientes

- **Coautores sin decidir** (`hpsatviews_jtech.tex`, l. 29). Pendiente desde el
  resumen de la RAUGM. Bloquea *Author contributions* y la portada.
- **Declaración de uso de LLM** — decisión de los autores, ver §5.
- **ORCID** de los autores para la portada.
- **Respuesta de la RAUGM** todavía sin llegar. No bloquea nada de este plan.
- **Sesgo de −2.4 DN en el rojo, sin verificar** (`hpsatviews_esin.tex`,
  l. 503–509 y §`sec:redbias`). La hipótesis del texto —que viene de la
  corrección atmosférica, única etapa que actúa solo sobre ese canal— está
  razonada pero no comprobada. Si se investiga y resulta ser otra cosa, hay que
  cambiar el párrafo. No bloquea el envío; sí es lo primero que un revisor va a
  picar.
- ~~**`plan_jtech.md` en bucéfalo**: revisar si tiene pendientes que este plan no
  recoja.~~ Hecho el 2026-09-02; lo rescatado está en la §7.5, en el pendiente
  del rojo de arriba, y en la nota de FP64 de la §11.
- **Cobertura del APC por el acuerdo UNAM**, si se opta por acceso abierto.
- **Solo si se activa el plan B (ACAG):** depositar y citar los CSV de tiempos y
  los resultados de `reproduction/` (su política de datos es *Option C*,
  obligatoria), y vigilar el tope de 5 000 palabras. Ver §1.

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

### Rescate de `plan_jtech.md` — HECHO

Revisado en bucéfalo el 2026-09-02. Casi todo estaba superado (destino y
encuadre, `\datastatement`, coautores, cargos por página de AMS, medición de la
T4 ya hecha). Lo que **no** recogía este plan, ya incorporado arriba:

1. Las figuras, que aquí no se mencionaban en absoluto — §7.5.
2. El sesgo del rojo sin verificar — §10.
3. La corrección de FP64 — nota al final de esta sección.
4. La relectura con la prueba de encuadre — paso 8 de la §9.

El archivo original queda en `lanot/tmp/` en bucéfalo, ya sin contenido único.

Si quieres el build de envío en bucéfalo: `sudo apt install texlive-publishers`,
y cambiar la línea de `\documentclass` según la cabecera del `.tex`.

### Sesión del 2026-09-02 en bucéfalo — paso 2 HECHO

**Methods/Algorithm escrita e integrada** (798 palabras, seis apartados):
instrumento y malla fija; el producto y la síntesis del verde; preparación
radiométrica y Rayleigh con relajación por nubes; realce por razón; curva de
contraste; reproyección. Cierra con la tesis del sustrato y remite a
`sec:equivalence`.

Dos detalles se sacaron **del código, no del plan**, y quedaron con cifras
exactas: la curva de contraste son cinco puntos de control, en cuentas de 8 bits
$(0,0)$, $(25,90)$, $(55,140)$, $(100,175)$, $(255,255)$
(`GEO2GRID_STRETCH_X/Y` en `src/truecolor.c`); y el realce por razón es el
cociente de la banda roja contra su media de caja $2\times2$, acotado a
$[0.5,1.5]$, que multiplica verde y azul (`dataf_ratio_sharpen_map` +
`src/rgb.c:220`). Las mismas constantes alimentan CPU y GPU, lo que de paso
refuerza el argumento de equivalencia.

Verificado: `pdflatex` + `bibtex` compilan limpio, **17 páginas** (antes 15), sin
citas ni referencias indefinidas.

**Conteo de palabras: 5 374** (de Introducción a bibliografía, sin comentarios).
ESIN no publica tope, así que el plan A no se ve afectado. Pero **ya excede las
5 000 de ACAG** y el §4.2 todavía pide engordar la introducción: si el plan B se
activa, el recorte no será cosmético. Medir con:

```
python3 -c "import re;s=open('hpsatviews_esin.tex').read();s=re.sub(r'(?m)^\s*%.*$','',s);b=s[s.index(r'\section{Introduction}'):];print(b[:b.index(r'\bibliography')])" > /tmp/body.tex && detex /tmp/body.tex | wc -w
```

### Siguiente paso

Paso 3, **la bibliografía** — ahora es el hueco mayor y el más largo. La sección
nueva ya consume las citas que había (`Bah2018`, `Miller2016`, `Bucholtz1995`,
`Bodhaine1999`, `HansenTravis1974`, `Scheirer2018`, `PySpectralLUT2018`), así que
de las 11 entradas quedan pocas sin usar y siguen faltando los cuatro bloques del
§6 — con Schmit et al. para el ABI y satpy/dask como los más urgentes.

### Nota sobre el trabajo que venía de bucéfalo

Los commits `a3cf22b`, `76e146a` y `3d68276` añadieron `--timing-csv`, que
acumula tiempos en 11 etapas y los escribe en CSV para comparar el build de
OpenMP contra el de CUDA columna por columna. Es la instrumentación que alimenta
la tabla de tiempo por etapa de Resultados, y abarata la curva de escalamiento
fuerte de OpenMP que la §7 deja como medición preventiva opcional.

`CLAUDE.md` trae ahora una advertencia asociada: una etapa cronometrada en un
build y no en el otro sesga esa columna en silencio, y ya ocurrió una vez. Si se
añaden etapas para el paper, etiquetar los sitios de CPU y GPU juntos.

### Nota rescatada: el cociente de FP64 es 37.4×, no 16×

Viene de `plan_jtech.md` y vale la pena no perderla, porque corrige un error que
casi entra al artículo. El razonamiento «la T4 es 1:32 en FP64 y la A30 es 1:2,
luego el cociente es 16×» es **falso**: ignora que las dos tarjetas también
difieren en FP32. Medido con `reproduction/gpu_precision_ratio.cu` (FMA sostenida
en ambas precisiones): T4 3834/101 GFLOP/s (38.0×), A30 7546/3774 (2.0×); de ahí
A30/T4 = FP32 1.97×, FP64 **37.4×**, ancho de banda 2.92×.

La lectura correcta, ya en el texto: las etapas `float` siguen el FP32 casi
exacto (1.9 observado contra 1.97 teórico) y no el ancho de banda; las `double`
quedan muy por debajo del 37.4×, o sea que están penalizadas por FP64 pero no
gobernadas solo por ella — también escriben rejillas grandes y evalúan
trascendentes. El 15.4× observado cae entre 2.92× y 37.4×, que es donde debe caer
algo mixto. Si se reescribe ese párrafo al reencuadrar, no volver al 16×.
