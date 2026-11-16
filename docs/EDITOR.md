# Bedienung des OpenEM Editors (`em_editor`)

Bearbeitet die Dateien des Originalspiels. Version 0.2, nur Windows (Win32 mit Standard-Steuerelementen). Die Dokumentlogik (`src/editor/*_doc.*`, `pixel_editor.*`, `modpack.*`) hat keine Oberflächenabhängigkeit und läuft im Test (`ctest`, mit `-DEM_DATA=<Spielordner>`).

```
em_editor [--data <EM-Ordner>] [--tab 0..6] [--file <KB-Nr.>] [--scene <Name>] [--frame <n>] [--mode 0|1|2]
          [--file N --export-png out.png | --file N --import-png in.png] [--shot out.bmp]
```

**Spielordner:** Der Editor benutzt zuerst `--data`, sonst den Ordner, in dem er liegt oder gestartet wird, wenn dort `EM.EXE` und `KB` liegen. Sonst fragt er nach dem Ordner. Vor dem ersten Überschreiben legt er neben jede Datei eine `.bak` (wird nie überschrieben, enthält immer das Original).

## Registerkarten

| Registerkarte | Inhalt |
|---|---|
| Bilder | alle KB-Bilder (`KB\*.vga`, auch die mit eigener Palette); Palette KB21 bzw. die eingebettete, für Bilder ohne eigene Palette wählbar (KB8 liegt im Spiel über KB14) |
| Torszenen | Hallenkacheln KB231/232 (16 × 16), Spieler- und Torhütersprites KB233–235 (32 × 32), Anzeigetafel KB246 (320 × 40); Palette KB247; Zellgrenzen orange |
| Paletten | KB21 und KB247: 255 Farben mit sechs Bit je Kanal (VGA-DAC); die ungenutzten oberen Bits der Datei bleiben erhalten |
| Mannschaften | `KB\23.vga`: Vereinsname (22 Zeichen), 25 Spielernamen (19 Zeichen, Großbuchstaben, Zeichensatz des Spiels) mit Ausländer-Haken, Wappen 0–63 mit Vorschau und Zeichenfläche, Platz, drei Stärkewerte, Trikotfarbe der Torszenen, Tauschen zweier Mannschaften; gruppiert nach Liga 1–3, weitere Vereine, Nationalmannschaften und Europapokal-Vereinen |
| Hallenkarten | Kachelkarten der Torszenen-Halle (KB236, KB242–245, 42 × 72 Kacheln): Kachelwahl, Malen, Füllen, Pipette, Haken „Gespiegelt“, Raster, Zoom |
| Spielszenen | die Skripte der Torszenen `TORE\*` (siehe unten) |
| Töne | die Töne der Torszenen `KB\237–241` (siehe unten) |

## Pixel-Editor (Bilder, Torszenen)

Werkzeuge: Stift (P), Linie (L), Rechteck (R), gefülltes Rechteck (B), Füllen (F), Pipette (I oder Rechtsklick), **Auswahl (M)**; Rückgängig/Wiederholen (Strg+Z/Y), Speichern (Strg+S), Raster (G), Zoom (Strg+Mausrad, +/−).

**Auswahl:** Mit gedrückter Maustaste ein Rechteck aufziehen; ein Zug in der Auswahl verschiebt die Pixel (die Fläche dahinter wird Farbe 0). Bearbeiten-Menü: Ausschneiden/Kopieren/Einfügen (Strg+X/C/V; Einfügen an der Mausposition, das Eingefügte bleibt ausgewählt und lässt sich verschieben), Löschen (Entf), Alles auswählen (Strg+A), horizontal/vertikal spiegeln, um 90° drehen (quadratischer Bereich). Ohne Auswahl wirkt Spiegeln/Drehen auf das ganze Bild; Kachel- und Spritebögen brauchen eine Auswahl, sonst würden alle Zellen vertauscht.

**PNG und BMP:** Strg+E exportiert das Bild (8 Bit mit der aktuellen Palette), Strg+I importiert PNG, BMP, GIF oder JPEG: jedes Pixel erhält die nächste Farbe der Palette (0–254, transparente Pixel Farbe 0), unveränderte Farben behalten ihren Index. Eine andere Größe nimmt der Editor nur bei einfachen Bildern nach Rückfrage an; Bögen und die Anzeigetafel müssen genau passen. Nach dem Import zeigt die Statusleiste, wie viele Pixel angenähert wurden und die größte Abweichung (von 441); **Ansicht › Palettenabweichung des Imports zeigen** färbt diese Pixel rot.

**Alle Bilder exportieren / Bilder aus Ordner importieren** (Datei-Menü, Registerkarten Bilder und Torszenen): schreibt `kbNN.png` für jede Datei der Liste und liest sie wieder ein (nur Dateien gleicher Größe, mit Rückfrage, jede mit `.bak`).

**Vergleich mit dem Original:** **Ansicht › Änderungen gegenüber dem Original zeigen** färbt die Pixel gelb, die sich von der Sicherung (`.bak`) unterscheiden.

## Spielszenen

Die 48 Skripte, links nach Gruppen (Heim/Gast greift an, Tor/vergebene Chance; dazu Strafschuss und sechs Feldspieler). **Abspielen** mit der Zeitleiste (Pfeil links/rechts, Pos1/Ende, Leertaste), drei Ansichten (Spielfenster 320 × 200 wie im Spiel, Ausschnitt um die Auswahl, ganze Halle mit orangem Kamerafenster), Halle KB236/KB242–245 und **Trikotfarbe** beider Seiten nach einer Mannschaft aus `KB\23.vga` wählbar.

Figuren, Puck und Kamera mit der Maus ziehen (die Bahn der Auswahl ist blau) oder in x/y eintippen; Typ (Sprite) aus der Liste der 94 Objekttypen oder als Zahl, „bis zum Ende“ setzt ihn für alle Folgebilder; Ton 0–4 je Bild; Bild einfügen (das Bild bleibt gleich), kopieren (die Bewegung wiederholt sich), löschen; Rückgängig/Wiederholen.

*Position 0* ist die Startaufstellung: ein Zug dort verschiebt die ganze Bahn. Ab Bild 1 ändert ein Zug nur den Schritt in dieses Bild (höchstens 7 Pixel je Achse zum vorigen); „Folgebilder bleiben“ gleicht den nächsten Schritt aus, soweit er in 7 Pixel passt.

**Bahn bis Bild … nach x, y → Bewegen:** die gewählte Figur läuft vom aktuellen Bild in gleichmäßigen Schritten bis zum Zielbild an den Zielpunkt (mehr als 7 Pixel je Bild geht nicht, dann bleibt alles unverändert). **Glätten** macht die Schritte zwischen aktuellem Bild und Zielbild gleichmäßig und lässt beide Endpunkte stehen.

**Datei › Neue Szene anlegen…** kopiert die vier Skripte der gewählten Szene (Heim/Gast × Tor/Chance, gleiche Variante) unter der nächsten freien Nummer und erhöht `TORE\anzahl` (`MAXSZENE:a|b|c`), damit das Spiel die neue Szene auch auswählt. Ein unvollständiger Satz würde das Spiel zum Absturz bringen; der Editor legt deshalb immer alle vier an.

Unveränderte Skripte schreibt der Editor byte-gleich zurück.

## Töne

`KB\237–241`: das Publikum (Dauerschleife) und vier Szenen-Töne, 8 Bit mono, 4,7 bis 11,9 kHz. Die Wellenform zeigt den Ton; ein Bereich lässt sich ziehen (Abspielen der Auswahl, **Auswahl behalten** schneidet zu). **Abspielen** (Leertaste), **Stopp** (Esc), „Endlos wiederholen“ (das Publikum), **WAV exportieren** (8 Bit mono WAV).

**WAV importieren** nimmt PCM-WAV (8, 16, 24, 32 Bit oder Gleitkomma, beliebige Kanalzahl, beliebige Rate) und passt sie an das Spiel an. Das Spiel lädt jeden Ton in einen Puffer von der Größe der Originaldatei; ein größerer Ton würde den Speicher überschreiben. Deshalb:

- Der Ton wird auf 8 Bit mono gewandelt; beim Herunterrechnen der Rate wird gemittelt, damit nichts Hohes zurückfaltet.
- Der Editor wählt **die höchste Abtastrate** (aus den Raten, die die Zeitkonstante des Sound Blasters zulässt; bis 16 kHz, nie über der Quellrate), bei der der **ganze** Ton noch in den Puffer passt. Kurze Töne behalten dadurch eine hohe Qualität, lange werden gröber.
- Reicht selbst 4 kHz nicht, wird das Ende mit einer kurzen Ausblendung abgeschnitten (der Editor warnt).
- Auswahl „Qualität beim Import“: Beste (bis 16 kHz), bis 11 kHz, bis 8 kHz oder wie das Original. „Lautstärke normalisieren“ skaliert den lautesten Punkt auf Vollaussteuerung; „Dithering“ mischt vor dem Runden auf 8 Bit leises Rauschen bei, das die Stufen verwischt.

## Dateien, Sicherung, Mod-Pakete

- **Datei auf Original zurücksetzen…** ersetzt die geöffnete Datei durch ihre Sicherung `.bak`.
- **Mod-Paket erstellen…** packt alle Dateien aus `KB` und `TORE`, die sich von ihrer `.bak` unterscheiden, in eine ZIP-Datei (zum Weitergeben von Mods). **Mod-Paket einspielen…** schreibt eine solche ZIP in den Spielordner (mit `.bak` der ersetzten Dateien); es werden nur `KB\<Nr>.vga` und `TORE\<Name>` angenommen, jede andere Datei lehnt der Editor ab. Der Editor liest ZIP-Dateien mit und ohne Kompression.

## Offen

Texte, Schriften und Musik (KB248) bearbeitet der Editor nicht. Die Bedeutung der drei Stärkewerte und des Platzes in `KB\23.vga` ist nicht abschließend geklärt (siehe [FORMATS.md](FORMATS.md)).
