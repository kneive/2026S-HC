#!/usr/bin/env python3
"""Zugriffsmuster-Plot aus `./bench sweeppat patterns.csv`.
Usage: python3 plot_patterns.py patterns.csv [out.png]"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "patterns.csv"
out  = sys.argv[2] if len(sys.argv) > 2 else "patterns.png"

peak = None
strides, gbs = [], []
gather = None
with open(path) as fh:
    for line in fh:
        if line.startswith("#"):
            for tok in line[1:].split():
                k, _, v = tok.partition("=")
                if k == "peak_gbs":
                    peak = float(v)
            continue
        if line.startswith("pattern"):
            continue
        pat, stride, g, pct = line.strip().split(",")
        if pat == "gather":
            gather = float(g)
        else:
            strides.append(int(stride)); gbs.append(float(g))

strides = np.array(strides); gbs = np.array(gbs)

fig, ax = plt.subplots(figsize=(8, 6))
ax.plot(strides, gbs, "o-", color="C0", label="coalesced/strided")
if peak:
    ax.axhline(peak, color="gray", ls="--", lw=1, label=f"Peak {peak:.0f} GB/s")
if gather is not None:
    ax.axhline(gather, color="C3", ls="-.", lw=1.5,
               label=f"random gather ({gather:.0f} GB/s)")

ax.annotate("Stride 1\n= coalesced", (strides[0], gbs[0]),
            textcoords="offset points", xytext=(10, -5), fontsize=8)
ax.set_xscale("log", base=2)
ax.set_xlabel("Stride k")
ax.set_ylabel("Effektive Bandbreite [GB/s]")
ax.set_title("Zugriffsmuster vs. erreichte Bandbreite (RTX 4060)")
ax.grid(True, which="both", ls=":", alpha=0.5)
ax.legend(loc="upper right", fontsize=8)
fig.tight_layout()
fig.savefig(out, dpi=130)
print(f"geschrieben: {out}")
