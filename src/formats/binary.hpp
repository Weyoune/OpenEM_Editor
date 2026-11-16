// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Little helpers for reading the original little-endian data files of EM.EXE
// (16-bit Borland C++ program: all multi-byte values are little-endian, ints are 16 bit).
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace em {

using Bytes = std::vector<uint8_t>;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Bytes read_file(const std::filesystem::path& path);

inline uint16_t le16(const Bytes& b, size_t o) { return uint16_t(b.at(o) | b.at(o + 1) << 8); }
inline uint32_t le32(const Bytes& b, size_t o) { return uint32_t(le16(b, o)) | uint32_t(le16(b, o + 2)) << 16; }

// The game's texts use ISO 646-DE like the later BMH: [ \ ] { | } ~ are Ä Ö Ü ä ö ü ß
// (seen in EM.EXE strings such as "TORE IN ]BERZAHL", "<TASTE DR]CKEN>"). Some names use the
// CP437 umlauts instead (0x8E 0x99 0x9A 0x84 0x94 0x81 0xE1); both are converted.
std::string iso646de_to_utf8(const char* s, size_t max_len = std::string::npos);
// Inverse: Ä Ö Ü ä ö ü ß -> [ \ ] { | } ~. Throws FormatError for any other non-ASCII character.
std::string utf8_to_iso646de(const std::string& s);

}  // namespace em
