#!/usr/bin/env python3
"""Publication-quality figures for the nsz manuscript.

Conventions: Okabe-Ito colorblind-safe palette, panel letters, direct
labeling instead of legend boxes where possible, journal column widths
(3.5 in single / 7.2 in double), 300-dpi PNG + vector PDF output.

All numbers are this project's measurements; constants below are the
single source of truth and can be refreshed from benchmark output.
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

OUT = Path(__file__).resolve().parent.parent / "work" / "figures" / "pub"
OUT.mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------------- style
plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 8,
    "axes.titlesize": 8.5,
    "axes.labelsize": 8,
    "xtick.labelsize": 7.5,
    "ytick.labelsize": 7.5,
    "legend.fontsize": 7.5,
    "axes.linewidth": 0.8,
    "xtick.direction": "out",
    "ytick.direction": "out",
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "pdf.fonttype": 42,
})

# Okabe-Ito palette
C_NSZ = "#D55E00"    # vermillion — ours
C_EXZD = "#E69F00"   # orange — published SOTA
C_VBZ = "#0072B2"    # blue — POD5 default
C_FLAC = "#009E73"   # green
C_GRAY = "#8C8C8C"
C_LIGHT = "#C8C8C8"

PANEL_KW = dict(fontsize=11, fontweight="bold", va="top", ha="left")

# ------------------------------------------------------------- data
DATASETS = ["R9.4.1 RNA002", "RNA004", "R10.4.1 DNA"]
# bits/sample on identical signal slices per chemistry
BPS = {  # name: (values per dataset, color)
    "nsz (this work)": ([6.3961, 6.5334, 6.6872], C_NSZ),
    "FLAC -8":         ([6.5137, 6.6433, 6.8546], C_FLAC),
    "VBZ (POD5)":      ([6.7015, 6.9829, 6.9227], C_VBZ),
    "bzip2 -9":        ([7.1462, 7.2564, 7.2464], C_GRAY),
    "xz -9":           ([7.4267, 7.6350, 7.6345], C_GRAY),
    "zstd -19":        ([8.4698, 8.4648, 8.3808], C_LIGHT),
}

# chr22 head-to-head (du bytes; uncompressed baseline 64,685,036,199)
CHR22 = [
    ("nsz5 (this work)", 2.381, C_NSZ),
    ("ex-zd zstd", 2.352, C_EXZD),
    ("ex-zd zlib", 2.332, C_EXZD),
    ("POD5 (VBZ)", 2.298, C_VBZ),
    ("zstd svb-zd", 2.291, C_GRAY),
    ("zlib svb-zd", 2.272, C_GRAY),
]
CHR22_PUBLISHED_BEST = 2.35

# ratio vs encode speed (chr22, 32.3 B samples)
PARETO = [
    ("nsz", 2.366, 73.4, C_NSZ),       # 32-thread parallel driver
    ("ex-zd zstd", 2.352, 326, C_EXZD),
    ("ex-zd zlib", 2.332, 267, C_EXZD),
    ("VBZ (POD5)", 2.298, 79, C_VBZ),  # blue-crab conversion rate
    ("zstd svb-zd", 2.291, 305, C_GRAY),
    ("zlib svb-zd", 2.272, 291, C_GRAY),
]

MILESTONES = [
    ("raw\nint16", 16.0),
    ("+ delta\npredictor", 8.23),
    ("+ order-8\nLPC", 8.21),
    ("+ range coder\n+ magnitude", 6.484),
    ("+ contexts\n+ prior", 6.451),
    ("+ count-adaptive\nrates", 6.415),
]
VBZ_R9 = 6.70

SIG_FILES = [
    ("R9.4.1 RNA002", "tests/data/real_signal.bin"),
    ("RNA004", "data/rna004_signal.bin"),
    ("R10.4.1 DNA", "data/r10_signal.bin"),
]


def panel_letter(ax, letter, dx=-0.12):
    ax.text(dx, 1.06, letter, transform=ax.transAxes, **PANEL_KW)


# ---------------------------------------------------------- figure 1
def fig1_main():
    fig = plt.figure(figsize=(7.2, 2.6))
    gs = fig.add_gridspec(1, 3, width_ratios=[1.25, 1.15, 1.0],
                          wspace=0.42, left=0.07, right=0.99,
                          top=0.86, bottom=0.17)

    # (A) compression ratio per chemistry
    ax = fig.add_subplot(gs[0])
    panel_letter(ax, "A")
    n_m = len(BPS)
    xs = np.arange(len(DATASETS))
    w = 0.8 / n_m
    for i, (name, (vals, color)) in enumerate(BPS.items()):
        ratios = [16.0 / v for v in vals]
        bars = ax.bar(xs + (i - n_m / 2 + 0.5) * w, ratios, w * 0.9,
                      color=color, label=name,
                      zorder=3, edgecolor="none")
        if name.startswith("nsz"):
            for b, r in zip(bars, ratios):
                ax.text(b.get_x() + b.get_width() / 2, r + 0.02,
                        f"{r:.2f}", ha="center", fontsize=6.5,
                        color=C_NSZ, fontweight="bold")
    ax.set_xticks(xs)
    ax.set_xticklabels(DATASETS, fontsize=7)
    ax.set_ylabel("compression ratio")
    ax.set_ylim(1.8, 2.62)
    ax.yaxis.grid(True, lw=0.4, alpha=0.35, zorder=0)
    ax.legend(frameon=False, ncol=2, loc="lower left", fontsize=6.5,
              bbox_to_anchor=(-0.02, 0.99), handlelength=0.8,
              columnspacing=0.7, borderaxespad=0.0, handletextpad=0.35,
              labelspacing=0.25)

    # (B) chr22 head-to-head
    ax = fig.add_subplot(gs[1])
    panel_letter(ax, "B", dx=-0.32)
    names = [r[0] for r in CHR22][::-1]
    vals = [r[1] for r in CHR22][::-1]
    cols = [r[2] for r in CHR22][::-1]
    bars = ax.barh(names, vals, color=cols, height=0.62, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(v + 0.002, b.get_y() + b.get_height() / 2, f"{v:.3f}",
                va="center", fontsize=6.5)
    ax.axvline(CHR22_PUBLISHED_BEST, color=C_EXZD, lw=0.8, ls=(0, (4, 2)))
    ax.text(CHR22_PUBLISHED_BEST - 0.003, 0.35, "published best",
            color=C_EXZD, fontsize=6, ha="right", va="center")
    ax.set_xlim(2.25, 2.41)
    ax.set_xlabel("compression ratio")
    ax.set_title("HG002 Chr22 (32.3 B samples)", fontsize=7.5)
    ax.xaxis.grid(True, lw=0.4, alpha=0.35, zorder=0)

    # (C) ratio vs speed
    ax = fig.add_subplot(gs[2])
    panel_letter(ax, "C", dx=-0.3)
    for name, ratio, speed, color in PARETO:
        ax.scatter(speed, ratio, s=26, color=color, zorder=3)
        va, dy = ("bottom", 3) if "nsz" not in name else ("top", -4)
        if name == "VBZ (POD5)":
            va, dy = "top", -4
        ax.annotate(name, (speed, ratio), textcoords="offset points",
                    xytext=(0, dy), ha="center", va=va, fontsize=6,
                    color=color)
    ax.set_xscale("log")
    ax.set_xlim(40, 700)
    ax.set_ylim(2.25, 2.395)
    ax.set_xticks([50, 100, 200, 400])
    ax.set_xticklabels(["50", "100", "200", "400"])
    ax.minorticks_off()
    ax.set_xlabel("encode throughput (Msamples/s)")
    ax.set_ylabel("compression ratio")
    ax.grid(True, which="major", lw=0.4, alpha=0.3, zorder=0)

    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_fig1_benchmark.{ext}", bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------- figure 2
def fig2_signal():
    root = Path(__file__).resolve().parent.parent
    fig, axes = plt.subplots(1, 4, figsize=(7.2, 1.9),
                             gridspec_kw=dict(wspace=0.25, left=0.05,
                                              right=0.99, top=0.82,
                                              bottom=0.24))
    letters = ["A", "B", "C"]
    for k, (ax, (name, path)) in enumerate(zip(axes[:3], SIG_FILES)):
        x = np.fromfile(root / path, dtype="<i2", count=20_000_000)
        lo, hi = np.percentile(x, [0.5, 99.5])
        vals, counts = np.unique(x[(x >= lo) & (x <= hi)],
                                 return_counts=True)
        ax.fill_between(vals, counts / 1e3, color=C_VBZ, lw=0, alpha=0.9)
        ax.set_title(name, fontsize=7.5)
        ax.set_xlabel("signal (ADC units)", labelpad=1)
        ax.set_yticks([])
        panel_letter(ax, letters[k], dx=-0.08)
        if k == 0:
            ax.set_ylabel("frequency")

    # (D) zoom on the ADC artifact (RNA004)
    ax = axes[3]
    panel_letter(ax, "D", dx=-0.08)
    x = np.fromfile(root / SIG_FILES[1][1], dtype="<i2", count=20_000_000)
    sel = (x >= 700) & (x <= 760)
    vals, counts = np.unique(x[sel], return_counts=True)
    mod4 = vals % 4
    colors = np.where(mod4 == 3, C_NSZ, C_LIGHT)
    ax.bar(vals, counts / 1e3, width=1.0, color=colors, lw=0)
    ax.set_title("ADC artifact (zoom)", fontsize=7.5)
    ax.set_xlabel("signal (ADC units)", labelpad=1)
    ax.set_yticks([])
    ax.text(0.97, 0.95, "values ending in binary 11\nover-represented",
            transform=ax.transAxes, fontsize=6, color=C_NSZ,
            ha="right", va="top")

    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_fig2_signal.{ext}", bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------- figure 3
def fig3_ablation():
    fig, ax = plt.subplots(figsize=(4.6, 2.6))
    xs = np.arange(len(MILESTONES))
    vals = [m[1] for m in MILESTONES]
    colors = [C_LIGHT] * (len(MILESTONES) - 1) + [C_NSZ]
    bars = ax.bar(xs, vals, 0.62, color=colors, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.25, f"{v:.2f}",
                ha="center", fontsize=6.5)
    ax.axhline(VBZ_R9, color=C_VBZ, lw=0.9, ls=(0, (4, 2)), zorder=2)
    ax.text(len(MILESTONES) - 0.55, 8.6, f"VBZ ({VBZ_R9:.2f})",
            color=C_VBZ, fontsize=6.5, ha="right")
    ax.set_xticks(xs)
    ax.set_xticklabels([m[0].replace("\n", " ") for m in MILESTONES],
                       fontsize=6, rotation=20, ha="right")
    ax.set_ylabel("bits / sample")
    ax.set_ylim(0, 17.4)
    ax.yaxis.grid(True, lw=0.4, alpha=0.35, zorder=0)
    ax.set_title("Cumulative design contributions "
                 "(108 M samples, R9 directRNA)", fontsize=7.5)
    fig.tight_layout()
    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_fig3_ablation.{ext}", bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    fig1_main()
    fig2_signal()
    fig3_ablation()
    for p in sorted(OUT.glob("*.png")):
        print(p)
