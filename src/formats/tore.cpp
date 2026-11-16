// SPDX-License-Identifier: GPL-3.0-only
#include "formats/tore.hpp"

#include <cstring>

namespace em {

namespace {
constexpr char kTorTag[] = {'T', 'O', 'R', '!'};
constexpr size_t kTorHeader = 0x54, kTorFrame = 28;
constexpr uint16_t kTorEnd = 0xAAAA;

uint16_t be16(const Bytes& b, size_t o) { return uint16_t(b.at(o) << 8 | b.at(o + 1)); }
}  // namespace

TorMove decode_tor_move(uint8_t m) {
    TorMove r{(m >> 3) & 7, m & 7};
    if (m & 0x40) r.dx = -r.dx;
    if (m & 0x80) r.dy = -r.dy;
    return r;
}

TorScene decode_tor(const Bytes& d) {
    if (d.size() < kTorHeader + 2 || std::memcmp(d.data(), kTorTag, 4) != 0) throw FormatError("TOR: no TOR! script");
    TorScene s;
    s.camera_x = be16(d, 4);
    s.camera_y = be16(d, 6);
    s.puck_x = be16(d, 8);
    s.puck_y = be16(d, 10);
    for (int i = 0; i < kTorObjects; i++) {
        size_t o = 0x0C + size_t(i) * 6;
        s.objects[size_t(i)] = {d[o + 1], be16(d, o + 2), be16(d, o + 4), d[o]};
    }
    size_t p = kTorHeader;
    while (le16(d, p) != kTorEnd) {
        if (p + kTorFrame + 2 > d.size()) throw FormatError("TOR: no end mark");
        TorFrame f;
        f.camera_move = d[p];
        f.puck_move = d[p + 1];
        f.sound = d[p + 3];
        f.extra = d[p + 2];
        for (int i = 0; i < kTorObjects; i++) {
            f.sprite[size_t(i)] = d[p + 4 + size_t(i) * 2];
            f.move[size_t(i)] = d[p + 5 + size_t(i) * 2];
        }
        s.frames.push_back(f);
        p += kTorFrame;
    }
    return s;
}

Bytes encode_tor(const TorScene& s) {
    if (s.frames.empty()) throw FormatError("TOR: a scene needs at least one frame");
    const auto in16 = [](int v) {
        if (v < 0 || v > 0xFFFF) throw FormatError("TOR: coordinate out of range 0..65535");
        return v;
    };
    Bytes out(kTorHeader);
    std::memcpy(out.data(), kTorTag, 4);
    const auto put16 = [&](size_t o, int v) {
        out[o] = uint8_t(in16(v) >> 8);
        out[o + 1] = uint8_t(v);
    };
    put16(4, s.camera_x);
    put16(6, s.camera_y);
    put16(8, s.puck_x);
    put16(10, s.puck_y);
    for (size_t i = 0; i < kTorObjects; i++) {
        const TorObject& o = s.objects[i];
        const size_t p = 0x0C + i * 6;
        if (o.sprite < 0 || o.sprite > 255) throw FormatError("TOR: sprite out of range 0..255");
        out[p] = o.lead;
        out[p + 1] = uint8_t(o.sprite);
        put16(p + 2, o.x);
        put16(p + 4, o.y);
    }
    for (const TorFrame& f : s.frames) {
        out.push_back(f.camera_move);
        out.push_back(f.puck_move);
        out.push_back(f.extra);
        out.push_back(f.sound);
        for (size_t i = 0; i < kTorObjects; i++) {
            out.push_back(f.sprite[i]);
            out.push_back(f.move[i]);
        }
    }
    out.push_back(0xAA);
    out.push_back(0xAA);
    return out;
}

TorState play_tor(const TorScene& s, size_t n) {
    TorState st{s.camera_x, s.camera_y, s.puck_x, s.puck_y, s.objects};
    for (size_t i = 0; i < n && i < s.frames.size(); i++) {
        const TorFrame& f = s.frames[i];
        TorMove c = decode_tor_move(f.camera_move), p = decode_tor_move(f.puck_move);
        st.camera_x += c.dx;
        st.camera_y += c.dy;
        st.puck_x += p.dx;
        st.puck_y += p.dy;
        for (size_t o = 0; o < kTorObjects; o++) {
            st.objects[o].sprite = f.sprite[o];
            if (f.sprite[o] == 0 && f.move[o] == 0) continue;
            TorMove m = decode_tor_move(f.move[o]);
            st.objects[o].x += m.dx;
            st.objects[o].y += m.dy;
        }
    }
    return st;
}

namespace {
int clamp_move(int pos, int d, int max) {
    if (d > 0) return pos + d < max ? pos + d : max;
    if (d < 0) return pos + d > 0 ? pos + d : 0;
    return pos;
}
}  // namespace

TorState tor_state_at(const TorScene& s, int n) {
    TorState v{s.camera_x > 16 ? s.camera_x - 16 : 0, s.camera_y > 32 ? s.camera_y - 32 : 0, s.puck_x, s.puck_y, s.objects};
    for (int i = 0; i < n && size_t(i) < s.frames.size(); i++) {
        const TorFrame& f = s.frames[size_t(i)];
        const TorMove c = decode_tor_move(f.camera_move), p = decode_tor_move(f.puck_move);
        v.camera_x = clamp_move(v.camera_x, c.dx, kTorCameraMaxX);
        v.camera_y = clamp_move(v.camera_y, c.dy, kTorCameraMaxY);
        v.puck_x += p.dx;
        v.puck_y += p.dy;
        for (size_t o = 0; o < kTorObjects; o++) {
            v.objects[o].sprite = f.sprite[o];
            if (f.sprite[o] == 0 && f.move[o] == 0) continue;
            const TorMove m = decode_tor_move(f.move[o]);
            v.objects[o].x += m.dx;
            v.objects[o].y += m.dy;
        }
    }
    return v;
}

TorSpriteRef tor_sprite_ref(int type) {
    using K = TorSpriteRef::Kind;
    if (type == 0) return {K::Dot, 0, 0};
    if (type == 1) return {K::Puck, 0, 0};
    if (type < 0 || type > 0x5D) return {};
    if (type < 0x2A) return {K::Sheet, 0, type - 2};
    if (type < 0x52) return {K::Sheet, 1, type - 0x2A};
    return {K::Sheet, 2, type - 0x52};
}

Bytes unpack_kb_block(const Bytes& d) {
    size_t len = le16(d, 0), end = 2 + len;
    if (end > d.size()) throw FormatError("KB block: length beyond file");
    Bytes out;
    for (size_t i = 2; i < end;) {
        uint8_t c = d[i++];
        if (c == 0x80) break;
        if (c < 0x80) {
            for (int k = 0; k <= c && i < end; k++) out.push_back(d[i++]);
        } else if (i < end) {
            uint8_t v = d[i++];
            out.insert(out.end(), size_t(c & 0x7F) + 1, v);
        }
    }
    return out;
}

Bytes pack_kb_block(const Bytes& raw) {
    Bytes body;
    rle_pack(raw.data(), raw.size(), body);
    if (body.size() > 0xFFFF) throw FormatError("KB block: packed data exceeds 65535 bytes");
    Bytes out{uint8_t(body.size()), uint8_t(body.size() >> 8)};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Bytes encode_tor_sprites(const std::vector<Image>& sprites) {
    Bytes raw;
    for (const Image& s : sprites) {
        if (s.width != kTorSprite || s.height != kTorSprite) throw FormatError("sprite must be 32 x 32");
        raw.insert(raw.end(), s.pixels.begin(), s.pixels.end());
    }
    return pack_kb_block(raw);
}

Bytes encode_tor_panel(const Image& panel) {
    if (panel.width != kTorPanelWidth || panel.height != kTorPanelHeight) throw FormatError("panel must be 320 x 40");
    Bytes out;
    rle_pack(panel.pixels.data(), panel.pixels.size(), out);
    return out;
}

Image decode_rink(const Bytes& tiles_a, const Bytes& tiles_b, const Bytes& map) {
    Bytes tiles = unpack_kb_block(tiles_a), b = unpack_kb_block(tiles_b);
    tiles.insert(tiles.end(), b.begin(), b.end());
    const size_t tile_bytes = kRinkTile * kRinkTile;
    if (map.size() < size_t(kRinkColumns) * kRinkRows * 2) throw FormatError("rink map too short");
    Image img(kRinkColumns * kRinkTile, kRinkRows * kRinkTile);
    // The map runs down each tile column: entry (column, row) is at index column * 72 + row.
    for (int c = 0; c < kRinkColumns; c++)
        for (int r = 0; r < kRinkRows; r++) {
            uint16_t e = be16(map, (size_t(c) * kRinkRows + size_t(r)) * 2);
            size_t t = e & 0x7FFF;
            bool mirror = e & 0x8000;  // mirrored top to bottom in screen orientation
            if ((t + 1) * tile_bytes > tiles.size()) continue;
            for (int y = 0; y < kRinkTile; y++)
                for (int x = 0; x < kRinkTile; x++) {
                    int sy = mirror ? kRinkTile - 1 - y : y;
                    img.at(c * kRinkTile + x, r * kRinkTile + y) = tiles[t * tile_bytes + size_t(sy) * kRinkTile + size_t(x)];
                }
        }
    return img;
}

std::vector<Image> decode_tor_sprites(const Bytes& data) {
    Bytes raw = unpack_kb_block(data);
    const size_t n = raw.size() / (kTorSprite * kTorSprite);
    std::vector<Image> out;
    for (size_t s = 0; s < n; s++) {
        Image img(kTorSprite, kTorSprite);
        std::memcpy(img.pixels.data(), raw.data() + s * kTorSprite * kTorSprite, kTorSprite * kTorSprite);
        out.push_back(std::move(img));
    }
    return out;
}


Image decode_tor_panel(const Bytes& d) {
    Image img(kTorPanelWidth, kTorPanelHeight);
    size_t out = 0, i = 0;
    const size_t total = img.pixels.size();
    while (i < d.size()) {
        uint8_t c = d[i++];
        if (c == 0x80) break;
        if (c < 0x80) {
            for (int k = 0; k <= c && i < d.size(); k++, i++)
                if (out < total) img.pixels[out++] = d[i];
        } else if (i < d.size()) {
            uint8_t v = d[i++];
            for (int k = 0; k <= (c & 0x7F); k++)
                if (out < total) img.pixels[out++] = v;
        }
    }
    img.complete = out == total;
    return img;
}

const std::array<uint8_t, 25> kTorDot = {0x00, 0xC7, 0xC7, 0xC7, 0x00, 0xC7, 0xC6, 0xC6, 0xC6, 0xC7, 0xC6, 0xC6, 0xC6,
                                         0xC6, 0xC6, 0xC7, 0xC6, 0xC6, 0xC6, 0xC7, 0x00, 0xC7, 0xC7, 0xC7, 0x00};
const std::array<uint8_t, 25> kTorPuck = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFB, 0xFB, 0xFB, 0x00, 0xFB, 0xFB, 0xFB,
                                          0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0x00, 0xFB, 0xFB, 0xFB, 0x00};
}  // namespace em
