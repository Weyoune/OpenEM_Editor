# OpenEM Editor (`em_editor`)

Bearbeitet die Bilddateien des Originalspiels. Stand 10.11.2026, nur Windows (Win32 mit Standard-Steuerelementen). Die Dokumentlogik (`src/editor/*_doc.*`, `pixel_editor.*`) hat keine Oberflächenabhängigkeit und läuft im Test (`ctest -R original`).

```
em_editor [--data <EM-Ordner>] [--file <KB-Nr.>] [--tab 0|1|2] [--shot out.bmp]
```
Ohne `--data` sucht der Editor wie `openem` (`OPENEM_DATA`, `work/EM`, Programmordner, aktueller Ordner) und fragt sonst nach dem Ordner. Vor dem ersten Überschreiben legt er neben jede Datei eine `.bak` (nie überschrieben).

| Registerkarte | Inhalt |
|---|---|
| Bilder | alle KB-Bilder (`KB\*.vga`, auch die mit eigener Palette); Palette KB21 bzw. die eingebettete |
| Torszenen | Hallenkacheln KB231/232 (16 × 16, 16 je Zeile), Spieler- und Torhütersprites KB233–235 (32 × 32, 8 je Zeile), Anzeigetafel KB246 (320 × 40); Palette KB247; Zellgrenzen orange, in der Statusleiste Kachel-/Spritenummer |
| Mannschaften | `KB\23.vga`: Vereinsname (22 Zeichen), 25 Spielernamen (19 Zeichen, Großbuchstaben, Zeichensatz des Spiels) mit Ausländer-Haken, Wappen 0–63 mit Vorschau, Platz 1–12, drei Stärkewerte, Trikotfarbe der Torszenen (Rot, Grün, Blau je 0–15) mit Vorschau, Tauschen zweier Mannschaften (1–48, 53–64; LEER1–4 ausgeblendet). Die Liste ist in Gruppen geteilt (Liga 1–3, weitere Vereine 37–48, Nationalmannschaften, Europapokal-Vereine; letztere mit Name, Trikot und Stärken). Das Wappen lässt sich direkt unter den Spielern zeichnen (Stift, Füllen, Rückgängig; „Wappen speichern“ schreibt KB80+Wappen und gilt für alle Vereine mit diesem Wappen). Änderungen gelten sofort, ohne „Übernehmen“; Speichern warnt vor doppelten Namen; „Rückgängig“ verwirft alle Änderungen der Datei |
| Hallenkarten | Kachelkarten der Torszenen-Halle: KB236 und KB242–245 (42 × 72 Kacheln); Kachelwahl (500 Kacheln aus KB231/232), Malen, Füllen, Pipette (auch Rechtsklick), Haken „Gespiegelt“ (Bit 0x8000: Kachel oben/unten gespiegelt), Raster, Zoom 1–4×; Palette KB247 |
| Spielszenen | Die Skripte der Torszenen `TORE\<n>.<h\|a><t\|v>[e\|j]` (48 Dateien: Heim/Gast greift an, Tor/vergebene Chance, Strafschuss, sechs Feldspieler), links nach Gruppen. Abspielen mit Zeitleiste (Pfeil links/rechts, Pos1/Ende, Leertaste), drei Ansichten: Spielfenster (320 × 200 wie im Spiel), Ausschnitt um die Auswahl, ganze Halle (mit orangem Kamerafenster); Halle KB236/KB242–245 wählbar. Figuren, Puck und Kamera mit der Maus ziehen (Bahn der Auswahl in Blau) oder in x/y eintippen; Typ (Sprite) per Liste der 94 Objekttypen oder Zahl, „bis zum Ende“ setzt ihn für alle Folgebilder; Ton 0–4 je Bild; Bild einfügen (Bild bleibt gleich), kopieren (Bewegung wiederholt sich), löschen; Rückgängig/Wiederholen. Position 0 ist die Startaufstellung: ein Zug dort verschiebt die ganze Bahn. Ab Bild 1 ändert ein Zug nur den Schritt in dieses Bild (höchstens 7 Pixel je Achse zum vorigen Bild); „Folgebilder bleiben“ gleicht den nächsten Schritt aus, sodass die Bahn danach unverändert bleibt, soweit er in 7 Pixel passt. Speichern schreibt die Datei mit `.bak`; unveränderte Skripte schreibt der Editor bytegleich zurück |
| Paletten | KB21 und KB247: 255 Farben mit sechs Bit je Kanal (VGA-DAC); die ungenutzten oberen Bits der Datei bleiben erhalten; „Rückgängig“ verwirft alle Änderungen der Palette |

Werkzeuge: Stift (P), Linie (L), Rechteck (R), gefülltes Rechteck (B), Füllen (F), Pipette (I, oder Rechtsklick); Rückgängig/Wiederholen (Strg+Z/Y), Speichern (Strg+S), BMP-Export (Strg+E), Raster (G), Zoom (Strg+Mausrad, +/−).

Bilder lassen sich als PNG (8 Bit mit der aktuellen Palette) oder BMP exportieren und als PNG, BMP, GIF oder JPEG importieren (Strg+I, über Windows Imaging Component): jedes Pixel erhält die nächste Farbe der Palette (Farben 0–254, transparente Pixel Farbe 0); unveränderte Farben behalten ihren Index, auch wenn die Palette eine Farbe doppelt enthält. Eine andere Größe akzeptiert der Editor nur bei einfachen Bildern und nach Rückfrage; Kachel-/Spritebögen und die Anzeigetafel müssen genau passen. Kommandozeile: `--file N --export-png out.png` und `--file N --import-png in.png` (speichert).

Ein Release-Ordner entsteht mit `cmake --install build --config Release --prefix <Ordner>`.

Noch offen: Szenen: neue Skriptdateien anlegen, Töne abspielen; Importe über mehrere Bilder gleichzeitig, eine Anzeige der Palettennähe (Abweichung je Pixel). Bedeutung der drei Stärkewerte und des Platzes ist nicht abschließend geklärt (siehe `docs/FORMATS.md`); die Ausländer-Markierung ist das Byte hinter dem Spielernamen.
