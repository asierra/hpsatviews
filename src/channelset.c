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
        LOG_ERROR("Failed to allocate memory for ChannelSet.");
        return NULL;
    }
    
    set->channels = calloc(count, sizeof(ChannelInfo));
    if (!set->channels) {
        LOG_ERROR("Failed to allocate memory for channel array.");
        free(set);
        return NULL;
    }
    
    set->count = count;
    set->id_signature[0] = '\0';
    set->scan_mode[0] = '\0';
    set->name_prefix[0] = '\0';
    set->satellite[0] = '\0';
    set->start[0] = '\0';
    
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
    if (!filename || !id_out || id_size < 13) {
        return -1;
    }
    
    // Find the start-time token "_sYYYYDDDHHMM" in the filename.
    // GOES format: OR_ABI-L2-CMIPC-M6C13_G19_s20253231800172_...
    const char *s_pos = strstr(filename, "_s");
    if (!s_pos) {
        LOG_DEBUG("Pattern '_s' not found in: %s", filename);
        return -1;
    }
    
    // "s" + YYYYJJJHHMM: 12 characters, down to the minute. Until 1.2.0 this
    // kept 11, i.e. the tens of minutes, and a mesoscale directory holds a
    // scene per minute, so an anchor could load its channels -- itself
    // included -- from any of up to ten scenes. The seconds are left out on
    // purpose, but measured over whole days of full disk, CONUS and mesoscale
    // every channel of a scene carries the identical start anyway; the
    // tie-break in find_channel_filenames() uses that.
    if (strlen(s_pos) < 13) {
        LOG_DEBUG("Name too short after '_s': %s", s_pos);
        return -1;
    }
    
    memcpy(id_out, s_pos + 1, 12);  // +1 para omitir el '_'
    id_out[12] = '\0';
    
    return 0;
}

int channelset_set_anchor(ChannelSet *set, const char *basename) {
    if (!set || !basename) return -1;
    if (find_id_from_name(basename, set->id_signature, sizeof(set->id_signature)) != 0)
        return -1;
    find_scan_mode_from_name(basename, set->scan_mode, sizeof(set->scan_mode));

    set->name_prefix[0] = '\0';
    set->satellite[0] = '\0';
    set->start[0] = '\0';

    // OR_ABI-L1b-RadM1-M6C01_G19_s20262570001261_e...
    //                     ^ channel digits, preceded by everything a sibling shares
    const char *p = basename;
    const char *chan = NULL;
    while ((p = strstr(p, "-M")) != NULL) {
        if (isdigit((unsigned char)p[2]) && p[3] == 'C' &&
            isdigit((unsigned char)p[4]) && isdigit((unsigned char)p[5])) {
            chan = p + 4;
            break;
        }
        p++;
    }
    const char *sat = chan ? chan + 2 : NULL;
    if (!sat || sat[0] != '_' || sat[1] != 'G' || !isdigit((unsigned char)sat[2]) ||
        !isdigit((unsigned char)sat[3]) || strncmp(sat + 4, "_s", 2) != 0) {
        LOG_DEBUG("Non-standard GOES name, siblings matched by start only: %s", basename);
        return 0;
    }
    size_t plen = (size_t)(chan - basename);
    if (plen >= sizeof(set->name_prefix)) return 0;
    memcpy(set->name_prefix, basename, plen);
    set->name_prefix[plen] = '\0';
    memcpy(set->satellite, sat + 1, 3);
    set->satellite[3] = '\0';
    const char *st = sat + 6;
    size_t n = 0;
    while (n < sizeof(set->start) - 1 && isdigit((unsigned char)st[n])) n++;
    memcpy(set->start, st, n);
    set->start[n] = '\0';
    return 0;
}

/* Does @name belong to the same scene as the anchor, for channel @chan ("C01")?
 * Checks the whole leading part of the name in order: product, sector, mode,
 * channel, satellite and start minute. */
static bool sibling_matches(const ChannelSet *set, const char *name, const char *chan) {
    size_t plen = strlen(set->name_prefix);
    if (strncmp(name, set->name_prefix, plen) != 0) return false;
    const char *p = name + plen;
    if (strncmp(p, chan + 1, 2) != 0) return false;
    p += 2;
    char head[64];
    snprintf(head, sizeof(head), "_%s_%s", set->satellite, set->id_signature);
    return strncmp(p, head, strlen(head)) == 0;
}

/* Full start token of a name ("_s" + digits), for the tie-break. */
static bool same_start(const ChannelSet *set, const char *name) {
    if (!set->start[0]) return false;
    const char *s = strstr(name, "_s");
    return s && strncmp(s + 2, set->start, strlen(set->start)) == 0 &&
           !isdigit((unsigned char)s[2 + strlen(set->start)]);
}

int find_channel_filenames(const char *directory, ChannelSet *set, bool is_l2_product) {
    if (!directory || !set || set->id_signature[0] == '\0') {
        LOG_ERROR("Invalid parameters for find_channel_filenames.");
        return -1;
    }
    
    DIR *dir = opendir(directory);
    if (!dir) {
        LOG_ERROR("Could not open directory: %s", directory);
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
        
        bool strict = set->name_prefix[0] != '\0';
        if (!strict) {
            // Nombre no estándar: el criterio viejo, por subcadenas.
            if (strstr(entry->d_name, product_pattern) == NULL) continue;
            if (strstr(entry->d_name, set->id_signature) == NULL) continue;
        }
        
        for (int i = 0; i < set->count; i++) {
            bool match;
            if (strict) {
                match = sibling_matches(set, entry->d_name, set->channels[i].name);
            } else {
                // Build channel pattern using the anchor file's scan mode (e.g., M6C01_).
                char pattern[16];
                const char *mode = (set->scan_mode[0] != '\0') ? set->scan_mode : "M6";
                snprintf(pattern, sizeof(pattern), "%s%s_", mode, set->channels[i].name);
                match = strstr(entry->d_name, pattern) != NULL;
            }
            
            if (match && set->channels[i].filename) {
                // Two candidates for one channel: keep the one whose start is
                // the anchor's exactly. readdir() order is arbitrary, so
                // "last one wins" would pick a scene at random.
                const char *prev = strrchr(set->channels[i].filename, '/');
                prev = prev ? prev + 1 : set->channels[i].filename;
                bool prev_exact = same_start(set, prev);
                bool this_exact = same_start(set, entry->d_name);
                if (prev_exact || !this_exact) {
                    if (!prev_exact && !this_exact)
                        LOG_WARN("Ambiguous %s for scene %s: %s and %s; keeping the first",
                                 set->channels[i].name, set->id_signature, prev, entry->d_name);
                    break;
                }
            }

            if (match) {
                // Construir ruta completa
                size_t path_len = strlen(directory) + strlen(entry->d_name) + 2;
                char *full_path = malloc(path_len);
                if (!full_path) {
                    LOG_ERROR("Failed to allocate memory for path.");
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
                LOG_DEBUG("Found %s: %s", set->channels[i].name, full_path);
                break;
            }
        }
    }
    
    closedir(dir);
    
    // Verificar que todos los canales fueron encontrados
    if (found_count != set->count) {
        LOG_WARN("Only found %d of %d required channels", found_count, set->count);
        for (int i = 0; i < set->count; i++) {
            if (!set->channels[i].filename) {
                LOG_WARN("  Missing channel: %s", set->channels[i].name);
            }
        }
        return -1;
    }
    
    return 0;
}
