# GPrimes64 - slim prime generator (Win x64)

A deliberately **slim command-line application** for prime computation.
Written in pure **C (C11)**, compiled with MSVC (x64) into a single native EXE.

It offers several **mathematical methods**, **multithreading**, an **ASCII
table** with the **compute time per prime**, **color-coded threads** and a
**live progress** display (CPU per thread + RAM + extrapolated duration).

## Why slim?

| Property          | Value                                        |
|-------------------|----------------------------------------------|
| Language          | C (no framework, no VM)                      |
| EXE size          | about 176 KB                                 |
| DLL dependencies  | only `KERNEL32.dll`                          |
| Memory usage      | constant for `sieve`/`trial`/`miller` & `-q` |

## How it works

### Methods (`-m`)

| Name        | Method                                      | Notes                                  |
|-------------|---------------------------------------------|----------------------------------------|
| `sieve`     | Segmented sieve of Eratosthenes             | **default**, fast, memory-efficient    |
| `atkin`     | Sieve of Atkin                              | full sieve; memory ~ N/8 bytes         |
| `sundaram`  | Sieve of Sundaram                           | full sieve; memory ~ N/16 bytes        |
| `trial`     | Trial division (6k +/- 1)                   | simple, slow for large N               |
| `miller`    | Miller-Rabin (deterministic, 64-bit)        | ideal for large numbers/ranges         |

`atkin` and `sundaram` are **full sieves**: they need memory proportional to
the upper bound N. For large N or large ranges, `sieve`, `trial` or `miller`
are the right choice. A guard prevents a hanging system and prints a clear
error.

### Internals

1. **Segmented sieve (default):** odd numbers only, in adaptive blocks
   (~256 blocks, 16 KiB-4 MiB). Memory stays bounded; for large *ranges* only
   the base primes up to `sqrt(high)` are needed.
2. **Atkin / Sundaram:** full sieves with a bit array (compact storage).
3. **Trial division:** test by 2, 3 and all `6k +/- 1` up to `sqrt(n)`.
4. **Miller-Rabin:** 7 bases `{2,325,9375,28178,450775,9780504,1795265022}`,
   proven correct for all 64-bit numbers. Modular multiplication via
   `_umul128`/`_udiv128` (no overflow).
5. **First N primes:** upper bound via `n*(ln n + ln ln n)`.
6. **Overflow-safe tests** (`p > n / p` instead of `p*p > n`).

Correctness is checked against known values (all five methods produce
bit-identical results):
`π(10^6) = 78498`, `π(10^8) = 5761455`, 1000000th prime = 15485863,
`π([10^12, 10^12+10^6]) = 36249`.

## Usage

```text
gprimes64 <N>                 All primes up to and including N
gprimes64 -c <N>              The first N primes
gprimes64 -r <A> <B>          All primes in the range A to B

Options:
  -l, --limit <N>             Same as 'gprimes64 <N>'
  -c, --count <N>             The first N primes
  -r, --range <A> <B>         Primes in the range A..B
  -m, --method <name>         Computation method (default: sieve)
  -t, --time                  ASCII table: prime | compute time
  -j, --threads <N>           Use N threads (1 = off, default)
      --mt                    As many threads as CPU cores (-j 0)
      --list-methods          List available methods
  -q, --quiet                 Output only the summary (nothing else)
  -h, --help                  Show this help
  -v, --version               Show version
```

A **summary is always printed** - normally on `stderr`, and with `-q` as the
only output on `stdout`:

```text
Count: 78498, Time: 0.001 s, Method: sieve, Threads: 1
```

## Number range and limits

- **Valid number range:** `0 ... 18446744073709551615` (= 2^64-1). Larger
  values are rejected with a clear message (the program uses 64-bit integers).
- **Sieve vs. large numbers:** the `sieve` method builds base primes up to
  `sqrt(N)`. For large `high` (near 2^64) the memory requirement grows strongly
  (bitset + prime array per thread). Before starting, the estimated requirement
  is checked against the **available RAM**; if it does not fit, the program
  aborts with an estimate and alternatives:

  ```text
  Error: upper bound ... is too large for the sieve method.
         Estimated base-prime memory ~1.26 GB per thread x 8 = ~10.1 GB,
         but only ~34.2 GB are available.
         Please use -m miller or -m trial (or reduce -j).
  ```

- For **very large numbers** or **small ranges near large values**, `-m miller`
  or `-m trial` are much better suited (they test individual candidates instead
  of sieving up to `sqrt(N)`).

## Live progress (CPU per thread + RAM + extrapolation)

During the computation, `stderr` shows in a **standing line** (carriage return)
the CPU load per thread and the RAM usage. The live line appears in **all modes
except `-q`**.

```text
CPU/Thread: T01= 94% T02=100% T03= 97% ... / RAM: 28.8 MB
```

The separator between the thread and RAM display **rotates** during the
computation through `|` -> `/` -> `-` -> `\`.

The columns are **fixed** (thread index and percentage values in fixed width)
so the line does not jump due to changing number lengths; `=` and `:` stay at
fixed positions.

### Extrapolation

The extrapolation is **always** shown at the end of the live line. Until the
first extrapolation, **all number positions are replaced by `-`**:

```text
... | RAM: 28.8 MB | --% / --:--:-- sec of --:--:-- sec
```

From about **10% of the numbers processed in the threads** (not the emitted
primes) it is replaced by real, **live** values. For large `high` the
base-prime build up to `sqrt(high)` is included as well, since it dominates the
runtime there. The marking steps are counted **stride-weighted** (cache-line
effect) and the estimate is smoothed, so the duration is immediately realistic:

```text
... | RAM: 28.8 MB | 13% / 00:00:39 sec of 00:05:00 min
                    ^       ^              ^
               percent   elapsed     extrapolated total duration
```

The unit suffix follows the magnitude (for elapsed **and** extrapolated time):
`< 60 s` -> `sec`, `< 1 h` -> `min`, otherwise `h`. In the example above the
elapsed time is 39 seconds (`sec`) and the extrapolated duration is 5 minutes
(`min`).

- The expected duration is re-extrapolated **every further 5%** and additionally
  **corrected upward continuously** (shown in red) if the real time would
  overtake it - it therefore never falls below the elapsed time.
- Its color follows the trend:
  - **red** - longer than the previous estimate
  - **green** - shorter than the previous estimate
  - **white** - first estimate or deviation <= 5%
- Percent and elapsed time are updated on every refresh (~10x/s).

To keep the live line stable and flicker-free, it is **overwritten in place**
(no line clearing). During console streaming the primes are briefly **buffered**
and emitted only ~10x/s together with the status line. With redirected `stdout`
the file stays clean (primes only) and the live line stays on `stderr`.

- Force it (e.g. for testing/redirection): `PRIMES_PROGRESS=1`.

## Multithreading (`-j` / `--mt`)

```bat
gprimes64 -m sieve -j 4   -q 1000000000   REM 4 threads
gprimes64 -m sieve --mt   -q 1000000000   REM all CPU cores
```

The range is split into contiguous blocks, each thread computes one block, and
the output is emitted **in ascending order** - deterministic and identical to
the single-threaded output.

Example scaling (sieve up to 10^9, 50847534 primes):

| Threads | Time   |
|---------|--------|
| 1       | 0.92 s |
| 4       | 0.42 s |
| auto    | 0.25 s |

## Examples

```bat
gprimes64 -t 100
gprimes64 -m atkin -q 100000000
gprimes64 -m sieve -j 8 -q 1000000000
gprimes64 -m miller --mt -q -r 1000000000000 1000001000000
```

## Color coding (ANSI 256)

With multithreading, **each thread** gets a color from the ANSI 256 color space:

- Colors are **as far apart as possible** (farthest-point selection in the RGB
  space of the 6x6x6 color cube).
- The **16 grayscale tones are excluded** (neutral `r=g=b` colors).
- Special case: with **one thread**, the color is **white**.

In the **live line** the label and value of each thread are colored:

```text
CPU/Thread: T01= 94% T02= 94% T03= 94% ... T12= 94% / RAM: 28.8 MB
```

In the **`-t` table** the **vertical bars** of each row are drawn in the color
of the thread that computed the value:

```text
| Prime | Compute time |   <- bars colored per row
```

Colors are enabled automatically on real consoles. When redirecting,
`PRIMES_COLOR=1` forces them.

## Environment variables

| Variable            | Effect                                             |
|---------------------|----------------------------------------------------|
| `PRIMES_COLOR=1`    | Force colors (also with redirected stdout)         |
| `PRIMES_PROGRESS=1` | Force the live line (also without a console)       |

## Files

- `primes.c` - source code (single file)
- `build.bat` - build script
- `changelog.md` - continuously maintained changelog (since 1.0.0, English)
- `ChangelogOld_ger.md` - the previous German changelog (no longer maintained)
- `LICENSE` - MIT license
- `CONTRIBUTING.md` - contribution guide
- `.github/workflows/` - CI (build) and release (attach EXE on tag `v*`)

## Build

Prerequisite: Visual Studio 2022 **Build Tools** with the C++ workload
(MSVC + Windows SDK). Simply run:

```bat
build.bat
```

This produces `gprimes64.exe` in the project directory.
