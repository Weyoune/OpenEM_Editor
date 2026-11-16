// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Goal scenes ("Torszenen"): TORE\<n>.<side><kind>[variant] scripts and the KB graphics they use.
//
// File name, built by ovl15_0143: "tore\" + n + (".h" home goal | ".a" away goal)
//   + ("t" | "v") + optional "e" / "j" variant. n is rolled from 2..MAXSZENE; TORE\anzahl holds
//   "MAXSZENE:11|2|2" = number of normal scenes and of the e and j variants (read by ovl15_0000).
//
// Script layout (played by ovl14_4938; header parsed by FUN_1bbf_5250, frames by FUN_1bbf_5316):
//   0x00  "TOR!"
//   0x04  u16be camera x, u16be camera y   (the player clamps x-16 / y-32 at 0)
//   0x08  u16be puck x, u16be puck y
//   0x0C  12 x { u8 (unused), u8 sprite, u16be x, u16be y }   start of the 12 skaters
//   0x54  frames of 28 bytes until the u16 0xAAAA end mark:
//         +0 camera move, +1 puck move, +2 u8 (always 0), +3 sound (0 = none, else sample 1..4)
//         +4 12 x { u8 sprite, u8 move }  (sprite 0 = object not drawn this frame; a zero word
//            leaves the object unchanged)
//   Move byte m: dx = (m >> 3) & 7, dy = m & 7, negated by bit 0x40 (dx) and 0x80 (dy) - so an
//   object moves at most 7 pixels per frame (FUN_1bbf_51ad).
// Scene coordinates are those of the portrait rink below (x 0..671, y 0..1151); goals are at the
// top (".h" scenes, camera y near 0) and bottom (".a" scenes, camera y near 870).
//
// Graphics, loaded by ovl14_4938 as KB\<231 + i>.vga for i = 0..13 (unpacked sizes at DS:5400):
//   231, 232  rink tile sets: u16 packed length + RLE (same scheme as images) -> 250 tiles of
//             16 x 16 each (64000 bytes); together tiles 0..499
//   233, 234  skater sprites of the two teams: packed, 40 sprites of 32 x 32 (40960 bytes)
//   235       12 further 32 x 32 sprites (packed, 12288 bytes; goalies/puck, open)
//   236       rink tile map, plain: 42 x 72 u16be (column by column in screen terms, see
//             decode_rink); entry & 0x7FFF = tile, bit 0x8000 = mirrored. 242..245 replace 236
//             for some arenas (scene argument 1..4)
//   237..241  VOC samples; 246 = 630-byte table (open); 247 = palette
// All 16x16 tiles and 32x32 sprites are stored row by row in screen orientation; the old
// renderer writes them to Mode X planes (FUN_1bbf_2cb6 unpacks).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/vga.hpp"

namespace em {

constexpr int kTorObjects = 12;
constexpr int kRinkTile = 16;
constexpr int kRinkColumns = 42, kRinkRows = 72;  // portrait: 672 x 1152 pixels
constexpr int kTorSprite = 32;

struct TorMove {
    int dx = 0, dy = 0;
};
TorMove decode_tor_move(uint8_t m);

struct TorObject {
    int sprite = 0;
    int x = 0, y = 0;
    uint8_t lead = 0;  // byte in front of the sprite in the start table (unused by the player, kept for exact rewriting)
};

struct TorFrame {
    uint8_t camera_move = 0, puck_move = 0;
    uint8_t sound = 0;
    uint8_t extra = 0;  // byte +2 (always 0 in the originals, kept for exact rewriting)
    std::array<uint8_t, kTorObjects> sprite{};
    std::array<uint8_t, kTorObjects> move{};
};

struct TorScene {
    int camera_x = 0, camera_y = 0;
    int puck_x = 0, puck_y = 0;
    std::array<TorObject, kTorObjects> objects{};
    std::vector<TorFrame> frames;
};

TorScene decode_tor(const Bytes& data);
// Inverse of decode_tor: byte identical for the original scripts. Throws FormatError for values the file cannot hold
// (coordinates above 65535, no frame).
Bytes encode_tor(const TorScene& scene);

// Positions after playing frames [0, n): applies all moves like FUN_1bbf_5316.
struct TorState {
    int camera_x = 0, camera_y = 0, puck_x = 0, puck_y = 0;
    std::array<TorObject, kTorObjects> objects{};
};
TorState play_tor(const TorScene& scene, size_t n);

// What the renderer shows after n frames (shared by the classic and the high resolution scene): FUN_1bbf_5250 starts the
// camera at the script position minus (16, 32), clamped at 0; FUN_1bbf_4d98 clamps every camera move to 0..0x160 /
// 0..0x3B8 (the 320 x 200 window inside the 672 x 1152 rink). FUN_1bbf_5316 moves the puck freely and an object whose
// frame word is not zero; the object type is the frame's sprite.
constexpr int kTorViewWidth = 320, kTorViewHeight = 200;
constexpr int kTorCameraMaxX = 0x160, kTorCameraMaxY = 0x3B8;
TorState tor_state_at(const TorScene& scene, int n);

// FUN_1bbf_4ecf / FUN_1bbf_4fe7: an object at (0, 0) is skipped; its top left corner is (x - 0x50, y - 0x60) in rink
// pixels; types 0 and 1 are the 5 x 5 bitmaps of the renderer, 2..0x29 / 0x2A..0x51 / 0x52..0x5D the sprites of KB233 /
// KB234 / KB235, higher types draw nothing.
constexpr int kTorAnchorX = 0x50, kTorAnchorY = 0x60;
struct TorSpriteRef {
    enum class Kind { None, Dot, Puck, Sheet } kind = Kind::None;
    int sheet = 0;  // 0..2 = KB233, KB234, KB235
    int index = 0;  // sprite within the sheet
};
TorSpriteRef tor_sprite_ref(int type);

// u16 packed length + RLE stream (KB\231..235.vga) -> raw bytes.
Bytes unpack_kb_block(const Bytes& data);

// Inverse of unpack_kb_block: u16 packed length (RLE incl. the 0x80 end marker) + stream.
Bytes pack_kb_block(const Bytes& raw);
// Inverse of decode_tor_sprites (32 x 32 images, concatenated and packed).
Bytes encode_tor_sprites(const std::vector<Image>& sprites);
// Inverse of decode_tor_panel (320 x 40, plain RLE stream with end marker, no length).
Bytes encode_tor_panel(const Image& panel);

// Rink as one portrait image from the two tile sets (231, 232) and a tile map (236 or 242..245).
Image decode_rink(const Bytes& tiles_a, const Bytes& tiles_b, const Bytes& map);

// Sprite sheet (233, 234, 235) as a column of 32 x 32 images.
std::vector<Image> decode_tor_sprites(const Bytes& data);

// KB\246.vga: the panel of the goal scene (the bottom 40 lines, shown through the VGA split screen), a plain RLE stream
// without length (FUN_1bbf_54bf: c < 0x80 -> c + 1 literal bytes, c > 0x80 -> (c & 0x7F) + 1 copies of the next byte,
// 0x80 ends) of 320 x 40 pixels row by row.
constexpr int kTorPanelWidth = 320, kTorPanelHeight = 40;
Image decode_tor_panel(const Bytes& data);

// The puck (object type 1) and type 0 as 5 x 5 bitmaps of the renderer (seg007:0054 / seg007:006D), 0 = transparent.
extern const std::array<uint8_t, 25> kTorPuck, kTorDot;

}  // namespace em
