#!/usr/bin/env python3
"""Divergenz-Plot aus `./bench sweepdiv divergence.csv`.
Usage: python3 plot_divergence.py divergence.csv [out.png]"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "divergence.csv"
out  = sys.argv[2] if len(sys.argv) > 2 else "divergence.png"

stride, gflops = [], []
with open(path) as fh:
    for line in fh:
        if line.startswith("#") or line.startswith("stride"):
            continue
        parts = line.strip().split(",")
        if len(parts) < 3:
            continue
        stride.append(int(parts[0]))
        gflops.append(float(parts[1]))

stride = np.array(stride); gflops = np.array(gflops)

fig, ax = plt.subplots(figsize=(8, 6))
colors = [
    "C2" if s % 32 == 0 else ("C3" if s < 32 else "C1")
    for s in stride
]
ax.bar([str(s) for s in stride], gflops, color=colors)
ax.axvline(np.argmax(stride >= 32) - 0.5, color="black", ls=":", lw=1)
ax.text(np.argmax(stride >= 32) - 0.5, gflops.max() * 0.5,
        " Warp-Grenze\n (stride=32)", fontsize=9, va="center")

ax.set_xlabel("stride  (Vielfache von 32 = warp-homogen)")
ax.set_ylabel("Durchsatz [GFLOP/s]")
ax.set_title("Warp-Divergenz: Performance-Einbruch (RTX 4060)")
ax.grid(True, axis="y", ls=":", alpha=0.5)
# Divergenz-Faktor annotieren
div = gflops[stride < 32].mean()
nodiv = gflops[stride % 32 == 0].mean()
ax.text(0.02, 0.95, f"Durchsatzfaktor warp-homogen/divergent: {nodiv/div:.2f}×",
        transform=ax.transAxes, fontsize=9, va="top",
        bbox=dict(boxstyle="round", fc="white", alpha=0.8))
fig.tight_layout()
fig.savefig(out, dpi=130)
print(f"geschrieben: {out}")
