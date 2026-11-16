// SPDX-License-Identifier: GPL-3.0-only
#include "editor/scene_doc.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace em::editor {

namespace {

bool same_frame(const TorFrame& a, const TorFrame& b) {
    return a.camera_move == b.camera_move && a.puck_move == b.puck_move && a.sound == b.sound && a.extra == b.extra &&
           a.sprite == b.sprite && a.move == b.move;
}

bool same_scene(const TorScene& a, const TorScene& b) {
    if (a.camera_x != b.camera_x || a.camera_y != b.camera_y || a.puck_x != b.puck_x || a.puck_y != b.puck_y ||
        a.frames.size() != b.frames.size())
        return false;
    for (size_t i = 0; i < a.objects.size(); i++) {
        const TorObject &p = a.objects[i], &q = b.objects[i];
        if (p.sprite != q.sprite || p.x != q.x || p.y != q.y || p.lead != q.lead) return false;
    }
    for (size_t i = 0; i < a.frames.size(); i++)
        if (!same_frame(a.frames[i], b.frames[i])) return false;
    return true;
}

// <n>.<h|a><t|v>[e|j]
bool parse_name(const std::string& s, int& n, char& side, char& kind, char& variant) {
    size_t i = 0;
    n = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') n = n * 10 + (s[i++] - '0');
    if (i == 0 || i + 3 > s.size() || s[i] != '.') return false;
    side = s[i + 1];
    kind = s[i + 2];
    variant = i + 3 < s.size() ? s[i + 3] : 0;
    if ((side != 'h' && side != 'a') || (kind != 't' && kind != 'v')) return false;
    if (variant != 0 && variant != 'e' && variant != 'j') return false;
    return i + 3 + (variant ? 1 : 0) == s.size();
}

int clamp_step(int v) { return std::clamp(v, -7, 7); }

}  // namespace

int SceneDoc::encode_move(int dx, int dy) {
    int m = (std::abs(dx) << 3) | std::abs(dy);
    if (dx < 0) m |= 0x40;
    if (dy < 0) m |= 0x80;
    return m;
}

SceneDoc::SceneDoc(const fs::path& data) : data_(data) {
    const fs::path kb = data / "KB";
    tiles_a_ = read_file(kb / "231.vga");
    tiles_b_ = read_file(kb / "232.vga");
    for (int i = 0; i < 3; i++) sprites_[size_t(i)] = decode_tor_sprites(read_file(kb / (std::to_string(233 + i) + ".vga")));
    palette_ = decode_palette(read_file(kb / "247.vga"));
    set_shirt(0, 0xE0, 0x30, 0x30);
    set_shirt(1, 0xF0, 0xF0, 0xF0);
}

void SceneDoc::set_shirt(int side, uint8_t r, uint8_t g, uint8_t b) {
    palette_.rgb[size_t(254 - (side ? 1 : 0))] = {r, g, b};
    revision_++;
}

std::vector<std::string> SceneDoc::scene_names(const fs::path& data) {
    struct Key {
        int side, kind, variant, n;
        std::string name;
    };
    std::vector<Key> keys;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(data / "TORE", ec)) {
        const std::string name = e.path().filename().string();
        int n;
        char side, kind, variant;
        if (!parse_name(name, n, side, kind, variant)) continue;
        keys.push_back({side == 'h' ? 0 : 1, kind == 't' ? 0 : 1, variant == 0 ? 0 : variant == 'e' ? 1 : 2, n, name});
    }
    std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
        if (a.side != b.side) return a.side < b.side;
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.variant != b.variant) return a.variant < b.variant;
        return a.n < b.n;
    });
    std::vector<std::string> out;
    for (const Key& k : keys) out.push_back(k.name);
    return out;
}

std::wstring SceneDoc::describe(const std::string& name) {
    int n;
    char side, kind, variant;
    if (!parse_name(name, n, side, kind, variant)) return std::wstring(name.begin(), name.end());
    std::wstring s = std::to_wstring(n) + L"  " + (side == 'h' ? L"Heim greift an" : L"Gast greift an") + L" · " + (kind == 't' ? L"Tor" : L"vergebene Chance");
    if (variant == 'e') s += L" · Strafschuss";
    if (variant == 'j') s += L" · mit sechstem Feldspieler";
    return s;
}

bool SceneDoc::set_arena(int a) {
    if (a < 0 || a > 4) return false;
    try {
        if (!rink_loaded_[size_t(a)]) {
            rinks_[size_t(a)] = decode_rink(tiles_a_, tiles_b_, read_file(data_ / "KB" / (std::to_string(a ? 241 + a : 236) + ".vga")));
            rink_loaded_[size_t(a)] = true;
        }
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
    arena_ = a;
    revision_++;
    return true;
}

bool SceneDoc::load(const std::string& name) {
    try {
        TorScene s = decode_tor(read_file(data_ / "TORE" / name));
        if (!set_arena(arena_)) return false;
        scene_ = std::move(s);
        name_ = name;
        dirty_ = false;
        in_stroke_ = false;
        undo_.clear();
        redo_.clear();
        revision_++;
        status_ = "TORE\\" + name + " geladen: " + std::to_string(scene_.frames.size()) + " Bilder";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
}

void SceneDoc::close() {
    name_.clear();
    scene_ = TorScene();
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    revision_++;
}

bool SceneDoc::save() {
    if (name_.empty()) return false;
    try {
        const Bytes d = encode_tor(scene_);
        const fs::path p = data_ / "TORE" / name_;
        fs::path bak = p;
        bak += ".bak";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(p, bak, ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
        if (!f) throw std::runtime_error("Schreiben fehlgeschlagen");
        dirty_ = false;
        status_ = "TORE\\" + name_ + " gespeichert (Sicherung als .bak)";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Speichern fehlgeschlagen: ") + e.what();
        return false;
    }
}

// ------------------------------------------------------------------------------------------------ queries

const uint8_t* SceneDoc::type_pixels(int type, int& size) const {
    const TorSpriteRef ref = tor_sprite_ref(type);
    size = kTorSprite;
    switch (ref.kind) {
        case TorSpriteRef::Kind::Dot: size = 5; return kTorDot.data();
        case TorSpriteRef::Kind::Puck: size = 5; return kTorPuck.data();
        case TorSpriteRef::Kind::Sheet: {
            const auto& sheet = sprites_[size_t(ref.sheet)];
            return size_t(ref.index) < sheet.size() ? sheet[size_t(ref.index)].pixels.data() : nullptr;
        }
        default: return nullptr;
    }
}

ScenePoint SceneDoc::position(int n, int subject) const {
    const TorState st = play_tor(scene_, size_t(std::clamp(n, 0, frames())));
    if (subject == kPuck) return {st.puck_x, st.puck_y};
    if (subject == kCamera) return {st.camera_x, st.camera_y};
    const TorObject& o = st.objects[size_t(std::clamp(subject, 0, kObjects - 1))];
    return {o.x, o.y};
}

ScenePoint SceneDoc::camera_view(int n) const {
    const TorState st = tor_state_at(scene_, std::clamp(n, 0, frames()));
    return {st.camera_x, st.camera_y};
}

const uint8_t& SceneDoc::move_byte(const TorFrame& f, int subject) const {
    return subject == kPuck ? f.puck_move : subject == kCamera ? f.camera_move : f.move[size_t(subject)];
}

uint8_t& SceneDoc::move_byte(TorFrame& f, int subject) {
    return subject == kPuck ? f.puck_move : subject == kCamera ? f.camera_move : f.move[size_t(subject)];
}

std::vector<ScenePoint> SceneDoc::trajectory(int subject) const {
    std::vector<ScenePoint> out;
    ScenePoint p = position(0, subject);
    out.push_back(p);
    for (const TorFrame& f : scene_.frames) {
        const TorMove m = decode_tor_move(move_byte(f, subject));
        p.x += m.dx;
        p.y += m.dy;
        out.push_back(p);
    }
    return out;
}

int SceneDoc::sprite(int n, int object) const {
    if (object < 0 || object >= kObjects) return 0;
    if (n <= 0) return scene_.objects[size_t(object)].sprite;
    return scene_.frames[size_t(std::min(n, frames()) - 1)].sprite[size_t(object)];
}

int SceneDoc::sound(int n) const { return n <= 0 ? 0 : scene_.frames[size_t(std::min(n, frames()) - 1)].sound; }

bool SceneDoc::box(int n, int subject, int& x, int& y, int& w, int& h) const {
    const int type = subject == kPuck ? 1 : sprite(n, subject);
    const ScenePoint p = position(n, subject);
    int size = 0;
    if (subject >= kCamera || (p.x == 0 && p.y == 0) || !type_pixels(type, size)) return false;
    x = p.x - kTorAnchorX;
    y = p.y - kTorAnchorY;
    w = h = size;
    return true;
}

int SceneDoc::hit_test(int n, int rx, int ry) const {
    const TorState st = play_tor(scene_, size_t(std::clamp(n, 0, frames())));
    const auto hit = [&](int type, int px, int py) {
        int size = 0;
        if ((px == 0 && py == 0) || !type_pixels(type, size)) return false;
        const int slack = size < 8 ? 4 : 0;  // the 5 x 5 dots are hard to hit
        const int x = px - kTorAnchorX, y = py - kTorAnchorY;
        return rx >= x - slack && ry >= y - slack && rx < x + size + slack && ry < y + size + slack;
    };
    for (int o = kObjects - 1; o >= 0; o--)
        if (hit(st.objects[size_t(o)].sprite, st.objects[size_t(o)].x, st.objects[size_t(o)].y)) return o;
    return hit(1, st.puck_x, st.puck_y) ? kPuck : -1;
}

Image SceneDoc::render(int n, int x0, int y0, int w, int h) const {
    Image out(w, h);
    if (!loaded()) return out;
    const Image& r = rink();
    for (int y = 0; y < h; y++) {
        const int sy = y0 + y;
        if (sy < 0 || sy >= r.height) continue;
        for (int x = 0; x < w; x++) {
            const int sx = x0 + x;
            if (sx >= 0 && sx < r.width) out.pixels[size_t(y) * size_t(w) + size_t(x)] = r.pixels[size_t(sy) * size_t(r.width) + size_t(sx)];
        }
    }
    const TorState st = play_tor(scene_, size_t(std::clamp(n, 0, frames())));
    const auto draw = [&](int type, int px, int py) {  // FUN_1bbf_4ecf: an object at (0, 0) is skipped
        int size = 0;
        const uint8_t* pixels = type_pixels(type, size);
        if ((px == 0 && py == 0) || !pixels) return;
        const int left = px - kTorAnchorX - x0, top = py - kTorAnchorY - y0;
        for (int row = 0; row < size; row++)
            for (int col = 0; col < size; col++) {
                const uint8_t c = pixels[row * size + col];
                const int sx = left + col, sy = top + row;
                if (c && sx >= 0 && sx < w && sy >= 0 && sy < h) out.pixels[size_t(sy) * size_t(w) + size_t(sx)] = c;
            }
    };
    draw(1, st.puck_x, st.puck_y);  // the puck first, then the 12 objects
    for (const TorObject& o : st.objects) draw(o.sprite, o.x, o.y);
    return out;
}

// ------------------------------------------------------------------------------------------------ editing

void SceneDoc::snapshot() {
    undo_.push_back(scene_);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}

void SceneDoc::changed() {
    dirty_ = true;
    revision_++;
}

void SceneDoc::stroke_begin() {
    if (!loaded()) return;
    snapshot();
    in_stroke_ = true;
    stroke_changed_ = false;
}

void SceneDoc::stroke_end() {
    if (!in_stroke_) return;
    in_stroke_ = false;
    if (!stroke_changed_) undo_.pop_back();
}

void SceneDoc::undo() {
    if (undo_.empty()) return;
    redo_.push_back(std::move(scene_));
    scene_ = std::move(undo_.back());
    undo_.pop_back();
    changed();
}

void SceneDoc::redo() {
    if (redo_.empty()) return;
    undo_.push_back(std::move(scene_));
    scene_ = std::move(redo_.back());
    redo_.pop_back();
    changed();
}

#define SCENE_EDIT(...)                       \
    do {                                         \
        if (!loaded()) return;                   \
        const TorScene before_ = scene_;         \
        __VA_ARGS__;\
        if (same_scene(before_, scene_)) return; \
        if (in_stroke_) stroke_changed_ = true;  \
        else {                                   \
            undo_.push_back(before_);            \
            if (undo_.size() > 200) undo_.erase(undo_.begin()); \
            redo_.clear();                       \
        }                                        \
        changed();                               \
    } while (0)

void SceneDoc::move_to(int n, int subject, int x, int y, bool keep_later) {
    if (subject < 0 || subject >= kSubjects || n < 0 || n > frames()) return;
    SCENE_EDIT({
        x = std::clamp(x, 0, 0xFFFF);
        y = std::clamp(y, 0, 0xFFFF);
        if (n == 0) {
            if (subject == kPuck) { scene_.puck_x = x; scene_.puck_y = y; }
            else if (subject == kCamera) { scene_.camera_x = x; scene_.camera_y = y; }
            else { scene_.objects[size_t(subject)].x = x; scene_.objects[size_t(subject)].y = y; }
        } else {
            const ScenePoint prev = position(n - 1, subject);
            const ScenePoint old_next = n < frames() ? position(n + 1, subject) : ScenePoint{};
            const int dx = clamp_step(x - prev.x), dy = clamp_step(y - prev.y);
            move_byte(scene_.frames[size_t(n - 1)], subject) = uint8_t(encode_move(dx, dy));
            if (keep_later && n < frames()) {
                const int ndx = old_next.x - (prev.x + dx), ndy = old_next.y - (prev.y + dy);
                if (std::abs(ndx) <= 7 && std::abs(ndy) <= 7) move_byte(scene_.frames[size_t(n)], subject) = uint8_t(encode_move(ndx, ndy));
            }
        }
    });
}

void SceneDoc::set_sprite(int n, int object, int sprite, bool to_end) {
    if (object < 0 || object >= kObjects || n < 0 || n > frames() || sprite < 0 || sprite > 255) return;
    SCENE_EDIT({
        if (n == 0) scene_.objects[size_t(object)].sprite = sprite;
        else scene_.frames[size_t(n - 1)].sprite[size_t(object)] = uint8_t(sprite);
        if (to_end)
            for (int i = n; i < frames(); i++) scene_.frames[size_t(i)].sprite[size_t(object)] = uint8_t(sprite);
    });
}

void SceneDoc::set_sound(int n, int sample) {
    if (n < 1 || n > frames() || sample < 0 || sample > 4) return;
    SCENE_EDIT({ scene_.frames[size_t(n - 1)].sound = uint8_t(sample); });
}

void SceneDoc::insert_frame(int n) {
    if (n < 0 || n > frames()) return;
    SCENE_EDIT({
        TorFrame f;
        for (int o = 0; o < kObjects; o++) f.sprite[size_t(o)] = uint8_t(sprite(n, o));
        scene_.frames.insert(scene_.frames.begin() + n, f);
    });
}

void SceneDoc::duplicate_frame(int n) {
    if (n < 1 || n > frames()) return;
    SCENE_EDIT({
        TorFrame f = scene_.frames[size_t(n - 1)];
        f.sound = 0;
        scene_.frames.insert(scene_.frames.begin() + n, f);
    });
}

void SceneDoc::delete_frame(int n) {
    if (n < 1 || n > frames() || frames() <= 1) return;
    SCENE_EDIT({ scene_.frames.erase(scene_.frames.begin() + (n - 1)); });
}

}  // namespace em::editor
