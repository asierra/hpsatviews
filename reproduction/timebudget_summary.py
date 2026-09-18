#!/usr/bin/env python3
"""Roll a --timing-csv record up into the four-phase time budget.

    python3 reproduction/timebudget_summary.py <timing.csv> [--json out.json]
                                               [--wall wall.csv]

Groups rows by (host, build, path, commit), takes the median of each stage
across the runs in the group, and reports both the 15 stages and the four-phase
rollup the paper argues over, with the min-max of t_total beside its median.
Reads only the header names, so adding a stage upstream does not break it — but
note that a stage timed in one build and not the other silently biases that
column, which is why the taxonomy is shared (see include/timing.h).

Two guards, both for failure modes the paper describes:
  * a group whose build is "cuda" but whose path is "cpu" fell back silently,
    and its numbers are not GPU numbers; one whose path is "mixed" composed on
    the CPU and ran only some stages (reprojection, viewing geometry) on the
    GPU, so it is neither a CPU nor a GPU number;
  * groups drawn from different git commits are not comparable, and the CPU
    figure from an older revision is exactly what inflates a GPU speed-up.

External wall time. t_total starts once the configuration is parsed and stops
when the row is written, so it leaves out process loading and teardown. If the
external times written by bench_timebudget.sh are present (<timing>_wall.csv
beside the record, or the file given with --wall), each run is paired with its
own by t_start_utc, and the wall time is reported with how much it exceeds
t_total. Quote the wall time, not t_total, against a tool timed from outside.
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


def wall_companion(path):
    return (path[:-4] if path.endswith(".csv") else path) + "_wall.csv"


def load_wall(path):
    """t_start_utc -> external wall seconds, as written by bench_timebudget.sh."""
    with open(path, newline="") as fh:
        return {r["t_start_utc"]: float(r["wall_s"]) for r in csv.DictReader(fh)}


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

    # An explicit --wall that is missing is an error; the implicit companion
    # simply may not exist, as for every record taken before it was written.
    walls = {}
    if "--wall" in argv:
        walls = load_wall(argv[argv.index("--wall") + 1])
    else:
        try:
            walls = load_wall(wall_companion(path))
        except FileNotFoundError:
            pass

    # Derive the stage list from the call-count columns, not from the t_*
    # ones: t_start_utc and t_end_utc are timestamps, not stages.
    stages = [c[2:] for c in rows[0] if c.startswith("n_")]

    # Group by commit as well as by build: a CSV accumulates across runs, so
    # re-measuring after a code change lands the new rows next to the old ones.
    # Warning about that but pooling them anyway is worse than useless — it hands
    # back a median of both revisions that looks like a result. This already hid
    # one: three runs of a change appended to three of its parent, and the pooled
    # median moved by 2 ms, which reads as "no effect" whether or not there was one.
    groups = defaultdict(list)
    for r in rows:
        groups[(r.get("host", "?"), r.get("build", "?"), r.get("path", "?"),
                r.get("git_commit", "") or "?")].append(r)

    hosts = {r.get("host", "") for r in rows}
    if len(hosts) > 1:
        print(f"NOTE: rows span {len(hosts)} hosts {sorted(hosts)}; "
              "reported separately.", file=sys.stderr)
    commits = {r.get("git_commit", "") for r in rows}
    if len(commits) > 1:
        print(f"NOTE: rows span {len(commits)} commits {sorted(commits)}; "
              "reported separately.", file=sys.stderr)
        print("      Compare a CPU and a GPU group only within one revision.",
              file=sys.stderr)

    summary = {}
    multi = len({c for _, _, _, c in groups}) > 1
    multi_host = len({h for h, _, _, _ in groups}) > 1
    for (host, build, taken, commit), rs in sorted(groups.items()):
        def med(col):
            vals = [float(r[col]) for r in rs if r.get(col) not in (None, "")]
            return statistics.median(vals) if vals else 0.0

        total = med("t_total")
        totals = [float(r["t_total"]) for r in rs if r.get("t_total")]
        per_stage = {s: med("t_" + s) for s in stages}
        phases = {name: sum(per_stage.get(s, 0.0) for s in members)
                  for name, members in PHASES}
        accounted = sum(phases.values())
        paired = [(walls[r["t_start_utc"]], float(r["t_total"])) for r in rs
                  if r.get("t_start_utc") in walls and r.get("t_total")]

        print(f"=== host={host} build={build} path={taken} commit={commit}  "
              f"({len(rs)} runs, median) ===")
        if build == "cuda" and taken == "cpu":
            print("  !! built with CUDA but ran the CPU path: silent fallback.")
            print("     These are not GPU numbers.")
        elif taken == "mixed":
            print("  !! composite on the CPU, some stages on the GPU (path=mixed).")
            print("     Neither a CPU nor a GPU number.")
        print(f"  {'phase':<10} {'s':>8} {'% of total':>11}")
        for name, _ in PHASES:
            v = phases[name]
            print(f"  {name:<10} {v:8.3f} {100 * v / total if total else 0:10.1f}%")
        print(f"  {'-' * 31}")
        print(f"  {'accounted':<10} {accounted:8.3f} "
              f"{100 * accounted / total if total else 0:10.1f}%")
        print(f"  {'t_total':<10} {total:8.3f}"
              + (f"   [{min(totals):.3f}-{max(totals):.3f}]" if totals else ""))
        if paired:
            ws = [w for w, _ in paired]
            gap = statistics.median(w - t for w, t in paired)
            print(f"  {'wall':<10} {statistics.median(ws):8.3f}   "
                  f"[{min(ws):.3f}-{max(ws):.3f}]  external clock, "
                  f"{len(paired)}/{len(rs)} runs; exceeds t_total by "
                  f"{gap:.3f} s (median)")
        elif walls:
            print("  wall: no run of this group appears in the wall-time file")
        print("  stages: " + "  ".join(
            f"{s}={per_stage[s]:.3f}" for s in stages if per_stage[s] > 0))
        print()

        # The JSON key stays "build/path" while a file holds one revision, so
        # fig_timebudget.py keeps reading it unchanged; it only grows the commit
        # when there is more than one to tell apart. New fields are additions.
        key = f"{build}/{taken}"
        if multi_host:
            key = f"{host}/{key}"      # dos aceleradores en un mismo registro
        if multi:
            key = f"{key}@{commit}"
        summary[key] = {
            "runs": len(rs), "t_total": total, "commit": commit,
            "phases": phases, "stages": per_stage,
        }
        if totals:
            summary[key]["t_total_min"] = min(totals)
            summary[key]["t_total_max"] = max(totals)
        if paired:
            ws = [w for w, _ in paired]
            summary[key]["wall"] = {
                "runs": len(paired), "median": statistics.median(ws),
                "min": min(ws), "max": max(ws),
                "exceeds_t_total_median":
                    statistics.median(w - t for w, t in paired),
            }

    if out_json:
        with open(out_json, "w") as fh:
            json.dump(summary, fh, indent=2)
        print(f"wrote {out_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
