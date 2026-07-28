#!/usr/bin/env python3
"""Print the reproduction verdict from the burst node's markers and the smaps CSV.

Reads baseline / peak / drained Rss around the node's `MARKER` phase lines and reports
whether freed physical pages stayed resident (leak reproduced) or dropped back toward
baseline (reclaim working).

Usage: summarize_repro.py <node_log> <smaps_csv>
"""

import sys


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: summarize_repro.py <node_log> <smaps_csv>")
    node_log, csv = sys.argv[1], sys.argv[2]

    markers = {}
    for line in open(node_log):
        p = line.split()
        if len(p) == 3 and p[0] == "MARKER":
            markers[p[1]] = int(p[2])

    rows = []
    for line in open(csv).read().splitlines()[1:]:
        ms, rss, pss = line.split(",")
        rows.append((int(ms), int(rss), int(pss)))

    if not rows or "peak_begin" not in markers:
        sys.exit(f"insufficient data — check {csv} and {node_log}")

    def last_before(t):
        vals = [r[1] for r in rows if r[0] <= t]
        return vals[-1] if vals else rows[0][1]

    def max_between(a, b):
        vals = [r[1] for r in rows if a <= r[0] <= b]
        return max(vals) if vals else 0

    def last_between(a, b):
        vals = [r[1] for r in rows if a <= r[0] <= b]
        return vals[-1] if vals else rows[-1][1]

    baseline = last_before(markers["peak_begin"])
    peak = max_between(markers.get("peak_hold", 0), markers.get("drain_begin", rows[-1][0]))
    drained = last_between(markers.get("drained_hold", rows[-1][0]), markers.get("done", rows[-1][0]))

    def mib(kb):
        return kb / 1024.0

    print(f"  baseline Rss : {baseline:>10} kB ({mib(baseline):7.1f} MiB)")
    print(f"  peak     Rss : {peak:>10} kB ({mib(peak):7.1f} MiB)")
    print(f"  drained  Rss : {drained:>10} kB ({mib(drained):7.1f} MiB)  <- after freeing every message")

    grown = peak - baseline
    if grown <= 0:
        print("\n  INCONCLUSIVE: peak did not exceed baseline (increase REPRO_NUM_MSGS/REPRO_BLOCK_BYTES).")
        return

    retained = (drained - baseline) / grown
    if retained > 0.5:
        print(f"\n  >>> LEAK REPRODUCED: {retained * 100:.0f}% of the peak stayed resident after free")
        print("      (freed shared-memory pages were not returned to the kernel — no MADV_REMOVE).")
    else:
        print(f"\n  >>> pages reclaimed: only {retained * 100:.0f}% stayed resident after free")
        print("      (physical memory dropped back toward baseline — reclaim is working).")


if __name__ == "__main__":
    main()
