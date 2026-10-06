# Changelog – primes

Alle nennenswerten Änderungen an diesem Projekt, laufend gepflegt.
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).

## Versionsschema

| Stelle | Beispiel | Bedeutung                         |
|--------|----------|-----------------------------------|
| 1.     | `2.x.y`  | kompletter Code-Rewrite           |
| 2.     | `x.1.y`  | Update von Hauptfeatures          |
| 3.     | `x.y.1`  | Hotfixes                          |

> Diese Konvention gilt ab Version 2.0.1 und wird **rückwirkend** auf die
> gesamte Historie seit 1.0.0 angewandt.

---

## [2.1.8] – 2026-10-06

### Behoben
- **Live-Zeile: falsches Einheitenkürzel.** Die Einheit war fest verdrahtet
  (`sek` für die verstrichene, `min` für die extrapolierte Zeit), sodass z. B.
  22 Sekunden als „00:00:22 min" erschienen. Jetzt richtet sich die Einheit
  nach der Größenordnung – für verstrichene **und** extrapolierte Zeit:
  - `< 60 s` → `sek`
  - `< 1 h`  → `min`
  - sonst   → `std`

---

## [2.1.7] – 2026-10-06

### Behoben
- **Irreführende Fehlermeldung bei zu großen Zahlen** (z. B.
  `-r 100000000000000000000 …` → „erwartet zwei Zahlen A B"): `parse_u64`
  unterscheidet jetzt **Überlauf** (`ERANGE`, Wert > 2⁶⁴−1) von ungültiger
  Eingabe. Die Meldung nennt das betroffene Argument (Grenze A/B, Obergrenze,
  Anzahl) und den Maximalwert:
  `Fehler: Grenze A (-r) … ist zu gross (max. 18446744073709551615 = 2^64-1).`

### Hinzugefügt
- **Machbarkeitsprüfung für das Sieb-Verfahren:** Der geschätzte
  Basisprimzahl-Speicher (Bitset + Primzahl-Array je Thread) wird vor dem Start
  gegen den verfügbaren RAM geprüft. Bei Überschreitung erfolgt ein klarer
  Abbruch mit Schätzung und Alternativen (`-m miller`/`-m trial`, `-j`
  reduzieren). `miller`/`trial` sind davon nicht betroffen.
- **Hilfe/Doku:** gültiger Zahlenbereich `0 … 18446744073709551615 (2⁶⁴−1)`
  und Empfehlung für sehr große Zahlen dokumentiert.

---

## [2.1.6] – 2026-10-05

### Behoben
- **Live-Zeile:** Die reale Zeit konnte die extrapolierte Gesamtdauer
  überholen (z. B. `100% / 00:00:49 sek von 00:00:22 sek`). Ursache: Die
  Schätzung wurde nur an den 5-%-Schwellen neu berechnet; wenn der Fortschritt
  am Ende nur noch langsam vorankam, wurde keine Schwelle mehr erreicht und die
  Schätzung „fror ein".
  - Die erwartete Dauer wird jetzt zusätzlich **kontinuierlich nach oben
    korrigiert** (rot dargestellt), sodass die verstrichene Zeit die
    extrapolierte Dauer nie mehr überschreitet.
  - Verifiziert: über zwei lange Läufe (50 s bzw. 27 s) gab es **0 Zeilen**
    mit `elapsed > expected`; die Schätzung konvergiert auf die reale Dauer.

---

## [2.1.5] – 2026-10-05

### Geändert
- **Dauerabschätzung der Live-Zeile** deutlich verbessert:
  - Die Streichschritte des Basisprimzahl-Siebs werden jetzt **stride-gewichtet**
    gezählt (Kosten pro Schreibzugriff steigen mit dem Sprung, Cache-Line-Effekt;
    Sättigung bei ~512). Der Gesamtaufwand wird **exakt** vorab berechnet
    (identische Gewichtung → exakte Normierung).
  - Die Schätzung wird **geglättet** (EMA).
  - Ergebnis: Die Schätzung ist **sofort realistisch** – im Beispiel
    `-r 10¹⁹ … -j 8` bei 10 % ca. **29 s** (tatsächlich 25,6 s) statt vorher
    anfänglich 4 s.
- **`-t`-Tabelle:** Berechnungszeit in **ms mit drei Nachkommastellen**.

### Hinzugefügt
- **Rotierender Trenner** in der Live-Zeile zwischen Thread- und RAM-Anzeige:
  `|` → `/` → `-` → `\`.

---

## [2.1.4] – 2026-10-05

### Behoben
- **Extrapolation erschien bei großem `high` / kleinem Bereich nie** (dauerhaft
  nur Platzhalter): Ursache war, dass der **Basisprimzahl-Aufbau bis √(high)**
  die Laufzeit dominierte (z. B. `-r 10¹⁹ 10¹⁹+1000`: Sieb bis ~3,16 Mrd.,
  ~1,5 GB RAM, ~25 s), aber nicht in den Fortschritt einging – der Fortschritt
  zählte nur die paar Bereichszahlen und blieb daher bei 0 %.
  - Der Basisprimzahl-Aufbau wird jetzt in den Fortschritt **einbezogen**
    (grobe Aufwandsschätzung der Streichungen; Summe über Primzahlen ≤ √r).
  - Ergebnis: Die Extrapolation erscheint ab ~10 % und läuft bis 100 % durch.
    Im Beispiel `-r 10¹⁹ … -j 8` konvergiert sie von anfänglich 4 s auf die
    tatsächlichen **25 s**.

---

## [2.1.3] – 2026-10-05

### Behoben
- **Live-Zeile flackert nicht mehr:**
  - Die Statuszeile wird nur noch **in-place überschrieben** (kein
    `ESC[K`-Zeilenlöschen mehr) und bei kürzerem Text aufgefüllt.
  - Die Primzahlausgabe wird im Konsolen-Streaming **gepuffert** und nur
    noch ~10×/s (bzw. bei vollem Puffer) zusammen mit der Statuszeile
    ausgegeben. Dadurch wird die Live-Zeile nicht mehr pro Primzahl neu
    geschrieben.
- **Extrapolation erscheint jetzt bei ca. 10 % der bearbeiteten Zahlen**
  (nicht mehr erst kurz vor Ende): Das Sieb verwendet ein **adaptives
  Segment** (Ziel ~256 Blöcke, 16 KiB–4 MiB) statt fester 4-MiB-Blöcke, und
  der Fortschritt wird zusätzlich innerhalb des Scans fein fortgeschrieben.
- **Tabellenzeiten (`-t`) realistisch:** Die einmalige Setup- und die
  Markierzeit wird nicht mehr der ersten Primzahl zugeschrieben, sondern
  proportional zur Kandidaten-Lücke auf die gefundenen Primzahlen verteilt.
  Die Ausgabe-/Pufferzeit (`realloc`) wird nicht der nächsten Primzahl
  zugerechnet.

### Behoben (intern)
- Regressionsfehler behoben: adaptives Segment konnte **ungerade** werden und
  damit die Ausrichtung des odd-only-Siebs brechen (falsche Ergebnisse bei
  einigen Single-Thread-Läufen). Segmentgröße wird nun gerade gehalten.

---

## [2.1.2] – 2026-10-05

### Hotfixes
- **`-q`/`--quiet`** gibt jetzt **ausschließlich die Zusammenfassung** aus
  (auf `stdout`) – sonst nichts: kein separater Anzahl-Wert, keine
  Primzahlen, keine Live-Zeile.
- **`-q` + `-t`** ist unzulässig; die Fehlermeldung erklärt das in **einem
  Satz**.
- Die **Extrapolation** der Live-Zeile wird **immer** angezeigt. Solange die
  erste Extrapolation noch nicht erfolgt ist, werden **alle Zahlenpositionen
  durch `-`** ersetzt: `--% / --:--:-- sek von --:--:-- min`.
- Klargestellt/sichergestellt: Die Extrapolation beginnt bei ca. **10 % der in
  den Threads bearbeiteten Zahlen** (Fortschrittszähler), **nicht** bei 10 %
  der ausgegebenen Primzahlen.

---

## [2.1.1] – 2026-10-05

### Hinzugefügt
- **Live-Zeile in allen Berechnungsarten** – sie erscheint jetzt immer, außer
  bei `-q`/`--quiet`.
- **Extrapolation des Fortschritts** in der Live-Zeile:
  - Ab ca. **10 %** der zu testenden Zahlen wird am Ende angezeigt:
    abgeschlossene **Prozent** (live), **verstrichene Zeit** (live) und die
    **extrapolierte Gesamtdauer**. Format z. B.:
    `13% / 00:00:39 sek von 00:05:00 min`
  - Alle weiteren **5 %** wird die Gesamtdauer neu extrapoliert.
  - Farbe der extrapolierten Dauer: **rot** (länger als zuvor), **grün**
    (kürzer als zuvor), **weiß** (erste Extrapolation oder Abweichung ≤ 5 %).

### Behoben / Geändert
- Die Live-Zeile zerstört die Primzahlen-Ausgabe nicht mehr: Vor jeder
  Primzahlzeile wird die Statuszeile kurz entfernt und danach wieder
  daruntergesetzt (Synchronisation zwischen Rechen- und Status-Thread).
- Fortschrittsermittlung je Verfahren; Fehler in `trial`/`miller` behoben
  (Fortschritt wurde nur ungerade fortgeschrieben).

---

## [2.1.0] – 2026-10-05

### Hinzugefügt
- **Farbcodierte Threads** (ANSI 256):
  - Bei Multithreading erhält jeder Thread eine im Farbraum **möglichst weit
    entfernte** Farbe; die **16 Grautöne sind ausgeschlossen**.
  - Sonderfall: Bei nur **einem Thread** ist die Farbe **weiß**.
  - In der **Live-Zeile** werden Thread-Label und -Wert in der jeweiligen
    Farbe dargestellt.
  - In der **`-t`-Tabelle** werden die **vertikalen Balken** jeder Zeile in
    der Farbe des Threads gezeichnet, der den Wert berechnet hat.
- Farben werden auf echten Konsolen automatisch aktiviert; zum Erzwingen
  (z. B. bei Umleitung) dient `PRIMES_COLOR=1`.

### Hotfixes
- **`-p`/`--per-prime` entfernt** (durch die `-t`-Tabelle redundant).
- Die **Zusammenfassung** (Anzahl/Zeit/Verfahren/Threads) wird jetzt **immer**
  ausgegeben, nicht mehr nur bei `-t`.
- **Live-Zeile springt nicht mehr**: Thread-Label, `=` und Prozentwerte haben
  feste Spaltenbreiten, sodass `=` und `:` an fixen Positionen stehen.

### Geändert
- `-t` und `-q` schließen sich weiterhin aus (jetzt die einzige
  Ausschlusskombination).

---

## [2.0.1] – 2026-10-05

### Hinzugefügt
- **Versionsschema** (Rewrite/Features/Hotfixes) eingeführt und dokumentiert.
- **`-t`/`--time`** gibt die Ergebnisse jetzt als **ASCII-Tabelle** mit den
  Spalten `Primzahl` und `Berechnungszeit` aus.
- Die Spalte `Berechnungszeit` zeigt die Zeiten in **Millisekunden inkl.
  Einheitenkürzel `ms`**.
- **Live-Fortschrittszeile** während der Berechnung (auf `stderr`, via
  Wagenrücklauf): **CPU-Auslastung je genutztem Thread** und **RAM-Verbrauch**
  des Programms.
- Diese `changelog.md` als laufender Changelog (rückwirkend ab 1.0.0).

### Geändert
- `-t` erzeugt keine reine Zusammenfassung mehr, sondern die Tabelle;
  die Zusammenfassung erscheint zusätzlich auf `stderr`.
- `-q`/`--quiet` schließt `-t` und `-p` aus.
- `-p`/`--per-prime` gibt die Dauer mit Einheit an (`ms`).

### Behoben / Optimiert
- Multithreading im Nur-Zählen-Modus (`-q`) puffert keine Primzahlen mehr:
  RAM bei `-m sieve -j 4 -q 1000000000` von ~334 MB auf ~12 MB reduziert.

---

## [2.0.0] – 2026-10-05

### Hinzugefügt
- Auswahl verschiedener **mathematischer Verfahren** über `-m`/`--method`:
  - `sieve` – Segmentiertes Sieb des Eratosthenes (**Standard**)
  - `atkin` – Sieb des Atkin
  - `sundaram` – Sieb des Sundaram
  - `trial` – Probedivision (6k ± 1)
  - `miller` – Miller-Rabin (deterministisch für 64 Bit)
- `-p`/`--per-prime`: Anzeige der Berechnungsdauer jeder einzelnen Primzahl
  in Millisekunden (in 2.1.0 entfernt).
- **Multithreading** über `-j`/`--threads N` bzw. `--mt` (alle CPU-Kerne);
  deterministische, aufsteigende Ausgabe.
- Speicher-Schutzprüfung für vollständige Siebe (Atkin/Sundaram) mit klarer
  Fehlermeldung statt hängendem System.

### Geändert
- Vollständige Neuimplementierung des Quellcodes (einheitliche
  Methoden-Schnittstelle, Threading, exakte Arithmetik via `_umul128`/
  `_udiv128`).

### Verifikation
- Alle Verfahren liefern bit-identische Ergebnisse.
- `π(10⁶) = 78498`, `π(10⁸) = 5761455`, 100000. Primzahl = 1299709,
  `π([10¹², 10¹²+10⁶]) = 36249`.
- Skalierung `sieve` bis 10⁹: 1 Thread 0.92 s → 4 Threads 0.42 s → auto 0.25 s.

---

## [1.0.0] – 2026-10-05

### Hinzugefügt
- Erstveröffentlichung: schlanke Windows-x64-Kommandozeilenanwendung.
- Segmentiertes Sieb des Eratosthenes (odd-only, speicherschonend).
- Modi: Obergrenze (`<N>`), erste N (`-c`), Bereich (`-r`).
- `-q`/`--quiet` (nur Anzahl), `-t`/`--time` (Laufzeit/Anzahl auf `stderr`),
  `-h`/`--help`, `-v`/`--version`.
- Statisch gelinkte CRT (`/MT`), einzige DLL-Abhängigkeit `KERNEL32.dll`,
  EXE ca. 149 KB.
