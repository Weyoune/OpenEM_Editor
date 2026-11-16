// SPDX-License-Identifier: GPL-3.0-only
#include "editor/pixel_editor.hpp"

#include "formats/tore.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace em::editor {

namespace {
// Goal scene graphics: sheets are laid out as grids of cells (tiles 16 x 16, 16 per row; sprites 32 x 32, 8 per row)
struct SheetSpec {
    int number;
    PixelEditor::Kind kind;
    int cell, cols;
    const char* label;
};
constexpr SheetSpec kSheets[] = {
    {231, PixelEditor::Kind::Tiles, 16, 16, "Hallenkacheln A"}, {232, PixelEditor::Kind::Tiles, 16, 16, "Hallenkacheln B"},
    {233, PixelEditor::Kind::Sprites, 32, 8, "Spieler Team 1"},  {234, PixelEditor::Kind::Sprites, 32, 8, "Spieler Team 2"},
    {235, PixelEditor::Kind::Sprites, 32, 8, "Torhüter, Puck"},
};
constexpr int kPanelNumber = 246;

const SheetSpec* sheet_spec(int n) {
    for (const auto& s : kSheets)
        if (s.number == n) return &s;
    return nullptr;
}

Image sheet_from_raw(const Bytes& raw, int cell, int cols, int& count) {
    const size_t bytes = size_t(cell) * cell;
    count = int(raw.size() / bytes);
    const int rows = (count + cols - 1) / cols;
    Image img(cols * cell, rows * cell);
    for (int i = 0; i < count; i++)
        for (int y = 0; y < cell; y++)
            std::copy_n(raw.data() + size_t(i) * bytes + size_t(y) * cell, cell,
                        img.pixels.data() + size_t(i / cols * cell + y) * img.width + size_t(i % cols * cell));
    return img;
}

Bytes raw_from_sheet(const Image& img, int cell, int cols, int count) {
    const size_t bytes = size_t(cell) * cell;
    Bytes raw(size_t(count) * bytes);
    for (int i = 0; i < count; i++)
        for (int y = 0; y < cell; y++)
            std::copy_n(img.pixels.data() + size_t(i / cols * cell + y) * img.width + size_t(i % cols * cell), cell,
                        raw.data() + size_t(i) * bytes + size_t(y) * cell);
    return raw;
}
}  // namespace

PixelEditor::PixelEditor(const fs::path& data) : kb_(data / "KB") {
    global_pal_ = decode_palette(read_file(kb_ / "21.vga"));
    pal_ = global_pal_;
    std::error_code ec0;
    if (fs::exists(kb_ / "247.vga", ec0)) tore_pal_ = decode_palette(read_file(kb_ / "247.vga"));
    else tore_pal_ = global_pal_;
    for (int n = 0; n < 256; n++) {
        const fs::path p = kb_ / (std::to_string(n) + ".vga");
        std::error_code ec;
        if (!fs::exists(p, ec)) continue;
        try {
            Bytes d = read_file(p);
            const std::string name = "KB" + std::to_string(n);
            if (const SheetSpec* s = sheet_spec(n)) {
                int count = 0;
                Image sheet = sheet_from_raw(unpack_kb_block(d), s->cell, s->cols, count);
                files_.push_back({n, s->kind, sheet.width, sheet.height, false, name + "  " + s->label});
            } else if (n == kPanelNumber) {
                files_.push_back({n, Kind::Panel, kTorPanelWidth, kTorPanelHeight, false, name + "  Anzeigetafel"});
            } else {
                VgaKind k = classify_vga(d);
                if (k == VgaKind::Image || k == VgaKind::ImageWithPalette)
                    files_.push_back({n, Kind::Image, le16(d, 0), le16(d, 2), k == VgaKind::ImageWithPalette, name});
            }
        } catch (const std::exception&) {
        }
    }
    status_ = std::to_string(files_.size()) + " Bilder in " + kb_.string();
}

// Images drawn by the game over the palette of another screen (found in src/ui): KB8 over KB14
static int default_source(int number) { return number == 8 ? 14 : 21; }

std::vector<int> PixelEditor::palette_choices() const {
    std::vector<int> out;
    for (int n : {21, 247, 5, 14, 16, 151, 195, 197}) {
        std::error_code ec;
        if (fs::exists(kb_ / (std::to_string(n) + ".vga"), ec)) out.push_back(n);
    }
    return out;
}

bool PixelEditor::set_palette_source(int number) {
    if (current_ >= 0 && has_embedded_) return false;  // the image brings its own palette
    try {
        Bytes d = read_file(kb_ / (std::to_string(number) + ".vga"));
        if (number == 21 || number == 247) pal_ = decode_palette(d);
        else if (classify_vga(d) == VgaKind::ImageWithPalette) pal_ = decode_palette(d, 6 + le16(d, 4));
        else return false;
        pal_source_ = number;
        revision_++;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void PixelEditor::close() {
    current_ = -1;
    dirty_ = false;
    drawing_ = false;
    undo_.clear();
    redo_.clear();
    img_ = Image();
    cell_ = cols_ = count_ = 0;
    kind_ = Kind::Image;
    revision_++;
}

bool PixelEditor::load(int number) {
    const fs::path p = kb_ / (std::to_string(number) + ".vga");
    try {
        Bytes d = read_file(p);
        // palettes may have been edited since start-up
        global_pal_ = decode_palette(read_file(kb_ / "21.vga"));
        std::error_code ec;
        if (fs::exists(kb_ / "247.vga", ec)) tore_pal_ = decode_palette(read_file(kb_ / "247.vga"));
        std::string what;
        Kind kind = Kind::Image;
        int cell = 0, cols = 0, count = 0;
        Image img;
        bool embedded = false;
        if (const SheetSpec* s = sheet_spec(number)) {
            kind = s->kind;
            cell = s->cell;
            cols = s->cols;
            img = sheet_from_raw(unpack_kb_block(d), cell, cols, count);
            what = std::to_string(count) + (kind == Kind::Tiles ? " Kacheln" : " Sprites") + " à " + std::to_string(cell) + " × " + std::to_string(cell) + ", Palette KB247";
        } else if (number == kPanelNumber) {
            kind = Kind::Panel;
            img = decode_tor_panel(d);
            what = "Anzeigetafel, Palette KB247";
        } else {
            VgaKind k = classify_vga(d);
            if (k != VgaKind::Image && k != VgaKind::ImageWithPalette) {
                status_ = "KB" + std::to_string(number) + " ist kein Bild";
                return false;
            }
            img = decode_vga(d);
            embedded = k == VgaKind::ImageWithPalette;
            what = embedded ? std::string("eigene Palette") : "Palette KB" + std::to_string(default_source(number));
            if (!img.complete) what += ", Datenstrom unvollständig";
        }
        const int source = embedded ? number : kind != Kind::Image ? 247 : default_source(number);
        img_ = std::move(img);
        has_embedded_ = embedded;
        pal_ = embedded ? decode_palette(d, 6 + le16(d, 4)) : global_pal_;
        pal_source_ = source;
        if (!embedded && source != 21) set_palette_source(source);
        kind_ = kind;
        cell_ = cell;
        cols_ = cols;
        count_ = count;
        current_ = number;
        dirty_ = false;
        drawing_ = false;
        undo_.clear();
        redo_.clear();
        revision_++;
        status_ = "KB" + std::to_string(number) + " geladen: " + std::to_string(img_.width) + " × " +
                  std::to_string(img_.height) + ", " + what;
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
}

bool PixelEditor::save() {
    if (current_ < 0) return false;
    try {
        Bytes out;
        if (kind_ == Kind::Image) out = encode_vga(img_, has_embedded_ ? &pal_ : nullptr);
        else if (kind_ == Kind::Panel) out = encode_tor_panel(img_);
        else out = pack_kb_block(raw_from_sheet(img_, cell_, cols_, count_));
        const fs::path p = kb_ / (std::to_string(current_) + ".vga");
        fs::path bak = p;
        bak += ".bak";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(p, bak, ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        if (!f) throw std::runtime_error("Schreiben fehlgeschlagen");
        dirty_ = false;
        status_ = "KB" + std::to_string(current_) + " gespeichert (" + std::to_string(out.size()) +
                  " Byte, Sicherung als .bak)";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Speichern fehlgeschlagen: ") + e.what();
        return false;
    }
}

bool PixelEditor::export_bmp(const fs::path& to) {
    if (current_ < 0) return false;
    const int w = img_.width, h = img_.height, stride = (w + 3) & ~3;
    const uint32_t offset = 14 + 40 + 256 * 4, size = offset + uint32_t(stride) * uint32_t(h);
    Bytes b(size, 0);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + size_t(i)] = uint8_t(v >> (8 * i)); };
    auto put16 = [&](size_t o, uint16_t v) { b[o] = uint8_t(v); b[o + 1] = uint8_t(v >> 8); };
    b[0] = 'B'; b[1] = 'M';
    put32(2, size); put32(10, offset);
    put32(14, 40); put32(18, uint32_t(w)); put32(22, uint32_t(h)); put16(26, 1); put16(28, 8);
    put32(34, uint32_t(stride) * uint32_t(h)); put32(46, 256);
    for (size_t i = 0; i < 256; i++) {  // BGRA palette
        b[54 + i * 4] = pal_.rgb[i][2];
        b[55 + i * 4] = pal_.rgb[i][1];
        b[56 + i * 4] = pal_.rgb[i][0];
    }
    for (int y = 0; y < h; y++)  // bottom-up rows
        std::copy_n(img_.pixels.data() + size_t(h - 1 - y) * w, w, b.data() + offset + size_t(y) * stride);
    std::ofstream f(to, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    const bool ok = bool(f);
    status_ = ok ? "Exportiert: " + to.filename().string() : "Export fehlgeschlagen";
    return ok;
}

bool PixelEditor::import_argb(const std::vector<uint32_t>& argb, int w, int h, bool allow_resize, int* approximated) {
    if (current_ < 0 || w <= 0 || h <= 0 || argb.size() != size_t(w) * size_t(h)) {
        status_ = "Import: kein Bild geöffnet oder ungültige Daten";
        return false;
    }
    const bool resize = w != img_.width || h != img_.height;
    if (resize && (!allow_resize || kind_ != Kind::Image)) {
        status_ = "Import: Größe " + std::to_string(w) + " × " + std::to_string(h) + " passt nicht zu " + std::to_string(img_.width) + " × " + std::to_string(img_.height);
        return false;
    }
    std::vector<uint8_t> out(argb.size());
    std::unordered_map<uint32_t, uint8_t> cache;
    int inexact = 0;
    for (size_t i = 0; i < argb.size(); i++) {
        const uint32_t v = argb[i];
        if ((v >> 24) < 128) { out[i] = 0; continue; }
        const uint32_t key = v & 0xFFFFFF;
        if (!resize) {  // same colour as before: keep the index (palettes may hold a colour twice)
            const auto& oc = pal_.rgb[img_.pixels[i]];
            if (key == (uint32_t(oc[0]) << 16 | uint32_t(oc[1]) << 8 | oc[2])) { out[i] = img_.pixels[i]; continue; }
        }
        auto it = cache.find(key);
        if (it == cache.end()) {
            const int r = int(key >> 16 & 255), g = int(key >> 8 & 255), b = int(key & 255);
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 255; k++) {
                const auto& c = pal_.rgb[size_t(k)];
                const int d = (c[0] - r) * (c[0] - r) + (c[1] - g) * (c[1] - g) + (c[2] - b) * (c[2] - b);
                if (d < bd) { bd = d; best = k; }
            }
            it = cache.emplace(key, uint8_t(best)).first;
            if (bd != 0) cache_inexact_.insert(key);
        }
        out[i] = it->second;
        if (cache_inexact_.count(key)) inexact++;
    }
    cache_inexact_.clear();
    if (resize) {
        img_ = Image(w, h);
        undo_.clear();
        redo_.clear();
    } else {
        push_undo();
    }
    img_.pixels = std::move(out);
    dirty_ = true;
    revision_++;
    if (approximated) *approximated = inexact;
    status_ = "Importiert: " + std::to_string(w) + " × " + std::to_string(h) + (inexact ? ", " + std::to_string(inexact) + " Pixel der Palette angenähert" : ", Farben exakt");
    return true;
}

void PixelEditor::put(int x, int y, int c) {
    if (x >= 0 && y >= 0 && x < img_.width && y < img_.height) {
        img_.at(x, y) = uint8_t(c);
        revision_++;
    }
}

void PixelEditor::draw_line(int x0, int y0, int x1, int y1, int c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void PixelEditor::draw_rect(int x0, int y0, int x1, int y1, int c, bool filled) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (filled || x == x0 || x == x1 || y == y0 || y == y1) put(x, y, c);
}

void PixelEditor::flood(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= img_.width || y >= img_.height) return;
    const uint8_t from = img_.at(x, y);
    if (from == c) return;
    std::vector<std::pair<int, int>> stack{{x, y}};
    while (!stack.empty()) {
        auto [px, py] = stack.back();
        stack.pop_back();
        if (px < 0 || py < 0 || px >= img_.width || py >= img_.height || img_.at(px, py) != from) continue;
        put(px, py, c);
        stack.push_back({px + 1, py});
        stack.push_back({px - 1, py});
        stack.push_back({px, py + 1});
        stack.push_back({px, py - 1});
    }
}

void PixelEditor::push_undo() {
    undo_.push_back(img_.pixels);
    if (undo_.size() > 64) undo_.erase(undo_.begin());
    redo_.clear();
}

void PixelEditor::undo() {
    if (undo_.empty()) return;
    redo_.push_back(img_.pixels);
    img_.pixels = undo_.back();
    undo_.pop_back();
    dirty_ = true;
    revision_++;
}

void PixelEditor::redo() {
    if (redo_.empty()) return;
    undo_.push_back(img_.pixels);
    img_.pixels = redo_.back();
    redo_.pop_back();
    dirty_ = true;
    revision_++;
}

int PixelEditor::pick(int x, int y) const {
    if (current_ < 0 || x < 0 || y < 0 || x >= img_.width || y >= img_.height) return -1;
    return img_.pixels[size_t(y) * img_.width + size_t(x)];
}

void PixelEditor::stroke_begin(int x, int y) {
    if (current_ < 0 || x < 0 || y < 0 || x >= img_.width || y >= img_.height) return;
    if (tool_ == Tool::Picker) {
        color_ = img_.at(x, y);
        return;
    }
    push_undo();
    base_ = img_.pixels;
    drawing_ = true;
    sx_ = lx_ = x;
    sy_ = ly_ = y;
    if (tool_ == Tool::Fill) {
        flood(x, y, color_);
        stroke_end();
    } else if (tool_ == Tool::Pencil) {
        put(x, y, color_);
    } else {
        stroke_move(x, y);
    }
}

void PixelEditor::stroke_move(int x, int y) {
    if (!drawing_) return;
    x = std::clamp(x, 0, img_.width - 1);
    y = std::clamp(y, 0, img_.height - 1);
    if (tool_ == Tool::Pencil) {
        draw_line(lx_, ly_, x, y, color_);
    } else {
        img_.pixels = base_;
        revision_++;
        if (tool_ == Tool::Line) draw_line(sx_, sy_, x, y, color_);
        else draw_rect(sx_, sy_, x, y, color_, tool_ == Tool::FilledRect);
    }
    lx_ = x;
    ly_ = y;
}

void PixelEditor::stroke_end() {
    if (!drawing_) return;
    drawing_ = false;
    if (img_.pixels == base_) {  // nothing changed: drop the snapshot
        undo_.pop_back();
        return;
    }
    dirty_ = true;
}

}  // namespace em::editor
