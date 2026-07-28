#!/usr/bin/env bash
# End-to-end reproduction of the Agnocast shared-memory physical-page reclaim gap.
#
# Launches the burst node under the heaphook, samples the shared-memory pool's Rss
# across the baseline -> peak -> drain -> observe phases, and prints a verdict:
# if resident memory stays near the peak after every message is freed, the freed
# physical pages were not returned to the kernel (the bug is reproduced).
#
# Tunables (env): REPRO_BLOCK_BYTES, REPRO_NUM_MSGS, REPRO_PHASE_SECS (see the node).
set -euo pipefail

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
# Repository root, which is also the colcon workspace root (see the top-level README).
WS="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT_DIR="$SCRIPT_DIR/out"
NODE_LOG="$OUT_DIR/node.log"
CSV="$OUT_DIR/smaps.csv"
HEAPHOOK="$WS/install/agnocastlib/lib/libagnocast_heaphook.so"
BIN="$WS/install/agnocast_repro/lib/agnocast_repro/burst_leak_repro"

mkdir -p "$OUT_DIR"

# ROS setup scripts reference unset vars, so relax nounset while sourcing them.
set +u
# shellcheck disable=SC1091
source /opt/ros/humble/setup.bash
# shellcheck disable=SC1091
source "$WS/install/setup.bash"
set -u

# --- Preflight ------------------------------------------------------------------
[ -c /dev/agnocast ] || { echo "ERROR: /dev/agnocast missing — load the kernel module (sudo insmod agnocast.ko)"; exit 1; }
[ -d /sys/module/agnocast ] || { echo "ERROR: agnocast kernel module not loaded"; exit 1; }
[ -f "$HEAPHOOK" ] || { echo "ERROR: $HEAPHOOK missing — run scripts/dev/build_all.bash first"; exit 1; }
[ -x "$BIN" ] || { echo "ERROR: $BIN missing — colcon build --packages-select agnocast_repro"; exit 1; }

echo "== launching burst node (bridge disabled, heaphook preloaded) =="
# LD_PRELOAD is scoped to the node only, so the sampler/summary below run un-hooked.
AGNOCAST_BRIDGE_MODE=off LD_PRELOAD="libagnocast_heaphook.so" "$BIN" >"$NODE_LOG" 2>&1 &
NODE_PID=$!

# Wait for the node to announce its pid (also confirms it started cleanly).
for _ in $(seq 1 50); do
  grep -q "REPRO_PID" "$NODE_LOG" 2>/dev/null && break
  kill -0 "$NODE_PID" 2>/dev/null || { echo "ERROR: node exited early:"; cat "$NODE_LOG"; exit 1; }
  sleep 0.1
done

echo "== sampling /proc/$NODE_PID/smaps -> $CSV =="
python3 "$SCRIPT_DIR/sample_smaps.py" "$NODE_PID" "$CSV" 200 &
SAMPLER_PID=$!

wait "$NODE_PID"
wait "$SAMPLER_PID" 2>/dev/null || true

echo
echo "== node phase markers =="
grep -E "REPRO_CONFIG|MARKER" "$NODE_LOG"

echo
echo "== verdict =="
python3 "$SCRIPT_DIR/summarize_repro.py" "$NODE_LOG" "$CSV"
