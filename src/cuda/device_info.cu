/* Selección y reporte del dispositivo CUDA: qué GPU agarró la corrida y si el
 * binario tiene código máquina para ella.
 * Copyright (c) 2025-2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 *
 * This file is part of HPSATVIEWS.
 * Licensed under the GNU General Public License v3.0 (see LICENSE file).
 *
 * Motivación: el build se compila para UNA arquitectura (CUDA_ARCH, por defecto
 * sm_120) y los servidores de despliegue no la comparten (tsom04: Tesla T4 =
 * sm_75). Un binario con la arch equivocada no falla al arrancar: falla en el
 * primer lanzamiento de kernel con cudaErrorNoKernelImageForDevice, que sin
 * contexto se ve como un error genérico a media pipeline. Este módulo lo
 * detecta antes de leer un solo NetCDF, con un mensaje que dice qué recompilar.
 */

#include "cuda_common.cuh"

extern "C" {
#include "cuda_device.h"
#include "logger.h"
}

/* Kernel vacío: solo sirve para forzar la carga del module y provocar el error
 * de arch-mismatch aquí, donde podemos explicarlo. */
__global__ static void probe_kernel(void) {}

extern "C" bool cuda_report_device(void) {
  /* Idempotente: --cuda se resuelve una vez, pero no cuesta nada blindarlo
   * para que el reporte no se duplique si algún día se llama desde otro sitio. */
  static bool checked = false;
  static bool ok = false;
  if (checked) return ok;
  checked = true;

  int count = 0;
  cudaError_t e = cudaGetDeviceCount(&count);
  if (e != cudaSuccess) {
    LOG_ERROR("--cuda: no usable CUDA device: %s. "
              "Check the driver ('nvidia-smi') or drop --cuda to use the CPU path.",
              cudaGetErrorString(e));
    return false;
  }
  if (count == 0) {
    LOG_ERROR("--cuda: no CUDA device found. "
              "Drop --cuda to use the CPU path.");
    return false;
  }

  int dev = 0;
  cudaGetDevice(&dev);

  cudaDeviceProp prop;
  e = cudaGetDeviceProperties(&prop, dev);
  if (e != cudaSuccess) {
    LOG_ERROR("--cuda: cudaGetDeviceProperties failed: %s", cudaGetErrorString(e));
    return false;
  }

  /* Memoria libre vs total: en un servidor compartido lo que importa no es el
   * tamaño de la tarjeta sino cuánto queda. cudaMemGetInfo crea el contexto. */
  size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess) {
    free_b = 0;
    total_b = (size_t)prop.totalGlobalMem;
  }

  LOG_INFO("CUDA device %d: %s (sm_%d%d, %d SMs, %.0f/%.0f MiB free)",
           dev, prop.name, prop.major, prop.minor, prop.multiProcessorCount,
           free_b / (1024.0 * 1024.0), total_b / (1024.0 * 1024.0));

  if (count > 1) {
    LOG_INFO("CUDA: %d devices present; using device %d "
             "(set CUDA_VISIBLE_DEVICES to pick another).", count, dev);
  }

  /* Arch-mismatch: probamos con un kernel vacío en vez de comparar strings
   * contra CUDA_ARCH, porque el síntoma real es el fallo de carga del module. */
  probe_kernel<<<1, 1>>>();
  e = cudaGetLastError();
  if (e == cudaSuccess) e = cudaDeviceSynchronize();
  if (e != cudaSuccess) {
    LOG_ERROR("--cuda: this binary has no device code for %s (sm_%d%d): %s. "
              "Rebuild with 'make clean && make CUDA=1 CUDA_ARCH=sm_%d%d'.",
              prop.name, prop.major, prop.minor, cudaGetErrorString(e),
              prop.major, prop.minor);
    return false;
  }

  ok = true;
  return ok;
}
