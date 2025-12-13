#define _GNU_SOURCE
/*
 * RGB and day/night composite generation module.
 *
 * Copyright (c) 2025  Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 */
#include "rgb.h"
#include "clip_loader.h"
#include "datanc.h"
#include "filename_utils.h"
#include "image.h"
#include "logger.h"
#include "reader_nc.h"
#include "reprojection.h"
#include "writer_geotiff.h"
#include "reader_png.h"
#include "writer_png.h"
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Estructuras para manejo de canales ---
typedef struct {
  const char *name;
  char *filename;
} ChannelInfo;

typedef struct {
  ChannelInfo *channels;
  int count;
  char *id_signature;
} ChannelSet;

// --- Prototipos de funciones internas ---
ImageData create_truecolor_rgb(DataF B, DataF R, DataF NiR);
ImageData create_truecolor_rgb_rayleigh(DataF B, DataF R, DataF NiR,
                                        const char *filename, bool apply_rayleigh);
ImageData create_nocturnal_pseudocolor(const DataF* temp_data, const ImageData* fondo);
ImageData blend_images(ImageData bottom, ImageData top, ImageData mask);

// NOTA: Eliminamos el prototipo local de image_crop para usar el de image.h

// --- Función helper para procesar coordenadas de clip ---
static bool process_clip_coords(ArgParser *parser, const char *clip_csv_path,
                                float clip_coords[4]) {
  if (!ap_found(parser, "clip")) {
    return false;
  }

  const char *clip_value = ap_get_str_value(parser, "clip");
  if (!clip_value || strlen(clip_value) == 0) {
    return false;
  }

  // Intentar parsear como 4 coordenadas separadas por comas o espacios
  float coords[4];
  int parsed = sscanf(clip_value, "%f%*[, ]%f%*[, ]%f%*[, ]%f", &coords[0],
                      &coords[1], &coords[2], &coords[3]);

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
    LOG_ERROR("No se encontró el recorte con clave '%s' en %s", clip_value,
              clip_csv_path);
    LOG_ERROR("Formato esperado: clave (ej. 'mexico') o coordenadas "
              "\"lon_min,lat_max,lon_max,lat_min\"");
    return false;
  }

  LOG_INFO("Usando recorte '%s': %s", clip_value, clip.region);
  clip_coords[0] = clip.ul_x; // lon_min
  clip_coords[1] = clip.ul_y; // lat_max
  clip_coords[2] = clip.lr_x; // lon_max
  clip_coords[3] = clip.lr_y; // lat_min
  return true;
}

// --- Funciones de normalización y creación de RGB ---
static unsigned char *normalize_to_u8(const DataF *data, float min_val,
                                      float max_val) {
  size_t buffer_size = data->size * sizeof(unsigned char);
  unsigned char *buffer = malloc(buffer_size);
  // Usamos la macro MALLOC_CHECK para estandarizar el manejo de errores de memoria.
  // Esta macro termina el programa si la memoria no se puede asignar.
  // Si quisiéramos propagar el error, devolveríamos NULL y lo manejaríamos en la función que llama.
  // Por ahora, un fallo aquí es suficientemente crítico para terminar.
  MALLOC_CHECK(buffer, buffer_size);

  float range = max_val - min_val;
  if (range < 1e-9)
    range = 1.0f;

#pragma omp parallel for
  for (size_t i = 0; i < data->size; i++) {
    float val = data->data_in[i];
    if (IS_NONDATA(val)) {
      buffer[i] = 0;
    } else {
      float normalized = (val - min_val) / range;
      if (normalized < 0.0f)
        normalized = 0.0f;
      if (normalized > 1.0f)
        normalized = 1.0f;
      buffer[i] = (unsigned char)(normalized * 255.0f);
    }
  }
  return buffer;
}

ImageData create_multiband_rgb(const DataF *r_ch, const DataF *g_ch,
                               const DataF *b_ch, float r_min, float r_max,
                               float g_min, float g_max, float b_min,
                               float b_max) {
  if (r_ch->size != g_ch->size || r_ch->size != b_ch->size) {
    LOG_ERROR(
        "Las dimensiones de las bandas de entrada para RGB no coinciden.");
    return image_create(0, 0, 0);
  }

  ImageData imout = image_create(r_ch->width, r_ch->height, 3);
  if (imout.data == NULL)
    return imout;

  // Verificar la asignación de memoria para cada canal normalizado.
  unsigned char *r_norm = normalize_to_u8(r_ch, r_min, r_max);
  if (!r_norm) { image_destroy(&imout); return imout; }

  unsigned char *g_norm = normalize_to_u8(g_ch, g_min, g_max);
  if (!g_norm) { free(r_norm); image_destroy(&imout); return imout; }

  unsigned char *b_norm = normalize_to_u8(b_ch, b_min, b_max);
  if (!b_norm) {
    free(r_norm);
    free(g_norm);
    image_destroy(&imout);
    return imout;
  }

  size_t num_pixels = imout.width * imout.height;
#pragma omp parallel for
  for (size_t i = 0; i < num_pixels; i++) {
    imout.data[i * 3] = r_norm[i];
    imout.data[i * 3 + 1] = g_norm[i];
    imout.data[i * 3 + 2] = b_norm[i];
  }

  free(r_norm);
  free(g_norm);
  free(b_norm);
  return imout;
}

ImageData create_daynight_mask(DataNC datanc, DataF navla, DataF navlo,
                               float *dnratio, float max_tmp);

// --- Gestión de ChannelSet ---
ChannelSet *channelset_create(const char *channel_names[], int count) {
  ChannelSet *set = malloc(sizeof(ChannelSet));
  if (!set) {
    LOG_FATAL("Falla de memoria al crear ChannelSet.");
    return NULL;
  }

  set->channels = malloc(sizeof(ChannelInfo) * count);
  if (!set->channels) {
    LOG_FATAL("Falla de memoria para los canales en ChannelSet.");
    free(set);
    return NULL;
  }

  set->id_signature = malloc(40);
  if (!set->id_signature) {
    LOG_FATAL("Falla de memoria para la firma de ID en ChannelSet.");
    free(set->channels);
    free(set);
    return NULL;
  }

  set->count = count;
  for (int i = 0; i < count; i++) {
    set->channels[i].name = channel_names[i];
    set->channels[i].filename = NULL;
  }
  return set;
}

void channelset_destroy(ChannelSet *set) {
  if (!set)
    return;
  if (set->channels) {
    for (int i = 0; i < set->count; i++) {
      free(set->channels[i].filename);
    }
    free(set->channels);
  }
  free(set->id_signature);
  free(set);
}

int find_id_from_name(const char *name, char *id_buffer) {
  const char *s_pos = strstr(name, "_s");
  if (!s_pos)
    return -1;
  const char *e_pos = strstr(s_pos, "_e");
  if (!e_pos)
    return -1;

  size_t length = (size_t)(e_pos - s_pos);
  strncpy(id_buffer, s_pos + 1, length - 1);
  id_buffer[length] = '\0';
  return 0;
}

char *concat(const char *s1, const char *s2) {
  char *result = malloc(strlen(s1) + strlen(s2) + 2);
  if (!result)
    return NULL;
  sprintf(result, "%s/%s", s1, s2);
  return result;
}

int find_channel_filenames(const char *dirnm, ChannelSet *channelset,
                           bool is_l2_product) {
  DIR *d = opendir(dirnm);
  if (!d)
    return -1;

  struct dirent *dir;
  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, channelset->id_signature) &&
        strstr(dir->d_name, ".nc")) {
      if (is_l2_product && !strstr(dir->d_name, "CMIP"))
        continue;

      for (int i = 0; i < channelset->count; i++) {
        if (strstr(dir->d_name, channelset->channels[i].name)) {
          free(channelset->channels[i].filename);
          channelset->channels[i].filename = concat(dirnm, dir->d_name);
          LOG_DEBUG("Encontrado archivo para canal %s: %s",
                    channelset->channels[i].name,
                    channelset->channels[i].filename);
          break;
        }
      }
    }
  }
  closedir(d);
  return 0;
}

// --- Función Principal del Módulo RGB ---

int run_rgb(ArgParser *parser) {
  LogLevel log_level = ap_found(parser, "verbose") ? LOG_DEBUG : LOG_INFO;
  logger_init(log_level);
  LOG_DEBUG("Modo verboso activado para el comando 'rgb'.");

  const char *input_file;
  if (ap_has_args(parser)) {
    input_file = ap_get_arg_at_index(parser, 0);
  } else {
    LOG_ERROR("El comando 'rgb' requiere un archivo NetCDF de entrada como "
              "referencia.");
    return -1;
  }

  // --- Opciones ---
  const bool do_reprojection = ap_found(parser, "geographics");
  const float gamma = ap_get_dbl_value(parser, "gamma");
  const char *mode = ap_get_str_value(parser, "mode");
  const bool apply_histogram = ap_found(parser, "histo");
  const bool clahe_flag = ap_found(parser, "clahe");
  const char* clahe_params = ap_get_str_value(parser, "clahe-params");
  const bool force_geotiff = ap_found(parser, "geotiff");
  const bool apply_rayleigh = ap_found(parser, "rayleigh");
  const bool use_citylights = ap_found(parser, "citylights");
  const bool use_alpha = ap_found(parser, "alpha");
  
  // CLAHE se activa si se da --clahe o --clahe-params
  const bool apply_clahe = clahe_flag || (clahe_params != NULL);
  
  // Parsear parámetros de CLAHE
  int clahe_tiles_x = 8, clahe_tiles_y = 8;
  float clahe_clip_limit = 4.0f;
  if (apply_clahe && clahe_params) {
      int parsed = sscanf(clahe_params, "%d,%d,%f", &clahe_tiles_x, &clahe_tiles_y, &clahe_clip_limit);
      if (parsed < 3) {
          if (parsed < 1) clahe_tiles_x = 8;
          if (parsed < 2) clahe_tiles_y = 8;
          if (parsed < 3) clahe_clip_limit = 4.0f;
      }
      LOG_DEBUG("CLAHE params: tiles=%dx%d, clip_limit=%.2f", clahe_tiles_x, clahe_tiles_y, clahe_clip_limit);
  }

  const char *basenm = basename((char *)input_file);
  char *dirnm_dup = strdup(input_file);
  const char *dirnm = dirname(dirnm_dup);

  bool is_l2_product = (strstr(basenm, "CMIP") != NULL);

  // --- Configurar conjunto de canales ---
  ChannelSet *channels = NULL;
  if (strcmp(mode, "composite") == 0 || strcmp(mode, "truecolor") == 0) {
    const char *required[] = {"C01", "C02", "C03", "C13"};
    channels = channelset_create(required, 4);
  } else if (strcmp(mode, "night") == 0) {
    const char *required[] = {"C13"};
    channels = channelset_create(required, 1);
  } else if (strcmp(mode, "ash") == 0) {
    const char *required[] = {"C11", "C13", "C14", "C15"};
    channels = channelset_create(required, 4);
  } else if (strcmp(mode, "airmass") == 0) {
    const char *required[] = {"C08", "C10", "C12", "C13"};
    channels = channelset_create(required, 4);
  } else if (strcmp(mode, "so2") == 0) {
    const char *required[] = {"C09", "C10", "C11", "C13"};
    channels = channelset_create(required, 4);
  } else {
    LOG_ERROR("Modo '%s' no reconocido.", mode);
    free(dirnm_dup);
    return -1;
  }

  if (!channels) {
    free(dirnm_dup);
    return -1;
  }

  if (!channels) {
    LOG_FATAL("Falla de memoria al crear el conjunto de canales.");
    free(dirnm_dup);
    return -1;
  }
  if (find_id_from_name(basenm, channels->id_signature) != 0) {
    LOG_ERROR("No se pudo extraer el ID del nombre de archivo: %s", basenm);
    channelset_destroy(channels);
    free(dirnm_dup);
    return -1;
  }

  if (find_channel_filenames(dirnm, channels, is_l2_product) != 0) {
    LOG_ERROR("No se pudo acceder al directorio %s", dirnm);
    channelset_destroy(channels);
    free(dirnm_dup);
    return -1;
  }
  free(dirnm_dup);

  // Mapeo de canales
  ChannelInfo *c_info[17] = {NULL};
  for (int i = 0; i < channels->count; i++) {
    if (channels->channels[i].filename == NULL) {
      LOG_ERROR("Archivo faltante para canal %s", channels->channels[i].name);
      channelset_destroy(channels);
      return -1;
    }
    int cn = atoi(channels->channels[i].name + 1);
    if (cn > 0 && cn <= 16)
      c_info[cn] = &channels->channels[i];
  }

  // --- Carga de Datos ---
  DataNC c[17];
  DataF aux, navlo, navla;
  const char *var_name = is_l2_product ? "CMI" : "Rad";

  for (int i = 1; i <= 16; i++) {
    if (c_info[i])
      load_nc_sf(c_info[i]->filename, var_name, &c[i]);
  }

  // Identificar canal de referencia
  int ref_ch_idx = 0;
  for (int i = 16; i > 0; i--) {
    if (c_info[i]) {
      ref_ch_idx = i;
      break;
    }
  }
  if (ref_ch_idx == 0) {
    channelset_destroy(channels);
    return -1;
  }

  // Downsampling simple
  if (c_info[1]) {
    aux = downsample_boxfilter(c[1].fdata, 2);
    dataf_destroy(&c[1].fdata);
    c[1].fdata = aux;
  }
  if (c_info[2]) {
    aux = downsample_boxfilter(c[2].fdata, 4);
    dataf_destroy(&c[2].fdata);
    c[2].fdata = aux;
  }
  if (c_info[3]) {
    aux = downsample_boxfilter(c[3].fdata, 2);
    dataf_destroy(&c[3].fdata);
    c[3].fdata = aux;
  }

  // Calcular Navegación
  compute_navigation_nc(c_info[ref_ch_idx]->filename, &navla, &navlo);

  // --- Manejo de recorte (CLIP) ---
  unsigned int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
  float clip_coords[4] = {0};
  bool has_clip = process_clip_coords(
      parser, "/usr/local/share/lanot/docs/recortes_coordenadas.csv",
      clip_coords);

  float final_lon_min = 0, final_lon_max = 0, final_lat_min = 0,
        final_lat_max = 0;
  unsigned crop_x_offset = 0, crop_y_offset = 0;

  // --- RECORTE PRE-REPROYECCIÓN (Geostacionario) ---
  if (has_clip && do_reprojection) {
    int ix, iy, iw, ih, vs;
    vs = reprojection_find_bounding_box(&navla, &navlo, clip_coords[0],
                                        clip_coords[1], clip_coords[2],
                                        clip_coords[3], &ix, &iy, &iw, &ih);

    if (vs > 4) {
      clip_x = (unsigned int)ix;
      clip_y = (unsigned int)iy;
      clip_w = (unsigned int)iw;
      clip_h = (unsigned int)ih;
      LOG_INFO("Recorte PRE-reproyección: Start(%u, %u) Size(%u, %u)", clip_x,
               clip_y, clip_w, clip_h);

      for (int i = 1; i <= 16; i++) {
        if (c_info[i]) {
          DataF cropped =
              dataf_crop(&c[i].fdata, clip_x, clip_y, clip_w, clip_h);
          dataf_destroy(&c[i].fdata);
          c[i].fdata = cropped;
        }
      }
      DataF navla_c = dataf_crop(&navla, clip_x, clip_y, clip_w, clip_h);
      DataF navlo_c = dataf_crop(&navlo, clip_x, clip_y, clip_w, clip_h);
      dataf_destroy(&navla);
      dataf_destroy(&navlo);
      navla = navla_c;
      navlo = navlo_c;
    }
  }

  // --- REPROYECCIÓN ---
  if (do_reprojection) {
    LOG_INFO("Iniciando reproyección a coordenadas geográficas...");
    bool first = true;
    const char *nav_ref = c_info[ref_ch_idx]->filename;

    for (int i = 1; i <= 16; i++) {
      if (c_info[i]) {
        float lmin, lmax, ltmin, ltmax;
        DataF repro;

        if (has_clip) {
          repro = reproject_to_geographics_with_nav(
              &c[i].fdata, &navla, &navlo, c[i].native_resolution_km, &lmin,
              &lmax, &ltmin, &ltmax, clip_coords);
        } else {
          repro = reproject_to_geographics(&c[i].fdata, nav_ref,
                                           c[i].native_resolution_km, &lmin,
                                           &lmax, &ltmin, &ltmax);
        }

        dataf_destroy(&c[i].fdata);
        c[i].fdata = repro;

        if (first) {
          final_lon_min = lmin;
          final_lon_max = lmax;
          final_lat_min = ltmin;
          final_lat_max = ltmax;
          first = false;
        }
      }
    }

    dataf_destroy(&navla);
    dataf_destroy(&navlo);
    create_navigation_from_reprojected_bounds(
        &navla, &navlo, c[ref_ch_idx].fdata.width, c[ref_ch_idx].fdata.height,
        final_lon_min, final_lon_max, final_lat_min, final_lat_max);

    // --- RECORTE POST-REPROYECCIÓN ---
    if (has_clip) {
      size_t curr_w = c[ref_ch_idx].fdata.width;
      size_t curr_h = c[ref_ch_idx].fdata.height;

      int ix_s = (int)(((clip_coords[0] - final_lon_min) /
                        (final_lon_max - final_lon_min)) *
                       curr_w);
      int iy_s = (int)(((final_lat_max - clip_coords[1]) /
                        (final_lat_max - final_lat_min)) *
                       curr_h); // Y invertida
      int ix_e = (int)(((clip_coords[2] - final_lon_min) /
                        (final_lon_max - final_lon_min)) *
                       curr_w);
      int iy_e = (int)(((final_lat_max - clip_coords[3]) /
                        (final_lat_max - final_lat_min)) *
                       curr_h);

      if (ix_s < 0)
        ix_s = 0;
      if (iy_s < 0)
        iy_s = 0;
      if (ix_e > (int)curr_w)
        ix_e = (int)curr_w;
      if (iy_e > (int)curr_h)
        iy_e = (int)curr_h;

      unsigned int crop_w = (unsigned int)abs(ix_e - ix_s);
      unsigned int crop_h = (unsigned int)abs(iy_e - iy_s);

      if (crop_w > 0 && crop_h > 0) {
        LOG_INFO("Recorte POST-reproyección: Offset(%d, %d) Size(%u, %u)", ix_s,
                 iy_s, crop_w, crop_h);

        float pixel_w = (final_lon_max - final_lon_min) / curr_w;
        float pixel_h = (final_lat_min - final_lat_max) / curr_h;

        // Ajustar origen para el writer
        final_lon_min = final_lon_min + ix_s * pixel_w;
        final_lat_max = final_lat_max + iy_s * pixel_h;

        // Ajustar extremos (para mantener consistencia)
        final_lon_max = final_lon_min + crop_w * pixel_w;
        final_lat_min = final_lat_max + crop_h * pixel_h;

        for (int i = 1; i <= 16; i++)
          if (c_info[i]) {
            DataF cropped = dataf_crop(&c[i].fdata, ix_s, iy_s, crop_w, crop_h);
            dataf_destroy(&c[i].fdata);
            c[i].fdata = cropped;
          }

        dataf_destroy(&navla);
        dataf_destroy(&navlo);
        create_navigation_from_reprojected_bounds(
            &navla, &navlo, crop_w, crop_h, final_lon_min, final_lon_max,
            final_lat_min, final_lat_max);
      }
    }
  } else {
    // MODO NATIVO (GEOESTACIONARIO)
    if (has_clip) {
      int ix, iy, iw, ih;
      if (reprojection_find_bounding_box(
              &navla, &navlo, clip_coords[0], clip_coords[1], clip_coords[2],
              clip_coords[3], &ix, &iy, &iw, &ih) > 4) {
        crop_x_offset = (unsigned)ix;
        crop_y_offset = (unsigned)iy;
        clip_w = (unsigned)iw;
        clip_h = (unsigned)ih;

        LOG_INFO("Aplicando recorte Nativo: Start(%u, %u) Size(%u, %u)", ix, iy,
                 iw, ih);
        for (int i = 1; i <= 16; i++)
          if (c_info[i]) {
            DataF cropped = dataf_crop(&c[i].fdata, ix, iy, iw, ih);
            dataf_destroy(&c[i].fdata);
            c[i].fdata = cropped;
          }
        DataF navla_c = dataf_crop(&navla, ix, iy, iw, ih);
        DataF navlo_c = dataf_crop(&navlo, ix, iy, iw, ih);
        dataf_destroy(&navla);
        dataf_destroy(&navlo);
        navla = navla_c;
        navlo = navlo_c;
      }
    }
  }

  // --- Generación del nombre de archivo ---
  char *out_filename_generated = NULL;
  const char *out_filename;
  if (ap_found(parser, "out")) {
    const char* user_out = ap_get_str_value(parser, "out");
    // Detectar si hay patrón (contiene llaves)
    if (strchr(user_out, '{') && strchr(user_out, '}')) {
      const char *ts_ref = c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : input_file;
      out_filename_generated = expand_filename_pattern(user_out, ts_ref);
      out_filename = out_filename_generated;
    } else {
      out_filename = user_out;
    }
    
    // Si se fuerza GeoTIFF pero el nombre tiene extensión .png, cambiarla a .tif
    if (force_geotiff && out_filename) {
      const char *ext = strrchr(out_filename, '.');
      if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0)) {
        size_t base_len = ext - out_filename;
        out_filename_generated = malloc(base_len + 5); // espacio para ".tif\0"
        if (out_filename_generated) {
          strncpy(out_filename_generated, out_filename, base_len);
          strcpy(out_filename_generated + base_len, ".tif");
          LOG_INFO("Extensión cambiada de .png a .tif por usar --geotiff: %s", out_filename_generated);
          out_filename = out_filename_generated;
        }
      }
    }
  } else {
    const char *ts_ref =
        c_info[ref_ch_idx] ? c_info[ref_ch_idx]->filename : input_file;
    const char *ext = force_geotiff ? ".tif" : ".png";
    char *base_fn = generate_default_output_filename(ts_ref, mode, ext);
    if (do_reprojection && base_fn) {
      char *dot = strrchr(base_fn, '.');
      if (dot) {
        size_t pre_len = dot - base_fn;
        size_t total = strlen(base_fn) + 5;
        out_filename_generated = malloc(total);
        if (out_filename_generated) {
          strncpy(out_filename_generated, base_fn, pre_len);
          out_filename_generated[pre_len] = '\0';
          strcat(out_filename_generated, "_geo");
          strcat(out_filename_generated, dot);
          free(base_fn);
        } else
          out_filename_generated = base_fn;
      } else
        out_filename_generated = base_fn;
    } else
      out_filename_generated = base_fn;
    out_filename = out_filename_generated;
  }

  // --- Composición de Imágenes ---
  ImageData final_image = {0};
  DataF r_ch = {0}, g_ch = {0}, b_ch = {0};

  if (strcmp(mode, "truecolor") == 0) {
    LOG_INFO("Generando 'truecolor'...");
    final_image =
        apply_rayleigh
            ? create_truecolor_rgb_rayleigh(c[1].fdata, c[2].fdata, c[3].fdata,
                                            c_info[1]->filename, true)
            : create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
  } else if (strcmp(mode, "night") == 0) {
    LOG_INFO("Generando 'night'...");
    ImageData citylights_bg = {0};
    if (use_citylights) {
        const char* bg_path = (c[ref_ch_idx].fdata.width == 2500)
            ? "/usr/local/share/lanot/images/land_lights_2012_conus.png"
            : "/usr/local/share/lanot/images/land_lights_2012_fd.png";
        
        LOG_INFO("Cargando fondo de luces de ciudad: %s", bg_path);
        ImageData raw_bg = reader_load_png(bg_path);
        if (raw_bg.data) {
            // Remuestrear el fondo para que coincida con las dimensiones del canal 13
            if (raw_bg.width != c[13].fdata.width || raw_bg.height != c[13].fdata.height) {
                LOG_INFO("Remuestreando fondo de %ux%u a %zux%zu", 
                         raw_bg.width, raw_bg.height, c[13].fdata.width, c[13].fdata.height);
                int factor = raw_bg.width / c[13].fdata.width;
                if (factor > 1) {
                    citylights_bg = image_downsample_boxfilter(&raw_bg, factor);
                } else {
                    LOG_WARN("No se pudo determinar factor de remuestreo para el fondo. Ignorando.");
                }
                image_destroy(&raw_bg);
            } else {
                citylights_bg = raw_bg;
            }
        } else {
            LOG_WARN("No se pudo cargar el archivo de fondo. Se continuará sin él.");
        }
    }
    final_image = create_nocturnal_pseudocolor(&c[13].fdata, citylights_bg.data ? &citylights_bg : NULL);
    image_destroy(&citylights_bg);
  } else if (strcmp(mode, "ash") == 0) {
    LOG_INFO("Generando 'ash'...");
    r_ch = dataf_op_dataf(&c[15].fdata, &c[13].fdata, OP_SUB);
    g_ch = dataf_op_dataf(&c[14].fdata, &c[11].fdata, OP_SUB);
    final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -6.7f, 2.6f,
                                       -6.0f, 6.3f, 243.6f, 302.4f);
  } else if (strcmp(mode, "airmass") == 0) {
    LOG_INFO("Generando 'airmass'...");
    r_ch = dataf_op_dataf(&c[8].fdata, &c[10].fdata, OP_SUB);
    g_ch = dataf_op_dataf(&c[12].fdata, &c[13].fdata, OP_SUB);
    b_ch = dataf_op_scalar(&c[8].fdata, 273.15f, OP_SUB, true);
    final_image = create_multiband_rgb(&r_ch, &g_ch, &b_ch, -26.2f, 0.6f,
                                       -43.2f, 6.7f, 29.25f, 64.65f);
  } else if (strcmp(mode, "so2") == 0) {
    LOG_INFO("Generando 'so2'...");
    r_ch = dataf_op_dataf(&c[9].fdata, &c[10].fdata, OP_SUB);
    g_ch = dataf_op_dataf(&c[13].fdata, &c[11].fdata, OP_SUB);
    final_image = create_multiband_rgb(&r_ch, &g_ch, &c[13].fdata, -4.0f, 2.0f,
                                       -4.0f, 5.0f, 233.0f, 300.0f);
  } else { // composite
    LOG_INFO("Generando 'composite'...");
    ImageData diurna =
        apply_rayleigh
            ? create_truecolor_rgb_rayleigh(c[1].fdata, c[2].fdata, c[3].fdata,
                                            c_info[1]->filename, true)
            : create_truecolor_rgb(c[1].fdata, c[2].fdata, c[3].fdata);
    if (apply_histogram)
      image_apply_histogram(diurna);
    if (apply_clahe)
      image_apply_clahe(diurna, clahe_tiles_x, clahe_tiles_y, clahe_clip_limit);

    ImageData citylights_bg = {0};
    if (use_citylights) {
        const char* bg_path = (c[ref_ch_idx].fdata.width == 2500)
            ? "/usr/local/share/lanot/images/land_lights_2012_conus.png"
            : "/usr/local/share/lanot/images/land_lights_2012_fd.png";
        
        LOG_INFO("Cargando fondo de luces de ciudad: %s", bg_path);
        ImageData raw_bg = reader_load_png(bg_path);
        if (raw_bg.data) {
            if (raw_bg.width != c[13].fdata.width || raw_bg.height != c[13].fdata.height) {
                LOG_INFO("Remuestreando fondo de %ux%u a %zux%zu", 
                         raw_bg.width, raw_bg.height, c[13].fdata.width, c[13].fdata.height);
                int factor = raw_bg.width / c[13].fdata.width;
                citylights_bg = image_downsample_boxfilter(&raw_bg, factor);
                image_destroy(&raw_bg);
            } else {
                citylights_bg = raw_bg;
            }
        } else {
            LOG_WARN("No se pudo cargar el archivo de fondo. Se continuará sin él.");
        }
    }
    ImageData nocturna = create_nocturnal_pseudocolor(&c[13].fdata, citylights_bg.data ? &citylights_bg : NULL);
    image_destroy(&citylights_bg);
    float dnratio;
    ImageData mask =
        create_daynight_mask(c[13], navla, navlo, &dnratio, 263.15);
    LOG_INFO("Ratio día/noche: %.2f%%", dnratio);
    final_image = blend_images(nocturna, diurna, mask);
    image_destroy(&diurna);
    image_destroy(&nocturna);
    image_destroy(&mask);
  }

  if (gamma != 1.0)
    image_apply_gamma(final_image, gamma);
  
  // Para modos que no sean composite, aplicar histogram/clahe al final
  // (composite ya aplicó a 'diurna' antes del blend)
  if (strcmp(mode, "composite") != 0) {
    if (apply_histogram)
      image_apply_histogram(final_image);
    if (apply_clahe)
      image_apply_clahe(final_image, clahe_tiles_x, clahe_tiles_y, clahe_clip_limit);
  }

  // --- CREAR MÁSCARA ALPHA (si se solicitó) ---
  // Se crea aquí al final para tener las dimensiones correctas de la imagen
  ImageData alpha_mask = {0};
  if (use_alpha) {
    LOG_INFO("Creando máscara alpha desde canal de referencia...");
    alpha_mask = image_create_alpha_mask_from_dataf(&c[ref_ch_idx].fdata);
    if (alpha_mask.data == NULL) {
      LOG_WARN("No se pudo crear la máscara alpha, continuando sin canal alpha.");
    }
  }

  // --- REMUESTREO (si se solicitó) ---
  const int scale = ap_get_int_value(parser, "scale");
  if (scale < 0) {
    ImageData scaled = image_downsample_boxfilter(&final_image, -scale);
    image_destroy(&final_image);
    final_image = scaled;
    
    // Remuestrear máscara alpha también
    if (alpha_mask.data) {
      ImageData scaled_mask = image_downsample_boxfilter(&alpha_mask, -scale);
      image_destroy(&alpha_mask);
      alpha_mask = scaled_mask;
    }
  } else if (scale > 1) {
    ImageData scaled = image_upsample_bilinear(&final_image, scale);
    image_destroy(&final_image);
    final_image = scaled;
    
    // Remuestrear máscara alpha también
    if (alpha_mask.data) {
      ImageData scaled_mask = image_upsample_bilinear(&alpha_mask, scale);
      image_destroy(&alpha_mask);
      alpha_mask = scaled_mask;
    }
  }

  // --- AGREGAR CANAL ALPHA (si se solicitó) ---
  if (use_alpha && alpha_mask.data) {
    LOG_INFO("Agregando canal alpha a la imagen final...");
    ImageData with_alpha = image_add_alpha_channel(&final_image, &alpha_mask);
    if (with_alpha.data) {
      image_destroy(&final_image);
      final_image = with_alpha;
    } else {
      LOG_WARN("No se pudo agregar el canal alpha.");
    }
  }

  // --- ESCRITURA FINAL ---
  bool is_geotiff =
      force_geotiff || (out_filename && (strstr(out_filename, ".tif") ||
                                         strstr(out_filename, ".tiff")));

  if (is_geotiff) {
    DataNC meta_out = {0};

    if (do_reprojection) {
      // Construir metadatos LAT/LON manualmente
      meta_out.proj_code = PROJ_LATLON;

      size_t w = final_image.width;
      size_t h = final_image.height;

      meta_out.geotransform[0] = final_lon_min;
      meta_out.geotransform[1] = (final_lon_max - final_lon_min) / (double)w;
      meta_out.geotransform[2] = 0.0;
      meta_out.geotransform[3] = final_lat_max;
      meta_out.geotransform[4] = 0.0;
      meta_out.geotransform[5] = (final_lat_min - final_lat_max) / (double)h;

      write_geotiff_rgb(out_filename, &final_image, &meta_out, 0, 0);

    } else {
      // MODO NATIVO
      meta_out = c[ref_ch_idx];
      
      // Ajustar geotransform si se aplicó scale
      const int scale = ap_get_int_value(parser, "scale");
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
      
      write_geotiff_rgb(out_filename, &final_image, &meta_out, crop_x_offset,
                        crop_y_offset);
    }
  } else {
    writer_save_png(out_filename, &final_image);
  }
  LOG_INFO("Imagen guardada en: %s", out_filename);

  dataf_destroy(&r_ch);
  dataf_destroy(&g_ch);
  dataf_destroy(&b_ch);
  if (out_filename_generated)
    free(out_filename_generated);
  image_destroy(&final_image);
  image_destroy(&alpha_mask);
  for (int i = 1; i <= 16; i++) {
    if (c_info[i])
      datanc_destroy(&c[i]);
  }
  dataf_destroy(&navla);
  dataf_destroy(&navlo);
  channelset_destroy(channels);
  return 0;
}
