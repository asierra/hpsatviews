/* Mide en el propio dispositivo el cociente de rendimiento entre precisión
 * simple y doble, más el ancho de banda teórico de memoria.
 *
 * Por qué existe: el cociente FP64:FP32 es una propiedad arquitectónica y NO lo
 * reporta nvidia-smi ni ninguna API del runtime; solo aparece en los whitepapers
 * del fabricante. Medirlo sobre la tarjeta concreta es más defendible que citar
 * un número de catálogo: recoge el reloj real, el estado térmico y el toolkit
 * con el que se compiló.
 *
 * Compilar (ajustar la arquitectura a la GPU):
 *   nvcc -O3 -arch=sm_80 reproduction/gpu_precision_ratio.cu -o /tmp/gpuratio
 * Ejecutar:
 *   /tmp/gpuratio
 *
 * Copyright (c) 2026 Alejandro Aguilar Sierra (asierra@unam.mx)
 * Laboratorio Nacional de Observación de la Tierra, UNAM
 * Licensed under the GNU General Public License v3.0.
 */

#include <cstdio>
#include <cuda_runtime.h>

#define CHECK(x)                                                              \
  do {                                                                        \
    cudaError_t e = (x);                                                      \
    if (e != cudaSuccess) {                                                   \
      fprintf(stderr, "CUDA %s:%d: %s\n", __FILE__, __LINE__,                 \
              cudaGetErrorString(e));                                         \
      return 1;                                                               \
    }                                                                         \
  } while (0)

/* Acumuladores independientes por hilo: con uno solo, cada FMA esperaría al
 * anterior y se mediría la latencia en vez del rendimiento. Ocho bastan para
 * saturar la tubería en las arquitecturas en uso. */
#define NACC 8

template <typename T>
__global__ void fma_bench(T *out, T a, T b, int iters) {
  T acc[NACC];
#pragma unroll
  for (int i = 0; i < NACC; i++) acc[i] = (T)(threadIdx.x + i + 1);

  for (int it = 0; it < iters; ++it) {
#pragma unroll
    for (int i = 0; i < NACC; i++) acc[i] = acc[i] * a + b;
  }

  T s = (T)0;
#pragma unroll
  for (int i = 0; i < NACC; i++) s += acc[i];

  /* La condición nunca se cumple, pero el compilador no puede probarlo, así que
   * no elimina el cálculo como código muerto. */
  if (s == (T)-1.0e30) out[threadIdx.x] = s;
}

template <typename T>
static double measure(const char *label, int blocks, int threads, int iters) {
  T *d_out = nullptr;
  cudaMalloc((void **)&d_out, threads * sizeof(T));

  /* a poco menor que 1 y b pequeño mantienen los acumuladores cerca de 1.0:
   * evita desnormales, que en algunas arquitecturas se procesan más despacio y
   * contaminarían la medición. */
  const T a = (T)0.9999999, b = (T)0.0000001;

  fma_bench<T><<<blocks, threads>>>(d_out, a, b, 64);  // calentamiento
  cudaDeviceSynchronize();

  cudaEvent_t t0, t1;
  cudaEventCreate(&t0);
  cudaEventCreate(&t1);
  cudaEventRecord(t0);
  fma_bench<T><<<blocks, threads>>>(d_out, a, b, iters);
  cudaEventRecord(t1);
  cudaEventSynchronize(t1);

  float ms = 0.0f;
  cudaEventElapsedTime(&ms, t0, t1);
  cudaEventDestroy(t0);
  cudaEventDestroy(t1);
  cudaFree(d_out);

  /* Cada FMA cuenta como 2 operaciones de punto flotante. */
  double flops = (double)blocks * threads * iters * NACC * 2.0;
  double gflops = flops / (ms * 1.0e6);
  printf("  %-18s %8.1f GFLOP/s   (%6.2f ms)\n", label, gflops, ms);
  return gflops;
}

int main(void) {
  cudaDeviceProp p;
  CHECK(cudaGetDeviceProperties(&p, 0));

  /* clockRate y memoryClockRate salieron de cudaDeviceProp en CUDA 13; la API
   * de atributos existe desde hace mucho y funciona en ambas. */
  int mem_khz = 0, sm_khz = 0, bus_bits = 0;
  CHECK(cudaDeviceGetAttribute(&mem_khz, cudaDevAttrMemoryClockRate, 0));
  CHECK(cudaDeviceGetAttribute(&sm_khz, cudaDevAttrClockRate, 0));
  CHECK(cudaDeviceGetAttribute(&bus_bits, cudaDevAttrGlobalMemoryBusWidth, 0));

  /* Ancho de banda teórico: bus x reloj efectivo (DDR, de ahí el x2). */
  double bw = 2.0 * mem_khz * (bus_bits / 8.0) / 1.0e6;

  printf("%s (sm_%d%d, %d SMs, %.0f MHz, %zu MiB)\n", p.name, p.major, p.minor,
         p.multiProcessorCount, sm_khz / 1000.0,
         p.totalGlobalMem / (1024 * 1024));
  printf("  ancho de banda teórico: %.0f GB/s\n\n", bw);

  int threads = 256;
  int blocks = p.multiProcessorCount * 32;

  double f32 = measure<float>("precisión simple", blocks, threads, 4096);
  double f64 = measure<double>("precisión doble", blocks, threads, 4096);

  printf("\n  FP32/FP64 = %.1fx\n", f32 / f64);
  return 0;
}
