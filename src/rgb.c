/* RGB and day/night composite generation for ABI multi-band imagery.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 */
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "clip_loader.h"
#include "config.h"
#ifdef HPSV_CUDA
#include "cuda_daynite.h"
#include "cuda_kernels.h"
#endif
#include "datanc.h"
#include "daynight_mask.h"
#include "image.h"
#include "logger.h"
#include "metadata.h"
#include "nocturnal_pseudocolor.h"
#include "parse_expr.h"
#include "processing.h"
#include "rayleigh.h"
#include "reader_nc.h"
#include "nav_plan.h"
#include "reader_webp.h"
#include "reprojection.h"
#include "rgb.h"
#include "truecolor.h"
#include "writer_geotiff.h"
#include "writer_png.h"

void rgb_context_init(RgbContext *ctx) {
    memset(ctx, 0, sizeof(RgbContext));
    ctx->opts.gamma[0] = ctx->opts.gamma[1] = ctx->opts.gamma[2] = 1.0f;
    ctx->opts.clahe_tiles_x = 8;
    ctx->opts.clahe_tiles_y = 8;
    ctx->opts.clahe_clip_limit = 4.0f;
    ctx->opts.scale = 1;
}

void rgb_context_destroy(RgbContext *ctx) {
    if (!ctx)
        return;

    channelset_destroy(ctx->channel_set);

    for (int i = 1; i <= 16; i++) {
        datanc_destroy(&ctx->channels[i]);
    }

    dataf_destroy(&ctx->nav_lat);
    dataf_destroy(&ctx->nav_lon);

    dataf_destroy(&ctx->comp_r);
    dataf_destroy(&ctx->comp_g);
    dataf_destroy(&ctx->comp_b);

    image_destroy(&ctx->final_image);
    image_destroy(&ctx->alpha_mask);

#ifdef HPSV_CUDA
    cuda_free_device_image((unsigned char *)ctx->d_final_image);
#endif
    ctx->d_final_image = NULL;

    if (ctx->opts.output_generated && ctx->opts.output_filename) {
        free(ctx->opts.output_filename);
    }
}

// --- PHASE 2: COMPOSERS (STRATEGY PATTERN) ---

// El archivo del canal de referencia define la rejilla de navegación (lat/lon se
// calculan a esa resolución, sin remuestrear). Compartido por process_geospatial
// y la ruta CUDA: en daynite la referencia es C13, no C01, y construir el plan
// desde el archivo equivocado da una rejilla del tamaño que no es.
static const char *rgb_ref_filename(const RgbContext *ctx) {
    char ref_name[8];
    snprintf(ref_name, sizeof(ref_name), "C%02d", ctx->ref_channel_idx);
    for (int i = 0; i < ctx->channel_set->count; i++) {
        if (strcmp(ctx->channel_set->channels[i].name, ref_name) == 0)
            return ctx->channel_set->channels[i].filename;
    }
    return ctx->channel_set->channels[0].filename; // fallback
}

// Única definición de "esta corrida la compone la GPU": la consultan tanto
// run_rgb (para despachar) como process_geospatial (para saltarse el cálculo de
// lat/lon en CPU, que la GPU va a rehacer). Tenerla en dos lugares sería una
// fuente de desincronización silenciosa.
static bool truecolor_cuda_eligible(const RgbOptions *o) {
#ifdef HPSV_CUDA
    return o->use_cuda && strcmp(o->mode, "truecolor") == 0 &&
           !o->rayleigh_analytic;
#else
    (void)o;
    return false;
#endif
}

// Contraparte para daynite. No incluye apply_rayleigh porque compose_daynite_cuda
// lo fuerza él mismo; sí excluye las luces de ciudad, que siguen en CPU.
static bool daynite_cuda_eligible(const RgbOptions *o) {
#ifdef HPSV_CUDA
    return o->use_cuda && strcmp(o->mode, "daynite") == 0 && !o->rayleigh_analytic &&
           !o->use_sharpen && !o->use_citylights;
#else
    (void)o;
    return false;
#endif
}

// Locates the C01 file and loads Rayleigh viewing geometry (sza/vza/raa) at the
// given target resolution, reusing pre-computed lat/lon when available. Shared
// by the CPU and CUDA true-color paths.
static bool load_rayleigh_nav(RgbContext *ctx, RayleighNav *nav,
                              unsigned int w, unsigned int h) {
    const char *nav_file = NULL;
    for (int i = 0; i < ctx->channel_set->count; i++) {
        if (strcmp(ctx->channel_set->channels[i].name, "C01") == 0) {
            nav_file = ctx->channel_set->channels[i].filename;
            break;
        }
    }
#ifdef HPSV_CUDA
    // La geometría de vista (sza/vza/raa) es lo más caro de los composers que
    // siguen en CPU: en un daynite de disco completo son 0.28 s de los 0.67 s del
    // composite. Los kernels ya existen (los usa truecolor), así que se calculan
    // en device y se bajan los tres grids para que el resto de la cadena CPU siga
    // igual. No es el port completo del modo, pero captura la mitad del costo sin
    // tocar ningún composer.
    //
    // Solo aplica cuando lat/lon ya están a la resolución pedida; si hay que
    // remuestrear, lo hace la ruta CPU, que ya sabe.
    if (ctx->opts.use_cuda && ctx->has_navigation && ctx->nav_lat.data_in &&
        ctx->nav_lon.data_in && ctx->nav_lat.width == w && ctx->nav_lat.height == h &&
        nav_file) {
        SolarEphemeris eph;
        float sat_lon = 0.0f, sat_h = 0.0f;
        if (reader_solar_ephemeris_from_file(nav_file, &eph) == 0 &&
            reader_read_satellite_params(nav_file, &sat_lon, &sat_h) == 0) {
            DataFDev dla = dataf_dev_upload(&ctx->nav_lat);
            DataFDev dlo = dataf_dev_upload(&ctx->nav_lon);
            DataFDev sza = {0}, vza = {0}, raa = {0};
            bool ok = dla.d_data && dlo.d_data &&
                      compute_rayleigh_nav_dev(&dla, &dlo, eph.sd, eph.cd, eph.ha_base,
                                               sat_lon, sat_h, &sza, &vza, &raa);
            dataf_dev_destroy(&dla);
            dataf_dev_destroy(&dlo);
            if (ok) {
                ok = dataf_dev_download(&sza, &nav->sza) &&
                     dataf_dev_download(&vza, &nav->vza) &&
                     dataf_dev_download(&raa, &nav->raa);
            }
            dataf_dev_destroy(&sza);
            dataf_dev_destroy(&vza);
            dataf_dev_destroy(&raa);
            if (ok) return true;
            // Fallo a medias: soltar lo que se haya bajado y rehacerlo en CPU.
            dataf_destroy(&nav->sza);
            dataf_destroy(&nav->vza);
            dataf_destroy(&nav->raa);
            LOG_WARN("Geometría de vista en device falló; se calcula en CPU.");
        }
    }
#endif

    if (ctx->has_navigation && ctx->nav_lat.data_in && ctx->nav_lon.data_in)
        return rayleigh_load_navigation_from_latlon(nav_file, &ctx->nav_lat,
                                                    &ctx->nav_lon, nav, w, h);
    return rayleigh_load_navigation(nav_file, nav, w, h);
}

static bool compose_truecolor(RgbContext *ctx) {
    // 1. Setup and copy.
    DataF *ch_blue = &ctx->channels[1].fdata; // C01
    DataF *ch_red = &ctx->channels[2].fdata;  // C02
    DataF *ch_nir = &ctx->channels[3].fdata;  // C03

    ctx->comp_b = dataf_copy(ch_blue);
    ctx->comp_r = dataf_copy(ch_red);
    if (!ctx->comp_b.data_in || !ctx->comp_r.data_in)
        return false;

    // Rayleigh correction.
    if (ctx->opts.apply_rayleigh || ctx->opts.rayleigh_analytic) {
        RayleighNav nav = {0};
        bool nav_ok = load_rayleigh_nav(ctx, &nav, ctx->comp_b.width, ctx->comp_b.height);
        if (nav_ok) {
            apply_solar_zenith_correction(&ctx->comp_b, &nav.sza);
            apply_solar_zenith_correction(&ctx->comp_r, &nav.sza);
            apply_solar_zenith_correction(ch_nir, &nav.sza);
            if (ctx->opts.rayleigh_analytic) {
                analytic_rayleigh_correction(&ctx->comp_b, &nav, 0.47);
                analytic_rayleigh_correction(&ctx->comp_r, &nav, 0.64);
            } else {
                luts_rayleigh_correction(&ctx->comp_b, &nav, 1, &ctx->comp_r);
                luts_rayleigh_correction(&ctx->comp_r, &nav, 2, NULL);
            }
            rayleigh_free_navigation(&nav);
        } else {
            LOG_WARN("Failed to load navigation, skipping Rayleigh.");
        }
    }
    // 3. Generate the green channel.
    ctx->comp_g = create_truecolor_synthetic_green(&ctx->comp_b, &ctx->comp_r, ch_nir);
    if (!ctx->comp_g.data_in)
        return false;
    // Green formula already uses geo2grid-matched CIMSS coefficients; no extra boost needed.

    // 3b. Ratio Sharpening
    if (ctx->opts.use_sharpen) {
        DataF ratio_map = dataf_ratio_sharpen_map(&ctx->comp_r);
        if (ratio_map.data_in) {
            DataF new_g = dataf_op_dataf(&ctx->comp_g, &ratio_map, OP_MUL);
            dataf_destroy(&ctx->comp_g);
            ctx->comp_g = new_g;
            DataF new_b = dataf_op_dataf(&ctx->comp_b, &ratio_map, OP_MUL);
            dataf_destroy(&ctx->comp_b);
            ctx->comp_b = new_b;
            dataf_destroy(&ratio_map);
        }
    }

    if (ctx->opts.use_piecewise_stretch) {
        apply_piecewise_stretch(&ctx->comp_r);
        apply_piecewise_stretch(&ctx->comp_g);
        apply_piecewise_stretch(&ctx->comp_b);
    }

    // With piecewise stretch, output is in [0, 1.0];
    // without it, reflectance can exceed 1.0 but is clamped in the 8-bit conversion.
    float range_max = ctx->opts.use_piecewise_stretch ? 1.0f : 1.1f;
    ctx->min_r = 0.0f;
    ctx->max_r = range_max;
    ctx->min_g = 0.0f;
    ctx->max_g = range_max;
    ctx->min_b = 0.0f;
    ctx->max_b = range_max;

    return true;
}

#ifdef HPSV_CUDA
/* Device-resident default true-color: sube C01/C02/C03 una sola vez, sintetiza
 * el verde, aplica gamma por canal y compone en la GPU, y baja una sola imagen
 * RGB. Solo cubre la ruta por defecto (sin Rayleigh/sharpen/stretch, que siguen
 * en CPU). Deja ctx->final_image lista y devuelve true si la manejó; false ante
 * cualquier fallo CUDA para que el llamador reintente por CPU.
 *
 * comp_r = C02 (rojo), comp_b = C01 (azul), comp_g = verde sintetizado de
 * (azul=C01, rojo=C02, nir=C03), igual que compose_truecolor(). */
/* nav_keep_la/lo: si no son NULL y la navegación se resolvió en device, reciben
 * los grids lat/lon en vez de liberarse aquí. daynite los necesita después para
 * la máscara; sin esto habría que volver a subirlos (medido: 0.042 s, y más caro
 * de lo normal porque re-registrar páginas ya registradas cuesta 6x). */
static bool compose_truecolor_cuda(RgbContext *ctx, DataFDev *nav_keep_la,
                                   DataFDev *nav_keep_lo) {
    if (nav_keep_la) *nav_keep_la = (DataFDev){0};
    if (nav_keep_lo) *nav_keep_lo = (DataFDev){0};
    DataF *c01 = &ctx->channels[1].fdata;
    DataF *c02 = &ctx->channels[2].fdata;
    DataF *c03 = &ctx->channels[3].fdata;
    if (!c01->data_in || !c02->data_in || !c03->data_in) return false;

    DataFDev b = dataf_dev_upload(c01);   // azul
    DataFDev r = dataf_dev_upload(c02);   // rojo
    DataFDev nir = dataf_dev_upload(c03);
    DataFDev g = {0};
    bool handled = false;

    if (b.d_data && r.d_data && nir.d_data) {
        // Rayleigh (LUT) chain, device-resident, on the channels already uploaded.
        // The analytic variant stays on CPU (excluded by the gate in run_rgb), so
        // only apply_rayleigh reaches here. The viewing geometry (sza/vza/raa) is
        // also computed on the GPU from the lat/lon grids — the astronomy that
        // used to be the ~8 s CPU bottleneck. Order matches compose_truecolor():
        // solar correction on all three, LUT on blue (redband = red), LUT on red.
        // Anything that prevents the device nav (resolution mismatch, read error,
        // CUDA error) aborts to a full CPU compose so Rayleigh is never dropped.
        bool ray_ok = true;
        if (ctx->opts.apply_rayleigh) {
            const char *nav_file = NULL;
            for (int i = 0; i < ctx->channel_set->count; i++) {
                if (strcmp(ctx->channel_set->channels[i].name, "C01") == 0) {
                    nav_file = ctx->channel_set->channels[i].filename;
                    break;
                }
            }

            SolarEphemeris eph;
            float sat_lon = 0.0f, sat_h = 0.0f;
            DataFDev navla = {0}, navlo = {0}, sza = {0}, vza = {0}, raa = {0};
            bool nav_ready = false;
            bool have_nav = false;

            if (nav_file && reader_solar_ephemeris_from_file(nav_file, &eph) == 0 &&
                reader_read_satellite_params(nav_file, &sat_lon, &sat_h) == 0) {
                if (ctx->nav_on_device) {
                    // Se calcula la malla directamente en device desde el plan de
                    // proyección: ni cómputo en CPU ni subida de los dos grids.
                    // Rejilla desde el canal de referencia; la efeméride y los
                    // parámetros del satélite siguen viniendo de C01, igual que
                    // hace la ruta CPU (compute_navigation_nc sobre el archivo de
                    // referencia, compute_solar_angles_nc sobre C01).
                    NavPlan plan;
                    if (nav_build_plan(rgb_ref_filename(ctx), &plan) == 0) {
                        if (plan.width == b.width && plan.height == b.height) {
                            float la_min, la_max, lo_min, lo_max;
                            have_nav = compute_navigation_dev(&plan, &navla, &navlo,
                                                              &la_min, &la_max,
                                                              &lo_min, &lo_max);
                            if (have_nav) {
                                // Lo único que la ruta host sigue necesitando.
                                ctx->nav_lat.fmin = la_min; ctx->nav_lat.fmax = la_max;
                                ctx->nav_lon.fmin = lo_min; ctx->nav_lon.fmax = lo_max;
                            }
                        } else {
                            LOG_WARN("Navegación en device: resolución %zux%zu != canal %ux%u.",
                                     plan.width, plan.height, b.width, b.height);
                        }
                        nav_plan_destroy(&plan);
                    }
                } else {
                    // Device nav needs lat/lon already at the channel resolution
                    // (true after process_geospatial).
                    if (ctx->has_navigation && ctx->nav_lat.data_in &&
                        ctx->nav_lat.width == b.width && ctx->nav_lat.height == b.height) {
                        navla = dataf_dev_upload(&ctx->nav_lat);
                        navlo = dataf_dev_upload(&ctx->nav_lon);
                        have_nav = (navla.d_data && navlo.d_data);
                    }
                }

                if (have_nav)
                    nav_ready = compute_rayleigh_nav_dev(&navla, &navlo, eph.sd, eph.cd,
                                                         eph.ha_base, sat_lon, sat_h,
                                                         &sza, &vza, &raa);
            }
            if (nav_keep_la && nav_keep_lo && navla.d_data && navlo.d_data) {
                *nav_keep_la = navla; // la propiedad pasa al llamador
                *nav_keep_lo = navlo;
            } else {
                dataf_dev_destroy(&navla);
                dataf_dev_destroy(&navlo);
            }

            // Respaldo: si se difirió la navegación a la GPU y allá falló, hay que
            // calcularla en CPU antes de caer a la composición host — si no, ni el
            // Rayleigh de CPU ni la extensión del reproyectado tendrían de dónde salir.
            if (!nav_ready && ctx->nav_on_device) {
                LOG_WARN("Navegación en device falló; se recalcula en CPU.");
                if (compute_navigation_nc(nav_file, &ctx->nav_lat, &ctx->nav_lon) == 0) {
                    ctx->nav_on_device = false;
                } else {
                    ctx->has_navigation = false;
                }
            }

            if (nav_ready) {
                RayleighLUTDev lut1 = rayleigh_lut_dev_load(1);
                RayleighLUTDev lut2 = rayleigh_lut_dev_load(2);
                if (lut1.d_table && lut2.d_table) {
                    apply_solar_zenith_correction_dev(&b, &sza);
                    apply_solar_zenith_correction_dev(&r, &sza);
                    apply_solar_zenith_correction_dev(&nir, &sza);
                    luts_rayleigh_correction_dev(&b, &sza, &vza, &raa, &lut1, &r);
                    luts_rayleigh_correction_dev(&r, &sza, &vza, &raa, &lut2, NULL);
                } else {
                    ray_ok = false; // CUDA error -> fall back to CPU
                }
                rayleigh_lut_dev_destroy(&lut1);
                rayleigh_lut_dev_destroy(&lut2);
                dataf_dev_destroy(&sza);
                dataf_dev_destroy(&vza);
                dataf_dev_destroy(&raa);
            } else {
                ray_ok = false; // nav unavailable on GPU -> full CPU fallback
            }
        }

        if (ray_ok) g = create_truecolor_green_from_dev(&b, &r, &nir);

        // Orden idéntico a compose_truecolor(): rayleigh -> verde -> sharpen
        // -> stretch. El sharpening lee el rojo y modifica verde y azul en
        // sitio, así que tiene que ir antes de que el stretch toque el rojo.
        if (g.d_data && ctx->opts.use_sharpen) {
            if (!apply_ratio_sharpen_dev(&r, &g, &b)) {
                ray_ok = false;
                dataf_dev_destroy(&g);
            }
        }

        if (g.d_data && ctx->opts.use_piecewise_stretch) {
            if (!apply_piecewise_stretch_dev(&r) || !apply_piecewise_stretch_dev(&g) ||
                !apply_piecewise_stretch_dev(&b)) {
                ray_ok = false;
                dataf_dev_destroy(&g);
            }
        }

        if (g.d_data) {
            /* Con stretch el rango de render queda en [0, 1]; sin él, [0, 1.1]
             * por canal (igual que compose_truecolor()). La gamma por canal
             * normaliza a [0,1] solo el canal al que se aplica. */
            const float range_max = ctx->opts.use_piecewise_stretch ? 1.0f : 1.1f;
            float rmin = 0.0f, rmax = range_max;
            float gmin = 0.0f, gmax = range_max;
            float bmin = 0.0f, bmax = range_max;

            if (fabsf(ctx->opts.gamma[0] - 1.0f) > 1e-6f) {
                dataf_dev_apply_gamma(&r, ctx->opts.gamma[0], rmin, rmax);
                rmin = 0.0f; rmax = 1.0f;
            }
            if (fabsf(ctx->opts.gamma[1] - 1.0f) > 1e-6f) {
                dataf_dev_apply_gamma(&g, ctx->opts.gamma[1], gmin, gmax);
                gmin = 0.0f; gmax = 1.0f;
            }
            if (fabsf(ctx->opts.gamma[2] - 1.0f) > 1e-6f) {
                dataf_dev_apply_gamma(&b, ctx->opts.gamma[2], bmin, bmax);
                bmin = 0.0f; bmax = 1.0f;
            }

            // Se conserva la imagen en device: si nada la modifica en host, la
            // reproyección la consume tal cual y se ahorra el H2D de vuelta.
            unsigned char *d_img = NULL;
            ctx->final_image = create_multiband_rgb_from_dev(
                &r, &g, &b, rmin, rmax, gmin, gmax, bmin, bmax, &d_img);
            handled = (ctx->final_image.data != NULL);
            if (handled) {
                ctx->d_final_image = d_img;
            } else {
                cuda_free_device_image(d_img);
            }
        }
    }

    dataf_dev_destroy(&b);
    dataf_dev_destroy(&r);
    dataf_dev_destroy(&nir);
    dataf_dev_destroy(&g);
    return handled;
}

/* Composite día/noche entero en device. Reutiliza compose_truecolor_cuda() para
 * el lado diurno —que ya deja la imagen residente en d_final_image— y añade en
 * GPU las tres piezas que faltaban: pseudocolor nocturno, máscara y mezcla.
 *
 * Con esto el composite deja de bajar y volver a subir resultados intermedios:
 * la única transferencia de salida es la imagen final, y aun esa la aprovecha la
 * reproyección vía el handoff residente.
 *
 * Las luces de ciudad (-l) siguen en CPU: requieren subir el fondo WebP y no
 * están en la ruta operativa. El gate en run_rgb las excluye. */
static bool compose_daynite_cuda(RgbContext *ctx) {
    // Mismas opciones que fuerza compose_daynite() en la ruta CPU.
    ctx->opts.apply_rayleigh = true;
    ctx->opts.use_piecewise_stretch = true;

    DataFDev dla = {0}, dlo = {0};
    if (!compose_truecolor_cuda(ctx, &dla, &dlo)) return false; // lado diurno
    unsigned char *d_day = (unsigned char *)ctx->d_final_image;
    if (!d_day) return false; // sin imagen residente no hay nada que encadenar

    DataNC *c13 = &ctx->channels[13];
    if (!c13->fdata.data_in) return false;

    bool ok = false;
    DataFDev temp = dataf_dev_upload(&c13->fdata);
    unsigned char *d_night = NULL, *d_mask = NULL, *d_blend = NULL;

    if (!temp.d_data) goto done;
    if (!create_nocturnal_pseudocolor_dev(&temp, NULL, 0, &d_night)) goto done;

    // La máscara reutiliza los lat/lon que ya dejó el lado diurno en device. Solo
    // hay que subirlos si la navegación se resolvió en host (p.ej. porque el
    // camino device falló y se recalculó en CPU).
    if (!dla.d_data || !dlo.d_data) {
        dataf_dev_destroy(&dla);
        dataf_dev_destroy(&dlo);
        dla = dataf_dev_upload(&ctx->nav_lat);
        dlo = dataf_dev_upload(&ctx->nav_lon);
        if (!dla.d_data || !dlo.d_data) goto done;
    }

    {
        SolarEphemeris_dn eph = solar_ephemeris_precompute(c13->timestamp);
        float day_pct = 0.0f;
        if (!create_daynight_mask_dev(&temp, &dla, &dlo, &eph, ctx->opts.cloud_temp,
                                      &d_mask, &day_pct))
            goto done;
        float night_pct = 100.0f - day_pct;

        // Igual que apply_enhancements(): por debajo del 0.1% nocturno la escena
        // se considera diurna y se deja el compuesto visible tal cual.
        if (night_pct > 0.1f) {
            LOG_INFO("Blending day/night images (night: %.2f%%)", night_pct);
            if (!blend_images_dev(d_night, d_day, d_mask, temp.width, temp.height,
                                  &d_blend))
                goto done;

            // La mezcla sustituye a la imagen diurna, en device y en host.
            ImageData blended = image_create(temp.width, temp.height, 3);
            if (!blended.data) goto done;
            if (!cuda_download_device_image(d_blend, blended.data,
                                            (size_t)temp.size * 3)) {
                image_destroy(&blended);
                goto done;
            }
            image_destroy(&ctx->final_image);
            ctx->final_image = blended;
            cuda_free_device_image(d_day);
            ctx->d_final_image = d_blend;
            d_blend = NULL; // ya es del contexto
        } else {
            LOG_INFO("Scene is mostly daytime (%.2f%%), using only visible composite.",
                     day_pct);
        }
    }

    ctx->composite_finalized = true;
    ok = true;

done:
    // Si se difirió la navegación y aquí fallamos, el llamador cae a la ruta CPU,
    // que necesita lat/lon en host para la máscara: hay que reponerlos.
    if (!ok && ctx->nav_on_device) {
        LOG_WARN("daynite en device falló; se recalcula la navegación en CPU.");
        if (compute_navigation_nc(rgb_ref_filename(ctx), &ctx->nav_lat,
                                  &ctx->nav_lon) == 0)
            ctx->nav_on_device = false;
    }
    dataf_dev_destroy(&temp);
    dataf_dev_destroy(&dla);
    dataf_dev_destroy(&dlo);
    cuda_free_device_image(d_night);
    cuda_free_device_image(d_mask);
    cuda_free_device_image(d_blend);
    return ok;
}

#endif /* HPSV_CUDA */

static bool compose_night(RgbContext *ctx) {
    // Load city-lights background image if requested.
    ImageData fondo_img = {0};
    const ImageData *fondo_ptr = NULL;
    if (ctx->opts.use_citylights) {
        int width = ctx->channels[ctx->ref_channel_idx].fdata.width;
        const char *bg_path = NULL;

        if (width == 2500) {
            bg_path = "/usr/local/share/lanot/images/land_lights_2016_conus.webp";
        } else if (width == 5424) {
            bg_path = "/usr/local/share/lanot/images/land_lights_2016_fd.webp";
        } else if (width == 8987) {
            bg_path = "/usr/local/share/lanot/images/land_lights_2016_lalo.webp";
        } else {
            LOG_WARN("Resolution (%d) does not match available backgrounds; skipping lights.", width);
        }

        if (bg_path) {
            LOG_INFO("Loading background image: %s", bg_path);
            fondo_img = reader_load_webp(bg_path);
            if (fondo_img.data != NULL) {
                fondo_ptr = &fondo_img;
            } else {
                LOG_WARN("Could not load the city-lights background image.");
            }
        }
    } else {
        LOG_INFO("City lights disabled. Use -l or --citylights to enable them.");
    }
    ctx->final_image = create_nocturnal_pseudocolor(&ctx->channels[13].fdata, fondo_ptr);
    image_destroy(&fondo_img);
    return true;
}

static bool compose_ash(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[15].fdata, &ctx->channels[13].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[14].fdata, &ctx->channels[11].fdata, OP_SUB);
    ctx->comp_b = dataf_copy(&ctx->channels[13].fdata);
    ctx->min_r = -6.7f;
    ctx->max_r = 2.6f;
    ctx->min_g = -6.0f;
    ctx->max_g = 6.3f;
    ctx->min_b = 243.6f;
    ctx->max_b = 302.4f;
    return true;
}

static bool compose_severestorm(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[8].fdata, &ctx->channels[10].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[7].fdata, &ctx->channels[13].fdata, OP_SUB);
    ctx->comp_b = dataf_op_dataf(&ctx->channels[5].fdata, &ctx->channels[2].fdata, OP_SUB);
    ctx->min_r = -35.0f;
    ctx->max_r = 5.0f;
    ctx->min_g = -5.0f;
    ctx->max_g = 60.0f;
    ctx->min_b = -0.75f;
    ctx->max_b = 0.25f;
    return true;
}

static bool compose_airmass(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[8].fdata, &ctx->channels[10].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[12].fdata, &ctx->channels[13].fdata, OP_SUB);
    ctx->comp_b = dataf_op_scalar(&ctx->channels[8].fdata, 273.15f, OP_SUB, true);
    ctx->min_r = -26.2f;
    ctx->max_r = 0.6f;
    ctx->min_g = -43.2f;
    ctx->max_g = 6.7f;
    ctx->min_b = 29.25f;
    ctx->max_b = 64.65f;
    return true;
}

static bool compose_so2(RgbContext *ctx) {
    ctx->comp_r = dataf_op_dataf(&ctx->channels[9].fdata, &ctx->channels[10].fdata, OP_SUB);
    ctx->comp_g = dataf_op_dataf(&ctx->channels[13].fdata, &ctx->channels[11].fdata, OP_SUB);
    ctx->comp_b = dataf_copy(&ctx->channels[13].fdata);
    ctx->min_r = -4.0f;
    ctx->max_r = 2.0f;
    ctx->min_g = -4.0f;
    ctx->max_g = 5.0f;
    ctx->min_b = 233.0f;
    ctx->max_b = 300.0f;
    return true;
}

static bool compose_daynite(RgbContext *ctx) {
    ctx->opts.apply_rayleigh = true;
    ctx->opts.use_piecewise_stretch = true;
    if (!compose_truecolor(ctx)) {
        return false;
    }
    // City lights on the night side follow the user's -l/--citylights flag
    // (same as standalone night mode), not forced on.
    if (!compose_night(ctx)) {
        return false;
    }
    ctx->alpha_mask = ctx->final_image;

    return true;
}

static bool compose_custom(RgbContext *ctx) {
    LOG_INFO("Building custom RGB with expression: %s", ctx->opts.expr);
    LinearCombo combo[3];
    memset(combo, 0, sizeof(combo));
    float ranges[3][2] = {{0, 255}, {0, 255}, {0, 255}}; // [min, max]

    // 1. Parse expressions (R;G;B).
    // Use a copy of the string because strtok modifies the original.
    char *expr_copy = strdup(ctx->opts.expr);
    if (!expr_copy) {
        LOG_ERROR("Memory allocation failed while duplicating expression.");
        return false;
    }
    char *token = strtok(expr_copy, ";");
    bool parse_error = false;
    for (int i = 0; i < 3; i++) {
        if (token == NULL) {
            LOG_ERROR("Error: there must be 3 expressions separated by ';'.");
            parse_error = true;
            break;
        }
        if (parse_expr_string(token, &combo[i]) != 0) {
            LOG_ERROR("Error parsing component expression %d", i);
            parse_error = true;
        }
        token = strtok(NULL, ";");
    }
    free(expr_copy);
    if (parse_error)
        return false;

    // 2. Parse minmax ranges (min,max; min,max; min,max) if provided.
    if (ctx->opts.minmax) {
        char *minmax_copy = strdup(ctx->opts.minmax);
        if (!minmax_copy) {
            LOG_ERROR("Memory allocation failed while duplicating minmax.");
            return false;
        }
        char *m_token = strtok(minmax_copy, ";");
        for (int i = 0; i < 3 && m_token != NULL; i++) {
            if (sscanf(m_token, "%f,%f", &ranges[i][0], &ranges[i][1]) != 2) {
                LOG_WARN("Could not read ranges for component %d: %s", i, m_token);
            }
            m_token = strtok(NULL, ";");
        }
        free(minmax_copy);
    }
    LOG_DEBUG("Custom RGB ranges: %s: %f,%f  %f,%f %f,%f", ctx->opts.minmax, ranges[0][0],
              ranges[0][1], ranges[1][0], ranges[1][1], ranges[2][0], ranges[2][1]);

    // 3. Evaluate the linear combinations.
    ctx->comp_r = evaluate_linear_combo(&combo[0], ctx->channels);
    ctx->comp_g = evaluate_linear_combo(&combo[1], ctx->channels);
    ctx->comp_b = evaluate_linear_combo(&combo[2], ctx->channels);

    if (!ctx->comp_r.data_in || !ctx->comp_g.data_in || !ctx->comp_b.data_in) {
        LOG_ERROR("Failed to evaluate custom mode math formulas.");
        return false;
    }

    // 4. Assign ranges.
    ctx->min_r = ranges[0][0];
    ctx->max_r = ranges[0][1];
    ctx->min_g = ranges[1][0];
    ctx->max_g = ranges[1][1];
    ctx->min_b = ranges[2][0];
    ctx->max_b = ranges[2][1];

    return true;
}

static const RgbStrategy STRATEGIES[] = {
    {"truecolor",
     {"C01", "C02", "C03", NULL},
     compose_truecolor,
     "True Color",
     false},
    {"night", {"C13", NULL}, compose_night, "Nocturnal IR with temperature", false},
    {"ash", {"C11", "C13", "C14", "C15", NULL}, compose_ash, "Volcanic Ash", false},
    {"airmass", {"C08", "C10", "C12", "C13", NULL}, compose_airmass, "Air Mass", false},
    {"severestorm", {"C02", "C05", "C07", "C08", "C10", "C13", NULL}, compose_severestorm,
     "Severe Convection", false},
    {"so2", {"C09", "C10", "C11", "C13", NULL}, compose_so2, "SO2 Detection", false},
    {"daynite", {"C01", "C02", "C03", "C13", NULL}, compose_daynite, "Day/Night Composite", true},
    {"custom", {NULL}, compose_custom, "Custom mode", false},
    {NULL, {NULL}, NULL, NULL, false} // Sentinel.
};

static const RgbStrategy *get_strategy_for_mode(const char *mode) {
    if (strcmp("default", mode) == 0)
        return &STRATEGIES[5];
    for (int i = 0; STRATEGIES[i].mode_name != NULL; i++) {
        if (strcmp(STRATEGIES[i].mode_name, mode) == 0) {
            return &STRATEGIES[i];
        }
    }
    return NULL;
}

// --- PHASE 3: MAIN PIPELINE (THE RUNNER) ---

static bool load_channels(RgbContext *ctx, const char **req_channels) {
    // 1. Create the ChannelSet.
    int count = 0;
    while (req_channels[count] != NULL)
        count++;
    ctx->channel_set = channelset_create((const char **)req_channels, count);
    if (!ctx->channel_set) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falla de memoria al crear ChannelSet.");
        return false;
    }

    // 2. Extract the ID signature from input_file.
    char *input_dup_id = strdup(ctx->opts.input_file);
    if (!input_dup_id) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Falla de memoria al duplicar nombre de archivo.");
        return false;
    }
    const char *basename_input = basename(input_dup_id);
    if (find_id_from_name(basename_input, ctx->id_signature, sizeof(ctx->id_signature)) != 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No se pudo extraer ID del nombre: %s",
                 basename_input);
        free(input_dup_id);
        return false;
    }
    strcpy(ctx->channel_set->id_signature, ctx->id_signature);
    find_scan_mode_from_name(basename_input, ctx->channel_set->scan_mode,
                             sizeof(ctx->channel_set->scan_mode));
    free(input_dup_id);

    // 3. Locate channel files.
    char *input_dup_dir = strdup(ctx->opts.input_file);
    if (!input_dup_dir) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Falla de memoria al duplicar nombre de archivo.");
        return false;
    }
    const char *dirnm = dirname(input_dup_dir);
    if (find_channel_filenames(dirnm, ctx->channel_set, ctx->opts.is_l2_product) != 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "No se pudo acceder al directorio o encontrar los canales en %s", dirnm);
        free(input_dup_dir);
        return false;
    }
    free(input_dup_dir);

    // 4. Load channels and validate.
    for (int i = 0; i < ctx->channel_set->count; i++) {
        if (!ctx->channel_set->channels[i].filename) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falta archivo para canal %s",
                     ctx->channel_set->channels[i].name);
            return false;
        }
        int cn = atoi(ctx->channel_set->channels[i].name + 1); // "C01" -> 1
        if (cn > 0 && cn <= 16) {
            LOG_DEBUG("Loading channel C%02d from %s", cn, ctx->channel_set->channels[i].filename);
            if (load_nc_sf(ctx->channel_set->channels[i].filename, &ctx->channels[cn]) != 0) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Falla al cargar NetCDF: %s",
                         ctx->channel_set->channels[i].filename);
                return false;
            }

            if (ctx->opts.use_full_res) {
                // Select highest resolution (smallest km value) for --full-res.
                if (ctx->ref_channel_idx == 0 ||
                    ctx->channels[cn].native_resolution_km <
                        ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
                    ctx->ref_channel_idx = cn;
                }
            } else {
                // Default: select lowest resolution (largest km value).
                if (ctx->ref_channel_idx == 0 ||
                    ctx->channels[cn].native_resolution_km >
                        ctx->channels[ctx->ref_channel_idx].native_resolution_km) {
                    ctx->ref_channel_idx = cn;
                }
            }
        }
    }

    LOG_DEBUG("Channels loaded:");
    for (int i = 0; i < ctx->channel_set->count; i++) {
        int cn = atoi(ctx->channel_set->channels[i].name + 1);
        if (ctx->channels[cn].fdata.data_in) {
            LOG_DEBUG("  C%02d: %.1f km", cn, ctx->channels[cn].native_resolution_km);
        }
    }

    LOG_INFO("Reference channel: C%02d (%.1fkm)", ctx->ref_channel_idx,
             ctx->channels[ctx->ref_channel_idx].native_resolution_km);

    // Resample channels to match reference resolution
    float ref_res = ctx->channels[ctx->ref_channel_idx].native_resolution_km;
    for (int i = 0; i < ctx->channel_set->count; i++) {
        int cn = atoi(ctx->channel_set->channels[i].name + 1);
        if (cn == ctx->ref_channel_idx || ctx->channels[cn].fdata.data_in == NULL)
            continue;

        float res = ctx->channels[cn].native_resolution_km;
        float factor_f = res / ref_res;

        if (fabs(factor_f - 1.0f) > 0.01f) {
            int factor = (int)(factor_f + 0.5f);
            DataF resampled = {0};

            if (factor_f < 1.0f) { // this channel has finer resolution than reference -> downsample
                factor = (int)((1.0f / factor_f) + 0.5f);
                LOG_INFO("Downsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res,
                         factor);
                resampled = downsample_boxfilter(ctx->channels[cn].fdata, factor);
            } else { // this channel has coarser resolution than reference -> upsample
                LOG_INFO("Upsampling C%02d (%.1fkm -> %.1fkm, factor %d)", cn, res, ref_res,
                         factor);
                resampled = upsample_bilinear(ctx->channels[cn].fdata, factor);
            }

            if (resampled.data_in) {
                dataf_destroy(&ctx->channels[cn].fdata);
                ctx->channels[cn].fdata = resampled;
            } else {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                         "Falla al remuestrear el canal C%02d", cn);
                for (int j = 0; j < i; j++) { // Clean up channels already resampled in this pass.
                    dataf_destroy(
                        &ctx->channels[atoi(ctx->channel_set->channels[j].name + 1)].fdata);
                }
                return false;
            }
        }
    }

    return true;
}

static bool process_geospatial(RgbContext *ctx, const RgbStrategy *strategy) {
    // Compute navigation using the reference channel file (already at the target resolution)
    // to avoid computing at full resolution and then resampling.
    const char *ref_filename = rgb_ref_filename(ctx);
    // Si la compone la GPU, la malla lat/lon se calcula allá (compute_navigation_dev)
    // y no tiene caso pagarla también aquí: son ~0.15 s de CPU más dos subidas de
    // ~450 MB. nav_lat/nav_lon quedan sin data_in; el composer les llena fmin/fmax,
    // que es lo único que la ruta host sigue usando (extensión del reproyectado).
    // Si la ruta GPU falla, el composer llama a compute_navigation_nc() como
    // respaldo antes de caer a CPU, así que nadie se queda sin navegación.
    // La condición incluye apply_rayleigh porque el composer solo llama a
    // compute_navigation_dev() dentro del bloque de Rayleigh: sin él la
    // navegación no se calcularía en ningún lado y fmin/fmax quedarían en cero,
    // colapsando la extensión del reproyectado. Diferir aquí algo que allá no se
    // produce es justo el error que esto evita.
    if ((truecolor_cuda_eligible(&ctx->opts) && ctx->opts.apply_rayleigh) ||
        daynite_cuda_eligible(&ctx->opts)) {
        ctx->nav_on_device = true;
        ctx->has_navigation = true;
        LOG_DEBUG("Navegación diferida a la GPU (no se calcula lat/lon en CPU).");
    } else if (compute_navigation_nc(ref_filename, &ctx->nav_lat, &ctx->nav_lon) == 0) {
        ctx->has_navigation = true;
    } else {
        LOG_WARN("Could not load navigation data.");
        ctx->has_navigation = false;
    }

    // Check if strategy requires navigation.
    if (strategy->needs_navigation && !ctx->has_navigation) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "El modo '%s' requiere datos de navegación, pero no se pudieron "
                 "cargar.",
                 strategy->mode_name);
        return false;
    }

    // Resample navigation if it differs from the reference channel (rare in practice).
    // No aplica a la navegación en device: allá se calcula ya a la resolución del
    // canal de referencia, sin remuestreo intermedio.
    if (ctx->has_navigation && !ctx->nav_on_device && ctx->ref_channel_idx > 0) {
        size_t nav_width = ctx->nav_lat.width;
        size_t ref_width = ctx->channels[ctx->ref_channel_idx].fdata.width;

        if (nav_width != ref_width) {
            if (nav_width > ref_width) {
                int factor = nav_width / ref_width;
                LOG_DEBUG("Resampling navigation (downsample factor %d)", factor);
                DataF nav_lat_resampled = downsample_boxfilter(ctx->nav_lat, factor);
                DataF nav_lon_resampled = downsample_boxfilter(ctx->nav_lon, factor);

                if (nav_lat_resampled.data_in && nav_lon_resampled.data_in) {
                    dataf_destroy(&ctx->nav_lat);
                    dataf_destroy(&ctx->nav_lon);
                    ctx->nav_lat = nav_lat_resampled;
                    ctx->nav_lon = nav_lon_resampled;
                } else {
                    LOG_ERROR("Failed to resample navigation.");
                    return false;
                }
            } else {
                int factor = ref_width / nav_width;
                LOG_DEBUG("Resampling navigation (upsample factor %d)", factor);
                DataF nav_lat_resampled = upsample_bilinear(ctx->nav_lat, factor);
                DataF nav_lon_resampled = upsample_bilinear(ctx->nav_lon, factor);

                if (nav_lat_resampled.data_in && nav_lon_resampled.data_in) {
                    dataf_destroy(&ctx->nav_lat);
                    dataf_destroy(&ctx->nav_lon);
                    ctx->nav_lat = nav_lat_resampled;
                    ctx->nav_lon = nav_lon_resampled;
                } else {
                    LOG_ERROR("Failed to resample navigation.");
                    return false;
                }
            }
        }
    }

    return true;
}

static bool apply_enhancements(RgbContext *ctx) {
    // daynite is a special composite mode: blend day (visible) and night (IR) images.
    // La ruta CUDA ya entregó la mezcla hecha, así que aquí no hay nada que hacer.
    if (strcmp(ctx->opts.mode, "daynite") == 0 && !ctx->composite_finalized) {
        // Navigation is already resampled to reference resolution in process_geospatial.
        DataF *nav_lat_ptr = &ctx->nav_lat;
        DataF *nav_lon_ptr = &ctx->nav_lon;

        float day_pct = 0.0f;
        ImageData mask = create_daynight_mask(ctx->channels[13], *nav_lat_ptr, *nav_lon_ptr,
                                              &day_pct, ctx->opts.cloud_temp);
        float night_pct = 100.0f - day_pct;

        // Blend if nighttime fraction exceeds 0.1%; for fully nocturnal scenes night_pct=100.
        if (night_pct > 0.1f && mask.data) {
            LOG_INFO("Blending day/night images (night: %.2f%%)", night_pct);
            ctx->final_image = blend_images(ctx->alpha_mask, ctx->final_image, mask);
            ctx->final_image_touched = true;
        } else {
            LOG_INFO("Scene is mostly daytime (%.2f%%), using only visible composite.", day_pct);
        }
        image_destroy(&ctx->alpha_mask);
        image_destroy(&mask);
    }
    // 1. Gamma was already applied to the DataF channels earlier (see run_rgb).

    // 2. Histogram/CLAHE (skipped for daynite).
    if (strcmp(ctx->opts.mode, "daynite") != 0) {
        if (ctx->opts.apply_histogram) {
            image_apply_histogram(ctx->final_image);
            ctx->final_image_touched = true;
        }
        if (ctx->opts.apply_clahe) {
            LOG_INFO("Applying CLAHE (tiles=%dx%d, clip=%.1f)", ctx->opts.clahe_tiles_x,
                     ctx->opts.clahe_tiles_y, ctx->opts.clahe_clip_limit);
            image_apply_clahe(ctx->final_image, ctx->opts.clahe_tiles_x, ctx->opts.clahe_tiles_y,
                              ctx->opts.clahe_clip_limit);
            ctx->final_image_touched = true;
        }
    }

    if (ctx->opts.use_alpha) {
        ctx->alpha_mask =
            image_create_alpha_mask_from_dataf(&ctx->channels[ctx->ref_channel_idx].fdata);
    }

    if (ctx->opts.use_alpha && ctx->alpha_mask.data) {
        ImageData with_alpha = image_add_alpha_channel(&ctx->final_image, &ctx->alpha_mask);
        if (with_alpha.data) {
            image_destroy(&ctx->final_image);
            ctx->final_image = with_alpha;
            ctx->final_image_touched = true;
        }
        image_destroy(&ctx->alpha_mask);
        memset(&ctx->alpha_mask, 0, sizeof(ImageData));
    }

    return true;
}

static bool apply_scaling(RgbContext *ctx) {
    if (ctx->opts.scale != 1) {
        ImageData scaled_img = {0};
        if (ctx->opts.scale < 0) {
            LOG_INFO("Reducing image by factor %d", -ctx->opts.scale);
            scaled_img = image_downsample_boxfilter(&ctx->final_image, -ctx->opts.scale);
        } else { // scale > 1
            LOG_INFO("Enlarging image by factor %d", ctx->opts.scale);
            scaled_img = image_upsample_bilinear(&ctx->final_image, ctx->opts.scale);
        }

        if (scaled_img.data) {
            image_destroy(&ctx->final_image);
            ctx->final_image = scaled_img;
            ctx->final_image_touched = true;
        } else {
            LOG_ERROR("Failed to scale image.");
            return false;
        }
    }
    return true;
}

static bool write_output(RgbContext *ctx, const char *product_label) {
    bool is_geotiff = ctx->opts.force_geotiff ||
                      (ctx->opts.output_filename && (strstr(ctx->opts.output_filename, ".tif") ||
                                                     strstr(ctx->opts.output_filename, ".tiff")));

    if (is_geotiff) {
        DataNC meta_out =
            ctx->channels[ctx->ref_channel_idx]; // Preserves sat_id, sector_id, band_id, timestamp, etc.
        if (ctx->opts.do_reprojection) {
            meta_out.proj_code = PROJ_LATLON;
            meta_out.proj_info.valid = false; // Not applicable for lat/lon.
            meta_out.geotransform[0] = ctx->final_lon_min;
            meta_out.geotransform[1] =
                (ctx->final_lon_max - ctx->final_lon_min) / (double)ctx->final_image.width;
            meta_out.geotransform[2] = 0.0;
            meta_out.geotransform[3] = ctx->final_lat_max;
            meta_out.geotransform[4] = 0.0;
            meta_out.geotransform[5] =
                (ctx->final_lat_min - ctx->final_lat_max) / (double)ctx->final_image.height;
        } else {
            // Native (geostationary) metadata.
            meta_out = ctx->channels[ctx->ref_channel_idx];

            // 1. Apply the crop offset to the origin (in original radians).
            meta_out.geotransform[0] += ctx->crop_x_offset * meta_out.geotransform[1];
            meta_out.geotransform[3] += ctx->crop_y_offset * meta_out.geotransform[5];

            // Adjust pixel scale in geotransform if the image was scaled.
            if (ctx->opts.scale != 1) {
                double scale_factor = (ctx->opts.scale < 0) ? -ctx->opts.scale : ctx->opts.scale;
                if (ctx->opts.scale > 1) {
                    meta_out.geotransform[1] /= scale_factor;
                    meta_out.geotransform[5] /= scale_factor;
                } else {
                    meta_out.geotransform[1] *= scale_factor;
                    meta_out.geotransform[5] *= scale_factor;
                }
            }
        }
        // Pass 0,0 as the offset: it's already folded into meta_out.geotransform.
        write_geotiff_rgb(ctx->opts.output_filename, &ctx->final_image, &meta_out, 0, 0,
                          product_label, ctx->opts.build_cog);
    } else {
        writer_save_png(ctx->opts.output_filename, &ctx->final_image);
    }
    return true;
}

// =============================================================================
// UNIFIED INTERFACE (dependency injection via ProcessConfig)
// =============================================================================

/// Adapts ProcessConfig to RgbContext, bridging the stable public API and the internal implementation.
static void config_to_rgb_context(const ProcessConfig *cfg, RgbContext *ctx) {
    rgb_context_init(ctx);

    ctx->opts.input_file = cfg->input_file;
    // Normalize 'default' to 'daynite' for later string comparisons.
    if (cfg->strategy && strcmp(cfg->strategy, "default") == 0) {
        ctx->opts.mode = "daynite";
    } else {
        ctx->opts.mode = cfg->strategy ? cfg->strategy : "daynite";
    }
    ctx->opts.gamma[0] = cfg->gamma[0];
    ctx->opts.gamma[1] = cfg->gamma[1];
    ctx->opts.gamma[2] = cfg->gamma[2];
    ctx->opts.scale = cfg->scale;

    ctx->opts.do_reprojection = cfg->do_reprojection;
    ctx->opts.save_both = cfg->save_both;
    ctx->opts.apply_histogram = cfg->apply_histogram;
    ctx->opts.force_geotiff = cfg->force_geotiff;
    ctx->opts.build_cog = cfg->build_cog;
    ctx->opts.apply_rayleigh = cfg->apply_rayleigh;
    ctx->opts.rayleigh_analytic = cfg->rayleigh_analytic;
    ctx->opts.use_piecewise_stretch = cfg->use_piecewise_stretch;
    ctx->opts.use_sharpen = cfg->use_sharpen;
    ctx->opts.use_cuda = cfg->use_cuda;
    ctx->opts.use_citylights = cfg->use_citylights;
    ctx->opts.use_alpha = cfg->use_alpha;
    ctx->opts.use_full_res = cfg->use_full_res;
    ctx->opts.cloud_temp = cfg->cloud_temp;

    // CLAHE
    ctx->opts.apply_clahe = cfg->apply_clahe;
    if (cfg->apply_clahe) {
        ctx->opts.clahe_tiles_x = cfg->clahe_tiles_x;
        ctx->opts.clahe_tiles_y = cfg->clahe_tiles_y;
        ctx->opts.clahe_clip_limit = cfg->clahe_clip_limit;
    }

    // Clip
    ctx->opts.has_clip = cfg->has_clip;
    if (cfg->has_clip) {
        for (int i = 0; i < 4; i++) {
            ctx->opts.clip_coords[i] = cfg->clip_coords[i];
        }
    }

    // Cast to silence -Wdiscarded-qualifiers; the clean fix is to change
    // RgbOptions.expr/minmax to 'const char*'.
    ctx->opts.expr = (char *)cfg->custom_expr;
    ctx->opts.minmax = (char *)cfg->custom_minmax;

    // Output filename
    if (cfg->output_path_override) {
        ctx->opts.output_filename = (char *)cfg->output_path_override;
        ctx->opts.output_generated = false;
    }

    // Detect L2 product.
    const char *basename_input = strrchr(cfg->input_file, '/');
    basename_input = basename_input ? basename_input + 1 : cfg->input_file;
    ctx->opts.is_l2_product = (strstr(basename_input, "CMIP") != NULL);
}

int run_rgb(const ProcessConfig *cfg, MetadataContext *meta) {
    if (!cfg || !meta) {
        LOG_ERROR("run_rgb: NULL parameters");
        return 1;
    }

    LOG_INFO("Processing RGB: %s", cfg->input_file);

    RgbContext ctx;
    config_to_rgb_context(cfg, &ctx);
    int status = 1;
    char **custom_channels = NULL;

    // Use the short product name (-N flag) if provided; otherwise fall back to mode string.
    const char *mode_label = (cfg->product_short && cfg->product_short[0])
                                 ? cfg->product_short
                                 : (ctx.opts.mode ? ctx.opts.mode : "unknown");
    metadata_add(meta, "mode", mode_label);

    if (fabsf(ctx.opts.gamma[0] - 1.0f) > 1e-6f || fabsf(ctx.opts.gamma[1] - 1.0f) > 1e-6f ||
        fabsf(ctx.opts.gamma[2] - 1.0f) > 1e-6f) {
        if (fabsf(ctx.opts.gamma[0] - ctx.opts.gamma[1]) < 1e-6f &&
            fabsf(ctx.opts.gamma[0] - ctx.opts.gamma[2]) < 1e-6f) {
            metadata_add(meta, "gamma", ctx.opts.gamma[0]);
        } else {
            char gamma_str[48];
            snprintf(gamma_str, sizeof(gamma_str), "%.4g;%.4g;%.4g", ctx.opts.gamma[0],
                     ctx.opts.gamma[1], ctx.opts.gamma[2]);
            metadata_add(meta, "gamma", (const char *)gamma_str);
        }
    }
    if (ctx.opts.apply_clahe)
        metadata_add_bool(meta, "clahe", true);
    if (ctx.opts.apply_rayleigh)
        metadata_add_bool(meta, "rayleigh", true);
    if (ctx.opts.apply_histogram)
        metadata_add_bool(meta, "histogram", true);
    if (ctx.opts.use_piecewise_stretch)
        metadata_add_bool(meta, "stretch", true);
    if (ctx.opts.do_reprojection && !ctx.opts.save_both)
        metadata_add_bool(meta, "geographics", true);
    if (ctx.opts.has_clip)
        metadata_set_clip(meta, true);

    if (ctx.opts.apply_clahe) {
        metadata_add(meta, "clahe_limit", ctx.opts.clahe_clip_limit);
    }

    const RgbStrategy *strategy = get_strategy_for_mode(ctx.opts.mode);
    if (!strategy) {
        LOG_ERROR("Mode '%s' not recognized.", ctx.opts.mode);

        char available[512] = {0};
        for (int i = 0; STRATEGIES[i].mode_name != NULL; i++) {
            if (i > 0)
                strcat(available, ", ");
            strcat(available, STRATEGIES[i].mode_name);
        }
        LOG_INFO("Available modes: %s", available);
        goto cleanup;
    }
    LOG_INFO("Selected mode: %s - %s", strategy->mode_name, strategy->description);

    const char *product = cfg->product_long ? cfg->product_long : strategy->description;
    metadata_set_product(meta, product);

    // Rayleigh correction is not meaningful for night mode (thermal IR only).
    if (strcmp(ctx.opts.mode, "night") == 0) {
        if (ctx.opts.apply_rayleigh || ctx.opts.rayleigh_analytic) {
            LOG_WARN(
                "Rayleigh correction is ignored in 'night' mode (only affects visible channels).");
        }
        if (ctx.opts.use_piecewise_stretch) {
            LOG_WARN("Contrast stretch is ignored in 'night' mode.");
        }
    }

    const char **req_channels = NULL;
    if (strcmp(ctx.opts.mode, "custom") == 0) {
        if (!ctx.opts.expr) {
            LOG_ERROR("'custom' mode requires specifying --expr");
            goto cleanup;
        }
        int count = get_unique_channels_rgb(ctx.opts.expr, &custom_channels);
        if (count == 0 || !custom_channels) {
            LOG_ERROR("No valid bands detected in: %s", ctx.opts.expr);
            goto cleanup;
        }
        LOG_INFO("Custom mode: %d bands required", count);
        req_channels = (const char **)custom_channels;
    } else {
        req_channels = (const char **)strategy->req_channels;
    }

    if (!load_channels(&ctx, req_channels)) {
        LOG_ERROR("%s", ctx.error_msg);
        goto cleanup;
    }

    // Extract satellite/band/timestamp/geometry metadata from reference channel.
    metadata_from_nc(meta, &ctx.channels[ctx.ref_channel_idx]);

    if (!process_geospatial(&ctx, strategy)) {
        LOG_ERROR("%s", ctx.error_msg);
        goto cleanup;
    }

    // RGB composite. The true-color path can run device-resident under --cuda;
    // every other mode/option (analytic Rayleigh, non-truecolor modes, custom)
    // still runs on the CPU.
    bool cuda_handled = false;
#ifdef HPSV_CUDA
    if (cfg->use_cuda) {
        // Accelerated: true-color, optionally with Rayleigh LUT, ratio
        // sharpening and piecewise stretch. Still CPU-only: analytic Rayleigh
        // and the other modes.
        bool truecolor_cuda = truecolor_cuda_eligible(&ctx.opts);
        // daynite: mismo gate salvo el modo, más las luces de ciudad, que siguen
        // en CPU (habría que subir el fondo WebP y no están en la ruta operativa).
        bool daynite_cuda = strcmp(ctx.opts.mode, "daynite") == 0 &&
                            !ctx.opts.rayleigh_analytic && !ctx.opts.use_sharpen &&
                            !ctx.opts.use_citylights && ctx.channels[13].fdata.data_in;
        if (daynite_cuda) {
            LOG_INFO("Generating 'daynite' composite (CUDA, device-resident)...");
            cuda_handled = compose_daynite_cuda(&ctx);
        }
        if (!cuda_handled && truecolor_cuda) {
            LOG_INFO("Generating 'truecolor' composite (CUDA, device-resident)...");
            cuda_handled = compose_truecolor_cuda(&ctx, NULL, NULL);
        }
        if (!cuda_handled)
            LOG_WARN("--cuda: this RGB configuration isn't GPU-accelerated yet; using CPU path.");
    }
#endif

    if (!cuda_handled) {
        LOG_INFO("Generating '%s' composite...", strategy->mode_name);
        if (!strategy->composer_func(&ctx)) {
            LOG_ERROR("Failed to generate RGB composite.");
            goto cleanup;
        }

        // Preprocess the DataF channels (apply per-channel gamma).
        if (ctx.comp_r.data_in && ctx.comp_g.data_in && ctx.comp_b.data_in) {
            bool any_gamma = fabsf(ctx.opts.gamma[0] - 1.0f) > 1e-6f ||
                             fabsf(ctx.opts.gamma[1] - 1.0f) > 1e-6f ||
                             fabsf(ctx.opts.gamma[2] - 1.0f) > 1e-6f;
            if (any_gamma) {
                LOG_INFO("Applying gamma R=%.2f G=%.2f B=%.2f", ctx.opts.gamma[0], ctx.opts.gamma[1],
                         ctx.opts.gamma[2]);
                // Only update the range to [0,1] for channels where gamma != 1.0; otherwise
                // dataf_apply_gamma leaves the data unchanged, so the --minmax range
                // (already in ctx.min_*/max_*) must be kept for rendering.
                if (fabsf(ctx.opts.gamma[0] - 1.0f) > 1e-6f) {
                    dataf_apply_gamma(&ctx.comp_r, ctx.opts.gamma[0], ctx.min_r, ctx.max_r);
                    ctx.min_r = 0.0f;
                    ctx.max_r = 1.0f;
                }
                if (fabsf(ctx.opts.gamma[1] - 1.0f) > 1e-6f) {
                    dataf_apply_gamma(&ctx.comp_g, ctx.opts.gamma[1], ctx.min_g, ctx.max_g);
                    ctx.min_g = 0.0f;
                    ctx.max_g = 1.0f;
                }
                if (fabsf(ctx.opts.gamma[2] - 1.0f) > 1e-6f) {
                    dataf_apply_gamma(&ctx.comp_b, ctx.opts.gamma[2], ctx.min_b, ctx.max_b);
                    ctx.min_b = 0.0f;
                    ctx.max_b = 1.0f;
                }
                ctx.opts.gamma[0] = ctx.opts.gamma[1] = ctx.opts.gamma[2] = 1.0f;
            }

            // Render to image.
            ctx.final_image =
                create_multiband_rgb(&ctx.comp_r, &ctx.comp_g, &ctx.comp_b, ctx.min_r, ctx.max_r,
                                     ctx.min_g, ctx.max_g, ctx.min_b, ctx.max_b);
        }
    }

    if (ctx.final_image.data == NULL) {
        LOG_ERROR("Failed to generate RGB image.");
        goto cleanup;
    }

    // Post-processing (blending, CLAHE, alpha) — before reprojection.
    if (!apply_enhancements(&ctx)) {
        LOG_ERROR("Failure in post-processing (enhancements).");
        goto cleanup;
    }

    // -B: scale and save the fixed-grid output before reprojecting.
    if (ctx.opts.save_both) {
        if (!apply_scaling(&ctx)) {
            LOG_ERROR("Failure in scaling (fixed-grid).");
            goto cleanup;
        }
        if (ctx.opts.output_filename == NULL) {
            const char *ext_fg = ctx.opts.force_geotiff ? ".tif" : ".png";
            ctx.opts.output_filename = metadata_build_filename(meta, ext_fg);
            ctx.opts.output_generated = true;
            if (ctx.opts.output_filename == NULL) {
                LOG_ERROR("Failed to generate fixed-grid filename.");
                goto cleanup;
            }
        }
        LOG_INFO("Saving fixed-grid: %s", ctx.opts.output_filename);
        // Temporarily disable reprojection flag so write_output uses the native projection.
        ctx.opts.do_reprojection = false;
        if (!write_output(&ctx, product)) {
            LOG_ERROR("Failed to save fixed-grid.");
            goto cleanup;
        }
        ctx.opts.do_reprojection = true;
        // Append _geo suffix to the filename for the reprojected output.
        char *geo_filename = insert_geo_suffix(ctx.opts.output_filename);
        if (ctx.opts.output_generated) {
            free(ctx.opts.output_filename);
        }
        ctx.opts.output_filename = geo_filename;
        ctx.opts.output_generated = true;
        if (ctx.opts.output_filename == NULL) {
            LOG_ERROR("Failed to generate reprojected filename.");
            goto cleanup;
        }
        LOG_INFO("Saving reprojected: %s", ctx.opts.output_filename);
    }

    // Reprojection.
    if (ctx.opts.do_reprojection) {
        if (!ctx.has_navigation) {
            LOG_ERROR("Navigation required for reprojection.");
            goto cleanup;
        }

        // Areas outside the visible disk must read as NonData (alpha=0), not real data —
        // the same convention already used for interior NonData pixels in apply_enhancements().
        unsigned char nodata_pattern[4] = {0};
        const unsigned char *nodata_pixel = ctx.opts.use_alpha ? nodata_pattern : NULL;
#ifdef HPSV_CUDA
        // La copia en device solo sirve si nadie tocó la imagen en host desde la
        // composición; si la tocaron, el espejo quedó obsoleto y hay que subirla.
        // HPSV_NO_DEVICE_HANDOFF=1 fuerza el H2D aunque el espejo sea válido: es
        // el A/B que prueba que ambos caminos dan los mismos píxeles.
        const unsigned char *d_src =
            (ctx.final_image_touched || getenv("HPSV_NO_DEVICE_HANDOFF"))
                ? NULL
                : (const unsigned char *)ctx.d_final_image;
        if (d_src) {
            LOG_INFO("Reprojection reuses the device-resident composite (no H2D).");
        }
        ImageData reprojected = cfg->use_cuda
            ? reproject_image_analytical_cuda(
                  &ctx.final_image, &ctx.channels[ctx.ref_channel_idx], ctx.nav_lat.fmin,
                  ctx.nav_lat.fmax, ctx.nav_lon.fmin, ctx.nav_lon.fmax,
                  ctx.channels[ctx.ref_channel_idx].native_resolution_km,
                  ctx.opts.has_clip ? ctx.opts.clip_coords : NULL, nodata_pixel, d_src)
            : reproject_image_analytical(
                  &ctx.final_image, &ctx.channels[ctx.ref_channel_idx], ctx.nav_lat.fmin,
                  ctx.nav_lat.fmax, ctx.nav_lon.fmin, ctx.nav_lon.fmax,
                  ctx.channels[ctx.ref_channel_idx].native_resolution_km,
                  ctx.opts.has_clip ? ctx.opts.clip_coords : NULL, nodata_pixel);
#else
        ImageData reprojected = reproject_image_analytical(
            &ctx.final_image, &ctx.channels[ctx.ref_channel_idx], ctx.nav_lat.fmin,
            ctx.nav_lat.fmax, ctx.nav_lon.fmin, ctx.nav_lon.fmax,
            ctx.channels[ctx.ref_channel_idx].native_resolution_km,
            ctx.opts.has_clip ? ctx.opts.clip_coords : NULL, nodata_pixel);
#endif

        if (reprojected.data == NULL) {
            LOG_ERROR("Failure during reprojection.");
            goto cleanup;
        }

        image_destroy(&ctx.final_image);
        ctx.final_image = reprojected;

        // Update final bounding box from reprojected extent.
        if (ctx.opts.has_clip) {
            ctx.final_lon_min = ctx.opts.clip_coords[0];
            ctx.final_lat_max = ctx.opts.clip_coords[1];
            ctx.final_lon_max = ctx.opts.clip_coords[2];
            ctx.final_lat_min = ctx.opts.clip_coords[3];
        } else {
            ctx.final_lon_min = ctx.nav_lon.fmin;
            ctx.final_lon_max = ctx.nav_lon.fmax;
            ctx.final_lat_min = ctx.nav_lat.fmin;
            ctx.final_lat_max = ctx.nav_lat.fmax;
        }
    } else {
        // No reprojection: apply clip in native fixed-grid coordinates if requested.
        if (ctx.opts.has_clip && ctx.has_navigation) {
            int ix, iy, iw, ih;
            reprojection_find_bounding_box(&ctx.nav_lat, &ctx.nav_lon, ctx.opts.clip_coords[0],
                                           ctx.opts.clip_coords[1], ctx.opts.clip_coords[2],
                                           ctx.opts.clip_coords[3], &ix, &iy, &iw, &ih);

            ImageData cropped = image_crop(&ctx.final_image, ix, iy, iw, ih);
            image_destroy(&ctx.final_image);
            ctx.final_image = cropped;

            ctx.crop_x_offset = (unsigned)ix;
            ctx.crop_y_offset = (unsigned)iy;
        } else if (ctx.has_navigation) {
            ctx.final_lon_min = ctx.nav_lon.fmin;
            ctx.final_lon_max = ctx.nav_lon.fmax;
            ctx.final_lat_min = ctx.nav_lat.fmin;
            ctx.final_lat_max = ctx.nav_lat.fmax;
        }
    }

    // Write geometry metadata for JSON sidecar.
    if (ctx.has_navigation || ctx.opts.has_clip) {
        if (ctx.opts.do_reprojection) {
            metadata_set_geometry(meta, ctx.final_lon_min, ctx.final_lat_min, ctx.final_lon_max,
                                  ctx.final_lat_max);
            metadata_set_projection(meta, "EPSG:4326");
        } else {
            // Compute bounds in metres for geostationary projection metadata.
            DataNC *ref = &ctx.channels[ctx.ref_channel_idx];
            double *gt = ref->geotransform;
            double h = (ref->proj_info.valid) ? ref->proj_info.sat_height : 35786023.0;

            if (gt[1] != 0.0) {
                double x_min = (gt[0] + ctx.crop_x_offset * gt[1]) * h;
                double y_top = (gt[3] + ctx.crop_y_offset * gt[5]) * h;
                double x_max = x_min + (ctx.final_image.width * gt[1] * h);
                double y_bot = y_top + (ctx.final_image.height * gt[5] * h);

                double y_min = (y_bot < y_top) ? y_bot : y_top;
                double y_max = (y_bot > y_top) ? y_bot : y_top;
                metadata_set_geometry(meta, (float)x_min, (float)y_min, (float)x_max, (float)y_max);
            }

            const char *sat_crs = "geostationary";
            int sid = ctx.channels[ctx.ref_channel_idx].sat_id;
            if (sid == SAT_GOES16)
                sat_crs = "goes16";
            else if (sid == SAT_GOES17)
                sat_crs = "goes17";
            else if (sid == SAT_GOES18)
                sat_crs = "goes18";
            else if (sid == SAT_GOES19)
                sat_crs = "goes19";
            metadata_set_projection(meta, sat_crs);
        }
    }

    // Final scaling — after reprojection (for save_both, already applied before reprojection).
    if (!ctx.opts.save_both && !apply_scaling(&ctx)) {
        LOG_ERROR("Failure in final scaling.");
        goto cleanup;
    }

    // Generate output filename if not specified.
    if (ctx.opts.output_filename == NULL) {
        const char *ext = ctx.opts.force_geotiff ? ".tif" : ".png";
        ctx.opts.output_filename = metadata_build_filename(meta, ext);
        ctx.opts.output_generated = true;

        if (ctx.opts.output_filename == NULL) {
            LOG_ERROR("Failed to generate output filename.");
            goto cleanup;
        }
    }

    if (!write_output(&ctx, product)) {
        LOG_ERROR("Failed to save image.");
        goto cleanup;
    }

    metadata_add(meta, "output_file", ctx.opts.output_filename);
    metadata_add(meta, "output_width", (int)ctx.final_image.width);
    metadata_add(meta, "output_height", (int)ctx.final_image.height);

    status = 0;

cleanup:
    rgb_context_destroy(&ctx);
    if (custom_channels) {
        for (int i = 0; custom_channels[i] != NULL; i++) {
            free(custom_channels[i]);
        }
        free(custom_channels);
    }
    return status;
}
