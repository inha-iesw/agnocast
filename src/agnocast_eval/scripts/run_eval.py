#!/usr/bin/env python3
"""Sweep driver: measure Agnocast vs standard ROS 2 payload memory, write results.csv.

Defines the experiment only — the measurement mechanics live in harness.py. Two sweeps:
  * subscribers — 16 MiB payload, K in {1,2,4,8}
  * size        — 1 subscriber, payload in {1,4,16,64} MiB

Run after `source install/setup.bash`. Output: eval/out/results.csv.
"""

import subprocess
import time
from pathlib import Path

import harness

# Repository root, which is also the colcon workspace root (see the top-level README).
WS = Path(__file__).resolve().parents[3]
BIN = WS / "install/agnocast_eval/lib/agnocast_eval"
DISCOVERY_BIN = WS / "install/ros2agnocast_discovery_agent/bin/discovery_agent"
OUT_DIR = Path(__file__).resolve().parent / "out"
LOG_DIR = OUT_DIR / "logs"
START_DELAY_MS = 2500
REPEATS = 2

# Each mode maps to its publisher/subscriber binaries and whether to preload the heaphook.
MODES = {
    "agnocast": dict(talker="agnocast_talker", listener="agnocast_hold_listener", preload=True),
    "std": dict(talker="std_talker", listener="std_hold_listener", preload=False),
}

NODE_NAMES = "agnocast_talker|agnocast_hold_listener|std_talker|std_hold_listener"


def measured(mode, subscribers, payload_bytes, label):
    """Average REPEATS measurements of one (mode, subscribers, payload) case."""
    cfg = MODES[mode]
    vals = []
    for r in range(REPEATS):
        tag = f"{label}_r{r}"
        mib = harness.measure_case(
            BIN / cfg["talker"], BIN / cfg["listener"],
            subscribers=subscribers, payload_bytes=payload_bytes,
            topic=f"/eval_{tag}", tag=tag, log_dir=str(LOG_DIR),
            start_delay_ms=START_DELAY_MS, preload=cfg["preload"],
        )
        vals.append(mib)
        time.sleep(0.5)
    return sum(vals) / len(vals)


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    subprocess.run("rm -f /dev/shm/agnocast_discovery_agent_*.lock", shell=True)
    subprocess.run(f"pkill -9 -f '{NODE_NAMES}' 2>/dev/null || true", shell=True)

    disc = harness.spawn(
        DISCOVERY_BIN, harness.child_env(1, "/unused", START_DELAY_MS, False),
        str(LOG_DIR / "discovery.log"),
    )
    time.sleep(2.0)

    rows = [("sweep", "mode", "x", "payload_mib", "subscribers", "total_payload_pss_mib")]
    try:
        # Sweep 1 — subscriber count at a fixed 16 MiB payload.
        for k in (1, 2, 4, 8):
            for mode in ("std", "agnocast"):
                mib = measured(mode, k, 16 << 20, f"subs_{mode}_k{k}")
                print(f"[subscribers] {mode:8s} K={k}  payload=16MiB  ->  {mib:7.1f} MiB")
                rows.append(("subscribers", mode, k, 16, k, round(mib, 2)))

        # Sweep 2 — payload size with a single subscriber.
        for n_mib in (1, 4, 16, 64):
            for mode in ("std", "agnocast"):
                mib = measured(mode, 1, n_mib << 20, f"size_{mode}_n{n_mib}")
                print(f"[size]        {mode:8s} N={n_mib:2d}MiB  K=1  ->  {mib:7.1f} MiB")
                rows.append(("size", mode, n_mib, n_mib, 1, round(mib, 2)))
    finally:
        harness.kill_all([disc])
        subprocess.run("rm -f /dev/shm/agnocast_discovery_agent_*.lock", shell=True)

    csv_path = OUT_DIR / "results.csv"
    with open(csv_path, "w") as f:
        for row in rows:
            f.write(",".join(str(c) for c in row) + "\n")
    print(f"\nwrote {csv_path} ({len(rows) - 1} rows)")


if __name__ == "__main__":
    main()
