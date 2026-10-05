# Contributing

Danke für dein Interesse an **GPrimes64**!

## Build (Windows x64)
Voraussetzung: Visual Studio 2022 **Build Tools** mit C++-Workload (MSVC + Windows SDK).

```bat
build.bat
```

Erzeugt `primes.exe` im Projektverzeichnis. Der Build ist statisch (`/MT`) und
hängt nur von `KERNEL32.dll` ab.

## Tests
Es gibt keinen Test-Runner; bitte nach Änderungen die Smoke-Tests ausführen:

```bat
primes.exe -q 1000000
primes.exe -m miller -q 1000000
primes.exe -c 100
primes.exe -r 1000000000000 1000001000000
```

Bekannte Referenzwerte: `π(10^6) = 78498`, `π(10^8) = 5761455`,
100000. Primzahl = 1299709.

## Pull Requests
1. Feature-Branch anlegen (`git checkout -b feature/xyz`).
2. Änderungen committen (klare, beschreibende Commit-Messages).
3. `build.bat` ausführen und die Smoke-Tests prüfen.
4. Pull Request gegen `main` stellen; die CI (`.github/workflows/build.yml`) muss grün sein.

## Stil
- Reines C (C11), keine externen Bibliotheken außer der Windows-API.
- Bestehenden Stil/Struktur beibehalten (Kommentare auf Deutsch).
- Versionsschema (`primes.c`, `PRIMES_VERSION`) beachten:
  1. Stelle = Rewrite, 2. Stelle = Features, 3. Stelle = Hotfixes.
- `changelog.md` pflegen.
