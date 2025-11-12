// rayleigh_lut.h

#ifndef RAYLEIGH_LUT_H_
#define RAYLEIGH_LUT_H_

#include <stddef.h> // Para size_t

// Los 4 componentes de las constantes de transferencia radiativa (RTCs)
typedef enum { 
    LP_0_COMP,      // Radiancia de Trayectoria (Lp_0)
    T_UP_COMP,      // Transmitancia Ascendente (T_up)
    E_DOWN_COMP,    // Irradiancia Descendente (E_down)
    S_ALB_COMP,     // Albedo Esférico (S_alb)
    NUM_COMPONENTS 
} LutComponent;

// Estructura para almacenar la LUT de Rayleigh (ya poblada y lista para interpolar)
typedef struct {
    float *data;            // Puntero al gran buffer (4 * sza_dim * vza_dim * ra_dim)
    size_t sza_dim, vza_dim, ra_dim; // Dimensiones de los nudos (knots)
    size_t component_size;  // sza_dim * vza_dim * ra_dim
    float *sza_knots;       // Vector de nudos SZA
    float *vza_knots;       // Vector de nudos VZA
    float *ra_knots;        // Vector de nudos RA
} RayleighLUT;

// Prototipos
RayleighLUT lut_load_for_band(int band_id);
void lut_destroy(RayleighLUT *lut);

#endif // RAYLEIGH_LUT_H_