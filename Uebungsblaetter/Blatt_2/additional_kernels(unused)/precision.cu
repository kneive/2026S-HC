// precision.cu — Implementierung des Präzisions-Sweeps (FP64/FP32/FP16)
#include "precision.cuh"
#include "common.cuh"
#include <cstdio>
#include <cuda_fp16.h>

// ---- FP64-Kernel: FMA-Kette in double --------------------------------------
//   Auf Consumer-Ada laufen FP64-Einheiten mit ~1/64 der FP32-Rate.
__global__ void kernel_fma_f64(const double* __restrict__ in,
                               double* __restrict__ out, int n, int iters)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    double x = in[idx], a = 1.0000001, b = -0.0000001;
    double acc0 = x, acc1 = x * 0.5 + 1.0;
    #pragma unroll 8
    for (int i = 0; i < iters; ++i) {
        acc0 = fma(acc0, a, b);
        acc1 = fma(acc1, a, b);
    }
    out[idx] = acc0 + acc1;
}

// ---- FP32-Kernel: FMA-Kette in float ---------------------------------------
__global__ void kernel_fma_f32(const float* __restrict__ in,
                               float* __restrict__ out, int n, int iters)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    float x = in[idx], a = 1.0000001f, b = -0.0000001f;
    float acc0 = x, acc1 = x * 0.5f + 1.0f;
    #pragma unroll 8
    for (int i = 0; i < iters; ++i) {
        acc0 = fmaf(acc0, a, b);
        acc1 = fmaf(acc1, a, b);
    }
    out[idx] = acc0 + acc1;
}

// ---- FP16-Kernel: FMA-Kette in half2 (gepackt) -----------------------------
//   half2 verarbeitet 2 Lanes pro Instruktion -> nutzt den FP16-Durchsatz
//   (~2x FP32). Jedes __hfma2 = 2 Lanes * 2 FLOP = 4 FLOP.
__global__ void kernel_fma_f16(const __half2* __restrict__ in,
                               __half2* __restrict__ out, int n, int iters)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    __half2 x = in[idx];
    __half2 a = __float2half2_rn(1.0009766f);   // nahe 1, in FP16 darstellbar
    __half2 b = __float2half2_rn(-0.0009766f);
    __half2 acc0 = x, acc1 = __hadd2(x, a);
    #pragma unroll 8
    for (int i = 0; i < iters; ++i) {
        acc0 = __hfma2(acc0, a, b);
        acc1 = __hfma2(acc1, a, b);
    }
    out[idx] = __hadd2(acc0, acc1);
}

namespace {
// Generisch: alloziert Puffer, misst, druckt Zeile. FLOP je Element = flopPerIter*iters.
template <typename T, typename Launch>
void bench_prec(const char* label, int n, int iters, int reps,
                double flopPerElem, Launch launch) {
    size_t bytes = (size_t)n * sizeof(T);
    T *d_in, *d_out;
    CUDA_CHECK(cudaMalloc(&d_in, bytes));
    CUDA_CHECK(cudaMalloc(&d_out, bytes));
    CUDA_CHECK(cudaMemset(d_in, 0, bytes));
    int block = 256, grid = (n + block - 1) / block;
    launch(grid, block, d_in, d_out);          // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r) launch(grid, block, d_in, d_out);
    float ms = t.stop() / reps;
    double flop = flopPerElem * n;
    printf("  %-5s Zeit=%8.3f ms   %9.1f GFLOP/s\n",
           label, ms, flop / (ms * 1e6));
    cudaFree(d_in); cudaFree(d_out);
}
} // namespace

// ---- Öffentliche API -------------------------------------------------------
void precision_run(int n, int iters, int reps) {
    printf("--- Präzisions-Sweep: n=%d, iters=%d, reps=%d ---\n", n, iters, reps);

    // FP64: 2 FMAs -> 4 FLOP/iter
    bench_prec<double>("FP64", n, iters, reps, 4.0 * iters,
        [=](int g, int b, double* i, double* o) {
            kernel_fma_f64<<<g, b>>>(i, o, n, iters); });

    // FP32: 2 FMAs -> 4 FLOP/iter
    bench_prec<float>("FP32", n, iters, reps, 4.0 * iters,
        [=](int g, int b, float* i, float* o) {
            kernel_fma_f32<<<g, b>>>(i, o, n, iters); });

    // FP16: 2 half2-FMAs -> 2*(2 Lanes*2 FLOP) = 8 FLOP/iter je half2-Element
    bench_prec<__half2>("FP16", n, iters, reps, 8.0 * iters,
        [=](int g, int b, __half2* i, __half2* o) {
            kernel_fma_f16<<<g, b>>>(i, o, n, iters); });

    printf("\n  Hinweis: FP16 zählt 2 Lanes/Element (half2); FP64 ist auf\n"
           "  Consumer-Ada ~1/64 der FP32-Rate.\n\n");
}
