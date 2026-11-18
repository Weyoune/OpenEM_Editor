// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the pixel editor for the KB\*.vga images: loading, drawing tools, undo/redo, saving.
// No user interface in here (the Windows front end is win_main.cpp), so it can be tested without a window.
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "formats/vga.hpp"

namespace em::editor {

class PixelEditor {
public:
    enum class Kind { Image, Tiles, Sprites, Panel };
    struct Entry {
        int number;
        Kind kind;
        int w, h;       // size of the picture shown (sheets: the laid-out sheet)
        bool embedded;  // image carries its own palette
        std::string label;
    };
    enum class Tool { Pencil, Line, Rect, FilledRect, Fill, Picker, Select };
    struct Rect {
        int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // inclusive corners
        int width() const { return x1 - x0 + 1; }
        int height() const { return y1 - y0 + 1; }
    };
    struct ImportReport {
        int pixels = 0, approximated = 0;  // approximated: no exact palette colour
        double worst = 0, mean = 0;        // RGB distance (0..441) of the approximated pixels
    };

    explicit PixelEditor(const std::filesystem::path& data);

    const std::vector<Entry>& files() const { return files_; }
    bool load(int number);
    void close();  // drop the open picture (no saving)
    Kind kind() const { return kind_; }
    int cell_size() const { return cell_; }     // tile or sprite edge, 0 for plain images
    int cell_columns() const { return cols_; }  // cells per row of a sheet
    bool save();                                       // writes KB\<n>.vga, first time with a .bak copy
    bool export_bmp(const std::filesystem::path& to);  // 8-bit indexed BMP with the current palette
    // Replaces the picture by 32-bit pixels (0xAARRGGBB, row by row): each pixel takes the nearest colour of the
    // current palette (colours 0..254; transparent pixels become colour 0). A different size is accepted for plain images
    // only when `allow_resize` is set (the undo history is then cleared); sheets and the panel must match.
    // `approximated` receives the number of pixels without an exact palette match.
    bool import_argb(const std::vector<uint32_t>& argb, int w, int h, bool allow_resize, int* approximated = nullptr);
    Kind picture_kind() const { return kind_; }
    // How far the last import was from the palette: report and a per-pixel distance map (0 = exact, 255 = far)
    const ImportReport& last_import() const { return report_; }
    const std::vector<uint8_t>& deviation() const { return deviation_; }
    // Comparison with the original: pixels that differ from the backup (.bak) or, without one, from the file on disk;
    // empty when there is nothing to compare. restore_original() copies the backup back over the file and reloads it.
    bool has_backup() const;
    std::vector<uint8_t> changed_pixels() const;
    bool restore_original();
    void undo();
    void redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    void set_tool(Tool t) { tool_ = t; }
    Tool tool() const { return tool_; }
    void set_color(int c) { color_ = c & 255; }
    int color() const { return color_; }

    // Strokes in image coordinates (the canvas converts the mouse position)
    void stroke_begin(int x, int y);
    void stroke_move(int x, int y);
    void stroke_end();
    bool drawing() const { return drawing_; }
    int pick(int x, int y) const;  // colour index at a pixel, -1 outside

    // Selection (tool Select: drag draws a rectangle, dragging inside it moves the pixels) and clipboard
    bool has_selection() const { return sel_valid_; }
    Rect selection() const { return sel_; }
    void select_all();
    void clear_selection();
    bool copy();
    bool cut();
    bool paste(int x, int y);  // top left corner; the pasted pixels become the selection
    bool has_clipboard() const { return !clip_.pixels.empty(); }
    bool delete_selection();   // colour 0
    // Mirror or rotate the selection (without one: the whole picture, plain images only); rotation needs a square
    bool flip(bool horizontal);
    bool rotate_cw();

    bool dirty() const { return dirty_; }
    int current() const { return current_; }
    const Image& image() const { return img_; }
    const Palette& palette() const { return pal_; }
    // Images without their own palette are shown with a palette chosen here: KB21 (game), KB247 (goal scenes) or the
    // palette of a full screen (KB5, 14, 16, 151, 195, 197) - the game keeps the palette of the last screen it loaded,
    // e.g. KB8 (stadium parts) is drawn over KB14. The choice does not change what is saved.
    std::vector<int> palette_choices() const;
    // Pseudo palette for the newspaper photos of some mods (KB200..229): 16 grey levels whose palette index is stored
    // bit-reversed (index bit 0 is the most significant brightness bit), entries 16..255 black.
    static constexpr int kGreyPhoto = 1000;
    int palette_source() const { return pal_source_; }
    bool set_palette_source(int number);
    bool has_own_palette() const { return has_embedded_; }
    const std::string& status() const { return status_; }
    void set_status(std::string s) { status_ = std::move(s); }
    // Bumped on every change of the picture; front ends redraw when it differs from what they showed
    unsigned revision() const { return revision_; }

private:
    std::filesystem::path kb_;
    std::vector<Entry> files_;
    Palette global_pal_, tore_pal_, pal_;
    bool has_embedded_ = false;
    int pal_source_ = 21;
    Image img_;
    int current_ = -1;
    Kind kind_ = Kind::Image;
    int cell_ = 0, cols_ = 0, count_ = 0;  // sheet layout: cell edge, columns, used cells
    bool dirty_ = false;
    std::string status_;
    unsigned revision_ = 0;

    Tool tool_ = Tool::Pencil;
    int color_ = 1;
    std::vector<std::vector<uint8_t>> undo_, redo_;
    bool drawing_ = false;
    int sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0;
    std::vector<uint8_t> base_;
    ImportReport report_;
    mutable Image baseline_;  // the file as it was before the editor touched it (cached for changed_pixels)
    mutable int baseline_for_ = -1;
    mutable bool baseline_ok_ = false;
    std::vector<uint8_t> deviation_;
    bool sel_valid_ = false;
    Rect sel_, sel_orig_;
    int anchor_x_ = 0, anchor_y_ = 0, press_x_ = 0, press_y_ = 0;
    enum class Drag { None, Select, Move } drag_ = Drag::None;
    std::vector<uint8_t> float_;
    Image clip_;
    bool load_file(const std::filesystem::path& p, int number);
    bool region(Rect& r, const char* what);

    void put(int x, int y, int c);
    void draw_line(int x0, int y0, int x1, int y1, int c);
    void draw_rect(int x0, int y0, int x1, int y1, int c, bool filled);
    void flood(int x, int y, int c);
    void push_undo();
};

}  // namespace em::editor
