// stream.cuh — STREAM-Triad-Komponente: bewusst memory-bound.
//   a[i] = b[i] + alpha*c[i]. Misst die real erreichbare Bandbreite und
//   füllt die schräge (BW-limitierte) Seite der Roofline empirisch.
#pragma once

// n Elemente pro Array; reps Wiederholungen (Mittel).
void stream_run(int n, int reps);
