/*
 * ChannelSet: Gestión de conjuntos de canales para procesamiento RGB.
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#ifndef HPSATVIEWS_CHANNELSET_H_
#define HPSATVIEWS_CHANNELSET_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Información de un canal individual.
 */
typedef struct {
    const char *name;     // Nombre del canal (ej. "C01", "C13")
    char *filename;       // Ruta completa al archivo NetCDF (allocado dinámicamente)
} ChannelInfo;

/**
 * @brief Conjunto de canales requeridos para un modo RGB.
 */
typedef struct {
    ChannelInfo *channels;      // Array de canales
    int count;                  // Número de canales en el conjunto
    char id_signature[40];      // ID del satélite/producto (ej. "s20253231800")
} ChannelSet;

/**
 * @brief Crea un nuevo ChannelSet con los canales especificados.
 * 
 * @param channel_names Array de nombres de canales (debe terminar en NULL)
 * @param count Número de canales (sin contar el terminador NULL)
 * @return Puntero al ChannelSet creado, o NULL si falla
 */
ChannelSet* channelset_create(const char **channel_names, int count);

/**
 * @brief Libera toda la memoria asociada al ChannelSet.
 * 
 * @param set Puntero al ChannelSet a destruir (puede ser NULL)
 */
void channelset_destroy(ChannelSet *set);

/**
 * @brief Busca los archivos de canales en un directorio basándose en el ID del conjunto.
 * 
 * @param directory Directorio donde buscar los archivos
 * @param set ChannelSet con los canales a buscar (debe tener id_signature establecido)
 * @param is_l2_product true si es producto L2 (CMIP), false si es L1b (Rad)
 * @return 0 si todos los canales fueron encontrados, -1 si hubo error
 */
int find_channel_filenames(const char *directory, ChannelSet *set, bool is_l2_product);

/**
 * @brief Extrae el ID signature de un nombre de archivo GOES.
 * Ejemplo: "OR_ABI-L2-CMIPC-M6C13_G19_s20253231800172_..." -> "s20253231800"
 * 
 * @param filename Nombre del archivo
 * @param id_out Buffer de salida para el ID (debe tener al menos 40 bytes)
 * @param id_size Tamaño del buffer de salida
 * @return 0 si se extrajo correctamente, -1 si hubo error
 */
int find_id_from_name(const char *filename, char *id_out, size_t id_size);

#endif /* HPSATVIEWS_CHANNELSET_H_ */
