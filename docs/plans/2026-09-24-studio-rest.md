# Studio und Editor: alle restlichen Phasen (Plan)

Datum: 2026-09-24. Basis: Branch `feat/studio` bei `d2d4bf3` (S0 bis S2 umgesetzt, PR #13).
Auftrag: nach S2 "den Rest der Phasen" fertig machen, vorher alles planen (Verhalten und
Implementierung), danach reviewen. Quellen der Punkte: `docs/plans/2026-09-24-studio-features.md`
(S3 bis S8, Abschnitt 9) und `docs/plans/2026-09-21-crop-audio-projects-annotations.md` (P3 bis P8).
Beide Pläne beschreiben das Verhalten schon ausführlich; dieser Plan legt fest, was davon wie
gebaut wird, wo es im Code landet, was brechen kann und was noch zu entscheiden ist.

Arbeitsweise wie in S2: Welle für Welle, pro Punkt ein Commit mit seinem Test, vor jedem Commit
`cmake --build build-rel --parallel 2 && ctest --test-dir build-rel && git diff --check`. Keine
Subagents, Commits ohne Agent-Trailer, Boltsnap nur lesen, Hyprlock nie anfassen. Vor größeren
Wellen ein eigener Implementierungsplan mit Code wie für S2, wenn die Welle neue UI bringt.

## 1. Wellen und Abhängigkeiten

Reihenfolge nach N3 (Gesamtplan 9), mit einer Änderung als Entscheidung E1: der Export-Popover
(S3) vor den Projekt-Kern, weil die Projektaktionen in seine Fußzeile gehören.

| Welle | Punkte | Warum hier |
| --- | --- | --- |
| W1 | C1 Export-Popover und Presets, C2 GIF | klein, isoliert; schafft den Ort für Projektaktionen |
| W2 | A1 Projektformat, A2 Projekt speichern und öffnen | sichert Zoom-Arbeit, Grundlage für A3 |
| W3 | B1 Fragment-Modell und Timeline, B2 Fragment-Export, B3 Fragment-Vorschau | größter Timeline-Umbau |
| W4 | D1 Spur-Interpolation, D2 Follow Cursor, D3 Zoom-Vorschläge, D4 keep zoomed in mit Cursor | braucht S2, echte Spur liegt jetzt vor |
| W5 | A3 Autosave, Recovery, Resume | braucht A1 |
| W6 | E1 Zeitfenster Modell und Export, E2 Masken-Spur und Leisten, F1 Studio-Presets, F2 Motion Blur | braucht S2 und B2 |
| W7 | G1 Audio-Probe und Wellenform-Daten, G2 Wellenform-Spur, G3 Ton in der Ausgabe | braucht B1 (Ausgabezeit) |
| W8 | H1 Nummerierte Schritte, H2 Hilfslinien und Einrasten, H3 Ausrichten und Verteilen | unabhängig vom Video |

Querbezüge:
- Das Projektformat (A1) muss alles tragen, was spätere Wellen hinzufügen: Fragmente (B1, im
  Studio-Block schon da), Zeitfenster (E1), Export-Einstellungen (C1), Ton-Ausgabe (G3),
  Schritte (H1). Jede dieser Wellen erweitert den Codec um ihr optionales Feld und den Roundtrip-Test.
- Die Timeline wächst mit jeder Spur: Filmstreifen 30, Audio 28, Zoom 28, Masken 20 px plus
  Fugen. B1 stellt die Zeitachse auf `TimeMap` um; G2 und E2 zeichnen danach in derselben Achse.
- `VideoExportRequest` bekommt pro Welle Felder (C1 Format/Größe/fps, B2 Fragmente, D2 Spur,
  E1 Zeitfenster, F2 Motion Blur, G3 Ton). Der Filtergraph bleibt ohne diese Felder bitgleich.
- Der Export-Cache (`m_videoRevision`, `src/editorwindow.cpp:2046`) muss jede neue
  Einstellung als Änderung zählen, sonst liefert Copy/Drag eine veraltete Datei.

## 2. Punkte

### C1 Export-Popover und Presets (S3)

**Phase 1: Verhalten.** Gesamtplan 6.9, Q7 = B mit B2 (segmentierte Reihen).
- Kurzer Klick auf Save und Enter speichern wie heute. Halten von Save (oder Alt+Down) öffnet
  einen etwa 300 px breiten Popover am Save-Knopf, wie das Halten von Copy heute sein Menü
  öffnet (`src/toolbar.cpp:22-37`).
- Video: Reihen Preset (Original · Web · Small · GIF), Format (MP4 · WebM · GIF), Size (Full ·
  1080 · 720 · 480, kurze Seite), Frames (60 · 30 · 15). Presets setzen die Reihen; eine
  geänderte Reihe hebt die Preset-Markierung auf. Unten links Ausgabegröße und Dauer in Mono,
  rechts die invertierte Aktion "Save MP4/WebM/GIF". Die Wahl gilt für jede Route (Datei,
  Copy, Drag, Shelf) und wird in der Config gemerkt (`[export]`).
- Bild: keine Format-Reihen (PNG bleibt), der Popover zeigt nur die Fußzeile mit den
  Projektaktionen (A2). Bis A2 existiert, öffnet Halten bei Bildern nichts.
- Original = heutiges Verhalten (Quellformat, volle Größe, höchstens 60 fps, Audio wie heute).
- Keine Qualitäts- oder Dateigrößenregler, keine geschätzte Dateigröße.

**Phase 2: Bau.**
- Neu `src/exportpopover.{h,cpp}` nach dem Muster von `StudioPopover` (Popup, `raise1`,
  translucent, `WA_DeleteOnClose`), Segmentreihen als `QButtonGroup` mit Text-Buttons. QSS-Token
  für das gewählte Segment (dunkel `#4A4A4A`, hell `#FFFFFF`) als `@segment-on`.
- `src/videoexporter.h`: `struct ExportFormat { enum Kind { Original, Mp4, WebM, Gif } kind; int maxShortSide = 0; int fps = 0; }`
  und `VideoExportRequest::format`. Filtergraph: nach Crop/Studio `scale` auf die kurze Seite
  (gerade Maße, lanczos), `fps=`; Codec nach Format statt nach Endung (`writeVideo`,
  `src/videoexporter.cpp:236`). Render-Pfad (`writeRendered`, `:170`): gleiche Skalierung und
  fps hinter der Encode-Pipe.
- `src/editorwindow.cpp`: `createVideoTempPath` (`:2031`) und die Dateinamen in `saveVideo`
  (`:2347`) nehmen die Endung aus dem Format; Änderungen am Format erhöhen `m_videoRevision`.
  `src/toolbar.cpp:240`: Save bekommt `DelayedPopup` und ein Signal `exportMenuRequested`.
- `mediaMimeTypeForPath` (`src/mediaio.h:54`) kennt `image/gif` und `video/webm` für Drag und Copy.

**Was brechen kann.** Boltsnap-Karten-Ersatz (`replaceVideoCard`) erwartet vermutlich den
Dateityp der Karte: bei gesetzter Karte bleibt das Format Original, der Popover zeigt das.
Shelf nimmt nur `video/mp4` (Gesamtplan 6.8): WebM und GIF gehen bei der Shelf-Route in die
Zwischenablage mit Toast. Tests, die Endungen oder Argumentlisten prüfen, müssen mit Original
unverändert grün bleiben.

**Test.** `test_videoexporter`: je Format ffprobe (Codec, Größe gerade, Seitenverhältnis, fps).
`test_exportpopover` (neu): Presets setzen Reihen, Einzeländerung hebt Preset auf.
`test_editorwindow`: Enter und kurzer Klick bleiben beim heutigen Weg; mit Web-Preset endet die
gespeicherte Datei auf `.mp4` mit 1080 als kurzer Seite; Formatwechsel macht den Cache ungültig.
**Prüfung.** Renders des Popovers in beiden Themes, schmales Fenster.

### C2 GIF-Export (S3)

**Phase 1.** Format GIF: Preset GIF = 480 kurze Seite, 15 fps, eine Palette pro Datei, kein Ton.
Datei und Copy (als Datei-URI) wie heute; Shelf siehe C1. Mit Zoom über den Render-Pfad.

**Phase 2.** Filterkette aus Gesamtplan 4.3 (`palettegen=stats_mode=diff`, `paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle`)
im Filtergraph hinter Crop/Studio und im Render-Pfad hinter der Encode-Pipe; `-an`.
**Was brechen kann.** Lange GIFs werden sehr groß; kein Limit geplant (abgewählt), aber die
Dauer steht in der Fußzeile. Hardware-Encoder-Pfad darf für GIF nicht greifen.
**Test.** ffprobe: `gif`, fps 15, kurze Seite 480, Dauer; Pixelprobe mit Palettentoleranz; einmal mit Studio, einmal mit Zoom.

### A1 Projektformat (P5)

**Phase 1.** 21.09.-Plan 5 "Einfaches, dauerhaftes Format" und "Datenmodell". Ein Projekt ist
`name.eddy` (JSON) plus `name.eddy.assets/` mit einer unveränderlichen Kopie der Quelle.
Es enthält alle Annotationen (jeder Typ vollständig), Crop, Trim, den Studio-Block
(`studioToJson`), die Export-Einstellungen und später Ton-Ausgabe und Schritte. Kein
gerendertes Ergebnis, keine Undo-Historie. Ungültige Dateien werden mit Grund abgelehnt,
nie still repariert.

**Phase 2.**
- Neu `src/projectcodec.{h,cpp}`: `QJsonObject sceneToJson(const QGraphicsScene &, …)` und
  `std::optional<ProjectSnapshot> projectFromJson(…, QString *error)`, reine Funktionen ohne
  Dateizugriff. Pro Typ ein expliziter Eintrag: Arrow (`src/items/arrowitem.h:13-14`), Rect,
  Ellipse, Highlight (`rect()`), Pen (braucht einen Getter `points()`, heute nur `pointCount`,
  `src/items/penpathitem.h:9`), Text (`TextState`, `src/items/textitem.h:8-15`), Redact (Modus,
  Region, Textrechtecke, `src/items/redactitem.h:27-32`), Spotlight (Region, Form, Stärke,
  `src/items/spotlightitem.h:15-18`); dazu Position, Z-Wert, Farbe, Strichbreite. Grenzen wie im
  Studio-Codec (Anzahl, Punkte, endliche Zahlen).
- Neu `src/projectstore.{h,cpp}`: Schreiben mit `QSaveFile` (Manifest zuletzt), Asset-Kopie im
  Hintergrund-Thread mit SHA-256 (`QCryptographicHash`) und Fortschritt, Laden mit Hash-Prüfung.
  Stdin-Bilder als PNG-Asset.
- `EditorWindow` bekommt `ProjectSnapshot snapshot() const` und `void applySnapshot(const ProjectSnapshot &)`;
  `applySnapshot` baut Items über dieselben Klassen wie `ToolController::begin`
  (`src/toolcontroller.cpp:101`) und wendet OCR-Rechtecke erst nach der Geometrie an.

**Was brechen kann.** `hasVideoAnnotations` zählt heute alles über z = -1000
(`src/editorwindow.cpp:1877`); geladene Items müssen dieselben Flags und Z-Werte haben.
Redact-Items brauchen das Quellbild (`setSource`) nach dem Laden eines Videos erst mit dem
ersten Frame. Großer Video-Kopiervorgang darf die UI nicht blockieren.

**Test.** `test_projectcodec` (neu): Roundtrip jedes Typs inklusive Pen-Punkten, Textmetriken,
Z-Reihenfolge und OCR-Rechtecken; kaputte, zu große und höhere Versionen abgelehnt.
`test_projectstore` (neu): Asset-Kopie, Hash-Fehler erkannt, Abbruch lässt ein altes Projekt
unverändert, Schreibfehler sichtbar. Visuell: gespeichert und geladen rendert
`exportComposite` pixelgleich.

### A2 Projekt speichern und öffnen (P5)

**Phase 1.** Aktionen `Save project…` (Ctrl+Shift+S), `Save project as…`, `Open project…`
(Ctrl+O) in der Fußzeile des Export-Popovers (E1/E2). Erster Speicherdialog nennt den
Begleitordner ("Includes the original and editable layers"). Weitere Projekt-Saves ohne
Dialog. `eddy name.eddy` öffnet ein Projekt; das Video startet pausiert an der gespeicherten
Stelle. Ctrl+S bleibt die Ausgabe. Fehlende Quelle: "Locate original" mit Hash-Prüfung.

**Phase 2.** `src/main.cpp:73` erkennt `.eddy` vor `loadMediaInput` und lädt über `ProjectStore`.
Fußzeile im Export-Popover (C1). Normale Ausgabe darf nie das Asset oder das Manifest
überschreiben (`sameExistingPath`-Prüfung in `writeVideo` erweitern). Desktop-Datei bekommt
den MIME-Typ `application/x-eddy-project`.
**Was brechen kann.** Boltsnap-Karten-IDs und Stdout-Routen werden nicht aus dem Projekt
übernommen (21.09.-Plan). CLI-Parser (`src/cli.h:27`) bleibt unverändert, nur die Endung zählt.
**Test.** Editor-Test: speichern, neues Fenster aus dem Projekt, Annotationen, Crop, Trim und
Zooms gleich, Export gleich; Speichern unter neuem Namen kopiert das Asset; Ausgabe auf den
Asset-Pfad wird abgelehnt.

### A3 Autosave, Recovery und Resume (P6)

**Phase 1.** 21.09.-Plan 5 "Autosave, Recovery und Quell-Lebensdauer": Recovery-Snapshot
2 s nach der letzten abgeschlossenen Änderung, spätestens nach 10 s; nie mitten in einem
Ziehen. Liegt im App-Datenverzeichnis, nicht in `/tmp`. Quelle einmal kopiert (Budget 2 GiB,
konfigurierbar). Benannte Projekte werden nie ungefragt überschrieben. `eddy --resume` und
`Resume editing…` zeigen eine kurze Liste (Datei, Zeit, Status, Discard). Beim erneuten
Öffnen derselben Quelle bietet ein Toast den neueren Stand an.

**Phase 2.** Neu `src/recoverystore.{h,cpp}` (Einträge `recovery/<id>/`, Manifest wie A1,
`QLockFile` pro Eintrag, Budget). Timer an `QUndoStack::indexChanged` im `EditorWindow`, gesperrt
während `m_timelineActive`, Kamera-Gesten und Textbearbeitung. `src/cli.cpp` bekommt
`--resume` (ohne Eingabedatei), `src/main.cpp` zeigt dann den Resume-Dialog (neu
`src/resumedialog.{h,cpp}`, einfache Liste). Schließen (`closeEvent`, `:601`) schreibt einen
letzten Snapshot begrenzt synchron.
**Was brechen kann.** Temporäre Shelf-Quellen verschwinden: die Quelle muss vor der
Bestätigung "wiederherstellbar" kopiert sein. Zwei Fenster derselben Quelle: Lock verhindert
Überschreiben. Plattenplatz voll: einmal sichtbar melden, Bearbeitung geht weiter.
**Test.** `test_recoverystore`: Snapshot nach Änderung, kein Snapshot mitten im Ziehen,
Budget, Lock, Discard, Wiederherstellung ohne ursprüngliche Datei; CLI-Test für `--resume`.

### B1 Fragmente: Modell und Timeline (S4)

**Phase 1.** Gesamtplan 6.6 und Q5 = B. `S` teilt am Playhead. Klick auf den Filmstreifen
bleibt Seek und wählt das Fragment unter dem Playhead, sobald es mehrere gibt; die übrigen
treten eine Helligkeitsstufe zurück. Kontextleiste (wie die Zoom-Leiste): Speed (0.25×, 0.5×,
1×, 1.5×, 2×, 4×), Cut bzw. Restore. Die Timeline zeigt die bearbeitete Zeit: geschnittene
Fragmente verschwinden, 2× ist halb so breit, zwischen Fragmenten eine 2-px-Fuge, an einem
Schnitt eine Marke im Lineal (Entscheidung E3); Klick darauf wählt den Schnitt, die Leiste
bietet Restore. Gleiche Nachbarn nach Restore verschmelzen wieder. `Duration` zeigt die
Ausgabedauer. Trim bleibt die äußere Grenze. Zooms in geschnittenen Bereichen bleiben
gespeichert, wirken aber nicht (so baut `CameraPath` sie schon).

**Phase 2.**
- Neu `src/fragments.{h,cpp}`: reine Operationen `split`, `setSpeed`, `cut`, `restore`, `join`,
  `fragmentAt` über `QVector<Fragment>` (`src/studiodocument.h`).
- `VideoTimeline`: eine Achse `setTimeAxis(const TimeMap &)`, gebaut ohne Trim (nur Fragmente),
  damit die Trim-Griffe an den Enden bleiben. `xForTime`/`timeForX` (`src/videotimeline.cpp:234`)
  rechnen über die Achse, öffentliche Werte bleiben Quellzeit. Thumbnails
  (`thumbnailTimes`, `:87`) und Hover fragen `toSource`. Zoom-Spur und spätere Spuren nutzen
  dieselbe Abbildung.
- `EditorWindow`: `S`, Fragment-Auswahl, `FragmentBar` (neu, nach `ZoomBar`), Edits über
  `editStudio` (ein Undo-Schritt). `TimeMap` in `rebuildCamera` hat die Fragmente schon.

**Was brechen kann.** Jede Stelle, die heute Zeit linear in x umrechnet: Trim-Ziehen,
Kantenschwenk, Lineal-Beschriftung, Hover-Vorschau, Zoom-Spur. Ohne Fragmente muss die Achse
exakt die Identität sein (alle Timeline-Tests bleiben unverändert).
**Test.** `test_fragments` (neu): Split an Grenzen, Speed-Grenzen, Cut/Restore/Join.
`test_timemap` erweitert (Rundung auf Frames). `test_videotimeline`: Achse mit Schnitt und 2×,
Klick auf die Schnittmarke. Editor: `S` ist ein Undo-Schritt, Duration zeigt Ausgabedauer.

### B2 Fragmente im Export (S4)

**Phase 1.** Export exakt nach Fragmenten: Schnitte fehlen, Speed ändert Bild und Ton,
Zeitfenster und Zooms folgen der Ausgabezeit.

**Phase 2.** Filtergraph (Gesamtplan 4.3): nach Blur und Overlay `split` auf n behaltene
Fragmente, je `trim` und `setpts=(PTS-STARTPTS)/speed`, `concat`, dann `fps=60`; Ton je `atrim`,
`asetpts`, `atempo`-Kette (Faktoren unter 0,5 als Kette), `concat=a=1`. `VideoInfo` bekommt
`hasAudio` (`src/mediaio.h:16`, Probe `src/mediaio.cpp` um `a:0`). Render-Pfad: dieselbe Kette
im Decoder, `TimeMap` mit Fragmenten statt `{}` in `writeRendered` (`src/videoexporter.cpp:170`).
`VideoExportRequest::fragments`.
**Was brechen kann.** Heute beginnt die Eingabe bei Trim-In (`-ss`); Fragmentzeiten müssen
relativ dazu stehen. Stream-Copy des Tons ist mit Fragmenten nicht mehr möglich.
**Test.** Generierter Clip mit Farbe pro Sekunde und Piep pro Sekunde: nach Schnitt und 2×
stimmen Dauer (ffprobe), Farben an festen Ausgabezeiten und Pieps (höchstens ein Frame
Abweichung); mit und ohne Tonspur; einmal über den Render-Pfad mit Zoom.

### B3 Fragmente in der Vorschau (S4)

**Phase 1.** Wiedergabe überspringt Schnitte und spielt Fragmente mit ihrem Speed (mal
Vorschau-Speed). Kleine Seek-Ruckler an Schnitten sind möglich und werden gemessen.
**Phase 2.** Im `positionChanged`- und `videoFrameChanged`-Weg: erreicht die Position das Ende
eines behaltenen Fragments, Seek auf den nächsten behaltenen Anfang (`TimeMap::toOutputAfter`),
`setPlaybackRate(speed × preview)`. Loop und Trim-Ende laufen über die Ausgabezeit.
**Was brechen kann.** Der bestehende Seek-Zustand (`m_seekSettling`, Loop) darf nicht hängen
bleiben; `realVideoScrubCopyLoopAndStop` bleibt grün.
**Test.** Editor-Test mit generiertem Clip: Wiedergabe über einen Schnitt zeigt nie ein Frame
aus dem Schnitt (Farbe), Rate im 2×-Fragment 2.

### D1 Spur-Interpolation bei Stillstand

**Phase 1 (Fix).** Boltsnap schreibt nur Bewegungs-Events und dünnt auf 120 Hz aus
(`~/projects/boltsnap/src/record/cursor.rs`, `write_sidecar`); eine Lücke zwischen zwei Samples
heißt, der Zeiger stand still. Die echte Spur vom 2026-09-24 hat Lücken bis 4,2 s.
`CursorTrack::positionAt` interpoliert linear zwischen allen sichtbaren Samples
(`src/cursortrack.cpp:17`) und erfindet damit eine langsame Drift über die Lücke. Für
Follow Cursor und Stillstands-Erkennung ist das falsch.
**Phase 2.** Ab einer Lücke über 50 ms hält `positionAt` die alte Position bis kurz vor das
nächste Sample (dann linear über die letzten 8,33 ms, eine Sample-Periode). Spec
`docs/specs/2026-09-23-studio-mode.md` (Vertrag v1) um den Satz ergänzen.
**Test.** `test_cursortrack`: Lücke von 2 s hält die Position, danach Bewegung; schlägt vor
dem Fix fehl. Gegen die echte Spur: Stillstände an den Lückenstellen.

### D2 Follow Cursor

**Phase 1.** Gesamtplan 6.2: Ziel "Cursor" in der Kontextleiste (Point · Cursor), nur mit
gültiger Spur, sonst deaktiviert mit Tooltip "Needs a Boltsnap cursor track". Totzone 40 % des
Ausschnitts; Ziel bewegt sich nur, bis der Cursor wieder auf dem Rand liegt; hält bei `null`.
Die Feder glättet.
**Phase 2.** `CameraPath` bekommt eine optionale Spur (Zeiger, Quellzeit über `TimeMap::toSource`)
und berechnet das Ziel pro 240-Hz-Schritt. `ZoomBar` bekommt die Point/Cursor-Reihe.
`VideoExportRequest::cursorTrack` (geteilte Kopie) für den Render-Pfad. Die Vorschau zeigt beim
gewählten Cursor-Zoom den Zielausschnitt am Playhead statt eines festen Punktes.
**Was brechen kann.** Rechenzeit der Pfadsimulation mit Spur (Spur-Lookup pro Schritt: binäre
Suche); die Kamera bleibt geklemmt. Spur- und Videozeit müssen übereinstimmen (Gesamtplan 10):
an der echten Aufnahme gegen den eingebrannten Pfeil prüfen.
**Test.** `test_camerapath`: synthetische Spur (Sprung, Kreis, Pause, Lücke), Kamera bleibt in
der Quelle, Totzone, Halten. Export-Frame: Pfeilposition im Ausschnitt an der echten Aufnahme angesehen.

### D3 Zoom-Vorschläge

**Phase 1.** Gesamtplan 6.3 mit N5 = a: "Suggest zooms" auf der Camera-Seite, nur mit Spur;
fügt normale Segmente in einem Undo-Schritt ein. Klickgruppen haben Vorrang (heute liefert
Boltsnap keine), sonst Stillstand 0,5 bis 2,6 s in 1,5 % der Diagonale; Abstand 1,8 s zu
bestehenden Zooms; nur in behaltenen Fragmenten.
**Phase 2.** Neu `src/zoomsuggest.{h,cpp}` (reine Funktion über `CursorTrack`, `TimeMap`,
bestehende Zooms); Knopf auf der Camera-Seite (`src/studiopopover.cpp`, Zeile "Zoom").
**Test.** `test_zoomsuggest` (neu): bekannte Stillstände ergeben erwartete Segmente, Abstände,
Randfälle (Anfang, Ende, im Schnitt, über Lücke), Klickgruppen vor Stillstand; an der echten
Spur eine plausible Zahl Vorschläge (angesehen, nicht im Test fixiert).

### D4 keep zoomed in mit Cursor

**Phase 1.** Gesamtplan 6.5: Mit Spur folgt die Basis dem Cursor (Totzone wie D2); ohne Spur
fester Mittelpunkt wie heute. Entscheidung E10: automatisch folgen, bis der Nutzer die Mitte
zieht (dann fest).
**Phase 2.** `StudioDocument` bekommt `keepFollowsCursor` (JSON optional). `CameraPath` rechnet
die Basis pro Schritt; Export über den Render-Pfad, sobald die Basis der Spur folgt
(`timeVarying`).
**Test.** 16:9-Quelle nach 9:16 mit Spur: Mitte folgt; Export-Frames an zwei Zeitpunkten.

### E1 Zeitfenster für Redact und Spotlight: Modell und Export (S6)

**Phase 1.** Gesamtplan 6.7 mit N1 = a: optionales `timeWindow` (Quellzeit) nur an Redact und
Spotlight. Ohne Fenster gilt das Item für den ganzen Clip wie heute. In der Vorschau blenden
sich Items nach dem Playhead ein und aus. Export zeigt sie nur im Fenster.
**Phase 2.** `RedactItem`/`SpotlightItem` (`src/items/redactitem.h`, `spotlightitem.h`) bekommen
`std::optional<QPair<qint64, qint64>> timeWindow`. Blur: `appendBlurFilters`
(`src/videoexporter.cpp:70`) mit `enable='between(t,a,b)'` in der Zeitbasis vor dem
Fragment-`split`. Spotlight und andere Items liegen im statischen Overlay; zeitgebundene Items
bekommen eigene Overlay-Bilder mit `enable`, im Render-Pfad pro Frame gezeichnet.
`renderAnnotationOverlay` (`src/editorwindow.cpp:1850`) trennt statische und zeitgebundene Items.
**Was brechen kann.** Heute gibt es genau ein Spotlight pro Dokument
(`src/toolcontroller.cpp:142`, "Replace Spotlight"); siehe E6. Blur-Rechtecke aus
`blurRectsInScene` müssen ihr Fenster mitnehmen. Projekt-Codec (A1) bekommt das Feld.
**Test.** Export mit Blur nur 1 bis 2 s: Pixelproben davor, darin, danach; Spotlight ebenso;
mit Fragmenten verschoben korrekt.

### E2 Masken-Spur und Leisten (S6)

**Phase 1.** Eine Masken-Spur (Entscheidung E4) unter der Zoom-Spur zeigt jedes Item mit
Fenster als Block ("Blur", "Spotlight"); Ränder ziehen ändert das Fenster, Klick wählt das Item.
Redact- und Spotlight-Leiste bekommen "Whole clip · From playhead" (Entscheidung E5 für die Länge).
**Phase 2.** `VideoTimeline` bekommt eine zweite Spur nach dem Muster der Zoom-Spur (Logik in
`zoomlane` ist allgemein genug für "Blöcke ohne Überlappung pro Item", hier dürfen Blöcke
verschiedener Items überlappen: eigene Zeilenlogik). `RedactBar`/`SpotlightBar`
(`src/redactbar.h`, `src/spotlightbar.h`) um das Segment erweitern; Undo über einen neuen
`SetTimeWindowCommand`.
**Test.** Timeline-Tests wie für die Zoom-Spur; Editor: "From playhead" ist ein Undo-Schritt,
Item verschwindet in der Vorschau vor dem Fenster.

### F1 Teilbare Studio-Presets (S7)

**Phase 1.** Gesamtplan 6.10: Menü "Presets" oben rechts im Studio-Popover: gespeicherte
Presets (Klick wendet an, ein Undo-Schritt), "Save current…", "Import…", "Export…". Inhalt:
Stil und Kamera-Standard, keine Zeitdaten. Bildhintergrund eingebettet (höchstens 3840 px, 8 MB).
**Phase 2.** Neu `src/studiopresets.{h,cpp}`: Format `eddy.studio-preset` v1, Ablage
`~/.config/eddy/studio-presets/`, Bilder nach `~/.local/share/eddy/backgrounds/<sha256>.<ext>`.
Codec nutzt den Stil-Teil von `studioToJson`.
**Test.** `test_studiopresets` (neu): Roundtrip, Import mit Bild, kaputte und zu große Dateien,
höhere Version abgelehnt; Editor: Anwenden ist ein Undo-Schritt.

### F2 Motion Blur (S8)

**Phase 1.** Gesamtplan 6.11 mit N4 = a: Regler "Motion blur" 0 bis 100 auf der Camera-Seite,
Standard 0 (aus). Nur im Render-Pfad; bei Kamerageschwindigkeit über einer Schwelle k = 6
Teilzeitpunkte im halben Frame-Intervall rendern und mitteln. Vorschau: Entscheidung E7.
**Phase 2.** `StudioDocument::motionBlur` (JSON optional), `CameraPath::speedAt`,
`StudioRenderer::render` mit mehreren Kamerarechtecken und Mittelung (Premultiplied-Summe).
`timeVarying()` wird bei Blur > 0 und Zooms wahr (schon vorgesehen, `src/studiodocument.cpp`).
**Was brechen kann.** Bis 6× Renderzeit in Kamerafahrten: messen am echten Clip, vor Abnahme.
**Test.** Renderer: bei stehender Kamera bitgleich zu ohne Blur, in der Fahrt weicher (Kanten-
Gradient breiter); Export-Zeit gemessen und dokumentiert.
**Umgesetzt.** Statt `CameraPath::speedAt` vergleicht der Export die Kamera am Anfang und Ende
des Verschlusses (Blur/100 × halbes Frame-Intervall); ab 0,5 Ausgabe-Pixel Weg (gemessen an der
Kamerabreite) werden 6 Zeitpunkte gerendert und laufend gemittelt
(`StudioRenderer::renderBlurred`), sonst einmal. `timeVarying()` bleibt unverändert: ohne
Zoom oder Cursor-Basis steht die Kamera, Blur hat dann nichts zu tun. Regler "Blur" mit
"Export only" auf der Camera-Seite, Teil der einen Undo-Sitzung. Die Export-Zeit am echten
Clip ist noch nicht gemessen; Mehraufwand fällt nur in Frames mit Kamerafahrt an.

### G1 Audio-Probe und Wellenform-Daten (P3)

**Phase 1.** 21.09.-Plan 4 "Berechnung und Ressourcen" und "Audio-Zeitbasis": Peaks und RMS
asynchron, 10-ms-Bins, Cache begrenzt, Zeitachse gleich der Videoachse.
**Phase 2.** `VideoInfo` bekommt Tonspuren (Anzahl, Start-Offset); neu
`src/audiowaveform.{h,cpp}` (`AudioWaveformProvider`, ffmpeg-Prozess auf eigenem Thread,
`-f f32le` gestreamt, Min/Max/RMS pro Bin, Stufen für Zoom). Zeitursprung über ffprobe
(`start_time` von Audio- und Videostream).
**Was brechen kann.** Speicher bei Stunden-Clips (Grenze eine Million Bins); CPU parallel zum
Export (Provider pausiert während eines Exports).
**Test.** `test_audiowaveform` (neu): Klick-Fixtures bei bekannten Zeiten, Stille, gegenphasiges
Stereo sichtbar, positiver Startversatz; Abbruch räumt den Prozess ab.

### G2 Wellenform-Spur (P4)

**Phase 1.** 21.09.-Plan 4 "Oberfläche und Navigation": 28-px-Spur direkt unter dem
Filmstreifen (Entscheidung E8 zur Reihenfolge), symmetrische graue Hüllkurve, feste Skala,
dieselbe Achse (mit Fragmenten gestaucht), Klick/Ziehen scrubbt, Kontextmenü "Show waveform".
Ohne Tonspur keine Spur.
**Phase 2.** `VideoTimeline` zeichnet die Spur aus den Provider-Stufen pro Pixel; unbekannte
Bereiche sichtbar anders als Stille.
**Test.** Timeline-Render mit synthetischen Peaks; Editor: Spur nur mit Ton; Scrubben auf der
Spur seekt.

### G3 Ton in der Ausgabe (P4)

**Phase 1.** Speaker-Menü unter dem Lautstärkeregler: "Include audio in output" (Standard an);
aus zeigt "No audio" neben dem Speaker. Undo-fähiger Dokumentedit, im Projekt gespeichert.
Ausgabe ohne Ton hat keinen Audiostream, auch bei sonst unbearbeitetem Clip (Stream-Copy des
Videos, wo möglich).
**Phase 2.** `VideoExportRequest::includeAudio`; `-an` bzw. keine Audio-Map; `hasVideoEdits`
zählt "No audio" als Edit. `SetOutputAudioCommand`.
**Test.** ffprobe: kein Audiostream; mit Ton bleibt er; Undo.

### Umsetzungsplan W7 (vor dem Bau, 24.09.)

Grundlage: 21.09.-Plan Abschnitt 4. Reihenfolge G1, G2, G3, je ein Commit mit Tests.

**G1.** `VideoInfo` bekommt `audioOffsetMs` (Start des ersten Audiostreams minus Start des
Videostreams, per ffprobe `stream=start_time`). Neu `src/audiowaveform.{h,cpp}`:
`AudioWaveformProvider` (QObject) startet einen ffmpeg-Prozess
`-i src -map 0:a:0 -ac 2 -ar 16000 -f f32le -`, liest stdout asynchron im GUI-Thread
(`readyRead`, Stücke bis 256 KiB, Ausrichtung auf ganze Frames) und füllt 10-ms-Bins
(Peak = maximaler Betrag über beide Kanäle, RMS aus Quadratsumme; kein Mono-Downmix, also bleibt
gegenphasiges Stereo sichtbar). Höchstens eine Million Grund-Bins, sonst gröbere Bins. Eine
Aggregationsstufe je Faktor 16 für das Zeichnen. Der Versatz verschiebt die Bins; vor dem Ton
ist Stille, nicht "unbekannt". Zustand `Loading`/`Ready`/`Failed`, `changed()` höchstens alle
100 ms, Stillstands-Timeout 30 s, Destruktor/`cancel()` beendet den Prozess. Der Prozess läuft
mit `nice 10` (`setChildProcessModifier`). API: `summary(fromMs, toMs)` liefert
{peak, rms, known} für einen Pixelbereich.
Bewusst weggelassen (YAGNI, ffmpeg dekodiert Audio weit schneller als Echtzeit): priorisierter
Bereichsauftrag, Festplatten-Cache, Trackwahl (angezeigt wird die erste Tonspur, die auch der
Player nimmt).
Test `test_audiowaveform`: Klicks bei 1,0 s und 2,5 s liegen im richtigen Bin, Stille ist
bekannt und leer, gegenphasiges Stereo hat Pegel, positiver Versatz (0,5 s `-itsoffset`)
verschiebt den Klick, Abbruch beendet den Prozess, Datei ohne Ton scheitert sauber.

**G2.** `VideoTimeline::setWaveform(const AudioWaveformProvider *)` und
`setWaveformVisible(bool)`: 28-px-Spur direkt unter dem Filmstreifen, Zoom- und Masken-Spur
rücken nach unten (E8). `waveformRect()` wie die anderen Spuren; Höhe +32. Zeichnen pro
Pixel über `summary()` in editierter Zeit (mit Fragmenten gestaucht), symmetrische graue
Hüllkurve mit hellerem RMS-Kern, feste Skala; unbekannt: gestrichelte Mittellinie; Fehler:
kleiner Text "Waveform unavailable". Klick/Ziehen auf der Spur scrubbt wie der Filmstreifen
(fällt in den vorhandenen Seek-Zweig), die Trim-Griffe greifen auch dort. Kontextmenü
"Show waveform" (abhakbar, nur mit Ton), Ansichtswert, kein Dokumentedit. Editor legt den
Provider nur bei `hasAudio` an.
Test: Timeline mit synthetischem Provider (Pixel in der Spur dunkler bei Pegel, unbekannt
anders als Stille), Spur verschiebt die Zoom-Spur, Klick in der Spur seekt; Editor ohne Ton
ohne Spur.

**G3.** `StudioDocument::audio` (Standard an, JSON `"audio"` optional) statt eigener
`SetOutputAudioCommand`: Undo, Projekt und Kept Edits laufen so über den vorhandenen
`SetStudioDocumentCommand`. Speaker-Menü: abhakbare Aktion "Include audio in output" unter
dem Regler (nur bei Ton aktiv), Label "No audio" neben dem Speaker, wenn aus.
`hasVideoEdits()` zählt `!audio`. `VideoExportRequest::includeAudio`: Render-Pfad und
Filtergraph lassen die Audio-Map weg und setzen `-an`. Stream-Copy für "nur ohne Ton" entfällt
vorerst (der Plan sagt "kann"); das Ergebnis ist gleich, nur der Encode dauert.
Test: Exporter ohne Ton hat per ffprobe keinen Audiostream, mit Ton einen (beide Pfade);
Editor: Aktion schaltet, Undo stellt zurück, Label sichtbar; Codec-Roundtrip.

### H1 Nummerierte Schritte (P7)

**Phase 1.** 21.09.-Plan 6 "Nummerierte Schritte": Werkzeug Step (`N`), Kreis mit Nummer in der
Annotationsfarbe, Klick setzt 1, 2, 3; die nächste Nummer ist eine gespeicherte Folge.
Duplizieren nimmt die nächste Nummer. Leiste: Nummernfeld, S/M/L, Menü "Renumber by creation
order" (ein Undo-Schritt). Mehrstellige Nummern vergrößern den Kreis. Platz im Rail:
Entscheidung E9.
**Phase 2.** Neu `src/items/stepitem.{h,cpp}` (`AnnotationItem`), `ToolType::Step`
(`src/toolcontroller.h:13`), Icon `step.svg` normalisiert (`tools/normalize_icons.py`), neue
`StepBar` nach dem Muster der Textleiste; Codec (A1) um den Typ.
**Test.** Platzieren, Duplizieren, Undo gibt die Nummer zurück, 1/8/10/99/100 zentriert (Render),
Roundtrip im Projekt.

### H2 Hilfslinien und Einrasten (P8)

**Phase 1.** 21.09.-Plan 6 "Guides und Snapping": beim Verschieben dünne neutrale Linien zu
Kanten und Mitten anderer Objekte und des Inhaltsrahmens; 5 px einrasten, 8 px lösen (Bildschirm-
pixel); Ctrl setzt aus; Gruppen rasten als Ganzes; Guides nie im Export. Kontextmenü "Snap to objects".
**Phase 2.** Neu `src/snapping.{h,cpp}` (`alignmentBounds(item)`, Kandidaten, Hysterese).
Verschieben geschieht heute nativ durch `QGraphicsView` mit `ItemIsMovable`; `Canvas::mouseMoveEvent`
korrigiert nach dem nativen Zug die Positionen der Gruppe um den Einrast-Versatz (die native
Bewegung rechnet jedes Mal vom Druckpunkt, der Versatz sammelt sich nicht). Linien in
`drawForeground`. Undo bleibt `MoveItemsCommand` (`src/undocommands.h:29`).
**Was brechen kann.** Alt-Duplizieren, Shift-Mehrfachauswahl, Space-Schwenken und Kamera-Ziehen
(S2) im selben Maus-Weg.
**Test.** `test_snapping` (neu): Kandidaten, Hysterese, Gruppe, Spotlight nur mit Fokusregion;
Canvas-Test: Ziehen nahe einer Kante rastet ein, mit Ctrl nicht.

### H3 Ausrichten und Verteilen (P8)

**Phase 1.** 21.09.-Plan 6 "Ausrichten": ab zwei gewählten Objekten eine Auswahl-Leiste
(links, horizontal mittig, rechts, oben, vertikal mittig, unten), ab drei zusätzlich gleiche
Abstände. Nur Positionen, ein Undo-Schritt, No-op innerhalb Toleranz.
**Phase 2.** Neu `src/selectionbar.{h,cpp}` mit acht Icons (normalisiert), Logik in `snapping`
(`alignmentBounds` geteilt). `MoveItemsCommand` für alle betroffenen Items.
**Test.** Ausrichten und Verteilen mit gemischten Typen, exakte Zwischenräume, zu wenig Platz
deaktiviert.

### Umsetzungsplan W8 (vor dem Bau, 24.09.)

Grundlage: 21.09.-Plan Abschnitt 6. Reihenfolge H1, H2, H3, je ein Commit mit Tests.

**H1.** `src/items/stepitem.{h,cpp}`: `StepItem : AnnotationItem`, Mitte = `pos()`, Nummer,
Größe S/M/L (Durchmesser 28/40/56 Szenen-px), Füllung in der Annotationsfarbe, Ziffern in Weiß
oder Schwarz nach Helligkeit, fett, optisch mittig (`QFontMetricsF::tightBoundingRect`).
Mehrstellig wächst der Durchmesser, nicht die Schrift kleiner. `rect()` bleibt leer, also keine
Größen-Anfasser. `ToolType::Step` nach Text im Rail, Taste `N`, Name "step"; Klick setzt über
den vorhandenen begin/update/finish-Weg (Ziehen verschiebt noch), `finish` vergibt die Nummer.
Nächste Nummer = höchste vorhandene + 1 (abgeleitet statt eigener Folge: die Items sind
gespeichert, also die Folge auch; Undo gibt die Nummer mit dem Item zurück; Duplizieren und
Alt-Ziehen nehmen die nächsten Nummern). `StepBar` wie `TextBar`: Nummernfeld (QSpinBox 1 bis
999, Enter/Escape), S/M/L, Menü-Knopf "Renumber by creation order" (Stapelreihenfolge, ein
Undo-Makro). `SetStepCommand` (Nummer/Größe) in `undocommands`. Farbe wie bei anderen Items.
Codec: Typ "step" mit `pos`, `number`, `size`, `color`. Icon `step.svg` (Kreis mit "1"
als Pfad, kein Text-Glyph) über `tools/normalize_icons.py 20 2.8`.
Test `test_stepitem`: 1/8/10/99/100 zentriert (Render: Ziffern-Tinte mittig ±1 px), Durchmesser
wächst ab zwei Stellen, Platzieren 1-2-3, Löschen und Undo geben die Nummer zurück,
Duplizieren nimmt die nächste, Renumber ist ein Undo-Schritt; Projekt-Roundtrip.

**H2.** `src/snapping.{h,cpp}`: `alignmentBounds(item)` (Spotlight: nur Fokusregion; Text,
Pfeil, Stift, Step: Szenen-Bounding der Geometrie), `Snapper` mit Zustand pro Achse (Anker des
bewegten Rahmens links/Mitte/rechts gegen Kanten und Mitten der Ziele und des
Inhaltsrahmens; einrasten ab 5, lösen ab 8 Bildschirm-px, geteilt durch den View-Maßstab;
bei Gleichstand gewinnt der kleinere Abstand, dann die Zielreihenfolge). Canvas: nach dem
nativen `QGraphicsView::mouseMoveEvent` im Move/Text-Zug mit gegriffenem Item wird die Gruppe
(Auswahl) um den Versatz verschoben; Ctrl aus; Guides in `drawForeground` (1 px, neutral),
weg bei Release/Cancel. Kontextmenü "Snap to objects" (Canvas, Sitzungswert). Ziele: sichtbare
Annotationen außerhalb der Auswahl plus Inhaltsrahmen; keine Handles/Hintergrund.
Test `test_snapping` (Logik) und Canvas-Test (Ziehen nahe Kante rastet, mit Ctrl nicht,
Gruppe hält Abstände, Guides nach Release weg).

**H3.** `src/selectionbar.{h,cpp}`: ab zwei gewählten verschiebbaren Items; sechs Ausrichten,
zwei Verteilen (ab drei, sonst aus; ohne Platz aus mit Tooltip "Not enough room"). Logik in
`snapping` (`alignDeltas`, `distributeDeltas`, sichtbare Zwischenräume, äußere bleiben stehen,
Reihenfolge nach Achse). Ein `MoveItemsCommand`; Änderungen unter 0,01 px sind No-op. Acht
Icons `objects-*.svg`/`distribute-*.svg`, normalisiert. Position wie die anderen
Kontextleisten über der Auswahlgrenze.
Test: Ausrichten gemischter Typen, exakte Abstände beim Verteilen, zu wenig Platz aus,
ein Undo-Schritt.

## 3. Entscheidungen

**Entschieden am 2026-09-24: alle zehn Empfehlungen** (E1 a, E2 a, E3 a, E4 a, E5 a, E6 a, E7 a,
E8 wie empfohlen, E9 a, E10 a).

1. **E1 Reihenfolge S3 vor P5.** (a) S3 zuerst, die Projektaktionen landen direkt in der
   Popover-Fußzeile; (b) wie N3 festgelegt P5 zuerst, mit einem Übergangsmenü am Save-Knopf.
   **Empfehlung a**, sonst entsteht UI, die eine Welle später wieder verschwindet.
2. **E2 Ort der Projektaktionen.** (a) Fußzeile des Export-Popovers für Video und Bild (bei
   Bildern nur die Fußzeile); (b) eigenes Menü am Save-Knopf. **Empfehlung a.**
3. **E3 Schnittmarke im Lineal (S4).** (a) kleine Kerbe im Lineal über der Fuge, Hover zeigt
   "Cut 2.4 s · click to restore", Klick wählt; (b) zusätzlich ein Dreieck im Filmstreifen.
   **Empfehlung a.**
4. **E4 Masken-Spur.** (a) eigene 20-px-Spur unter der Zoom-Spur; (b) Blöcke in der Zoom-Spur.
   **Empfehlung a** (Gesamtplan 6.7).
5. **E5 "From playhead".** (a) Fenster vom Playhead bis zum Clipende; (b) 3 s ab Playhead.
   **Empfehlung a**: die Beschriftung sagt genau das, die Spur kürzt.
6. **E6 Mehrere Spotlights.** (a) mehrere, solange sich ihre Fenster nicht überlappen; ein
   Spotlight ohne Fenster bleibt das einzige; (b) weiterhin genau eines. **Empfehlung a.**
7. **E7 Motion Blur in der Vorschau.** (a) nur im Export, der Regler sagt "export only";
   (b) im Pause-Frame gerendert. **Empfehlung a**, (b) kostet viel für wenig.
8. **E8 Spur-Reihenfolge.** Filmstreifen, Audio, Zoom, Masken. **Empfehlung** so.
9. **E9 Step im Rail.** (a) direkt nach Text; (b) am Ende nach Crop. **Empfehlung a**,
   Schritte sind eine Annotation.
10. **E10 keep zoomed in mit Spur.** (a) folgt automatisch dem Cursor, bis die Mitte gezogen
    wird; (b) nur auf Wunsch per Schalter. **Empfehlung a** (Gesamtplan 6.5).

## 4. Status

Zwischendurch auf Wunsch (2026-09-24): alle Punkt-Trenner aus Tooltips, Menüs und Labels
entfernt (`204624d`); Kürzel stehen als leise Spalte, Hinweise auf eigenen Zeilen.

Abweichungen W1/W2: Der Popover ist Inhalt eines Menüs am Save-Knopf (Halten, Alt+Down),
damit Position und Schließen wie beim Copy-Menü funktionieren. Unter "Original" markiert
die Format-Reihe das Format der Quelle. Open project öffnet ein zweites Fenster im selben
Prozess. Der volle SHA-256 wird nur bei "Locate original" geprüft; beim normalen Öffnen
genügen Existenz und Größe (Hash eines großen Videos bei jedem Öffnen wäre spürbar).

Abweichungen W3: Join verbindet nur zwei behaltene Fragmente; ein Restore verschmilzt mit
gleichen Nachbarn (ein bloßer Split bleibt als Fuge sichtbar, bis Join oder Restore ihn
auflöst). Der Filmstreifen zeigt mit Fragmenten nur Thumbnails, das Kontaktblatt deckt die
ganze Quelle ab. Beim Abspielen zeigt eine Fuge höchstens zwei Frames aus dem Schnitt
(gemessen im Test), der Export ist exakt.

Welle 4 an der echten Aufnahme vom 2026-09-24 (77 s, 4299 Samples, keine Klicks): 14
Vorschläge, etwa alle 5 s einer. Ein Cursor-Zoom 2,5× über 10 s hält den eingebrannten
Pfeil in allen angesehenen Frames im Ausschnitt; Spur und Video passen zeitlich (Risiko aus
dem Gesamtplan 10 damit erledigt). Abweichung: ein gewählter Cursor-Zoom zeigt pausiert das
Fenster um den Zeiger am Playhead (bzw. am Zoom-Anfang), eine folgende Basis beginnt schon
beim ersten Frame auf dem Zeiger. D2 und D3 liegen in einem Commit.

Abweichungen W5: Recovery ist eine App-Funktion, die `main.cpp` einschaltet; Tests und
Werkzeuge schreiben nie in den Recovery-Ordner des Nutzers. Jeder Eintrag ist ein normales
Projekt (Wiederverwendung von A1). Beim Schließen wird die letzte Änderung sofort gesichert;
eine erste Kopie des Originals dabei nur bis 100 MB. Das Angebot beim Wiederöffnen ist ein
Toast mit "Resume"; es ersetzt die neue Bearbeitung nicht und schließt das neue Fenster nur,
wenn es noch unverändert ist. Kein eigener Desktop-Eintrag "Resume editing" (nur `--resume`).

Review W1 bis W3 (`/code-review high` über `d2d4bf3..e5adc67`): zehn Funde, alle behoben in
einem Commit (Join ins Leere, Wiedergabe über einen Schnitt am Ende, Trim ganz im Schnitt,
Format bei `-o`, Restore ohne Verschmelzen, springende Ansicht, Asset-Kopie bei jedem
Speichern, zweites ffprobe, Textvergleich statt Zustand, doppelte Uhr). `ctest` 46/46.

Review W4 bis W6 (`/code-review high` über `abd91f9..b9dded4`): zehn Funde, alle behoben in
einem Commit. Schließen wartet auf einen laufenden Snapshot und hält danach die letzte
Änderung fest; ein gescheiterter erster Snapshot und Ordner ohne Eintrag ohne Halter werden
entfernt; das Leerlauf-Intervall bleibt beim Aufschieben erhalten. "From playhead" am Ende gibt
ein Fenster von mindestens 100 ms (`AnnotationItem::kMinWindowMs`), sonst wäre die Datei nicht
mehr lesbar. Pausiert gilt für Zeitfenster der Abspielkopf, nicht der Frame-Start (ohne Test:
braucht echten Player). Eine normale Meldung verdrängt das Resume-Angebot samt Knopf. Motion
Blur mittelt nicht über harte Instant-Schnitte (`CameraPath::cutWithin`). Ein folgender
Ausschnitt wird von seiner aktuellen Mitte aus verschoben (`CameraPath::homeAt`). Ältere
Dateien ohne `followCursor` mit gesetzter Mitte folgen nicht. Preset-Dateinamen bekommen
bei verlustigem Säubern einen Hash, der Bildtyp kommt aus den Bytes. Dazu `padding: 0 12px`
im Resume-Dialog (Raster-Test aus W5). `ctest` 49/49.

Review W7 und W8 (`/code-review high` über `52d48e7..09f1b12`): zehn Funde, alle behoben in
einem Commit. Kein `destroyed`-Handler mehr am Wellenform-Provider (lief im Abbau der Timeline);
eine Spalte an einem Schnitt liest nur ihr eigenes Stück, nie den herausgeschnittenen Ton.
Step- und Auswahl-Leiste rechnen nur bei Auswahl- und Undo-Änderungen neu und werden bei
Szenen- und View-Änderungen bloß verschoben (die Szene ändert sich mit jedem Video-Frame),
die Step-Leiste folgt jetzt auch Zoom und Pan. "Renumber by creation order" folgt einer
Seriennummer pro Step statt der Stapelreihenfolge, die Löschen und Undo verschieben. Der
Durchmesser wird einmal pro Nummer und Größe gemessen. Das Canvas-Kontextmenü "Snap to objects"
erscheint nur mit Move/Text, nicht über editiertem Text, nicht in Crop und Pipette; der
Rechtsklick, der die Pipette abbricht, öffnet nichts mehr. Hilfslinien verschwinden bei jedem
Loslassen. Ein gemeinsamer `theme::floatButton`, der Exporter-Kommentar wieder richtig.
`ctest` 52/52.

| Welle | ID | Punkt | Commit |
| --- | --- | --- | --- |
| W1 | C1 | Export-Popover und Presets | `3b40865` |
| W1 | C2 | GIF-Export (mit Größe und Bildrate im Exporter) | `0a7afc4` |
| W2 | A1 | Projektformat | `4d29dc5` |
| W2 | A2 | Projekt speichern und öffnen, Locate original | `b91cb21`, siehe Log |
| W3 | B1 | Fragmente: Modell und Timeline | `cf7218e`, `e5adc67` |
| W3 | B2 | Fragmente im Export | `c2c885c` |
| W3 | B3 | Fragmente in der Vorschau | `cf7218e` |
| W4 | D1 | Spur-Interpolation bei Stillstand | `a1c03e6` |
| W4 | D2 | Follow Cursor | `5b68b73` |
| W4 | D3 | Zoom-Vorschläge | `5b68b73` |
| W4 | D4 | keep zoomed in mit Cursor | `5b68b73` |
| W5 | A3 | Autosave, Recovery, Resume | `486f14d` |
| W6 | E1 | Zeitfenster: Modell und Export | `675e8a0` |
| W6 | E2 | Masken-Spur und Leisten | `675e8a0` |
| W6 | F1 | Studio-Presets | `dd54861` |
| W6 | F2 | Motion Blur | `9a42553` |
| W7 | G1 | Audio-Probe und Wellenform-Daten | `d84b51b` |
| W7 | G2 | Wellenform-Spur | `f59cb2e` |
| W7 | G3 | Ton in der Ausgabe | `40dd2cf` |
| W8 | H1 | Nummerierte Schritte | `2fbff1c` |
| W8 | H2 | Hilfslinien und Einrasten | `7e6ce49` |
| W8 | H3 | Ausrichten und Verteilen | `067ebf7` |
