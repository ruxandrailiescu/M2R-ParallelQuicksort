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

*To do.*

## 3. Analysis

*To do.*
