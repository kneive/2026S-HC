// stream.cu — Implementierung der STREAM-Triad-Komponente
#include "stream.cuh"
#include "common.cuh"
#include <cstdio>

// ---- Kernel: a = b + alpha*c -----------------------------------------------
//   Pro Element: 2 Loads + 1 Store = 12 B (FP32) bei nur 2 FLOP (1 FMA).
//   -> AI = 2/12 ≈ 0,17 FLOP/Byte, weit links vom Roofline-Knick:
//   die Laufzeit wird komplett von der Speicherbandbreite bestimmt.
__global__ void kernel_triad(float* __restrict__ a,
                             const float* __restrict__ b,
                             const float* __restrict__ c,
                             float alpha, int n)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    a[idx] = fmaf(alpha, c[idx], b[idx]);   // b + alpha*c
}

// ---- Öffentliche API -------------------------------------------------------
void stream_run(int n, int reps) {
    printf("--- STREAM Triad: n=%d Elemente/Array, reps=%d ---\n", n, reps);
    size_t bytes = (size_t)n * sizeof(float);
    float *da, *db, *dc;
    CUDA_CHECK(cudaMalloc(&da, bytes));
    CUDA_CHECK(cudaMalloc(&db, bytes));
    CUDA_CHECK(cudaMalloc(&dc, bytes));
    CUDA_CHECK(cudaMemset(db, 1, bytes));   // irgendein Inhalt
    CUDA_CHECK(cudaMemset(dc, 2, bytes));

    int block = 256, grid = (n + block - 1) / block;
    kernel_triad<<<grid, block>>>(da, db, dc, 3.0f, n);   // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r)
        kernel_triad<<<grid, block>>>(da, db, dc, 3.0f, n);
    float ms = t.stop() / reps;

    double bytes_moved = 3.0 * bytes;   // 2 read + 1 write
    double flop = 2.0 * n;              // 1 FMA
    Peaks pk = get_peaks();
    double gbs = bytes_moved / (ms * 1e6);
    printf("  Zeit=%.3f ms  %.1f GB/s  (%.0f%% vom Peak-BW)  AI=%.3f FLOP/Byte\n\n",
           ms, gbs, 100.0 * gbs / pk.gbs, flop / bytes_moved);

    float h; CUDA_CHECK(cudaMemcpy(&h, da, sizeof(float), cudaMemcpyDeviceToHost));
    printf("  (check a[0]=%g)\n\n", h);
    cudaFree(da); cudaFree(db); cudaFree(dc);
}
