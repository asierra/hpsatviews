/* Multi-channel bundle management for RGB composite processing.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include "channelset.h"
#include "logger.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

ChannelSet* channelset_create(const char **channel_names, int count) {
    if (!channel_names || count <= 0) {
        return NULL;
    }
    
    ChannelSet *set = malloc(sizeof(ChannelSet));
    if (!set) {
        LOG_ERROR("Falla al asignar memoria para ChannelSet");
        return NULL;
    }
    
    set->channels = calloc(count, sizeof(ChannelInfo));
    if (!set->channels) {
        LOG_ERROR("Falla al asignar memoria para array de canales");
        free(set);
        return NULL;
    }
    
    set->count = count;
    set->id_signature[0] = '\0';
    set->scan_mode[0] = '\0';
    
    // Copiar nombres de canales
    for (int i = 0; i < count; i++) {
        set->channels[i].name = channel_names[i];
        set->channels[i].filename = NULL;
    }
    
    return set;
}

void channelset_destroy(ChannelSet *set) {
    if (!set) {
        return;
    }
    
    if (set->channels) {
        // Liberar filenames allocados
        for (int i = 0; i < set->count; i++) {
            if (set->channels[i].filename) {
                free(set->channels[i].filename);
            }
        }
        free(set->channels);
    }
    
    free(set);
}

int find_scan_mode_from_name(const char *filename, char *mode_out, size_t mode_size) {
    if (!filename || !mode_out || mode_size < 3) return -1;
    // Find the scan mode pattern "-M[digit]C" in the filename (e.g., "-M3C13_" or "-M6C01_").
    const char *p = filename;
    while ((p = strchr(p, 'M')) != NULL) {
        if (p > filename && *(p - 1) == '-' &&
            isdigit((unsigned char)*(p + 1)) && *(p + 2) == 'C') {
            mode_out[0] = 'M';
            mode_out[1] = *(p + 1);
            mode_out[2] = '\0';
            return 0;
        }
        p++;
    }
    return -1;
}

int find_id_from_name(const char *filename, char *id_out, size_t id_size) {
    if (!filename || !id_out || id_size < 12) {
        return -1;
    }
    
    // Find the start-time token "_sYYYYDDDHHMM" in the filename.
    // GOES format: OR_ABI-L2-CMIPC-M6C13_G19_s20253231800172_...
    const char *s_pos = strstr(filename, "_s");
    if (!s_pos) {
        LOG_DEBUG("No se encontró patrón '_s' en: %s", filename);
        return -1;
    }
    
    // Extract 11 characters of the start timestamp after '_s'.
    // Example: "s20253231800"
    if (strlen(s_pos) < 12) {
        LOG_DEBUG("Nombre muy corto después de '_s': %s", s_pos);
        return -1;
    }
    
    strncpy(id_out, s_pos + 1, 11);  // +1 para omitir el '_'
    id_out[11] = '\0';
    
    return 0;
}

int find_channel_filenames(const char *directory, ChannelSet *set, bool is_l2_product) {
    if (!directory || !set || set->id_signature[0] == '\0') {
        LOG_ERROR("Parámetros inválidos para find_channel_filenames");
        return -1;
    }
    
    DIR *dir = opendir(directory);
    if (!dir) {
        LOG_ERROR("No se pudo abrir el directorio: %s", directory);
        return -1;
    }
    
    // Determine product pattern (L1b radiance or L2 derived).
    const char *product_pattern = is_l2_product ? "L2-CMI" : "L1b-Rad";
    
    struct dirent *entry;
    int found_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // Saltar directorios
        if (entry->d_type == DT_DIR) {
            continue;
        }
        
        // Verificar que sea un archivo NetCDF del producto correcto
        if (strstr(entry->d_name, product_pattern) == NULL) {
            continue;
        }
        
        // Verificar que contenga el ID signature
        if (strstr(entry->d_name, set->id_signature) == NULL) {
            continue;
        }
        
        for (int i = 0; i < set->count; i++) {
            // Build channel pattern using the anchor file's scan mode (e.g., M6C01_).
            char pattern[16];
            const char *mode = (set->scan_mode[0] != '\0') ? set->scan_mode : "M6";
            snprintf(pattern, sizeof(pattern), "%s%s_", mode, set->channels[i].name);
            
            if (strstr(entry->d_name, pattern) != NULL) {
                // Construir ruta completa
                size_t path_len = strlen(directory) + strlen(entry->d_name) + 2;
                char *full_path = malloc(path_len);
                if (!full_path) {
                    LOG_ERROR("Falla al asignar memoria para ruta");
                    closedir(dir);
                    return -1;
                }
                
                snprintf(full_path, path_len, "%s/%s", directory, entry->d_name);
                
                // Solo incrementar el contador la primera vez que se encuentra este canal
                if (set->channels[i].filename) {
                    free(set->channels[i].filename);
                } else {
                    found_count++;
                }
                
                set->channels[i].filename = full_path;
                //found_count++;
                LOG_DEBUG("Encontrado %s: %s", set->channels[i].name, full_path);
                break;
            }
        }
    }
    
    closedir(dir);
    
    // Verificar que todos los canales fueron encontrados
    if (found_count != set->count) {
        LOG_WARN("Solo se encontraron %d de %d canales requeridos", found_count, set->count);
        for (int i = 0; i < set->count; i++) {
            if (!set->channels[i].filename) {
                LOG_WARN("  Falta canal: %s", set->channels[i].name);
            }
        }
        return -1;
    }
    
    return 0;
}
