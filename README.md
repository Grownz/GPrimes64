# primes – Primzahlen berechnen (Windows x64)

Eine bewusst **schlanke Kommandozeilenanwendung** zur Primzahlberechnung.
Geschrieben in reinem **C (C11)**, kompiliert mit MSVC (x64) zu einer einzelnen
nativen EXE mit **statisch gelinkter C-Runtime**.

Auswählbar sind mehrere **mathematische Verfahren**, **Multithreading**, eine
**ASCII-Tabelle** mit der **Berechnungszeit je Primzahl**, **farbcodierte
Threads** sowie ein **Live-Fortschritt** (CPU je Thread + RAM).

> Versionshistorie und Änderungen: siehe [changelog.md](changelog.md).
> Versionierungsschema: 1. Stelle = Rewrite, 2. Stelle = Hauptfeatures,
> 3. Stelle = Hotfixes. Aktuell: **2.1.8**.

## Warum schlank?

| Eigenschaft        | Wert                                           |
|--------------------|------------------------------------------------|
| Sprache            | C (kein Framework, keine VM)                   |
| EXE-Größe          | ca. 172 KB                                     |
| DLL-Abhängigkeiten | nur `KERNEL32.dll` (kein VC++ Redistributable) |
| Speicherverbrauch  | konstant bei `sieve`/`trial`/`miller` & `-q`   |

Die statische CRT (`/MT`) bedeutet: die EXE läuft auf jedem Windows-x64-System
**ohne Installation** weiterer Komponenten.

## Build

Voraussetzung: Visual Studio 2022 **Build Tools** mit C++-Workload
(MSVC + Windows SDK). Einfach ausführen:

```bat
build.bat
```

## Verwendung

```text
primes <N>                 Alle Primzahlen bis einschliesslich N
primes -c <N>              Die ersten N Primzahlen
primes -r <A> <B>          Alle Primzahlen im Bereich A bis B

Optionen:
  -l, --limit <N>          Wie 'primes <N>'
  -c, --count <N>          Die ersten N Primzahlen
  -r, --range <A> <B>      Primzahlen im Bereich A..B
  -m, --method <name>      Berechnungsverfahren (Standard: sieve)
  -t, --time               ASCII-Tabelle: Primzahl | Berechnungszeit
  -j, --threads <N>        N Threads verwenden (1 = aus, Standard)
      --mt                 So viele Threads wie CPU-Kerne (-j 0)
      --list-methods       Verfuegbare Verfahren anzeigen
  -q, --quiet              Nur die Zusammenfassung ausgeben (nichts sonst)
  -h, --help               Hilfe anzeigen
  -v, --version            Version anzeigen
```

**Es wird immer eine Zusammenfassung ausgegeben** – normal auf `stderr`,
bei `-q` als einzige Ausgabe auf `stdout`:

```text
Anzahl: 78498, Zeit: 0.001 s, Verfahren: sieve, Threads: 1
```

## Mathematische Verfahren (`-m`)

| Name        | Verfahren                                   | Hinweis                                   |
|-------------|---------------------------------------------|-------------------------------------------|
| `sieve`     | Segmentiertes Sieb des Eratosthenes         | **Standard**, schnell, speicherschonend   |
| `atkin`     | Sieb des Atkin                              | klassisch; Speicher ~ N/8 Byte            |
| `sundaram`  | Sieb des Sundaram                           | klassisch; Speicher ~ N/16 Byte           |
| `trial`     | Probedivision (6k ± 1)                      | einfach, langsam bei großen N             |
| `miller`    | Miller-Rabin (deterministisch für 64 Bit)   | ideal für große Zahlen/Bereiche           |

`atkin` und `sundaram` sind **vollständige Siebe**: sie benötigen Speicher
proportional zur Obergrenze N. Für große N oder große Bereiche sind `sieve`,
`trial` oder `miller` die richtige Wahl. Eine Schutzprüfung verhindert dabei
ein hängendes System und gibt einen klaren Fehler aus.

## Zahlenbereich und Grenzen

- **Gültiger Zahlenbereich:** `0 … 18446744073709551615` (= 2⁶⁴−1). Größere
  Werte werden mit einer klaren Meldung abgelehnt (das Programm rechnet in
  64-Bit-Ganzzahlen).
- **Sieb vs. große Zahlen:** Das `sieve`-Verfahren baut Basisprimzahlen bis
  √N auf. Bei großem `high` (nahe 2⁶⁴) wächst der Speicherbedarf stark
  (Bitset + Primzahl-Array je Thread). Vor dem Start wird der geschätzte
  Bedarf gegen den **verfügbaren RAM** geprüft; reicht er nicht, bricht das
  Programm mit Schätzung und Alternativen ab:

  ```text
  Fehler: Obergrenze … ist fuer das Sieb-Verfahren zu gross.
          Geschaetzter Basisprimzahl-Speicher ~1.26 GB pro Thread x 8 = ~10.1 GB,
          verfuegbar sind nur ~34.2 GB.
          Bitte -m miller oder -m trial verwenden (oder -j reduzieren).
  ```

- Für **sehr große Zahlen** oder **kleine Bereiche nahe großer Werte** sind
  `-m miller` oder `-m trial` deutlich besser geeignet (sie prüfen einzelne
  Kandidaten, statt bis √N zu sieben).

## ASCII-Tabelle mit Berechnungszeit (`-t`)

Erzeugt eine Tabelle mit den Spalten **`Primzahl`** und **`Berechnungszeit`**
(Zeit, die der jeweilige Thread zum Auffinden dieser Primzahl benötigt hat, in
**ms**). Die einmalige Setup-Zeit wird nicht einer einzelnen Primzahl
zugeschrieben; die Markierzeit des Siebs wird anteilig auf die gefundenen
Primzahlen verteilt.

```text
> primes -t -m miller -c 8
+----------+-----------------+
| Primzahl | Berechnungszeit |
+----------+-----------------+
|        2 |        0.000 ms |
|        3 |        0.003 ms |
|        5 |        0.001 ms |
|        7 |        0.001 ms |
|       11 |        0.001 ms |
|       13 |        0.000 ms |
|       17 |        0.001 ms |
|       19 |        0.000 ms |
+----------+-----------------+
Anzahl: 8, Zeit: 0.000 s, Verfahren: miller, Threads: 1
```

Die Tabelle geht auf `stdout`, die Zusammenfassung auf `stderr`.
`-t` und `-q` schließen sich aus (die Fehlermeldung erklärt es in einem Satz).

## Farbcodierte Threads (ANSI 256)

Bei Multithreading erhält **jeder Thread** eine Farbe aus dem ANSI-256-Farbraum:

- Die Farben liegen **möglichst weit auseinander** (Farthest-Point-Auswahl im
  RGB-Raum des 6×6×6-Farbwürfels).
- Die **16 Grautöne sind verboten** (neutrale `r=g=b`-Farben werden
  ausgeschlossen).
- Sonderfall: Bei **nur einem Thread** ist die Farbe **weiß**.

In der **Live-Zeile** sind Label und Wert je Thread eingefärbt:

```text
CPU/Thread: T01= 94% T02= 94% T03= 94% ... T12= 94% | RAM: 28.8 MB
```

In der **`-t`-Tabelle** werden die **vertikalen Balken** jeder Zeile in der
Farbe des Threads gezeichnet, der den Wert berechnet hat:

```text
| Primzahl | Berechnungszeit |   ← Balken je Zeile unterschiedlich eingefärbt
```

Farben sind auf echten Konsolen automatisch aktiv. Bei Umleitung:
`PRIMES_COLOR=1` erzwingt sie.

## Live-Fortschritt (CPU je Thread + RAM + Extrapolation)

Während der Berechnung zeigt `stderr` in einer **stehenden Zeile**
(Wagenrücklauf) die CPU-Last je Thread und den RAM-Verbrauch. Die Live-Zeile
erscheint in **allen Berechnungsarten, außer bei `-q`**.

```text
CPU/Thread: T01= 94% T02=100% T03= 97% ... / RAM: 28.8 MB
```

Der Trenner zwischen Thread- und RAM-Anzeige **rotiert** während der
Berechnung durch `|` → `/` → `-` → `\`.

Die Spalten sind **fest** (Thread-Index und Prozentwerte in fester Breite),
damit die Zeile durch wechselnde Zahlenlängen nicht springt; `=` und `:`
stehen an fixen Positionen.

### Extrapolation

Die Extrapolation steht **immer** am Ende der Live-Zeile. Solange die erste
Extrapolation noch nicht erfolgt ist, werden **alle Zahlenpositionen durch
`-` ersetzt**:

```text
... | RAM: 28.8 MB | --% / --:--:-- sek von --:--:-- sek
```

Ab ca. **10 % der in den Threads bearbeiteten Zahlen** (nicht der
ausgegebenen Primzahlen) wird sie durch echte, **live** laufende Werte ersetzt.
Bei großem `high` zählt dabei auch der Basisprimzahl-Aufbau bis √(high) mit,
der dort die Laufzeit dominiert. Die Streichschritte werden dabei
**stride-gewichtet** (Cache-Line-Effekt) gezählt und die Schätzung geglättet,
sodass die Dauer sofort realistisch ist:

```text
... | RAM: 28.8 MB | 13% / 00:00:39 sek von 00:05:00 min
                    ^       ^              ^
               Prozent   verstrichen   extrapolierte Gesamtdauer
```

Das Einheitenkürzel richtet sich nach der Größenordnung (für verstrichene
**und** extrapolierte Zeit): `< 60 s` → `sek`, `< 1 h` → `min`, sonst `std`.
Im obigen Beispiel ist die verstrichene Zeit 39 Sekunden (`sek`) und die
extrapolierte Dauer 5 Minuten (`min`).

- Die erwartete Dauer wird ab 10 % **alle weiteren 5 %** neu extrapoliert und
  zusätzlich **kontinuierlich nach oben korrigiert** (rot), falls die reale Zeit
  sie überholen würde – sie liegt dadurch nie unter der verstrichenen Zeit.
- Ihre Farbe richtet sich nach der Tendenz:
  - **rot** – länger als die vorherige Schätzung
  - **grün** – kürzer als die vorherige Schätzung
  - **weiß** – erste Schätzung oder Abweichung ≤ 5 %
- Prozent und verstrichene Zeit werden bei jeder Aktualisierung (~10×/s)
  fortgeschrieben.

Damit die Live-Zeile stabil bleibt und nicht flackert, wird sie **in-place
überschrieben** (ohne Zeilenlöschen). Beim Konsolen-Streaming werden die
Primzahlen kurz **gepuffert** und zusammen mit der Statuszeile nur ~10×/s
ausgegeben. Bei umgeleitetem `stdout` wird direkt geschrieben – die Datei
bleibt sauber (nur Primes), die Live-Zeile bleibt auf `stderr`.

- Erzwingen (z. B. zum Testen/Umlenken): `PRIMES_PROGRESS=1`.

## Multithreading (`-j` / `--mt`)

```bat
primes -m sieve -j 4   -q 1000000000   REM 4 Threads
primes -m sieve --mt   -q 1000000000   REM alle CPU-Kerne
```

Der Bereich wird in zusammenhängende Blöcke geteilt, jeder Thread berechnet
einen Block, anschließend wird **in aufsteigender Reihenfolge** ausgegeben –
das Ergebnis ist deterministisch und identisch zur Single-Thread-Ausgabe.

Beispiel-Skalierung (Eratosthenes bis 10⁹, 50.847.534 Primzahlen):

| Threads | Zeit    |
|---------|---------|
| 1       | 0.92 s  |
| 4       | 0.42 s  |
| auto    | 0.25 s  |

## Beispiele

```bat
primes -t 100
primes -m atkin -q 100000000
primes -m sieve -j 8 -q 1000000000
primes -m miller --mt -q -r 1000000000000 1000001000000
```

## Funktionsweise

1. **Segmentiertes Sieb (Standard):** Nur ungerade Zahlen, Blöcke von ~4 Mio.
   Zahlen. Speicher konstant, auch bei großen Obergrenzen. Für große *Bereiche*
   werden nur die Basisprimzahlen bis `sqrt(high)` benötigt.
2. **Atkin / Sundaram:** vollständige Siebe mit Bit-Array (kompakte Speicherung).
3. **Probedivision:** Test durch 2, 3 und alle `6k ± 1` bis `sqrt(n)`.
4. **Miller-Rabin:** 7 Basen `{2,325,9375,28178,450775,9780504,1795265022}`,
   beweisbar korrekt für alle 64-Bit-Zahlen. Multiplikation modulo über
   `_umul128`/`_udiv128` (kein Overflow).
5. **Erste N Primzahlen:** Obergrenze über `n·(ln n + ln ln n)`.
6. **Overflow-sichere Tests** (`p > n / p` statt `p*p > n`).

Korrekt getestet u. a. gegen bekannte Werte (alle fünf Verfahren liefern
bit-identische Ergebnisse):
`π(10⁶) = 78498`, `π(10⁸) = 5761455`, 100000. Primzahl = 1299709,
`π` im Bereich `[10¹², 10¹²+10⁶] = 36249`.

## Umgebungsvariablen

| Variable          | Wirkung                                            |
|-------------------|----------------------------------------------------|
| `PRIMES_COLOR=1`  | Farben erzwingen (auch bei umgeleitetem stdout)    |
| `PRIMES_PROGRESS=1` | Live-Zeile erzwingen (auch ohne Konsole)         |

## Hinweis: unsignierte Binärdatei / AV-Fehlalarme

Die EXE ist **nicht digital signiert** und wird aus Quellcode gebaut. Manche
Virenscanner melden dafür **generische Heuristik-Fehlalarme** (z. B.
`Trojan.Malware.…susgen`), obwohl das Programm ausschließlich Primzahlen
berechnet. Gegenmaßnahmen:

- **Aus dem Quellcode selbst bauen** (`build.bat`) – der Quellcode ist offen.
- Die Binärdatei stammt aus **GitHub Releases** (nicht aus dem Repo); Hash prüfen.
- Fehlalarme ggf. beim jeweiligen Hersteller melden.

## Dateien

- `primes.c` – Quellcode (eine Datei)
- `build.bat` – Build-Skript
- `changelog.md` – laufender Changelog (seit 1.0.0)
- `LICENSE` – MIT-Lizenz
- `.github/workflows/` – CI (Build) und Release (EXE-Anhang bei Tag `v*`)
