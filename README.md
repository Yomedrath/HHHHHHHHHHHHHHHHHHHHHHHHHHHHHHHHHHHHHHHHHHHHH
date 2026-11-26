# proto1v1

Eigenständiger 1-gegen-1-Prototyp mit Melee-artiger Bewegung, vier Kämpfern und sechs
Stage-Layouts (die fünf neutralen plus Pokémon Stadium). Die Grafik besteht aus Platzhaltern (Rechtecke). Es werden **keine Assets,
kein ISO und kein Code aus dem Spiel** verwendet, nur öffentlich dokumentierte Zahlenwerte.
Läuft unter Windows, Linux und macOS und baut mit GCC, Clang oder MSVC.

![Screenshot](docs/screenshot.png)

## Download

Unter **Releases → „Neuester Build“** liegt nach jedem Push automatisch:
- `proto1v1-windows-x64.zip`
- `proto1v1-linux-x64.tar.gz`
- `proto1v1-macos-universal.zip` (unsigniert; beim ersten Start Rechtsklick → Öffnen)

## Kämpfer

Die Bewegungswerte entsprechen den dokumentierten Melee-Attributen (SmashWiki, NTSC): Gewicht,
Dash, Run, Walk, Traction, Luftgeschwindigkeit, Luftbeschleunigung, Luftreibung, Gravitation, Fall-
und Fastfall-Speed, Jumpsquat sowie Full-Hop-, Short-Hop- und Doppelsprung-Höhe.

| Name | Vorbild |
|---|---|
| Fuchs | Fox |
| Birdie | Captain Falcon |
| Assassin | Sheik |
| Duelist | Marth |

Noch Platzhalter: Hurtbox-Größen, Walk-Beschleunigung und die Angriffe. Alle Kämpfer teilen sich
Jab, Strong und Nair mit eigenen Werten.

## Stages

BF-, FD-, YS-, DL-, FoD- und PS-Layout mit gemessenen Kantenpositionen, Plattformhöhen und
-breiten und Blastzones. Die FoD-Seitenplattformen stehen fest. PS gibt es nur in der Standardform
ohne Verwandlungen („frozen“). Die Unterseiten der Hauptplattformen sind vereinfacht.

## Airdodge (wie Melee)

Die Logik folgt `ftCo_EscapeAir` aus dem Melee-Decomp:
- **Momentum:** Das alte Momentum wird komplett ersetzt.
- **Deadzone:** Bleiben |x| und |y| des Sticks unter 0.2875, bleibst du auf der Stelle.
- **Richtung:** Sonst fliegst du mit 3.1 in Stick-Richtung. Nur der Winkel zählt, nicht die Auslenkung.
- **Bremsen:** Bis Frame 29 wird jeden Frame mit 0.9 multipliziert, ohne Schwerkraft. Danach greift
  die normale Luftphysik.
- **Unverwundbar:** Frame 4–29.
- **Dauer:** 49 Frames, danach bist du hilflos (Freefall). Kanten greifst du erst nach Ende der
  Animation.
- **Landung:** Landest du während des Airdodge, folgen 10 Frames Landing Lag (Wavedash/Waveland).

## Steuerung

| | GameCube-Controller / Gamepad | Tastatur P1 | Tastatur P2 |
|---|---|---|---|
| Bewegen | Stick | WASD (Shift = Gehen) | Pfeile (RShift = Gehen) |
| Angriff | A (Stick zur Seite = Strong) | J | Num 1 |
| Sprung | X / Y / Stick hoch | Space / I | Num 0 / Num 5 |
| Airdodge / Wavedash | L / R | L | Num 2 |
| Pause | Start | Enter | Num Enter |

F1 zeigt Zustände an, Esc führt zurück ins Menü. Im Menü lässt sich P2 auf CPU stellen.

Funktionen: Dash, Dashdance, Run, Turn, Crouch, Short Hop und Full Hop, Doppelsprung, Fastfall,
Airdodge, Wavedash, Waveland, Plattform-Drop, Ledge-Grab mit Getup und Ledge-Jump, Knockback mit
Hitstun, KOs und 4 Stocks.

## Replays und Videos

- **Replays:** Jedes Match wird automatisch als Replay in `replays/` gespeichert, wenige KB pro
  Match. Die Simulation ist deterministisch, deshalb reichen die Eingaben.
- **Replay ansehen:** Die `.p1v1r`-Datei auf `proto1v1` ziehen oder `proto1v1 datei.p1v1r` aufrufen.
- **Video:** `proto1v1 --video match.mp4 --replay replays/<datei>.p1v1r` rendert ein MP4 in
  1280×720 mit 60 fps. Dafür muss [ffmpeg](https://ffmpeg.org) im PATH liegen.
- **Video ohne Replay:** Ohne `--replay` wird ein CPU-gegen-CPU-Match gerendert, mit
  `--stage 1..6 --p1 0..3 --p2 0..3`.
- **Live aufnehmen:** Das geht natürlich auch mit OBS.

### GameCube-Adapter (offizieller / Mayflash im Wii-U-Modus)
- **Windows:** Den WinUSB-Treiber mit Zadig installieren, genau wie für Dolphin.
- **Linux:** Die udev-Regel aus der Dolphin-Doku einrichten, damit der Adapter ohne root lesbar ist.
- **macOS:** Funktioniert in der Regel direkt.

## Selbst bauen

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # SDL3 wird bei Bedarf automatisch geladen
cmake --build build --config Release
ctest --test-dir build -C Release                # Headless-Physiktests
./build/proto1v1
```

Ohne Fenster lässt sich ein Bild rendern:
`proto1v1 --screenshot out.bmp --stage 1..6 --frames 600 --p1 0..3 --p2 0..3`.

## Aufbau

- `src/core/`: Simulation ohne Abhängigkeiten (fester 60-Hz-Takt, deterministisch)
- `src/app/main.c`: SDL3-Frontend (Fenster, Eingabe, Platzhalter-Grafik, Menü)
- `src/core/replay.c`: Input-Replays
- `tests/sim_test.c`: prüft Sprunghöhen gegen die Melee-Werte, den Airdodge (Deadzone, Strecke,
  Unverwundbarkeit, Dauer), Wavedash, Ledge, Treffer, ob Replays deterministisch sind, und
  komplette CPU-gegen-CPU-Matches auf allen Stages
