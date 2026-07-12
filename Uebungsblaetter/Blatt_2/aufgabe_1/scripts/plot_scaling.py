#!/usr/bin/env python3
"""n-Skalierungs-Plot aus `./bench sweepn scaling.csv`.
Usage: python3 plot_scaling.py scaling.csv [out.png]"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "scaling.csv"
out  = sys.argv[2] if len(sys.argv) > 2 else "scaling.png"

sat = None
n, gflops, frac = [], [], []
with open(path) as fh:
    for line in fh:
        if line.startswith("#"):
            for tok in line[1:].split():
                k, _, v = tok.partition("=")
                if k == "saturation_threads":
                    sat = int(v)
            continue
        if line.startswith("n,"):
            continue
        parts = line.strip().split(",")
        if len(parts) < 5:
            continue
        n.append(int(parts[0]))
        gflops.append(float(parts[2]))
        frac.append(float(parts[4]))

n = np.array(n); gflops = np.array(gflops)
peak = gflops.max()

fig, ax = plt.subplots(figsize=(8, 6))
ax.plot(n, gflops, "o-", color="C0", label="FMA-Kernel")
ax.axhline(peak, color="gray", ls="--", lw=1, label=f"Plateau ≈ {peak:.0f} GFLOP/s")

# 90%-Warm-up-Schwelle markieren
warm_idx = np.argmax(gflops >= 0.9 * peak)
ax.axvline(n[warm_idx], color="red", ls=":", lw=1,
           label=f"90%-Warm-up bei n≈{n[warm_idx]:,}")

# Sättigungsschwelle (1 Thread pro n): Linie bei n = sat
if sat:
    ax.axvline(sat, color="green", ls="-.", lw=1,
               label=f"1× Belegung ({sat:,} Threads)")

ax.set_xscale("log", base=2)
ax.set_xlabel("Problemgröße n = Threads")
ax.set_ylabel("Durchsatz [GFLOP/s]")
ax.set_title("Skalierung über n – GPU-Warm-up (RTX 4060)")
ax.grid(True, which="both", ls=":", alpha=0.5)
ax.legend(loc="lower right", fontsize=8)
fig.tight_layout()
fig.savefig(out, dpi=130)
print(f"geschrieben: {out}")
