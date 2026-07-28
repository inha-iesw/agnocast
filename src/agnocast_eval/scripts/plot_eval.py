#!/usr/bin/env python3
"""Plot the Agnocast vs standard ROS 2 memory evaluation from eval/out/results.csv.

Two panels: payload physical memory (summed PSS) versus subscriber count (fixed 16 MiB
payload) and versus payload size (single subscriber). Agnocast stays at one shared copy;
standard ROS 2 scales with the number of copies (DDS history caches + application copies).

Colors are the Okabe-Ito colorblind-safe pair, reinforced by distinct markers and line
styles so the two series never rely on color alone.
"""

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT_DIR = Path(__file__).resolve().parent / "out"
CSV = OUT_DIR / "results.csv"
OUT = OUT_DIR / "zero_copy_memory.png"

# Okabe-Ito CVD-safe pair + redundant marker/linestyle encoding.
STYLE = {
    "agnocast": dict(color="#0072B2", marker="o", linestyle="-", label="Agnocast (zero-copy)"),
    "std": dict(color="#D55E00", marker="s", linestyle="--", label="Standard ROS 2 (copy)"),
}
INK, MUTED, GRID = "#222222", "#555555", "#DDDDDD"


def load():
    data = {"subscribers": {"std": [], "agnocast": []}, "size": {"std": [], "agnocast": []}}
    with open(CSV) as f:
        for row in csv.DictReader(f):
            data[row["sweep"]][row["mode"]].append(
                (float(row["x"]), float(row["total_payload_pss_mib"]))
            )
    for sweep in data.values():
        for series in sweep.values():
            series.sort()
    return data


def draw(ax, series_by_mode, xlabel, title, subtitle, payload_fixed):
    # Cross-check: effective copies = measured PSS / logical message size. The logical
    # size is the fixed payload (subscriber sweep) or the x value itself (size sweep).
    for mode in ("std", "agnocast"):  # draw std first so Agnocast sits on top
        pts = series_by_mode[mode]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ax.plot(xs, ys, linewidth=2, markersize=7, zorder=3,
                markeredgecolor="white", markeredgewidth=1.2, **STYLE[mode])
        denom = payload_fixed if payload_fixed else xs[-1]
        ax.annotate(f"{ys[-1]:.0f} MiB  ({ys[-1] / denom:.0f}×)", (xs[-1], ys[-1]),
                    textcoords="offset points", xytext=(6, 4), fontsize=9,
                    color=STYLE[mode]["color"], fontweight="bold")

    ax.set_title(title, fontsize=12, fontweight="bold", color=INK, pad=24)
    ax.text(0.0, 1.03, subtitle, transform=ax.transAxes, fontsize=9, color=MUTED)
    ax.set_xlabel(xlabel, fontsize=10, color=INK)
    ax.set_ylabel("Payload physical memory  (MiB, summed PSS)", fontsize=10, color=INK)
    ax.grid(True, color=GRID, linewidth=0.6, zorder=0)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color(GRID)
    ax.tick_params(colors=MUTED, labelsize=9)
    ax.set_ylim(bottom=0)
    ax.legend(frameon=False, fontsize=9, loc="upper left")


def main():
    data = load()
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5.4))
    fig.suptitle("Zero-copy memory efficiency: Agnocast vs standard ROS 2",
                 fontsize=14, fontweight="bold", color=INK, x=0.5, y=0.98)

    draw(ax1, data["subscribers"], "Subscribers", "Scaling with subscriber count",
         "16 MiB message — each subscriber holds it", payload_fixed=16)
    ax1.set_xticks([1, 2, 4, 8])

    draw(ax2, data["size"], "Message size  (MiB)", "Scaling with message size",
         "1 subscriber holding the message", payload_fixed=None)
    ax2.set_xticks([1, 4, 16, 32, 64])

    fig.text(0.5, 0.005,
             "(N×) = measured PSS / message size = effective physical copies. Agnocast stays "
             "1× (one shared copy); standard ROS 2 grows a private copy per participant.",
             ha="center", fontsize=8.5, color=MUTED)
    fig.tight_layout(rect=[0, 0.04, 1, 0.93])
    fig.savefig(OUT, dpi=150, facecolor="white")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
