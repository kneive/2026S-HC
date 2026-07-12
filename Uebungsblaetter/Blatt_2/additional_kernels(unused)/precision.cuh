// precision.cuh — Präzisions-Sweep-Komponente: dieselbe FMA-Last in
//   FP64 / FP32 / FP16. Zeigt den Durchsatz-vs-Genauigkeit-Tradeoff und
//   die drastisch reduzierte FP64-Rate auf Consumer-Ada (~1/64 von FP32).
#pragma once

// Führt alle drei Präzisionen nacheinander aus (n Elemente, iters, reps).
void precision_run(int n, int iters, int reps);
