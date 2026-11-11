# Parallel Quicksort: Improving the Experimental Methodology

Changes made to the original setup (see [journal.org](journal.org)), in order.
Test machine: Apple M1 (4P+4E cores), macOS 14, Apple clang 15.

## 1. Code fixes

These issues biased *what* was being measured, so they had to be fixed before redesigning the experiment.

| # | Problem in original code | Consequence | Fix |
|---|---|---|---|
| 1 | `srand(time(NULL))` | Runs started within the same second sort the **same array** with the same pivots, so repetitions are not independent | Seed taken from the ns clock and printed; can be fixed with `argv[3]` for reproducibility |
| 2 | `rand()` called concurrently by all threads in `partition` | Data race (POSIX does not require `rand()` to be thread-safe); on glibc a lock serialises threads and inflates Par time | `rand_r()` with a private state per thread; child seeds derived from the parent's, so a run is deterministic given the seed |
| 3 | `THREAD_LEVEL` hard-coded to 10 | Always ~2047 thread creations, even for 100 elements; cannot be studied | Optional `argv[2]` (default 10, unchanged) |
| 4 | `gettimeofday` | 1 µs resolution while n=100 sorts take ~5 µs; wall clock can jump | `clock_gettime(CLOCK_MONOTONIC_RAW)`, printed with `%.9f` (falls back to `CLOCK_MONOTONIC`; on macOS the latter only has 1 µs resolution) |
| 5 | Built with `-O0` | Custom sorts are unoptimised but `qsort` comes from optimised libc. The conclusion flips with the flag: at `-O0` Seq ≈ Libc, at `-O2` Seq is ~20% faster | Makefile defaults to `-O2`; debug build via `make WARNINGS='$(PEDANTIC_PARANOID_FREAK)'` |
| 6 | `-lrt` in `CFLAGS` | Fails to link on macOS | Moved to `LIBS`, Linux only |
| 7 | `pthread_exit()` at the end of `main` | Hangs under ThreadSanitizer on macOS | `return 0` |

**Usage:** `./src/parallelQuicksort [size [thread_level [seed]]]`. Output gains a first line
`Size: N, Level: L, Seed: S`; the timing lines are unchanged, so the existing Perl extractors still work.

**Verification**
- Sorted output (no `Oops`, no crash) for sizes {0, 1, 2, 3, 100, 10⁶} × levels {0, 1, 5, 10, 12}.
- Warning-free with the pedantic flags (clang), and with `gcc-14 -Wall -Wextra`.
- Back-to-back runs now get different seeds.
- Timer resolution is now 42 ns (the M1's 24 MHz counter).
- *Not tested:* the build on Linux.

## 2. Experimental design

Runner: `python3 scripts/run_benchmark.py [reps=30] [design_seed=42]`. It rebuilds the binary, then writes
`data/<host>_<date>/measurements_<HH-MM>.csv` and a matching `.info` file.
Code change: an optional 4th argument sets the algorithm order (a permutation of `spl`).

| | Original | New | Why |
|---|---|---|---|
| Sizes | 5, from 10² to 10⁶ in ×10 steps | 21, log-spaced from 10² to 10⁷ (4 per decade) | Locate the crossover and see the shape of the curves; 10⁷ shows the asymptotic regime |
| Thread level | 10 only | {0, 2, 4, 6, 8, 10} | Pilot: the best level is 4–8 depending on size. Level 0 (one thread, no split) isolates the thread-creation cost |
| Repetitions | 5 | 30 per (size, level) | Pilot CV of Par ≈ 15%, so the 95% CI is about ±5.6% of the mean. Seq/Libc vary less (CV < 2%) and get 180 runs per size |
| Run order | Sizes increasing, repetitions back to back | All 3780 runs shuffled (fixed seed) | Keeps drift (heat, background load) from lining up with any factor |
| Algorithm order within a run | Always Seq → Par → Libc | Rotates through the 6 orders across repetitions (5 of each per cell); position recorded | Removes, and lets us measure, order and cache effects |
| Input array | Seeded by the clock | Seed per (size, rep), shared across levels | Common random numbers: levels are compared on identical arrays; every run is reproducible |
| Output | Text, parsed with Perl | Long CSV: `run,size,level,rep,seed,algo,position,time` | Read directly by R |
| Metadata | None | `.info`: git commit, compile line, compiler, OS, CPU, power source, load average before/after, design, duration | Needed to interpret and compare runs |

**Run:** [`data/Ruxandras-MacBook-Air_2026-11-11/measurements_11-31.csv`](data/Ruxandras-MacBook-Air_2026-11-11/measurements_11-31.csv)
- 3780 runs (11 340 timings), 18 minutes, all outputs sorted.
- Balance confirmed: 30 reps per cell, and each algorithm 1260 times in each position.
- Load average rose from 2.4 to 3.0 during the run.

**Limitations:**
- One machine only.
- Laptop running on battery with normal background apps (load is recorded).
- Thread placement on the M1's performance vs efficiency cores is not controlled (macOS has no thread pinning).
- Seq and Libc are measured in every run, so they are oversampled. That is the cost of keeping paired measurements within each run.

## 3. Analysis

*To do.*
