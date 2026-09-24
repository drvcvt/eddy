# Screen Studio: Analyse und Übernahme in Eddy

Datum: 2026-09-23. Basis: `main` bei `2910125` plus uncommittete Export-Arbeit
(Fortschritt, Stall-Erkennung, Abbrechen).
Status: **Recherche und Vorschlag. Nichts davon ist implementiert oder abgenommen.**

Quellen sind die öffentliche Screen-Studio-Webseite und der offizielle Guide
(Stand heute), dazu Open-Source-Nachbauten für die Technik. Screen Studio ist
closed source und nur für macOS. Konkrete Zahlenwerte (Zoomstufen, Federkonstanten,
Dauern) veröffentlicht Screen Studio nicht. Solche Werte unten sind eigene
Vorschläge und als solche markiert.

## 1. Kurzfassung

- Screen Studio ist kein klassischer Videoeditor. Es nimmt **Rohvideo ohne Cursor
  plus Eingabe-Metadaten** (Cursorpositionen, Klicks, Tasten) auf und rendert das
  Endvideo erst beim Export aus einem nicht-destruktiven Projekt. Fast alles, was
  "wow" wirkt (Auto-Zoom, gleitender Cursor, Klick-Effekte, Tastenkürzel-Overlay),
  hängt an diesen Metadaten.
- Für **importierte Videos** schaltet Screen Studio genau diese Funktionen selbst ab.
  Übrig bleiben manuelle Zooms, Hintergrund/Rahmen, Schnitt, Speed, Masken, Export.
  Das ist exakt Eddys heutige Lage: Eddy bekommt fertige Dateien.
- Deshalb zwei Ebenen:
  1. **Ohne Metadaten, sofort in Eddy machbar:** Rahmen-Styling (Hintergrund,
     Padding, Rundung, Schatten, Seitenverhältnis) für Bilder und Videos, manuelle
     Zoom-Segmente mit animierter Kamera, Speed-Segmente und mehrere Schnitte,
     zeitlich begrenzte Masken/Highlights, GIF-Export, Presets.
  2. **Mit Metadaten, braucht Boltsnap:** Boltsnap nimmt ohne eingebrannten Cursor auf
     und schreibt eine Cursor-Spur daneben. Dann kann Eddy den Cursor neu zeichnen
     (glätten, vergrößern, ausblenden, Klick-Effekte) und Zooms automatisch vorschlagen.
- Technischer Kern für beides: ein **deterministisches Kompositionsmodell**
  (Zeit → Kamera, Cursor, Stil), das Vorschau und Export identisch auswerten, plus
  ein zweiter Exportpfad, der Frames selbst rendert, statt nur einen statischen
  ffmpeg-Filtergraph zu bauen.

## 2. Wie Screen Studio aufgebaut ist

### Aufnahme

- Aufnahme von Display, Fenster oder Bereich, dazu Webcam, Mikrofon, Systemaudio.
- Der Cursor wird **nicht** ins Video gebrannt. Cursorpositionen, Klicks,
  Scrolls, Fensterwechsel und Tastendrücke landen als Daten im Projekt.
- Ein Projekt ist nicht-destruktiv. Rohdateien lassen sich später extrahieren
  ("Extracting raw recording files").

### Editor-Layout

- Mitte: Vorschau des fertigen Ergebnisses, inklusive Hintergrund und Zoom.
- Rechts: Sidebar mit Panels, u.a. "Background and Screen", Cursor, Camera,
  Audio, Captions, Keyboard Shortcuts, Animations. Einstellungen gelten global
  für das Projekt, außer sie hängen an einem Timeline-Element.
- Unten: Timeline mit **Fragmenten** (Schnittstücke des Clips) und eigenen Spuren
  für Zooms (lila Blöcke), Kamera-Layouts, Masken/Highlights, Tastenkürzel.
  Einstellungen wie Speed oder "Hide mouse cursor" hängen per Rechtsklick an
  einem Fragment.
- Command Menu, Tastenkürzel pro Spur (z.B. `4` öffnet die Masken-Spur), Presets.

### Funktionskatalog

| Funktion | So funktioniert es bei Screen Studio | Braucht Metadaten | Eddy heute |
| --- | --- | --- | --- |
| Auto-Zoom | Zoomt auf Klickpositionen. Ohne Klick im Segment zoomt es nirgendwohin | ja (Klicks) | nein |
| Manueller Zoom | Klick in die Zoom-Spur legt Block an, Ränder ziehen = Dauer. Zielpunkt als Punkt in einer Mini-Vorschau, dazu Zoomstufe. Rechtsklick: Disable/Remove | nein | nein |
| Instant Zoom | Schalter "Instant Animation": Zoom springt ohne Übergang | nein | nein |
| Follow Cursor | Auto-Zoom-Modus folgt dem Cursor, solange der Zoom aktiv ist | ja | nein |
| Kamera-Animation | Zwei Profile: "Focused" (beruhigt sich schnell, gut lesbar) und "Smooth" (flüssiger) | nein | nein |
| Motion Blur | Stärke-Slider, getrennt für Cursorbewegung, Zoom rein/raus, Kameraschwenk | teilweise | nein |
| Glatter Cursor | Zittrige Bewegungen werden zu einer Gleitbahn. Stile "Smooth/Medium/Rapid/None" | ja | nein |
| Cursor-Optionen | Größe, macOS- oder Touch-Stil, bei Stillstand ausblenden, Loop zur Startposition, Rotation beim Bewegen, Wackler entfernen, hochauflösende Systemcursor | ja | nein |
| Cursor pro Abschnitt ausblenden | Rechtsklick auf Fragment, "Hide mouse cursor" | ja | nein |
| Klickgeräusch | Optionaler Sound pro Klick | ja | nein |
| Hintergrund | Wallpaper, Gradient, Farbe, eigenes Bild | nein | nein |
| Rahmen | Padding (0 = kein Hintergrund sichtbar), Rounded corners, Inset, Shadow | nein | nein |
| Seitenverhältnis | Auto, Wide 16:9, Vertical 9:16, Square 1:1, Classic 4:3, Tall 3:4. Optional "Always keep zoomed in": Ausschnitt folgt dem Cursor | teilweise | Crop mit 16:9/9:16/1:1, ohne Hintergrund |
| Crop | Ausschnitt der Aufnahme | nein | ja |
| Trim, Cut | Anfang/Ende, mehrere Schnitte als Fragmente | nein | nur ein In/Out-Bereich |
| Speed | Rechtsklick auf Fragment, "Set speed", Presets | nein | nur Vorschau-Speed |
| Tipp-Abschnitte beschleunigen | Erkennt Tipp-Phasen, Slider, "Apply to all typing parts" | ja (Tasten) | nein |
| Masken/Highlights | Rechteckige Bereiche auf eigener Spur, Größe und Deckkraft. Maske verdeckt, Highlight hebt hervor. Folgen nicht dem Scrollen, nicht beide im selben Frame | nein | Redact und Spotlight, aber statisch über das ganze Video |
| Tastenkürzel-Overlay | Zeigt Shortcuts als Labels, Größe einstellbar, Einzeltasten optional, eigene Spur zum Deaktivieren | ja (Tasten) | nein |
| Webcam | Overlay, Layouts-Spur (Fullscreen/Default/Hidden), Form, Größe, Position, weicht Cursor aus | eigene Aufnahme | nein |
| Captions | Lokal per Whisper (Base/Small/Medium) oder Apple Speech, editierbar, als Datei exportierbar | Mikrofon-Audio | nein |
| Audio | Lautstärke, Normalisierung, Rauschunterdrückung, Hintergrundmusik | nein | Mute im Player, Audio-Plan offen |
| Export | MP4 oder GIF, bis 4K 60 fps, Qualität, Export-Presets für Web/Social/Weiterbearbeitung, Clipboard | nein | MP4/WebM, Clipboard, Shelf, Drag |
| Presets | Speichert Stil-Einstellungen (Hintergrund, Seitenverhältnis, Kamera usw.), teilbar | nein | nein |
| Frame als Bild | "Copy current frame as an image" | nein | ja (Frame-Copy) |
| Share-Links | Cloud-Upload mit Kommentaren | nein | Boltsnap-Shelf statt Cloud |

## 3. Technik: wie man es nachbaut

### 3.1 Kompositionsmodell

Alles Sichtbare im Endvideo ist eine reine Funktion der Zeit:

```
Composition(t) = {
    camera:   Rechteck im Quellvideo, das auf die Ausgabe abgebildet wird
    cursor:   Position, Größe, Sichtbarkeit, Klickzustand (nur mit Metadaten)
    style:    Hintergrund, Padding, Rundung, Schatten, Ausgabegröße
    overlays: Annotationen, Masken, Highlights mit Zeitfenster
}
```

Wichtig ist **Determinismus**: Vorschau und Export müssen für dasselbe `t` exakt
dasselbe liefern. Federanimationen werden deshalb nicht in Echtzeit "abgespielt",
sondern nach jeder Änderung einmal mit festem Zeitschritt (z.B. 240 Hz) von `t = 0`
durchsimuliert und als Kurve gecacht. Vorschau und Export lesen nur noch aus dieser
Kurve. OpenScreen beschreibt dasselbe Prinzip: der Cursorpfad wird für Vorschau und
Export identisch geglättet.

Das Modell gehört in dieselbe speicherbare Form, die der bestehende Plan für
fortsetzbare Projekte vorsieht (`2026-09-21-crop-audio-projects-annotations.md`).

### 3.2 Kamera und Zoom

- **Zoom-Segment:** `start`, `end`, `scale` (Zoomstufe), `target` (fester Punkt oder
  "folge Cursor"), `instant` (ohne Übergang).
- **Zielrechteck:** Aus `target` und `scale` ergibt sich ein Rechteck mit dem
  Seitenverhältnis der Ausgabe. Es wird immer in die Quelle geklemmt, damit nie Rand
  außerhalb des Videos sichtbar wird.
- **Übergang:** Mittelpunkt und Zoomstufe laufen über eine Feder auf das Ziel zu.
  Vorschlag: kritisch gedämpft (Dämpfung 1,0) mit ca. 0,6 s Einschwingzeit für
  "Focused", leicht unterdämpft (ca. 0,85) mit ca. 0,9 s für "Smooth". Zoomstufe
  logarithmisch interpolieren, sonst wirkt das Reinzoomen am Anfang zu schnell.
- **Follow Cursor:** Ziel ist die Cursorposition mit einer Totzone in der Mitte
  (Vorschlag: 20 % der Ausschnittbreite). Die Kamera bewegt sich erst, wenn der
  Cursor die Totzone verlässt. Das verhindert Dauerwackeln.
- **Auto-Zoom-Vorschläge:** Screen Studio nimmt Klicks. OpenScreen kommt ohne Klicks
  aus: es sucht Stellen, an denen der Cursor 0,5 bis 2,6 s fast stillsteht, sortiert
  nach Länge, verwirft Kandidaten näher als 1,8 s an einem anderen Zoom und legt je
  einen 2-s-Zoom mit 1,8× an. Das reicht als erster Algorithmus und braucht nur
  Positionen. Vorschläge sind normale Segmente, die man löschen oder ziehen kann.
- **Motion Blur:** Pro Ausgabeframe mehrere Zwischenzeitpunkte der Kamera rendern und
  mitteln (Vorschlag: 4 bis 8 Samples, nur wenn sich die Kamera bewegt). Teuer, daher
  später und optional.

### 3.3 Cursor (nur mit Metadaten)

- **Eingabe:** Zeitstempel und Position pro Sample, dazu Klick-Events (Taste, runter,
  hoch). Der Cursor ist nicht im Video.
- **Glättung:** Positionen auf ein gleichmäßiges Raster resamplen, dann Feder oder
  One-Euro-Filter. Die drei Stufen "Smooth/Medium/Rapid" sind nur verschiedene
  Federkonstanten, "None" ist die Rohspur.
- **Darstellung:** Eigene SVG-Cursor (Pfeil, Hand, Textcursor) über `theme::tintedIcon`
  bzw. direkt als Vektor, damit sie bei jeder Größe scharf bleiben. Die echte
  Cursorform kennt Eddy unter Wayland nicht, deshalb ein fester Stil (so macht es
  Screen Studio beim Hochskalieren ohnehin).
- **Effekte:** Ausblenden nach N Sekunden Stillstand mit Fade, Klick als kurzes
  Zusammendrücken (Skalierung ca. 0,8) plus optionaler Ring, Loop zurück zur
  Startposition am Clipende.

### 3.4 Rahmen-Styling

- Ausgabe = Hintergrund (Farbe, Gradient, Bild) in Ausgabegröße, darauf der
  Videoinhalt mit Padding, Rundung und Schlagschatten.
- Bei statischem Styling (kein Zoom) ist das billig: Hintergrund plus Schatten einmal
  als PNG rendern, Rundungsmaske als PNG, Video mit ffmpeg `scale`, `alphamerge` und
  `overlay` darauflegen. Das passt in den heutigen Filtergraph-Exporter und bleibt
  hardware-encodierbar.
- Für Bilder ist es nur ein weiterer Schritt in `exportComposite()`. Das ist der
  schnellste sichtbare Gewinn und funktioniert für Screenshots genauso gut.

### 3.5 Export: zwei Pfade

Heute baut `writeVideoWithOverlay` einen ffmpeg-Filtergraph mit einem **statischen**
Overlay-PNG. Das bleibt der schnelle Pfad für alles, was sich nicht über die Zeit
ändert (Annotationen, Blur, Crop, statisches Styling, Trim, Speed).

Für zeitabhängige Kamera, Cursor und Motion Blur braucht es einen Render-Pfad:

1. Decodieren per ffmpeg-Pipe (`-f rawvideo -pix_fmt bgra -`), Frame für Frame lesen.
2. Jedes Frame mit Qt rendern: Hintergrund, Video durch die Kamera-Transformation,
   Rundung und Schatten, Cursor, Overlays. Anfangs `QPainter` auf `QImage`, später
   GPU per `QRhi` offscreen, falls es zu langsam ist.
3. Encodieren per zweiter ffmpeg-Pipe (`-f rawvideo -i -`), Audio aus der Quelle
   mappen. Hardware-Encoder und CPU-Fallback bleiben wie heute.

Fortschritt, Stall-Erkennung und Abbrechen aus dem aktuellen Export-Fix gelten für
beide Pfade. Exporte bleiben explizit. Automatische Hintergrundexporte waren schon
einmal zu ressourcenhungrig.

Eine Alternative ohne eigenen Renderer (ffmpeg `zoompan`, oder `perspective` mit
Ausdrücken pro Frame) ist denkbar, aber `zoompan` ruckelt durch ganzzahlige
Positionen, und für Cursor und Motion Blur braucht es den Renderer sowieso. Nur als
kurzer Spike sinnvoll, falls der Qt-Renderer zu langsam ist.

### 3.6 Vorschau im Editor

Die Vorschau bleibt die bestehende `QGraphicsScene` mit dem Video-Item. Kamera-Zoom ist
dort nur eine Transformation des Video-Items samt Annotationen, Hintergrund und
Schatten sind zusätzliche Items dahinter. Das kostet während der Wiedergabe fast
nichts und braucht keine Frame-Konvertierung auf der CPU. Die Kamera-Kurve kommt aus
demselben Cache wie beim Export.

### 3.7 Speed und mehrere Schnitte

Fragmente sind Zeitbereiche der Quelle mit eigener Geschwindigkeit. Export über
ffmpeg `trim`/`atrim`, `setpts`/`atempo` pro Fragment und `concat`. Das bleibt im
schnellen Filtergraph-Pfad. Zoom- und Maskenzeiten müssen durch die Speed-Abbildung
von Quellzeit auf Ausgabezeit umgerechnet werden. Das gehört ins Kompositionsmodell,
nicht verstreut in UI-Code.

## 4. Die Linux- und Boltsnap-Kante

Nachtrag 2026-09-23: Der verbindliche Datei-Vertrag steht inzwischen in
`docs/specs/2026-09-23-studio-mode.md` (einzelne JSON-Datei in Videozeit und
Videopixeln). Er ersetzt den JSONL-Vorschlag weiter unten. Boltsnap baut die
Aufnahmeseite selbst (dort Plan `2026-09-23-performance-functionality.md`, Phase 4).

Für Cursor-Funktionen muss die Aufnahmeseite mitziehen. Stand heute:

- Boltsnap-Replay nutzt `gpu-screen-recorder` mit `-cursor yes`, der Cursor ist also
  eingebrannt. Die normale Aufnahme läuft über `wf-recorder`.
- Der saubere Wayland-Weg wäre der ScreenCast-Portal-Modus "Metadata" (Cursor als
  PipeWire-Metadaten statt im Bild). Bei `xdg-desktop-portal-hyprland` ist genau das
  seit 2024 als Problem offen, darauf sollte man nicht bauen.
- **Vorschlag für Hyprland:**
  - Video mit `-cursor no` aufnehmen.
  - Position per Hyprland-IPC `j/cursorpos` pollen (60 bis 120 Hz). Wichtig: Hyprland
    bearbeitet den Socket synchron, jede Anfrage muss kurz sein und sofort schließen,
    sonst friert Hyprland bis zu 5 s ein. Also pro Anfrage eine neue Verbindung mit
    kurzem Timeout, nie `hyprctl` als Subprozess in einer Schleife.
  - Klicks gibt es über Hyprland-IPC nicht. Optional per evdev, was die Gruppe `input`
    voraussetzt. Das ist Opt-in, weil ein Prozess mit Zugriff auf alle Eingaben
    praktisch ein Keylogger sein kann. Ohne Klicks funktioniert Auto-Zoom trotzdem über
    Stillstands-Erkennung (siehe 3.2).
  - Tastendrücke für das Shortcut-Overlay nur als Modifier-Kombinationen speichern,
    niemals normalen Text, und nur, wenn der Nutzer das Feature aktiv einschaltet.
- **Sidecar-Format (Vorschlag):** neben dem Video eine Datei `<video>.cursor.jsonl`,
  erste Zeile Kopf mit Version, Ausgabe-Geometrie, Skalierung und Zeitbasis relativ
  zum ersten Videoframe, danach eine Zeile pro Sample oder Event. JSON Lines, damit
  Boltsnap im Replay-Ringpuffer einfach vorne abschneiden kann.
- **Replay:** Der Ringpuffer muss die Cursor-Spur synchron mitführen und beim Speichern
  auf denselben Zeitbereich schneiden wie das Video. Das ist der heikelste Teil, weil
  die Zeitstempel beider Quellen zusammenpassen müssen.
- **Nicht-Hyprland und importierte Videos:** Kein Sidecar, also keine Cursor-Funktionen.
  Manuelle Zooms, Styling, Speed und Masken funktionieren trotzdem. Genau so verhält
  sich auch Screen Studio bei importierten Videos.

## 5. Übernahme in Eddys Oberfläche

Screen Studio setzt auf eine große Sidebar rechts. Eddys geltende UI-Regeln sagen
ausdrücklich: kein permanenter Inspector, Kontextleisten nur für die aktive Tätigkeit,
bestehende Größen, Icons und Grauwerte. Übersetzung:

- **Style** als Button in der oberen Leiste mit Popover: Hintergrund (Farbe, Gradient,
  Bild, keiner), Padding, Rundung, Schatten, Seitenverhältnis der Ausgabe. Gilt für
  Bilder und Videos gleich.
- **Timeline-Spuren** unter dem Filmstreifen, wie es der Audio-Plan schon mit einer
  28-px-Spur vorsieht: Zoom-Spur, später Masken-Spur. Klick in eine leere Stelle legt
  ein Segment an, Ränder ziehen ändert die Dauer, Rechtsklick oder `Delete` entfernt.
  Spuren erscheinen erst, wenn sie Inhalt haben oder man sie einblendet, damit kurze
  Clips so kompakt bleiben wie heute.
- **Kontextleiste für ein gewähltes Zoom-Segment:** Zoomstufe, Ziel (fester Punkt oder
  Cursor folgen, letzteres nur mit Sidecar), Instant. Das Ziel wird direkt im Canvas
  gezogen statt in einer Mini-Vorschau.
- **Fragmente:** Split am Playhead (z.B. `S`), Fragment wählen, Kontextleiste mit
  Speed und Entfernen. Erweitert das heutige In/Out, ersetzt es nicht.
- **Cursor-Einstellungen** (Größe, Glättung, Ausblenden bei Stillstand, Klick-Effekt)
  als Popover, das nur bei vorhandenem Sidecar erscheint.
- **Presets** im Menü des Style-Popovers: speichern, anwenden, als Datei teilen.

## 6. Vorschlag für Reihenfolge

Jede Phase ist für sich nützlich und einzeln abnehmbar.

| Phase | Inhalt | Braucht | Grobe Größe |
| --- | --- | --- | --- |
| 1 | Rahmen-Styling für Bilder: Hintergrund, Padding, Rundung, Schatten, Ausgabe-Seitenverhältnis. Style-Popover | nichts | klein |
| 2 | Dasselbe für Videos, statisch über ffmpeg-Filtergraph | Phase 1 | klein bis mittel |
| 3 | Kompositionsmodell und Render-Exportpfad (Decode-Pipe → Qt → Encode-Pipe), zunächst mit identischer Ausgabe zu Phase 2 als Referenztest | Phase 2 | mittel |
| 4 | Manuelle Zoom-Segmente mit Federkamera: Zoom-Spur, Kontextleiste, Vorschau per Szenen-Transformation, Export über Phase 3 | Phase 3 | mittel bis groß |
| 5 | Fragmente: Split, mehrere Schnitte, Speed pro Fragment | Kompositionsmodell | mittel |
| 6 | Zeitfenster für Masken, Highlights und Annotationen | Phase 4 (Spuren) | mittel |
| 7 | Boltsnap: Aufnahme ohne Cursor plus Sidecar, zuerst normale Aufnahme, dann Replay-Ringpuffer | eigenes Boltsnap-Projekt | groß |
| 8 | Eddy: Cursor neu zeichnen, Glättung, Größe, Ausblenden, Klick-Effekte, Auto-Zoom-Vorschläge, Follow Cursor | Phase 4 und 7 | groß |
| 9 | Extras nach Bedarf: GIF-Export, Export-Presets, Motion Blur, Shortcut-Overlay (Opt-in), lokale Captions per whisper.cpp | jeweils | je klein bis groß |

Tests pro Phase nach dem Muster der bestehenden Suites: generierte ffmpeg-Clips mit
bekannten Farben, Pixelproben an festen Stellen, Vorschau und Export für dasselbe `t`
vergleichen. Für die Federkamera zusätzlich reine Unit-Tests auf dem Modell
(Einschwingzeit, nie außerhalb der Quelle, Instant springt exakt).

Risiken:

- Render-Pfad-Performance bei 4K oder hohen Bildraten. Messpunkt vor Phase 4:
  1080p60 mit `QPainter` muss mindestens Echtzeit schaffen, sonst direkt `QRhi`.
- Zeitbasis zwischen Video und Cursor-Spur, besonders beim Replay-Ringpuffer.
- Windows ist experimentell. Styling und Zooms sind plattformneutral, das Sidecar
  gibt es dort vorerst nicht.

## 7. Bewusst nicht übernehmen

Cloud-Share-Links und Kommentare (Boltsnap-Shelf deckt Teilen lokal ab),
iPhone/iPad-Aufnahme und Geräterahmen, Webcam-Aufnahme und Kamera-Layouts,
Hintergrundmusik, Rauschunterdrückung. Das sind jeweils eigene Produkte. Webcam
könnte später als Import einer zweiten Datei kommen, nicht als Aufnahme in Eddy.

## 8. Offene Entscheidungen

1. Der Plan vom 21.09. hat "zeitlich begrenzte Annotationen" und
   "Mehrspur-Schnittsoftware" ausdrücklich ausgeschlossen. Phase 5 und 6 würden das
   aufheben. Soll das so sein, oder reichen Styling und Zooms?
2. Reihenfolge gegenüber dem offenen Plan (Audio → Fortsetzen → Annotation-Hilfen):
   Styling vorziehen oder danach?
3. Boltsnap-Sidecar: Ist evdev mit Gruppe `input` für Klicks akzeptabel, oder reicht
   Auto-Zoom über Stillstand ohne Klicks?
4. Sollen Style-Einstellungen global als Standard gelten (Config), pro Datei, oder
   beides über Presets?

## Quellen

- Screen Studio, Produktseite: https://screen.studio
- Guide-Übersicht: https://screen.studio/guide
- Zooms: https://screen.studio/guide/adding-editing-zooms,
  https://screen.studio/guide/auto-zoom, https://screen.studio/guide/manual-zoom,
  https://screen.studio/guide/instant-zoom
- Look & Feel: https://screen.studio/guide/background, https://screen.studio/guide/cursor,
  https://screen.studio/guide/animations,
  https://screen.studio/guide/hiding-the-cursor-in-specific-sections
- Bearbeitung: https://screen.studio/guide/aspect-ratio,
  https://screen.studio/guide/speeding-up-the-video,
  https://screen.studio/guide/speed-up-typing-segments,
  https://screen.studio/guide/adding-a-mask-and-highlight
- Weitere: https://screen.studio/guide/shortcuts, https://screen.studio/guide/captions,
  https://screen.studio/guide/dynamic-camera-layouts-,
  https://screen.studio/guide/creating-preset,
  https://screen.studio/guide/explanation-of-export-settings,
  https://screen.studio/guide/creating-project-from-existing-video
- OpenScreen, Auto-Zoom-Algorithmus: https://getopenscreen.com/features/auto-zoom/
- Recordly (MIT, PixiJS/Electron): https://github.com/DougNix/recordly
- Cap (AGPLv3, Rust): https://github.com/CapSoftware/Cap
- XDPH-Cursor-Metadaten-Issue: https://github.com/hyprwm/xdg-desktop-portal-hyprland/issues/170
- Hyprland IPC: https://wiki.hypr.land/IPC/
- Beispiel für begrenztes `j/cursorpos`-Polling: https://github.com/trycua/cua/pull/3553
