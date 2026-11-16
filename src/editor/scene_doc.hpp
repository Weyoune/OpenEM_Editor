// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the goal scene editor: the scripts TORE\<n>.<h|a><t|v>[e|j] (see src/formats/tore.hpp). A scene is a
// start layout plus one frame per step; objects, puck and camera move relative to the previous frame (at most 7 pixels
// per axis). The editor addresses every state by a position index n: 0 = the start layout, n >= 1 = after frame n - 1.
// No user interface in here (the front end is win_main.cpp).
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/tore.hpp"
#include "formats/vga.hpp"

namespace em::editor {

struct ScenePoint {
    int x = 0, y = 0;
};

class SceneDoc {
public:
    // Subjects that can be moved: the 12 objects (0..11), the puck and the camera position of the script
    static constexpr int kObjects = kTorObjects, kPuck = kTorObjects, kCamera = kTorObjects + 1, kSubjects = kTorObjects + 2;
    static constexpr int kMaxSprite = 0x5D;  // object types above draw nothing (tor_sprite_ref)

    explicit SceneDoc(const std::filesystem::path& data);

    // Script file names in TORE\ (without "anzahl"), ordered: home before away, goal before chance, plain before e / j, by number
    static std::vector<std::string> scene_names(const std::filesystem::path& data);
    // "2  Heim greift an · Tor" and so on (the side that attacks, goal or missed chance, variant)
    static std::wstring describe(const std::string& name);

    bool load(const std::string& name);
    void close();
    bool save();  // writes TORE\<name>, first time with a .bak copy
    const std::string& name() const { return name_; }
    bool loaded() const { return !name_.empty(); }
    bool dirty() const { return dirty_; }
    unsigned revision() const { return revision_; }
    const std::string& status() const { return status_; }
    void set_status(const std::string& s) { status_ = s; }

    const TorScene& scene() const { return scene_; }
    int frames() const { return int(scene_.frames.size()); }  // position indexes are 0..frames()

    // Where the scene is shown: rink map 0 = KB236, 1..4 = KB242..245
    int arena() const { return arena_; }
    bool set_arena(int a);
    void set_shirt(int side, uint8_t r, uint8_t g, uint8_t b);  // colours 254 (home) / 253 (away) of the rink palette
    const Palette& palette() const { return palette_; }
    const Image& rink() const { return rinks_[size_t(arena_)]; }
    int sprite_count(int sheet) const { return int(sprites_[size_t(sheet)].size()); }
    const Image& sprite_image(int sheet, int index) const { return sprites_[size_t(sheet)][size_t(index)]; }
    // The picture of an object type: nullptr for types that draw nothing; the dots are 5 x 5, the sprites 32 x 32
    const uint8_t* type_pixels(int type, int& size) const;

    // Positions
    ScenePoint position(int n, int subject) const;  // the camera is the script position (the game clamps it while playing)
    ScenePoint camera_view(int n) const;            // top left of the 320 x 200 window while playing (tor_state_at)
    std::vector<ScenePoint> trajectory(int subject) const;  // positions 0..frames()
    int sprite(int n, int object) const;
    int sound(int n) const;  // frame n - 1; 0 for the start

    // Editing; every call is an undo step (strokes can group several with stroke_begin / stroke_end).
    // move_to: n = 0 sets the start (the whole path follows); n >= 1 changes the step into that position (limited to 7 pixels
    // per axis from the previous one) and, with keep_later, adjusts the next step so the path afterwards stays where it was.
    void move_to(int n, int subject, int x, int y, bool keep_later = true);
    void set_sprite(int n, int object, int sprite, bool to_end = false);  // to_end: also all later frames
    void set_sound(int n, int sample);                                     // n >= 1, sample 0..4
    void insert_frame(int n);     // after position n: no movement, same sprites, so the picture stays
    void duplicate_frame(int n);  // n >= 1: after frame n - 1 a copy of it (the movement repeats)
    void delete_frame(int n);     // n >= 1; later positions lose that frame's movement; the last frame stays
    void stroke_begin();
    void stroke_end();
    void undo();
    void redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    // Picture of the rink region (x0, y0, w, h) in rink pixels at position n with puck and objects, indexed (palette()).
    // The region may reach beyond the rink (colour 0 there).
    Image render(int n, int x0, int y0, int w, int h) const;
    // Screen rectangle (rink pixels) of an object or the puck at position n; false when it is hidden or draws nothing
    bool box(int n, int subject, int& x, int& y, int& w, int& h) const;
    int hit_test(int n, int rx, int ry) const;  // topmost object (or the puck) at a rink pixel, else -1

    static int encode_move(int dx, int dy);

private:
    std::filesystem::path data_;
    std::string name_, status_;
    TorScene scene_;
    std::vector<TorScene> undo_, redo_;
    bool in_stroke_ = false, stroke_changed_ = false;
    bool dirty_ = false;
    unsigned revision_ = 0;
    int arena_ = 0;
    std::array<Image, 5> rinks_;
    std::array<bool, 5> rink_loaded_{};
    Bytes tiles_a_, tiles_b_;
    std::array<std::vector<Image>, 3> sprites_;
    Palette palette_;

    void snapshot();
    void changed();
    uint8_t& move_byte(TorFrame& f, int subject);
    const uint8_t& move_byte(const TorFrame& f, int subject) const;
};

}  // namespace em::editor
