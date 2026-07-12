// fma.cuh — FMA-Last-Komponente: reine FP32-Rechenlast mit einstellbarer
//           arithmetischer Intensität; optional mit Warp-Divergenz.
#pragma once

// Ein FMA-Lauf: n Elemente, iters FMA-Paare, reps Wiederholungen (Mittel).
void fma_run(int n, int iters, int reps);

// Divergenz-Experiment: halbe Threads "schwer", halbe "leicht"; 'stride'
// steuert die Verteilung (>=32 = divergenzfrei, 1 = maximale Divergenz).
void fma_run_div(int n, int iters, int stride, int reps);

// AI-Sweep (iters 32..16384) als CSV inkl. Roofline-Peaks im Header.
void fma_run_csv(const char* path);

// n-Skalierungs-Sweep (n=1024..2^27, festes iters) als CSV: zeigt Warm-up
// und Sättigung. Header enthält die Sättigungsschwelle (Threads).
void fma_run_sweepn(const char* path);

// Divergenz-Sweep mit Strides rund um die Warp-Grenze als CSV.
void fma_run_sweepdiv(const char* path);
