// SPDX-License-Identifier: GPL-3.0-only
#include "editor/map_doc.hpp"

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace em::editor {

const std::vector<int>& MapDoc::numbers() {
    static const std::vector<int> n{236, 242, 243, 244, 245};
    return n;
}

MapDoc::MapDoc(const fs::path& data) : kb_(data / "KB") {}

void MapDoc::draw_cell(int c, int r) {
    const int t = tile(c, r);
    const bool mirror = mirrored(c, r);
    if (t >= int(tiles_.size())) return;
    const Image& ti = tiles_[size_t(t)];
    for (int y = 0; y < kTile; y++) {
        const int sy = mirror ? kTile - 1 - y : y;
        std::copy_n(ti.pixels.data() + size_t(sy) * kTile, kTile,
                    rink_.pixels.data() + size_t(r * kTile + y) * rink_.width + size_t(c * kTile));
    }
}

bool MapDoc::load(int number) {
    try {
        Bytes a = unpack_kb_block(read_file(kb_ / "231.vga")), b = unpack_kb_block(read_file(kb_ / "232.vga"));
        a.insert(a.end(), b.begin(), b.end());
        const size_t bytes = size_t(kTile) * kTile;
        tiles_.clear();
        for (size_t i = 0; (i + 1) * bytes <= a.size(); i++) {
            Image t(kTile, kTile);
            std::copy_n(a.data() + i * bytes, bytes, t.pixels.data());
            tiles_.push_back(std::move(t));
        }
        palette_ = decode_palette(read_file(kb_ / "247.vga"));
        Bytes d = read_file(kb_ / (std::to_string(number) + ".vga"));
        if (d.size() < size_t(kCols) * kRows * 2) throw FormatError("Karte zu kurz");
        map_.assign(size_t(kCols) * kRows, 0);
        for (size_t i = 0; i < map_.size(); i++) map_[i] = uint16_t(d[i * 2] << 8 | d[i * 2 + 1]);
        rink_ = Image(kCols * kTile, kRows * kTile);
        for (int c = 0; c < kCols; c++)
            for (int r = 0; r < kRows; r++) draw_cell(c, r);
        current_ = number;
        dirty_ = false;
        in_stroke_ = false;
        undo_.clear();
        redo_.clear();
        revision_++;
        status_ = "KB" + std::to_string(number) + " geladen: " + std::to_string(kCols) + " × " + std::to_string(kRows) + " Kacheln, Palette KB247";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
}

void MapDoc::close() {
    current_ = -1;
    dirty_ = false;
    map_.clear();
    undo_.clear();
    redo_.clear();
    rink_ = Image();
    revision_++;
}

bool MapDoc::save() {
    if (current_ < 0) return false;
    try {
        // keep any bytes behind the 6048 map bytes as they are
        const fs::path p = kb_ / (std::to_string(current_) + ".vga");
        Bytes d = read_file(p);
        for (size_t i = 0; i < map_.size(); i++) {
            d[i * 2] = uint8_t(map_[i] >> 8);
            d[i * 2 + 1] = uint8_t(map_[i]);
        }
        fs::path bak = p;
        bak += ".bak";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(p, bak, ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
        if (!f) throw std::runtime_error("Schreiben fehlgeschlagen");
        dirty_ = false;
        status_ = "KB" + std::to_string(current_) + " gespeichert (Sicherung als .bak)";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Speichern fehlgeschlagen: ") + e.what();
        return false;
    }
}

void MapDoc::snapshot() {
    undo_.push_back(map_);
    if (undo_.size() > 64) undo_.erase(undo_.begin());
    redo_.clear();
}

void MapDoc::stroke_begin() {
    if (current_ < 0) return;
    snapshot();
    in_stroke_ = true;
    stroke_changed_ = false;
}

void MapDoc::stroke_end() {
    if (!in_stroke_) return;
    in_stroke_ = false;
    if (!stroke_changed_) undo_.pop_back();  // nothing changed
}

void MapDoc::set(int c, int r, int t, bool mirror) {
    if (current_ < 0 || c < 0 || r < 0 || c >= kCols || r >= kRows || t < 0 || t >= int(tiles_.size())) return;
    const uint16_t v = uint16_t(t | (mirror ? 0x8000 : 0));
    uint16_t& e = map_[index(c, r)];
    if (e == v) return;
    const bool own = !in_stroke_;
    if (own) snapshot();
    e = v;
    draw_cell(c, r);
    dirty_ = true;
    revision_++;
    if (in_stroke_) stroke_changed_ = true;
}

void MapDoc::fill(int c, int r, int t, bool mirror) {
    if (current_ < 0 || c < 0 || r < 0 || c >= kCols || r >= kRows || t < 0 || t >= int(tiles_.size())) return;
    const uint16_t from = map_[index(c, r)], to = uint16_t(t | (mirror ? 0x8000 : 0));
    if (from == to) return;
    snapshot();
    std::vector<std::pair<int, int>> stack{{c, r}};
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        if (x < 0 || y < 0 || x >= kCols || y >= kRows || map_[index(x, y)] != from) continue;
        map_[index(x, y)] = to;
        draw_cell(x, y);
        stack.push_back({x + 1, y});
        stack.push_back({x - 1, y});
        stack.push_back({x, y + 1});
        stack.push_back({x, y - 1});
    }
    dirty_ = true;
    revision_++;
}

void MapDoc::undo() {
    if (undo_.empty()) return;
    redo_.push_back(map_);
    map_ = undo_.back();
    undo_.pop_back();
    for (int c = 0; c < kCols; c++)
        for (int r = 0; r < kRows; r++) draw_cell(c, r);
    dirty_ = true;
    revision_++;
}

void MapDoc::redo() {
    if (redo_.empty()) return;
    undo_.push_back(map_);
    map_ = redo_.back();
    redo_.pop_back();
    for (int c = 0; c < kCols; c++)
        for (int r = 0; r < kRows; r++) draw_cell(c, r);
    dirty_ = true;
    revision_++;
}

}  // namespace em::editor
