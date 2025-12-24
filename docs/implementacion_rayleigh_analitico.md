# Corrección Rayleigh

## Implementación analítica y correcciones físicas

**Objetivo:** Reemplazar las Tablas de Búsqueda (LUTs) externas por una implementación física robusta, autoadaptable y científicamente precisa (estilo `geo2grid`).

### Cambios Clave Implementados:

1. Modelo Físico Analítico:
	- Eliminamos la dependencia de archivos .lut.
	- Implementamos en `rayleigh.c` la aproximación de **Dispersión Simple (Single Scattering)**:
	$$R_{corr} = R_{obs} - \frac{\tau \cdot P(\Theta)}{4 \cos(\theta_s) \cos(\theta_v)}$$
	- Esto hace el código más portátil y elimina errores de lectura de archivos binarios.

2. Coeficientes y Ajustes Espectrales:
	- Actualizamos los valores de Profundidad Óptica ($\tau$) específicos para GOES-R ABI:
		- Azul (C01): 0.188 (Corrección fuerte).
		- Rojo (C02): 0.055 (Corrección suave).
	- Implementamos el *verde híbrido* (Bah et al., 2018) que usa el canal NIR para recuperar el color de la vegetación que se perdía al corregir el azul.
	
3. Manejo de Robustez (Bug Fixes):
	- SZA Fading (Terminator): Implementamos un desvanecimiento lineal entre 65° y 80° de ángulo cenital solar para evitar que las nubes se vean amarillas al atardecer/amanecer.
	- Multiresolución Automática: Solucionamos el crash en modo daynite (mezcla de 1km y 2km) mediante la función enforce_resolution, que detecta si la geometría solar es más grande que la imagen y aplica downsampling automático.

## 🔬 Metodología Científica

`hpsatviews` implementa un pipeline de procesamiento físico riguroso para la generación de imágenes RGB, basado en literatura revisada por pares y estándares de la NOAA/CIMSS. A diferencia de las correcciones estéticas simples, este software utiliza modelos analíticos de transferencia radiativa.

### 1. Modelo Físico: Aproximación de Dispersión Simple

Se utiliza la **Aproximación de Dispersión Simple** (*Single-Scattering Approximation*) asumiendo una atmósfera plano-paralela. Este modelo postula que la radiancia de trayectoria (*path radiance*) observada por el sensor se debe principalmente a un único evento de dispersión de la luz solar por las moléculas de aire.

La reflectancia corregida ($R_{corr}$) se calcula como:

```math
R_{corr} = R_{obs} - \frac{\tau \cdot P(\Theta)}{4 \cdot \cos(\theta_s) \cdot \cos(\theta_v)}
```

Donde:

- τ (Tau): Espesor óptico de Rayleigh (dependiente de la banda).

- P(Θ): Función de fase de Rayleigh, $P(Θ)=0.75(1+cos^2 Θ)$.

- Θ: Ángulo de dispersión (Scattering Angle), calculado a partir de la geometría SZA, VZA y Azimut Relativo.

- θs ,θv: Ángulos cenitales solar y del satélite, respectivamente.

Referencia: Hansen, J. E., & Travis, L. D. (1974). Light scattering in planetary atmospheres. Space Science Reviews, 16(4).

### 2. Coeficientes de Profundidad Óptica (τ)

Los valores de profundidad óptica se calculan para las longitudes de onda centrales del sensor GOES-R ABI utilizando el modelo de Atmósfera Estándar de EE.UU. (1976). La fuerte dependencia de $\lambda^−4$ explica la necesidad de una corrección agresiva en el canal azul.

- Banda 1 (Azul, 0.47 µm): τ≈0.188

- Banda 2 (Rojo, 0.64 µm): τ≈0.055

- Banda 3 (NIR, 0.86 µm): Despreciable.

Referencia: Bucholtz, A. (1995). Rayleigh-scattering calculations for the terrestrial atmosphere. Applied Optics, 34(15).

### 3. Corrección del Terminador (SZA Fading)

La aproximación de atmósfera plana diverge matemáticamente cuando el Sol se acerca al horizonte (SZA → 90°), lo que introduce ruido y artefactos de color (nubes amarillas). Para mitigar esto, se implementa un factor de desvanecimiento lineal basado en heurísticas operativas (e.g., Geo2Grid/SatPy):

- SZA < 65°: Corrección completa (100%).

- SZA > 80°: Sin corrección (0%).

- 65° < SZA < 80°: Transición lineal suave.

### 4. Generación de Verde Híbrido (True Color)

Dado que el sensor ABI carece de una banda verde nativa, esta se sintetiza matemáticamente. hpsatviews implementa la fórmula híbrida que incorpora el canal NIR (Infrarrojo Cercano) para simular correctamente la clorofila, evitando que la vegetación aparezca de color marrón.

Fórmula de mezcla:

```C
Green = 0.48 * Red_corr + 0.46 * Blue_corr + 0.06 * NIR
``` 

Referencia: Bah, K., Schmit, T. J., et al. (2018). GOES-16 Advanced Baseline Imager (ABI) True Color Imagery for Legacy and Non-Traditional Applications. NOAA/CIMSS.

### Citación

Si utilizas este software para investigación, por favor considera citar el repositorio y las referencias metodológicas mencionadas anteriormente.
