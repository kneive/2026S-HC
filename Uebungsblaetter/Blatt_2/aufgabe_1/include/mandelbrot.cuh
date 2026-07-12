// mandelbrot.cuh — Mandelbrot-Komponente: datenabhängige Compute-Last,
//                  ein Thread pro Pixel, minimaler Speicherbedarf.
#pragma once

// Rendert ein w×h-Bild mit maxIter Iterationen; reps Wiederholungen (Mittel).
void mandelbrot_run(int w, int h, int maxIter, int reps);

// Laufzeit-Sweep fuer mehrere Aufloesungen als CSV.
void mandelbrot_run_sweep(const char* path);

// Rendert das Iterationsbild als PGM-Datei fuer eine spaetere PNG-Darstellung.
void mandelbrot_write_image(const char* path, int w, int h, int maxIter);
