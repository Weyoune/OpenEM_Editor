// SPDX-License-Identifier: GPL-3.0-only
#include "formats/vga.hpp"

#include <algorithm>
#include <cstring>

namespace em {

namespace {
constexpr size_t kPaletteBytes = 765;  // 255 RGB triples
constexpr size_t kHeader = 6;          // width, height, length (u16 each)
}  // namespace

VgaKind classify_vga(const Bytes& d) {
    if (d.size() >= 20 && std::memcmp(d.data(), "Creative Voice File", 19) == 0) return VgaKind::Voc;
    if (d.size() >= 4 && std::memcmp(d.data(), "CTMF", 4) == 0) return VgaKind::Cmf;
    if (d.size() == kPaletteBytes) return VgaKind::Palette;
    if (d.size() >= kHeader) {
        size_t len = le16(d, 4);
        if (le16(d, 0) > 0 && le16(d, 2) > 0) {
            if (kHeader + len == d.size()) return VgaKind::Image;
            if (kHeader + len + kPaletteBytes == d.size()) return VgaKind::ImageWithPalette;
        }
    }
    return VgaKind::Unknown;
}

Image decode_vga(const Bytes& d) {
    VgaKind kind = classify_vga(d);
    if (kind != VgaKind::Image && kind != VgaKind::ImageWithPalette)
        throw FormatError("VGA: not an image file");
    Image img(le16(d, 0), le16(d, 2));
    const size_t end = kHeader + le16(d, 4);
    size_t si = kHeader;

    // Decodes one chunk into rows [first_row, last_row) and stops at the 0x80 end marker
    // (FUN_1bbf_45c6, the memory-target path of the original decoder). Returns the number of
    // pixels written. The original does not check the destination size; we clip instead.
    auto chunk = [&](int first_row, int last_row) {
        size_t di = size_t(first_row) * img.width, n = size_t(last_row) * img.width, start = di;
        while (si < end) {
            uint8_t c = d[si++];
            if (c & 0x80) {
                uint8_t count = c & 0x7F;
                if (count == 0) break;  // 0x80: end of chunk
                if (si >= end) break;
                uint8_t v = d[si++];
                for (int k = 0; k <= count; k++)  // run of count + 1 copies
                    if (di < n) img.pixels[di++] = v;
            } else {
                for (int k = 0; k <= c && si < end; k++, si++)  // literal of c + 1 bytes
                    if (di < n) img.pixels[di++] = d[si];
            }
        }
        return di - start;
    };

    // The loader (FUN_199f_098d) keeps images taller than 200 rows in two buffers: rows 0..199
    // and the rest, each filled by one decoder call. The stream therefore holds one chunk per
    // buffer; the second chunk always starts at row 200, however much the first one produced.
    size_t produced;
    if (img.height > 200) {
        produced = chunk(0, 200);
        produced += chunk(200, img.height);
    } else {
        produced = chunk(0, img.height);
    }
    img.complete = produced == img.pixels.size();
    return img;
}

Palette decode_palette(const Bytes& d, size_t offset) {
    if (offset + kPaletteBytes > d.size()) throw FormatError("palette: block too short");
    Palette p;
    for (size_t i = 0; i < kPaletteBytes / 3; i++)
        for (size_t c = 0; c < 3; c++) {
            const uint8_t v = d[offset + i * 3 + c] & 63;
            p.rgb[i][c] = uint8_t(v << 2 | v >> 4);
        }
    return p;
}

Bytes encode_palette(const Palette& p) {
    Bytes out;
    out.reserve(kPaletteBytes);
    for (size_t i = 0; i < kPaletteBytes / 3; i++)
        for (size_t c = 0; c < 3; c++) out.push_back(uint8_t(p.rgb[i][c] >> 2));
    return out;
}

void rle_pack(const uint8_t* p, size_t n, Bytes& body) {
    // see vga.hpp
    size_t i = 0;
    while (i < n) {
            size_t run = 1;
            while (i + run < n && run < 128 && p[i + run] == p[i]) run++;
            if (run >= 3) {
                body.push_back(uint8_t(0x80 | (run - 1)));
                body.push_back(p[i]);
                i += run;
                continue;
            }
            size_t j = i;  // literal: until a run of >= 3 starts or 128 bytes
            while (j < n && j - i < 128) {
                if (j + 2 < n && p[j] == p[j + 1] && p[j] == p[j + 2]) break;
                j++;
            }
            if (j == i) j = i + 1;
            body.push_back(uint8_t(j - i - 1));
            body.insert(body.end(), p + i, p + j);
            i = j;
    }
    body.push_back(0x80);
}

Bytes encode_vga(const Image& img, const Palette* palette) {
    if (img.width <= 0 || img.height <= 0 || img.width > 0xFFFF || img.height > 0xFFFF ||
        img.pixels.size() != size_t(img.width) * img.height)
        throw FormatError("VGA: invalid image size");
    Bytes body;
    auto chunk = [&](int first_row, int last_row) {
        rle_pack(img.pixels.data() + size_t(first_row) * img.width, size_t(last_row - first_row) * img.width, body);
    };
    if (img.height > 200) {
        chunk(0, 200);
        chunk(200, img.height);
    } else {
        chunk(0, img.height);
    }
    if (body.size() > 0xFFFF) throw FormatError("VGA: packed image exceeds 65535 bytes");
    Bytes out{uint8_t(img.width), uint8_t(img.width >> 8), uint8_t(img.height), uint8_t(img.height >> 8),
              uint8_t(body.size()), uint8_t(body.size() >> 8)};
    out.insert(out.end(), body.begin(), body.end());
    if (palette) {
        Bytes pal = encode_palette(*palette);
        out.insert(out.end(), pal.begin(), pal.end());
    }
    return out;
}

}  // namespace em
