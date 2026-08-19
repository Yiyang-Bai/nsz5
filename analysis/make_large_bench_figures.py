#!/usr/bin/env python3
"""Publication figures + table for large-dataset file-level benchmarks.

Protocol matches the ex-zd paper (Gamaarachchi et al., Genome Res 2025):
slow5tools v1.3.0, 32 threads, sizes measured with ``du -b`` on BLOW5 /
POD5 / .nsz5 containers.  nsz5 numbers are from the latest Slurm jobs
(work/r1_chr22.*, work/chem_bench.*, work/rna004_decompress.*).

Three large datasets (the project's file-level benchmarks):
  1. HG002 Chr22      — R10.4.1 DNA,  PromethION, 32.3 B samples
  2. HEPG2 RNA002     — R9 RNA002,    MinION,       9.5 B samples
  3. RNA004 full      — RNA004,       PromethION,  69.5 B samples
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "work" / "figures" / "pub"
OUT.mkdir(parents=True, exist_ok=True)

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 8,
    "axes.titlesize": 8.5,
    "axes.labelsize": 8,
    "xtick.labelsize": 7.5,
    "ytick.labelsize": 7.5,
    "legend.fontsize": 7,
    "axes.linewidth": 0.8,
    "xtick.direction": "out",
    "ytick.direction": "out",
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "pdf.fonttype": 42,
})

C_NSZ = "#D55E00"
C_EXZD = "#E69F00"
C_VBZ = "#0072B2"
C_SVB = "#56B4E9"
C_UNC = "#999999"
PANEL = dict(fontsize=11, fontweight="bold", va="top", ha="left")

# (label, chemistry, samples, uncomp, zlib_svb, zstd_svb, zlib_exzd,
#  zstd_exzd, pod5_vbz, nsz5, compress_s, decompress_s)
BENCH = [
    (
        "HG002 Chr22",
        "R10.4.1 DNA",
        32_318_888_027,
        64_685_036_199,
        28_470_503_899,
        28_232_068_018,
        27_740_315_084,
        27_503_933_718,
        28_154_437_312,
        27_172_284_610,
        675,
        1106,
    ),
    (
        "HEPG2 RNA002",
        "R9 RNA002",
        9_496_828_513,
        19_010_223_375,
        8_854_979_474,
        8_819_830_537,
        8_650_652_030,
        8_626_837_006,
        8_781_248_616,
        8_188_388_046,
        192,
        135,
    ),
    (
        "RNA004 full",
        "RNA004",
        69_476_393_452,
        140_550_054_585,
        63_657_762_394,
        63_430_177_473,
        62_470_993_025,
        62_403_335_530,
        63_147_375_176,
        58_149_074_157,
        1899,
        892,
    ),
]

METHODS = [
    ("Uncompressed BLOW5", 3, C_UNC, False),
    ("zlib + svb-zd", 4, C_SVB, False),
    ("zstd + svb-zd", 5, C_SVB, False),
    ("zlib + ex-zd", 6, C_EXZD, False),
    ("zstd + ex-zd", 7, C_EXZD, True),
    ("POD5 (VBZ)", 8, C_VBZ, True),
    ("nsz5 (this work)", 9, C_NSZ, True),
]


def fmt_bytes(n):
    if n >= 1e12:
        return f"{n / 1e12:.2f} TB"
    if n >= 1e9:
        return f"{n / 1e9:.2f} GB"
    return f"{n / 1e6:.0f} MB"


def fmt_pct(num, den):
    return 100.0 * (1.0 - num / den)


def build_rows():
    rows = []
    for b in BENCH:
        name, chem, samples, *sizes, t_enc, t_dec = b
        uncomp, zsvb, zstd_svb, zexd, zstd_exzd, vbz, nsz = sizes
        row = {
            "dataset": name,
            "chemistry": chem,
            "samples_B": samples / 1e9,
            "uncomp": uncomp,
            "zlib_svb": zsvb,
            "zstd_svb": zstd_svb,
            "zlib_exzd": zexd,
            "zstd_exzd": zstd_exzd,
            "vbz": vbz,
            "nsz5": nsz,
            "ratio_uncomp_nsz": uncomp / nsz,
            "ratio_uncomp_exzd": uncomp / zstd_exzd,
            "ratio_uncomp_vbz": uncomp / vbz,
            "save_vs_vbz": fmt_pct(nsz, vbz),
            "save_vs_exzd": fmt_pct(nsz, zstd_exzd),
            "save_vs_svb": fmt_pct(nsz, zstd_svb),
            "enc_ms": samples / t_enc / 1e6,
            "dec_ms": samples / t_dec / 1e6,
        }
        rows.append(row)
    return rows


def write_table(rows):
    csv = OUT / "table_large_benchmark.csv"
    md = OUT / "table_large_benchmark.md"
    tex = OUT / "table_large_benchmark.tex"

    header = (
        "dataset,chemistry,samples_B,uncomp_GB,vbz_GB,zstd_exzd_GB,"
        "nsz5_GB,ratio_vs_uncomp_nsz,save_pct_vs_vbz,save_pct_vs_exzd,"
        "encode_Ms,decode_Ms\n"
    )
    lines = [header]
    md_lines = [
        "# Large-dataset compression benchmark\n",
        "Protocol: slow5tools v1.3.0 (32 threads) + nsz5 (32 threads). "
        "Sizes from `du -b`. Savings relative to POD5 (VBZ).\n",
        "| Dataset | Chemistry | Samples | Uncompressed | VBZ (POD5) | "
        "ex-zd zstd | nsz5 | nsz vs VBZ | nsz vs ex-zd | Encode | Decode |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    tex_lines = [
        r"\begin{table}[t]",
        r"\centering",
        r"\caption{File-level compression on three large nanopore datasets "
        r"(slow5tools v1.3.0 protocol; sizes in gigabytes). "
        r"Savings are relative to native POD5 (VBZ).}",
        r"\label{tab:large-bench}",
        r"\small",
        r"\begin{tabular}{llrrrrrrr}",
        r"\toprule",
        r"Dataset & Chemistry & Samples (B) & Uncomp. & VBZ & ex-zd & "
        r"nsz5 & vs VBZ & vs ex-zd \\",
        r"\midrule",
    ]

    for r in rows:
        lines.append(
            f"{r['dataset']},{r['chemistry']},{r['samples_B']:.2f},"
            f"{r['uncomp']/1e9:.3f},{r['vbz']/1e9:.3f},"
            f"{r['zstd_exzd']/1e9:.3f},{r['nsz5']/1e9:.3f},"
            f"{r['ratio_uncomp_nsz']:.3f},{r['save_vs_vbz']:.2f},"
            f"{r['save_vs_exzd']:.2f},{r['enc_ms']:.1f},{r['dec_ms']:.1f}\n"
        )
        md_lines.append(
            f"| {r['dataset']} | {r['chemistry']} | {r['samples_B']:.1f} B | "
            f"{r['uncomp']/1e9:.1f} GB | {r['vbz']/1e9:.1f} GB | "
            f"{r['zstd_exzd']/1e9:.1f} GB | {r['nsz5']/1e9:.1f} GB | "
            f"**{r['save_vs_vbz']:.1f}%** | {r['save_vs_exzd']:.1f}% | "
            f"{r['enc_ms']:.0f} Ms/s | {r['dec_ms']:.0f} Ms/s |"
        )
        tex_lines.append(
            f"{r['dataset']} & {r['chemistry']} & {r['samples_B']:.1f} & "
            f"{r['uncomp']/1e9:.1f} & {r['vbz']/1e9:.1f} & "
            f"{r['zstd_exzd']/1e9:.1f} & {r['nsz5']/1e9:.1f} & "
            f"{r['save_vs_vbz']:.1f}\\% & {r['save_vs_exzd']:.1f}\\% \\\\"
        )

    md_lines.extend([
        "",
        "## All methods (bytes)",
        "",
        "| Dataset | Uncomp | zlib svb-zd | zstd svb-zd | zlib ex-zd | "
        "zstd ex-zd | VBZ | nsz5 |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for r in rows:
        b = next(x for x in BENCH if x[0] == r["dataset"])
        md_lines.append(
            f"| {r['dataset']} | {fmt_bytes(b[3])} | {fmt_bytes(b[4])} | "
            f"{fmt_bytes(b[5])} | {fmt_bytes(b[6])} | {fmt_bytes(b[7])} | "
            f"{fmt_bytes(b[8])} | {fmt_bytes(b[9])} |"
        )

    tex_lines.extend([r"\bottomrule", r"\end{tabular}", r"\end{table}"])

    csv.write_text("".join(lines))
    md.write_text("\n".join(md_lines) + "\n")
    tex.write_text("\n".join(tex_lines) + "\n")
    return csv, md, tex


def fig_ratio_bars(rows):
    """Grouped bars: compression ratio (uncompressed / compressed)."""
    fig, axes = plt.subplots(1, 3, figsize=(7.2, 2.8),
                             gridspec_kw=dict(wspace=0.38, left=0.07,
                                              right=0.99, top=0.82,
                                              bottom=0.22))
    letters = "ABC"
    show_methods = [
        m for m in METHODS if m[0] not in ("Uncompressed BLOW5",)
    ]

    for ax, letter, row in zip(axes, letters, rows):
        ax.text(-0.22, 1.08, letter, transform=ax.transAxes, **PANEL)
        b = next(x for x in BENCH if x[0] == row["dataset"])
        names, ratios, cols = [], [], []
        for label, idx, color, _ in show_methods:
            size = b[idx]
            names.append(label.replace(" + ", "\n+ "))
            ratios.append(row["uncomp"] / size)
            cols.append(color)
        y = np.arange(len(names))
        bars = ax.barh(y, ratios, color=cols, height=0.62, zorder=3)
        for bar, val in zip(bars, ratios):
            ax.text(val + 0.02, bar.get_y() + bar.get_height() / 2,
                    f"{val:.2f}", va="center", fontsize=6)
        ax.set_yticks(y)
        ax.set_yticklabels(names, fontsize=6)
        ax.set_xlabel("ratio (uncomp. / compressed)")
        ax.set_title(f"{row['dataset']}\n{row['chemistry']}, "
                     f"{row['samples_B']:.1f} B samples", fontsize=7.5)
        ax.set_xlim(1.0, max(ratios) * 1.12)
        ax.xaxis.grid(True, lw=0.35, alpha=0.35, zorder=0)

    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_fig_large_ratio.{ext}", bbox_inches="tight")
    plt.close(fig)


def fig_savings_vs_vbz(rows):
    """Headline savings vs VBZ for the three SOTA methods."""
    fig, ax = plt.subplots(figsize=(5.2, 2.8))
    datasets = [r["dataset"] for r in rows]
    xs = np.arange(len(datasets))
    w = 0.24
    zstd_svb_save = []
    exzd_save = []
    nsz_save = []
    for r in rows:
        nsz_save.append(r["save_vs_vbz"])
        exzd_save.append(fmt_pct(r["zstd_exzd"], r["vbz"]))
        zstd_svb_save.append(fmt_pct(r["zstd_svb"], r["vbz"]))

    ax.bar(xs - w, zstd_svb_save, w, color=C_SVB, label="zstd + svb-zd")
    ax.bar(xs, exzd_save, w, color=C_EXZD, label="zstd + ex-zd")
    ax.bar(xs + w, nsz_save, w, color=C_NSZ, label="nsz5 (this work)")
    for i, v in enumerate(nsz_save):
        ax.text(i + w, v + 0.15, f"{v:.1f}%", ha="center", fontsize=7,
                color=C_NSZ, fontweight="bold")
    ax.set_xticks(xs)
    ax.set_xticklabels([f"{d}\n({r['chemistry']})"
                        for d, r in zip(datasets, rows)], fontsize=7)
    ax.set_ylabel("file size reduction vs POD5 (VBZ)")
    ax.set_ylim(0, max(nsz_save) * 1.25)
    ax.yaxis.grid(True, lw=0.35, alpha=0.35, zorder=0)
    ax.legend(frameon=False, loc="upper left", fontsize=7)
    ax.set_title("Compression gain over native POD5 (VBZ)", fontsize=8.5)
    fig.tight_layout()
    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_fig_large_savings.{ext}", bbox_inches="tight")
    plt.close(fig)


def fig_combined_overview(rows):
    """Single overview: nsz / ex-zd / VBZ ratio across all datasets."""
    fig, ax = plt.subplots(figsize=(5.0, 2.6))
    datasets = [f"{r['dataset']}\n({r['chemistry']})" for r in rows]
    xs = np.arange(len(datasets))
    w = 0.25
    vbz_r = [r["ratio_uncomp_vbz"] for r in rows]
    ex_r = [r["ratio_uncomp_exzd"] for r in rows]
    nsz_r = [r["ratio_uncomp_nsz"] for r in rows]
    ax.bar(xs - w, vbz_r, w, color=C_VBZ, label="POD5 (VBZ)")
    ax.bar(xs, ex_r, w, color=C_EXZD, label="zstd + ex-zd")
    ax.bar(xs + w, nsz_r, w, color=C_NSZ, label="nsz5")
    for x, v in zip(xs + w, nsz_r):
        ax.text(x, v + 0.03, f"{v:.2f}", ha="center", fontsize=6.5,
                color=C_NSZ, fontweight="bold")
    ax.set_xticks(xs)
    ax.set_xticklabels(datasets, fontsize=7)
    ax.set_ylabel("compression ratio\n(uncompressed BLOW5 / method)")
    ax.legend(frameon=False, ncol=3, loc="upper right", fontsize=6.5)
    ax.set_ylim(0, max(nsz_r) * 1.15)
    ax.yaxis.grid(True, lw=0.35, alpha=0.35, zorder=0)
    fig.tight_layout()
    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_fig_large_overview.{ext}", bbox_inches="tight")
    plt.close(fig)


def fig_table_image(rows):
    """Render the main comparison table as a figure for slides/papers."""
    headers = [
        "Dataset", "Chemistry", "Samples\n(B)",
        "Uncompressed", "POD5\n(VBZ)", "ex-zd\nzstd", "nsz5",
        "Δ vs VBZ", "Δ vs ex-zd", "Encode\n(Ms/s)", "Decode\n(Ms/s)",
    ]
    cell_text = []
    for r in rows:
        cell_text.append([
            r["dataset"],
            r["chemistry"],
            f"{r['samples_B']:.1f}",
            f"{r['uncomp']/1e9:.1f} GB",
            f"{r['vbz']/1e9:.1f} GB",
            f"{r['zstd_exzd']/1e9:.1f} GB",
            f"{r['nsz5']/1e9:.1f} GB",
            f"{r['save_vs_vbz']:.1f}%",
            f"{r['save_vs_exzd']:.1f}%",
            f"{r['enc_ms']:.0f}",
            f"{r['dec_ms']:.0f}",
        ])

    # column widths sum to 1.0; wider labels get more space
    col_widths = [
        0.13, 0.11, 0.06, 0.09, 0.08, 0.08, 0.08, 0.08, 0.08, 0.08, 0.08,
    ]
    left_cols = {0, 1}

    fig, ax = plt.subplots(figsize=(10.8, 2.05))
    ax.axis("off")
    tbl = ax.table(
        cellText=cell_text,
        colLabels=headers,
        loc="center",
        cellLoc="center",
        colWidths=col_widths,
        bbox=[0, 0, 1, 0.88],
    )
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(8.5)

    nrows = len(cell_text) + 1
    for (row, col), cell in tbl.get_celld().items():
        cell.set_edgecolor("#B0B0B0")
        cell.set_linewidth(0.6)
        cell.set_height(1.0 / (nrows + 0.6))
        txt = cell.get_text()
        txt.set_fontsize(8 if row > 0 else 8.5)
        if row == 0:
            cell.set_facecolor("#ECECEC")
            txt.set_fontweight("bold")
            txt.set_color("#222222")
        elif row % 2 == 0:
            cell.set_facecolor("#F8F8F8")
        else:
            cell.set_facecolor("#FFFFFF")
        if col in left_cols:
            txt.set_ha("left")
            txt.set_x(0.03)
        if col == 6 and row > 0:
            txt.set_color(C_NSZ)
            txt.set_fontweight("bold")

    ax.text(
        0.5, 0.97,
        "Large-dataset file-level compression (slow5tools v1.3.0 protocol; "
        "sizes in GB; savings relative to POD5 VBZ)",
        transform=ax.transAxes, ha="center", va="top", fontsize=9,
        fontweight="bold",
    )
    fig.subplots_adjust(left=0.02, right=0.98, top=0.92, bottom=0.02)
    for ext in ("png", "pdf"):
        fig.savefig(OUT / f"pub_table_large_benchmark.{ext}", bbox_inches="tight",
                    pad_inches=0.08)
    plt.close(fig)


def main():
    rows = build_rows()
    csv, md, tex = write_table(rows)
    fig_ratio_bars(rows)
    fig_savings_vs_vbz(rows)
    fig_combined_overview(rows)
    fig_table_image(rows)
    print("Wrote:")
    for p in sorted(OUT.glob("pub_*large*")) + [csv, md, tex]:
        print(f"  {p}")


if __name__ == "__main__":
    main()
