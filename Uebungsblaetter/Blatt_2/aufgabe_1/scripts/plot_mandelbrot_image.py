#!/usr/bin/env python3
"""Convert a Mandelbrot PGM image to a presentation PNG."""
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "outputs/mandelbrot.pgm"
out = sys.argv[2] if len(sys.argv) > 2 else "outputs/mandelbrot_result.png"

img = plt.imread(path)
fig, ax = plt.subplots(figsize=(8, 5))
ax.imshow(img, cmap="magma", origin="upper", extent=[-2.5, 1.0, -1.25, 1.25])
ax.set_xlabel("Realteil")
ax.set_ylabel("Imaginaerteil")
ax.set_title("Mandelbrot-Ergebnisbild: Iterationen bis zum Escape")
fig.tight_layout()
fig.savefig(out, dpi=160)
print(f"geschrieben: {out}")
