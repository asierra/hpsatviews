/*
 * WebP Image Reader
 * Copyright (c) 2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_READER_WEBP_H_
#define HPSATVIEWS_READER_WEBP_H_

#include "image.h"

/**
 * @brief Lee un archivo WebP y lo carga en una estructura ImageData.
 * Fuerza la salida a formato RGBA (32 bits) para consistencia.
 * @param filename La ruta al archivo WebP.
 * @return Una estructura ImageData con los datos de la imagen. Si falla,
 * la estructura devuelta tendrá su puntero `data` a NULL.
 */
ImageData reader_load_webp(const char *filename);

#endif /* HPSATVIEWS_READER_WEBP_H_ */