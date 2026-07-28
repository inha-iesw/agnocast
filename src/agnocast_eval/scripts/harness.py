#!/usr/bin/env python3
"""Reusable process + memory-measurement harness for the Agnocast evaluation.

Independent of any specific experiment: it launches node binaries, waits for stdout
markers, samples smaps_rollup Pss, and measures one publish-and-hold case given the
binaries and parameters. The sweep definition lives in run_eval.py; plotting is separate.
"""

import os
import subprocess
import time


def child_env(payload_bytes, topic, start_delay_ms, preload):
    """Build the environment for an eval node.

    LD_PRELOAD is set only for the zero-copy (Agnocast) path; standard ROS 2 nodes must
    not preload the heaphook (it would allocate their payloads in shared memory too).
    """
    env = dict(os.environ)
    env.update(
        {
            "EVAL_BLOCK_BYTES": str(payload_bytes),
            "EVAL_TOPIC": topic,
            "EVAL_START_DELAY_MS": str(start_delay_ms),
            "AGNOCAST_BRIDGE_MODE": "off",
        }
    )
    if preload:
        env["LD_PRELOAD"] = "libagnocast_heaphook.so"
    else:
        env.pop("LD_PRELOAD", None)
    return env


def spawn(binary, env, logpath):
    """Spawn a node binary directly so .pid is the node's pid and the heaphook (when
    preloaded) registers the process exactly once.

    Must be called from a sourced shell so the inherited environment already carries the
    ROS paths; wrapping in an LD_PRELOAD'd bash would make the wrapper register the pid
    first and the exec'd node's re-registration would fail with 'process already exists'.
    """
    f = open(logpath, "w")
    p = subprocess.Popen([str(binary)], env=env, stdout=f, stderr=subprocess.STDOUT)
    p._logf = f
    return p


def wait_marker(logpath, marker, timeout):
    """Poll a node's log until it contains `marker`; return True on success, False on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(logpath) as f:
                if marker in f.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.05)
    return False


def read_pss_kb(pid):
    """Return the process's total Pss (kB) from smaps_rollup, or None if it is gone."""
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Pss:"):
                    return int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError, ValueError):
        return None
    return None


def kill_all(procs):
    """SIGKILL every process and close its log file."""
    for p in procs:
        try:
            p.kill()
        except ProcessLookupError:
            pass
    for p in procs:
        try:
            p.wait(timeout=5)
        except Exception:
            pass
        try:
            p._logf.close()
        except Exception:
            pass


def measure_case(
    talker_bin, listener_bin, *, subscribers, payload_bytes, topic, tag, log_dir,
    start_delay_ms, preload
):
    """Run one publish-and-hold case and return the payload's physical memory in MiB.

    Starts `subscribers` message-holding listeners and one publisher, samples every
    participant's smaps_rollup Pss before the publish (baseline) and after all subscribers
    hold the message (loaded), and returns the summed delta. Raises RuntimeError if a node
    never reaches its expected phase. The caller owns the discovery agent lifecycle.
    """
    def env():
        return child_env(payload_bytes, topic, start_delay_ms, preload)

    listeners = []
    for i in range(subscribers):
        lp = f"{log_dir}/{tag}_sub{i}.log"
        p = spawn(listener_bin, env(), lp)
        if not wait_marker(lp, "SUBSCRIBED", 20):
            kill_all(listeners + [p])
            raise RuntimeError(f"{tag}: subscriber {i} never became ready")
        listeners.append(p)

    tlog = f"{log_dir}/{tag}_talker.log"
    talker = spawn(talker_bin, env(), tlog)
    procs = listeners + [talker]
    if not wait_marker(tlog, "READY", 20):
        kill_all(procs)
        raise RuntimeError(f"{tag}: talker never became ready")

    pids = [p.pid for p in procs]
    time.sleep(0.8)  # settle, still well before the publish at start_delay_ms
    baseline = {pid: read_pss_kb(pid) for pid in pids}

    if not wait_marker(tlog, "PUBLISHED", 20):
        kill_all(procs)
        raise RuntimeError(f"{tag}: talker never published")
    for i in range(subscribers):
        if not wait_marker(f"{log_dir}/{tag}_sub{i}.log", "HELD", 20):
            kill_all(procs)
            raise RuntimeError(f"{tag}: subscriber {i} never received")
    time.sleep(1.2)  # steady state
    loaded = {pid: read_pss_kb(pid) for pid in pids}

    kill_all(procs)

    if any(baseline[p] is None or loaded[p] is None for p in pids):
        raise RuntimeError(f"{tag}: a process died before measurement")
    total_kb = sum(loaded[p] - baseline[p] for p in pids)
    return total_kb / 1024.0  # MiB
