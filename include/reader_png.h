/*
 * PNG Image Reader
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_READER_PNG_H_
#define HPSATVIEWS_READER_PNG_H_

#include "image.h"

/**
 * @brief Lee un archivo PNG y lo carga en una estructura ImageData.
 * @param filename La ruta al archivo PNG.
 * @return Una estructura ImageData con los datos de la imagen. Si falla,
 *         la estructura devuelta tendrá su puntero `data` a NULL.
 */
ImageData reader_load_png(const char *filename);

#endif /* HPSATVIEWS_READER_PNG_H_ */