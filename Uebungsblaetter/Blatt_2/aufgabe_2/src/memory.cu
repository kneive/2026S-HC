// memory.cu — Implementierung der Speicher-Komponente
#include "memory.cuh"
#include "common.cuh"
#include <cstdio>
#include <cstdlib>

// ---- Kernel: b[i] = a[i] * c, coalesced (Stride 1) -------------------------
//   Aufeinanderfolgende Threads lesen aufeinanderfolgende Adressen -> eine
//   Warp (32 Lanes) trifft zusammenhängende 128-Byte-Segmente: maximale
//   Coalescing-Effizienz, wenige Speichertransaktionen.
__global__ void kernel_copy_coalesced(const float* __restrict__ a,
                                      float* __restrict__ b, int n, float c)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) b[i] = a[i] * c;
}

// ---- Kernel: strided (Stride k) --------------------------------------------
//   Lane l liest a[(i*stride) % n]: die 32 Lanes einer Warp liegen jetzt
//   stride*4 Byte auseinander -> mehr Cache-Lines / Transaktionen pro Warp,
//   die nützliche Bandbreite sinkt (Segmente werden nur teilweise genutzt).
__global__ void kernel_copy_strided(const float* __restrict__ a,
                                    float* __restrict__ b,
                                    int n, int stride, float c)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        long long j = ((long long)i * stride) % n;
        b[i] = a[j] * c;
    }
}

// ---- Kernel: random gather -------------------------------------------------
//   Lane l liest a[idx[i]] mit zufälligem idx -> praktisch keine Coalescing,
//   jede Lane löst potenziell eine eigene Transaktion aus (Cache-Miss-lastig).
__global__ void kernel_copy_gather(const float* __restrict__ a,
                                   float* __restrict__ b,
                                   const int* __restrict__ idx, int n, float c)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) b[i] = a[idx[i]] * c;
}

namespace {
struct MemBuffers {
    float *a, *b; int *idx; int n;
    MemBuffers(int n_) : n(n_) {
        CUDA_CHECK(cudaMalloc(&a, (size_t)n * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&b, (size_t)n * sizeof(float)));
        CUDA_CHECK(cudaMalloc(&idx, (size_t)n * sizeof(int)));
        CUDA_CHECK(cudaMemset(a, 1, (size_t)n * sizeof(float)));
        // zufällige Indizes auf dem Host erzeugen und kopieren
        int* h = (int*)malloc((size_t)n * sizeof(int));
        srand(12345);
        for (int i = 0; i < n; ++i) h[i] = rand() % n;
        CUDA_CHECK(cudaMemcpy(idx, h, (size_t)n * sizeof(int),
                              cudaMemcpyHostToDevice));
        free(h);
    }
    ~MemBuffers() { cudaFree(a); cudaFree(b); cudaFree(idx); }
};

// Bandbreite (GB/s) für Coalesced/Strided. bytes = 8/Element (a lesen + b schreiben).
float time_copy(MemBuffers& m, int stride, int block, int reps) {
    int grid = (m.n + block - 1) / block;
    auto launch = [&]() {
        if (stride == 1)
            kernel_copy_coalesced<<<grid, block>>>(m.a, m.b, m.n, 3.0f);
        else
            kernel_copy_strided<<<grid, block>>>(m.a, m.b, m.n, stride, 3.0f);
    };
    launch();  // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r) launch();
    return t.stop() / reps;
}

float time_gather(MemBuffers& m, int block, int reps) {
    int grid = (m.n + block - 1) / block;
    kernel_copy_gather<<<grid, block>>>(m.a, m.b, m.idx, m.n, 3.0f);  // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r)
        kernel_copy_gather<<<grid, block>>>(m.a, m.b, m.idx, m.n, 3.0f);
    return t.stop() / reps;
}

// Gemessene (theoretische) Occupancy für kernel_copy_coalesced bei blockSize.
double occupancy_for(int block) {
    int dev; CUDA_CHECK(cudaGetDevice(&dev));
    cudaDeviceProp p; CUDA_CHECK(cudaGetDeviceProperties(&p, dev));
    int maxBlocks = 0;
    CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
        &maxBlocks, kernel_copy_coalesced, block, 0));
    int activeWarps = maxBlocks * (block / 32);
    int maxWarps    = p.maxThreadsPerMultiProcessor / 32;
    return (double)activeWarps / maxWarps;
}
} // namespace

// ---- Öffentliche API -------------------------------------------------------
void memory_run(int n, int stride, int reps) {
    printf("--- Speicher-Zugriffsmuster: n=%d, stride=%d, reps=%d ---\n",
           n, stride, reps);
    MemBuffers m(n);
    Peaks pk = get_peaks();
    double bytes = 8.0 * n;          // a lesen + b schreiben
    double gbytes = 12.0 * n;        // gather: + idx lesen

    float ms1 = time_copy(m, 1, 256, reps);
    float ms2 = time_copy(m, stride, 256, reps);
    float ms3 = time_gather(m, 256, reps);

    double g1 = bytes / (ms1 * 1e6), g2 = bytes / (ms2 * 1e6),
           g3 = gbytes / (ms3 * 1e6);
    printf("  (1) coalesced (Stride 1): %8.1f GB/s  (%.0f%% Peak)\n",
           g1, 100.0 * g1 / pk.gbs);
    printf("  (2) strided  (Stride %-3d): %8.1f GB/s  (%.0f%% Peak)\n",
           stride, g2, 100.0 * g2 / pk.gbs);
    printf("  (3) random gather        : %8.1f GB/s  (%.0f%% Peak)\n\n",
           g3, 100.0 * g3 / pk.gbs);
}

void memory_run_patterns_csv(const char* path) {
    const int n = 1 << 25, reps = 30;    // ~33 Mio Elemente
    MemBuffers m(n);
    Peaks pk = get_peaks();
    double bytes = 8.0 * n, gbytes = 12.0 * n;
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# n=%d peak_gbs=%.3f\n", n, pk.gbs);
    fprintf(f, "pattern,stride,gbs,pct_peak\n");
    printf("Zugriffsmuster-Sweep (n=%d) -> %s   [Peak %.0f GB/s]\n",
           n, path, pk.gbs);

    // Nur bis Stride 128: darüber aliast (i*stride)%n bei 2er-Potenzen in
    // wenige, cache-residente Elemente -> unrealistisch schnell (Artefakt).
    for (int s = 1; s <= 128; s *= 2) {
        float ms = time_copy(m, s, 256, reps);
        double g = bytes / (ms * 1e6);
        const char* pat = (s == 1) ? "coalesced" : "strided";
        fprintf(f, "%s,%d,%.2f,%.2f\n", pat, s, g, 100.0 * g / pk.gbs);
        printf("  stride=%-5d %8.1f GB/s  (%.0f%% Peak)\n",
               s, g, 100.0 * g / pk.gbs);
    }
    float msg = time_gather(m, 256, reps);
    double gg = gbytes / (msg * 1e6);
    fprintf(f, "gather,0,%.2f,%.2f\n", gg, 100.0 * gg / pk.gbs);
    printf("  gather        %8.1f GB/s  (%.0f%% Peak)\n", gg, 100.0 * gg / pk.gbs);
    fclose(f);
    printf("Fertig. Plot:  python3 plot_patterns.py %s\n", path);
}

void memory_run_occupancy_csv(const char* path) {
    const int n = 1 << 25, reps = 30;
    MemBuffers m(n);
    Peaks pk = get_peaks();
    double bytes = 8.0 * n;
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# n=%d peak_gbs=%.3f\n", n, pk.gbs);
    fprintf(f, "blocksize,occupancy,gbs,pct_peak\n");
    printf("Occupancy-Sweep (coalesced, n=%d) -> %s\n", n, path);

    for (int block = 32; block <= 1024; block *= 2) {
        double occ = occupancy_for(block);
        float ms = time_copy(m, 1, block, reps);
        double g = bytes / (ms * 1e6);
        fprintf(f, "%d,%.3f,%.2f,%.2f\n", block, occ, g, 100.0 * g / pk.gbs);
        printf("  block=%-5d Occupancy=%.2f  %8.1f GB/s  (%.0f%% Peak)\n",
               block, occ, g, 100.0 * g / pk.gbs);
    }
    fclose(f);
    printf("Fertig. Plot:  python3 plot_occupancy.py %s\n", path);
}
