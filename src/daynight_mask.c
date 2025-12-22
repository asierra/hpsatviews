/* Solar computing to create a day/night mask.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Labotatorio Nacional de Observación de la Tierra, UNAM
 */
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include "datanc.h"
#include "image.h" 


double sun_zenith_angle(float la, float lo, DataNC datanc, 
                        double *zenith_out, double *azimuth_out) {
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

  return Zenith;
}

// All data structures must be of the same dimensions, or the result will be wrong.
ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo, 
    float *dnratio, float max_temp) {
  ImageData imout = image_create(navla.width, navla.height, 1);
  
  // Check if allocation was successful
  if (imout.data == NULL) {
    return imout; 
  }

  unsigned long day, nite;
  day = nite = 0;

  double start = omp_get_wtime();
  float *navla_data = navla.data_in;
  float *navlo_data = navlo.data_in;
  float *temp_data = datanc.fdata.data_in; 
  unsigned char *imout_data = imout.data;

#pragma omp parallel for shared(datanc, navla_data, navlo_data, temp_data, imout_data) reduction(+:day,nite)
  for (unsigned y = 0; y < navla.height; y++) {
    for (unsigned x = 0; x < navla.width; x++) {
      int i = y * navla.width + x;
      int po = i * imout.bpp;

      float w = 0;
      float la = navla_data[i];
      float lo = navlo_data[i];
      float temp = temp_data[i];
      double zenith_out, azimuth_out;
      double sza = sun_zenith_angle(la, lo, datanc, &zenith_out, &azimuth_out) * 180 / M_PI;
      if (sza > 88.0) { // Nite
        w = 1;
        nite++;
      } else if (78.0 < sza && sza < 88.0) { // Twilight
        w = (sza - 78.0) / 10.0;
        if (w >= 0.5)
          nite++;
        else
          day++;
      } else { // Day
        w = 0;
        day++;
      }
      // Even if it is day, it is opaque for high clouds
      if (temp < max_temp) {
        w = 1;
      }
      imout_data[po] = (unsigned char)(255 * w);
    }
  }
  *dnratio = (nite==0) ? 100: 100.0*day/navla.size;
  double end = omp_get_wtime();
  printf("Tiempo mask %lf\n", end - start);

  return imout;
}
