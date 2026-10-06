# Contributing

Thanks for your interest in **GPrimes64**!

## Build (Windows x64)
Prerequisite: Visual Studio 2022 **Build Tools** with the C++ workload
(MSVC + Windows SDK).

```bat
build.bat
```

This produces `gprimes64.exe` in the project directory. The build is static
(`/MT`) and depends only on `KERNEL32.dll`.

## Tests
There is no test runner; after changes, please run the smoke tests:

```bat
gprimes64.exe -q 1000000
gprimes64.exe -m miller -q 1000000
gprimes64.exe -c 100
gprimes64.exe -r 1000000000000 1000001000000
```

Known reference values: `π(10^6) = 78498`, `π(10^8) = 5761455`,
100000th prime = 1299709.

## Pull requests
1. Create a feature branch (`git checkout -b feature/xyz`).
2. Commit your changes (clear, descriptive commit messages).
3. Run `build.bat` and check the smoke tests.
4. Open a pull request against `main`; the CI
   (`.github/workflows/build.yml`) must be green.

## Style
- Pure C (C11), no external libraries other than the Windows API.
- Keep the existing style/structure. The project language is **English**
  (code comments and all user-facing texts).
- Respect the versioning scheme (`primes.c`, `PRIMES_VERSION`):
  1st = rewrite, 2nd = features, 3rd = hotfixes.
- Maintain `changelog.md`.
