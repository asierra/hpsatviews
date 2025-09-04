#include "reader_nc.h"


float lamin, lamax, lomin, lomax;

inline void getindices(float la, float lo, &unsigned i, &unsigned j) {
    i = (unsigned)((navlo.width-1)*(lo - lomin)/(lomax - lomin));
    j = (unsigned)((navla.height-1)*(la - lamin)/(lamax - lamin));
}


DataNC geos2geographics(DataNC datag, DataF navla, DataF navlo)
{
  DataNC datanc;
  datanc.fmin = datag.fmin;
  datanc.fmax = datag.fmax;

  // Define los extremos de acuerdo a las coordenadas originales
  int i, j;
  lamax = navla[navla.width/2];
  lamin = navla[(navla.height-1)*navla.width + navla.width/2];
  lomin = navlo[(navlo.height/2)*navlo.width];
  lomax = navlo[(navlo.height/2)*navlo.width + navlo.width - 1];

  // Define las dimensiones del nuevo datanc
  datanc.width  = datag.width;
  datanc.height = datag.height;
  datanc.size = datanc.width * datanc.height;

  datanc.data_in = malloc(sizeof(float)*datanc.size);
  
  double start = omp_get_wtime();
  #pragma omp parallel for shared(datanc, datanc_big, factor) 
  for (int j=0; j < datag.height; j++)
    for (int i=0; i < datag.width; i++) {
        unsigned ig = j*datag.width + i;
        if (datag.data_in[ig] != NonData) {
            unsigned is, js;
            getindices(navla.data_in[ig], navlo.data_in[ig], is, js);
      is = (js*datanc.width + is);
      datanc.data_in[is] = datag.data_in[ig];
        }
    }
    // Interpola los datos faltantes
    // ...
    
  double end = omp_get_wtime();
  return datanc;
}


int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usanza %s <Archivo NetCDF ABI>\n", argv[0]);
    return -1;
  }
  const char *fnc = argv[1];

  // Obtén las coordenadas
  DataF navlo, navla;
  compute_navigation_nc(fnc, &navla, &navlo);

