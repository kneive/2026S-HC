#!/usr/bin/env python3
"""Plot Mandelbrot sweep CSV."""
import csv
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "outputs/mandelbrot.csv"
out = sys.argv[2] if len(sys.argv) > 2 else "outputs/mandelbrot_scaling.png"

rows = []
with open(path, newline="") as fh:
    for line in fh:
        if line.startswith("#"):
            continue
        rows.append(line)

data = list(csv.DictReader(rows))
width = np.array([int(r["width"]) for r in data])
pixels = np.array([int(r["pixels"]) for r in data])
ms = np.array([float(r["ms"]) for r in data])
gflops = np.array([float(r["gflops_upper"]) for r in data])
mean_iter = np.array([float(r["mean_iter"]) for r in data])

fig, ax1 = plt.subplots(figsize=(8, 5.2))
ax1.plot(width, ms, "o-", color="C0", label="Zeit")
ax1.set_xscale("log", base=2)
ax1.set_xlabel("Bildbreite = Bildhoehe [Pixel]")
ax1.set_ylabel("Zeit [ms]", color="C0")
ax1.tick_params(axis="y", labelcolor="C0")
ax1.grid(True, which="both", ls=":", alpha=0.45)

ax2 = ax1.twinx()
ax2.plot(width, mean_iter, "s--", color="C3", label="mittlere Iterationen")
ax2.set_ylabel("mittlere Iterationen pro Pixel", color="C3")
ax2.tick_params(axis="y", labelcolor="C3")

title = f"Mandelbrot: Laufzeit und mittlere Iteration (max {width.max()} px)"
ax1.set_title(title)
lines = ax1.get_lines() + ax2.get_lines()
labels = [line.get_label() for line in lines]
ax1.legend(lines, labels, loc="upper left", fontsize=8)
fig.tight_layout()
fig.savefig(out, dpi=140)
print(f"geschrieben: {out}")
