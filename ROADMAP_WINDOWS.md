# Eddy auf Windows – Scan und Portierungsroadmap

Stand des Scans: 20.07.2026, Repository-Commit `172d39e` (`main`).

## Kurzfazit

Ein nahezu identischer Windows-Port des **Eddy-Editors** ist realistisch, ohne die Anwendung neu zu schreiben. Die Kernanwendung basiert auf Qt 6 Widgets und besteht überwiegend aus plattformneutralem C++/Qt-Code. Zeichenwerkzeuge, Undo/Redo, Bildexport, Zwischenablage, Konfiguration, OCR-Steuerung und große Teile der Video-Unterstützung können beibehalten werden.

Es gibt jedoch zwei unterschiedliche Ziele:

1. **Eddy 1:1 als Editor:** gleiche Oberfläche, Werkzeuge, Shortcuts, Bild-/Videoeingabe, Copy, Save und Drag-out. Dieses Ziel ist mit überschaubaren, gezielten Änderungen erreichbar.
2. **Gesamter Boltsnap-Workflow 1:1:** Screenshot-Aufnahme, Shelf, Daemon und Rückgabe an die Shelf. Dafür muss zusätzlich das separate Projekt Boltsnap auf Windows portiert werden. Boltsnap ist aktuell Linux-only und seine Shelf nutzt Wayland sowie Unix-Sockets.

Empfehlung: Zuerst Eddy als vollwertigen Windows-Editor fertigstellen. Die Shelf-Anbindung wird hinter eine kleine Plattform-Schnittstelle gelegt und anschließend gemeinsam mit einem Windows-Port von Boltsnap aktiviert. So bleibt Linux unverändert und Eddy muss nicht später erneut umgebaut werden.

## Projektbestand

- Sprache und Build: C++20, CMake 3.21+, Qt 6.
- Qt-Module: Widgets, Svg, Multimedia, MultimediaWidgets, Test; optional GuiPrivate und Wayland Client.
- Umfang: 64 Quelldateien mit rund 4.633 Zeilen, 23 Testdateien mit rund 1.916 Zeilen.
- UI: frameless, always-on-top, dunkles Fusion-Theme, QGraphicsView/QGraphicsScene.
- Werkzeuge: Move, Arrow, Pen, Rectangle, Ellipse, Highlight, Text und vier Redact-Modi.
- I/O: Bilddatei oder stdin; Datei, stdout, Clipboard, Drag-and-drop und Boltsnap-Shelf als Ausgabe.
- Medien: Qt Multimedia für Wiedergabe; `ffprobe` für Metadaten und `ffmpeg` für Videoexport.
- OCR: externer `tesseract`-Prozess mit TSV-Ausgabe.
- Tests: umfangreiche Qt-Test-Suite, standardmäßig mit `QT_QPA_PLATFORM=offscreen`.

## Architektur und Portierbarkeit

### Direkt wiederverwendbar

Diese Bereiche sind bereits Qt-basiert und sollten unverändert oder nur minimal angepasst werden:

- `src/canvas.*`, `src/toolcontroller.*`, `src/undocommands.*`
- `src/items/*`
- `src/selectionhandles.*`
- `src/toolbar.*`, `src/colorpopover.*`, `src/redactbar.*`, `src/toast.*`, `src/loupe.*`
- `src/exporter.*`, große Teile von `src/imageio.*` und `src/mediaio.*`
- `src/theme.*`, Ressourcen, SVG-Icons und QSS
- CLI-Parser und QSettings-Konfiguration

### Plattformabhängig oder unter Windows zu prüfen

| Bereich | Ist-Zustand | Windows-Strategie |
|---|---|---|
| Boltsnap IPC | POSIX `AF_UNIX`, `unistd.h`, Unix-Socketpfad | Transport abstrahieren; Windows Named Pipe über `QLocalSocket`, Linux-Vertrag beibehalten |
| Native Drag-out | Spezieller Wayland-Pfad plus Qt-Fallback | Wayland-Datei nur unter Linux bauen; unter Windows Qt `QDrag`/`QMimeData` verwenden und real testen |
| Fensterregeln | `hyprctl`/`swaymsg` | Unter Windows No-op; Qt-Flags beibehalten, Win32-Verhalten für Fokus/Z-Order prüfen |
| stdin/stdout | POSIX-Binärverhalten vorausgesetzt | Windows CRT explizit mit `_setmode(..., _O_BINARY)` auf Binärmodus setzen |
| Dateiersetzung | `std::filesystem` mit `QString::toStdString()` | Unicode-sichere Qt-Dateioperation oder Wide-Path unter Windows verwenden |
| Video | `ffmpeg`/`ffprobe` aus `PATH` | Programme mitliefern oder konfigurierbar auflösen; Qt-Multimedia-Plugins deployen |
| OCR | `tesseract` aus `PATH` | Tesseract plus gewünschte `tessdata` mitliefern oder bei fehlender Installation sauber deaktivieren |
| Tests | Mehrere feste `/tmp/...`-Pfade und POSIX-Sockettest | `QTemporaryDir`/`QDir::tempPath`; plattformspezifische IPC-Tests |
| Buildflags | Release erzwingt `-O2` | Compilerneutrale CMake-Optionen beziehungsweise MSVC `/O2` |
| Packaging | Noch kein Install-/Deployziel | CMake Install-Regeln, Qt Deployment API/`windeployqt`, ZIP und optional MSIX |

## Konkrete technische Risiken

### 1. Aktueller harter Windows-Compile-Blocker

`src/boltsnapipc.cpp` bindet `sys/socket.h`, `sys/un.h` und `unistd.h` ohne Plattformschutz ein. Das Projekt kann deshalb mit MSVC derzeit nicht kompilieren. Auch `tests/test_boltsnapipc.cpp` ist POSIX-spezifisch.

**Lösung:** Das Protokoll-Frame bleibt plattformneutral. Nur der Transport wird getrennt:

- `ShelfClient` oder eine freie Transportfunktion als gemeinsame API.
- Linux: bestehender Unix-Socket-Transport ohne Verhaltensänderung.
- Windows: Named Pipe über `QLocalSocket`.
- Ohne laufende Shelf: exakt heutiger Fallback auf die Bild-Zwischenablage.

### 2. „1:1 Shelf“ benötigt Boltsnap für Windows

Eddy stellt selbst keine Shelf dar. Der Shelf-Button sendet ein PNG-Frame an den Boltsnap-Daemon. Solange es keinen Windows-Daemon gibt, kann die Oberfläche zwar identisch aussehen, die Shelf-Funktion aber nicht identisch arbeiten.

**Empfohlener Vertrag:** Framing und JSON-Header unverändert lassen; nur den IPC-Transport austauschen. Der spätere Boltsnap-Windows-Port implementiert denselben Named-Pipe-Namen und dasselbe Byteprotokoll. Bis dahin wird der Shelf-Button unter Windows entweder mit verständlichem Hinweis deaktiviert oder löst den vorhandenen Clipboard-Fallback aus.

### 3. Binäre Pipes unter Windows

PNG-Daten über stdin/stdout dürfen nicht durch den Textmodus des Windows-CRT laufen. Andernfalls können Bytes verändert werden.

**Lösung:** Nur auf Windows und nur für verwendete Standardstreams `_setmode(_fileno(stdin), _O_BINARY)` beziehungsweise `_setmode(_fileno(stdout), _O_BINARY)` setzen. Dazu Tests mit einer PNG-Datei über PowerShell und `cmd.exe` ergänzen.

### 4. Unicode-Dateipfade

Die Konvertierung von `QString` nach `std::string` in `videoexporter.cpp` kann unter Windows Pfade mit Umlauten oder nicht-lateinischen Zeichen beschädigen.

**Lösung:** Datei-Ersetzung möglichst vollständig mit Qt-APIs implementieren oder unter Windows Wide Strings verwenden. Abnahmetests müssen Pfade wie `C:\Users\...\Bilder\Grüße 日本語\` enthalten.

### 5. Frameless Window und High-DPI

Qt abstrahiert den Großteil, aber `FramelessWindowHint`, `WindowStaysOnTopHint`, Fokus, Alt+Tab, Snap Layouts, mehrere Monitore und gemischte DPI-Skalierungen verhalten sich unter Windows anders als unter Wayland.

**Lösung:** Zuerst Qt-only testen. Nur falls nötig eine kleine Windows-Fensterintegration ergänzen. Keine Win32-Typen in `EditorWindow` verteilen. Visuelle Referenztests bei 100 %, 125 %, 150 % und 200 % Skalierung durchführen.

### 6. Externe Laufzeitkomponenten

Eddy benötigt für alle Funktionen mehr als nur Qt-DLLs:

- Qt-Plattform-, Bildformat-, SVG- und Multimedia-Plugins.
- Qt-Multimedia-FFmpeg-Bibliotheken für konsistente Videowiedergabe.
- Separate `ffmpeg.exe` und `ffprobe.exe` für Probe und Export.
- `tesseract.exe` und mindestens die konfigurierten Sprachdaten für OCR.

Diese Komponenten müssen lizenzkonform verpackt und in einer definierten Verzeichnisstruktur gesucht werden. Ein zufälliger globaler `PATH` darf für Release-Builds nicht die einzige Strategie sein.

## Zielarchitektur

Die Anwendung bleibt ein gemeinsamer Qt-Codebestand. Plattformcode wird nur an den tatsächlichen Betriebssystemgrenzen getrennt:

```text
src/
  platform/
    shelfclient.h
    windowintegration.h
    stdio.h
    linux/
      shelfclient_unix.cpp
      windowintegration_linux.cpp
    windows/
      shelfclient_namedpipe.cpp
      windowintegration_windows.cpp
      stdio_windows.cpp
```

Nicht jede Datei muss sofort entstehen. Entscheidend ist, dass POSIX- und Win32-Details nicht in UI, Editor, Exporter oder Protokollparser gelangen.

## Roadmap

### Phase 0 – Reproduzierbare Ausgangsbasis

- Referenzscreenshots und kurze Referenzvideos aller Tools unter Linux aufnehmen.
- Aktuelle 23 Tests unter Linux als Baseline grün ausführen.
- Eingabe-/Ausgabe-Golden-Files für PNG, Clipboard, Drag-out und Videooverlay anlegen.
- Unterstützte Windows-Zielversion festlegen; Empfehlung: Windows 10 22H2 und Windows 11, x64.
- Compiler festlegen; Empfehlung: Visual Studio 2022 Build Tools, MSVC x64, CMake und Ninja.

**Abnahme:** Linux-Verhalten und sichtbare UI sind dokumentiert; spätere Windows-Ergebnisse können objektiv verglichen werden.

### Phase 1 – Windows-kompilierbarer Kern

- `CMakeLists.txt` nach Plattform aufteilen, ohne Quellen zu duplizieren.
- GCC-Flag `-O2` compilerabhängig machen.
- Wayland-Abhängigkeiten und `GuiPrivate` ausschließlich auf Unix/Linux suchen.
- Boltsnap-Protokoll von POSIX-Sockettransport trennen.
- Windows zunächst mit einem Shelf-No-op beziehungsweise Clipboard-Fallback bauen.
- POSIX-Test `test_boltsnapipc` unter Windows durch Protokolltest plus separaten Transporttest ersetzen.
- `/tmp`-Annahmen in Tests durch Qt-Temporärverzeichnisse ersetzen.

**Abnahme:** `eddy.exe`, `eddy_preview.exe` und alle plattformneutralen Tests kompilieren mit MSVC; Linux kompiliert weiterhin unverändert.

### Phase 2 – I/O und Windows-Grundfunktion

- Binärmodus für stdin/stdout implementieren.
- Bild laden aus Datei, `-f -`, Copy, `-o FILE`, `-o -` und `--save-dir` testen.
- Unicode- und lange Pfade prüfen und Dateiersetzung korrigieren.
- Qt-Drag-out mit PNG und Datei-URL gegen Explorer, Browser, Discord/Slack-ähnliche Drop-Ziele und Bildeditoren testen.
- Clipboard sowohl als Bild als auch als Datei-URL für Videos prüfen.
- Default-Konfigurationspfad unter `%APPDATA%\eddy\config` dokumentieren; `QStandardPaths` erledigt die Auflösung bereits.

**Abnahme:** Alle Bild-Workflows verhalten sich aus Anwendersicht wie unter Linux; Pipe-Ausgabe ist byteidentisch.

### Phase 3 – 1:1 UI und Eingabeverhalten

- Fenstergröße, Zentrierung, Always-on-top, Fokus und Schließen mit Escape testen.
- Frameless Window auf Windows 10/11 und mehreren Monitoren prüfen.
- Zoom, Pan, Toolbar-Autohide, Farbpipette, Texteditierung und Resize-Handles vergleichen.
- Shortcuts mit deutschem und US-Tastaturlayout testen.
- High-DPI-/DPR-Tests bei gemischten Monitor-Skalierungen durchführen.
- Falls erforderlich nur eine kleine `windowintegration_windows.cpp` für Z-Order oder Zentrierung ergänzen.

**Abnahme:** Referenzscreenshots und Interaktionscheckliste zeigen keine relevanten Abweichungen zum Linux-Original.

### Phase 4 – OCR und Video vollständig machen

- Tool-Auflösung zentralisieren: Anwendungsordner zuerst, danach konfigurierter Pfad, danach System-`PATH`.
- `tesseract.exe` und Sprachdaten für die gewünschten Sprachen paketieren.
- Fehlerzustände für fehlende OCR-Sprache sichtbar und nicht-blockierend behandeln.
- Qt-Multimedia-Backend inklusive benötigter Plugins/DLLs deployen.
- `ffmpeg.exe` und `ffprobe.exe` paketieren und Codec-Matrix testen: MP4, MOV, WebM, MKV und AVI.
- Videoexport mit H.264/AAC, VP9/Opus, Video ohne Audio und Überschreiben der Quelldatei testen.
- Temporärdateien und abgebrochene Exporte aufräumen.

**Abnahme:** Wiedergabe, Annotation, Save, Copy und Drag-out funktionieren für die unterstützte Medienmatrix; OCR liefert dieselben Redact-Ergebnisse innerhalb sinnvoller Toleranzen.

### Phase 5 – Boltsnap-Shelf auf Windows

- Gemeinsamen IPC-Vertrag mit dem Boltsnap-Windows-Port festlegen.
- Named-Pipe-Transport in Eddy mit Timeout, Teilwrites und verständlichen Fehlern implementieren.
- Boltsnap-Daemon auf Windows implementieren oder portieren; dies liegt außerhalb dieses Eddy-Repositories.
- Shelf-Start, Prozesslebensdauer, Tray, Capture und Windows Graphics Capture im Boltsnap-Projekt lösen.
- Eddy-Shelf-Button und Default-Save erst aktivieren, wenn der Windows-Daemon verfügbar ist.
- End-to-End testen: Capture → Shelf → Eddy → Save → neue Shelf-Karte.

**Abnahme:** Der vollständige Workflow ist funktional 1:1; ohne Daemon bleibt Eddy stabil und fällt auf Clipboard zurück.

### Phase 6 – Packaging, CI und Release

- CMake-Installregeln und Qt Deployment API oder `windeployqt` ergänzen.
- Portable ZIP als erstes Release-Artefakt erzeugen.
- Danach optional MSIX/Installer mit Startmenüeintrag, Dateizuordnung und sauberer Deinstallation.
- App-Icon, Versionsressource und Windows-Metadaten ergänzen.
- GitHub Actions Matrix für Ubuntu und `windows-latest` einrichten.
- Unit-Tests headless, Integrationstests auf echtem Windows-Desktop und Release-Smoke-Test ausführen.
- Lizenzdateien für Qt, FFmpeg und Tesseract im Paket aufnehmen.

**Abnahme:** Frische Windows-10/11-VM kann Eddy ohne Entwicklungsumgebung starten; keine DLL oder externe EXE fehlt.

## Empfohlene Reihenfolge der ersten Änderungen

1. CMake-Plattformbedingungen und MSVC-Flags.
2. `boltsnapipc` in Protokoll und Transport trennen.
3. Tests von `/tmp` und POSIX-only APIs bereinigen.
4. Windows-Binärmodus für stdin/stdout.
5. Unicode-sichere Video-Dateiersetzung.
6. Erster MSVC-Build und komplette Qt-Test-Suite.
7. Manuelle UI-, Clipboard- und Drag-out-Prüfung.
8. Erst danach Abhängigkeiten paketieren und Shelf anbinden.

## Grobe Aufwandseinordnung

- **Eddy kompiliert und Bildeditor nutzbar:** klein bis mittel.
- **Eddy standalone weitgehend 1:1 inklusive Video/OCR/Packaging:** mittel.
- **Kompletter Boltsnap-Workflow 1:1:** groß, weil Capture, Shelf, Daemon und Tray im Schwesterprojekt eigene Windows-Backends benötigen.

Die größte Gefahr wäre, Eddy und Boltsnap gleichzeitig ungeplant umzubauen. Die gestufte Strategie liefert früh eine nutzbare `eddy.exe`, bewahrt die Linux-Version und hält den Weg zum vollständigen 1:1-Workflow offen.

## Definition of Done für „Eddy 1:1 auf Windows“

- Alle Werkzeuge und Redact-Modi sehen gleich aus und reagieren gleich.
- Undo/Redo, Auswahl, Verschieben und Resize sind identisch.
- Alle dokumentierten Shortcuts funktionieren.
- PNG-Datei, stdin/stdout, Clipboard, Save und Drag-out funktionieren.
- Video-Wiedergabe und Video-Export funktionieren für die definierte Codec-Matrix.
- OCR funktioniert mit den mitgelieferten Sprachdaten.
- Fenster, Toolbar-Autohide und Farbpipette funktionieren auf mehreren DPI-Stufen.
- Linux-Tests bleiben grün; Windows-CI und Windows-Smoke-Test sind grün.
- Portable Release startet auf einem sauberen Windows-System.
- Für das volle Systemziel funktioniert zusätzlich Boltsnap Capture → Shelf → Eddy → Shelf.

## Lokaler Umsetzungsstand

Für den ersten Windows-Port wurden Visual Studio 2022/MSVC, das mitgelieferte CMake und Qt 6.9.3 mit Multimedia verwendet. Debug- und Release-Build kompilieren nativ; alle 23 Tests laufen unter Windows erfolgreich. Die Named-Pipe-Kompatibilität wurde zusätzlich gegen den laufenden Windows-Boltsnap-Daemon geprüft. Ein portables Qt-Deployment wird unter `dist/eddy` erzeugt. Zusätzlich erzeugt `packaging/windows/build-msi.ps1` eine ICE-validierte x64-MSI mit Qt- und MSVC-Laufzeit, Startmenüeintrag sowie Windows-Dateizuordnungen. Externe `ffmpeg`-/`ffprobe`-Programme und Tesseract sind noch nicht paketiert und bleiben der nächste Funktionsblock für vollständigen Videoexport und OCR.
