#!/usr/bin/env python3
"""Plot Monte-Carlo pi sweep CSV."""
import csv
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "outputs/montecarlo.csv"
out = sys.argv[2] if len(sys.argv) > 2 else "outputs/montecarlo_scaling.png"

rows = []
with open(path, newline="") as fh:
    for line in fh:
        if line.startswith("#"):
            continue
        rows.append(line)

data = list(csv.DictReader(rows))
samples = np.array([int(r["samples_per_thread"]) for r in data])
gsamples = np.array([float(r["gsamples_s"]) for r in data])
error = np.array([float(r["error"]) for r in data])

fig, ax1 = plt.subplots(figsize=(8, 5.2))
ax1.plot(samples, gsamples, "o-", color="C0", label="Durchsatz")
ax1.set_xscale("log", base=2)
ax1.set_xlabel("Samples pro Thread")
ax1.set_ylabel("Durchsatz [Gsamples/s]", color="C0")
ax1.tick_params(axis="y", labelcolor="C0")
ax1.grid(True, which="both", ls=":", alpha=0.45)

ax2 = ax1.twinx()
ax2.plot(samples, error, "s--", color="C3", label="Fehler |pi-pi_hat|")
ax2.set_yscale("log")
ax2.set_ylabel("Fehler der Pi-Schaetzung", color="C3")
ax2.tick_params(axis="y", labelcolor="C3")

ax1.set_title("Monte-Carlo-Pi: Durchsatz und Schaetzfehler")
lines = ax1.get_lines() + ax2.get_lines()
labels = [line.get_label() for line in lines]
ax1.legend(lines, labels, loc="best", fontsize=8)
fig.tight_layout()
fig.savefig(out, dpi=140)
print(f"geschrieben: {out}")
