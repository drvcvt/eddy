# Studio: alle fehlenden Screen-Studio-Funktionen

Datum: 2026-09-24. Basis: `main` bei `2910125` plus die uncommittete Studio-Arbeit vom
2026-09-23 (Export-Rückmeldung, Cursor-Loader, Studio-Styling für Bilder und Videos).
Status: **Plan vollständig und freigegeben, alle Design- und Umfangsfragen am 2026-09-24
beantwortet (Abschnitt 11 und 12). S0 und S1 umgesetzt (Abschnitt 9), nächste Phase S2
(Zoom-Segmente in der UI).** Offen bleiben nur Fragen, die echte Daten oder Clips brauchen
(Federwerte N6: Vergleichsclips gerendert, Wahl offen; Schnittmarke in S4, Masken-Spur in S6,
Projektaktionen im Export-Popover mit P5/P6). Umsetzung Phase für Phase mit je eigenem
Implementierungsplan (Abschnitt 9).

Grundlagen, die hier nicht wiederholt werden:
`docs/specs/2026-09-23-studio-mode.md` (Grundsätze, UI-Entscheidungen, Cursor-Vertrag v1,
was Boltsnap liefert), `docs/plans/2026-09-23-screen-studio-research.md` (Funktionskatalog,
Technik, Quellen), `docs/plans/2026-09-21-crop-audio-projects-annotations.md` (offener Plan
Audio → Fortsetzen → Annotation-Hilfen), `docs/handoffs/2026-09-24-studio-styling-and-export-feedback.md`.

## 0. Ausgangsstand, heute geprüft

- Eddy: Branch `main`, HEAD `2910125`, 4 Commits vor dem lokal bekannten `origin/main`.
  19 geänderte und 11 neue Dateien aus der letzten Session, alle uncommittet. Nichts
  davon wird ohne Auftrag committet, überschrieben oder zurückgedreht.
- Boltsnap: `main`, 15 Commits voraus, `src/platform/linux/shelf/mod.rs` geändert (andere
  Session). Nur gelesen.
- `cmake --build build-rel --parallel 2`: grün. `ctest --test-dir build-rel`: **28/28
  bestanden, 34,85 s**. Kein Watchdog-Hänger in `test_crop` in diesem Lauf.
- `find ~ -name "*.cursor.json"`: **keine Datei**. Der Parser ist weiterhin nur mit
  synthetischen Spuren getestet.
- Der Parser liest heute `samples`, `clicks`, `clean_video`, `cursor_in_video`. Das vierte
  Sample-Element (Bild-ID), `images` und `render.preset` werden noch ignoriert.
- `VideoInfo` kennt keine Tonspuren. `writeVideoWithOverlay` mappt `0:a?` blind.
- Freie Einzeltasten ohne Modifier (für neue Kürzel geprüft in `EditorWindow::keyPressEvent`):
  u.a. `Z` (heute nur Ctrl+Z) und `S` (heute nur Ctrl+S). `N` ist im Plan vom 21.09.
  für Schritte reserviert.

## 1. Umfang

| Nr. | Funktion | Braucht | Status in diesem Plan |
| --- | --- | --- | --- |
| a | Kompositionsmodell, Render-Exportpfad | nichts | geplant |
| b | Zoom-Segmente (Spur, Kontextleiste, Ziel im Canvas, Vorschau), Motion Blur optional | a | geplant, Motion Blur später und optional (N4) |
| c | Follow Cursor, Auto-Zoom-Vorschläge per Stillstand (und Klicks, sobald die Spur welche hat). **Kein** Neu-Rendern und keine Cursor-Wahl in Eddy (6.4) | a, b, Cursor-Spur | geplant |
| d | "Always keep zoomed in" für 9:16 und andere Verhältnisse | a, b; mit Spur auch c | geplant |
| e | Fragmente: Split, mehrere Schnitte, Speed pro Fragment | a (Zeitabbildung) | geplant |
| f | Masken und Highlights mit Zeitfenster | Spuren aus b | geplant: nur Redact und Spotlight (N1 = a) |
| g | GIF, Export-Presets, teilbare Studio-Presets | a für GIF mit Zoom | geplant |
| h | Audio und Untertitel | Audio-Plan 21.09. | Audio-Plan unverändert, Untertitel raus (N2 = a) |

Beschlossen und hier nicht neu verhandelt: Studio ist optional (jedes neue Dokument ohne
Studio, ein Klick schaltet mit dem letzten Stil ein), ohne Studio bleibt der heutige
Exportpfad bitgleich im Verhalten, UI grau und Farbe nur im Export, kein permanenter
Inspector, Exporte bleiben explizit.

Raus (keine Daten von Hyprland 0.56 oder abgewählt): Klick-Effekte, Klicksound,
Shortcut-Overlay, Typing-Speedup, Cursorformen außer dem einen Pfeil, Webcam, iOS,
Cloud-Share-Links, eigene Farben, Wallpaper-Blur. Ebenfalls nicht geplant, nur auf Wunsch:
Cursor-Rotation beim Bewegen, Cursor-Loop zur Startposition am Ende.

## 2. Messung vor der Planung: schafft QPainter 1080p60?

Pflichtmessung aus dem Rechercheplan. Wegwerf-Probe `/tmp/studio-probe/render.cpp` und
`pipe.cpp`: ffmpeg decodiert per Pipe (`-f rawvideo -pix_fmt bgra`), Qt komponiert jedes
Frame (Verlaufshintergrund, Inhalt durch eine zoomende Kamera mit `SmoothPixmapTransform`,
abgerundeter Clip), ffmpeg encodiert per zweiter Pipe. Quelle: der echte Clip
`boltsnap-2026-09-21_18-25-58.mp4` (1894×1026, 240 fps), 5 s ab Sekunde 5, auf 60 fps
gezogen, 300 Ausgabeframes. Rechner: 12 Threads, RTX 4060, Encoder `h264_vulkan` bzw.
`libx264 veryfast`.

| Aufbau | Ausgabe | Encoder | Wand-Zeit | Durchsatz |
| --- | --- | --- | --- | --- |
| seriell (lesen, rendern, schreiben nacheinander), 1 Thread | 1920×1080 | libx264 | 6,64 s | 45 fps |
| seriell, 2 Render-Threads | 1920×1080 | h264_vulkan | 5,09 s | 59 fps |
| **überlappend** (Decoder-, Render-, Encoder-Thread), 1 Render-Thread | 1920×1080 | libx264 | 2,93 s | 102 fps |
| überlappend, 1 Render-Thread | 1920×1080 | h264_vulkan | 2,46 s | 122 fps |
| überlappend, 2 Render-Threads | 1920×1080 | h264_vulkan | 2,27 s | 132 fps |
| überlappend, ohne Encoder, 1 Render-Thread | 1920×1080 | keiner | 1,89 s | 159 fps |
| überlappend, 2 Render-Threads | 3840×2160 | h264_vulkan | 7,01 s | 43 fps |

Reines QPainter-Rendern kostet 4,1 bis 5,3 ms pro 1080p-Frame auf einem Thread. Die
Ausgabe wurde angesehen (Frame bei 2,5 s: Verlauf, abgerundeter Inhalt, gezoomter
Ausschnitt korrekt). Zum Vergleich: der heutige Studio-Export über `maskedmerge` braucht
für denselben Ausschnitt 2,54 s.

**Folgerung:** QPainter reicht, **kein QRhi**. Bedingung ist die überlappende Pipeline
(Decoder, Rendern, Encoder in eigenen Threads mit begrenzten Warteschlangen). 1080p60 läuft
mit 1,7× (x264) bis 2,2× (Vulkan) Echtzeit. 4K-Ausgabe liegt mit 43 fps unter Echtzeit;
das ist für explizite Exporte akzeptabel und wird im Fortschritt sichtbar, nicht versteckt.

## 3. Kompositionsmodell

### 3.1 Grundsatz

Alles, was Studio über die Zeit verändert, ist eine reine Funktion der Ausgabezeit:

```
Composition(tOut) = { camera: Rechteck in Dokumentpixeln,
                      style:  StudioStyle (zeitlos),
                      overlays: Redact/Spotlight, sichtbar nach ihrem Zeitfenster }
```

Vorschau und Export rufen dieselbe Auswertung mit demselben Cache auf. Es gibt keinen
Echtzeit-Integrator in der Vorschau.

### 3.2 Datenmodell

Neue Datei `src/studiodocument.{h,cpp}`. Alles speicherbar, alle Zeiten in **Quellzeit**
(ms ab dem ersten Frame der Quelldatei, wie Trim und Cursor-Spur), alle Orte in
**Dokumentpixeln** (dasselbe orientierte Koordinatensystem wie Crop und Annotationen).
Quellzeit bleibt stabil, wenn Fragmente geschnitten oder beschleunigt werden, und passt
direkt zur Cursor-Spur.

```cpp
struct ZoomSegment {
    quint32 id;                       // stabil, für Auswahl, Undo und Projektdatei
    qint64 startMs, endMs;            // Quellzeit, endMs exklusiv
    double scale = 2.0;               // 1.1 … 4.0
    enum class Target { Point, Cursor } target = Target::Point;
    QPointF point;                    // Ausschnittsmitte in Dokumentpixeln
    enum class Motion { Focused, Smooth, Instant } motion = Motion::Focused;
};
struct Fragment {                     // Partition von [0, Dauer], endet am nächsten start
    qint64 startMs = 0;
    double speed = 1.0;               // 0.25 … 4
    bool removed = false;
};
struct StudioDocument {
    StudioStyle style;                // wie heute
    QVector<ZoomSegment> zooms;       // nach start sortiert, überlappungsfrei
    QVector<Fragment> fragments;      // leer = ein Fragment mit 1×
    bool keepZoomedIn = false;
    QPointF keepCenter;               // Mitte ohne Cursor-Spur
    bool timeVarying() const;         // entscheidet über den Exportpfad (4.1)
};
```

Trim bleibt, wo er ist (`m_trimInMs`/`m_trimOutMs`), und begrenzt die Fragmente außen.
Zeitfenster für Masken und Highlights liegen nicht hier, sondern als optionales
`timeWindow` (Quellzeit) an Redact- und Spotlight-Items (6.7), weil es dieselben Objekte sind,
die heute schon statisch existieren.
Crop bleibt ein statisches Quellrechteck; Kamera und "keep zoomed in" arbeiten innerhalb
des Crop-Rechtecks.

**Undo:** Ein Befehl `SetStudioDocumentCommand(before, after)` nach dem Muster von
`SetStudioStyleCommand`. Das Dokument ist klein, Snapshots sind die einfachste robuste
Form. Eine Geste (Ziehen, Popover-Sitzung, Vorschläge einfügen) ist genau ein Schritt.

**Speicherform:** `studioToJson`/`studioFromJson` gleich in Phase S0, auch wenn das
Projektformat (21.09., P5) noch fehlt. P5 hängt den Block nur ein. Vorschlag:

```json
"studio": {
  "version": 1,
  "style": {"background": "gradient", "color": "#5b4bdb", "color2": "#ff7eb3", "angle": 135,
            "image": null, "padding": 8, "radius": 2, "shadow": 50, "aspect": [16, 9]},
  "zooms": [{"id": 1, "start": 3000, "end": 7500, "scale": 2, "target": "point",
             "point": [1060, 410], "motion": "focused"}],
  "fragments": [{"start": 0}, {"start": 8000, "removed": true}, {"start": 12500, "speed": 2}],
  "keepZoomedIn": {"on": false, "center": [947, 513]}
}
```

Validierung wie im 21.09.-Plan: höhere `version` ablehnen, nur endliche Zahlen, Zeiten
innerhalb der Dauer, Zoom 1,1 bis 4, Speed 0,25 bis 4, höchstens 1000 Zooms und 1000
Fragmente, Segmente sortiert und ohne Überlappung, Punkte innerhalb der Quelle. Ein
ungültiger Block wird als Ganzes abgelehnt, nicht still repariert.

### 3.3 Zeitabbildung

`TimeMap` (in `studiodocument`): aus Trim und Fragmenten.

- `outputDuration()`: Summe der behaltenen Fragmentlängen innerhalb von Trim, je geteilt
  durch ihren Speed.
- `toOutput(srcMs)` → `std::optional<double>`: leer in geschnittenen Bereichen.
- `toSource(outMs)` → `double`: monoton, stetig über Fragmentgrenzen.
- Frame-Raster: Ausgabe immer 60 fps (heute `-fpsmax 60`). Ausgabeframe `n` liegt bei
  `tOut = n / 60`.

Alle Umrechnungen stehen hier und nirgends im UI-Code.

### 3.4 Kamera: Federn, vorab simuliert und gecacht

`CameraPath::build(document, trimmedTimeMap, contentRect, outputAspect, cursorTrack)`:

1. Zielfunktion über der **Ausgabezeit**: aktives Segment bei `toSource(tOut)` → Ziel
   (Punkt oder Cursor-Folge, 6.2), sonst Vollbild (bzw. die "keep zoomed in"-Basis, 6.5).
   Das Zielrechteck hat das Seitenverhältnis des Inhaltsrahmens der Ausgabe und wird vor
   der Feder in die Quelle geklemmt.
2. Zustand: Mitte `x, y` und `log(scale)` samt Geschwindigkeiten. Zoom logarithmisch, damit
   das Reinzoomen nicht am Anfang hastet.
3. Integration mit fester Schrittweite 1/240 s ab `tOut = 0`, mit der **exakten Lösung** der
   kritisch gedämpften Feder pro Schritt (`x = z + (x0 + (v0 + ωx0)t)e^(−ωt)` bei konstantem
   Ziel `z` im Schritt). Stabil, unabhängig von Bildrate und Rechner, deterministisch.
4. Federn (Masse 1, kritisch gedämpft wie bei Boltsnap). **Vorschlag, in Phase S2 an
   gerenderten Clips abzustimmen:** Focused Spannung 100 / Reibung 20 (≈0,58 s bis 2 %),
   Smooth 36 / 12 (≈0,97 s). Beim Hineinfahren gilt das Profil des neuen Segments, beim
   Herausfahren das des verlassenen. Instant setzt Zustand und Geschwindigkeit an der
   Segmentgrenze exakt auf das Ziel.
5. Nahe Segmente: Ist die Lücke zwischen zwei Segmenten kürzer als 1 s (Vorschlag), fährt
   die Kamera direkt weiter, statt kurz auf Vollbild zu gehen.
6. Cache: Samples (Position und Geschwindigkeit, `float`) alle 1/240 s. Auswertung per
   kubischer Hermite-Interpolation zwischen zwei Samples, danach eine letzte Klemmung in die
   Quelle. Eine Stunde Video sind 864 000 Samples, etwa 21 MB. Der Cache wird nach jeder
   Dokumentänderung neu gebaut (Millisekunden, synchron, nie während der Wiedergabe pro
   Frame).

Eigenschaften, die Unit-Tests festnageln: Einschwingzeit je Profil, nie außerhalb der Quelle,
Instant springt exakt, gleiche Eingabe gibt bitgleiche Kurven, Ausgangszustand vor dem ersten
Segment ist exakt Vollbild, nach dem letzten Segment kehrt die Kamera exakt zurück.

## 4. Export

### 4.1 Welcher Pfad

`StudioDocument::timeVarying()` ist wahr bei aktiven Zoom-Segmenten, "keep zoomed in" mit
Cursor-Folge oder Motion Blur. Dann Render-Pfad, sonst der heutige
ffmpeg-Filtergraph-Pfad. Ohne Studio ändert sich nichts.

| Inhalt | Pfad |
| --- | --- |
| kein Studio | heutiger Pfad, unverändert |
| statisches Styling, Crop, Blur, Annotationen, Trim | heutiger Pfad (heute schon so) |
| Fragmente (Schnitt, Speed) | heutiger Pfad, erweitert um `trim`/`setpts`/`atrim`/`atempo`/`concat` |
| "keep zoomed in" mit festem Mittelpunkt | heutiger Pfad (ist ein statischer Crop) |
| Masken/Highlights mit Zeitfenster ohne Zoom | heutiger Pfad mit `enable='between(t,…)'` |
| GIF ohne Zeitabhängiges | heutiger Pfad plus Paletten-Filter |
| Zoom, "keep zoomed in" mit Cursor, Motion Blur | Render-Pfad |

### 4.2 Render-Pfad

Neue Dateien `src/studiorenderer.{h,cpp}` (reines Zeichnen, threadsicher) und
`src/studioexport.{h,cpp}` (Pipeline), eingehängt in `writeVideoWithOverlay` hinter
derselben `VideoExportRequest`-Schnittstelle, damit Cache, Pending-Actions, Fortschritt,
Stall und Abbrechen in `EditorWindow` unverändert bleiben.

1. **Decoder-Prozess:** ffmpeg liest die Quelle (mit Boltsnaps eingebranntem Cursor), wendet im
   Filtergraph an, was ohnehin ffmpeg-Sache ist und im Quellraum passiert: Orientierung und
   SAR wie heute, Blur-Rechtecke (dieselbe Filterkette wie heute, bei Zeitfenstern mit
   `enable`), Fragmente (`trim`/`setpts`/`concat`), dann `fps=60`, Crop. Ausgabe
   `-f rawvideo -pix_fmt bgra` auf stdout. Damit gibt es Blur nur einmal im Code.
2. **Render-Threads (1 bis 2):** pro Ausgabeframe `n`: `tOut = n/60`, Kamera aus
   dem Cache, `StudioRenderer::render()` zeichnet Hintergrund samt Schatten (einmal
   vorberechnet, wie heute), den Inhalt durch die Kamera in den abgerundeten Rahmen, das
   Annotations-Overlay durch dieselbe Kamera, Highlights.
   Das Annotations-Overlay wird einmal in höherer Auflösung vorgerendert (Quelle × höchste
   Zoomstufe, gedeckelt bei 64 Megapixel), damit Pfeile und Text im Zoom scharf bleiben.
3. **Encoder-Prozess:** `-f rawvideo -pix_fmt bgra -s WxH -r 60 -i -` plus die Quelle als
   zweiter Eingang nur für Ton (dieselbe `atrim`/`atempo`/`concat`-Kette wie im schnellen
   Pfad), Encoderwahl wie heute: Hardware zuerst, CPU als Rückfall; fällt der
   Hardware-Encoder unterwegs aus, startet der ganze Durchlauf mit CPU neu.
4. **Warteschlangen:** begrenzt nach Bytes (256 MB gesamt), damit 4K keinen Speicher frisst.
   Reihenfolge der Frames bleibt erhalten.
5. **Fortschritt:** geschriebene Frames / erwartete Frames. **Stall:** kein geschriebenes
   Frame innerhalb `stallTimeoutMs`. **Abbrechen:** beide Prozesse beenden, Threads
   abräumen, Temp-Datei löschen. Alles wie heute sichtbar in `Exporting N%`.

Referenztest vor jeder Zoom-Funktion: Render-Pfad mit statischem Stil gegen den
`maskedmerge`-Pfad, gleiche Pixelproben (Rand, Ecke, Inhalt) innerhalb YUV-Toleranz, gleiche
Dauer, gleiche Framerate.

### 4.3 Schneller Pfad, Erweiterungen

- **Fragmente:** Nach Blur und Overlay, vor Crop/Studio: `split` auf n behaltene Fragmente,
  je `trim=start:end,setpts=(PTS-STARTPTS)/speed`, dann `concat=n:v=1`, danach `fps=60`.
  Ton je `atrim,asetpts=PTS-STARTPTS,atempo=…` (Faktoren unter 0,5 als Kette), `concat=a=1`.
  Tonspur vorher per ffprobe erkennen (`VideoInfo` bekommt `hasAudio`), sonst nur Video.
  Zeiten relativ zur Eingabe nach `-ss` (heute beginnt die Eingabe bei Trim-In).
- **Zeitfenster** in Filtern (Blur, Highlight-Overlay) über `enable='between(t,a,b)'` in
  derselben Zeitbasis, also vor dem Fragment-`split`.
- **GIF:** `fps=…,scale=…:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle`, ohne Ton.
  Vom Render-Pfad aus derselbe Filter hinter der Encode-Pipe.

## 5. Vorschau im Editor

- **Kamera:** `Canvas` bekommt `setCameraTransform(QTransform)`. Die Kamera ist ein
  zusätzlicher Anteil der View-Transformation; `drawForeground` zeichnet den Studio-Rahmen
  weiter mit der Transformation **ohne** Kamera. Weil der Rahmen alles außerhalb des
  abgerundeten Inhalts abdeckt, zoomen Video, Annotationen und Blur-Items zusammen, ohne
  neue Items und ohne CPU-Frames. Maus-Mapping bleibt korrekt, Annotationen werden im
  Zoom in Dokumentkoordinaten gezeichnet.
- **Takt:** Pro Video-Frame aus `QVideoSink::videoFrameChanged` (Startzeit des Frames →
  `tOut` → Kamera aus dem Cache). Kein Timer, keine eigene Animation. Pausiert und beim
  Scrubben dieselbe Auswertung.
- **Timeline in Ausgabezeit (Q5 = B):** Filmstreifen, Zoom-Spur und Lineal zeigen die
  Ausgabezeit. Die x↔t-Abbildung der Timeline läuft über `TimeMap`: Thumbnails, Hover-Vorschau
  und Seek fragen `toSource(tOut)` ab. Ohne Fragmente ist das die Identität, also das heutige
  Verhalten.
- **Fragmente:** Wiedergabe überspringt geschnittene Fragmente (Seek an die nächste behaltene
  Quellzeit) und setzt `playbackRate = Speed × Vorschau-Speed` pro Fragment. Kleine
  Seek-Ruckler in der Vorschau sind möglich und werden gemessen, der Export ist exakt.
- **Masken mit Zeitfenster:** Items blenden sich nach der Wiedergabeposition ein und aus.
- Performance-Messpunkt: Canvas-Repaint mit Kamera während der Wiedergabe, Ziel wie heute
  (Studio kostete 0,4 ms pro Vollbild-Repaint).

## 6. Funktionen

Aufwand: klein ≈ eine Session, mittel ≈ zwei, groß ≈ drei oder mehr. Schätzungen, keine
Zusagen.

### 6.1 Zoom-Segmente (b)

**Verhalten**
- Zoom-Spur erscheint, sobald Studio aktiv ist (oder Segmente existieren). Ohne Studio
  bleibt die Timeline wie heute.
- Klick in eine leere Stelle der Spur oder `Z`: neues Segment ab dieser Zeit, 2 s lang
  (in die freie Lücke geklemmt, mindestens 0,5 s), 2×, Ziel = Mitte des Inhalts bzw. das
  zuletzt benutzte Ziel, Focused. Es ist sofort gewählt.
- Körper ziehen verschiebt, Ränder ziehen ändern die Dauer. Einrasten an Playhead,
  Trim-Grenzen und Nachbarsegmenten (6 px). Segmente überlappen nie.
- Wählen: Klick. `Delete`/`Backspace` entfernt, `Esc` bricht Ziehen ab bzw. hebt die Wahl
  auf, Pfeiltasten verschieben um ein Frame, Shift um zehn. Rechtsklick: Motion, Ziel,
  Entfernen.
- Gewähltes Segment: schwebende Kontextleiste unten mittig im Canvas (Q2 = A) mit Zoomstufe,
  Ziel (Point/Cursor), Motion, Remove.
- **Ziel im Canvas (Q3 = C):** Der Canvas bleibt im Zoom. Ist ein Segment gewählt und die
  Wiedergabe pausiert, zeigt er den eingeschwungenen Zielausschnitt dieses Segments (nicht
  einen Zwischenstand der Fahrt). Ziehen auf leerem Inhalt mit dem Move-Werkzeug verschiebt
  das Ziel; Ziehen auf einer Annotation bewegt wie heute die Annotation, andere Werkzeuge
  zeichnen wie heute (in Dokumentkoordinaten, auch im Zoom). Unten rechts im Inhalt sitzt
  eine Mini-Karte (`raise1`-Kachel, ganzes Bild, Ausschnitt hell, Rest gedimmt); den
  Ausschnitt dort zu ziehen verschiebt das Ziel, das Mausrad über der Karte ändert die
  Zoomstufe. Die Karte ist UI, nie im Export, und verschwindet bei Wiedergabe und ohne
  gewähltes Segment.
- Wiedergabe zeigt immer das Ergebnis mit Kamerafahrt.
- Deaktivieren ohne Löschen (Screen Studio "Disable"): **nicht geplant**, Undo und Löschen
  reichen. Auf Wunsch nachrüstbar.

**UI:** Spur Q1 = C: 28 px hoch unter dem Filmstreifen. Jedes Segment ist ein Block, in dem
die **tatsächliche** Zoomstufe über die Zeit als Fläche steht (aus dem `CameraPath`-Cache,
also mit echten Rampen und Profil), das Label (Stufe, Motion bzw. Follow) links im Block.
Gewählt = eine Stufe heller, Ränder als Griffe. Leere Spur mit Hinweis "Click to add a
zoom · Z". Kontextleiste Q2 = A, Ziel Q3 = C (oben). Zoomstufen im Menü: 1.25×, 1.5×, 2×,
3×, frei per Mausrad über der Mini-Karte. Beschriftungen Englisch wie der Rest der App,
Zahlen in der Mono-Schrift.

**Umsetzung:** `VideoTimeline` bekommt Spuren als eigene Rechtecke unter dem Filmstreifen
(gleiche x↔t-Abbildung, wie der 21.09.-Plan es für Audio verlangt), statt `trackRect` zu
strecken. Eine kleine `ZoomLane`-Logik für Hit-Test und Gesten, `ZoomBar` als
Kontextleiste nach dem Muster von `CropBar` (inklusive Umbruch bei schmalem Fenster),
`CameraTargetController` für das Ziehen im Zoom und die Mini-Karte (Hit-Test in
Viewport-Koordinaten, Ziel in Dokumentpixeln, Undo als ein Schritt pro Geste).

**Tests:** Unit: Einfügen in Lücken, Klemmen, Einrasten, keine Überlappung, Undo als ein
Schritt, Cache-Neubau nach Änderung. Export: generierter Clip mit farbigem Raster und
Markierungspunkt; bei `t` in der Mitte eines 2×-Segments liegt der Punkt im Export an der
erwarteten Stelle (±1 px nach Skalierung), vor und nach dem Segment exakt Vollbild.
Vorschau gegen Export (7.2). Render beider Themes, eine offene Leiste, ein schmales Fenster.

**Abhängigkeiten:** S0, S1. **Aufwand:** groß.

### 6.2 Follow Cursor (c)

**Verhalten:** Ziel "Cursor" nur wählbar, wenn eine gültige Spur geladen ist (sonst
deaktiviert mit Tooltip "Needs a Boltsnap cursor track"). Während das Segment aktiv ist,
folgt das Ziel dem Cursor mit einer Totzone: das Ziel bewegt sich erst, wenn der Cursor ein
zentrales Rechteck von 40 % der Ausschnittbreite und -höhe verlässt, und dann nur so weit,
dass er wieder auf dessen Rand liegt (Vorschlag, abzustimmen). Die Kamerafeder glättet.
Verlässt der Cursor die Aufnahme (`null`), hält das Ziel.

**Umsetzung:** Teil von `CameraPath::build`, Cursor per `positionAt(toSource(tOut))` im
240-Hz-Raster. **Tests:** synthetische Spur (Sprung, Kreis, Pause, Lücke): Kamera bleibt in
der Quelle, Totzone verhindert Bewegung bei kleinen Ausschlägen, Hold bei `null`.
**Abhängigkeiten:** 6.1, Spur. **Aufwand:** klein.

### 6.3 Auto-Zoom-Vorschläge per Stillstand und Klicks (c)

**Verhalten:** Aktion "Suggest zooms" (Camera-Seite im Popover) fügt Segmente ein, **als
normale Segmente in einem Undo-Schritt**, keine Geister-Zustände (N5 = a). Signale:
**Klicks, sobald die Spur welche enthält** (`clicks` ist im Vertrag v1 schon vorgesehen und
wird geparst, Boltsnap liefert heute keine), sonst und dazwischen Stillstand.
Algorithmus nach OpenScreen, Startwerte: Stillstand = Cursor bleibt 0,5 bis 2,6 s innerhalb
eines Radius von 1,5 % der Diagonale; Kandidaten nach Länge sortieren; verwerfen, wenn näher
als 1,8 s an einem bestehenden Segment oder Kandidaten; je Kandidat 2 s ab 0,4 s vor
Stillstandsbeginn, 1,8×, Ziel = mittlere Position. Nur innerhalb behaltener Fragmente.
Klicks (Startwerte wie Screen Studio beschrieben, eigene Zahlen): Linksklicks innerhalb von
1,5 s zu einer Gruppe zusammenfassen; je Gruppe ein Segment ab 0,6 s vor dem ersten bis
1,2 s nach dem letzten Klick, 2×, Ziel = Mittelpunkt der Klickpositionen. Klickgruppen haben
Vorrang, Stillstands-Kandidaten füllen nur Lücken mit dem gleichen Mindestabstand.
Ohne Spur ist die Aktion nicht sichtbar.

**Tests:** synthetische Spuren mit bekannten Stillständen → erwartete Segmente, Abstand,
Randfälle (Stillstand am Anfang, am Ende, in geschnittenem Fragment, über Lücke hinweg);
Spur mit Klicks: Gruppierung, Vorrang vor Stillstand, Spur ohne Klicks verhält sich wie
reine Stillstandserkennung.
**Abhängigkeiten:** 6.1, Spur. **Aufwand:** klein.

### 6.4 Cursor: bleibt, wie Boltsnap ihn einbrennt (c)

Entschieden 2026-09-24: Der Cursor wird in Boltsnap festgelegt (Glättung Mellow oder Quick,
eingebauter Pfeil mit Motion Blur) und ins Video gebrannt. Eddy rendert ihn nicht neu und bietet
keine Cursor-Wahl an (kein Redraw, kein "None", keine Größe, kein Ausblenden), weil ein Neu-
Rendern jeden Export über den Render-Pfad zwingen und verlängern würde. Eddy exportiert immer
die Hauptdatei; `clean_video` wird nicht benutzt (der Parser kennt es weiter, es schadet nicht).
Die Spur bleibt wertvoll: Follow Cursor (6.2), Auto-Zoom-Vorschläge (6.3), "keep zoomed in"
mit Cursor-Folge (6.5). Der eingebrannte Cursor zoomt mit der Kamera mit, weil er Teil des
Bildes ist.

### 6.5 Always keep zoomed in (d)

**Verhalten:** Schalter im Popover (Q4), nur sinnvoll, wenn das Ausgabeverhältnis vom Inhalt
abweicht (z.B. 9:16 aus 16:9); sonst deaktiviert. An: Der Inhaltsrahmen der Ausgabe wird
voll gefüllt, die Basis-Kamera ist das größte Rechteck mit dessen Seitenverhältnis in der
Quelle. Ohne Spur: fester Mittelpunkt (`keepCenter`), im Canvas wie ein Zoom-Ziel ziehbar.
Mit Spur: folgt dem Cursor (Totzone wie 6.2). Zoom-Segmente skalieren relativ zu dieser
Basis.

**Umsetzung:** `studioLayout` bekommt den Fall "Inhalt füllt den Rahmen"; fester Mittelpunkt
ist ein statischer Crop im schnellen Pfad, Cursor-Folge läuft über den Render-Pfad.
**Tests:** 16:9-Quelle nach 9:16: Ausgabe ohne Hintergrundbalken im Inhaltsrahmen, Crop
innerhalb der Quelle, Mitte an der erwarteten Stelle; mit Spur folgt die Mitte.
**Abhängigkeiten:** 6.1 (Ziel ziehen), für Folge 6.2. **Aufwand:** klein bis mittel.

### 6.6 Fragmente: Split, Schnitte, Speed (e)

**Verhalten**
- `S` teilt am Playhead. Klick auf den Filmstreifen bleibt Seek wie heute und wählt dabei
  das Fragment unter dem Playhead, sobald es mehr als eins gibt; die übrigen treten eine
  Helligkeitsstufe zurück (kein Rahmen).
- Timeline in **Ausgabezeit (Q5 = B)**: Geschnittene Fragmente verschwinden, ein 2×-Fragment
  ist halb so breit, zwischen Fragmenten liegt eine 2-px-Fuge. An einer Schnittstelle
  bleibt eine kleine Marke im Lineal; Klick darauf wählt den Schnitt, die Kontextleiste
  bietet "Restore". Trim-Griffe und -Felder funktionieren wie heute an den Enden.
- Kontextleiste für das gewählte Fragment: Speed (0.25×, 0.5×, 1×, 1.5×, 2×, 4×), Cut
  bzw. Restore. Zwei angrenzende gleichartige Fragmente werden
  beim Entfernen eines Splits wieder eins ("Join").
- Trim bleibt die äußere Grenze und funktioniert wie heute.
- Die Anzeige `Duration` zeigt die **Ausgabedauer**.
- Zoom-Segmente, die in einem Schnitt liegen, bleiben erhalten, wirken aber nicht.

**UI:** Q5 = B. Offene Detailfrage, in S4 gerendert und gefragt: Aussehen der
Schnittmarke und ihr Hover.
**Umsetzung:** `TimeMap` (3.3), Export 4.3, Vorschau 5. Speichert in Quellzeit.
**Tests:** Unit: `toOutput`/`toSource` inklusive Grenzen, Rundung auf Frames, Monotonie.
Export: generierter Clip, jede Sekunde eine andere Farbe und ein Ton-Piep pro Sekunde; nach
Schnitt und 2× stimmen Dauer (ffprobe), Farben an festen Ausgabezeiten und Pieps
(Zeitabweichung höchstens eine Frame-Dauer). Mit und ohne Tonspur.
Die Zoom-Spur liegt ebenfalls in Ausgabezeit: Ein Segment über einen Schnitt hinweg
erscheint durchgehend, Ziehen rechnet über `toSource` zurück in Quellzeit.
**Abhängigkeiten:** S0; für Zooms S2. **Aufwand:** mittel bis groß (die Timeline wird auf
`TimeMap` umgestellt).

### 6.7 Masken und Highlights mit Zeitfenster (f)

Entschieden (N1 = a): Zeitfenster nur für die vorhandenen Werkzeuge Redact (Blur,
Solid, OCR) und Spotlight. Neue Items gelten wie heute für den ganzen Clip. Wer ein Fenster
setzt, bekommt einen Eintrag in einer Masken-Spur unter der Zoom-Spur; Ränder ziehen ändert
das Fenster. Kontextleisten von Redact und Spotlight bekommen "Whole clip / From playhead".
Andere Annotationen bleiben statisch, der Ausschluss "zeitlich begrenzte Annotationen" im
21.09.-Plan gilt für sie weiter. Datenmodell: `timeWindow` (Quellzeit) am Item, im
Projektformat als optionales Feld. Export: `enable` im schnellen Pfad, Zeichnen bzw.
Blur-Filter mit `enable` im Render-Pfad. Masken-Spur und Leistenerweiterung werden vor S6
als eigene Designfrage gerendert (in Ausgabezeit, im Stil der Zoom-Spur). Der 21.09.-Plan
bekommt dazu einen Verweis auf diese Ausnahme. **Aufwand:** mittel.

### 6.8 GIF-Export (g)

**Verhalten:** GIF ist ein Format im Export-Popover (6.9). Preset GIF: kurze Seite 480,
15 fps. Palette pro Datei, ohne Ton. Ziel: Datei im Speicherort wie heute
und Copy als Datei-URI. **Shelf nur, wenn Boltsnap GIF-Karten annimmt**: heute bietet der
Shelf-Code nur `video/mp4` an; das klärt die Boltsnap-Session, Eddy fasst Boltsnap nicht an.
**Tests:** ffprobe (gif, fps, Größe, Dauer), Pixelprobe mit Palettentoleranz, mit Studio und
mit Zoom. **Abhängigkeiten:** keine für statisch, S1 für Zoom. **Aufwand:** klein.

### 6.9 Export-Presets (g)

**Verhalten (Q7 = B, schmal):** Kurzer Klick auf Save und Enter speichern unverändert wie
heute. Halten (oder ein kleiner Pfeil, wie bei Copy) öffnet ein Export-Popover am
Save-Button:

| Zeile | Wahl |
| --- | --- |
| Preset | Original · Web · Small · GIF |
| Format | MP4 · WebM · GIF |
| Size | Full · 1080 · 720 · 480 (kurze Seite) |
| Frames | 60 · 30 · 15 |

Presets setzen die Zeilen darunter: Original = heutiges Format, volle Größe, ≤ 60 fps;
Web = MP4, 1080, 60; Small = MP4, 720, 30; GIF = GIF, 480, 15. Ändert man eine Zeile, ist
kein Preset mehr markiert (kein eigener "Custom"-Chip). Unten links Ausgabegröße und Dauer in
Mono, **keine** geschätzte Dateigröße (wäre geraten); rechts die eine invertierte Aktion
"Save MP4/WebM/GIF". Die letzte Wahl merkt sich die Config. Keine Qualitäts- oder
Dateigrößenregler (bleibt abgewählt wie am 21.09.). Jede Zeile ist eine segmentierte
Leiste (Q7b = B2): `raise2`-Rille mit 2 px Innenrand, 20-px-Segmente, die Wahl eine
Helligkeitsstufe darüber (dunkel `#4A4A4A`, hell `#FFFFFF`; als Token festlegen). Popover
etwa 300 px breit.

Die Projektaktionen des 21.09.-Plans ("Save project…" usw.) waren für ein Save-Menü
gedacht. Mit dem Popover wandern sie in dessen Fußzeile; das wird mit P5/P6 gerendert und
entschieden.

**Datenmodell:** `VideoExportRequest` bekommt `format`, `maxShortSide`, `fps`.
**Tests:** jede Route (Datei, Copy, Drag, Shelf wo erlaubt) liefert das gewählte Format;
Größe gerade, Seitenverhältnis erhalten; Enter und kurzer Klick bleiben beim heutigen
Verhalten. **Aufwand:** klein bis mittel.

### 6.10 Teilbare Studio-Presets (g)

**Verhalten:** Im Popover ein Menü "Presets": Liste gespeicherter Presets (Klick wendet an,
ein Undo-Schritt), "Save current…", "Import…", "Export…". Ein Preset enthält Stil, Kamera-
Standard (Focused/Smooth), **keine** Zeitdaten.
**Format:** `~/.config/eddy/studio-presets/<name>.json`,
`{"format": "eddy.studio-preset", "version": 1, "name": …, "style": …, "camera": …}`.
Bildhintergrund wird beim Export eingebettet (PNG/JPEG, höchstens 3840 px, höchstens 8 MB) und
beim Import unter `~/.local/share/eddy/backgrounds/<sha256>.<ext>` abgelegt.
**Tests:** Round-Trip, Import mit Bild, kaputte und zu große Dateien, höhere Version abgelehnt.
**Aufwand:** klein.

### 6.11 Motion Blur (b, später und optional, N4 = a)

Nur im Render-Pfad: bei Kamerageschwindigkeit über einer Schwelle k = 6 Teilzeitpunkte im
halben Frame-Intervall rendern und mitteln, sonst ein Render. Stärke-Regler 0 bis 100 im
Popover (Camera). Keine Vorschau im Canvas (würde die Wiedergabe teuer machen), dafür im
Pause-Frame. Kosten bis 6× Renderzeit in Kamerafahrten, messen vor Abnahme.
**Aufwand:** klein bis mittel.

### 6.12 Audio und Untertitel (h), N2 = a

- Der Audio-Plan vom 21.09. (Wellenform, "Include audio in output") ist eigenständig und
  braucht Studio nicht. Mit Fragmenten muss nur zusätzlich gelten: Die Wellenform
  muss mit Q5 = B in Ausgabezeit gezeichnet werden: geschnittene Bereiche fehlen, schnelle
  Fragmente werden gestaucht (Peaks pro Pixel über den passenden Quellbereich). Ausgabeton
  folgt der `atrim`/`atempo`-Kette.
- Lautstärke und Normalisierung pro Dokument: nicht geplant.
- Untertitel: raus.

## 7. Tests und Abnahme, für jede Phase

### 7.1 Pflicht pro Phase

- `cmake --build build-rel --parallel 2`, `ctest --test-dir build-rel` grün, `git diff --check`.
- `chromeFollowsTheDensityRules` und `everyIconSharesOneGrid` bleiben grün. Neue Leisten
  nutzen Text-Buttons; falls doch Icons nötig sind, normalisiert über
  `tools/normalize_icons.py`.
- Generierte ffmpeg-Clips mit bekannten Farben und Markierungen, Pixelproben an festen
  Stellen. Keine Vollbild-Pixelgleichheit, sondern Proben mit Toleranz.
- Unit-Tests für Federn, Zeitabbildung und Modell (Grenzen, Determinismus).
- Echte Renders beider Themes über `eddy_preview` (neue Modi pro Phase) ansehen.
- Aussagen wie "funktioniert" nur mit Testlauf oder angesehenem Render. Kein Klicktest in
  einem echten Fenster wird behauptet, wenn er nicht stattfand.

### 7.2 Vorschau gleich Export

Für feste `t` (vor, während, nach einer Kamerafahrt): Canvas pausiert an `t` rendern
(Viewport-Grab bei bekannter Fit-Skalierung), Export-Frame an `t` decodieren, auf dieselbe
Größe skalieren. Markierungspunkte liegen auf ±1 px, mittlere Abweichung unter einer
Schwelle, die in Phase S2 an einem Referenzpaar festgelegt und dokumentiert wird. Dieselbe
Prüfung für Fragmente (Frame an Ausgabezeit).

## 8. Boltsnap

- Eddy liest nur. Keine Änderungen, keine Aufträge an die andere Session aus dieser Session.
- Offen auf Boltsnap-Seite, für Eddy nur Voraussetzungen: echte Sidecar-Dateien auf Platte
  (für die Abnahme von 6.2 bis 6.4), GIF-Annahme im Shelf (6.8).
- Replay-Clips haben keine Spur; dort gibt es nur Weg 2 und manuelle Zooms.

## 9. Phasen und Reihenfolge

| Phase | Inhalt | Braucht | Aufwand |
| --- | --- | --- | --- |
| S0 | `StudioDocument`, JSON-Codec, `TimeMap`, `CameraPath` mit Federn, Undo-Befehl. Keine UI | nichts | mittel |
| S1 | Render-Exportpfad (Pipeline, Renderer, Ton, Fortschritt/Stall/Abbruch), Referenztest gegen `maskedmerge` | S0 | mittel |
| S2 | Zoom-Segmente: Spur, Kontextleiste, Canvas-Ziel, Vorschau-Kamera, Export, Federn abstimmen; "keep zoomed in" mit festem Punkt | S1 | groß |
| S3 | Export-Presets und GIF (Save-Menü) | S1 für GIF mit Zoom | klein |
| S4 | Fragmente: Split, Cut, Speed, Vorschau, schneller Pfad und Render-Pfad | S0, S2 | mittel bis groß |
| S5 | Cursor-Spur: Follow Cursor, Vorschläge (Stillstand, Klicks), keep zoomed in mit Cursor | S2, echte Sidecars | mittel |
| S6 | Masken/Highlights mit Zeitfenster für Redact und Spotlight | S2 | mittel |
| S7 | Teilbare Studio-Presets | S0 | klein |
| S8 | Motion Blur (optional) | S1, S2 | klein bis mittel |

Verzahnung mit dem 21.09.-Plan, entschieden (N3 = a): S0 → S1 → S2 → **Projekt-Kern
P5** (speichert dann Crop, Trim, Annotationen und den Studio-Block; sonst gehen Zoom-Arbeiten
beim Schließen verloren) → S4 → S3 → S5 → P6 (Recovery) → S6/S7/S8 → Audio P3/P4 →
Annotation-Hilfen P7/P8.

Implementierungspläne, je Phase ein eigener, geschrieben kurz bevor die Phase beginnt:
S0 `docs/plans/2026-09-24-studio-s0-model.md`, S1 `docs/plans/2026-09-24-studio-s1-render-export.md`.

Umsetzungsstand: **S0 umgesetzt** (2026-09-24): `src/studiodocument.*`, `src/timemap.*`,
`src/camerapath.*` mit `test_studiodocument`, `test_timemap`, `test_camerapath`. Build grün,
`ctest` 31/31, `git diff --check` sauber. Noch nicht in der App verdrahtet.
**S1 umgesetzt** (2026-09-24): `src/studiorenderer.*`, `src/renderexport.*`,
`VideoExportRequest::zooms`, `writeVideoRendered`. Build grün, `ctest` 33/33. Renderpfad gegen
Filtergraph: mittlere Abweichung 0,6/255. Echter Clip (5 s, 2058×1190, 60 fps, Hardware-
Encoder): Filtergraph 2,47 bis 2,51 s, Renderpfad mit 2×-Zoom 2,81 bis 2,86 s. In der App erst
wirksam, wenn S2 Zoom-Segmente anlegt.
Committet am 2026-09-24 auf Branch `feat/studio` (Details:
`docs/handoffs/2026-09-24-studio-model-render-export-preview.md`).

Arbeitsweise pro Phase: Arbeitsbranch `feat/studio`, Commits nur auf Auftrag, Push erst nach
Freigabe. Subagents nur auf ausdrücklichen Wunsch. Builds mit `--parallel 2`.

## 10. Risiken

- Vorschau-Wiedergabe über Fragmentgrenzen: `QMediaPlayer`-Seeks können kurz haken. Messen
  in S4; Export ist davon nicht betroffen.
- Vorschau gleich Export hängt an der Frame-Startzeit aus `QVideoSink`; bei VFR-Quellen
  wird die Toleranz in S2 an einem VFR-Clip geprüft.
- 4K-Ausgabe unter Echtzeit (43 fps gemessen). Speicher per Byte-Grenze der Queues gedeckelt.
- Die Cursor-Spur muss zeitlich zum eingebrannten Cursor passen. Nicht an echten Dateien
  geprüft, solange keine `*.cursor.json` auf der Platte liegt; Follow Cursor wird in S5 an
  einer echten Aufnahme gegen den sichtbaren Pfeil kontrolliert.
- Timeline-Höhe wächst mit jeder Spur (Zoom 28 px, Masken 20 bis 28 px, Audio 28 px). Mindesthöhe
  des Videofensters und schmales Layout (520 px) in jeder Phase rendern.
- Wayland-Popup-Positionen und Presets-Menü im echten Fenster sind nur offscreen geprüft.
- Der einmalige `test_crop`-Watchdog-Hänger (nicht reproduziert). Tritt er wieder auf,
  erst Backtrace per gdb, dann weiter.

## 11. Designfragen mit gerenderten Varianten

Alle Varianten sind echte Qt-Renders im laufenden Eddy-Fenster (echtes QSS, echte Schrift,
echte Timeline, echter Clip `boltsnap-2026-09-21_18-25-58.mp4`), gebaut von der
Wegwerf-Probe `/tmp/studio-probe/variants.cpp` gegen `build-rel/libeddy_core.a`. Die neuen
Teile sind Attrappen aus echten Widgets bzw. mit den Timeline-Farben gemalt, keine
Implementierung. Dateien: `/tmp/studio-variants/q<N>-*-{dark,light}.png`.

Entschieden am 2026-09-24:

| Frage | Varianten | Entscheidung |
| --- | --- | --- |
| **Q1** Zoom-Spur (`q1-zoom-lane`) | A 20-px-Spur; B Band auf dem Filmstreifen; C 28-px-Spur mit Zoomkurve | **C** |
| **Q2** Kontextleiste (`q2-context-bar`) | A unten mittig im Canvas; B über dem Segment; C in der Wiedergabezeile | **A** |
| **Q3** Ziel im Canvas (`q3-canvas-target`) | A Kamerarahmen im Vollbild; B Zielpunkt im Vollbild; C im Zoom bleiben, Mini-Karte | **C** |
| **Q4** Popover-Erweiterung (`q4-popover`) | A ein langes Popover; B Seiten Style · Camera · Cursor | **B, mit Symbolen wo sinnvoll.** Seit 2026-09-24 ohne Cursor-Seite (6.4): Seiten Style · Camera, Camera nur bei Video |
| **Q5** Fragmente (`q5-fragments`) | A Quellzeit; B Ausgabezeit | **B** |
| **Q6** Paper-Kachel (`q6-paper-swatch`) | A wie heute; B Kacheln auf raise2; C Paper dunkler | **A**, bleibt wie heute; offener Punkt aus dem Handoff damit erledigt |
| **Q7** Export (`q7-export`) | A Menü am Save-Button; B Export-Popover | **B, schmalere und ruhigere Elemente** |

Nachgerendert und entschieden:

| Frage | Varianten | Entscheidung |
| --- | --- | --- |
| **Q4b** Symbole im Popover (`q4b-popover-symbols`, `q4c-popover-curves`) | B1 bis B3 mit Symbolen für Seiten, Pointer, Presets | **Keine Symbole außer bei Motion.** Seiten und Presets bleiben Text. Motion (Focused, Smooth, Instant) trägt die Kurve, die tatsächlich angewendet wird |
| **Q7b** Export-Popover (`q7b-export-narrow`) | B1 Chips in gemeinsamen Spalten; B2 segmentierte Reihen | **B2** |

**Motion-Symbole:** Sie werden nicht als feste SVGs gezeichnet, sondern zur Laufzeit aus
derselben Federfunktion wie die Kamera (`CameraSpring`, 3.4): Sprungantwort der kritisch
gedämpften Feder für Focused und Smooth, ein Sprung für Instant, alle auf derselben
Zeitachse (0,25 s Vorlauf, 1,2 s gezeigt), in die 24er-Box mit 20er-Live-Fläche und dem
Strich 2,8 des übrigen Icon-Satzes gesetzt (**Q4d:** dünnere Varianten 2,0 und 1,6 wurden bei
1× und 2× Pixeldichte gerendert, `q4d-motion-strokes`; entschieden: bei 2,8 bleiben).
Ändern sich die Federwerte (N6), ändern sich die Symbole mit. Gerendert mit den
Vorschlagswerten 100/20 und 36/12: Focused steigt sichtbar steiler, Smooth läuft länger aus.
Neben 13-px-Text 16-px-Box (13 px Tinte). Test: Tintenmitte und Ausdehnung wie
`everyIconSharesOneGrid`, und die gezeichnete Kurve trifft die Federfunktion an Stützstellen.

Zu allen Attrappen: Die Beschriftungen in den Attrappen sind Platzhaltertexte für die Frage
(z.B. "Keep zoomed in", "Suggest zooms", "Hide after 2 s"); Wortlaut wird mit der
Implementierung festgelegt.

## 12. Weitere Entscheidungen

Am 2026-09-24 entschieden:

- **N1 Masken/Highlights mit Zeitfenster: a.** Nur Redact und Spotlight bekommen ein
  optionales Zeitfenster und eine eigene Masken-Spur; alle anderen Annotationen bleiben
  statisch, der Ausschluss im 21.09.-Plan gilt für sie weiter.
- **N2 Audio und Untertitel: a.** Audio-Plan vom 21.09. unverändert (mit der
  Ausgabezeit-Wellenform aus 6.12), keine Untertitel, keine Lautstärke/Normalisierung.
- **N3 Reihenfolge: a.** Wie in Abschnitt 9, Projekt-Kern P5 direkt nach den Zooms.
- **N4 Motion Blur: a.** Später als S8, optional.
- **N5 Zoom-Vorschläge: a,** normale Segmente in einem Undo-Schritt. Zusätzlich Klicks als
  Signal, sobald eine Spur welche enthält (6.3).

Noch offen, weil es echte Clips oder Daten braucht:

- **N6 Federwerte der Kamera:** Vergleichsclips über den echten Renderpfad gerendert
  (2026-09-24, Boltsnap-Clip 14,0 bis 19,4 s, Focused 100/20 gegen 196/28, Smooth 36/12 gegen
  20/9, Übergabe zwischen zwei Zooms). Wahl des Maintainers steht aus; bis dahin gelten die
  Vorschläge aus 3.4. Die Clips lagen in `/tmp/studio-springs/` (nicht dauerhaft) und lassen
  sich mit einem kleinen Programm gegen `writeVideoWithOverlay` neu erzeugen.
