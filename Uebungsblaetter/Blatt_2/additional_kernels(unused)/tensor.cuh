// tensor.cuh — Tensor-Core-GEMM-Komponente: C = A*B mit WMMA (FP16-Eingang,
//   FP32-Akkumulation). Zeigt die spezialisierte Matrix-Hardware, deren
//   Durchsatz weit über dem generischen FP32-FMA-Peak liegt.
#pragma once

// Quadratische GEMM der Größe dim x dim (auf Vielfache von 16 gerundet).
void tensor_run(int dim, int reps);
