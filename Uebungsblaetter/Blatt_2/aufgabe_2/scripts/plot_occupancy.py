#!/usr/bin/env python3
"""Occupancy-Plot aus `./bench sweepocc occupancy.csv`.
Usage: python3 plot_occupancy.py occupancy.csv [out.png]"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "occupancy.csv"
out  = sys.argv[2] if len(sys.argv) > 2 else "occupancy.png"

peak = None
block, occ, gbs = [], [], []
with open(path) as fh:
    for line in fh:
        if line.startswith("#"):
            for tok in line[1:].split():
                k, _, v = tok.partition("=")
                if k == "peak_gbs":
                    peak = float(v)
            continue
        if line.startswith("blocksize"):
            continue
        b, o, g, pct = line.strip().split(",")
        block.append(int(b)); occ.append(float(o)); gbs.append(float(g))

block = np.array(block); occ = np.array(occ); gbs = np.array(gbs)
x = np.arange(len(block))

fig, ax1 = plt.subplots(figsize=(8, 6))
ax1.bar(x, gbs, color="C0", alpha=0.7, label="Bandbreite")
ax1.set_xticks(x); ax1.set_xticklabels([str(b) for b in block])
ax1.set_xlabel("Blockgröße (Threads/Block)")
ax1.set_ylabel("Effektive Bandbreite [GB/s]", color="C0")
if peak:
    ax1.axhline(peak, color="gray", ls="--", lw=1, label=f"Peak {peak:.0f} GB/s")

ax2 = ax1.twinx()
ax2.plot(x, occ, "o-", color="C3", label="Occupancy")
ax2.set_ylabel("Occupancy (aktive / max. Warps)", color="C3")
ax2.set_ylim(0, 1.05)

ax1.set_title("Occupancy vs. Bandbreite – Latenz-Verstecken (RTX 4060)")
ax1.grid(True, axis="y", ls=":", alpha=0.5)
fig.legend(loc="lower center", ncol=3, fontsize=8, bbox_to_anchor=(0.5, -0.02))
fig.tight_layout()
fig.savefig(out, dpi=130, bbox_inches="tight")
print(f"geschrieben: {out}")
