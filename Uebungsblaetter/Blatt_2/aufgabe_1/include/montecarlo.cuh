// montecarlo.cuh — Monte-Carlo-Komponente: stochastische Compute-Last.
//   π-Schätzung durch zufällige Stichproben; RNG pro Thread, Reduktion via
//   atomicAdd. Ein Thread bearbeitet 'samplesPerThread' Samples.
#pragma once

// n Threads, jeder zieht samplesPerThread Punkte; reps Wiederholungen (Mittel).
void montecarlo_run(int n, int samplesPerThread, int reps);

// Sweep ueber Stichproben pro Thread als CSV.
void montecarlo_run_sweep(const char* path);

// Beispielpunkte des Monte-Carlo-Verfahrens als CSV fuer eine Illustration.
void montecarlo_write_points(const char* path, int n);
