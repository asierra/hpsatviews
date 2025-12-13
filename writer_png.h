/*
 * PNG Image Writer
 * Copyright (c) 2025 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_WRITER_PNG_H_
#define HPSATVIEWS_WRITER_PNG_H_

#include "image.h"

/**
 * @brief Escribe una imagen RGB/Grayscale (ImageData con bpp 1, 2, 3 o 4) a un archivo PNG.
 * @param filename La ruta del archivo de salida.
 * @param image Puntero a la estructura ImageData a guardar.
 * @return 0 en caso de éxito, 1 en caso de error.
 */
int writer_save_png(const char *filename, const ImageData *image);

/**
 * @brief Escribe una imagen de paleta (ImageData con bpp 1 o 2) a un archivo PNG.
 * 
 * Si bpp=1, la imagen contiene solo índices de paleta.
 * Si bpp=2, la imagen contiene [índice, alfa] por píxel, y se generará un chunk tRNS.
 * 
 * @param filename La ruta del archivo de salida.
 * @param image Puntero a la estructura ImageData a guardar.
 * @param palette Puntero a la paleta de colores a usar.
 * @return 0 en caso de éxito, 1 en caso de error.
 */
int writer_save_png_palette(const char *filename, const ImageData *image, const ColorArray *palette);

#endif /* HPSATVIEWS_WRITER_PNG_H_ */