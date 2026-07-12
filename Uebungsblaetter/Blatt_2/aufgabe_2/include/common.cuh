// common.cuh — geteilte Infrastruktur für alle Kernel-Komponenten
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>

// ---- Fehlerprüfung ----------------------------------------------------------
#define CUDA_CHECK(call)                                                       \
    do {                                                                       \
        cudaError_t _e = (call);                                              \
        if (_e != cudaSuccess) {                                              \
            fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__,     \
                    cudaGetErrorString(_e));                                  \
            exit(EXIT_FAILURE);                                               \
        }                                                                      \
    } while (0)

// ---- GPU-Timing über CUDA-Events -------------------------------------------
struct GpuTimer {
    cudaEvent_t a, b;
    GpuTimer()  { cudaEventCreate(&a); cudaEventCreate(&b); }
    ~GpuTimer() { cudaEventDestroy(a); cudaEventDestroy(b); }
    void  start() { cudaEventRecord(a); }
    float stop()  { cudaEventRecord(b); cudaEventSynchronize(b);
                    float ms; cudaEventElapsedTime(&ms, a, b); return ms; }
};

// ---- Roofline-Peaks der aktiven GPU ----------------------------------------
struct Peaks { double gflops, gbs, ai_star; };

// clockRate/memoryClockRate/memoryBusWidth wurden aus cudaDeviceProp entfernt
// (CUDA 13). -> über cudaDeviceGetAttribute abfragen (kHz bzw. Bit).
inline void query_clocks(int dev, int* clk, int* memclk, int* bus) {
    CUDA_CHECK(cudaDeviceGetAttribute(clk,    cudaDevAttrClockRate,           dev));
    CUDA_CHECK(cudaDeviceGetAttribute(memclk, cudaDevAttrMemoryClockRate,     dev));
    CUDA_CHECK(cudaDeviceGetAttribute(bus,    cudaDevAttrGlobalMemoryBusWidth,dev));
}

inline Peaks get_peaks() {
    int dev; CUDA_CHECK(cudaGetDevice(&dev));
    cudaDeviceProp p; CUDA_CHECK(cudaGetDeviceProperties(&p, dev));
    int clk, memclk, bus; query_clocks(dev, &clk, &memclk, &bus);
    double gflops = p.multiProcessorCount * 128.0 * 2.0 * (clk * 1e3) / 1e9;
    double gbs    = 2.0 * memclk * 1e3 * (bus / 8.0) / 1e9;
    return { gflops, gbs, gflops / gbs };
}

// Maximale gleichzeitig resident lauffähige Thread-Zahl (Sättigungsschwelle):
// SMs * maxThreadsPerMultiProcessor. Ab hier ist das Gerät voll ausgelastet.
inline long long saturation_threads() {
    int dev; CUDA_CHECK(cudaGetDevice(&dev));
    cudaDeviceProp p; CUDA_CHECK(cudaGetDeviceProperties(&p, dev));
    return (long long)p.multiProcessorCount * p.maxThreadsPerMultiProcessor;
}

inline void print_gpu_info() {
    int dev; CUDA_CHECK(cudaGetDevice(&dev));
    cudaDeviceProp p; CUDA_CHECK(cudaGetDeviceProperties(&p, dev));
    int clk, memclk, bus; query_clocks(dev, &clk, &memclk, &bus);
    Peaks pk = get_peaks();
    printf("=== GPU: %s (sm_%d%d) ===\n", p.name, p.major, p.minor);
    printf("  SMs=%d  FP32-Kerne~%.0f  Boost=%.0f MHz\n",
           p.multiProcessorCount, p.multiProcessorCount * 128.0, clk / 1e3);
    printf("  Peak FP32 ~ %.1f GFLOP/s   Peak BW ~ %.1f GB/s   "
           "Roofline-Knick AI* ~ %.2f FLOP/Byte\n\n",
           pk.gflops, pk.gbs, pk.ai_star);
}
