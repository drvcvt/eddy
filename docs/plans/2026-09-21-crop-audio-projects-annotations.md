# Crop, Audio, fortsetzbare Bearbeitung und Annotation-Hilfen

Datum: 2026-09-21. Geprüfte Basis: `4d57ac5` auf `main`.
Status: **Crop implementiert; Audio, Fortsetzen und Annotation-Hilfen bleiben geplant.**

Crop besitzt einen sichtbaren Werkzeug-Button, acht Griffe und eine native
Kontextleiste. Bild-/Videoausgabe und Frame-Copy teilen den Ausschnitt. Der
Medien-Probe liefert orientierte Displaymaße; eine separate `MediaGeometry`-Klasse
ist dafür nicht erforderlich. Normale Rotationen und Sample Aspect Ratio werden
getestet, weitere Displaymatrizen sperren Crop mit einem Hinweis.

Der Nutzer hat die Funktionsgruppen 1, 2, 5 und 6 bejaht und ausdrücklich zuerst
einen Verhaltens-, UI- und Implementierungsplan verlangt. Die heutige Oberfläche
ist die gestalterische Referenz. Ihre Icons, Größen, Ausrichtung, Komposition und
Interaktionen sollen erhalten bleiben. Die unten beschriebenen neuen Details
sind konkrete Vorschläge, keine bereits abgenommenen oder implementierten Fakten.

## 1. Umfang und Reihenfolge

| Gruppe | Geplantes Ergebnis |
| --- | --- |
| Crop | Veränderbarer Bild- und Videoausschnitt, frei oder mit Seitenverhältnis |
| Audio | Navigierbare Wellenform im vorhandenen Zeitbereich; Ausgabe ohne Ton |
| Fortsetzen | Editierbare Projekte, automatische Wiederherstellung, gesicherte Originale |
| Annotationen | Nummerierte Schritte, magnetische Hilfslinien, Ausrichten und gleiche Abstände |

Die nicht ausgewählten Vorschläge bleiben außerhalb dieses Vorhabens:
Export-Qualitätsdialog/Dateigrößenprofile und zeitlich begrenzte Annotationen.
Ebenso keine Mehrspur-Schnittsoftware, Sprachtranskription, bewegte Crop-Keyframes,
Audioeffekte, zusätzlichen UI-Frameworks oder pauschalen Architekturumbauten.

Produktreihenfolge: **Crop → Audio → Fortsetzen → Annotation-Hilfen.** Vor Crop
stehen kurze technische Prüfungen für Medienkoordinaten und Audio-Zeitversatz.
Die Zustände für Crop und Ausgabe-Audio erhalten von Beginn an eine speicherbare
Form, damit das Projektformat später dieselben Werte übernimmt.

## 2. Verbindliche UI-Grundlage

Maßgeblich sind `src/theme.h`, `src/theme.cpp`, `resources/eddy.qss`, `src/toolbar.cpp`
und die aktuellen Kontextleisten. Ältere Spezifikationen dienen nur als Historie.

| Bestandteil | Bestehende Vorgabe, auch für neue Elemente |
| --- | --- |
| Hauptleiste, Werkzeugleiste, Transport | 22 × 22 logische Pixel, Icons 18 px |
| Kontextleisten auf dem Canvas | 24 × 24 px, Icons 20 px |
| SVG-Raster | 24er ViewBox, zentrierte 20er Live-Fläche, 2,8 Strichstärke |
| Icon-Rendering | `theme::tintedIcon`, richtige Zielgröße, 2× Rasterisierung |
| Hauptleisten-Abstände | Bestehende 2 px; Ränder 6/3 px oben, 4 px im Tool-Rail |
| Kontextleisten | 3 px Innenrand, 11 px äußere Rundung, 8 px aktive Chips |
| Typografie | Noto Sans; MonoLisa mit vorhandenem Fallback für Zahlen/Zeit/Footer |
| S/M/L/B-Glyphen | Bestehende monoline SVGs; keine Ersatzbuchstaben einer Icon-Font |
| Farben | Vorhandene semantische Grauwerte in Dark und Light |
| Tiefe | Helligkeitsstufen; keine neuen strukturellen Rahmen, Schatten oder Glows |
| Tooltips | Bestehender kompakter Child-Tooltip: 11 px, 3/6 px Padding, Radius 7 px |

Neue Crop-, Schritt- und Ausrichtungszeichen werden in dasselbe SVG-System
gezeichnet und normalisiert. Keine neue Icon-Bibliothek und keine Emojis als
Produkt-Icons. Der vorhandene `everyIconSharesOneGrid`-Test gilt auch für sie.

### Komposition

- Bestehende Toolbar, Footer und Tool-Reihenfolge bleiben erhalten. Die zwei neuen
  Werkzeuge kommen als eigene kleine Gruppe ans Ende des Rails. Ein 6-px-Abstand
  trennt sie optisch, ohne Separator-Linie oder Panel. Die Platzierung wird im
  echten Qt-Render mit der gewachsenen Mindesthöhe überprüft.
- Crop bekommt eine Kontextleiste am Ausschnitt. Mehrfachauswahl bekommt eine
  Kontextleiste an der Auswahl. Nur die zur aktiven Tätigkeit passende Leiste
  ist sichtbar. Kein permanenter Inspector.
- Audio fügt dem bestehenden Timeline-Widget eine 28-px-Spur unter dem Filmstreifen
  hinzu. Die heutige 52-px-Zeile bleibt geometrisch erhalten; mit 4 px Abstand
  ergibt sich als erster Entwurf eine Gesamthöhe von 84 px.
- Projektaktionen liegen im Menü des vorhandenen Save-Buttons. Sein kurzer Klick
  und `Ctrl+S` behalten die bestehende Ausgabeaktion. Keine weitere globale
  Button-Reihe und keine neue Start-Dashboard-Oberfläche.
- Neue Popover bleiben innerhalb des Fensters; bei Platzmangel versetzen oder
  umbrechen sie, statt Icons oder Text kleiner zu skalieren. Bestehender Video-
  Umbruch bei 520 px bleibt erhalten. Bildfenster behalten ihre aktuelle Grenze.
- Tastaturfokus sichtbar, Aktionen auch ohne Hover erreichbar. Menüs mit
  Alt+Down, sinnvolle Accessible Names, gleiche Bedienung in beiden Themes.
- Geometrie, Playhead und Griffpositionen reagieren unmittelbar. Nur vorhandene
  kurze Präsentationsanimationen; `animations=false` bleibt maßgeblich.

Die begleitende UI-Skizze zeigt räumliche Anordnung und Zustände. Sie ersetzt
keine Prüfung von Font-Metriken, SVG-Kanten und Subpixel-Ausrichtung im Qt-Build.
Sie übernimmt vorhandene Icon-Assets; neue Zeichen sind Entwurfs-Platzhalter
und müssen für die App als eigene normalisierte Glyphen fertiggestellt werden.
Im Entwurf lassen sich Crop/Mehrfachauswahl, Dark/Light und 24/28/32 px Audiohöhe
vergleichen. Simulierte Wellenform und Navigation sind keine implementierte
Decoder- oder Crop-Funktion.

## 3. Crop

### Bedienung

1. Werkzeug `Crop`, vorgeschlagene freie Taste `C`. Strg+C bleibt Copy;
   Textbearbeitung und Eingabefelder behalten ihre Tasten.
2. Der vollständige Ursprung wird sichtbar. Der Bereich außerhalb des Ausschnitts
   ist gedimmt. Acht Griffe verwenden die vorhandene Griffgestaltung mit größeren
   unsichtbaren Trefferflächen. Das Bild selbst bewegt sich beim Ziehen eines
   Griffes nicht.
3. Innen ziehen verschiebt den Ausschnitt; außen ziehen legt einen neuen an.
   Mitteltaste und Space+Drag verschieben weiterhin die Kamera.
4. Kleine Kontextleiste: `Free / Original / 16:9 / 9:16 / 1:1`, Ausgabegröße,
   Reset, Abbrechen und Anwenden. Ein Seitenverhältnis-Menü hält die Leiste kurz.
   Zahlen nutzen die vorhandene Mono-Typografie. Keine neue Farbe für den Crop.
5. Ein Verhältniswechsel erhält den Mittelpunkt und passt den größten passenden
   Ausschnitt innerhalb des bisherigen Rahmens ein. Er vergrößert nicht heimlich
   die Auswahl. Reset stellt den gesamten Ursprung wieder her.
6. Shift hält beim freien Resize das Verhältnis des Gestenbeginns, Alt vergrößert
   um den Mittelpunkt. Ein gewähltes Preset bleibt gesperrt. Modifier-Wechsel
   rebasiert den Griffanker und erzeugt keinen Sprung.
7. Pfeiltasten verschieben den Rahmen um einen Bildpixel; Shift um zehn. Bei
   Video gilt die unten definierte Rasterung. Keine Auswirkungen auf Annotationen.
8. Enter/Anwenden übernimmt die komplette Crop-Sitzung als **einen** Undo-Schritt.
   Escape bricht zunächst einen laufenden Drag ab, danach die Crop-Sitzung und
   stellt ihren Anfangszustand einschließlich Kamera und Auswahl wieder her.
   Escape im Crop schließt nicht das Fenster.
9. Ein expliziter Werkzeugwechsel oder eine Ausgabeaktion übernimmt einen gültigen
   Entwurf einmalig. Fenster-Deaktivierung beendet nur den laufenden Drag, ohne
   halbfertige Arbeit automatisch zu exportieren. Ungültige Geometrie wird nie
   übernommen; unverändertes Anwenden erzeugt keinen Undo-Schritt.
10. Nach Anwenden zeigt das Canvas den zugeschnittenen Bereich. Bei zuvor aktivem
    Fit wird dieser eingepasst; sonst bleibt der Zoom erhalten. Erneutes Crop
    stellt den Ursprung mit bisherigem Ausschnitt wieder bereit.

### Dokument und Rendern

- Crop ist ein optionales, ganzzahliges Quellrechteck in **einem gemeinsamen
  orientierten Dokumentkoordinatensystem**. Vollbild bedeutet keinen Crop.
  Annotationen bleiben in ihren ursprünglichen Koordinaten, auch außerhalb.
- Draft-Crop, committed Crop und Kamera sind getrennt. Die Szene wird nicht
  destruktiv beschnitten, Elemente werden nicht verschoben oder gelöscht.
- Nach Anwenden ist der Außenbereich ausgeblendet und nicht versehentlich
  anklickbar. Crop-Modus kann ihn wieder zeigen. Fit verwendet die aktive
  Inhaltsgrenze; die Kamera behält trotzdem ihren zusätzlichen Pan-Bereich.
- Für Bilder gilt 1-px-Präzision. Für das vorhandene 4:2:0-Videoziel werden
  Breite/Höhe und Ursprung auf ein gültiges gerades Raster eingegrenzt.
  Seitenverhältnisse werden auf dieses Pixelraster gerundet. Die tatsächliche
  Größe steht in der Kontextleiste; kein überraschendes Nachrunden erst beim Export.
  Unveränderte Videos behalten den bestehenden direkten Dateipfad.
- Bildexport und Frame-Copy rendern exakt den aktiven Quellausschnitt auf eine
  gleich große Zielabbildung, einschließlich angeschnittener Texte, Pfeile und
  Redactions. UI-Griffe, Crop-Maske und Guides sind niemals Teil des Ergebnisses.
- Video: erst gemeinsame Orientierung herstellen, dann Blur und Annotationen in
  Dokumentkoordinaten anwenden, danach genau dieses Ergebnis croppen. Das hält
  die heutigen Redaction-Koordinaten konsistent. Kein zweiter Encode-Durchlauf.
- Crop ist in `hasVideoEdits`, Exportrevision und Cache enthalten. Copy, Save,
  Frame-Copy, Drag und Shelf benutzen denselben Zustand. Crop-Drags starten keine
  Hintergrundexports. Undo zu Vollbild entfernt die Crop-Wirkung vollständig.

### Vorher zu klärende technische Kante

Heute beschreibt `VideoInfo::size` die codierten Abmessungen. Die Frame-Copy
berücksichtigt Rotation, passt aber in die bisherigen Dokumentmaße ein. Ein Crop
darf diese bestehenden Unterschiede nicht übernehmen. Vor der UI-Implementierung
werden 90/180/270-Grad-Rotation, Spiegelung, Sample Aspect Ratio und normale
Landscape-Clips durch Vorschau **und** Export verglichen. Daraus entsteht ein
kleiner `MediaGeometry`-Wert mit Quellgröße, Dokumentgröße und Transformation.
Diese Transformation wird genau einmal je Pipeline angewendet, einschließlich
korrekter FFmpeg-Autorotation/Metadatenbehandlung. Keine doppelte Rotation.

Prüfkriterium: Ein markierter Punkt und ein Crop-Rand treffen in Canvas,
Frame-Copy und decodiertem Export dieselben Pixel. Bei nicht unterstützter
Transformationsmetadatenform bleibt Crop für diese Datei mit erklärendem Hinweis
gesperrt, statt einen falschen Ausschnitt zu produzieren. Normale Rotationsfälle
müssen vor Freigabe unterstützt sein.

## 4. Wellenform und Ausgabe-Audio

### Oberfläche und Navigation

- Eine zusammenhängende, symmetrische Hüllkurve, mittig in der 28-px-Spur:
  ruhige graue Fläche, echte Pausen erkennbar, keine bunten Equalizer-Stäbe.
  Spitzen und ein zurückhaltender RMS-Kern können Lesbarkeit geben. Amplituden
  verwenden eine feste Skala; beim Zoomen wird nicht pro Ausschnitt normalisiert.
- Gleiche linke/rechte Inhaltskante, Zeitachse, In/Out-Punkte, Außenabdunklung
  und Playhead wie der Filmstreifen. Die Griffe gelten für beide Spuren.
- Klick/Drag auf Audio scrubbt denselben Player. Ctrl+Wheel zoomt am Cursor;
  Shift+Wheel/horizontaler Scroll pans. Shift am Trim-Griff bleibt Feintrim.
  `+`, `-`, `0`, `I`, `O`, `J`, `K`, `L` behalten ihre heutigen Bedeutungen.
- Audio-Hover zeigt denselben Zeitwert und dieselbe Frame-Vorschau. Es gibt
  keinen konkurrierenden Tooltip, zweiten Loop oder separaten Audiobereich.
- Die Wellenform bleibt auch bei stummgeschalteter Vorschau oder tonloser Ausgabe
  sichtbar. Sie ist Orientierung, kein Wiedergabepegelmesser.
- Ohne Audiospur bleibt das heutige kompakte Layout. Bei vorhandener Spur wird
  deren Platz von Beginn an reserviert: Laden ersetzt keine Geometrie. Stille,
  noch nicht berechnete Bereiche und fehlgeschlagene Analyse sehen verschieden
  aus. Unbekannte Bereiche erhalten keine erfundene flache Wellenform.
- Timeline-Kontextmenü: `Show waveform`, standardmäßig an, wenn Audio existiert.
  Dies ist eine Ansichtspräferenz, kein Dokumentedit. Fehlerhinweis/erneut laden
  bleibt klein und lokal; Scrubben und Export funktionieren weiterhin.

### Ton ausgeben

- Kurzer Klick auf den bisherigen Speaker bleibt Vorschau-Mute. Sein Menü erhält
  unter dem vorhandenen Lautstärkeregler `Include audio in output`, standardmäßig
  an. Der Unterschied steht in Beschriftung und Tooltip.
- Wenn aus, erscheint neben dem Speaker eine knappe Zustandsangabe `No audio`.
  Im schmalen Layout wird dafür Platz eingeplant; kein zusätzliches Statussymbol
  ohne Erklärung. Der Menüzustand und Accessible Name bestätigen denselben Wert.
- Das Ausschließen von Ton ist ein Undo-fähiger Dokumentedit und wird gespeichert.
  Vorschau-Mute, Vorschau-Lautstärke und Abspielrate bleiben Ansichts-/Sitzungswerte.
- Ausgabe ohne Ton bedeutet keine Audiostreams im Ergebnis. Auch ein sonst
  unbearbeiteter Clip muss deshalb durch den Ausgabeweg. In diesem Spezialfall
  kann Video ohne Re-Encode kopiert werden, soweit der vorhandene Containerweg
  das erlaubt. Crop/Trim/Annotationen bleiben bei einem gemeinsamen Encode.
- Mehrere Tonspuren: Die angezeigte Spur entspricht dem aktiven Preview-Track.
  Soweit mehrere verfügbar sind, bietet das Speaker-Menü deren Namen/Sprache.
  Die Ausgabe behält bei `Include audio` die bisher enthaltenen Spuren; `No audio`
  entfernt alle. Das Menü benennt dies, statt Auswahl mit Export-Löschen zu
  verwechseln. Projekt und Waveform-Cache merken sich die Preview-Track-Identität.

### Berechnung und Ressourcen

- `AudioWaveformProvider` extrahiert mit dem bereits vorhandenen FFmpeg Audio
  asynchron. Kein Qt-API voraussetzen, das auf dem Qt-6.4-CI-Runner fehlt, und
  keine blockierenden `waitForFinished`-Aufrufe im GUI-Thread.
- PCM nur als begrenzte Streaming-Chunks auswerten, nicht die ganze Tonspur
  speichern. Pro Zeit-Bin maximalen Betrag über alle Kanäle und Quadratsumme/
  Samplezahl sammeln. Kein Mono-Downmix, der gegenphasiges Stereo verschwinden
  lässt. Aus den Grunddaten entstehen gröbere Min/Max-/Peak- und RMS-Stufen.
- Ausgangspunkt: 10-ms-Bins, höchstens eine Million Grund-Bins, höchstens 32 MiB
  Gesamtcache. Bei sehr langen Clips wird die Grundauflösung gröber. Pro
  Bildschirmpixel den passenden Aggregationslevel zeichnen; kein neuer Decode
  für jeden Zoom oder Pointer-Move. Werte sind Navigationshilfe, kein Messgerät.
- Erste Analyse läuft vom Quellanfang progressiv und meldet höchstens alle
  100 ms zusammengefasste Änderungen. Für einen noch ungelesenen sichtbaren
  Abschnitt darf genau ein priorisierter Bereichsauftrag die Hintergrundarbeit
  ersetzen; danach am letzten bestätigten Hintergrundoffset fortsetzen.
  Debounce 150 ms und ein neuester Auftrag verhindern Prozessflattern.
- Pro Provider ein Analyseprozess, keine unbeschränkte Thread-/Prozessliste.
  Parserarbeit mit gemessenen GUI-Zeitbudgets, nötigenfalls dedizierter Worker-
  Eventloop. Export hat Vorrang; Lesen von Diagnose- und PCM-Pipes ist begrenzt.
  Stillstands-Timeout statt eines zu kurzen Gesamtlimits für lange Dateien.
- Cache-Schlüssel: Source-Fingerprint, Track-ID, Binauflösung, Decoderformatversion.
  Cancel/Close/Source-Wechsel verwerfen alte Ergebnisse anhand ihrer Identität.
  Ein optionaler abgeleiteter Peak-Cache darf gelöscht/regeneriert werden und
  gehört niemals zum unverzichtbaren Projektinhalt.

### Audio-Zeitbasis als eigener Nachweis

Audiodaten müssen auf derselben ursprünglichen Zeitachse liegen wie die Frames.
Startversatz, Encoder-Delay, Zeitstempellücken und Trim dürfen keine Verschiebung
erzeugen. Eine PCM-Pipe allein enthält diese Zeitinformation nicht. Deshalb
Audio-/Videostream und Container-Zeitursprung gemeinsam per ffprobe erfassen;
Normalisierung und gegebenenfalls Stille mit FFmpeg auf diese Achse beziehen.
Nicht Audio unabhängig mit `PTS-STARTPTS` auf null setzen.

Die konkrete Filter-/Seek-Kombination wird vor der Waveform-Integration an
synthetischen Klicks und gleichzeitig blinkenden Frames geprüft: positive und
negative Startzeit, Stille, AAC-Delay, Trackwechsel, VFR und Zoom nach Range-Seek.
Peak-Zeitabweichung darf höchstens eine Analyse-Binbreite plus ausgewiesene
Decoder-Toleranz betragen. Diese Skizze behauptet noch keinen gemessenen Wert.

## 5. Bearbeitung speichern und wiederherstellen

### Nutzerfluss

- Save-Menü: `Save output` (heutige Aktion), `Save project…` (`Ctrl+Shift+S`),
  `Open project…` (`Ctrl+O`) und `Resume editing…`. Kurzer Save-Klick, Enter,
  Copy, Drag und Shelf behalten die heutige Ausgabe-Semantik.
- Ein Projekt enthält die editierbaren Objekte, Originalquelle, Crop, Trim und
  Ausgabe-Audio. Es ist ausdrücklich kein gerendertes Bild/Video. Im ersten
  Speicherdialog ein knapper Hinweis: `Includes the original and editable layers`.
  Die Share-/Shelf-Wege versenden weiterhin ausschließlich das fertige Ergebnis.
- Nach `Save project…` bekommt es einen Namen. Weitere Projekt-Saves speichern
  ohne erneuten Dialog; `Save project as…` erzeugt eine unabhängige Kopie.
  `Ctrl+S` wird auch in einem Projekt nicht stillschweigend umgedeutet.
- Wiederöffnen stellt die Komposition editierbar wieder her und startet Video
  pausiert am gespeicherten Zeitpunkt. Auswahl, Kamera und Timeline-Ausschnitt
  können als Sitzungszustand zurückkehren. Audio startet niemals automatisch.
- Rückgängig-Historie wird in Version 1 nicht über Programmstarts serialisiert.
  Die wiederhergestellten Elemente selbst bleiben voll editierbar.

### Einfaches, dauerhaftes Format

Vorgeschlagen: eine `.eddy`-JSON-Datei plus ein gleichnamiger `.eddy.assets`-
Ordner daneben. Darin liegt eine unveränderliche Kopie der Originalquelle.
Beides zusammen bildet das portable Projekt. Keine eigene ZIP-Implementierung
oder neue Archivabhängigkeit nur für diesen Zweck. Die Speicheroberfläche nennt
den Begleitordner; zum Verschieben müssen beide Bestandteile mitgenommen werden.

- Originaldatei beim ersten Projekt-Save asynchron und mit Fortschritt kopieren;
  später nur kleine Metadaten schreiben. Stdin-Bilder werden verlustfrei als PNG
  abgelegt. Videodaten bleiben Originalbytes, kein neuer Encode.
- Assets haben kollisionsfreie, vom Programm erzeugte Namen. Hash und Größe
  werden beim Kopieren ermittelt. Manifest verweist relativ auf das eigene Asset;
  ein SHA-256-Fingerprint erkennt vertauschte Quellen.
- Erst Asset vollständig abschließen, dann Manifest atomisch veröffentlichen.
  Vorhandene funktionierende Projekte bleiben bei Abbruch, vollem Datenträger
  oder Schreibfehler unverändert. Ein nicht referenziertes neues Asset ist weniger
  schlimm als ein gültig aussehendes Manifest mit unvollständiger Quelle.
- Speichern unter neuem Namen übernimmt die Assets; keine stille Abhängigkeit
  vom alten Projekt. Das Original im Projekt wird durch normale Bild-/Video-
  Ausgabe auch bei gleicher Ausgangsdatei nicht überschrieben.
- Fehlende Quelle: kompakter Dialog `Locate original` mit Identitätsprüfung.
  Keine unbemerkte Ersatzdatei. Fehlende Schrift: benennen, Darstellung mit
  Fallback ermöglichen, gespeicherte Familienangabe dabei behalten.
- Vor einer Ausgabe, die eine noch nicht gesicherte Originalquelle ersetzen
  würde, zuerst deren Asset-Kopie abschließen. Ein Projektmanifest oder ein
  verwaltetes Original-Asset darf niemals Ziel des normalen Video-/Bildexports
  werden. Alte Shelf-Card-IDs, Stdout-Routen und fremde Ausgabeziele werden nicht
  aus einem Projekt übernommen; die aktuelle Sitzung bestimmt die Ausgabe.

### Datenmodell und vollständige Objektabdeckung

Eine kleine Snapshot-Schicht serialisiert die bestehende Szene. Sie ersetzt
weder QGraphicsScene noch alle Item-Klassen durch ein neues Framework.

- Manifest: Formatversion, Projekt-ID, Asset-ID/Hash, Mediengeometrie, Crop,
  In/Out, Ausgabe-Audio, Liste typisierter Annotationen, optionale View-State-Werte.
- Jeder Eintrag: stabile ID, expliziter Typ, Position, Z-Reihenfolge, unterstützte
  Transformation, Farbe, Strichstärke und typbezogene Geometrie.
- Typen vollständig: Arrow-Endpunkte; Rect/Ellipse/Highlight-Rechtecke;
  Pen-Punktliste; Text inklusive Klartext, Font, Größe, Gewicht, Umbruchbreite,
  Ausrichtung und Labelstil; Redact-Modus, Region, OCR-Rechtecke und Gültigkeit;
  Spotlight-Region, Form und Intensität; neue NumberStep-Geometrie/Nummer/Stil.
- `items/rasteritem.*` ist heute ein Blur-Helfer, kein weiterer Szene-Objekttyp.
  Background, Crop-Maske, Griffe, Guides, Videooberfläche und Fade-Animationen
  gehören nicht in die Annotationenliste. Temporäre Opacity ist kein Objektstil.
- OCR-Ergebnisse erst nach kompletter Geometrie/Position wieder anwenden, damit
  Item-Move-Signale sie nicht versehentlich invalidieren. Unaufgelöste oder
  ungültige OCR-Zustände decken weiterhin die ganze Redact-Region ab.
- Keine QGraphicsItem-Zeiger, Qt-Metatype-Dumps oder beliebiges rich HTML im Format.
  Begrenzte JSON-Daten mit expliziter Schema-Validierung vor der Szenenerzeugung.
  Unbekannte Hauptversion/Objekttypen nicht stillschweigend droppen und speichern.
- Grenzen für Dateigröße, Objekte, Pen-Punkte, Zahlen, Bilddimensionen und Pfade
  verhindern übergroße/allokierende Eingaben. Kein NaN/Infinity, Pfadtraversal,
  externe Befehle oder aus dem Manifest ausgeführte FFmpeg-Ausdrücke.

### Autosave, Recovery und Quell-Lebensdauer

- Autosave schreibt einen Recovery-Snapshot nach etwa 2 Sekunden ohne weitere
  abgeschlossene Änderung, spätestens 10 Sekunden nach dem letzten stabilen
  Commit. Aktiver Drag oder Texteingabe wird nicht halb serialisiert. Kein
  vermeintliches Save für jedes Videoframe oder jede Mausposition.
- Benannte Projekte werden durch Recovery **nicht** ungefragt überschrieben.
  Save project markiert den gespeicherten Stand; Recovery behält spätere Arbeit.
- Unbenannte Bearbeitungen erhalten bei der ersten Änderung einen eigenen
  Recovery-Eintrag. Die Quelle wird gesichert, bevor die UI dauerhafte
  Wiederherstellbarkeit bestätigt. Für temporäre Shelf-/Stdin-Quellen besonders
  wichtig. Während der ersten Sicherung wird ein ruhiger Status angezeigt.
- Recovery liegt im benutzerspezifischen App-Datenverzeichnis, nicht in `/tmp`.
  Quelle einmal kopieren, anschließend nur Snapshots. Größere Quellen dürfen die
  Oberfläche nicht blockieren. Schreib-/Platzfehler werden einmal sichtbar und
  lassen das normale Bearbeiten/Ausgeben weiter zu; kein falsches `Saved`.
- Sauberes Schließen behält unbenannte Editierstände für `Resume editing…`.
  Erneutes Öffnen derselben Quelle bietet den neueren Stand kompakt an, ersetzt
  aber nicht ungefragt eine angeforderte neue Bearbeitung. Nach Absturz ebenso.
- Eine kurze Resume-Liste mit Dateiname, Zeitpunkt und Status genügt. Sie verwaltet
  Editierstände, keine zweite Capture-History neben Boltsnap. Kein neues Dashboard.
- `eddy --resume` und eine Desktop-Aktion `Resume editing` öffnen diese Liste
  auch ohne vorhandene Eingabedatei. Der Einstieg erfolgt vor dem normalen
  Medienladen, damit Recovery nach dem Entfernen einer Shelf-Quelle erreichbar
  bleibt. Das Save-Menü verwendet denselben Dialog.
- Explizite Aktionen `Save as project` und `Discard recovery`; Datennutzung
  sichtbar. Startbudget 2 GiB für neue Recovery-Quellkopien, konfigurierbar.
  Wenn voll, vorhandene Stände behalten und weitere Sicherung sichtbar aussetzen.
  Keine heimliche Löschung ungesicherter Arbeit zur Einhaltung einer Stückzahl.
- `QSaveFile` ohne Direct-Write-Fallback für Manifest/Snapshots; `QLockFile` für
  gleichzeitige Projekt-/Recovery-Writer. Kurze nichtblockierende Lock-Versuche;
  zweites Fenster öffnet einen belegten Projektstand als unabhängige Bearbeitung
  oder read-only, überschreibt ihn aber nicht. Keine Löschung lebender Locks.
- Shutdown wartet begrenzt auf einen ausstehenden Recovery-Commit. Bei Fehler
  oder noch nicht gepinnter Quelle verlangt nur der drohende Verlust eine
  Entscheidung. Vorhandene Export-/Shelf-Fertigstellungsregeln bleiben wirksam.

## 6. Annotation-Hilfen

### Nummerierte Schritte

- Eigenes Werkzeug `Step`, vorgeschlagene Taste `N`, Kreis mit zentrierter Nummer
  in der aktuellen Annotationsfarbe und kontrastierendem Text. Kein Emoji.
- Klick setzt 1, 2, 3 usw. Nächste Nummer ist eine persistierte Sequenz, nicht
  bloß die Anzahl vorhandener Kreise. Löschen nummeriert keine bestehenden
  Schritte überraschend um; Undo stellt die ursprüngliche Nummer wieder her.
- Ctrl+D/Alt-Drag erzeugt einen weiteren Schritt mit der nächsten Nummer.
  Undo/Redo verwendet dieselbe reservierte Nummer. Manuell gesetzte Nummern
  erhöhen bei Bedarf den Folgewert, verändern aber keine anderen Schritte.
- Kontextleiste: Nummer als kleines Feld, vorhandene S/M/L-Größenglyphen.
  Explizites `Renumber by creation order` im Menü für lückenloses Neuordnen,
  zusammen genau ein Undo-Schritt. Keine automatische Nummerierung nach Position.
- Numerischer Text optisch und rechnerisch zentriert. 1/8/10/99/100 prüfen;
  mehrstellige Werte passen ihren Badge-Durchmesser an, nicht die Schrift immer
  kleiner. Fokus/Enter/Escape entsprechen den vorhandenen Editierfeldern.

### Guides und Snapping

- Beim Verschieben erscheinen dünne neutrale Hilfslinien zu Kanten und Zentren
  anderer sichtbarer Objekte sowie des aktiven Bild-/Crop-Rahmens.
- Trefferabstand in **logischen Bildschirmpixeln**, unabhängig vom Zoom und DPR:
  zunächst 5 px einrasten, 8 px lösen. Hysterese und stabiles Tie-Breaking
  verhindern Flackern zwischen nahegelegenen Kandidaten.
- Ctrl während eines Drags setzt Snapping temporär aus. Alt bleibt Duplizieren
  beziehungsweise Resize aus der Mitte; Shift behält die bisherigen Constraints.
  Loslassen von Ctrl rebasiert die Bewegung ohne Sprung. Kein neues Verhalten
  der Pfeiltasten: numerisches 1/10-px-Nudging bleibt exakt.
- Mehrfachauswahl snappt als zusammenhängende Gruppe, interne Abstände bleiben
  erhalten. Bewegte Elemente sind keine Ziele für sich selbst. Ausgeblendete
  Elemente, Guides und Background sind keine Objektkandidaten.
- Guides werden nicht gespeichert, exportiert oder Undo-Schritte. Sie verschwinden
  auf Release, Cancel und Fokusverlust. Die eigentliche Bewegung bleibt ein
  `MoveItemsCommand`. Regler im Canvas-Kontextmenü: `Snap to objects`, an/aus.
- Erste Ausbaustufe snappt Move/Alt-Duplicate und Crop an Quellgrenzen. Resize-
  Constraints behalten ihr heutiges Verhalten; Objekt-Snapping beim Resize kommt
  nur hinzu, wenn es mit Griffanker, Shift/Alt und Mindestmaßen eindeutig ist.

### Ausrichten und gleiche Abstände

- Ab zwei gewählten Objekten erscheint eine kompakte Auswahl-Kontextleiste:
  links / horizontal mittig / rechts sowie oben / vertikal mittig / unten.
  Ab drei Objekten zusätzlich horizontal/vertikal gleiche Abstände.
- Referenz für Ausrichten ist die gemeinsame geometrische Auswahlgrenze vor der
  Aktion. Es gibt zunächst kein verstecktes Key-Object-Konzept. Die passende
  Taste wird anhand ihres Tooltips eindeutig benannt.
- Gleiche Abstände verteilt die **sichtbaren Zwischenräume**, nicht einfach die
  Objektmittelpunkte. Äußerstes erstes/letztes Element bleiben stehen. Reihenfolge
  nach Achse mit stabiler ID als Tie-Breaker. Bei zu wenig Platz Aktion deaktivieren
  und erklären, statt negative Abstände unerwartet zu erzeugen.
- Eine gemeinsame Funktion liefert Alignment-Bounds: Text-Label, Strichgeometrie,
  Arrow und Step; bei Spotlight nur die Fokusregion, nicht seine canvasgroße
  Abdunklung. Das gilt ebenso für Guide-Ziele und Gruppengrenzen.
- Aktionen ändern nur Positionen, keine Größen/Schriftgrößen. Eine Aktion ergibt
  einen Undo-Schritt für alle betroffenen Elemente. Ergebnisse innerhalb einer
  kleinen numerischen Toleranz sind No-ops. Redact/OCR-Move-Regeln bleiben aktiv.
- Keine parallele Text-Ausrichtungsleiste: Text-internes Alignment erscheint bei
  einzelner Textauswahl, Objekt-Alignment nur bei Mehrfachauswahl.

## 7. Implementierung in überprüfbaren Schritten

| Schritt | Änderungen | Fertig, wenn |
| --- | --- | --- |
| P0: Baseline | Aktuelle Qt-Ansichten, Fokuswege und Maße dokumentieren; `MediaGeometry`-/Audio-Zeitversatz-Proben | Orientierung/PTS-Vertrag mit synthetischen Medien nachgewiesen, Crop-/Waveform-Skizze mit realen Maßen plausibel |
| P1: Crop-Zustand | Kleine Edit-State-Werte, `CropController`/Geometriehilfe, `SetCropCommand`; Canvas-Clip/Fit | Bild-Crop reversibel, keine Objektverschiebung, ein Undo, Pan/Space unverändert |
| P2: Crop-Ausgabe/UI | `CropBar`, Toolbar/Cursor, gemeinsamer Render-Ausschnitt; `VideoExportRequest` erweitern | Bild, Frame-Copy und Video pixelkonsistent; jede Ausgabe-Route korrekt |
| P3: Audio-Daten | `VideoInfo`/Probe erweitern, `AudioWaveformProvider`, Peak-Aggregation und Zeitnormalisierung | Echte Peaks synchron, begrenzte Speicher-/Prozessnutzung, Fehler/Cancel sicher |
| P4: Audio-UI/Ausgabe | `VideoTimeline` um Lane erweitern; gemeinsamer Hit-Test; Speaker-Menü; `SetOutputAudioCommand` | Beide Spuren identisch navigierbar; No-audio korrekt exportiert und Undo-fähig |
| P5: Projekt-Kern | `ProjectSnapshot`, expliziter Item-Codec, `ProjectStore`; Original-Asset-Kopie, atomischer Commit | Alle Item-Typen und Crop/Trim/Audio nach Roundtrip editierbar und visuell gleich |
| P6: Projekt-Flows | Save-Menü, CLI/MIME/Open, `RecoveryStore`, Locking, Resume und Source-Lebensdauer | Schließen/Absturz/temporäre Quelle/Schreibfehler verlieren keinen bestätigten Stand |
| P7: Step | `NumberStepItem`, ToolController/Toolbar, Kontextleiste und Nummerierungs-Undo | Platzieren, Duplizieren, Reopen, Undo und drei Ziffern konsistent |
| P8: Guides/Alignment | Kleine `alignmentBounds`-/Snap-Hilfe, Canvas/Move-Wiring, SelectionBar | Gruppen stabil, Constraints unverändert, exakte Zwischenräume, keine Guides im Export |
| P9: Integration | README, Projektformat-Doku, Preview-Szenen, Theme-/DPI-/Plattformprüfung | Abnahmematrix erfüllt; normaler getesteter PR-Merge |

Konkrete bestehende Integrationspunkte:

- `editorwindow.*`: bleibt Koordinator; Ausgabewege, Exportrevision, Space/Fokus,
  Recovery-Status. Kein weiterer riesiger Block für Dateiformat/Peak-Analyse darin.
- `canvas.*`: Crop-Clip und Pointer-Routing, Pan-Bounds, Guide-Overlay.
- `toolcontroller.*`, `selectionhandles.*`, `undocommands.*`: bestehende Gesten und
  Undo wiederverwenden; neue Hilfen eng auf Crop, Steps und Layout begrenzen.
- `exporter.*`, `videoexporter.*`: Quellrechteck und Tonentscheidung explizit in
  Requests. Cacheidentität und `hasVideoAnnotations` auf echte Dokumentobjekte
  prüfen: Der heutige Z-Wert-Test darf neue UI-Overlays nicht als Annotation zählen.
- `videotimeline.*`: dieselbe x↔t-Abbildung für Bild und Audio; separate Rechtecke
  für Filmstreifen und Audio, statt `trackRect` einfach höher zu machen und damit
  Thumbnails oder Griffe zu strecken.
- `mediaio.*`: begrenzte strukturierte ffprobe-Daten für Geometrie, Tonspuren und
  Zeitursprung. Alte Metadatenfelder weiter sinnvoll unterstützen.
- `main.*`, `cli.*`, Desktop/MIME-Registrierung: `.eddy` vor normalem Medienladen
  erkennen. Native Open-Dialoge erhalten den Projektfilter. Kein neues globales
  Dateiformat-Handling für fremde Anwendungen. `--resume` ohne Input und die
  Desktop-Resume-Aktion sind explizite neue Einstiege, keine Änderung von Capture.
- `theme.*`, QSS, QRC: existierende Tokens und Größen; nur notwendige neue Selektoren
  und Icons, keine globale Neuskalierung bisheriger Controls.

## 8. Abnahmekriterien

### Visuell und interaktiv

- Vorher/Nachher mit identischem Inhalt in Dark/Light und DPR 1/1,25/1,5/2.
  Fensterbreiten 520 (Video), 760 und 1000/1200 px; schmale/kurze verfügbare Fläche.
- Crop aktiv, angewendet, abgebrochen; hochkant/quer/quadratisch; gezoomt/gepannt;
  Annotation über Crop-Rand; Popup an jeder Fensterecke.
- Audio ohne Spur, echte Stille, Laden, Teilergebnis, Fehler, No-audio,
  mehrere Tracks, sehr lange Datei und Waveform ausgeblendet.
- Projekt Save/Save-as/Resume, großer Quellcopy-Fortschritt, fehlendes Asset,
  fehlende Schrift, belegt und schreibgeschützt. Zustände verschieben keine
  existierenden Leisten oder zeigen zwei konkurrierende Toasts/Statuszeilen.
- Steps mit ein bis drei Ziffern; zwei und viele Objekte; Guide auf hellem/dunklem
  Inhalt; wechselnde Modifier; Pan funktioniert bei Fit und in jedem Werkzeug.
- Neue Icons einzeln normalisieren und gemeinsam neben den vorhandenen prüfen.
  Screenshotvergleich fokussiert auch auf unveränderte Bereiche; normale
  Antialiasing-Unterschiede sind kein Anlass für fragile Vollbild-Pixeltests.

### Funktional und gegen Datenverlust

- Crop-Farbraster mit Punktmarken: Bildexport, annotierter Frame, decodiertes
  Video, Rotation/SAR, ungerade Maße, Blur/OCR/Spotlight und alle Ausgaberouten.
- Audio-Klick/Video-Blitz-Fixtures mit Stille, gegenphasigem Stereo, Offset,
  VFR, AAC-Delay und Trackwechsel. Zoom/Pan verändert weder Zeit noch Lautstärke.
- `ffprobe` bestätigt: No-audio hat keine Audiostreams; Keep-audio behält die
  vereinbarten Spuren. Trim bleibt synchron, auch zusammen mit Crop und Redact.
- Serialisierung jeden Typs inklusive Pen-Punkten, Text-Metriken, Z-Reihenfolge,
  OCR-Zuständen, Step-Zähler und Mediengeometrie roundtrippen. Nach Laden ändern,
  duplizieren, verschieben, undo/redo und erneut speichern.
- Fehlendes/vertauschtes Asset, ungültige Version, übergroßes/abgeschnittenes JSON,
  Disk-full/Write/Commit-Fehler, parallele Fenster, unterbrochene erste Quellkopie
  und Wiederherstellung ohne ursprüngliche Shelf-Datei gezielt testen.
- Keine Tests nur für Getter/Setter oder als Spiegel der Implementierung. Neue
  Tests müssen beobachtbares Verhalten, Plattformkanten oder Datenverlust abdecken.

### Leistung und Abschluss

- GUI bleibt während Quellkopie, Waveform-Aufbau und Export bedienbar. Messen:
  lange H.264/AAC-Screenaufnahme, 4K60 und VFR. Bin-/Cache-/Job-Grenzen kontrollieren.
  Keine Behauptung bestimmter Latenzen ohne Messung.
- Bestehende vollständige CTest-Suite und neue relevante Regressionen; Linux
  Qt 6.4 sowie Windows-CI. Menschliche Wayland-Prüfung und Windows-Prüfung separat
  ehrlich dokumentieren, nicht aus Offscreen-Tests ableiten.
- Implementierung später auf einem begrenzten Arbeitsbranch, nach jedem Slice
  sinnvolle Commits ohne Agent-Trailer. Vor Integration Diff gegen main prüfen,
  beide Pflicht-CI-Checks bestehen, normal mergen und nur vollständig übernommene
  eigene Arbeitsbranches entfernen. Dieser Planungsschritt setzt das nicht vorweg.

## 9. Technische Quellen und offene Nachweise

Die UI-Entscheidungen stammen aus dem lokalen Code und dem Nutzerauftrag.
Offizielle Dokumentation wurde nur für die technischen Bausteine geprüft:

- [FFmpeg crop](https://ffmpeg.org/ffmpeg-filters.html#crop): explizite Crop-
  Geometrie und Subsampling-Rundung; unterstützt den geforderten gemeinsamen
  Preview-/Export-Vertrag, ersetzt aber keine Fixtures.
- [FFmpeg aresample](https://ffmpeg.org/ffmpeg-filters.html#aresample): zeitbezogene
  Sample-Anpassung/Stille; die passende Quellzeit-Normalisierung ist in P0/P3 zu
  belegen, nicht aus einer Roh-PCM-Pipe abzuleiten.
- [Qt QSaveFile](https://doc.qt.io/qt-6/qsavefile.html): temporär schreiben und mit
  Commit ersetzen; Direct-Write-Fallback wird für Projekte nicht verwendet.
- [Qt QLockFile](https://doc.qt.io/qt-6/qlockfile.html): Prozess-Locks; bei länger
  geöffneten Dokumenten keine altersbasierte Übernahme lebender Sperren.

Noch nicht nachgewiesen: reale Performance der vorgeschlagenen Waveform-Budgets,
vollständige Mediengeometrie über beide Qt-Backends, exakte Crop-/Alignment-Icon-
Optik im Qt-Renderer und Fehlerfälle der noch zu bauenden Persistenz. Diese Punkte
haben oben explizite Implementierungs- und Abnahmeschritte, keine stillen Annahmen.

Planungsprüfung: Die HTML-Skizze wurde im Browser bei 320/360/520/736/1024 px
geprüft, darunter Dark und Light. Keine fehlenden Glyphen, JavaScript-Ausnahmen
oder horizontal überstehenden Controls im geprüften Layout. Playhead-Zeit,
Audio-Ausgabestatus, Seitenverhältnis-Anzeige und Waveform-Sichtbarkeit reagieren
im Entwurf. Diese Prüfung ist keine Produktabnahme oder Qt-Pixelprüfung.
