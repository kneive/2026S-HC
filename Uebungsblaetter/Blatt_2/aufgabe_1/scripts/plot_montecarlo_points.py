#!/usr/bin/env python3
"""Plot Monte-Carlo points and the quarter circle."""
import csv
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "outputs/montecarlo_points.csv"
out = sys.argv[2] if len(sys.argv) > 2 else "outputs/montecarlo_result.png"

x, y, inside = [], [], []
with open(path, newline="") as fh:
    reader = csv.DictReader(fh)
    for row in reader:
        x.append(float(row["x"]))
        y.append(float(row["y"]))
        inside.append(int(row["inside"]))

x = np.array(x)
y = np.array(y)
inside = np.array(inside, dtype=bool)
pi_hat = 4.0 * inside.mean()

fig, ax = plt.subplots(figsize=(6.2, 6.2))
ax.scatter(x[inside], y[inside], s=2, alpha=0.45, color="C0", label="Treffer")
ax.scatter(x[~inside], y[~inside], s=2, alpha=0.45, color="C3", label="ausserhalb")
theta = np.linspace(0.0, np.pi / 2.0, 300)
ax.plot(np.cos(theta), np.sin(theta), color="black", lw=1.2, label="Viertelkreis")
ax.set_aspect("equal", adjustable="box")
ax.set_xlim(0, 1)
ax.set_ylim(0, 1)
ax.set_xlabel("x")
ax.set_ylabel("y")
ax.set_title(f"Monte-Carlo-Ergebnisbild: pi ≈ {pi_hat:.5f}")
ax.legend(loc="lower left", fontsize=8)
ax.grid(True, ls=":", alpha=0.35)
fig.tight_layout()
fig.savefig(out, dpi=160)
print(f"geschrieben: {out}")
