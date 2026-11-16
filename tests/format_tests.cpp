// SPDX-License-Identifier: GPL-3.0-only
// Format tests: encoders and decoders of the editor on synthetic data and (with the folder of the original game as
// argument) on every original file: images, palettes, names, team tables and goal scene scripts must survive a round trip.
#include <cstdio>
#include <filesystem>

#include "formats/db.hpp"
#include "formats/teamdata.hpp"
#include "formats/tore.hpp"
#include "formats/vga.hpp"

using namespace em;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

// encoder: round trip of runs, literals, long runs, tall images
static void encoder_tests() {
    for (int h : {1, 7, 200, 202, 450}) {
        Image src(37, h);
        for (size_t i = 0; i < src.pixels.size(); i++)
            src.pixels[i] = (i / 300) % 3 == 0 ? uint8_t(i * 7) : uint8_t(i / 50);
        Image back = decode_vga(encode_vga(src));
        CHECK(back.complete && back.width == 37 && back.height == h && back.pixels == src.pixels);
    }
    Palette pal;
    for (size_t i = 0; i < 256; i++) pal.rgb[i] = {uint8_t((i & 63) << 2 | (i & 63) >> 4), 0, 0};
    Bytes withpal = encode_vga(Image(2, 2, 5), &pal);
    CHECK(classify_vga(withpal) == VgaKind::ImageWithPalette);
    CHECK(decode_palette(withpal, withpal.size() - 765).rgb[3] == pal.rgb[3]);
}

static void original_kb(const fs::path& em_dir) {
    int images = 0, complete = 0, files = 0;
    for (auto& e : fs::directory_iterator(em_dir / "KB")) {
        if (e.path().extension() != ".vga") continue;
        files++;
        Bytes d = read_file(e.path());
        VgaKind k = classify_vga(d);
        if (k == VgaKind::Image || k == VgaKind::ImageWithPalette) {
            images++;
            Image img = decode_vga(d);
            complete += img.complete;
            // encoder round trip on the original (palette from the file, six-bit)
            if (k == VgaKind::Image) {
                Image back = decode_vga(encode_vga(img));
                CHECK(back.pixels == img.pixels);
            } else {
                Palette pal = decode_palette(d, d.size() - 765);
                Bytes re = encode_vga(img, &pal);
                CHECK(decode_vga(re).pixels == img.pixels);
                Bytes want(d.end() - 765, d.end());
                for (auto& b : want) b &= 63;
                CHECK(encode_palette(pal) == want);
            }
        }
    }
    std::printf("KB: %d files, %d images, %d complete\n", files, images, complete);
    CHECK(files == 114);
    CHECK(images == 94);     // 88 plain + 6 screens with palette
    CHECK(complete == 94);   // every image fills width*height
}

// Master database: 48 named clubs (player ids 0..1199, so the pool starts at PIELER1200) and the
// European club list.
static void original_db(const fs::path& em_dir) {
    Database db = decode_db(read_file(em_dir / "KB" / "23.vga"));
    CHECK(db.clubs.size() == 64 && db.clubs[52].name == "RUSSLAND" && db.clubs[63].name == "TSCHECHEI");
    CHECK(db.clubs[0].name == "EISBÄREN BERLIN");
    CHECK(db.clubs[1].name == "SC PREUSSEN BERLIN" && db.clubs[1].players[0].name == "APPEL");
    CHECK(db.clubs[8].name == "MANNHEIMER ERC");
    CHECK(db.clubs[47].name == "ESV SCHIERKE");
    CHECK(db.europe_clubs.size() == 86 && db.europe_clubs.front() == "KLAGENFURTER AC");
    std::printf("DB: %zu clubs, %zu European clubs\n", db.clubs.size(), db.europe_clubs.size());

    // writer: unchanged database -> identical bytes; one edited name changes only its field
    const Bytes orig = read_file(em_dir / "KB" / "23.vga");
    CHECK(encode_db(orig, db) == orig);
    Database edited = db;
    edited.clubs[0].name = "HAMBURGER SV";
    edited.clubs[1].players[0].name = "MÜLLER";
    edited.clubs[1].players[1].name = "";
    Bytes out = encode_db(orig, edited);
    size_t diff = 0;
    for (size_t i = 0; i < orig.size(); i++) diff += orig[i] != out[i];
    Database back = decode_db(out);
    CHECK(back.clubs[0].name == "HAMBURGER SV" && back.clubs[1].players[0].name == "MÜLLER");
    CHECK(back.clubs[1].players[1].name.empty() && back.clubs[1].players[2].name == db.clubs[1].players[2].name);
    CHECK(diff > 0 && diff < 80 && out.size() == orig.size());
    // team tables
    {
        auto td = decode_team_data(orig);
        CHECK(td.size() == 150 && encode_team_data(orig, td) == orig);
        bool crests_ok = true;
        for (int t = 0; t < 64; t++) crests_ok = crests_ok && td[t].crest >= 0 && td[t].crest <= 63;
        CHECK(crests_ok);
        // Bundesliga: place = rank by strength (largest a0 -> 1)
        bool rank_ok = true;
        for (int t = 0; t < 12; t++) {
            int better = 0;
            for (int u = 0; u < 12; u++) better += td[u].strength[0] > td[t].strength[0];
            rank_ok = rank_ok && td[t].place == better + 1;
        }
        CHECK(rank_ok);
        auto ed = td;
        ed[3].crest = 17; ed[3].strength = {50, 51, 52}; ed[3].place = 4; ed[3].jersey = 0x0F0F;
        auto re = decode_team_data(encode_team_data(orig, ed));
        CHECK(re[3].crest == 17 && re[3].strength[1] == 51 && re[3].place == 4 && re[3].jersey == 0x0F0F);
        CHECK(re[4].crest == td[4].crest && re[4].strength == td[4].strength);
        ed[3].crest = 64;
        bool bad = false;
        try { encode_team_data(orig, ed); } catch (const FormatError&) { bad = true; }
        CHECK(bad);
    }
    bool threw = false;
    try { edited.clubs[0].name = "ÅÅ"; encode_db(orig, edited); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
}

// Goal scenes: every TORE script parses up to its 0xAAAA end mark with the frame count implied by
// the file size, and all objects stay on (or just beside) the 672 x 1152 portrait rink. The rink
// and sprite graphics unpack to the sizes the loader allocates (DS:5400).
static void original_tore(const fs::path& em_dir) {
    int files = 0;
    bool frames_ok = true, bounds_ok = true;
    for (auto& e : fs::directory_iterator(em_dir / "TORE")) {
        if (e.path().filename() == "anzahl") continue;
        Bytes d = read_file(e.path());
        TorScene s = decode_tor(d);
        files++;
        CHECK(encode_tor(s) == d);  // the writer reproduces every original script byte for byte
        frames_ok = frames_ok && s.frames.size() * 28 + 0x54 + 2 == d.size();
        for (size_t n = 0; n <= s.frames.size(); n += 10) {
            TorState st = play_tor(s, n);
            bounds_ok = bounds_ok && st.puck_x >= -16 && st.puck_x < 1000 && st.puck_y >= -16 && st.puck_y < 1250;
        }
    }
    CHECK(files == 48 && frames_ok && bounds_ok);
    CHECK(decode_tor(read_file(em_dir / "TORE" / "2.at")).frames.size() == 139);
    CHECK(decode_tor_move(0x94).dx == 2 && decode_tor_move(0x94).dy == -4);
    const fs::path kb = em_dir / "KB";
    CHECK(unpack_kb_block(read_file(kb / "231.vga")).size() == 64000);
    CHECK(unpack_kb_block(read_file(kb / "233.vga")).size() == 40960);
    CHECK(unpack_kb_block(read_file(kb / "235.vga")).size() == 12288);
    for (const char* n : {"231.vga", "232.vga", "233.vga", "234.vga", "235.vga"}) {
        Bytes raw = unpack_kb_block(read_file(kb / n));
        CHECK(unpack_kb_block(pack_kb_block(raw)) == raw);
    }
    {
        auto sp = decode_tor_sprites(read_file(kb / "233.vga"));
        auto back = decode_tor_sprites(encode_tor_sprites(sp));
        CHECK(back.size() == sp.size() && !sp.empty());
        for (size_t i = 0; i < sp.size() && i < back.size(); i++) CHECK(back[i].pixels == sp[i].pixels);
        Image panel = decode_tor_panel(read_file(kb / "246.vga"));
        CHECK(decode_tor_panel(encode_tor_panel(panel)).pixels == panel.pixels);
    }
    Image rink = decode_rink(read_file(kb / "231.vga"), read_file(kb / "232.vga"), read_file(kb / "236.vga"));
    CHECK(rink.width == 672 && rink.height == 1152);
    // centre spot: the faceoff dot in the middle of the rink is not ice colour
    CHECK(rink.at(336, 576) != rink.at(336, 500));
    std::printf("TORE: %d scripts\n", files);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        encoder_tests();
        if (argc > 1) {
            original_kb(argv[1]);
            original_db(argv[1]);
            original_tore(argv[1]);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 2;
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
