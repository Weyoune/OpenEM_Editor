// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the hall map editor for KB\236.vga and KB\242..245.vga: the tile maps of the goal scene rink
// (42 columns x 72 rows of 16 x 16 tiles; entry = u16 big endian, tile = entry & 0x7FFF, bit 0x8000 = mirrored top to
// bottom). The tiles come from KB231 and KB232 (500 tiles), the palette is KB247. See src/formats/tore.hpp.
#include <filesystem>
#include <string>
#include <vector>

#include "formats/tore.hpp"
#include "formats/vga.hpp"

namespace em::editor {

class MapDoc {
public:
    static constexpr int kCols = kRinkColumns, kRows = kRinkRows, kTile = kRinkTile, kTiles = 500;
    explicit MapDoc(const std::filesystem::path& data);

    static const std::vector<int>& numbers();  // 236, 242, 243, 244, 245
    bool load(int number);
    void close();
    bool save();  // writes KB\<n>.vga, first time with a .bak copy
    int current() const { return current_; }
    bool dirty() const { return dirty_; }
    unsigned revision() const { return revision_; }
    const std::string& status() const { return status_; }

    int tile(int c, int r) const { return map_[index(c, r)] & 0x7FFF; }
    bool mirrored(int c, int r) const { return (map_[index(c, r)] & 0x8000) != 0; }

    // Painting: strokes are one undo step
    void stroke_begin();
    void stroke_end();
    void set(int c, int r, int tile, bool mirror);
    void fill(int c, int r, int tile, bool mirror);  // replaces the connected cells that equal the clicked one
    void undo();
    void redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    const Image& rink() const { return rink_; }        // the whole map, 672 x 1152 (updated per cell)
    const Image& tile_image(int t) const { return tiles_[size_t(t)]; }
    int tile_count() const { return int(tiles_.size()); }
    const Palette& palette() const { return palette_; }

private:
    std::filesystem::path kb_;
    std::vector<Image> tiles_;
    Palette palette_;
    std::vector<uint16_t> map_;
    Image rink_;
    int current_ = -1;
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;
    std::vector<std::vector<uint16_t>> undo_, redo_;
    bool in_stroke_ = false, stroke_changed_ = false;

    static size_t index(int c, int r) { return size_t(c) * kRows + size_t(r); }  // the map runs down each column
    void draw_cell(int c, int r);
    void snapshot();
};

}  // namespace em::editor
