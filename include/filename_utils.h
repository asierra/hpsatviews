/*
 * Utilidades para la manipulación de nombres de archivo.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_FILENAME_UTILS_H_
#define HPSATVIEWS_FILENAME_UTILS_H_

#include <stdbool.h>

/**
 * @brief Genera un nombre de archivo de salida por defecto basado en el archivo de entrada,
 *        el modo de procesamiento y la extensión.
 *        Ejemplo: "out20253231800-night.png"
 * @param input_file_path La ruta al archivo de entrada GOES NetCDF.
 * @param processing_mode La cadena que representa el modo de procesamiento (ej. "truecolor", "night").
 * @param output_extension La extensión deseada para el archivo de salida (ej. ".png").
 * @return Una cadena de caracteres asignada dinámicamente para el nombre del archivo de salida,
 *         o NULL en caso de error. Es responsabilidad del llamador liberar esta cadena con `free()`.
 */
char* generate_default_output_filename(const char* input_file_path, const char* processing_mode, const char* output_extension);

/**
 * @brief Expande un patrón de nombre de archivo reemplazando marcadores de tiempo y metadatos.
 *        Marcadores soportados: {YYYY}, {YY}, {MM}, {DD}, {hh}, {mm}, {ss}, {JJJ}, {CH}, {SAT}.
 *        La información se extrae del nombre de archivo de entrada (formato GOES).
 *        {CH}: Número de banda (ej. C01, C02, C13)
 *        {SAT}: Nombre del satélite (ej. goes-16, goes-19)
 * 
 * @param pattern El patrón de nombre de archivo (ej. "img_{SAT}_{CH}_{YYYY}{MM}{DD}_{hh}{mm}.tif").
 * @param input_filename El nombre del archivo de entrada del cual extraer los metadatos.
 * @return Una nueva cadena con los marcadores reemplazados, o una copia del patrón si no se encuentra información.
 *         El llamador debe liberar la memoria.
 */
char* expand_filename_pattern(const char* pattern, const char* input_filename);



#endif /* HPSATVIEWS_FILENAME_UTILS_H_ */