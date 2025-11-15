/*
 * Geostationary to Geographics Reprojection Module
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_REPROJECTION_H_
#define HPSATVIEWS_REPROJECTION_H_

#include "datanc.h"

DataF reproject_to_geographics(const DataF* source_data, const char* nav_reference_file);

#endif /* HPSATVIEWS_REPROJECTION_H_ */