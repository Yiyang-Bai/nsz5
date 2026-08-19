#!/usr/bin/env python3
"""Figures for the head-to-head reproduction of the ex-zd paper's Fig 1
(Gamaarachchi et al., Genome Res 2025) on the SAME dataset:
HG002-Prom5K Chr22 subset (flowcell PGXXSX240041, 32.3 B samples).

All sizes measured by this project's Slurm job (work/sigcomp_bench.*.out)
with slow5tools v1.3.0, identical to the paper's protocol.
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

OUT = Path(__file__).resolve().parent.parent / "work" / "figures"
OUT.mkdir(parents=True, exist_ok=True)

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 10,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.dpi": 150,
})

NSZ = "#c0392b"
EXZD = "#e67e22"
VBZ = "#2c3e50"
GRAY = "#95a5a6"

UNCOMP = 64_685_036_199
# (name, bytes, encode_seconds, color)
ROWS = [
    ("nsz5 (this work)",            27_339_081_585, 5071, NSZ),
    ("ex-zd (zstd)  [paper's best]", 27_503_933_718,   99, EXZD),
    ("ex-zd (zlib)",                 27_740_315_084,  121, EXZD),
    ("POD5 native (VBZ)",            28_154_437_312,  410, VBZ),
    ("svb-zd (zstd)",                28_232_068_018,  106, GRAY),
    ("svb-zd (zlib)  [BLOW5 default]", 28_470_503_899, 111, GRAY),
    ("uncompressed BLOW5",           64_685_036_199,  291, "#d5d8dc"),
]


def fig6_paper_head_to_head():
    fig, ax = plt.subplots(figsize=(7.6, 3.8))
    rows = ROWS[::-1]
    names = [r[0] for r in rows]
    ratios = [UNCOMP / r[1] for r in rows]
    colors = [r[3] for r in rows]
    bars = ax.barh(names, ratios, color=colors, height=0.62)
    for b, r in zip(bars, ratios):
        ax.text(b.get_width() + 0.015, b.get_y() + b.get_height() / 2,
                f"{r:.3f}", va="center", fontsize=9)
    ax.axvline(2.352, color=EXZD, lw=0.9, ls="--", alpha=0.7)
    ax.text(2.352, len(rows) - 0.28, " published best (2.35)",
            color=EXZD, fontsize=8, va="top")
    ax.set_xlim(0.95, 2.62)
    ax.set_xlabel("compression ratio (uncompressed / compressed)")
    ax.set_title(
        "Head-to-head on the ex-zd paper's own benchmark dataset\n"
        "HG002-Prom5K Chr22 subset (PGXXSX240041, 32.3 B samples), "
        "slow5tools v1.3.0 protocol", fontsize=10)
    fig.tight_layout()
    fig.savefig(OUT / "fig6_paper_head_to_head.png", bbox_inches="tight")
    plt.close(fig)


def fig7_ratio_vs_time():
    fig, ax = plt.subplots(figsize=(6.8, 4.0))
    n_samples = 32_318_888_027
    for name, size, secs, color in ROWS[:-1]:
        ratio = UNCOMP / size
        speed = n_samples / secs / 1e6
        ax.scatter(speed, ratio, s=70, color=color, zorder=3)
        offset = (0, 7) if "nsz5" not in name else (0, -14)
        ax.annotate(name.split("  [")[0], (speed, ratio),
                    textcoords="offset points", xytext=offset,
                    ha="center", fontsize=8.5, color=color)
    ax.set_xscale("log")
    ax.set_xlabel("encode throughput (Msamples/s, log scale)")
    ax.set_ylabel("compression ratio")
    ax.set_title("Compression ratio vs speed — the Pareto tradeoff\n"
                 "(same dataset; slow5tools methods use 32 threads, "
                 "nsz5 driver is still single-threaded)", fontsize=10)
    ax.grid(alpha=0.25, which="both")
    fig.tight_layout()
    fig.savefig(OUT / "fig7_ratio_vs_time.png", bbox_inches="tight")
    plt.close(fig)


def fig8_four_datasets():
    """Compression ratio of VBZ, nsz and ex-zd on every dataset tested.
    First three datasets: 20M-sample signal payloads (ratio = 16/bits per
    sample); chr22: whole-file du measurement from the Slurm job."""
    datasets = [
        "R9.4.1/RNA002\n(SG-NEx, 20M)",
        "RNA004\n(ONT native, 20M)",
        "R10.4.1 DNA\n(GIAB, 20M)",
        "R10 DNA Chr22\n(ex-zd paper, 32.3B)",
    ]
    vbz = [2.388, 2.291, 2.311, 2.298]
    nsz = [2.488, 2.435, 2.378, 2.366]
    exzd = [None, None, None, 2.352]  # measured here on chr22

    fig, ax = plt.subplots(figsize=(7.6, 3.9))
    xs = np.arange(len(datasets))
    w = 0.26
    ax.bar(xs - w, vbz, w, color=VBZ, label="VBZ (POD5 default)")
    ax.bar(xs, nsz, w, color=NSZ, label="nsz (this work)")
    for x, v in zip(xs, vbz):
        ax.text(x - w, v + 0.005, f"{v:.3f}", ha="center", fontsize=8)
    for x, v in zip(xs, nsz):
        ax.text(x, v + 0.005, f"{v:.3f}", ha="center", fontsize=8,
                color=NSZ, fontweight="bold")
    ax.bar(xs[3] + w, exzd[3], w, color=EXZD, label="ex-zd (paper's method)")
    ax.text(xs[3] + w, exzd[3] + 0.005, f"{exzd[3]:.3f}", ha="center",
            fontsize=8, color=EXZD)
    ax.set_xticks(xs)
    ax.set_xticklabels(datasets, fontsize=8.5)
    ax.set_ylabel("compression ratio")
    ax.set_ylim(2.2, 2.56)
    ax.legend(frameon=False, fontsize=9, loc="upper right")
    ax.set_title("VBZ vs nsz vs ex-zd across all four datasets "
                 "(higher = better; y-axis zoomed)", fontsize=10.5)
    fig.tight_layout()
    fig.savefig(OUT / "fig8_four_datasets.png", bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    fig6_paper_head_to_head()
    fig7_ratio_vs_time()
    fig8_four_datasets()
    for p in ["fig6_paper_head_to_head.png", "fig7_ratio_vs_time.png",
              "fig8_four_datasets.png"]:
        print(OUT / p)
