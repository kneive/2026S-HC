// fma.cu — Implementierung der FMA-Last-Komponente
#include "fma.cuh"
#include "common.cuh"
#include <cstdio>

// ---- Kernel: reine FMA-Kette -----------------------------------------------
//   Jeder Thread lädt 1 float, führt iters mal 2 FMAs (=4 FLOP) aus und
//   schreibt 1 float. 8 B Speicher / Element -> AI = iters/2 [FLOP/Byte].
//   Zwei unabhängige Akkus verstecken die FMA-Latenz (ILP).
__global__ void kernel_fma(const float* __restrict__ in,
                           float* __restrict__ out, int n, int iters)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    float x = in[idx];
    float a = 1.0000001f, b = -0.0000001f;
    float acc0 = x, acc1 = x * 0.5f + 1.0f;

    #pragma unroll 8
    for (int i = 0; i < iters; ++i) {
        acc0 = fmaf(acc0, a, b);
        acc1 = fmaf(acc1, a, b);
    }
    out[idx] = acc0 + acc1;   // datenabhängig -> nicht wegoptimierbar
}

// ---- Kernel: FMA mit steuerbarer Warp-Divergenz ----------------------------
//   Hälfte "schwer" (iters), Hälfte "leicht" (iters/8). 'stride' verteilt die
//   Pfade: >=32 -> ganze Warps homogen (keine Divergenz), 1 -> Lanes wechseln.
__global__ void kernel_fma_div(const float* __restrict__ in,
                               float* __restrict__ out,
                               int n, int iters, int stride)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    float x = in[idx];
    float a = 1.0000001f, b = -0.0000001f;
    float acc = x;
    bool heavy = ((idx / stride) & 1) == 0;

    if (heavy) {
        #pragma unroll 8
        for (int i = 0; i < iters; ++i) acc = fmaf(acc, a, b);
    } else {
        int light = iters / 8;
        #pragma unroll 8
        for (int i = 0; i < light; ++i) acc = fmaf(acc, a, b);
    }
    out[idx] = acc;
}

// ---- Host-Helfer: Ein-/Ausgabepuffer vorbereiten ---------------------------
namespace {
struct Buffers {
    float *d_in, *d_out; float *h_in; size_t bytes; int n;
    Buffers(int n_) : n(n_) {
        bytes = (size_t)n * sizeof(float);
        h_in = (float*)malloc(bytes);
        for (int i = 0; i < n; ++i) h_in[i] = 1.0f + 1e-6f * i;
        CUDA_CHECK(cudaMalloc(&d_in, bytes));
        CUDA_CHECK(cudaMalloc(&d_out, bytes));
        CUDA_CHECK(cudaMemcpy(d_in, h_in, bytes, cudaMemcpyHostToDevice));
    }
    ~Buffers() { cudaFree(d_in); cudaFree(d_out); free(h_in); }
};

// Ein Messpunkt für den reinen FMA-Kernel -> Zeit in ms.
float time_fma(Buffers& buf, int iters, int reps) {
    int block = 256, grid = (buf.n + block - 1) / block;
    kernel_fma<<<grid, block>>>(buf.d_in, buf.d_out, buf.n, iters); // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r)
        kernel_fma<<<grid, block>>>(buf.d_in, buf.d_out, buf.n, iters);
    return t.stop() / reps;
}
} // namespace

// ---- Öffentliche API -------------------------------------------------------
void fma_run(int n, int iters, int reps) {
    printf("--- FMA-Last: n=%d Elemente, iters=%d, reps=%d ---\n", n, iters, reps);
    Buffers buf(n);
    float ms = time_fma(buf, iters, reps);
    double flop = 4.0 * (double)iters * n;
    double bytes = 8.0 * n;
    printf("  Zeit=%.3f ms  %.1f GFLOP/s  %.2f GB/s  AI=%.2f FLOP/Byte\n\n",
           ms, flop / (ms * 1e6), bytes / (ms * 1e6), flop / bytes);

    float h_out; CUDA_CHECK(cudaMemcpy(&h_out, buf.d_out, sizeof(float),
                                       cudaMemcpyDeviceToHost));
    printf("  (check out[0]=%g)\n\n", h_out);
}

void fma_run_div(int n, int iters, int stride, int reps) {
    printf("--- FMA-Divergenz: n=%d iters=%d stride=%d reps=%d ---\n",
           n, iters, stride, reps);
    Buffers buf(n);
    int block = 256, grid = (n + block - 1) / block;
    kernel_fma_div<<<grid, block>>>(buf.d_in, buf.d_out, n, iters, stride); // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r)
        kernel_fma_div<<<grid, block>>>(buf.d_in, buf.d_out, n, iters, stride);
    float ms = t.stop() / reps;
    double flop = 2.0 * (double)(n / 2) * (iters + iters / 8);
    printf("  Zeit=%.3f ms  %.1f GFLOP/s  (%s)\n\n",
           ms, flop / (ms * 1e6),
           stride >= 32 ? "divergenzfrei" : "divergent");
}

void fma_run_csv(const char* path) {
    Peaks pk = get_peaks();
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# peak_gflops=%.3f peak_gbs=%.3f ai_star=%.4f\n",
            pk.gflops, pk.gbs, pk.ai_star);
    fprintf(f, "kernel,param,iters,ai_flop_per_byte,gflops,gbs,ms\n");
    int n = 1 << 26;
    Buffers buf(n);
    printf("AI-Sweep -> %s\n", path);
    for (int it = 32; it <= 16384; it *= 2) {
        float ms = time_fma(buf, it, 20);
        double flop = 4.0 * (double)it * n, bytes = 8.0 * n;
        double g = flop / (ms * 1e6), b = bytes / (ms * 1e6), ai = flop / bytes;
        fprintf(f, "fma,%d,%d,%.4f,%.3f,%.3f,%.4f\n", n, it, ai, g, b, ms);
        printf("  iters=%-6d AI=%-9.2f %8.1f GFLOP/s  %7.2f GB/s\n", it, ai, g, b);
    }
    fclose(f);
    printf("Fertig. Plot:  python3 plot_roofline.py %s\n", path);
}

// ---- A) n-Skalierungs-Sweep ------------------------------------------------
//   Festes iters, n von 1024 bis 2^27 verdoppelt. Zeigt, ab welcher
//   Problemgröße die GPU "warmläuft" (Plateau) und wie die aktive Thread-Zahl
//   zur Sättigungsschwelle steht.
void fma_run_sweepn(const char* path) {
    const int iters = 512, reps = 30;
    const int maxN = 1 << 27;                 // ~134 Mio Elemente
    long long sat = saturation_threads();
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# iters=%d saturation_threads=%lld\n", iters, sat);
    fprintf(f, "n,threads,gflops,ms,frac_of_saturation\n");
    printf("n-Sweep (iters=%d) -> %s   [Saettigung ~%lld Threads]\n",
           iters, path, sat);

    Buffers buf(maxN);                        // einmal allozieren, Bereiche nutzen
    for (int n = 1024; n <= maxN; n *= 2) {
        int block = 256, grid = (n + block - 1) / block;
        long long threads = (long long)grid * block;
        kernel_fma<<<grid, block>>>(buf.d_in, buf.d_out, n, iters); // Warmup
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());
        GpuTimer t; t.start();
        for (int r = 0; r < reps; ++r)
            kernel_fma<<<grid, block>>>(buf.d_in, buf.d_out, n, iters);
        float ms = t.stop() / reps;
        double g = 4.0 * (double)iters * n / (ms * 1e6);
        double frac = (double)threads / sat;
        fprintf(f, "%d,%lld,%.3f,%.5f,%.3f\n", n, threads, g, ms, frac);
        printf("  n=%-10d threads=%-10lld %9.1f GFLOP/s  (%.2fx Saettigung)\n",
               n, threads, g, frac);
    }
    fclose(f);
    printf("Fertig. Plot:  python3 plot_scaling.py %s\n", path);
}

// ---- B) Divergenz-Sweep ----------------------------------------------------
//   Festes n/iters, mit zusaetzlichen Strides knapp oberhalb der Warp-Grenze.
//   Gleiche Gesamtarbeit -> Laufzeitunterschied = reine Divergenz-Serialisierung.
void fma_run_sweepdiv(const char* path) {
    const int n = 1 << 24, iters = 4096, reps = 20;
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# n=%d iters=%d warpSize=32\n", n, iters);
    fprintf(f, "stride,gflops,ms,regime\n");
    printf("Divergenz-Sweep (n=%d iters=%d) -> %s\n", n, iters, path);

    Buffers buf(n);
    int block = 256, grid = (n + block - 1) / block;
    // tatsächliche FLOPs: halbe Threads schwer (iters), halbe leicht (iters/8)
    double flop = 2.0 * (double)(n / 2) * (iters + iters / 8);
    const int strides[] = {
        1, 2, 4, 8, 16, 32,
        33, 34, 36, 40, 48, 64,
        65, 66, 68, 72, 80, 96
    };
    const int num_strides = sizeof(strides) / sizeof(strides[0]);
    for (int i = 0; i < num_strides; ++i) {
        int stride = strides[i];
        kernel_fma_div<<<grid, block>>>(buf.d_in, buf.d_out, n, iters, stride);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());
        GpuTimer t; t.start();
        for (int r = 0; r < reps; ++r)
            kernel_fma_div<<<grid, block>>>(buf.d_in, buf.d_out, n, iters, stride);
        float ms = t.stop() / reps;
        double g = flop / (ms * 1e6);
        const char* regime = (stride % 32 == 0)
            ? "divergenzfrei"
            : (stride < 32 ? "divergent" : "teilweise divergent");
        fprintf(f, "%d,%.3f,%.5f,%s\n", stride, g, ms, regime);
        printf("  stride=%-3d %9.1f GFLOP/s  %8.3f ms  (%s)\n",
               stride, g, ms, regime);
    }
    fclose(f);
    printf("Fertig. Plot:  python3 plot_divergence.py %s\n", path);
}
