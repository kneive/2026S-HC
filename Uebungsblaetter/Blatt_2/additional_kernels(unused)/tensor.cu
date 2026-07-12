// tensor.cu — Implementierung der Tensor-Core-GEMM-Komponente (WMMA)
#include "tensor.cuh"
#include "common.cuh"
#include <cstdio>
#include <mma.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>

using namespace nvcuda;

// Block-Kachel: 64x64 Ausgabe, K-Schritt 16. 4 Warps (128 Threads), als 2x2
// angeordnet; jeder Warp berechnet 32x32 = 2x2 WMMA-Fragmente (16x16 each).
static const int BM = 64, BN = 64, BK = 16;

// ---- Kernel: C = A*B mit Shared-Memory-Tiling ------------------------------
//   A,B,C sind row-major. Pro K-Schritt lädt der Block je eine 64x16- und
//   16x64-Kachel EINMAL in Shared Memory; die 4 Warps lesen daraus mehrfach
//   (Datenwiederverwendung -> compute-bound). mma_sync ist die Tensor-Core-
//   Instruktion: ein 16x16x16-Fused-Multiply-Add pro Aufruf.
__global__ void kernel_wmma_gemm(const half* __restrict__ A,
                                 const half* __restrict__ B,
                                 float* __restrict__ C, int M, int N, int K)
{
    __shared__ half As[BM][BK];   // 64x16
    __shared__ half Bs[BK][BN];   // 16x64

    int tid     = threadIdx.x;              // 0..127
    int warpId  = tid / warpSize;           // 0..3
    int warpRow = warpId / 2, warpCol = warpId % 2;   // 2x2-Anordnung
    int blockRow = blockIdx.y * BM, blockCol = blockIdx.x * BN;

    // Akkumulatoren: 2x2 Fragmente pro Warp (32x32-Region).
    wmma::fragment<wmma::accumulator, 16, 16, 16, float> acc[2][2];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) wmma::fill_fragment(acc[i][j], 0.0f);

    for (int k = 0; k < K; k += BK) {
        // As (1024) und Bs (1024) kooperativ laden: 8 Elemente je Thread.
        for (int idx = tid; idx < BM * BK; idx += blockDim.x) {
            int r = idx / BK, c = idx % BK;
            As[r][c] = A[(blockRow + r) * K + (k + c)];
        }
        for (int idx = tid; idx < BK * BN; idx += blockDim.x) {
            int r = idx / BN, c = idx % BN;
            Bs[r][c] = B[(k + r) * N + (blockCol + c)];
        }
        __syncthreads();

        wmma::fragment<wmma::matrix_a, 16, 16, 16, half, wmma::row_major> a_frag[2];
        wmma::fragment<wmma::matrix_b, 16, 16, 16, half, wmma::row_major> b_frag[2];
        for (int i = 0; i < 2; ++i)
            wmma::load_matrix_sync(a_frag[i], &As[warpRow * 32 + i * 16][0], BK);
        for (int j = 0; j < 2; ++j)
            wmma::load_matrix_sync(b_frag[j], &Bs[0][warpCol * 32 + j * 16], BN);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                wmma::mma_sync(acc[i][j], a_frag[i], b_frag[j], acc[i][j]);
        __syncthreads();
    }

    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            int cRow = blockRow + warpRow * 32 + i * 16;
            int cCol = blockCol + warpCol * 32 + j * 16;
            if (cRow < M && cCol < N)
                wmma::store_matrix_sync(C + cRow * N + cCol, acc[i][j], N,
                                        wmma::mem_row_major);
        }
}

// ---- Öffentliche API -------------------------------------------------------
void tensor_run(int dim, int reps) {
    int M = (dim / 16) * 16, N = M, K = M;   // auf Vielfache von 16 runden
    printf("--- Tensor-Core GEMM (WMMA, FP16->FP32): %dx%d, reps=%d ---\n",
           M, N, reps);

    size_t szAB = (size_t)M * K * sizeof(half);
    size_t szC  = (size_t)M * N * sizeof(float);
    half *dA, *dB; float *dC;
    CUDA_CHECK(cudaMalloc(&dA, szAB));
    CUDA_CHECK(cudaMalloc(&dB, szAB));
    CUDA_CHECK(cudaMalloc(&dC, szC));
    CUDA_CHECK(cudaMemset(dA, 0, szAB));   // Inhalt egal für Timing
    CUDA_CHECK(cudaMemset(dB, 0, szAB));

    // 128 Threads (4 Warps) pro Block, jeder Block berechnet 64x64 Ausgabe.
    dim3 block(128);
    dim3 grid((N + BN - 1) / BN, (M + BM - 1) / BM);

    kernel_wmma_gemm<<<grid, block>>>(dA, dB, dC, M, N, K);   // Warmup
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    GpuTimer t; t.start();
    for (int r = 0; r < reps; ++r)
        kernel_wmma_gemm<<<grid, block>>>(dA, dB, dC, M, N, K);
    float ms = t.stop() / reps;

    double flop = 2.0 * (double)M * N * K;   // GEMM: 2*M*N*K
    printf("  handgeschrieben (WMMA):  Zeit=%.3f ms  %.1f TFLOP/s\n",
           ms, flop / (ms * 1e9));

    // ---- Referenz: cuBLAS mit Tensor-Ops (nahe Hardware-Peak) --------------
    // cuBLAS ist spaltenweise (column-major). Wir berechnen C^T = B^T * A^T,
    // was column-major demselben row-major C = A*B entspricht.
    cublasHandle_t h; cublasCreate(&h);
    cublasSetMathMode(h, CUBLAS_TENSOR_OP_MATH);
    const float alpha = 1.0f, beta = 0.0f;
    // Warmup
    cublasGemmEx(h, CUBLAS_OP_N, CUBLAS_OP_N, N, M, K, &alpha,
                 dB, CUDA_R_16F, N, dA, CUDA_R_16F, K, &beta,
                 dC, CUDA_R_32F, N, CUBLAS_COMPUTE_32F,
                 CUBLAS_GEMM_DEFAULT_TENSOR_OP);
    CUDA_CHECK(cudaDeviceSynchronize());
    GpuTimer t2; t2.start();
    for (int r = 0; r < reps; ++r)
        cublasGemmEx(h, CUBLAS_OP_N, CUBLAS_OP_N, N, M, K, &alpha,
                     dB, CUDA_R_16F, N, dA, CUDA_R_16F, K, &beta,
                     dC, CUDA_R_32F, N, CUBLAS_COMPUTE_32F,
                     CUBLAS_GEMM_DEFAULT_TENSOR_OP);
    float ms2 = t2.stop() / reps;
    printf("  cuBLAS (Tensor-Ops):     Zeit=%.3f ms  %.1f TFLOP/s  (%.1fx)\n",
           ms2, flop / (ms2 * 1e9), ms / ms2);
    printf("  -> naive WMMA erreicht nur einen Bruchteil des Tensor-Peaks;\n"
           "     Pipelining/vektorisierte Loads (cuBLAS/CUTLASS) sind n\u00f6tig.\n\n");
    cublasDestroy(h);

    cudaFree(dA); cudaFree(dB); cudaFree(dC);
}
