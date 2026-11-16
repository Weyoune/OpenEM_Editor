// SPDX-License-Identifier: GPL-3.0-only
#include "editor/palette_doc.hpp"

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace em::editor {

bool PaletteDoc::load(int number) {
    try {
        Bytes d = read_file(kb_ / (std::to_string(number) + ".vga"));
        if (classify_vga(d) != VgaKind::Palette) throw FormatError("keine Palette");
        raw_ = std::move(d);
        current_ = number;
        dirty_ = false;
        revision_++;
        status_ = "KB" + std::to_string(number) + " geladen: 255 Farben";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
}

void PaletteDoc::close() {
    current_ = -1;
    dirty_ = false;
    raw_.clear();
    revision_++;
}

void PaletteDoc::revert() {
    if (current_ >= 0) load(current_);
}

std::array<int, 3> PaletteDoc::get(int i) const {
    if (current_ < 0 || i < 0 || i >= kColors) return {0, 0, 0};
    return {raw_[size_t(i) * 3] & 63, raw_[size_t(i) * 3 + 1] & 63, raw_[size_t(i) * 3 + 2] & 63};
}

void PaletteDoc::set(int i, int r, int g, int b) {
    if (current_ < 0 || i < 0 || i >= kColors) return;
    const int v[3] = {r, g, b};
    for (int c = 0; c < 3; c++) {
        uint8_t& byte = raw_[size_t(i) * 3 + size_t(c)];
        const int nv = std::clamp(v[c], 0, 63);
        if ((byte & 63) == nv) continue;
        byte = uint8_t((byte & 0xC0) | nv);
        dirty_ = true;
        revision_++;
    }
}

Palette PaletteDoc::palette() const {
    if (current_ < 0) return {};
    return decode_palette(raw_);
}

bool PaletteDoc::save() {
    if (current_ < 0) return false;
    try {
        const fs::path p = kb_ / (std::to_string(current_) + ".vga");
        fs::path bak = p;
        bak += ".bak";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(p, bak, ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(raw_.data()), std::streamsize(raw_.size()));
        if (!f) throw std::runtime_error("Schreiben fehlgeschlagen");
        dirty_ = false;
        status_ = "KB" + std::to_string(current_) + " gespeichert (Sicherung als .bak)";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Speichern fehlgeschlagen: ") + e.what();
        return false;
    }
}

}  // namespace em::editor
