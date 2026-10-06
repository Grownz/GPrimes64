# Changelog - GPrimes64

All notable changes to this project, maintained continuously.
Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Versioning scheme

| Position | Example | Meaning           |
|----------|---------|-------------------|
| 1st      | `3.x.y` | full code rewrite |
| 2nd      | `x.1.y` | major features    |
| 3rd      | `x.y.1` | hotfixes          |

> This convention applies from version 2.0.1 and is applied **retroactively**
> to the entire history since 1.0.0.

---

## [3.1.0] - 2026-10-06

### Added
- **128-bit support.** If a limit, count or range value exceeds the 64-bit
  range, the program switches to **128-bit arithmetic** automatically. Values
  beyond 64-bit are handled with **Miller-Rabin** (probabilistic). The sieve
  methods (`sieve`, `atkin`, `sundaram`) and `trial` are not available there
  and are rejected with a one-sentence error:
  `method 'X' is not available for numbers beyond 64-bit; use -m miller`.
  Range/limit/count modes and plain/quiet/table output are supported in
  128-bit mode (single-threaded).
- **Average CPU in the console window title.** During a computation the title
  shows `gprimes64.exe - CPU NN%` (average over all threads), updated once per
  second; the original title is restored afterwards.

### Changed
- Help text documents the extended number range (0 ... 2^128-1).

### Verification
- 128-bit results independently confirmed: primes in
  `[2^64+1, 2^64+84]`, `M127 = 2^127-1` (prime) and `2^127-3` (composite).

---

## [3.0.0] - 2026-10-06

### Changed
- **Full code rewrite / translation to English.** All user-facing texts
  (help, errors, table headers, live line, summary) and the source comments
  are now in English. Functionality is unchanged.
- Application renamed to **GPrimes64**: the binary is `gprimes64.exe`, the
  version output reads `GPrimes64 X.Y.Z`, and usage examples use `gprimes64`.
- README restructured and translated to English.
- Changelog translated to English and continued here; the previous German
  changelog is kept as `ChangelogOld_ger.md` and is no longer maintained.

### Verification
- All methods produce identical results; `π(10^6) = 78498`,
  `π(10^8) = 5761455`, 1000000th prime = 15485863,
  `π([10^12, 10^12+10^6]) = 36249`.
- Only dependency remains `KERNEL32.dll`.

---

## [2.1.8] - 2026-10-06

### Fixed
- **Live line: wrong unit suffix.** The unit was hard-coded (`sec` for elapsed,
  `min` for the extrapolated time), so e.g. 22 seconds appeared as
  "00:00:22 min". The unit now follows the magnitude - for elapsed **and**
  extrapolated time:
  - `< 60 s` -> `sec`
  - `< 1 h`  -> `min`
  - otherwise -> `h`

---

## [2.1.7] - 2026-10-06

### Fixed
- **Misleading error message for oversized numbers** (e.g.
  `-r 100000000000000000000 ...` -> "expects two numbers A B"): `parse_u64`
  now distinguishes **overflow** (`ERANGE`, value > 2^64-1) from invalid input.
  The message names the affected argument (range bound A/B, limit, count) and
  the maximum value:
  `Error: range bound A (-r) ... is too large (max. 18446744073709551615 = 2^64-1).`

### Added
- **Feasibility check for the sieve method:** the estimated base-prime memory
  (bitset + prime array per thread) is checked against available RAM before
  starting. If exceeded, the program aborts with a clear estimate and
  alternatives (`-m miller`/`-m trial`, reduce `-j`). `miller`/`trial` are not
  affected.
- **Help/docs:** documented the valid number range
  `0 ... 18446744073709551615 (2^64-1)` and the recommendation for very large
  numbers.

---

## [2.1.6] - 2026-10-05

### Fixed
- **Live line:** the real time could overtake the extrapolated total duration
  (e.g. `100% / 00:00:49 sec of 00:00:22 sec`). Cause: the estimate was only
  recomputed at the 5% thresholds; if progress became slow near the end, no
  threshold was reached and the estimate "froze".
  - The expected duration is now additionally **corrected upward continuously**
    (shown in red) so the elapsed time never exceeds the extrapolated duration.
  - Verified: across two long runs (50 s and 27 s) there were **0 lines** with
    `elapsed > expected`; the estimate converges to the real duration.

---

## [2.1.5] - 2026-10-05

### Changed
- **Duration estimate of the live line** greatly improved:
  - The marking steps of the base-prime sieve are now counted **stride-weighted**
    (cost per write grows with the stride, cache-line effect; saturation at
    ~512). The total work is computed **exactly** in advance (identical
    weighting -> exact normalization).
  - The estimate is **smoothed** (EMA).
  - Result: the estimate is **immediately realistic** - in the example
    `-r 10^19 ... -j 8` about **29 s** at 10% (actual 25.6 s) instead of a
    starting value of 4 s.
- **`-t` table:** compute time in **ms with three decimal places**.

### Added
- **Rotating separator** in the live line between the thread and RAM display:
  `|` -> `/` -> `-` -> `\`.

---

## [2.1.4] - 2026-10-05

### Fixed
- **Extrapolation never appeared for large `high` / small range** (only
  placeholders): the **base-prime build up to sqrt(high)** dominated the
  runtime (e.g. `-r 10^19 10^19+1000`: sieve up to ~3.16e9, ~1.5 GB RAM,
  ~25 s) but was not part of the progress - progress only counted the few
  range numbers and therefore stayed at 0%.
  - The base-prime build is now **included** in the progress (rough estimate of
    the marking work; sum over primes <= sqrt(r)).
  - Result: the extrapolation appears from ~10% and runs through to 100%.
    In the example `-r 10^19 ... -j 8` it converges from an initial 4 s to the
    actual **25 s**.

---

## [2.1.3] - 2026-10-05

### Fixed
- **Live line no longer flickers:**
  - The status line is now only **overwritten in place** (no more `ESC[K` line
    clearing) and padded for shorter text.
  - Prime output is **buffered** during console streaming and emitted only
    ~10x/s (or when the buffer is full) together with the status line. The live
    line is therefore no longer rewritten per prime.
- **Extrapolation now appears at about 10% of the processed numbers** (no
  longer only shortly before the end): the sieve uses an **adaptive segment**
  (target ~256 blocks, 16 KiB-4 MiB) instead of fixed 4-MiB blocks, and the
  progress is additionally updated finely within the scan.
- **Table times (`-t`) realistic:** the one-time setup and marking time is no
  longer charged to the first prime but distributed proportionally to the
  candidate gap across the primes found. Output/buffer time (`realloc`) is not
  charged to the next prime.

### Fixed (internal)
- Regression fixed: the adaptive segment could become **odd** and break the
  odd-only sieve alignment (wrong results in some single-threaded runs). The
  segment size is now kept even.

---

## [2.1.2] - 2026-10-05

### Hotfixes
- **`-q`/`--quiet`** now outputs **only the summary** (on `stdout`) - nothing
  else: no separate count value, no primes, no live line.
- **`-q` + `-t`** is not allowed; the error message explains this in **one
  sentence**.
- The **extrapolation** of the live line is **always** shown. Until the first
  extrapolation, **all number positions are replaced by `-`**:
  `--% / --:--:-- sec of --:--:-- sec`.
- Clarified/ensured: the extrapolation starts at about **10% of the numbers
  processed in the threads** (progress counter), **not** at 10% of the emitted
  primes.

---

## [2.1.1] - 2026-10-05

### Added
- **Live line in all computation modes** - it now always appears, except with
  `-q`/`--quiet`.
- **Progress extrapolation** in the live line:
  - From about **10%** of the numbers to be tested, the end shows: completed
    **percent** (live), **elapsed time** (live) and the **extrapolated total
    duration**. Format e.g.: `13% / 00:00:39 sec of 00:05:00 min`.
  - Every further **5%** the total duration is re-extrapolated.
  - Color of the extrapolated duration: **red** (longer than before), **green**
    (shorter than before), **white** (first extrapolation or deviation <= 5%).

### Fixed / Changed
- The live line no longer corrupts the prime output: before each prime line the
  status line is briefly removed and placed underneath afterwards
  (synchronization between the compute and status threads).
- Progress tracking per method; fixed a bug in `trial`/`miller` (progress was
  only advanced on odd steps).

---

## [2.1.0] - 2026-10-05

### Added
- **Color-coded threads** (ANSI 256):
  - With multithreading, each thread gets a color **as far apart as possible**
    in the color space; the **16 grayscale tones are excluded**.
  - Special case: with only **one thread**, the color is **white**.
  - In the **live line**, the thread label and value are shown in the
    respective color.
  - In the **`-t` table**, the **vertical bars** of each row are drawn in the
    color of the thread that computed the value.
- Colors are enabled automatically on real consoles; `PRIMES_COLOR=1` forces
  them (e.g. when redirecting).

### Hotfixes
- **`-p`/`--per-prime` removed** (redundant due to the `-t` table).
- The **summary** (count/time/method/threads) is now **always** printed, no
  longer only with `-t`.
- **Live line no longer jumps**: thread label, `=` and percentage values have
  fixed column widths so `=` and `:` stay at fixed positions.

### Changed
- `-t` and `-q` remain mutually exclusive (now the only exclusion combination).

---

## [2.0.1] - 2026-10-05

### Added
- **Versioning scheme** (rewrite/features/hotfixes) introduced and documented.
- **`-t`/`--time`** now prints the results as an **ASCII table** with the
  columns `Prime` and `Compute time`.
- The `Compute time` column shows the times in **milliseconds including the
  unit suffix `ms`**.
- **Live progress line** during the computation (on `stderr`, via carriage
  return): **CPU load per used thread** and **RAM usage** of the program.
- This `changelog.md` as a continuously maintained changelog (retroactively
  from 1.0.0).

### Changed
- `-t` no longer produces a plain summary but the table; the summary appears
  additionally on `stderr`.
- `-q`/`--quiet` excludes `-t` and `-p`.
- `-p`/`--per-prime` prints the duration with a unit (`ms`).

### Fixed / Optimized
- Multithreading in count-only mode (`-q`) no longer buffers primes: RAM for
  `-m sieve -j 4 -q 1000000000` reduced from ~334 MB to ~12 MB.

---

## [2.0.0] - 2026-10-05

### Added
- Selection of various **mathematical methods** via `-m`/`--method`:
  - `sieve` - segmented sieve of Eratosthenes (**default**)
  - `atkin` - sieve of Atkin
  - `sundaram` - sieve of Sundaram
  - `trial` - trial division (6k +/- 1)
  - `miller` - Miller-Rabin (deterministic for 64-bit)
- `-p`/`--per-prime`: display of the computation time of each individual prime
  in milliseconds (removed in 2.1.0).
- **Multithreading** via `-j`/`--threads N` or `--mt` (all CPU cores);
  deterministic, ascending output.
- Memory guard for full sieves (Atkin/Sundaram) with a clear error message
  instead of a hanging system.

### Changed
- Complete reimplementation of the source code (uniform method interface,
  threading, exact arithmetic via `_umul128`/`_udiv128`).

### Verification
- All methods produce bit-identical results.
- `π(10^6) = 78498`, `π(10^8) = 5761455`, 100000th prime = 1299709,
  `π([10^12, 10^12+10^6]) = 36249`.
- Scaling of `sieve` up to 10^9: 1 thread 0.92 s -> 4 threads 0.42 s -> auto
  0.25 s.

---

## [1.0.0] - 2026-10-05

### Added
- Initial release: slim Windows x64 command-line application.
- Segmented sieve of Eratosthenes (odd-only, memory-efficient).
- Modes: upper bound (`<N>`), first N (`-c`), range (`-r`).
- `-q`/`--quiet` (count only), `-t`/`--time` (runtime/count on `stderr`),
  `-h`/`--help`, `-v`/`--version`.
- Statically linked CRT (`/MT`), only DLL dependency `KERNEL32.dll`,
  EXE about 149 KB.
