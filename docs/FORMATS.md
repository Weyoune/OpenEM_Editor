# Dateiformate des Eishockey Managers (1993)

Was der Editor liest und schreibt. Alle Zahlen sind, wenn nicht anders angegeben, Little Endian. Der Quelltext steht in `src/formats/`.

## Bilder (`KB\*.vga`)

Bild: `u16 Breite, u16 Höhe, u16 Länge`, dann `Länge` Bytes RLE, optional danach 765 Byte Palette (255 RGB-Tripel, Farben 0–254).

RLE-Stück (`vga.cpp`): `c < 0x80` = die nächsten c + 1 Bytes sind Pixel, `c > 0x80` = das nächste Byte (c & 0x7F) + 1 Mal, `0x80` beendet das Stück. Bilder bis 200 Zeilen sind ein Stück; höhere haben zwei Stücke (Zeilen 0–199 und der Rest). `encode_vga` packt gierig (Läufe ab 3 gleichen Pixeln, sonst Literale bis 128). Die Bytes gleichen nicht dem Packer des Originals, `decode_vga(encode_vga(x))` liefert aber jedes Pixel zurück.

Paletten (`21.vga` Spiel, `247.vga` Torszenen) sind 765 rohe Bytes mit den Werten des 6-Bit-VGA-DAC; höhere Bits werden vom Spiel ignoriert (der Editor erhält sie beim Speichern). Wert 8-Bit = `(v << 2) | (v >> 4)`. Einige Bilder haben eine eigene Palette (KB5, 14, 16, 151, 195, 197); KB8 wird vom Spiel über die Palette von KB14 gezeichnet.

## Namen und Teamdaten (`KB\23.vga`, 36 648 Byte)

Kein Bild, sondern Tabellen und Klartext (ISO 646-DE: `[ \ ] { | } ~` = Ä Ö Ü ä ö ü ß).

| Offset | Inhalt |
|---|---|
| 0x0000 | 150 × `u16` (hohes Byte zuerst): Trikotfarbe der Torszenen, drei Nibble R, G, B (im Original je 0 oder 0xF) |
| 0x012C | 64 × `u8`: Wappen. Wappen = (Wert + 0xB0) & 0x3F, Bild KB 80 + Wappen; gespeichert wird Wappen + 0x50 |
| 0x016C | 150 × 3 × `u8`: Stärkewerte (die Bedeutung des dritten ist nicht geklärt) |
| 0x046E | 63 × `u8`: Platz 1…12 in der Liga |
| 0x04AD | 64 Einträge zu 0x20B Byte (48 Vereine, LEER1–4, 12 Nationalmannschaften): `u8 Flag, char[22] Name`, dann 25 × `u8 Flag, char[19] Spielername` |
| 0x876E | 86 europäische Vereine zu 23 Byte |

Mannschaften 0–47: Liga 1–3 und weitere Vereine, 48–51: LEER1–4 (ausgeblendet), 52–63: Nationalmannschaften, 64–149: Europapokal-Vereine (nur Name, Trikot, Stärken). Ein gelöschter Name beginnt mit dem Byte 0. Der Ausländer-Haken eines Spielers ist das Byte direkt hinter seinem Namen (= das Flag des nächsten Platzes).

`encode_db` und `encode_team_data` ändern nur Felder, die sich vom Original unterscheiden; alles andere bleibt byte-identisch.

## Torszenen (`TORE\`, `KB\231–247`)

Skriptname `TORE\<n>.<h|a><t|v>[e|j]`: `h`/`a` = Heim/Gast greift an, `t`/`v` = Tor/vergebene Chance, `e` = Strafschuss, `j` = mit sechstem Feldspieler. `TORE\anzahl` (`MAXSZENE:11|2|2`) nennt die Zahl der normalen Szenen und der e- und j-Varianten.

Skript (`tore.cpp`):

| Offset | Inhalt |
|---|---|
| 0x00 | `"TOR!"` |
| 0x04 | u16 (hohes Byte zuerst) Kamera x, y |
| 0x08 | u16 Puck x, y |
| 0x0C | 12 × {u8 ungenutzt, u8 Sprite, u16 x, u16 y}: Startaufstellung |
| 0x54 | Bilder zu 28 Byte bis `0xAAAA`: +0 Kamerabewegung, +1 Puckbewegung, +2 immer 0, +3 Ton (0 = keiner, sonst Sample 1–4), +4 12 × {u8 Sprite, u8 Bewegung} |

Bewegungsbyte m: dx = (m >> 3) & 7, dy = m & 7, Bit 0x40 negiert dx, Bit 0x80 negiert dy; ein Objekt bewegt sich also höchstens 7 Pixel je Bild. Koordinaten liegen in der Hochkant-Halle von 672 × 1152 Pixeln. Ein Objekt auf (0, 0) wird nicht gezeichnet; seine linke obere Ecke liegt bei (x − 0x50, y − 0x60). Die Kamera beginnt bei Skriptposition − (16, 32) und ist im 320 × 200-Fenster auf 0..0x160 / 0..0x3B8 begrenzt. Ein Bild dauert einen BIOS-Tick (≈ 54,9 ms).

Objekttypen: 0 und 1 sind die 5 × 5-Punkte (0 = Punkt, 1 = Puck), 2–0x29 Sprite 0–39 aus KB233, 0x2A–0x51 aus KB234, 0x52–0x5D aus KB235 (12 Sprites); höhere Typen zeichnen nichts.

`encode_tor` schreibt jedes der 48 Originalskripte byte-genau zurück.

| Datei | Inhalt |
|---|---|
| 231, 232 | Kachelsätze: u16 gepackte Länge + RLE, je 250 Kacheln zu 16 × 16, zusammen Kacheln 0…499 |
| 233, 234 | Sprites der beiden Mannschaften: gepackt, 40 Sprites zu 32 × 32 |
| 235 | 12 weitere Sprites zu 32 × 32 |
| 236, 242–245 | Hallenkarte, ungepackt: 42 Spalten × 72 Zeilen u16 (hohes Byte zuerst), spaltenweise; Kachel = Wert & 0x7FFF, Bit 0x8000 = senkrecht gespiegelt |
| 237–241 | Creative Voice (VOC), siehe unten |
| 246 | Anzeigetafel 320 × 40, RLE ohne Längenfeld |
| 247 | Palette |

## Töne (`KB\237–241`)

Creative Voice 1.10, 8 Bit vorzeichenlos, mono (80h = Stille): Kennung `Creative Voice File` + 1Ah, u16 Datenbeginn (1Ah), u16 Version 010Ah, u16 Prüfwort `~Version + 1234h`, dann Blöcke `u8 Typ, u24 Länge, Daten`. Benutzt werden Typ 1 (Zeitkonstante tc, Packart 0, Abtastwerte; Rate = 1 000 000 / (256 − tc)) und die Wiederholung Typ 6/7.

| Datei | Rate | Rolle |
|---|---|---|
| 237 | 4 672 Hz | Hintergrund (Publikum), endlos wiederholt |
| 238 | 4 672 Hz | Szenen-Ton 1 |
| 239 | 11 904 Hz | Szenen-Ton 2 |
| 240 | 6 451 Hz | Szenen-Ton 3 |
| 241 | 6 451 Hz | Szenen-Ton 4 |

Das Spiel lädt jeden Ton in einen Puffer von der Größe der Originaldatei; ein ersetzter Ton darf diese Dateigröße nicht überschreiten. Der Editor wandelt importierte WAV-Dateien deshalb in 8 Bit mono und passt die Abtastrate an, bis die Datei in den Puffer passt.
