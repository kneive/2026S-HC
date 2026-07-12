// memory.cuh — Speicher-Komponente: memory-bound Streaming mit drei
//   Zugriffsmustern (coalesced / strided / random-gather) und Occupancy-Studie.
//   Zeigt, dass das Zugriffsmuster die effektive Bandbreite bestimmt und dass
//   die GPU Latenz durch viele residente Warps (Occupancy) versteckt.
#pragma once

// Einmal alle drei Muster ausführen (n Elemente, Stride k, reps).
void memory_run(int n, int stride, int reps);

// Stride-Sweep (coalesced -> strided) + Gather als CSV.
void memory_run_patterns_csv(const char* path);

// Blockgrößen-Sweep mit gemessener Occupancy als CSV.
void memory_run_occupancy_csv(const char* path);
