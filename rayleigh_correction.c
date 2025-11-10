#include "datanc.h" // Para NonData y DataF
#include "image.h"  // Para ImageData
#include "reader_nc.h"
#include "writer_png.h"
#include <math.h>
#include <omp.h>
#include <stdio.h>

#include "datanc.h"
#include <math.h>
#include <stdio.h>

// Definición de la LUT (debe estar en un .h compartido)
typedef enum { LP_0_COMP, T_UP_COMP, E_DOWN_COMP, S_ALB_COMP, NUM_COMPONENTS } LutComponent;
typedef struct {
    float *data;
    size_t sza_dim, vza_dim, ra_dim;
    size_t component_size;
    float *sza_knots, *vza_knots, *ra_knots;
} RayleighLUT;


#include "datanc.h"
#include <math.h>
#include <omp.h>
#include <stdio.h> // Para printf
#include "image.h" // Asumo que esto es para ImageData


void sun_geometry(float la, float lo, DataNC datanc, 
                  double *angle, double *zenith_out, double *azimuth_out) {
  
  // input data:
  double UT;
  int Day;
  int Month;
  int Year;
  double Dt;
  double Longitude;
  double Latitude;
  double Pressure;
  double Temperature;

  // output data
  double RightAscension;
  double Declination;
  double HourAngle;
  double Zenith;

  // auxiliary
  double t, te, wte, s1, c1, s2, c2, s3, c3, sp, cp, sd, cd, cH,
      se0, ep, De, lambda, epsi, sl, cl, se, ce, L, nu, Dlam;
  int yt, mt;

  double PI = M_PI;
  double PI2 = 2 * M_PI;
  double PIM = M_PI_2;

  UT = datanc.hour + datanc.min / 60.0 + datanc.sec / 3600.0;
  Day = datanc.day;
  Month = datanc.mon;
  Year = datanc.year;
  Longitude = lo * PI / 180.0;
  Latitude = la * PI / 180;

  Pressure = 1;
  Temperature = 0;

  if (Month <= 2) {
    mt = Month + 12;
    yt = Year - 1;
  } else {
    mt = Month;
    yt = Year;
  }

  t = (double)((int)(365.25 * (double)(yt - 2000)) +
               (int)(30.6001 * (double)(mt + 1)) - (int)(0.01 * (double)(yt)) +
               Day) +
      0.0416667 * UT - 21958.0;
  Dt = 96.4 + 0.00158 * t;
  te = t + 1.1574e-5 * Dt;

  wte = 0.0172019715 * te;

  s1 = sin(wte);
  c1 = cos(wte);
  s2 = 2.0 * s1 * c1;
  c2 = (c1 + s1) * (c1 - s1);
  s3 = s2 * c1 + c2 * s1;
  c3 = c2 * c1 - s2 * s1;

  L = 1.7527901 + 1.7202792159e-2 * te + 3.33024e-2 * s1 - 2.0582e-3 * c1 +
      3.512e-4 * s2 - 4.07e-5 * c2 + 5.2e-6 * s3 - 9e-7 * c3 -
      8.23e-5 * s1 * sin(2.92e-5 * te) + 1.27e-5 * sin(1.49e-3 * te - 2.337) +
      1.21e-5 * sin(4.31e-3 * te + 3.065) +
      2.33e-5 * sin(1.076e-2 * te - 1.533) +
      3.49e-5 * sin(1.575e-2 * te - 2.358) +
      2.67e-5 * sin(2.152e-2 * te + 0.074) +
      1.28e-5 * sin(3.152e-2 * te + 1.547) +
      3.14e-5 * sin(2.1277e-1 * te - 0.488);

  nu = 9.282e-4 * te - 0.8;
  Dlam = 8.34e-5 * sin(nu);
  lambda = L + PI + Dlam;

  epsi = 4.089567e-1 - 6.19e-9 * te + 4.46e-5 * cos(nu);

  sl = sin(lambda);
  cl = cos(lambda);
  se = sin(epsi);
  ce = sqrt(1 - se * se);

  RightAscension = atan2(sl * ce, cl);
  if (RightAscension < 0.0)
    RightAscension += PI2;

  Declination = asin(sl * se);

  HourAngle =
      1.7528311 + 6.300388099 * t + Longitude - RightAscension + 0.92 * Dlam;
  HourAngle = fmod(HourAngle + PI, PI2) - PI;
  if (HourAngle < -PI)
    HourAngle += PI2;

  sp = sin(Latitude);
  cp = sqrt((1 - sp * sp));
  sd = sin(Declination);
  cd = sqrt(1 - sd * sd);
  double sH = sin(HourAngle);
  cH = cos(HourAngle);
  se0 = sp * sd + cp * cd * cH;
  ep = asin(se0) - 4.26e-5 * sqrt(1.0 - se0 * se0);
  double Azimuth = atan2(sH, cH * sp - sd * cp / cd);

  if (ep > 0.0)
    De = (0.08422 * Pressure) /
         ((273.0 + Temperature) * tan(ep + 0.003138 / (ep + 0.08919)));
  else
    De = 0.0;

  Zenith = PIM - ep - De;
  
  *zenith_out = Zenith * 180.0 / M_PI; // Devolver en grados
  *azimuth_out = Azimuth * 180.0 / M_PI; // Devolver en grados
  *angle = Zenith;  
}


/* * Genera las grillas de ángulos geométricos necesarias para la corrección.
 */
int create_geometry_maps0(
    DataNC datanc,      // Entrada: Metadatos de tiempo
    DataF navla,        // Entrada: Latitud
    DataF navlo,        // Entrada: Longitud
    DataF nav_vza,      // Entrada: Ángulo Cenital de Vista (cargado)
    DataF nav_vaa,      // Entrada: Ángulo Acimutal de Vista (cargado)
    DataF *sza_map_out, // Salida: Mapa de SZA
    DataF *vza_map_out, // Salida: Mapa de VZA (copia)
    DataF *ra_map_out   // Salida: Mapa de RA
) {
  double start = omp_get_wtime();

  // Usamos la API de datanc.h para crear las grillas de salida
  *sza_map_out = dataf_create(navla.width, navla.height);
  *vza_map_out = dataf_create(navla.width, navla.height);
  *ra_map_out = dataf_create(navla.width, navla.height);

  if (!sza_map_out->data_in || !vza_map_out->data_in || !ra_map_out->data_in) {
      fprintf(stderr, "Error: No se pudo asignar memoria para los mapas geométricos\n");
      return 0; // Fallo
  }

#pragma omp parallel for
  for (int y = 0; y < navla.height; y++) {
    for (int x = 0; x < navla.width; x++) {
      int i = y * navla.width + x;

      float la = navla.data_in[i];
      float lo = navlo.data_in[i];
      float vza_deg = nav_vza.data_in[i];

      // ** Manejo de NonData (CRÍTICO) **
      if (la == NonData || lo == NonData || vza_deg == NonData) {
        sza_map_out->data_in[i] = NonData;
        vza_map_out->data_in[i] = NonData;
        ra_map_out->data_in[i] = NonData;
        continue;
      }

      // 1. Obtener SZA y Acimut Solar
      double sza_deg, solar_az_deg, angle;
      sun_geometry(la, lo, datanc, &sza_deg, &solar_az_deg, &angle);

      // 2. Obtener Acimut del Sensor
      float sensor_az_deg = nav_vaa.data_in[i];

      // 3. Calcular RA (Acimut Relativo)
      float ra_deg = fabs(solar_az_deg - sensor_az_deg);
      if (ra_deg > 180.0) {
          ra_deg = 360.0 - ra_deg;
      }

      // 4. Almacenar los valores
      sza_map_out->data_in[i] = (float)sza_deg;
      vza_map_out->data_in[i] = vza_deg;
      ra_map_out->data_in[i] = ra_deg;
    }
  }
  printf("Tiempo Geometría %lf\n", omp_get_wtime() - start);
  return 1; // Éxito
}


/*
 * create_geometry_maps (Versión Simplificada)
 * * Esta función toma los mapas de ángulos precalculados del archivo NAVF
 * y genera las tres grillas necesarias para la corrección de Rayleigh.
 *
 * ENTRADAS (Cargadas desde el archivo NAVF):
 * vza_in:  Ángulo Cenital de Vista (SenZenAng)
 * vaa_in:  Ángulo Acimutal de Vista (SenAziAng)
 * sza_in:  Ángulo Cenital Solar (SolZenAng)
 * saa_in:  Ángulo Acimutal Solar (SolAziAng)
 *
 * SALIDAS (Punteros a DataF que serán asignados):
 * sza_map_out: Mapa de SZA (copia de sza_in)
 * vza_map_out: Mapa de VZA (copia de vza_in)
 * ra_map_out:  Mapa de RA (Calculado: |SAA - VAA|)
 *
 * RETORNO:
 * 1 en éxito, 0 en fallo (p.ej., fallo de asignación)
 */
int create_geometry_maps(
    const DataF *vza_in,      // (SenZenAng)
    const DataF *vaa_in,      // (SenAziAng)
    const DataF *sza_in,      // (SolZenAng)
    const DataF *saa_in,      // (SolAziAng)
    DataF *sza_map_out, 
    DataF *vza_map_out, 
    DataF *ra_map_out
) {
    size_t width = vza_in->width;
    size_t height = vza_in->height;
    size_t size = vza_in->size;

    // 1. Asignar memoria para las 3 grillas de salida
    // Usamos la API de datanc.h
    *sza_map_out = dataf_create(width, height);
    *vza_map_out = dataf_create(width, height);
    *ra_map_out = dataf_create(width, height);

    // 2. Verificar que la asignación de memoria fue exitosa
    if (sza_map_out->data_in == NULL || 
        vza_map_out->data_in == NULL || 
        ra_map_out->data_in == NULL) {
        
        fprintf(stderr, "ERROR (create_geometry_maps): Fallo en asignación de memoria.\n");
        // Liberamos lo que se haya podido asignar (dataf_destroy maneja NULLs)
        dataf_destroy(sza_map_out);
        dataf_destroy(vza_map_out);
        dataf_destroy(ra_map_out);
        return 0; // Fallo
    }

    printf("Calculando Ángulo Acimutal Relativo (RA)...\n");
    double start = omp_get_wtime();

    // 3. Bucle principal (paralelizado)
#pragma omp parallel for
    for (size_t i = 0; i < size; i++) {
        
        // Obtener los 4 ángulos de entrada
        float sza_deg = sza_in->data_in[i];
        float vza_deg = vza_in->data_in[i];
        float solar_az_deg = saa_in->data_in[i];
        float sensor_az_deg = vaa_in->data_in[i];

        // 4. Manejo de píxeles inválidos (NonData)
        if (sza_deg == NonData || vza_deg == NonData || 
            solar_az_deg == NonData || sensor_az_deg == NonData) {
            
            sza_map_out->data_in[i] = NonData;
            vza_map_out->data_in[i] = NonData;
            ra_map_out->data_in[i] = NonData;
            continue;
        }
      
        // 5. Calcular RA (Acimut Relativo)
        // El RA es la diferencia absoluta entre el acimut solar y el del sensor.
        float ra_deg = fabs(solar_az_deg - sensor_az_deg);

        // 6. Manejar el "cruce" de 0/360 grados
        // (El ángulo relativo no puede ser mayor a 180)
        if (ra_deg > 180.0f) {
            ra_deg = 360.0f - ra_deg;
        }

        // 7. Almacenar los valores en las grillas de salida
        sza_map_out->data_in[i] = sza_deg;
        vza_map_out->data_in[i] = vza_deg;
        ra_map_out->data_in[i] = ra_deg;
    }

    double end = omp_get_wtime();
    printf("Tiempo Geometría (RA) %lf s\n", end - start);
    return 1; // Éxito
}

// ... (Aquí deben ir las funciones 'find_indices_and_weights' y 'interpolate_trilinear') ...

// Función LERP (Interpolación Lineal)
static inline float lerp(float v0, float v1, float w) {
    return v0 + w * (v1 - v0);
}

// Ayudante para encontrar índices y pesos de interpolación
// Busca el valor 'val' en el arreglo 'knots'
static void find_indices_and_weights(
    float val, const float *knots, size_t num_dims,
    size_t *idx0, size_t *idx1, float *weight1) {
    // 1. Encontrar el índice inferior
    // (Búsqueda binaria sería más rápida, pero lineal es más simple de leer)
    size_t i = 0;
    while (i < num_dims - 1 && knots[i+1] < val) {
        i++;
    }
    *idx0 = i;
    *idx1 = i + 1;

    // 2. Manejar bordes (extrapolación constante)
    if (val < knots[0]) {
        *idx0 = 0; *idx1 = 0; *weight1 = 0.0f;
        return;
    }
    if (val >= knots[num_dims - 1]) {
        *idx0 = num_dims - 1; *idx1 = num_dims - 1; *weight1 = 0.0f;
        return;
    }

    // 3. Calcular peso
    float val0 = knots[*idx0];
    float val1 = knots[*idx1];
    float denom = val1 - val0;
    
    *weight1 = (denom > 1e-6) ? ((val - val0) / denom) : 0.0f;
}

// IMPLEMENTACIÓN: Interpolación Trilineal
float interpolate_trilinear(
    const RayleighLUT *lut,
    float sza_val,
    float vza_val,
    float ra_val,
    LutComponent component
) {
    size_t i0, i1, j0, j1, k0, k1;
    float sza_w, vza_w, ra_w;

    find_indices_and_weights(sza_val, lut->sza_knots, lut->sza_dim, &i0, &i1, &sza_w);
    find_indices_and_weights(vza_val, lut->vza_knots, lut->vza_dim, &j0, &j1, &vza_w);
    find_indices_and_weights(ra_val, lut->ra_knots, lut->ra_dim, &k0, &k1, &ra_w);

    // Puntero base al componente específico
    const float *data = lut->data + (component * lut->component_size);
    
    // Dimensiones para indexación
    size_t vza_stride = lut->vza_dim * lut->ra_dim;
    size_t ra_stride = lut->ra_dim;

    // Obtener los 8 valores de las esquinas
    float c000 = data[i0*vza_stride + j0*ra_stride + k0];
    float c001 = data[i0*vza_stride + j0*ra_stride + k1];
    float c010 = data[i0*vza_stride + j1*ra_stride + k0];
    float c011 = data[i0*vza_stride + j1*ra_stride + k1];
    float c100 = data[i1*vza_stride + j0*ra_stride + k0];
    float c101 = data[i1*vza_stride + j0*ra_stride + k1];
    float c110 = data[i1*vza_stride + j1*ra_stride + k0];
    float c111 = data[i1*vza_stride + j1*ra_stride + k1];

    // Interpolar a lo largo del eje RA (k)
    float c00 = lerp(c000, c001, ra_w);
    float c01 = lerp(c010, c011, ra_w);
    float c10 = lerp(c100, c101, ra_w);
    float c11 = lerp(c110, c111, ra_w);

    // Interpolar a lo largo del eje VZA (j)
    float c0 = lerp(c00, c01, vza_w);
    float c1 = lerp(c10, c11, vza_w);

    // Interpolar a lo largo del eje SZA (i)
    return lerp(c0, c1, sza_w);
}

/* * Aplica la corrección de Rayleigh a una banda (L_TOA).
 * Genera una grilla de Reflectancia (rho).
 */
void apply_rayleigh_correction(
    const DataF *L_TOA,          // Radiancia de entrada (Banda X)
    const DataF *SZA_map,
    const DataF *VZA_map,
    const DataF *RA_map,
    const RayleighLUT *lut,
    DataF *rho_surface_out       // Salida: Reflectancia (rho)
) {
    if (L_TOA->size != rho_surface_out->size) return;
    const size_t size = L_TOA->size;
    const float PI = 3.1415926535f;

#pragma omp parallel for // Asumiendo que la interpolación es thread-safe
    for (size_t i = 0; i < size; ++i) {
        
        float L_TOA_i = L_TOA->data_in[i];
        float SZA_i = SZA_map->data_in[i];
        float VZA_i = VZA_map->data_in[i];
        float RA_i = RA_map->data_in[i];

        // ** Manejo de NonData (CRÍTICO) **
        if (L_TOA_i == NonData || SZA_i == NonData || VZA_i == NonData || RA_i == NonData) {
            rho_surface_out->data_in[i] = NonData;
            continue;
        }

        // Manejo de Noche (Terminador Solar)
        if (SZA_i > 89.0f) { // 89 grados es más seguro que 90
            rho_surface_out->data_in[i] = 0.0f; // Noche = reflectancia 0
            continue;
        }

        // 3. Interpolar los 4 RTCs
        float Lp_0 = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, LP_0_COMP);
        float T_up = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, T_UP_COMP);
        float E_down = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, E_DOWN_COMP);
        float S_alb = interpolate_trilinear(lut, SZA_i, VZA_i, RA_i, S_ALB_COMP);

        // 4. Aplicar Ecuación (Eq. 1 del artículo)
        float denominator_A = E_down * T_up;
        if (fabs(denominator_A) < 1e-9) {
            rho_surface_out->data_in[i] = 0.0f; 
            continue;
        }
        
        float numerator_A = PI * (L_TOA_i - Lp_0); 
        float A = numerator_A / denominator_A;
        
        float denominator_rho = 1.0f + A * S_alb;
        float rho_surface_i = (fabs(denominator_rho) < 1e-9) ? 0.0f : (A / denominator_rho);

        // 5. Almacenar y Restringir (Clamp)
        if (rho_surface_i < 0.0f) rho_surface_i = 0.0f;
        if (rho_surface_i > 1.0f) rho_surface_i = 1.0f;
        
        rho_surface_out->data_in[i] = rho_surface_i;
    }
}

// Función de ayuda para la Corrección Gamma
static inline unsigned char apply_gamma(float reflectance, float gamma) {
    float corrected = powf(reflectance, 1.0f / gamma);
    if (corrected > 1.0f) corrected = 1.0f;
    return (unsigned char)(corrected * 255.0f);
}

/* * Crea una imagen RGB de 8 bits a partir de las *reflectancias* corregidas.
 * Acepta DataF (flotantes 0-1) en lugar de DataNC.
 * Aplica Corrección Gamma para viveza.
 */
ImageData create_truecolor_rgb(
    DataF rho_c01, // Reflectancia Corregida Banda 1 (Azul)
    DataF rho_c02, // Reflectancia Corregida Banda 2 (Rojo)
    DataF rho_c03  // Reflectancia Corregida Banda 3 (Veggie)
) {
  ImageData imout = image_create(rho_c01.width, rho_c01.height, 3);
  if (imout.data == NULL) return imout; 

  double start = omp_get_wtime();
  
  // El valor de Gamma es subjetivo, 2.2 es estándar,
  // pero para imágenes de satélite a veces se usa 1.8.
  const float GAMMA = 1.8f; 

#pragma omp parallel for
  for (int y = 0; y < imout.height; y++) {
    for (int x = 0; x < imout.width; x++) {
      int i = y * imout.width + x;
      int po = i * imout.bpp;

      float c01f = rho_c01.data_in[i]; // Azul
      float c02f = rho_c02.data_in[i]; // Rojo
      float c03f = rho_c03.data_in[i]; // Veggie

      // ** Manejo de NonData (CRÍTICO) **
      if (c01f == NonData || c02f == NonData || c03f == NonData) {
        imout.data[po] = 0;     // R
        imout.data[po + 1] = 0; // G
        imout.data[po + 2] = 0; // B
        continue;
      }

      // Verde sintético (tu fórmula) usando reflectancias
      float gg = 0.48358168 * c02f + 0.45706946 * c01f + 0.08038137 * c03f;
      if (gg < 0.0f) gg = 0.0f;
      if (gg > 1.0f) gg = 1.0f;

      // Aplicar Corrección Gamma a cada canal antes de convertir a 8-bits
      imout.data[po] = apply_gamma(c02f, GAMMA);     // R
      imout.data[po + 1] = apply_gamma(gg, GAMMA);   // G
      imout.data[po + 2] = apply_gamma(c01f, GAMMA); // B
    }
  }
  printf("Tiempo RGB %lf\n", omp_get_wtime() - start);
  return imout;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <ruta_a_archivo_netcdf_goes>\n", argv[0]);
        return 1;
    }
    const char *netcdf_path = argv[1];

    // Variables de entrada: Radiancia y Mapas de Navegación
    DataNC c01_nc, c02_big, c02_nc, c03_nc; // Metadatos (hora, etc.) y Radiancia L_TOA
    DataF navla, navlo;           // Latitud y Longitud
    DataF nav_vza, nav_vaa;       // VZA y Acimut del Sensor (VAA)

    // 1. SIMULACIÓN DE CARGA DE DATOS REALES
    // ------------------------------------
    // TODO: Reemplazar estas llamadas simuladas por tu lógica real de carga NetCDF.
    printf("1. Cargando datos de entrada (L_TOA y mapas de navegación)...\n");
    // Asumimos que c01_nc contiene la radiancia de Banda 1 en c01_nc.base
    // Asumimos que c02_nc, c03_nc solo se usan para extraer c02_nc.base.data_in, etc.
    //c01_nc = datanc_load_from_netcdf(netcdf_path); // Carga Banda 1 y metadatos (hora)
    // El resto de las bandas solo necesitan la DataF base
    //c02_nc.base = dataf_load_from_netcdf(netcdf_path, "CMI_C02"); 
    //c03_nc.base = dataf_load_from_netcdf(netcdf_path, "CMI_C03");

    load_nc_sf("/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C01_G19_s20253101600214_e20253101609522_c20253101609553.nc", "Rad", &c01_nc);
    load_nc_sf("/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C02_G19_s20253101600214_e20253101609522_c20253101609568.nc", "Rad", &c02_big);
    load_nc_sf("/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C03_G19_s20253101600214_e20253101609522_c20253101609564.nc", "Rad", &c03_nc);
    c02_nc.base = downsample_boxfilter(c02_big.base, 2);
    dataf_destroy(&c02_big.base);

    // Carga de mapas de navegación que deben ser grillas DataF
    compute_navigation_nc("/data/input/abi/l1b/fd/OR_ABI-L1b-RadF-M6C03_G19_s20253101600214_e20253101609522_c20253101609564.nc", &navla, &navlo);
    //navla = dataf_load_from_netcdf(netcdf_path, "lat");
    //navlo = dataf_load_from_netcdf(netcdf_path, "lon"s);
    nav_vza = dataf_load_from_netcdf(netcdf_path, "sensor_zenith_angle");
    nav_vaa = dataf_load_from_netcdf(netcdf_path, "sensor_azimuth_angle");
    
    // Verificación básica de que la carga fue exitosa
    if (c01_nc.base.data_in == NULL || navla.data_in == NULL || nav_vza.data_in == NULL) {
        fprintf(stderr, "Error al cargar datos. Terminando.\n");
        return 1;
    }

    // 2. GENERACIÓN DE MAPAS GEOMÉTRICOS (SZA, VZA, RA)
    // ------------------------------------------------
    printf("2. Generando mapas geométricos (SZA, VZA, RA)...\n");
    DataF sza_map, vza_map, ra_map;
    int success = create_geometry_maps(c01_nc, navla, navlo, nav_vza, nav_vaa, 
                                       &sza_map, &vza_map, &ra_map);

    if (!success) {
        // La limpieza de navla, navlo, etc., ocurre al final.
        fprintf(stderr, "Error al crear mapas geométricos. Terminando.\n");
        return 1;
    }

    // 3. CARGA DE LOOKUP TABLES (LUTs)
    // --------------------------------
    printf("3. Cargando LUTs precalculadas...\n");
    // Debes tener una LUT separada para cada banda ya que son dependientes de la longitud de onda
    RayleighLUT lut_c01 = lut_load_for_band(1); // LUT para Banda 1 (Azul)
    RayleighLUT lut_c02 = lut_load_for_band(2); // LUT para Banda 2 (Rojo)
    RayleighLUT lut_c03 = lut_load_for_band(3); // LUT para Banda 3 (NIR/Veggie)
    // TODO: Añadir verificación de éxito para la carga de LUTs
    
    // 4. CORRECCIÓN ATMOSFÉRICA (Paso Crítico)
    // -----------------------------------------
    printf("4. Aplicando Corrección de Rayleigh banda por banda...\n");

    DataF rho_c01_out = dataf_create(c01_nc.base.width, c01_nc.base.height);
    DataF rho_c02_out = dataf_create(c01_nc.base.width, c01_nc.base.height);
    DataF rho_c03_out = dataf_create(c01_nc.base.width, c01_nc.base.height);

    // Banda 1 (Azul)
    apply_rayleigh_correction(&c01_nc.base, &sza_map, &vza_map, &ra_map, 
                              &lut_c01, &rho_c01_out);
    // Banda 2 (Rojo)
    apply_rayleigh_correction(&c02_nc.base, &sza_map, &vza_map, &ra_map, 
                              &lut_c02, &rho_c02_out);
    // Banda 3 (Veggie/NIR)
    apply_rayleigh_correction(&c03_nc.base, &sza_map, &vza_map, &ra_map, 
                              &lut_c03, &rho_c03_out);

    // 5. GENERACIÓN DE LA IMAGEN RGB A COLOR REAL
    // ------------------------------------------
    printf("5. Creando imagen True Color RGB con verde sintético...\n");
    ImageData final_rgb_image = create_truecolor_rgb(rho_c01_out, rho_c02_out, rho_c03_out);

    // 6. GUARDAR RESULTADO Y LIMPIEZA
    // ------------------------------
    printf("6. Guardando imagen final y liberando memoria...\n");
    // Asumiendo que tienes una función para guardar la imagen (p.ej., como PNG o JPG)
    // image_save(final_rgb_image, "goes_truecolor_corrected.png"); 

    // Limpieza de TODA la memoria asignada
    dataf_destroy(&c01_nc.base);
    dataf_destroy(&c02_nc.base);
    dataf_destroy(&c03_nc.base);
    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    dataf_destroy(&nav_vza);
    dataf_destroy(&nav_vaa);
    
    dataf_destroy(&sza_map);
    dataf_destroy(&vza_map);
    dataf_destroy(&ra_map);

    dataf_destroy(&rho_c01_out);
    dataf_destroy(&rho_c02_out);
    dataf_destroy(&rho_c03_out);

    image_destroy(&final_rgb_image); // Función que libera imout.data
    // TODO: Liberar la memoria de las LUTs (lut_destroy)

    printf("Procesamiento completado con éxito.\n");
    return 0;
}