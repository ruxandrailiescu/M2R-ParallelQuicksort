#!/usr/bin/env python3
"""Randomized full-factorial benchmark of src/parallelQuicksort (run from the repo root).

Usage: python3 scripts/run_benchmark.py [reps=30] [design_seed=42]
Writes data/<host>_<date>/measurements_<HH-MM>.csv (one row per algorithm and run)
and a matching .info file (machine, build and design metadata).
"""
import csv, datetime, itertools, os, platform, random, re, socket, subprocess, sys, time

REPS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
DSEED = int(sys.argv[2]) if len(sys.argv) > 2 else 42
SIZES = sorted({round(10 ** (2 + k / 4)) for k in range(21)})  # 1e2..1e7, 4 per decade
LEVELS = [0, 2, 4, 6, 8, 10]                                     # 2^L leaf threads
PERMS = ["".join(p) for p in itertools.permutations("spl")]      # algorithm orders
NAMES = {"Sequential": "Seq", "Parallel": "Par", "Built-in": "Libc"}
BIN = "./src/parallelQuicksort"


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout.strip()


rng = random.Random(DSEED)
# Common random numbers: the input seed depends on (size, rep) only, so all levels sort the same arrays.
seeds = {(n, r): rng.randrange(2**32) for n in SIZES for r in range(REPS)}
runs = [(n, l, r) for n in SIZES for l in LEVELS for r in range(REPS)]
rng.shuffle(runs)

now = datetime.datetime.now()
outdir = f"data/{socket.gethostname().split('.')[0]}_{now:%F}"
os.makedirs(outdir, exist_ok=True)
base = f"{outdir}/measurements_{now:%H-%M}"

info = {
    "date": now.isoformat(timespec="seconds"),
    "git": sh("git describe --always --dirty"),
    "build": sh("make -C src clean all 2>&1 | grep -E '^(cc|gcc|clang)'"),
    "compiler": sh("cc --version | head -1"),
    "os": platform.platform(),
    "cpu": sh("sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2"),
    "ncpu": os.cpu_count(),
    "power": sh("pmset -g batt 2>/dev/null | head -1"),
    "load_before": os.getloadavg(),
    "design": f"sizes={SIZES} levels={LEVELS} reps={REPS} design_seed={DSEED} "
              f"order=shuffled runs, algorithm order cycles over {PERMS} by rep",
}

pat = re.compile(r"^(\S+) quicksort took: ([\d.]+) sec\.$", re.M)
t0 = time.time()
with open(base + ".csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["run", "size", "level", "rep", "seed", "algo", "position", "time"])
    for i, (n, l, r) in enumerate(runs):
        out = subprocess.run([BIN, str(n), str(l), str(seeds[n, r]), PERMS[r % len(PERMS)]],
                             capture_output=True, text=True, check=True).stdout
        if "Oops" in out:
            sys.exit(f"Unsorted output in run {i}:\n{out}")
        for pos, (algo, t) in enumerate(pat.findall(out), 1):
            w.writerow([i, n, l, r, seeds[n, r], NAMES[algo], pos, t])
        if i % 200 == 0:
            f.flush()
            print(f"{i}/{len(runs)}", flush=True)

info["load_after"] = os.getloadavg()
info["duration_s"] = round(time.time() - t0)
with open(base + ".info", "w") as f:
    f.writelines(f"{k}: {v}\n" for k, v in info.items())
print("done:", base)
