# Studio-Modus und Cursor-Spur von Boltsnap

Datum: 2026-09-23, Stand 2026-09-24. Status: **Vertrag v1 und Loader implementiert, Studio-Styling
für Bilder und Videos implementiert (Schritte 1 und 2 unten), Kompositionsmodell und
Render-Exportpfad implementiert (Schritt 3). Zoom-UI noch nicht; Cursor-Rendering in Eddy
gestrichen.** Vollständiger Umsetzungsplan: `docs/plans/2026-09-24-studio-features.md`. Hintergrund und Funktionskatalog: `docs/plans/2026-09-23-screen-studio-research.md`.

## 1. Grundsatz: Studio ist ein Extra, kein Muss

Eddy bleibt zuerst der schnelle Annotations-Editor. Studio ist ein optionaler Teil
für den Screen-Studio-Look (Hintergrund, Rahmen, Zooms, später Cursor-Effekte).

- **Aus ist der Standard.** Wer Studio nie anfasst, sieht keine neuen Leisten, Spuren
  oder Dialoge, und Exporte laufen exakt über den heutigen Pfad.
- **Kein Zwang zu Boltsnap.** Styling, manuelle Zooms, Speed und Masken funktionieren
  mit jedem Bild oder Video. Nur Cursor-Funktionen und Auto-Zoom brauchen eine
  Cursor-Spur, und die ist optional.
- **Nichts blockiert.** Fehlt die Cursor-Spur, gibt es einfach keine Cursor-Funktionen.
  Ist sie kaputt oder passt nicht zum Video, öffnet das Video trotzdem normal, Eddy
  meldet nur eine Warnung auf stderr.
- **Keine Kosten im Leerlauf.** Ohne Studio-Einstellungen keine zusätzlichen Prozesse,
  kein Render-Pfad, keine Hintergrundarbeit. Beim Öffnen eines Videos kostet die
  Spur-Suche einen Dateisystem-Check.
- **Kein Datenverlust durch Studio.** Ein Export ohne sichtbaren Cursor, obwohl das
  Quellvideo keinen enthält, darf nie still passieren (siehe `cursor_in_video`).

### Cursor: Boltsnap entscheidet

**Geändert am 2026-09-24:** Der Cursor wird in Boltsnap festgelegt und eingebrannt. Eddy
rendert ihn nicht neu und bietet keine Cursor-Wahl an, weil das jeden Export über den
langsameren Render-Pfad zwingen würde. Eddy exportiert immer die Hauptdatei mit Boltsnaps
Cursor und nutzt die Spur nur für Follow Cursor, Auto-Zoom-Vorschläge und "keep zoomed in"
(`docs/plans/2026-09-24-studio-features.md`, 6.4). Weg 3 unten ist damit gestrichen.

Ursprüngliche Präferenz des Maintainers (2026-09-23), überholt:

1. **Ohne Eddy:** Boltsnap rendert den geglätteten Cursor beim Speichern selbst ein.
   Das gespeicherte Video ist fertig und braucht Eddy nie.
2. **In Eddy, Standard:** Eddy übernimmt genau dieses Video mit Boltsnaps Cursor.
   Öffnen in Eddy ändert am Cursor nichts.
3. **In Eddy, auf Wunsch:** Eddy rendert den Cursor selbst neu (eigene Größe,
   Glättung, Klick-Effekte, Ausblenden bei Stillstand) **oder** lässt ihn ganz weg.
   Dafür nutzt Eddy das cursorfreie Video (`clean_video`) plus die Spur. Beides ist
   pro Dokument abwählbar, zurück zu Weg 2 ist immer möglich.

Weg 3 gibt es nur, wenn Boltsnap `clean_video` mitliefert. Ohne das bietet Eddy nur
Weg 2 an, Zooms und Auto-Zoom über die Spur funktionieren trotzdem.

### Einstieg in der Oberfläche

Vom Maintainer am 2026-09-23 anhand gerenderter Varianten entschieden:

- **Platzierung:** Button **Studio** (Icon plus Label, Body-Schriftstufe wie "To shelf")
  in der oberen Leiste vor Save/Copy/To shelf, für Bilder und Videos. Aktiv = `raise3`,
  nicht das invertierte `chip-on`, weil es ein Dokumentzustand ist, keine Primäraktion.
- **Popover:** Hintergrund (Keiner, sechs Presets, Bild…), Padding, Corners, Shadow,
  Ratio (Auto, 16:9, 4:3, 1:1, 9:16). Jede Änderung ist sofort im Canvas sichtbar.
- **Presets:** Mix aus Graphit und Papier (neutral) und Dämmerung, Ozean,
  Sonnenuntergang, Minze (Farbverläufe). Die Eddy-UI bleibt grau, Farbe gibt es nur
  im exportierten Inhalt.
- **Standardwerte:** Padding 8, Rundung 2, Schatten 50 (Prozent der kürzeren
  Inhaltsseite bzw. Stärke). Erster Hintergrund: Dämmerung.
- **Merken:** Jedes neue Dokument startet ohne Studio. Ein Klick auf Studio schaltet
  es mit dem zuletzt benutzten Stil ein (`[studio]` in der Config), "Keiner" schaltet
  es aus. Eine Popover-Sitzung ist ein Undo-Schritt.
- Später: Zoom-Spur in der Timeline erst, wenn Studio aktiv ist oder Zoom-Segmente
  existieren; Cursor-Einstellungen nur mit geladener Cursor-Spur.

## 2. Vertrag: Cursor-Spur neben dem Video (v1)

Boltsnap und Eddy sind bewusst entkoppelt (Boltsnap-Plan `2026-07-21-decouple-eddy.md`).
Deshalb keine IPC und keine CLI-Übergabe. Die Spur ist eine Datei, die mit dem Video
wandert und die Eddy selbst findet.

### Ablage

- `clip.mp4` → `clip.cursor.json` im selben Verzeichnis (Basisname ohne letzte
  Endung, also `a.b.mp4` → `a.b.cursor.json`).
- Die Datei liegt überall dort, wo das Video liegt: Shelf-Cache, `record_dir`,
  Replay-Clips. Wird die Karte oder das Video gelöscht, löscht Boltsnap die Spur mit.
- Beim Umbenennen oder Verschieben durch Boltsnap wandert die Spur mit.

### Format

```json
{
  "format": "boltsnap.cursor",
  "version": 1,
  "width": 1920,
  "height": 1080,
  "cursor_in_video": true,
  "clean_video": "clip.clean.mp4",
  "samples": [[0, 512.5, 300.0], [16, 514.0, 301.5], [900, null, null], [1400, 80, 90]],
  "clicks": [[1200, 1, 1], [1290, 1, 0]]
}
```

| Feld | Pflicht | Bedeutung |
| --- | --- | --- |
| `format` | ja | Immer `"boltsnap.cursor"` |
| `version` | ja | `1`. Eddy lehnt höhere Versionen ab, statt zu raten |
| `width`, `height` | ja | Anzeigegröße des Videos, zu dem die Spur gehört. Muss exakt zur Videodatei passen, sonst ignoriert Eddy die Spur |
| `cursor_in_video` | nein, Standard `true` | `true`: Cursor ist im Video eingebrannt. `false`: Video ist cursorfrei |
| `clean_video` | nein | Dateiname (ohne Pfad, im selben Verzeichnis) eines cursorfreien Videos mit identischer Größe, Länge, Zeitbasis und Tonspur. Ermöglicht Weg 3. Eddy nutzt es nur, wenn die Datei existiert; ein Pfad mit Verzeichnisanteil wird ignoriert |
| `samples` | nein | `[ms, x, y]`. `x`/`y` als `null` heißt: Cursor hat den Aufnahmebereich verlassen |
| `clicks` | nein | `[ms, button, down]`, `button` 1 links, 2 Mitte, 3 rechts, `down` 1 oder 0 |

Unbekannte Felder ignoriert Eddy. So kann Boltsnap später z.B. Cursorbilder oder
Tasten ergänzen, ohne v1 zu brechen. Inkompatible Änderungen erhöhen `version`.

### Regeln, die der Produzent einhalten muss

1. **Zeit ist Videozeit.** `ms` zählt ab dem ersten Frame genau dieser Datei, nicht
   Monotonic-Zeit der Aufnahme. Boltsnap rechnet über die `.ts`-Datei von gsr um.
   Bei Pause/Resume werden die Segmente wie beim Video-Concat aneinandergehängt.
   Beim Replay-Clip wird die Spur auf denselben Ausschnitt geschnitten und auf 0
   verschoben.
2. **Koordinaten sind Videopixel.** Float, Ursprung oben links im fertigen Frame.
   Bereichsaufnahme, Crop beim Replay-Export und Combined-Layouts über mehrere
   Monitore werden vorher in diesen Raum umgerechnet. Eddy kennt keine
   Monitor-Geometrie und soll sie auch nicht kennen.
3. **Zeiten sind aufsteigend** (gleich ist erlaubt), jeweils getrennt für `samples` und
   `clicks`, und nicht negativ.
4. **Rohdaten, nicht geglättet.** Samples so, wie sie ankommen (Event-Rate, nicht pro
   Frame). Glättung, Federn und Auto-Zoom sind Sache des Konsumenten. Positionen
   außerhalb des Frames dürfen vorkommen.
5. **Klicks nur mit echter Quelle.** Ohne Klickdaten das Feld weglassen, nicht leer
   raten. Eddy fällt dann für Auto-Zoom auf Stillstands-Erkennung zurück.
6. **Das Hauptvideo hat den Cursor eingebrannt** (`cursor_in_video: true`), damit es
   ohne Eddy fertig ist. Das cursorfreie Gegenstück steht in `clean_video`. Es muss
   frame-genau zum Hauptvideo passen (gleiche Frames, gleiche Zeitstempel, gleicher
   Ton), nur ohne Cursor, damit Eddy zwischen beiden wechseln kann, ohne dass Zooms,
   Trim oder Annotationen verrutschen. `cursor_in_video: false` bleibt für Fälle
   reserviert, in denen es gar kein eingebranntes Video gibt. Eddy rendert dann immer
   oder verweigert den Export mit Hinweis, nie still ohne Cursor.
7. **Atomar schreiben.** Temp-Datei plus Rename, wie beim Video. Eine halb
   geschriebene Spur ist für Eddy nur eine kaputte Spur (Warnung, keine
   Cursor-Funktionen).
8. **Cursorbilder (optional, abwärtskompatibel):** Damit Eddys Neu-Rendering dieselben
   Cursorformen zeigen kann (Pfeil, Text, Hand), darf ein Sample ein viertes Element mit
   einer Bild-ID tragen: `[ms, x, y, "id"]`. Dazu ein Objekt
   `"images": {"id": {"png": "<base64>", "hotspot": [x, y], "scale": 2}}`, dedupliziert
   wie in Boltsnaps interner Spur. Fehlt es, zeichnet Eddy einen eigenen Pfeil.
9. **Glättung dokumentieren (optional):** `"render": {"preset": "mellow"}` mit dem
   Preset, mit dem Boltsnap den Cursor eingebrannt hat. Eddy kann dann dasselbe als
   Startwert für Weg 3 nehmen.
### Was Boltsnap tatsächlich liefert (Commit `1ff3923`, Branch `perf-cursor-rework`)

- Nur, wenn `record_cursor` auf `mellow` oder `quick` steht: `X.clean.mp4` und
  `X.cursor.json` neben `X.mp4`. Beim Verschieben auf Disk wandern beide mit,
  `clean_video` wird umgeschrieben. Karte verwerfen löscht beide.
- **Nie `clicks`:** Die Cursor-Session von Hyprland 0.56 liefert nur Enter, Leave und
  Position. Auto-Zoom in Eddy muss daher über Stillstand laufen.
- **Nur ein Cursorbild:** Hyprland 0.56 kann nur SHM-Cursorbuffer kopieren. Boltsnap
  zeichnet deshalb den Standardpfeil aus dem Xcursor-Theme, `images` enthält genau
  `"arrow"` samt Hotspot und Scale. Text- oder Hand-Cursor gibt es nicht.
- `ms` mit bis zu drei Nachkommastellen, jedes Segment beginnt mit einem Sample (Position
  oder `null`). `render.preset` ist `mellow` oder `quick`.
- **Federn** (Masse 1, kritisch gedämpft): mellow Spannung 170 / Reibung 26, quick
  600 / 49. Seit Boltsnap `1c1f6b4` (2026-09-24): mellow 60 / 15,5 (etwa 0,5 s, Bewegungen
  unter 2 px um das Ziel werden ignoriert), quick unverändert.
- Seit Boltsnap `ef8cf77` (2026-09-24): eingebauter, geglätteter Pfeil (schwarz, weißer Rand,
  weicher Schatten) statt des Xcursor-Bildes, mit Motion Blur (8 Abtastungen über 1/120 s).
  Die Spur trägt weiter `images.arrow`, `render.preset`, `clean_video`; Eddy braucht davon
  nur die Samples.
- Replay-Clips bekommen vorerst keine Spur, dort ist der Systemcursor eingebrannt.

### Verhältnis zu Boltsnaps Phase 4

Boltsnaps Plan (`2026-09-23-performance-functionality.md`, Phase 4) nimmt ohne Cursor
auf, schreibt intern eine Spur mit Monotonic-Zeit, Enter/Leave, Position und
Cursorbild-ID und brennt beim Speichern einen geglätteten Cursor ein. Das passt:

- Boltsnaps interne Spur darf ihr eigenes Format behalten.
- Beim Speichern eines Clips schreibt Boltsnap zusätzlich die v1-Datei oben, in
  Videozeit und Videopixeln, mit `cursor_in_video: true`.
- Die cursorfreien Segmente, die Boltsnap ohnehin aufnimmt, werden zusätzlich zu
  `clean_video` finalisiert (gleicher Concat/Crop wie das Hauptvideo, nur ohne das
  Cursor-Overlay). Das kostet Speicher etwa in Höhe eines zweiten Clips. Ob Boltsnap
  das immer, nur für Shelf-Clips oder per Schalter macht, entscheidet Boltsnap.
  Lebenszyklus wie die Spur: gleiches Verzeichnis, wird mit dem Hauptvideo verschoben
  und gelöscht.
- Eddy nutzt die Positionen für Zooms, Follow-Cursor und Auto-Zoom-Vorschläge. Den
  eingebrannten Cursor lässt Eddy in Ruhe, außer der Nutzer wählt Weg 3.
- Größe/Rate: 240 Hz über eine Stunde sind etwa 25 MB JSON. Eddy liest bis 128 MB.
  Wenn das eng wird, kann Boltsnap Samples bei Stillstand weglassen, weil Eddy
  zwischen Samples ohnehin interpoliert und bei Stillstand die letzte Position hält.

## 3. Was in Eddy schon da ist

- `src/cursortrack.{h,cpp}`: Pfadregel, Parser mit allen Prüfungen oben, `positionAt()`
  mit linearer Interpolation und ohne Interpolation in versteckte Abschnitte.
  `clean_video` wird nur als existierende Nachbardatei übernommen
  (`CursorTrack::cleanVideoPath`), ein ungültiger Eintrag verwirft nicht die Spur.
- `loadMediaInput` lädt die Spur für Videos in `MediaDocument::cursorTrack`. Fehler
  landen als `LoadMediaResult::warning` auf stderr, das Video öffnet trotzdem.
- `tests/test_cursortrack.cpp`: Format, Interpolation, alle Ablehnungsgründe, Öffnen
  mit, ohne und mit kaputter Spur.
- Noch nichts nutzt die Spur sichtbar. Sie ist die Grundlage für Phase 4 und 8 im
  Rechercheplan.
- Studio-Styling: `src/studiostyle.{h,cpp}` (Layout, Hintergrund, Schatten, Maske,
  Rahmen mit Loch, Presets, Merken), `src/studiopopover.{h,cpp}`, Button in
  `Toolbar`, Vorschau in `Canvas::drawForeground` (gleiches Bild wie der Export, auch
  Annotationen werden an den Ecken abgeschnitten), Bilder über `exportComposite`,
  Frame-Copy, Videos über `VideoExportRequest::studio`.
- Video-Filter: Video auf Ausgabegröße padden (Inhalt immer auf gerader Position,
  sonst rundet `pad` bei yuv420 still ab und das Video verfehlt den Rahmen um einen
  Pixel), dann `maskedmerge` mit einem deckenden Hintergrund-Standbild und einer
  Graustufen-Maske (`renderStudioFrameMask`, Chroma-Ebenen halb so groß). Die
  Standbilder sind Einzelframes, das Video bestimmt den Takt. Geloopte Standbilder
  als erster `overlay`-Eingang hätten jedes Video auf 25 fps gezogen.
- Gemessen am echten Clip (1894×1026, 240 fps Quelle, 5 s Ausschnitt, 60 fps Ausgabe,
  nur Filter): ohne Studio 1,30 s, RGBA-`overlay` über die volle Fläche 3,02 s,
  `maskedmerge` 1,57 s. Randstreifen per `overlay` waren wegen versteckter
  Formatkonvertierungen noch langsamer (20 s). Mit Encoder: Studio-Export so schnell
  wie ohne. Hintergrundfarben weichen nach YUV höchstens 2/255 ab.
- Popover zeigt die Ausgabegröße (`StudioSize`, Mono, rechts in der Ratio-Zeile).
- Tests: `test_studiostyle` (11), Studio-Fälle in `test_editorwindow` und
  `test_videoexporter` (Größe, Farben an Rand, Ecke und Inhalt, Framerate, Annotationen).

## 4. Nächste Schritte in Eddy

Reihenfolge aus dem Rechercheplan, angepasst an "optional":

1. ~~Studio-Button und Style-Popover, Styling für Bilder.~~ Erledigt.
2. ~~Styling für Videos über den bestehenden ffmpeg-Filtergraph.~~ Erledigt.
3. ~~Kompositionsmodell und Render-Exportpfad~~ Erledigt am 2026-09-24 (Phasen S0 und S1:
   `studiodocument`, `timemap`, `camerapath`, `studiorenderer`, `renderexport`). Nur aktiv
   bei Zoom-Segmenten; ohne sie bleibt der Filtergraph-Export unverändert.
4. Zoom-Segmente mit Federkamera. Mit Cursor-Spur zusätzlich Follow-Cursor und
   Auto-Zoom-Vorschläge.
5. ~~Cursor-Wahl pro Dokument~~ gestrichen am 2026-09-24: Boltsnap entscheidet den Cursor.
   Die Spur dient Follow Cursor und Auto-Zoom-Vorschlägen (Stillstand, Klicks).
