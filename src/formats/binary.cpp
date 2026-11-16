// SPDX-License-Identifier: GPL-3.0-only
#include "formats/binary.hpp"

#include <fstream>
#include <iterator>

namespace em {

Bytes read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FormatError("cannot open " + path.string());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

std::string iso646de_to_utf8(const char* s, size_t max_len) {
    std::string out;
    for (size_t i = 0; i < max_len && s[i] != '\0'; i++) {
        switch (s[i]) {
            case '[': out += "Ä"; break;
            case '\\': out += "Ö"; break;
            case ']': out += "Ü"; break;
            case '{': out += "ä"; break;
            case '|': out += "ö"; break;
            case '}': out += "ü"; break;
            case '~': out += "ß"; break;
            // CP437 umlauts, used next to the ISO 646 forms (e.g. "K\x99LNER EC" in KB\23.vga and
            // in savegames); EM.EXE keeps the same set as a table at DGROUP 0x4A46.
            case '\x8e': out += "Ä"; break;
            case '\x99': out += "Ö"; break;
            case '\x9a': out += "Ü"; break;
            case '\x84': out += "ä"; break;
            case '\x94': out += "ö"; break;
            case '\x81': out += "ü"; break;
            case '\xe1': out += "ß"; break;
            default: out += s[i];
        }
    }
    return out;
}

std::string utf8_to_iso646de(const std::string& s) {
    static const std::pair<const char*, char> map[] = {{"Ä", '['}, {"Ö", '\\'}, {"Ü", ']'}, {"ä", '{'},
                                                       {"ö", '|'}, {"ü", '}'}, {"ß", '~'}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (uint8_t(s[i]) < 0x80) { out += s[i++]; continue; }
        bool hit = false;
        for (auto& [u, c] : map) {
            size_t n = std::char_traits<char>::length(u);
            if (s.compare(i, n, u) == 0) { out += c; i += n; hit = true; break; }
        }
        if (!hit) throw FormatError("text: character not available in the game's character set");
    }
    return out;
}

}  // namespace em
