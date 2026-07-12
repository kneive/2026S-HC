// mandelbrot.cu — Implementierung der Mandelbrot-Komponente
#include "mandelbrot.cuh"
#include "common.cuh"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>

// ---- Kernel: z = z^2 + c pro Pixel -----------------------------------------
//   Jeder Thread iteriert bis |z|>2 oder maxIter. Speicher: nur 4 B write.
//   ~10 FLOP/Iteration, datenabhängig (natürliche Warp-Divergenz).
__global__ void kernel_mandelbrot(uint32_t* __restrict__ out,
                                  int width, int height, int maxIter,
                                  float xmin, float xmax,
                                  float ymin, float ymax)
{
    int px = blockIdx.x * blockDim.x + threadIdx.x;
    int py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= width || py >= height) return;

    float cx = xmin + (xmax - xmin) * (px / (float)width);
    float cy = ymin + (ymax - ymin) * (py / (float)height);

    float zx = 0.0f, zy = 0.0f;
    int it = 0;
    while (it < maxIter) {
        float zx2 = zx * zx, zy2 = zy * zy;
        if (zx2 + zy2 > 4.0f) break;      // |z|>2 -> divergiert
        zy = fmaf(2.0f * zx, zy, cy);     // 2*zx*zy + cy
        zx = zx2 - zy2 + cx;              // zx^2 - zy^2 + cx
        ++it;
    }
    out[py * width + px] = (uint32_t)it;
}

// ---- Öffentliche API -------------------------------------------------------
void mandelbrot_run(int w, int h, int maxIter, int reps) {
    printf("--- Mandelbrot: %dx%d, maxIter=%d, reps=%d ---\n", w, h, maxIter, reps);
    size_t px = (size_t)w * h, bytes = px * sizeof(uint32_t);
    uint32_t* d_out;
    CUDA_CHECK(cudaMalloc(&d_out, bytes));

    dim3 block(16, 16);
    dim3 grid((w + block.x - 1) / block.x, (h + block.y - 1) / block.y);
    kernel_mandelbrot<<<grid, block>>>(d_out, w, h, maxIter,
                                       -2.5f, 1.0f, -1.25f, 1.25f); // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r)
        kernel_mandelbrot<<<grid, block>>>(d_out, w, h, maxIter,
                                           -2.5f, 1.0f, -1.25f, 1.25f);
    float ms = t.stop() / reps;

    double flop_ub = 10.0 * (double)maxIter * px;   // obere Schranke
    double bytes_mv = 4.0 * px;                      // nur write
    printf("  Zeit=%.3f ms  <=%.1f GFLOP/s (obere Schranke)  "
           "AI (worst case)=%.1f FLOP/Byte\n\n",
           ms, flop_ub / (ms * 1e6), flop_ub / bytes_mv);

    uint32_t* h_out = (uint32_t*)malloc(bytes);
    CUDA_CHECK(cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost));
    uint64_t sum = 0; for (size_t i = 0; i < px; ++i) sum += h_out[i];
    printf("  (check Iterationssumme=%llu, mittel=%.1f)\n\n",
           (unsigned long long)sum, (double)sum / px);
    free(h_out); cudaFree(d_out);
}

static void write_pgm(const char* path, const uint32_t* data,
                      int w, int h, int maxIter)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        float t = data[i] >= (uint32_t)maxIter
            ? 0.0f
            : sqrtf(data[i] / (float)maxIter);
        unsigned char v = (unsigned char)(255.0f * t);
        fwrite(&v, 1, 1, f);
    }
    fclose(f);
}

void mandelbrot_run_sweep(const char* path) {
    const int sizes[] = {512, 1024, 2048, 4096};
    const int maxIter = 1000, reps = 10;
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fprintf(f, "# maxIter=%d reps=%d\n", maxIter, reps);
    fprintf(f, "width,height,pixels,ms,gflops_upper,mean_iter\n");
    printf("Mandelbrot-Sweep -> %s\n", path);

    for (int si = 0; si < (int)(sizeof(sizes) / sizeof(sizes[0])); ++si) {
        int w = sizes[si], h = sizes[si];
        size_t px = (size_t)w * h, bytes = px * sizeof(uint32_t);
        uint32_t* d_out;
        CUDA_CHECK(cudaMalloc(&d_out, bytes));

        dim3 block(16, 16);
        dim3 grid((w + block.x - 1) / block.x, (h + block.y - 1) / block.y);
        kernel_mandelbrot<<<grid, block>>>(d_out, w, h, maxIter,
                                           -2.5f, 1.0f, -1.25f, 1.25f);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());

        GpuTimer t; t.start();
        for (int r = 0; r < reps; ++r)
            kernel_mandelbrot<<<grid, block>>>(d_out, w, h, maxIter,
                                               -2.5f, 1.0f, -1.25f, 1.25f);
        float ms = t.stop() / reps;

        uint32_t* h_out = (uint32_t*)malloc(bytes);
        CUDA_CHECK(cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost));
        uint64_t sum = 0;
        for (size_t i = 0; i < px; ++i) sum += h_out[i];
        double mean_iter = (double)sum / px;
        double flop_ub = 10.0 * (double)maxIter * px;
        double gflops = flop_ub / (ms * 1e6);
        fprintf(f, "%d,%d,%zu,%.5f,%.3f,%.3f\n",
                w, h, px, ms, gflops, mean_iter);
        printf("  %4dx%-4d %8.3f ms  <=%8.1f GFLOP/s  mean_iter=%.1f\n",
               w, h, ms, gflops, mean_iter);

        free(h_out);
        cudaFree(d_out);
    }
    fclose(f);
}

void mandelbrot_write_image(const char* path, int w, int h, int maxIter) {
    size_t px = (size_t)w * h, bytes = px * sizeof(uint32_t);
    uint32_t* d_out;
    CUDA_CHECK(cudaMalloc(&d_out, bytes));

    dim3 block(16, 16);
    dim3 grid((w + block.x - 1) / block.x, (h + block.y - 1) / block.y);
    kernel_mandelbrot<<<grid, block>>>(d_out, w, h, maxIter,
                                       -2.5f, 1.0f, -1.25f, 1.25f);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    uint32_t* h_out = (uint32_t*)malloc(bytes);
    CUDA_CHECK(cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost));
    write_pgm(path, h_out, w, h, maxIter);
    printf("Mandelbrot-Bild geschrieben: %s (%dx%d, maxIter=%d)\n",
           path, w, h, maxIter);

    free(h_out);
    cudaFree(d_out);
}
