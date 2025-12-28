/*
 * Single-channel processing module (gray and pseudocolor)
 *
 * Copyright (c) 2025-2026  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "processing.h"
#include "args.h"
#include "logger.h"
#include "reader_nc.h"
#include "reader_cpt.h"
#include "writer_png.h"
#include "writer_geotiff.h"
#include "filename_utils.h"
#include "reprojection.h"
#include "image.h"
#include "datanc.h"
#include "clip_loader.h"
#include "gray.h"
#include "parse_expr.h"
#include "channelset.h"
#include "palette.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <libgen.h>  // Para dirname()
#include <math.h>    // Para fabs()


bool strinstr(const char *main_str, const char *sub) {
    return (main_str && sub && strstr(main_str, sub));
}

// --- Función helper para procesar coordenadas de clip ---
static bool process_clip_coords(ArgParser* parser, const char* clip_csv_path, float clip_coords[4]) {
    if (!ap_found(parser, "clip")) {
        return false;
    }
    
    const char* clip_value = ap_get_str_value(parser, "clip");
    if (!clip_value || strlen(clip_value) == 0) {
        return false;
    }
    
    // Intentar parsear como 4 coordenadas separadas por comas o espacios
    float coords[4];
    int parsed = sscanf(clip_value, "%f%*[, ]%f%*[, ]%f%*[, ]%f", 
                       &coords[0], &coords[1], &coords[2], &coords[3]);
    
    if (parsed == 4) {
        // Son 4 coordenadas
        for (int i = 0; i < 4; i++) {
            clip_coords[i] = coords[i];
        }
        LOG_INFO("Usando recorte con coordenadas: lon[%.3f, %.3f], lat[%.3f, %.3f]",
                 clip_coords[0], clip_coords[2], clip_coords[3], clip_coords[1]);
        return true;
    }
    
    // No se pudo parsear como coordenadas, intentar como clave
    GeoClip clip = buscar_clip_por_clave(clip_csv_path, clip_value);
    
    if (!clip.encontrado) {
        LOG_ERROR("No se encontró el recorte con clave '%s' en %s", clip_value, clip_csv_path);
        LOG_ERROR("Formato esperado: clave (ej. 'mexico') o coordenadas \"lon_min,lat_max,lon_max,lat_min\"");
        return false;
    }
    
    LOG_INFO("Usando recorte '%s': %s", clip_value, clip.region);
    clip_coords[0] = clip.ul_x;  // lon_min
    clip_coords[1] = clip.ul_y;  // lat_max
    clip_coords[2] = clip.lr_x;  // lon_max
    clip_coords[3] = clip.lr_y;  // lat_min
    return true;
}


int run_processing(ArgParser *parser, bool is_pseudocolor) {
    int status = -1; // Default to error
    char *fnc01 = NULL;
    char *out_filename_generated = NULL;
    CPTData* cptdata = NULL;
    ColorArray *color_array = NULL;
    DataNC c01 = {0}, channels[17] = {0};
    DataF result_data = {0}, navla_full = {0}, navlo_full = {0};
    ImageData final_image = {0}, temp_image = {0};
    char *palette_name = NULL;

    if (ap_has_args(parser)) {
        fnc01 = ap_get_arg_at_index(parser, 0);
    } else {
        LOG_ERROR("Se requiere un archivo NetCDF de entrada.");
        goto cleanup;
    }

    // --- Detección de modo --expr (álgebra de bandas) ---
    const bool expr_mode = ap_found(parser, "expr");
    LinearCombo combo = {0};
    char* required_channels[17] = {NULL};  // Máximo 16 bandas + terminador NULL
    int num_required_channels = 0;
    float minmax[2] = {0.0f, 255.0f};  // Rango por defecto
    bool minmax_provided = false;  // Track si el usuario especificó --minmax

    if (expr_mode) {
        // Parsear expresión
        const char* expr_str = ap_get_str_value(parser, "expr");
        if (!expr_str || strlen(expr_str) == 0) {
            LOG_ERROR("La opción --expr requiere una expresión válida.");
            goto cleanup;
        }

        LOG_INFO("Modo álgebra de bandas: %s", expr_str);
        if (parse_expr_string(expr_str, &combo) != 0) {
            LOG_ERROR("Error al parsear la expresión: %s", expr_str);
            goto cleanup;
        }

        // Extraer bandas requeridas
        num_required_channels = extract_required_channels(&combo, required_channels);
        if (num_required_channels == 0) {
            LOG_ERROR("No se encontraron bandas válidas en la expresión.");
            goto cleanup;
        }

        LOG_INFO("Expresión parseada correctamente: %d bandas requeridas", num_required_channels);
        for (int i = 0; i < num_required_channels; i++) {
            LOG_DEBUG("  - %s", required_channels[i]);
        }

        // Parsear rango --minmax si está presente
        if (ap_found(parser, "minmax")) {
            const char* minmax_str = ap_get_str_value(parser, "minmax");
            if (sscanf(minmax_str, "%f,%f", &minmax[0], &minmax[1]) != 2) {
                LOG_ERROR("Formato inválido para --minmax. Use: min,max"); goto cleanup;
            }
            minmax_provided = true;
            LOG_INFO("Rango de salida especificado: [%.2f, %.2f]", minmax[0], minmax[1]);
        } else {
            LOG_INFO("Rango no especificado, se calculará automáticamente del resultado");
        }
    }

    // --- Opciones de procesamiento ---
    const bool invert_values = ap_found(parser, "invert");
    const bool apply_histogram = ap_found(parser, "histo");
    const bool clahe_flag = ap_found(parser, "clahe");
    const bool clahe_params_provided = ap_found(parser, "clahe-params");
    const char* clahe_params = clahe_params_provided ? ap_get_str_value(parser, "clahe-params") : NULL;
    const bool use_alpha = ap_found(parser, "alpha");
    const float gamma = ap_get_dbl_value(parser, "gamma");
    const int scale = ap_get_int_value(parser, "scale");
    const bool do_reprojection = ap_found(parser, "geographics");
    const bool force_geotiff = ap_found(parser, "geotiff");
    
    // CLAHE se activa si se da --clahe o --clahe-params
    const bool apply_clahe = clahe_flag || clahe_params_provided;
    
    // Parsear parámetros de CLAHE (defecto: 8x8 tiles, clip_limit=4.0)
    int clahe_tiles_x = 8, clahe_tiles_y = 8;
    float clahe_clip_limit = 4.0f;
    if (apply_clahe && clahe_params) {
        int parsed = sscanf(clahe_params, "%d,%d,%f", &clahe_tiles_x, &clahe_tiles_y, &clahe_clip_limit);
        if (parsed < 3) {
            // Si no se especifican todos los parámetros, usar defaults para los faltantes
            if (parsed < 1) clahe_tiles_x = 8;
            if (parsed < 2) clahe_tiles_y = 8;
            if (parsed < 3) clahe_clip_limit = 4.0f;
        }
        LOG_DEBUG("CLAHE params: tiles=%dx%d, clip_limit=%.2f", clahe_tiles_x, clahe_tiles_y, clahe_clip_limit);
    }

    // Determinar si hay recorte ANTES de generar el nombre de archivo
    float clip_coords[4] = {0};
    const bool has_clip = process_clip_coords(parser, "/usr/local/share/lanot/docs/recortes_coordenadas.csv", clip_coords);

    // --- Nombre de archivo de salida (procesamiento de patrones) ---
    const char* outfn = NULL;
    bool user_provided_output = false;
    
    if (ap_found(parser, "out")) {
        const char* user_out = ap_get_str_value(parser, "out");
        user_provided_output = true;
        
        // Detectar si hay patrón (contiene llaves)
        if (strchr(user_out, '{') && strchr(user_out, '}')) {
            out_filename_generated = expand_filename_pattern(user_out, fnc01);
            outfn = out_filename_generated;
        } else {
            outfn = user_out;
        }
        
        // Si se fuerza GeoTIFF pero el nombre tiene extensión .png, cambiarla a .tif
        if (force_geotiff && outfn) {
            const char *ext = strrchr(outfn, '.');
            if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0)) {
                size_t base_len = ext - outfn;
                out_filename_generated = malloc(base_len + 5); // espacio para ".tif\0"
                if (out_filename_generated) {
                    strncpy(out_filename_generated, outfn, base_len);
                    strcpy(out_filename_generated + base_len, ".tif");
                    LOG_INFO("Extensión cambiada de .png a .tif por usar --geotiff: %s", out_filename_generated);
                    outfn = out_filename_generated;
                }
            }
        }
    }

    // --- Carga de datos ---
    if (is_pseudocolor) {
		if (ap_found(parser, "cpt")) {
			char *cptfn = ap_get_str_value(parser, "cpt");
			cptdata = read_cpt_file(cptfn);
			if (cptdata) {
				color_array = cpt_to_color_array(cptdata);
                // Extraer nombre de la paleta para el archivo de salida
                char *cpt_dup = strdup(cptfn);
                if (cpt_dup) {
                    char *base = basename(cpt_dup);
                    char *ext = strrchr(base, '.');
                    if (ext) *ext = '\0';
                    palette_name = strdup(base);
                    free(cpt_dup);
                }
			} else { LOG_ERROR("No se pudo cargar el archivo de paleta: %s", cptfn); goto cleanup; }
		} else {
			LOG_WARN("Sin opción -p/--cpt se usará arcoiris interno.");
			cptdata = cpt_create(256, true);
			color_array = create_rainbow_color_array(256);
		}
    }
    
    if (expr_mode) {
        // --- MODO EXPR: Cargar múltiples canales ---
        
        // 1. Crear ChannelSet con las bandas requeridas
        ChannelSet* cset = channelset_create((const char**)required_channels, num_required_channels);
        if (!cset) {
            LOG_ERROR("No se pudo crear el ChannelSet.");
            goto cleanup;
        }
        
        // 2. Extraer firma de identificación del archivo ancla (satélite + timestamp)
        char id_signature[256];
        char* input_dup_for_id = strdup(fnc01);
        if (!input_dup_for_id) {
            LOG_ERROR("Error de memoria al duplicar nombre de archivo.");
            channelset_destroy(cset); goto cleanup;
        }
        const char* basename_input = basename(input_dup_for_id);
        
        if (find_id_from_name(basename_input, id_signature, sizeof(id_signature)) != 0) {
            LOG_ERROR("No se pudo extraer la firma de identificación del archivo ancla: %s", basename_input);
            free(input_dup_for_id); channelset_destroy(cset); goto cleanup;
        }
        strcpy(cset->id_signature, id_signature);
        free(input_dup_for_id);
        LOG_DEBUG("Firma de identificación: %s", id_signature);
        
        // 3. Buscar archivos para cada canal
        char* input_dup_for_dir = strdup(fnc01); // dirname puede modificar el string
        if (!input_dup_for_dir) {
            LOG_ERROR("Error de memoria al duplicar nombre de archivo.");
            channelset_destroy(cset); goto cleanup;
        }
        const char* dirnm = dirname(input_dup_for_dir);
        
        // Detectar si es producto L2 (CMIP) o L1b (Rad)
        bool is_l2_product = strinstr(fnc01, "CMIP");
        
        if (find_channel_filenames(dirnm, cset, is_l2_product) != 0) {
            LOG_ERROR("No se pudieron encontrar todos los archivos necesarios en %s", dirnm);
            free(input_dup_for_dir); channelset_destroy(cset); goto cleanup;
        }
        free(input_dup_for_dir);
        
        // 4. Cargar cada canal en el array channels[]
        for (int i = 0; i < cset->count; i++) {
            const char* ch_name = cset->channels[i].name;
            const char* ch_file = cset->channels[i].filename;
            
            // Extraer band_id de "C01", "C13", etc.
            int band_id = atoi(ch_name + 1);  // Skip 'C'
            if (band_id < 1 || band_id > 16) {
                LOG_ERROR("Band ID inválido: %s", ch_name);
                channelset_destroy(cset); goto cleanup;
            }
            
            // Cargar NetCDF
            char* varname = "Rad"; // Default
            if (strinstr(ch_file, "CMIP")) varname = "CMI"; 
             else if (strinstr(ch_file, "LST")) varname = "LST";
             else if (strinstr(ch_file, "ACTP")) varname = "Phase"; 
             else if (strinstr(ch_file, "CTP")) varname = "PRES";
            LOG_INFO("Cargando canal %s desde: %s", ch_name, ch_file);
            if (load_nc_sf(ch_file, varname, &channels[band_id]) != 0) {
                LOG_ERROR("No se pudo cargar el canal %s: %s", ch_name, ch_file);
                channelset_destroy(cset);
                // Limpiar canales ya cargados
                for (int j = 1; j <= 16; j++) {
                    if (channels[j].fdata.data_in) datanc_destroy(&channels[j]);
                } goto cleanup;
            }
        }
        
        // 5. Identificar canal de referencia y resamplear (Fase 3)
        int ref_channel_idx = 0;
        const bool use_full_res = ap_found(parser, "full-res");
        
        // Identificar canal de referencia basándose en resolución
        for (int i = 0; i < cset->count; i++) {
            int cn = atoi(cset->channels[i].name + 1);
            if (use_full_res) {
                // Modo --full-res: buscar la MAYOR resolución (km MÁS PEQUEÑO)
                if (ref_channel_idx == 0 || channels[cn].native_resolution_km < channels[ref_channel_idx].native_resolution_km) {
                    ref_channel_idx = cn;
                }
            } else {
                // Modo por defecto: buscar la MENOR resolución (km MÁS GRANDE)
                if (ref_channel_idx == 0 || channels[cn].native_resolution_km > channels[ref_channel_idx].native_resolution_km) {
                    ref_channel_idx = cn;
                }
            }
        }
        
        LOG_INFO("Canal de referencia para expresión: C%02d (%.1fkm)", ref_channel_idx, channels[ref_channel_idx].native_resolution_km);
        
        // Resamplear canales para que coincidan con la resolución de referencia
        float ref_res = channels[ref_channel_idx].native_resolution_km;
        for (int i = 0; i < cset->count; i++) {
            int cn = atoi(cset->channels[i].name + 1);
            if (cn == ref_channel_idx || channels[cn].fdata.data_in == NULL) {
                continue; // No resamplear el canal de referencia o canales vacíos
            }
            
            float res = channels[cn].native_resolution_km;
            float factor_f = res / ref_res;
            
            if (fabs(factor_f - 1.0f) > 0.01f) {
                int factor = (int)(factor_f + 0.5f);
                DataF resampled = {0};
                
                if (factor_f < 1.0f) {
                    // Resolución mayor que referencia -> Downsample
                    factor = (int)((1.0f / factor_f) + 0.5f);
                    LOG_INFO("Downsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res, factor);
                    resampled = downsample_boxfilter(channels[cn].fdata, factor);
                } else {
                    // Resolución menor que referencia -> Upsample
                    LOG_INFO("Upsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res, factor);
                    resampled = upsample_bilinear(channels[cn].fdata, factor);
                }
                
                if (resampled.data_in) {
                    dataf_destroy(&channels[cn].fdata);
                    channels[cn].fdata = resampled;
                } else {
                    LOG_ERROR("Falla al resamplear el canal C%02d", cn);
                    channelset_destroy(cset); goto cleanup;
                }
            }
        }
        
        // 6. Evaluar combinación lineal (Fase 4)
        LOG_INFO("Evaluando expresión algebraica...");
        result_data = evaluate_linear_combo(&combo, channels);
        if (!result_data.data_in) {
            LOG_ERROR("Falla al evaluar la expresión."); channelset_destroy(cset); goto cleanup;
        }
        
        // Si no se proporcionó --minmax, calcular del resultado
        if (!minmax_provided) {
            minmax[0] = result_data.fmin;
            minmax[1] = result_data.fmax;
            LOG_INFO("Rango calculado automáticamente: [%.2f, %.2f]", minmax[0], minmax[1]);
        }
        
        // 7. Copiar metadatos del canal de referencia (sin compartir punteros)
        c01 = channels[ref_channel_idx];
        c01.fdata = result_data;  // Reemplazar datos con el resultado
        c01.is_float = true;
        result_data.data_in = NULL;  // Transferir ownership
        
        // Marcar el canal de referencia como ya procesado para evitar double-free
        channels[ref_channel_idx].fdata.data_in = NULL;
        
        channelset_destroy(cset);
        
    } else {
        // --- MODO NORMAL: Un solo canal ---
        char *varname = "Rad"; // Default para L1b
        if (strinstr(fnc01, "CMIP")) varname = "CMI"; else if (strinstr(fnc01, "LST")) varname = "LST"; else if (strinstr(fnc01, "ACTP")) varname = "Phase"; else if (strinstr(fnc01, "CTP")) varname = "PRES";
        
        // NOTA: load_nc_sf debe llenar los metadatos de DataNC (geotransform, proj_code)
        if (load_nc_sf(fnc01, varname, &c01) != 0) {
            LOG_ERROR("No se pudo cargar el archivo NetCDF: %s", fnc01);
            goto cleanup;
        }
    }

    // --- Generar nombre de archivo canónico si el usuario no proveyó uno ---
    if (!user_provided_output) {
        char* satellite_name = extract_satellite_name(fnc01);
        // Si es modo expr, forzar band_id=0 para que el generador de nombres use _expr
        DataNC c01_for_name = c01;
        if (expr_mode) {
            c01_for_name.band_id = 0;
        }
        FilenameGeneratorInfo info = {
            .datanc = &c01_for_name,
            .satellite_name = satellite_name,
            .command = is_pseudocolor ? "pseudocolor" : "gray",
            .mode = is_pseudocolor ? palette_name : NULL,
            .apply_rayleigh = false, // No aplicable
            .apply_histogram = apply_histogram,
            .apply_clahe = apply_clahe,
            .gamma = gamma,
            .has_clip = has_clip,
            .do_reprojection = do_reprojection,
            .force_geotiff = force_geotiff,
            .invert_values = invert_values
        };

        out_filename_generated = generate_hpsv_filename(&info);
        free(satellite_name);
        outfn = out_filename_generated;
    }

    // --- Navegación y Recorte ---
    bool nav_loaded = false;
    bool is_geotiff = force_geotiff || (outfn && (strstr(outfn, ".tif") || strstr(outfn, ".tiff")));

    if (ap_found(parser, "clip") || is_geotiff || do_reprojection) {
        if (compute_navigation_nc(fnc01, &navla_full, &navlo_full) == 0) {
            nav_loaded = true;
        } else {
            LOG_WARN("No se pudo cargar la navegación del archivo NetCDF.");
            if (is_geotiff) {
                LOG_ERROR("La navegación es requerida para GeoTIFF. Abortando."); goto cleanup;
            }
        }
    }

    // Variables para metadatos de salida
    float final_lon_min = 0, final_lon_max = 0, final_lat_min = 0, final_lat_max = 0;
    unsigned crop_x_start = 0, crop_y_start = 0;

	if (gamma != 1.0f) {
        if (c01.is_float && c01.fdata.data_in) {
            LOG_INFO("Aplicando corrección gamma %.2f a los datos flotantes...", gamma);
            dataf_apply_gamma(&c01.fdata, gamma);
        } else {
            LOG_WARN("La corrección gamma requiere datos flotantes (DataF). Se omitirá.");
        }
    }
    
    // --- PASO 1: Crear imagen en proyección nativa ---
    if (expr_mode) {
        // Modo expr: usar rango personalizado
        final_image = create_single_gray_range(c01.fdata, invert_values, use_alpha, minmax[0], minmax[1]);
    } else if (c01.is_float) {
        final_image = create_single_gray(c01.fdata, invert_values, use_alpha, is_pseudocolor ? cptdata : NULL);
    } else {
        final_image = create_single_grayb(c01.bdata, invert_values, use_alpha, is_pseudocolor ? cptdata : NULL);
    }
    if (!final_image.data) {
        LOG_ERROR("Falla al crear la imagen nativa.");
        goto cleanup;
    }

    // --- PASO 3: Reproyección o recorte (si es necesario) ---
    if (do_reprojection) {
        if (!nav_loaded) {
            LOG_ERROR("La reproyección fue solicitada pero no se pudo cargar la navegación.");
            goto cleanup;
        }

        LOG_INFO("Iniciando reproyección de imagen a coordenadas geográficas...");
        temp_image = reproject_image_to_geographics(
            &final_image, &navla_full, &navlo_full, c01.native_resolution_km,
            has_clip ? clip_coords : NULL
        );
        image_destroy(&final_image);
        final_image = temp_image;

        if (final_image.data == NULL) {
            LOG_ERROR("Falla durante la reproyección de la imagen.");
            goto cleanup;
        }

        // Actualizar los límites geográficos para el GeoTIFF de salida
        if (has_clip) {
            final_lon_min = clip_coords[0]; final_lat_max = clip_coords[1];
            final_lon_max = clip_coords[2]; final_lat_min = clip_coords[3];
        } else {
            final_lon_min = navlo_full.fmin; final_lon_max = navlo_full.fmax;
            final_lat_min = navla_full.fmin; final_lat_max = navla_full.fmax;
        }
    } else if (has_clip) {
        // --- Recorte en proyección nativa (sin reproyección) ---
        if (!nav_loaded) {
            LOG_ERROR("El recorte fue solicitado pero no se pudo cargar la navegación.");
            goto cleanup;
        }
        int ix, iy, iw, ih;
        reprojection_find_bounding_box(&navla_full, &navlo_full, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3], &ix, &iy, &iw, &ih);
        LOG_INFO("Aplicando recorte geoestacionario: start[%d,%d], size[%d,%d]", ix, iy, iw, ih);
        
        temp_image = image_crop(&final_image, ix, iy, iw, ih);
        image_destroy(&final_image);
        final_image = temp_image;
        crop_x_start = (unsigned)ix;
        crop_y_start = (unsigned)iy;
    }

    // --- PASO 4: Post-procesamiento ---
    // La ecualización de histograma y CLAHE no tienen sentido para pseudocolor,
    // ya que la paleta de colores es la que define el "realce".
    if (!is_pseudocolor) {
        if (apply_histogram) image_apply_histogram(final_image);
        if (apply_clahe) image_apply_clahe(final_image, clahe_tiles_x, clahe_tiles_y, clahe_clip_limit);
    }
    
    // --- REMUESTREO (si se solicitó) ---
    if (scale < 0) {
        temp_image = image_downsample_boxfilter(&final_image, -scale);
        image_destroy(&final_image);
        final_image = temp_image;
    } else if (scale > 1) {
        temp_image = image_upsample_bilinear(&final_image, scale);
        image_destroy(&final_image);
        final_image = temp_image;
    }

    // --- Escritura de archivo ---
    if (is_geotiff) {
        DataNC meta_out = {0};
        
        if (do_reprojection) {
            // MODO: Geográficas (Lat/Lon)
            meta_out.proj_code = PROJ_LATLON;
            
            // Construir GeoTransform [TL_X, W_Px, 0, TL_Y, 0, H_Px]
            meta_out.geotransform[0] = final_lon_min;
            meta_out.geotransform[1] = (final_lon_max - final_lon_min) / (double)final_image.width;
            meta_out.geotransform[2] = 0.0;
            meta_out.geotransform[3] = final_lat_max;
            meta_out.geotransform[4] = 0.0;
            // Altura de pixel suele ser negativa (Norte -> Sur)
            meta_out.geotransform[5] = (final_lat_min - final_lat_max) / (double)final_image.height; 

            // Pasamos offset 0,0 porque la imagen y el geotransform ya están alineados
            if (is_pseudocolor && color_array) {
                if (use_alpha) {
                    temp_image = image_expand_palette(&final_image, color_array);
                    write_geotiff_rgb(outfn, &temp_image, &meta_out, 0, 0);
                    image_destroy(&temp_image);
                } else {
                    write_geotiff_indexed(outfn, &final_image, color_array, &meta_out, 0, 0);
                }
            } else {
                write_geotiff_gray(outfn, &final_image, &meta_out, 0, 0);
            }
        } else {
            // MODO: Nativo (Geoestacionario)
            // Copiamos la info del NetCDF original (c01)
            meta_out = c01;
            
            // Ajustar geotransform si se aplicó scale
            if (scale != 1) {
                double scale_factor = (scale < 0) ? -scale : scale;
                if (scale > 1) {
                    // Upsampling: los píxeles son más pequeños
                    meta_out.geotransform[1] /= scale_factor;  // pixel width
                    meta_out.geotransform[5] /= scale_factor;  // pixel height (negativo)
                } else if (scale < 0) {
                    // Downsampling: los píxeles son más grandes
                    meta_out.geotransform[1] *= scale_factor;  // pixel width
                    meta_out.geotransform[5] *= scale_factor;  // pixel height (negativo)
                }
                LOG_DEBUG("Geotransform ajustado por scale=%d: PixelW=%.6f PixelH=%.6f", 
                          scale, meta_out.geotransform[1], meta_out.geotransform[5]);
            }
            
            // Pasamos el offset del recorte para que GDAL ajuste el origen
            if (is_pseudocolor && color_array) {
                if (use_alpha) {
                    temp_image = image_expand_palette(&final_image, color_array);
                    write_geotiff_rgb(outfn, &temp_image, &meta_out, crop_x_start, crop_y_start);
                    image_destroy(&temp_image);
                } else {
                    write_geotiff_indexed(outfn, &final_image, color_array, &meta_out, crop_x_start, crop_y_start);
                }
            } else {
                write_geotiff_gray(outfn, &final_image, &meta_out, crop_x_start, crop_y_start);
            }
        }
    } else {
        if (is_pseudocolor && color_array) {
            LOG_DEBUG("Escribiendo PNG con paleta: %ux%u bpp=%u", final_image.width, final_image.height, final_image.bpp);
            writer_save_png_palette(outfn, &final_image, color_array);
        } else {
            writer_save_png(outfn, &final_image);
        }
    }
    LOG_INFO("Imagen guardada en: %s", outfn);

    status = 0; // Éxito

cleanup:
    // --- Liberación de memoria ---
    if (nav_loaded) {
        dataf_destroy(&navla_full);
        dataf_destroy(&navlo_full);
    }
    free_cpt_data(cptdata);
    
    if (expr_mode) {
        for (int i = 1; i <= 16; i++) {
            if (channels[i].fdata.data_in || channels[i].bdata.data_in) {
                datanc_destroy(&channels[i]);
            }
        }
        for (int i = 0; i < num_required_channels; i++) {
            if (required_channels[i]) free(required_channels[i]);
        }
    }
    
    datanc_destroy(&c01);
    dataf_destroy(&result_data); // Seguro aunque esté vacío
    image_destroy(&final_image);
    color_array_destroy(color_array);
    if (out_filename_generated) free(out_filename_generated);
    if (palette_name) free(palette_name);

    return status;
}
