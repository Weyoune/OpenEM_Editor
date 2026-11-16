# OpenEM Editor

Asset- und Mannschaftseditor für die Originaldaten des **Eishockey Managers** (1993), ein Nebenprojekt von [OpenEM](https://github.com/Weyoune/OpenEM). Windows, native Oberfläche (Win32), eine einzelne `em_editor.exe` ohne weitere Laufzeit.

Bilder (mit Auswahlwerkzeugen, PNG-Import und -Export), Palettenabweichung, Torszenen-Grafik, Paletten, Hallenkarten, Mannschaften, **Spielszenen** (Torszenen-Skripte abspielen und bearbeiten, neue Szenen anlegen) und **Töne** (WAV-Import und -Export in der passenden Qualität), dazu Mod-Pakete (ZIP) und der Vergleich mit dem Original.

Der Editor enthält **keine Spieldaten**. Man braucht eine eigene Kopie des DOS-Spiels (den Ordner mit `EM.EXE`, `KB` und `TORE`).

## Benutzen

`em_editor.exe` in den Ordner des Eishockey Managers kopieren und starten. Liegt der Editor woanders, fragt er beim Start nach dem Ordner (oder `em_editor.exe --data "<Ordner>"`). Vor dem ersten Überschreiben legt er neben jede Datei eine `.bak`. Bedienung: [docs/EDITOR.md](docs/EDITOR.md), Dateiformate: [docs/FORMATS.md](docs/FORMATS.md).

## Bauen

Voraussetzungen: CMake ab 3.24, Visual Studio 2022 oder neuer (C++20).

```bash
cmake -S . -B build -DEM_DATA="<Pfad zum EM-Ordner>"
cmake --build build --config Release
ctest --test-dir build -C Release
```

`EM_DATA` braucht nur, wer die Tests mit den Originaldateien laufen lässt. Die Dokumentlogik und die Formate (`em_formats`, `em_editor_lib`) haben keine Oberflächenabhängigkeit und bauen auf jedem Compiler; nur `em_editor` ist Windows-only.

## Lizenz

GPL-3.0, siehe [LICENSE](LICENSE).
