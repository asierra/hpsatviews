/* Multi-channel bundle management for RGB composite processing.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#ifndef HPSATVIEWS_CHANNELSET_H_
#define HPSATVIEWS_CHANNELSET_H_

#include <stdbool.h>
#include <stddef.h>

// ABI channel descriptor.
typedef struct {
    const char *name;     // Channel identifier, e.g. "C01", "C13"
    char *filename;       // Full path to the NetCDF file (heap-allocated)
} ChannelInfo;

// Set of ABI channels required for a composite mode.
typedef struct {
    ChannelInfo *channels;      // Channel array
    int count;                  // Number of channels
    char id_signature[40];      // Scene timestamp token, e.g. "s20253231800"
    char scan_mode[4];          // ABI scan mode of the anchor file, e.g. "M6"
} ChannelSet;

// Creates a ChannelSet for the given channel names (NULL-terminated array). Returns NULL on failure.
ChannelSet* channelset_create(const char **channel_names, int count);

// Frees all memory associated with a ChannelSet. Safe to call with NULL.
void channelset_destroy(ChannelSet *set);

// Resolves NetCDF file paths for each channel in set by scanning directory.
// is_l2_product: true for CMIP (L2), false for Rad (L1b). Returns 0 on success, -1 on error.
int find_channel_filenames(const char *directory, ChannelSet *set, bool is_l2_product);

// Extracts the scene timestamp token from a GOES filename into id_out (≥40 bytes).
// e.g. "OR_ABI-L2-CMIPC-M6C13_G19_s20253231800172_..." → "s20253231800"
// Returns 0 on success, -1 on error.
int find_id_from_name(const char *filename, char *id_out, size_t id_size);

// Extracts the ABI scan mode from a GOES filename into mode_out (≥4 bytes).
// e.g. "OR_ABI-L2-CMIPC-M3C13_G16_..." → "M3". Returns 0 on success, -1 if not found.
int find_scan_mode_from_name(const char *filename, char *mode_out, size_t mode_size);

#endif /* HPSATVIEWS_CHANNELSET_H_ */
