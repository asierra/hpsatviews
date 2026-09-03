#!/usr/bin/env python3
"""Roll a --timing-csv record up into the four-phase time budget.

    python3 reproduction/timebudget_summary.py <timing.csv> [--json out.json]

Groups rows by (build, path), takes the median of each stage across the runs in
the group, and reports both the 15 stages and the four-phase rollup the paper
argues over. Reads only the header names, so adding a stage upstream does not
break it — but note that a stage timed in one build and not the other silently
biases that column, which is why the taxonomy is shared (see include/timing.h).

Two guards, both for failure modes the paper describes:
  * a group whose build is "cuda" but whose path is "cpu" fell back silently,
    and its numbers are not GPU numbers;
  * groups drawn from different git commits are not comparable, and the CPU
    figure from an older revision is exactly what inflates a GPU speed-up.
"""

import csv
import json
import statistics
import sys
from collections import defaultdict

# Stage -> phase. Deliberately explicit rather than inferred: the assignment of
# xfer and mem is a judgement, and burying it would hide it.
PHASES = [
    ("Startup",  ["init", "open"]),
    ("Input",    ["read", "decode", "unpack"]),
    ("Compute",  ["nav", "geom", "correct", "compose", "enhance", "reproject",
                  "xfer", "mem", "other"]),
    ("Output",   ["write"]),
]


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    path, out_json = argv[0], None
    if "--json" in argv:
        out_json = argv[argv.index("--json") + 1]

    with open(path, newline="") as fh:
        lines = [l for l in fh if not l.startswith("#")]
    rows = list(csv.DictReader(lines))
    if not rows:
        print(f"{path}: no data rows", file=sys.stderr)
        return 1

    # Derive the stage list from the call-count columns, not from the t_*
    # ones: t_start_utc and t_end_utc are timestamps, not stages.
    stages = [c[2:] for c in rows[0] if c.startswith("n_")]

    groups = defaultdict(list)
    for r in rows:
        groups[(r.get("build", "?"), r.get("path", "?"))].append(r)

    commits = {r.get("git_commit", "") for r in rows}
    if len(commits) > 1:
        print(f"WARNING: rows span {len(commits)} commits {sorted(commits)}.",
              file=sys.stderr)
        print("         Builds from different revisions are not comparable.",
              file=sys.stderr)

    summary = {}
    for (build, taken), rs in sorted(groups.items()):
        def med(col):
            vals = [float(r[col]) for r in rs if r.get(col) not in (None, "")]
            return statistics.median(vals) if vals else 0.0

        total = med("t_total")
        per_stage = {s: med("t_" + s) for s in stages}
        phases = {name: sum(per_stage.get(s, 0.0) for s in members)
                  for name, members in PHASES}
        accounted = sum(phases.values())

        print(f"=== build={build} path={taken}  ({len(rs)} runs, median) ===")
        if build == "cuda" and taken == "cpu":
            print("  !! built with CUDA but ran the CPU path: silent fallback.")
            print("     These are not GPU numbers.")
        print(f"  {'phase':<10} {'s':>8} {'% of total':>11}")
        for name, _ in PHASES:
            v = phases[name]
            print(f"  {name:<10} {v:8.3f} {100 * v / total if total else 0:10.1f}%")
        print(f"  {'-' * 31}")
        print(f"  {'accounted':<10} {accounted:8.3f} "
              f"{100 * accounted / total if total else 0:10.1f}%")
        print(f"  {'t_total':<10} {total:8.3f}")
        print("  stages: " + "  ".join(
            f"{s}={per_stage[s]:.3f}" for s in stages if per_stage[s] > 0))
        print()

        summary[f"{build}/{taken}"] = {
            "runs": len(rs), "t_total": total,
            "phases": phases, "stages": per_stage,
        }

    if out_json:
        with open(out_json, "w") as fh:
            json.dump(summary, fh, indent=2)
        print(f"wrote {out_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
