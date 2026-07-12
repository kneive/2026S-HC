// montecarlo.cu — Implementierung der Monte-Carlo-Komponente (π-Schätzung)
#include "montecarlo.cuh"
#include "common.cuh"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <curand_kernel.h>

// ---- Kernel: π per Monte-Carlo ---------------------------------------------
//   Jeder Thread besitzt einen eigenen Philox-RNG-Zustand (counter-based,
//   ideal für GPUs: unabhängige Ströme ohne geteilten Zustand). Er zieht
//   'samplesPerThread' Punkte im Einheitsquadrat und zählt Treffer im
//   Viertelkreis (x²+y² <= 1). Teilsummen werden per atomicAdd reduziert.
//   Speicher: nur ein globaler Zähler -> minimaler Footprint.
__global__ void kernel_mc_pi(unsigned long long* __restrict__ globalHits,
                             int n, int samplesPerThread, unsigned long long seed)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    // Eigener RNG-Strom: gleiche Seed, aber Sequenz = idx -> dekorreliert.
    curandStatePhilox4_32_10_t st;
    curand_init(seed, (unsigned long long)idx, 0ULL, &st);

    unsigned int local = 0;
    // Philox liefert 4 Zufallszahlen auf einmal -> 2 Punkte pro Aufruf.
    int pairs = samplesPerThread / 2;
    for (int i = 0; i < pairs; ++i) {
        float4 r = curand_uniform4(&st);
        if (fmaf(r.x, r.x, r.y * r.y) <= 1.0f) ++local;
        if (fmaf(r.z, r.z, r.w * r.w) <= 1.0f) ++local;
    }

    // Block-lokale Reduktion vor atomicAdd (weniger Kontention).
    atomicAdd(globalHits, (unsigned long long)local);
}

__global__ void kernel_mc_points(float* __restrict__ xs,
                                 float* __restrict__ ys,
                                 unsigned char* __restrict__ inside,
                                 int n, unsigned long long seed)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    curandStatePhilox4_32_10_t st;
    curand_init(seed, (unsigned long long)idx, 0ULL, &st);
    float x = curand_uniform(&st);
    float y = curand_uniform(&st);
    xs[idx] = x;
    ys[idx] = y;
    inside[idx] = fmaf(x, x, y * y) <= 1.0f ? 1 : 0;
}

// ---- Öffentliche API -------------------------------------------------------
void montecarlo_run(int n, int samplesPerThread, int reps) {
    // samplesPerThread auf gerade Zahl runden (Philox liefert Paare).
    samplesPerThread &= ~1;
    printf("--- Monte-Carlo π: n=%d Threads, samples/thread=%d, reps=%d ---\n",
           n, samplesPerThread, reps);

    unsigned long long* d_hits;
    CUDA_CHECK(cudaMalloc(&d_hits, sizeof(unsigned long long)));

    int block = 256, grid = (n + block - 1) / block;

    // Warmup
    CUDA_CHECK(cudaMemset(d_hits, 0, sizeof(unsigned long long)));
    kernel_mc_pi<<<grid, block>>>(d_hits, n, samplesPerThread, 1234ULL);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r) {
        CUDA_CHECK(cudaMemset(d_hits, 0, sizeof(unsigned long long)));
        kernel_mc_pi<<<grid, block>>>(d_hits, n, samplesPerThread,
                                      1234ULL + r);
    }
    float ms = t.stop() / reps;

    unsigned long long hits;
    CUDA_CHECK(cudaMemcpy(&hits, d_hits, sizeof(hits), cudaMemcpyDeviceToHost));

    double totalSamples = (double)n * samplesPerThread;
    double pi = 4.0 * (double)hits / totalSamples;
    double err = fabs(pi - M_PI);
    // Kosten grob: ~2 uniform-Generierungen + 1 FMA + Vergleich je Sample.
    double gsamples = totalSamples / (ms * 1e6);

    printf("  Zeit=%.3f ms  %.2f Gsamples/s  π≈%.6f  |Fehler|=%.2e  "
           "(N=%.3g)\n\n", ms, gsamples, pi, err, totalSamples);

    cudaFree(d_hits);
}

void montecarlo_run_sweep(const char* path) {
    const int n = 1 << 20, reps = 10;
    const int samples[] = {256, 512, 1024, 2048, 4096, 8192};
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# n=%d reps=%d\n", n, reps);
    fprintf(f, "threads,samples_per_thread,total_samples,ms,gsamples_s,pi,error\n");
    printf("Monte-Carlo-Sweep -> %s\n", path);

    unsigned long long* d_hits;
    CUDA_CHECK(cudaMalloc(&d_hits, sizeof(unsigned long long)));
    int block = 256, grid = (n + block - 1) / block;

    for (int si = 0; si < (int)(sizeof(samples) / sizeof(samples[0])); ++si) {
        int samplesPerThread = samples[si] & ~1;
        CUDA_CHECK(cudaMemset(d_hits, 0, sizeof(unsigned long long)));
        kernel_mc_pi<<<grid, block>>>(d_hits, n, samplesPerThread, 1234ULL);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());

        GpuTimer t; t.start();
        for (int r = 0; r < reps; ++r) {
            CUDA_CHECK(cudaMemset(d_hits, 0, sizeof(unsigned long long)));
            kernel_mc_pi<<<grid, block>>>(d_hits, n, samplesPerThread,
                                          1234ULL + r);
        }
        float ms = t.stop() / reps;

        unsigned long long hits;
        CUDA_CHECK(cudaMemcpy(&hits, d_hits, sizeof(hits), cudaMemcpyDeviceToHost));
        double totalSamples = (double)n * samplesPerThread;
        double pi = 4.0 * (double)hits / totalSamples;
        double err = fabs(pi - M_PI);
        double gsamples = totalSamples / (ms * 1e6);
        fprintf(f, "%d,%d,%.0f,%.5f,%.3f,%.8f,%.8e\n",
                n, samplesPerThread, totalSamples, ms, gsamples, pi, err);
        printf("  samples/thread=%-5d %8.3f ms  %7.2f Gsamples/s  pi=%.7f  err=%.2e\n",
               samplesPerThread, ms, gsamples, pi, err);
    }

    fclose(f);
    cudaFree(d_hits);
}

void montecarlo_write_points(const char* path, int n) {
    float *d_xs, *d_ys;
    unsigned char* d_inside;
    CUDA_CHECK(cudaMalloc(&d_xs, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_ys, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_inside, n * sizeof(unsigned char)));

    int block = 256, grid = (n + block - 1) / block;
    kernel_mc_points<<<grid, block>>>(d_xs, d_ys, d_inside, n, 2026ULL);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    float* xs = (float*)malloc(n * sizeof(float));
    float* ys = (float*)malloc(n * sizeof(float));
    unsigned char* inside = (unsigned char*)malloc(n * sizeof(unsigned char));
    CUDA_CHECK(cudaMemcpy(xs, d_xs, n * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(ys, d_ys, n * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(inside, d_inside, n * sizeof(unsigned char), cudaMemcpyDeviceToHost));

    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "x,y,inside\n");
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        hits += inside[i] ? 1 : 0;
        fprintf(f, "%.8f,%.8f,%u\n", xs[i], ys[i], (unsigned)inside[i]);
    }
    fclose(f);
    printf("Monte-Carlo-Punkte geschrieben: %s (n=%d, pi=%.6f)\n",
           path, n, 4.0 * hits / (double)n);

    free(xs);
    free(ys);
    free(inside);
    cudaFree(d_xs);
    cudaFree(d_ys);
    cudaFree(d_inside);
}
