#!/usr/bin/env python3
"""Publication-style figures for the nsz benchmark results
(styled after Gamaarachchi et al., Genome Res 2025 — the ex-zd paper).

Outputs PNGs into work/figures/.
All numbers were measured in this project (analysis/benchmark.py,
python/nsz5.py, tools/bench_file.c) on identical inputs per dataset.
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

NSZ_COLOR = "#c0392b"
FLAC_COLOR = "#7f8c8d"
VBZ_COLOR = "#2c3e50"
GRAY = "#95a5a6"

# bits/sample measured on identical 20M-sample slices per chemistry
DATASETS = ["R9.4.1 / RNA002", "RNA004", "R10.4.1 DNA"]
METHODS = [
    ("nsz (this work)", NSZ_COLOR, [6.4321, 6.5702, 6.7291]),
    ("FLAC -8",         FLAC_COLOR, [6.5137, 6.6433, 6.8546]),
    ("VBZ (POD5)",      VBZ_COLOR, [6.7015, 6.9829, 6.9227]),
    ("bzip2 -9",        GRAY, [7.1462, 7.2564, 7.2464]),
    ("xz -9",           GRAY, [7.4267, 7.6350, 7.6345]),
    ("zstd -19",        GRAY, [8.4698, 8.4648, 8.3808]),
    ("zstd -1",         GRAY, [10.9883, 10.9347, 10.8815]),
]


def fig1_compression_ratio():
    """Fig 1 style: horizontal bars of compression ratio, one panel/dataset."""
    fig, axes = plt.subplots(1, 3, figsize=(11, 3.4), sharey=True)
    for ax, ds, di in zip(axes, DATASETS, range(3)):
        names = [m[0] for m in METHODS][::-1]
        colors = [m[1] for m in METHODS][::-1]
        ratios = [16.0 / m[2][di] for m in METHODS][::-1]
        bars = ax.barh(names, ratios, color=colors, height=0.65)
        for b, r in zip(bars, ratios):
            ax.text(b.get_width() + 0.02, b.get_y() + b.get_height() / 2,
                    f"{r:.3f}", va="center", fontsize=8)
        ax.set_title(ds, fontsize=10)
        ax.set_xlabel("compression ratio (raw / compressed)")
        ax.set_xlim(1.0, 2.75)
        ax.axvline(16.0 / [m[2][di] for m in METHODS if m[0].startswith("VBZ")][0],
                   color=VBZ_COLOR, lw=0.8, ls="--", alpha=0.6)
    axes[0].set_ylabel("")
    fig.suptitle("Lossless compression of identical nanopore signal "
                 "(20 M samples per dataset)", y=1.03, fontsize=11)
    fig.tight_layout()
    fig.savefig(OUT / "fig1_compression_ratio.png", bbox_inches="tight")
    plt.close(fig)


def fig2_relative_size():
    """Fig 2B style: file size relative to VBZ = 100%."""
    fig, ax = plt.subplots(figsize=(7.2, 3.6))
    width = 0.11
    xs = np.arange(3)
    for i, (name, color, bps) in enumerate(METHODS):
        rel = [100.0 * b / METHODS[2][2][j] for j, b in enumerate(bps)]
        ax.bar(xs + (i - 3) * width, rel, width * 0.92, label=name,
               color=color, alpha=1.0 if color != GRAY else 0.75)
    ax.axhline(100, color=VBZ_COLOR, lw=0.8, ls="--", alpha=0.7)
    ax.set_xticks(xs)
    ax.set_xticklabels(DATASETS)
    ax.set_ylabel("signal size relative to VBZ (%)")
    ax.set_ylim(90, 170)
    ax.legend(frameon=False, fontsize=8, ncol=2)
    ax.set_title("Relative signal payload size (VBZ = 100%)", fontsize=11)
    fig.tight_layout()
    fig.savefig(OUT / "fig2_relative_size.png", bbox_inches="tight")
    plt.close(fig)


def fig3_signal_distribution():
    """Fig 2A style: frequency distribution of raw signal values."""
    files = [
        ("R9.4.1 / RNA002", "tests/data/real_signal.bin"),
        ("RNA004", "data/rna004_signal.bin"),
        ("R10.4.1 DNA (5 kHz)", "data/r10_signal.bin"),
    ]
    root = Path(__file__).resolve().parent.parent
    fig, axes = plt.subplots(1, 3, figsize=(11, 3.0))
    for ax, (name, path) in zip(axes, files):
        x = np.fromfile(root / path, dtype="<i2", count=20_000_000)
        lo, hi = np.percentile(x, [0.1, 99.9])
        vals, counts = np.unique(x[(x >= lo) & (x <= hi)], return_counts=True)
        ax.fill_between(vals, counts, color=NSZ_COLOR, lw=0.3, alpha=0.85)
        ax.set_title(name, fontsize=10)
        ax.set_xlabel("signal value (ADC units)")
        ax.set_yticks([])
    axes[0].set_ylabel("frequency")
    fig.suptitle("Raw signal value distributions (20 M samples each)",
                 y=1.04, fontsize=11)
    fig.tight_layout()
    fig.savefig(OUT / "fig3_signal_distribution.png", bbox_inches="tight")
    plt.close(fig)


def fig4_milestones():
    """Project narrative: bits/sample at each development stage (R9 data)."""
    stages = [
        ("raw\nint16", 16.0),
        ("M0\ndelta+varint", 8.23),
        ("M1\nLPC8+varint", 8.21),
        ("M2\nrange coder", 6.484),
        ("M3\ncontexts+prior", 6.451),
    ]
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    xs = np.arange(len(stages))
    vals = [s[1] for s in stages]
    colors = [GRAY] * (len(stages) - 1) + [NSZ_COLOR]
    bars = ax.bar(xs, vals, 0.62, color=colors)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.15, f"{v:.2f}",
                ha="center", fontsize=9)
    ax.axhline(6.7015, color=VBZ_COLOR, lw=1.0, ls="--")
    ax.text(-0.42, 7.15, "VBZ 6.70", color=VBZ_COLOR, fontsize=9, ha="left")
    ax.axhline(6.46, color="#27ae60", lw=1.0, ls=":")
    ax.text(-0.42, 5.55, "LPC residual entropy 6.46", color="#27ae60",
            fontsize=9, ha="left")
    ax.set_xticks(xs)
    ax.set_xticklabels([s[0] for s in stages], fontsize=8.5)
    ax.set_ylabel("bits / sample")
    ax.set_ylim(0, 17)
    ax.set_title("nsz development: bits/sample per milestone "
                 "(108 M real samples, R9 directRNA)", fontsize=10.5)
    fig.tight_layout()
    fig.savefig(OUT / "fig4_milestones.png", bbox_inches="tight")
    plt.close(fig)


def fig5_file_level():
    """Whole-file archival results incl. Dorado verification note."""
    files = [
        ("SG-NEx R9 directRNA\n(92 MB, blue-crab)", 4.22, "0 / 7,897"),
        ("ONT RNA004 control\n(364 MB, native MinKNOW)", 7.79, "0 / 60,053"),
        ("ONT GIAB HG002 R10 DNA\n(1.88 GB, native MinKNOW)", 2.83, "49 / 26,300"),
    ]
    fig, ax = plt.subplots(figsize=(6.8, 3.4))
    xs = np.arange(len(files))
    vals = [f[1] for f in files]
    bars = ax.bar(xs, vals, 0.55, color=NSZ_COLOR, alpha=0.9)
    for b, (name, v, fb) in zip(bars, files):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.12,
                f"−{v:.2f}%\nfallbacks: {fb}", ha="center", fontsize=8.5)
    ax.set_xticks(xs)
    ax.set_xticklabels([f[0] for f in files], fontsize=8.5)
    ax.set_ylabel("whole-file size reduction (%)")
    ax.set_ylim(0, 9.5)
    ax.set_title("POD5 → .nsz5 → POD5: whole-file results\n"
                 "(all SHA-256 identical; Dorado basecalls bit-identical)",
                 fontsize=10.5)
    fig.tight_layout()
    fig.savefig(OUT / "fig5_file_level.png", bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    fig1_compression_ratio()
    fig2_relative_size()
    fig3_signal_distribution()
    fig4_milestones()
    fig5_file_level()
    for p in sorted(OUT.glob("*.png")):
        print(p)
