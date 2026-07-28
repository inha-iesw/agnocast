#!/usr/bin/env python3
"""Sample a process's Agnocast shared-memory pool footprint into a CSV.

The pool appears in /proc/<pid>/smaps as a large `rw-s` mapping named
`/dev/shm/agnocast@<pid>`. This sums Rss and Pss across every such mapping and appends one
row (epoch_ms,rss_kb,pss_kb) per sample until the target process exits.

Usage: sample_smaps.py <pid> <output_csv> [interval_ms]
"""

import sys
import time


def sample(smaps_path):
    """Return (rss_kb, pss_kb) summed over the agnocast pool mappings, or None if gone."""
    try:
        with open(smaps_path) as f:
            lines = f.read().splitlines()
    except (FileNotFoundError, ProcessLookupError):
        return None
    rss = pss = 0
    in_pool = False
    for line in lines:
        # Mapping header lines have no leading whitespace and start with "addr-addr perms".
        if line and not line[0].isspace() and "-" in line.split()[0]:
            in_pool = "/dev/shm/agnocast@" in line
        elif in_pool and line.startswith("Rss:"):
            rss += int(line.split()[1])
        elif in_pool and line.startswith("Pss:"):
            pss += int(line.split()[1])
    return rss, pss


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: sample_smaps.py <pid> <output_csv> [interval_ms]")
    pid, out = sys.argv[1], sys.argv[2]
    interval_ms = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    smaps_path = f"/proc/{pid}/smaps"

    with open(out, "w") as f:
        f.write("epoch_ms,rss_kb,pss_kb\n")
        f.flush()
        while True:
            s = sample(smaps_path)
            if s is None:
                break
            f.write(f"{int(time.time() * 1000)},{s[0]},{s[1]}\n")
            f.flush()
            time.sleep(interval_ms / 1000.0)


if __name__ == "__main__":
    main()
