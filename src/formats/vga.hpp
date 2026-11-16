// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// KB\*.vga - images, palettes and a few unrelated files that share the .vga extension.
//
// Image layout (88 of the 114 original files):
//   u16 width, u16 height, u16 length, `length` bytes RLE data
//   [765 bytes palette]   optional (some full screens): 255 RGB triples, loaded as colours 0..254
//                          (loader FUN_199f_098d reads 0x2FD bytes and sets 0xFF colours)
// RLE chunk (decoder FUN_1bbf_45c6; the later BMH kept the scheme and widened `length` to u32):
//   c <  0x80: literal, the next c + 1 bytes are pixels
//   c >  0x80: run, next byte repeated (c & 0x7F) + 1 times
//   c == 0x80: end of chunk
// Images up to 200 rows are one chunk. Taller images are two chunks: rows 0..199, then rows 200..
// (the loader keeps them in two buffers). A chunk may produce fewer pixels than its rows hold;
// the rest stays 0 and decode_vga() reports `complete = false`.
//
// Other files with the extension: 21/247 = raw palettes (765 bytes), 237-241 = Creative Voice
// (VOC) sounds, 248 = Creative Music File (CMF), 23 = name database (db.hpp), 231-236 and
// 242-246 = goal scene tiles, sprites and rink maps (tore.hpp; 246 still open).
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "formats/binary.hpp"

namespace em {

// 8-bit indexed image, row-major
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    bool complete = true;  // false: RLE stream ended before width*height pixels were produced

    Image() = default;
    Image(int w, int h, uint8_t fill = 0) : width(w), height(h), pixels(size_t(w) * h, fill) {}
    uint8_t& at(int x, int y) { return pixels[size_t(y) * width + x]; }
};

// 256 colours, 8 bit per channel
struct Palette {
    std::array<std::array<uint8_t, 3>, 256> rgb{};
};

enum class VgaKind { Image, ImageWithPalette, Palette, Voc, Cmf, Unknown };

VgaKind classify_vga(const Bytes& data);
Image decode_vga(const Bytes& data);

// Palette from a raw 765-byte block (21.vga/247.vga or the tail of a screen image).
// The original passes these bytes unchanged to the six-bit VGA DAC: high bits
// are ignored, including KB21's values above63. Expand low6bits by bit repetition.
// Verified against every pixel of a native DOSBox-X PNG (day_20261103/palette_runtime_proof.json).
Palette decode_palette(const Bytes& data, size_t offset = 0);

// Inverse of decode_vga: header + RLE chunks (+ 765-byte palette when `palette` is given).
// Greedy RLE (runs of >= 3 equal pixels, otherwise literals of up to 128), each chunk closed by 0x80;
// images taller than 200 rows get two chunks (rows 0..199 and the rest). Throws FormatError when the
// packed stream exceeds the u16 length field or the size is out of range. decode_vga(encode_vga(x))
// reproduces every pixel; the bytes need not match the original packer's.
Bytes encode_vga(const Image& image, const Palette* palette = nullptr);
// Greedy RLE of n bytes plus the 0x80 end marker, appended to `out` (shared by the KB writers).
void rle_pack(const uint8_t* p, size_t n, Bytes& out);
// 765 raw palette bytes (six-bit DAC values: 8-bit channel >> 2).
Bytes encode_palette(const Palette& palette);

}  // namespace em
