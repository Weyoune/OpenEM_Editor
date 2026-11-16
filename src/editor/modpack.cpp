// SPDX-License-Identifier: GPL-3.0-only
#include "editor/modpack.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace em::editor {

namespace {

uint32_t crc32(const Bytes& d) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return ~c;
}

void put16(Bytes& b, unsigned v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(Bytes& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// ------------------------------------------------------------------------------------------------ inflate (RFC 1951)

struct Inflater {
    const uint8_t* in;
    size_t size, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    Bytes out;

    int bits(int need) {
        uint32_t v = bitbuf;
        while (bitcnt < need) {
            if (pos >= size) throw FormatError("ZIP: deflate data ends early");
            v |= uint32_t(in[pos++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = v >> need;
        bitcnt -= need;
        return int(v & ((1u << need) - 1));
    }

    struct Huffman {
        std::array<uint16_t, 16> count{};
        std::array<uint16_t, 288> symbol{};
    };

    static void build(Huffman& h, const uint8_t* lengths, int n) {
        h.count.fill(0);
        for (int i = 0; i < n; i++) h.count[lengths[i]]++;
        std::array<uint16_t, 16> offs{};
        for (int len = 1; len < 15; len++) offs[size_t(len) + 1] = uint16_t(offs[size_t(len)] + h.count[size_t(len)]);
        for (int i = 0; i < n; i++)
            if (lengths[i]) h.symbol[offs[lengths[i]]++] = uint16_t(i);
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= bits(1);
            const int count = h.count[size_t(len)];
            if (code - count < first) return h.symbol[size_t(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw FormatError("ZIP: bad deflate code");
    }

    void codes(const Huffman& lit, const Huffman& dist) {
        static const uint16_t lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint16_t lext[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint16_t dext[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lit);
            if (sym < 256) {
                out.push_back(uint8_t(sym));
            } else if (sym == 256) {
                return;
            } else {
                sym -= 257;
                if (sym >= 29) throw FormatError("ZIP: bad deflate length");
                const int len = lbase[sym] + bits(lext[sym]);
                const int ds = decode(dist);
                if (ds >= 30) throw FormatError("ZIP: bad deflate distance");
                const size_t d = size_t(dbase[ds] + bits(dext[ds]));
                if (d > out.size()) throw FormatError("ZIP: deflate distance too far back");
                for (int i = 0; i < len; i++) out.push_back(out[out.size() - d]);
            }
        }
    }

    Bytes run() {
        int last;
        do {
            last = bits(1);
            const int type = bits(2);
            if (type == 0) {
                bitbuf = 0;
                bitcnt = 0;
                if (pos + 4 > size) throw FormatError("ZIP: stored block ends early");
                const size_t len = in[pos] | in[pos + 1] << 8;
                pos += 4;
                if (pos + len > size) throw FormatError("ZIP: stored block ends early");
                out.insert(out.end(), in + pos, in + pos + len);
                pos += len;
            } else if (type == 1) {
                uint8_t l[288];
                for (int i = 0; i < 144; i++) l[i] = 8;
                for (int i = 144; i < 256; i++) l[i] = 9;
                for (int i = 256; i < 280; i++) l[i] = 7;
                for (int i = 280; i < 288; i++) l[i] = 8;
                Huffman lit, dist;
                build(lit, l, 288);
                uint8_t d[30];
                std::fill(d, d + 30, uint8_t(5));
                build(dist, d, 30);
                codes(lit, dist);
            } else if (type == 2) {
                static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
                if (nlen > 286 || ndist > 30) throw FormatError("ZIP: bad deflate header");
                uint8_t lengths[320] = {};
                for (int i = 0; i < ncode; i++) lengths[order[i]] = uint8_t(bits(3));
                Huffman lencode;
                build(lencode, lengths, 19);
                int idx = 0;
                uint8_t all[320] = {};
                while (idx < nlen + ndist) {
                    int sym = decode(lencode);
                    if (sym < 16) {
                        all[idx++] = uint8_t(sym);
                    } else {
                        int len = 0, rep;
                        if (sym == 16) {
                            if (!idx) throw FormatError("ZIP: bad deflate repeat");
                            len = all[idx - 1];
                            rep = 3 + bits(2);
                        } else if (sym == 17) {
                            rep = 3 + bits(3);
                        } else {
                            rep = 11 + bits(7);
                        }
                        if (idx + rep > nlen + ndist) throw FormatError("ZIP: bad deflate repeat");
                        while (rep--) all[idx++] = uint8_t(len);
                    }
                }
                Huffman lit, dist;
                build(lit, all, nlen);
                build(dist, all + nlen, ndist);
                codes(lit, dist);
            } else {
                throw FormatError("ZIP: bad deflate block");
            }
        } while (!last);
        return out;
    }
};

std::string slash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

}  // namespace

bool allowed_mod_path(const std::string& p) {
    const auto name_ok = [](const std::string& n) {
        return !n.empty() && n.size() <= 16 && std::all_of(n.begin(), n.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.'; }) && n.find("..") == std::string::npos;
    };
    if (p.rfind("KB/", 0) == 0) {
        const std::string n = p.substr(3);
        return n.size() > 4 && n.substr(n.size() - 4) == ".vga" && std::all_of(n.begin(), n.end() - 4, [](char c) { return c >= '0' && c <= '9'; });
    }
    if (p.rfind("TORE/", 0) == 0) return name_ok(p.substr(5));
    return false;
}

std::vector<ChangedFile> changed_files(const fs::path& data) {
    std::vector<ChangedFile> out;
    for (const char* dir : {"KB", "TORE"}) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(data / dir, ec)) {
            if (e.path().extension() != ".bak") continue;
            fs::path file = e.path();
            file.replace_extension();
            if (!fs::exists(file, ec)) continue;
            try {
                const Bytes a = read_file(file), b = read_file(e.path());
                const std::string rel = std::string(dir) + "/" + file.filename().string();
                if (a != b && allowed_mod_path(rel)) out.push_back({rel, a.size()});
            } catch (const std::exception&) {
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const ChangedFile& a, const ChangedFile& b) { return a.path < b.path; });
    return out;
}

bool restore_file(const fs::path& data, const std::string& rel) {
    if (!allowed_mod_path(rel)) return false;
    const fs::path file = data / rel;
    fs::path bak = file;
    bak += ".bak";
    std::error_code ec;
    if (!fs::exists(bak, ec)) return false;
    fs::copy_file(bak, file, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

Bytes make_zip(const ZipEntries& entries) {
    Bytes out, central;
    for (const auto& [name, data] : entries) {
        const uint32_t crc = crc32(data), offset = uint32_t(out.size());
        out.insert(out.end(), {'P', 'K', 3, 4});
        put16(out, 20);
        put16(out, 0x0800);  // UTF-8 names
        put16(out, 0);       // stored
        put16(out, 0);
        put16(out, 0x21);  // 1980-01-01
        put32(out, crc);
        put32(out, uint32_t(data.size()));
        put32(out, uint32_t(data.size()));
        put16(out, unsigned(name.size()));
        put16(out, 0);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), data.begin(), data.end());
        central.insert(central.end(), {'P', 'K', 1, 2});
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, crc);
        put32(central, uint32_t(data.size()));
        put32(central, uint32_t(data.size()));
        put16(central, unsigned(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central.insert(central.end(), name.begin(), name.end());
    }
    const uint32_t start = uint32_t(out.size());
    out.insert(out.end(), central.begin(), central.end());
    out.insert(out.end(), {'P', 'K', 5, 6});
    put16(out, 0);
    put16(out, 0);
    put16(out, unsigned(entries.size()));
    put16(out, unsigned(entries.size()));
    put32(out, uint32_t(central.size()));
    put32(out, start);
    put16(out, 0);
    return out;
}

ZipEntries read_zip(const Bytes& z) {
    // end of central directory
    if (z.size() < 22) throw FormatError("ZIP: file too short");
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i <= 22 + 0xFFFF;)
        if (z[i] == 'P' && z[i + 1] == 'K' && z[i + 2] == 5 && z[i + 3] == 6) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) throw FormatError("ZIP: no directory found");
    const size_t count = le16(z, eocd + 10);
    size_t p = le32(z, eocd + 16);
    ZipEntries out;
    for (size_t n = 0; n < count; n++) {
        if (p + 46 > z.size() || z[p] != 'P' || z[p + 1] != 'K' || z[p + 2] != 1 || z[p + 3] != 2) throw FormatError("ZIP: damaged directory");
        const unsigned method = le16(z, p + 10);
        const uint32_t crc = le32(z, p + 16);
        const size_t csize = le32(z, p + 20), usize = le32(z, p + 24), nlen = le16(z, p + 28), xlen = le16(z, p + 30), clen = le16(z, p + 32), local = le32(z, p + 42);
        if (p + 46 + nlen > z.size()) throw FormatError("ZIP: damaged directory");
        std::string name(z.begin() + long(p + 46), z.begin() + long(p + 46 + nlen));
        p += 46 + nlen + xlen + clen;
        if (!name.empty() && name.back() == '/') continue;  // folder
        if (local + 30 > z.size()) throw FormatError("ZIP: damaged entry");
        const size_t data = local + 30 + le16(z, local + 26) + le16(z, local + 28);
        if (data + csize > z.size()) throw FormatError("ZIP: entry runs past the end");
        Bytes content;
        if (method == 0) {
            content.assign(z.begin() + long(data), z.begin() + long(data + csize));
        } else if (method == 8) {
            Inflater inf{z.data() + data, csize};
            content = inf.run();
        } else {
            throw FormatError("ZIP: unsupported compression method " + std::to_string(method));
        }
        if (content.size() != usize || crc32(content) != crc) throw FormatError("ZIP: checksum error in " + name);
        out.emplace_back(slash(name), std::move(content));
    }
    return out;
}

size_t create_modpack(const fs::path& data, const fs::path& zip, std::string* error) {
    const auto files = changed_files(data);
    if (files.empty()) {
        if (error) *error = "Keine geänderten Dateien (es gibt keine .bak, die sich vom Original unterscheidet)";
        return 0;
    }
    ZipEntries entries;
    for (const auto& f : files) entries.emplace_back(f.path, read_file(data / f.path));
    const Bytes z = make_zip(entries);
    std::ofstream o(zip, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(z.data()), std::streamsize(z.size()));
    if (!o) {
        if (error) *error = "Schreiben fehlgeschlagen";
        return 0;
    }
    return files.size();
}

int apply_modpack(const fs::path& data, const fs::path& zip, std::string* error) {
    try {
        const ZipEntries entries = read_zip(read_file(zip));
        for (const auto& [name, content] : entries)
            if (!allowed_mod_path(name)) throw FormatError("Paket enthält eine unerlaubte Datei: " + name);
        int n = 0;
        for (const auto& [name, content] : entries) {
            const fs::path file = data / name;
            fs::path bak = file;
            bak += ".bak";
            std::error_code ec;
            if (fs::exists(file, ec) && !fs::exists(bak, ec)) fs::copy_file(file, bak, ec);
            std::ofstream o(file, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(content.data()), std::streamsize(content.size()));
            if (!o) throw FormatError("Schreiben fehlgeschlagen: " + name);
            n++;
        }
        return n;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return -1;
    }
}

}  // namespace em::editor
