Dieses Projekt wurde zuletzt geändert am: 12.07.2024

## Überblick

Der AFL-BufferFuzzer ist ein auf AFL basierender Fuzzer, der speziell für die Aufdeckung von Buffer Overflows konzipiert ist. Er verwendet eine Instrumentierung, die zur Laufzeit Informationen über Speicherzugriffe des System under Test (SUT) sammelt. Für das Erkennen von Buffer Overflows, Buffer Overreads und Index out of Bounds Fehler werden keine weitere Sanitizer benötigt.

## Verwendung

Um den Fuzzer zu verwenden wird die clang und llvm Version 12 benötigt. Diese kann auf Ubuntu 22 LTS Systemen und frühere Ubuntu Versionen über den apt Paketmanager installiert werden:

```bash
sudo apt install clang-12 llvm-12
```

Es ist wichtig, dass exakt diese Version verwendet wird. Wenn beim Bauen des Projekts etwas fehlschlägt, liegt es höchstwahrscheinlich daran, dass eine andere LLVM-Version verwendet wurde.

Nun müssen Symbolische Links auf die zuvor installierten LLVM-Tools erstellt werden, damit sie beim kompilieren gefunden werden:

```bash
sudo ln -s /usr/bin/clang-12 /usr/bin/clang
sudo ln -s /usr/bin/clang++12 /usr/bin/clang++
sudo ln -s /usr/bin/llvm-config-12 /usr/bin/llvm-config
```

Außerdem werden die Build-Essential-Tools benötigt, falls diese nicht bereits installiert sind. Diese können mit folgendem Befehl installiert werden:

```bash
sudo apt build-essentials
```

Anschließend kann das Projekt gebaut werden. Dazu muss im Hauptverzeichnis des Projekts folgende Befehle ausgeführt werden:

```bash
make
cd llvm_mode
make
```

Bevor ein Fuzzing run gestartet werden kann, muss die im Projekt befindene Skript setup.sh ausgeührt werden:

```bash
sudo sh setup.sh
```

Um das Skript auszuführen werden sudo Rechte benötigt, da durch das Skript Systemeinstellungen durchgeführt werden, die für das Fuzzing wichtig sind. Das Skript führt dazu, dass die CPU-Modus auf Performance gesetzt wird, wordurch die CPU immer mit maximaler Leistung läuft. Außerdem wird das das Core-Dump-Muster auf "core" gesetzt, was bedeutet, dass Core-Dumps im aktuellen Verzeichnis als "core" gespeichert werden. 

Bevor ein Program mit dem AFL-BufferFuzzer getestet werden kann muss das System under Test mit dem afl clang-Wrapper kompiliert werden. Dabei wird afl-clang-fast für C und afl-clang-fast++ für C++ Programme verwendet. Ein Beispielhafter Aufruf sieht wie folgt aus:

```bash
afl-clang-fast main.c -o program 
```

Beim Aufruf des Compilers wird ausgegeben wie viele Buffer im Programm gefunden wurden, und um welche Art von Buffer es sich dabei handelt.

Wenn ein Programm mit AFL-BufferFuzzer getestet werden soll, werden beispielhafte Inputs für das Programm benötigt. Diese werden in ein Verzeichnis gespeichert, dass an den Fuzzer beim Start übergeben wird.

Jetzt kann mit dem Fuzzing begonnen werden. Dies geschieht mit dem Programm afl-fuzz. Ein Aufruf des Programm könnte wie folgt aussehen:

```bash
afl-fuzz -m none -i input_dir -o output_dir -- ./program @@
```
Die Parameter haben dabei die folgende Bedeutung:
- `-m none`: Deaktiviert die Speicherbegrenzung für den Fuzzing-Prozess.
- `-i input_dir`: Eingabeverzeichnis, welches beim Aufruf mindestens eine Datei enthalten muss, die als beispielhafte Eingabe für das Programm dient.
- `-o output_dir`: Hier werden die Ergebnisse des Fuzzers gespeichert, darunter befinden sich die Queue, Crashes und Timeouts, sowie einige Performance-Daten.
- `--`: Trennzeichen.
- `./program`: Das Programm welches mit dem Fuzzer getestet werden soll.
- `@@`: Ein Platzhalter, der vom Fuzzer durch den Pfad zur aktuellen Eingabedatei ersetzt wird. Das bedeutet, das Programm nimmt eine Eingabedatei als Konsolenargument entgegen.

## Beispiel:

In dem Verzeichnis target/ befindet sich ein Fehlerhaftes Programm, dass als Beispiel für die Verwendung des Fuzzers dient. In der ./target/main.c Datei befindet sich ein Aufruf der Funktion memcpy, die zu einem möglichen Buffer Overflow führen kann. Im folgenden finden sich die Befehle, um das Programm zu kompilieren und anschließend zu fuzzen.

```bash
sudo sh setup.sh
afl-clang-fast ./target/main.c -o ./target/program
afl-fuzz -m none -i ./target/input_dir -o ./output_dir -- ./target/program @@
```

## Welche Programmteile wurden im Vergleich zu AFL modifiziert?

Der Quellcode für die Intrumentierung für das Erkennen von Speicherzugriffen befindet sich in den folgeenden Dateien:

- `BufferMonitor.cpp`
- `BufferMonitorLib.c`
- `HashMap.h` und `HashMap.c`

Damit der Pass verwendet wird wurden entsprechende Anpassungen in der Datei `afl-clang-fast.c` vorgenommen.

Für die Implemenetation der Fuzzing-Logik, welche die Daten über Speicherzugriffe verwendet wurde die Datei  `afl-fuzz` angepasst. Die wichtigsten Funktion die angepasst oder neu hinzugefügt wurden sind dabei

- `calculate_score_buffer_map`
- `calculate_favored_entries`
- `update_buffer_distances`
- `save_if_interesting_custom`



