// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the palette editor for KB\21.vga (game palette) and KB\247.vga (goal scene palette).
// 255 colours of three bytes; the VGA DAC uses the low six bits, so the editor edits 0..63 per channel and keeps
// the unused high bits of the file untouched.
#include <array>
#include <filesystem>
#include <string>

#include "formats/vga.hpp"

namespace em::editor {

class PaletteDoc {
public:
    static constexpr int kColors = 255;  // colour 255 is not stored
    explicit PaletteDoc(const std::filesystem::path& data) : kb_(data / "KB") {}

    bool load(int number);  // 21 or 247
    void close();
    bool save();            // writes KB\<n>.vga, first time with a .bak copy
    void revert();          // reload from disk

    int current() const { return current_; }
    bool dirty() const { return dirty_; }
    std::array<int, 3> get(int index) const;  // six-bit R, G, B
    void set(int index, int r, int g, int b);
    Palette palette() const;                  // expanded to eight bits (as the game shows it)
    unsigned revision() const { return revision_; }
    const std::string& status() const { return status_; }

private:
    std::filesystem::path kb_;
    int current_ = -1;
    Bytes raw_;
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;
};

}  // namespace em::editor
