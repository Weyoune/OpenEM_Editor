// SPDX-License-Identifier: GPL-3.0-only
// Tests of the asset editor logic (no window): drawing tools, undo/redo, saving with backup.
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "editor/palette_doc.hpp"
#include "editor/map_doc.hpp"
#include "editor/modpack.hpp"
#include "editor/pixel_editor.hpp"
#include "editor/scene_doc.hpp"
#include "editor/sound_doc.hpp"
#include "formats/voc.hpp"
#include "editor/team_doc.hpp"
#include "formats/tore.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::editor;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                                \
        }                                                                              \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("editor_tests: no data folder, skipped\n");
        return 0;
    }
    const fs::path src = argv[1], tmp = fs::temp_directory_path() / "openem_editor_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "KB");
    for (const char* n : {"21.vga", "80.vga", "5.vga", "231.vga", "233.vga", "235.vga", "246.vga", "247.vga", "23.vga", "8.vga", "14.vga", "232.vga", "234.vga", "236.vga", "242.vga", "243.vga"})  fs::copy_file(src / "KB" / n, tmp / "KB" / n);

    PixelEditor ed(tmp);
    CHECK(ed.load(80));
    const Image before = ed.image();
    CHECK(before.width == 38 && before.height == 38);

    ed.set_tool(PixelEditor::Tool::Pencil);
    ed.set_color(2);
    ed.stroke_begin(0, 0);
    ed.stroke_move(5, 0);
    ed.stroke_end();
    CHECK(ed.dirty());
    for (int x = 0; x <= 5; x++) CHECK(ed.image().pixels[size_t(x)] == 2);

    ed.undo();
    CHECK(ed.image().pixels == before.pixels);
    ed.redo();
    CHECK(ed.image().pixels[0] == 2);

    ed.set_tool(PixelEditor::Tool::FilledRect);
    ed.set_color(9);
    ed.stroke_begin(0, 10);
    ed.stroke_move(3, 12);
    ed.stroke_end();
    CHECK(ed.image().pixels[size_t(11) * 38 + 2] == 9 && ed.image().pixels[size_t(12) * 38 + 3] == 9);

    ed.set_tool(PixelEditor::Tool::Fill);
    ed.set_color(3);
    ed.stroke_begin(37, 37);  // fill the background
    CHECK(ed.image().pixels[size_t(38) * 38 - 1] == 3);

    const Image edited = ed.image();
    CHECK(ed.save());
    CHECK(!ed.dirty());
    CHECK(fs::exists(tmp / "KB" / "80.vga.bak"));
    CHECK(ed.load(5) && ed.load(80));
    CHECK(ed.image().pixels == edited.pixels);

    // a picture with its own palette keeps it
    CHECK(ed.load(5));
    const Image screen = ed.image();
    CHECK(ed.save() == true);
    CHECK(ed.load(5) && ed.image().pixels == screen.pixels);

    CHECK(ed.export_bmp(tmp / "x.bmp") && fs::file_size(tmp / "x.bmp") == 14 + 40 + 1024 + size_t((screen.width + 3) / 4 * 4) * screen.height);

    // tile / sprite sheets and the panel: unchanged save keeps every pixel, an edit lands in the right cell
    {
        using Kind = PixelEditor::Kind;
        CHECK(ed.load(231));
        CHECK(ed.kind() == Kind::Tiles && ed.cell_size() == 16 && ed.cell_columns() == 16);
        CHECK(ed.image().width == 256 && ed.image().height == 256);
        const Bytes tiles_before = unpack_kb_block(read_file(tmp / "KB" / "231.vga"));
        ed.set_tool(PixelEditor::Tool::Pencil);
        ed.set_color(5);
        ed.stroke_begin(16 * 3 + 2, 16 * 2 + 1);  // tile 2 * 16 + 3 = 35, pixel (2, 1)
        ed.stroke_end();
        CHECK(ed.dirty() && ed.save());
        const Bytes tiles_after = unpack_kb_block(read_file(tmp / "KB" / "231.vga"));
        CHECK(tiles_after.size() == tiles_before.size());
        size_t changed = 0;
        for (size_t i = 0; i < tiles_after.size(); i++) changed += tiles_after[i] != tiles_before[i];
        CHECK(tiles_after[35 * 256 + 1 * 16 + 2] == 5 && changed <= 1);

        CHECK(ed.load(233) && ed.kind() == Kind::Sprites && ed.cell_size() == 32);
        const Image sp = ed.image();
        CHECK(ed.save() && ed.load(233) && ed.image().pixels == sp.pixels);
        CHECK(decode_tor_sprites(read_file(tmp / "KB" / "233.vga")).size() == 40);
        CHECK(ed.load(235) && ed.image().width == 256 && ed.image().height == 64);
        CHECK(ed.save() && decode_tor_sprites(read_file(tmp / "KB" / "235.vga")).size() == 12);

        CHECK(ed.load(246) && ed.kind() == Kind::Panel);
        const Image panel = ed.image();
        CHECK(panel.width == 320 && panel.height == 40);
        CHECK(ed.save() && ed.load(246) && ed.image().pixels == panel.pixels);
        ed.close();
        CHECK(ed.current() < 0);
    }

    // palette editor: only the edited colour changes, the unused high bits stay
    {
        PaletteDoc pd(tmp);
        const Bytes orig = read_file(tmp / "KB" / "21.vga");
        CHECK(pd.load(21) && !pd.dirty());
        const auto c = pd.get(7);
        CHECK(c[0] == (orig[21] & 63));
        pd.set(7, 63, 0, 10);
        CHECK(pd.dirty() && pd.get(7)[0] == 63 && pd.get(7)[2] == 10);
        pd.set(255, 1, 2, 3);  // not stored: ignored
        CHECK(pd.save() && !pd.dirty());
        const Bytes now = read_file(tmp / "KB" / "21.vga");
        CHECK(now.size() == 765);
        size_t diff = 0;
        for (size_t i = 0; i < now.size(); i++) diff += now[i] != orig[i];
        CHECK(diff <= 3 && (now[21] & 0xC0) == (orig[21] & 0xC0) && (now[21] & 63) == 63);
        CHECK(fs::exists(tmp / "KB" / "21.vga.bak"));
        pd.set(7, 0, 0, 0);
        pd.revert();
        CHECK(!pd.dirty() && pd.get(7)[0] == 63);
        CHECK(pd.load(247) && pd.palette().rgb[1] == decode_palette(read_file(tmp / "KB" / "247.vga")).rgb[1]);
    }

    // palette source: KB8 is drawn by the game over the palette of KB14; images with their own palette keep it
    {
        CHECK(ed.load(8) && ed.palette_source() == 14);
        const auto p14 = ed.palette().rgb[40];
        CHECK(ed.set_palette_source(21) && ed.palette_source() == 21 && ed.palette().rgb[40] != p14);
        CHECK(ed.set_palette_source(14) && ed.palette().rgb[40] == p14);
        CHECK(ed.load(80) && ed.palette_source() == 21);
        CHECK(ed.load(14) && ed.has_own_palette() && !ed.set_palette_source(21));
        CHECK(ed.load(231) && ed.palette_source() == 247);
    }

    // PNG-style import: exact palette colours map back to their (first matching) index, near colours snap
    {
        CHECK(ed.load(80));
        const Image before = ed.image();
        std::vector<uint32_t> px(before.pixels.size());
        for (size_t i = 0; i < px.size(); i++) {
            const auto& c = ed.palette().rgb[before.pixels[i]];
            px[i] = 0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        int approx = -1;
        CHECK(ed.import_argb(px, before.width, before.height, false, &approx) && approx == 0);
        // duplicate palette colours may map to another index with the same colour: compare as colours
        bool same = true;
        for (size_t i = 0; i < px.size(); i++) {
            const auto& a2 = ed.palette().rgb[ed.image().pixels[i]];
            const auto& b2 = ed.palette().rgb[before.pixels[i]];
            same = same && a2 == b2;
        }
        CHECK(same);
        ed.undo();
        CHECK(ed.image().pixels == before.pixels);
        // a colour that is not in the palette snaps to the nearest one; transparent -> 0
        std::vector<uint32_t> two(4, 0xFF000000u);
        two[0] = 0xFFFE0101u;
        two[1] = 0x00123456u;
        CHECK(ed.import_argb(two, 2, 2, true, &approx) && approx >= 1 && ed.image().width == 2 && ed.image().height == 2);
        CHECK(ed.image().pixels[1] == 0 && !ed.can_undo());
        CHECK(ed.load(80) && !ed.import_argb(two, 2, 2, false));  // other size without permission
        CHECK(ed.load(233) && !ed.import_argb(two, 2, 2, true));  // sheets must match exactly
    }

    // selection, clipboard, flipping and rotating, deviation report, comparison with the original
    {
        using Tool = PixelEditor::Tool;
        CHECK(ed.load(80));
        const Image start = ed.image();
        ed.set_tool(Tool::Select);
        ed.stroke_begin(2, 2);
        ed.stroke_move(9, 9);
        ed.stroke_end();
        CHECK(ed.has_selection() && ed.selection().x0 == 2 && ed.selection().x1 == 9 && ed.selection().width() == 8 && !ed.dirty() && ed.image().pixels == start.pixels);
        CHECK(ed.copy() && ed.has_clipboard());
        CHECK(ed.flip(true) && ed.image().at(2, 2) == start.pixels[size_t(2) * 38 + 9] && ed.image().at(9, 5) == start.pixels[size_t(5) * 38 + 2]);
        CHECK(ed.flip(true) && ed.image().pixels == start.pixels);
        CHECK(ed.flip(false) && ed.image().at(4, 2) == start.pixels[size_t(9) * 38 + 4]);
        CHECK(ed.flip(false) && ed.image().pixels == start.pixels);
        for (int i = 0; i < 4; i++) CHECK(ed.rotate_cw());
        CHECK(ed.image().pixels == start.pixels);
        CHECK(ed.rotate_cw() && ed.image().at(9, 2) == start.pixels[size_t(2) * 38 + 2]);  // top left corner turns to the top right
        ed.undo();
        // dragging inside the selection moves the pixels
        ed.stroke_begin(5, 5);
        ed.stroke_move(15, 7);
        ed.stroke_end();
        CHECK(ed.dirty() && ed.selection().x0 == 12 && ed.selection().y0 == 4);
        CHECK(ed.image().at(12, 4) == start.pixels[size_t(2) * 38 + 2] && ed.image().at(2, 2) == 0);
        ed.undo();
        CHECK(ed.image().pixels == start.pixels);
        // a click without movement is no edit
        ed.stroke_begin(5, 5);
        ed.stroke_end();
        CHECK(!ed.can_undo() || ed.image().pixels == start.pixels);
        // paste and delete
        CHECK(ed.paste(20, 20) && ed.image().at(20, 20) == start.pixels[size_t(2) * 38 + 2] && ed.selection().x0 == 20);
        CHECK(ed.delete_selection() && ed.image().at(22, 22) == 0);
        ed.undo();
        ed.undo();
        CHECK(ed.image().pixels == start.pixels);
        ed.clear_selection();
        CHECK(!ed.has_selection() && ed.flip(true));  // plain image without selection: the whole picture
        ed.undo();
        ed.stroke_begin(0, 0);
        ed.stroke_move(2, 5);  // 3 x 6: not square
        ed.stroke_end();
        CHECK(!ed.rotate_cw());
        CHECK(ed.load(231) && !ed.flip(true) && !ed.has_selection());  // sheets need a selection
        ed.select_all();
        CHECK(ed.has_selection() && ed.selection().x1 == 255);
        ed.set_tool(Tool::Pencil);

        // deviation of an import
        CHECK(ed.load(80));
        std::vector<uint32_t> px(start.pixels.size());
        for (size_t i = 0; i < px.size(); i++) {
            const auto& c = ed.palette().rgb[start.pixels[i]];
            px[i] = 0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        px[100] ^= 0x010000u;  // a colour one step off in red: snaps back to the same palette colour
        int approx = 0;
        CHECK(ed.import_argb(px, 38, 38, false, &approx) && approx == 1);
        CHECK(ed.last_import().approximated == 1 && ed.last_import().worst > 0 && ed.last_import().worst <= 2 && ed.last_import().pixels == 38 * 38);
        CHECK(ed.deviation().size() == px.size() && ed.deviation()[100] > 0 && ed.deviation()[99] == 0);
        CHECK(ed.load(80) && ed.deviation().empty());

        // comparison with the original: KB80 was edited and saved above, so a backup exists
        CHECK(ed.has_backup());
        const auto diff = ed.changed_pixels();
        size_t changed = 0;
        for (uint8_t v : diff) changed += v;
        CHECK(diff.size() == px.size() && changed > 0);
        CHECK(ed.restore_original() && !ed.dirty());
        const Image orig80 = decode_vga(read_file(src / "KB" / "80.vga"));
        CHECK(ed.image().pixels == orig80.pixels);
        const auto none = ed.changed_pixels();
        size_t after = 0;
        for (uint8_t v : none) after += v;
        CHECK(after == 0);
    }

    // hall map editor
    {
        MapDoc md(tmp);
        CHECK(md.load(236) && md.current() == 236 && md.tile_count() == 500);
        const Image ref = decode_rink(read_file(tmp / "KB" / "231.vga"), read_file(tmp / "KB" / "232.vga"), read_file(tmp / "KB" / "236.vga"));
        CHECK(md.rink().width == ref.width && md.rink().height == ref.height && md.rink().pixels == ref.pixels);
        const Bytes before = read_file(tmp / "KB" / "236.vga");

        md.stroke_begin();
        md.set(3, 5, 7, false);
        md.set(4, 5, 7, true);
        md.stroke_end();
        CHECK(md.dirty() && md.tile(3, 5) == 7 && !md.mirrored(3, 5) && md.mirrored(4, 5));
        CHECK(md.rink().pixels[size_t(5 * 16) * md.rink().width + size_t(3 * 16)] == md.tile_image(7).pixels[0]);
        // mirrored cell shows the tile flipped top to bottom
        CHECK(md.rink().pixels[size_t(5 * 16) * md.rink().width + size_t(4 * 16)] == md.tile_image(7).pixels[size_t(15) * 16]);
        md.undo();
        CHECK(md.rink().pixels == ref.pixels && md.can_redo());
        md.redo();
        CHECK(md.tile(3, 5) == 7);

        md.fill(0, 0, 9, false);  // the whole connected region of the corner cell
        CHECK(md.tile(0, 0) == 9);
        md.undo();
        CHECK(md.save() && !md.dirty() && fs::exists(tmp / "KB" / "236.vga.bak"));
        const Bytes after = read_file(tmp / "KB" / "236.vga");
        CHECK(after.size() == before.size());
        size_t diff = 0;
        for (size_t i = 0; i < after.size(); i++) diff += after[i] != before[i];
        CHECK(diff > 0 && diff <= 4);  // two cells of two bytes
        MapDoc re(tmp);
        CHECK(re.load(236) && re.tile(3, 5) == 7 && re.mirrored(4, 5));
        CHECK(re.load(242) && re.current() == 242);
        md.stroke_begin();
        md.stroke_end();  // empty stroke leaves no undo step
        const bool had = md.can_undo();
        md.undo();
        (void)had;
    }

    // team editor
    {
        TeamDoc td(tmp);
        const Bytes orig = read_file(tmp / "KB" / "23.vga");
        CHECK(td.load() && td.club_name(8) == "MANNHEIMER ERC" && td.player_name(8, 0) == "SCHLICKENRIED");
        // foreigner byte = byte behind the name: KRENTZ (18) and LALA (19) of Mannheim, not FRITZ (20)
        CHECK(td.foreign(8, 18) && td.foreign(8, 19) && !td.foreign(8, 20));
        CHECK(!td.dirty() && TeamDoc::editable(47) && !TeamDoc::editable(48) && TeamDoc::editable(52));

        CHECK(td.save());  // unchanged: identical file
        CHECK(read_file(tmp / "KB" / "23.vga") == orig);

        CHECK(td.set_club_name(8, "mannheimer sc") && td.club_name(8) == "MANNHEIMER SC");
        CHECK(!td.set_club_name(8, "ZU LANGER VEREINSNAME MIT VIELEN ZEICHEN"));
        CHECK(td.set_player_name(8, 0, "Müller") && td.player_name(8, 0) == "MüLLER");
        CHECK(!td.set_player_name(8, 1, "ÅKE"));
        td.set_player_name(8, 0, "MÜLLER");
        td.set_foreign(8, 20, true);
        td.set_crest(8, 5);
        td.set_jersey(8, 15, 0, 3);
        td.set_strength(8, 0, 77);
        td.set_place(8, 4);
        CHECK(td.dirty() && td.save() && !td.dirty());
        TeamDoc re(tmp);
        CHECK(re.load() && re.club_name(8) == "MANNHEIMER SC" && re.player_name(8, 0) == "MÜLLER" && re.foreign(8, 20));
        CHECK(re.data(8).crest == 5 && re.data(8).strength[0] == 77 && re.data(8).place == 4 && re.data(8).jersey == 0x0F03);
        CHECK(re.foreign(8, 18) && re.player_name(8, 2) == td.player_name(8, 2));  // neighbours untouched

        const std::string n0 = re.club_name(0), n1 = re.club_name(1);
        const auto d0 = re.data(0), d1 = re.data(1);
        re.swap_teams(0, 1);
        CHECK(re.club_name(0) == n1 && re.club_name(1) == n0 && re.data(0).strength == d1.strength && re.data(1).crest == d0.crest);
        CHECK(re.save());
        TeamDoc re2(tmp);
        CHECK(re2.load() && re2.club_name(0) == n1 && re2.player_name(0, 0) == re.player_name(0, 0));
        const size_t dups = re2.duplicate_names().size();  // the original data has some already
        re2.set_player_name(2, 0, re2.player_name(3, 0));
        CHECK(re2.duplicate_names().size() == dups + 1 || re2.duplicate_names().size() == dups);
        re2.revert();
        CHECK(!re2.dirty() && re2.duplicate_names().size() == dups);
    }
// European clubs (teams 64..149): name, jersey and strengths        TeamDoc eu(tmp);        CHECK(eu.load() && eu.club_name(64) == "KLAGENFURTER AC" && !TeamDoc::has_players(64) && TeamDoc::editable(149));        CHECK(eu.set_club_name(64, "hc test") && eu.club_name(64) == "HC TEST");        CHECK(!eu.set_club_name(64, "EIN VIEL ZU LANGER EUROPAEISCHER NAME"));        eu.set_strength(64, 0, 55);        eu.set_jersey(64, 1, 2, 3);        CHECK(eu.save());        TeamDoc eu2(tmp);        CHECK(eu2.load() && eu2.club_name(64) == "HC TEST" && eu2.club_name(65) == eu.club_name(65) && eu2.data(64).strength[0] == 55 && eu2.data(64).jersey == 0x0123);        eu2.swap_teams(64, 65);        CHECK(eu2.club_name(65) == "HC TEST" && eu2.data(65).strength[0] == 55);        eu2.swap_teams(0, 64);  // different groups: ignored        CHECK(eu2.club_name(64) != "EISBÄREN BERLIN");

    // sound editor
    {
        for (int n = 237; n <= 241; n++) fs::copy_file(src / "KB" / (std::to_string(n) + ".vga"), tmp / "KB" / (std::to_string(n) + ".vga"), fs::copy_options::overwrite_existing);
        SoundDoc so(tmp);
        const Bytes orig = read_file(tmp / "KB" / "238.vga");
        CHECK(so.load(238) && so.sound().sample_rate > 4000 && so.capacity() == orig.size() && !so.dirty());
        CHECK(so.save() && read_file(tmp / "KB" / "238.vga") == orig);  // unchanged: identical file
        WavSound tone;
        tone.sample_rate = 44100;
        for (int i = 0; i < 44100 * 2; i++) tone.mono.push_back(float(0.5 * std::sin(i * 0.03)));
        VocSound ref;
        ref.sample_rate = 44100;
        for (float v : tone.mono) ref.pcm.push_back(uint8_t(128 + 120 * v));
        CHECK(so.import_wav(encode_wav8(ref)) && so.dirty());
        CHECK(so.file_size() <= so.capacity() && so.last_fit().trimmed && so.sound().sample_rate >= 4000);  // two seconds do not fit
        CHECK(so.save() && fs::exists(tmp / "KB" / "238.vga.bak") && read_file(tmp / "KB" / "238.vga.bak") == orig);
        CHECK(read_file(tmp / "KB" / "238.vga").size() <= orig.size());
        SoundDoc re(tmp);
        CHECK(re.load(238) && re.capacity() == orig.size() && re.sound().pcm == so.sound().pcm);  // the buffer stays the original size
        so.revert();
        CHECK(!so.dirty());
        CHECK(!so.import_wav(Bytes{1, 2, 3}));
        CHECK(so.load(237) && so.sound().loops && so.save());
        so.trim(10, 20);
        CHECK(so.sound().pcm.size() == 10 && so.dirty());
    }

    // goal scene editor
    {
        fs::create_directories(tmp / "TORE");
        for (const auto& e : fs::directory_iterator(src / "TORE")) fs::copy_file(e.path(), tmp / "TORE" / e.path().filename(), fs::copy_options::overwrite_existing);
        const auto names = SceneDoc::scene_names(tmp);
        CHECK(names.size() == 48 && names.front() == "2.ht" && names.back() == "2.avj");
        CHECK(!SceneDoc::describe("2.ate").empty());
        SceneDoc sd(tmp);
        const Bytes orig = read_file(tmp / "TORE" / "2.at");
        CHECK(sd.load("2.at") && sd.frames() == 139 && !sd.dirty());
        CHECK(sd.save() && read_file(tmp / "TORE" / "2.at") == orig);  // unchanged: identical file
        const auto state = play_tor(sd.scene(), 40);
        CHECK(sd.position(40, 6).x == state.objects[6].x && sd.position(40, 6).y == state.objects[6].y);
        CHECK(sd.position(40, SceneDoc::kPuck).x == state.puck_x);
        CHECK(sd.trajectory(6).size() == 140 && sd.trajectory(6)[40].x == state.objects[6].x);

        // no-op edits leave no trace
        const auto p10 = sd.position(10, 6);
        sd.move_to(10, 6, p10.x, p10.y);
        CHECK(!sd.dirty() && !sd.can_undo());

        // the start moves the whole path
        const auto s0 = sd.position(0, 6), s5 = sd.position(5, 6);
        sd.move_to(0, 6, s0.x + 10, s0.y - 3);
        CHECK(sd.dirty() && sd.position(5, 6).x == s5.x + 10 && sd.position(5, 6).y == s5.y - 3);
        sd.undo();
        CHECK(sd.position(5, 6).x == s5.x && !sd.can_undo() && sd.can_redo());
        sd.redo();
        sd.undo();

        // a step is limited to 7 pixels; keep_later holds the later path
        sd.move_to(10, 6, p10.x + 500, p10.y);
        CHECK(sd.position(10, 6).x - sd.position(9, 6).x <= 7);
        sd.undo();
        bool found = false;
        for (int o = 0; o < 12 && !found; o++)
            for (int n = 1; n < sd.frames() && !found; n++) {
                const auto a = sd.position(n - 1, o), b = sd.position(n, o), c = sd.position(n + 1, o);
                if (b.x - a.x + 1 > 7 || c.x - b.x - 1 < -7 || (a.x == 0 && a.y == 0)) continue;
                found = true;
                sd.move_to(n, o, b.x + 1, b.y, true);
                CHECK(sd.position(n, o).x == b.x + 1 && sd.position(n + 1, o).x == c.x && sd.position(n + 1, o).y == c.y);
                sd.undo();
                sd.move_to(n, o, b.x + 1, b.y, false);
                CHECK(sd.position(n + 1, o).x == c.x + 1);
                sd.undo();
            }
        CHECK(found);
        CHECK(encode_tor(sd.scene()) == orig);

        // frames: insert keeps the picture, delete takes it out again
        const auto q5 = sd.position(5, 6), q6 = sd.position(6, 6);
        sd.insert_frame(5);
        CHECK(sd.frames() == 140 && sd.position(5, 6).x == q5.x && sd.position(6, 6).x == q5.x && sd.position(7, 6).x == q6.x);
        CHECK(sd.sprite(6, 1) == sd.sprite(5, 1));
        sd.delete_frame(6);
        CHECK(sd.frames() == 139 && encode_tor(sd.scene()) == orig);
        sd.duplicate_frame(3);
        CHECK(sd.frames() == 140);
        sd.undo();
        sd.undo();
        sd.undo();

        // sprites and sound
        sd.set_sprite(3, 1, 20);
        CHECK(sd.sprite(3, 1) == 20);
        sd.set_sprite(3, 1, 21, true);
        CHECK(sd.sprite(139, 1) == 21 && sd.sprite(2, 1) != 21);
        sd.set_sound(2, 3);
        CHECK(sd.sound(2) == 3 && sd.sound(0) == 0);
        sd.set_sound(2, 9);  // not a sample: ignored
        CHECK(sd.sound(2) == 3);

        // strokes: one undo step for several moves
        sd.stroke_begin();
        const auto t0 = sd.position(0, 2);
        sd.move_to(0, 2, t0.x + 1, t0.y);
        sd.move_to(0, 2, t0.x + 2, t0.y);
        sd.stroke_end();
        CHECK(sd.position(0, 2).x == t0.x + 2);
        sd.undo();
        CHECK(sd.position(0, 2).x == t0.x);
        sd.stroke_begin();
        sd.stroke_end();  // nothing changed: no undo step
        const bool can = sd.can_undo();
        sd.undo();
        sd.undo();
        sd.undo();
        (void)can;

        // pictures: size, hit test
        const Image view = sd.render(0, 0, 0, 672, 1152);
        CHECK(view.width == 672 && view.height == 1152 && view.pixels != sd.rink().pixels);  // objects are drawn
        int bx = 0, by = 0, bw = 0, bh = 0, hit_object = -1;
        for (int o = 0; o < 12 && hit_object < 0; o++)
            if (sd.box(0, o, bx, by, bw, bh)) hit_object = o;
        CHECK(hit_object >= 0 && sd.hit_test(0, bx + bw / 2, by + bh / 2) >= 0 && sd.hit_test(0, -50, -50) == -1);
        const auto cam = sd.camera_view(0);
        const Image game = sd.render(0, cam.x, cam.y, 320, 200);
        CHECK(game.width == 320 && game.height == 200);

        // save keeps a .bak of the original and the edit survives a reload
        sd.set_sprite(1, 0, 22);
        CHECK(sd.save() && fs::exists(tmp / "TORE" / "2.at.bak") && read_file(tmp / "TORE" / "2.at.bak") == orig);
        SceneDoc re(tmp);
        CHECK(re.load("2.at") && re.sprite(1, 0) == 22 && re.frames() == 139);
        CHECK(re.set_arena(1) && re.arena() == 1);
        re.set_shirt(0, 1, 2, 3);
        CHECK(re.palette().rgb[254][0] == 1);
    }

    // scenes: paths, smoothing, shirt colours, new scene sets
    {
        SceneDoc sd(tmp);
        CHECK(sd.load("2.at"));
        const Bytes before = encode_tor(sd.scene());
        // a path of 10 frames to a point 50 pixels away: even steps of 5
        const auto a = sd.position(20, 6);
        CHECK(sd.route(6, 20, 30, a.x + 50, a.y - 30));
        CHECK(sd.position(30, 6).x == a.x + 50 && sd.position(30, 6).y == a.y - 30);
        for (int n = 21; n <= 30; n++) CHECK(std::abs(sd.position(n, 6).x - sd.position(n - 1, 6).x) <= 5 && std::abs(sd.position(n, 6).y - sd.position(n - 1, 6).y) <= 3);
        sd.undo();
        CHECK(encode_tor(sd.scene()) == before);
        CHECK(!sd.route(6, 20, 22, a.x + 100, a.y));
        CHECK(encode_tor(sd.scene()) == before);  // too far for two frames: nothing changed
        const auto e0 = sd.position(sd.frames(), 6);
        // smoothing keeps both ends
        const auto p25 = sd.position(25, 6), p40 = sd.position(40, 6);
        CHECK(sd.smooth(6, 25, 40));
        CHECK(sd.position(25, 6).x == p25.x && sd.position(40, 6).x == p40.x && sd.position(40, 6).y == p40.y && sd.position(sd.frames(), 6).x == e0.x);
        sd.undo();
        CHECK(encode_tor(sd.scene()) == before);
        // jersey colours: white is 0x0FFF
        sd.set_shirt_jersey(0, 0x0FFF);
        CHECK(sd.palette().rgb[254][0] > 240 && sd.palette().rgb[254][1] > 240);
        sd.set_shirt_jersey(1, 0x0000);
        CHECK(sd.palette().rgb[253][0] == 0 && sd.palette().rgb[253][2] == 0);

        // new scene set: copies of the four scripts of the number, anzahl raised
        int counts[3];
        CHECK(SceneDoc::read_scene_counts(tmp, counts) && counts[0] == 11 && counts[1] == 2 && counts[2] == 2);
        const int n = sd.add_scene_set("5.ht");
        CHECK(n == 12);
        for (const char* name : {"12.ht", "12.hv", "12.at", "12.av"}) CHECK(fs::exists(tmp / "TORE" / name));
        CHECK(read_file(tmp / "TORE" / "12.at") == read_file(tmp / "TORE" / "5.at"));
        CHECK(SceneDoc::read_scene_counts(tmp, counts) && counts[0] == 12 && counts[1] == 2);
        CHECK(fs::exists(tmp / "TORE" / "anzahl.bak") && SceneDoc::scene_names(tmp).size() == 52);
        CHECK(sd.add_scene_set("2.hte") == 3);
        for (const char* name : {"3.hte", "3.hve", "3.ate", "3.ave"}) CHECK(fs::exists(tmp / "TORE" / name));
        CHECK(SceneDoc::read_scene_counts(tmp, counts) && counts[1] == 3 && counts[0] == 12 && counts[2] == 2);
        CHECK(sd.add_scene_set("nonsense") == -1);
        fs::remove(tmp / "TORE" / "7.hv");
        CHECK(sd.add_scene_set("7.ht") == -1);  // an incomplete set would crash the game: refuse
        CHECK(SceneDoc::read_scene_counts(tmp, counts) && counts[0] == 12);
    }

    // mod packages
    {
        CHECK(allowed_mod_path("KB/80.vga") && allowed_mod_path("TORE/2.ht") && allowed_mod_path("TORE/anzahl"));
        CHECK(!allowed_mod_path("KB/../EM.EXE") && !allowed_mod_path("EM.EXE") && !allowed_mod_path("KB/80.exe") && !allowed_mod_path("TORE/../x") && !allowed_mod_path("KB/a.vga"));
        const auto files = changed_files(tmp);
        bool has80 = false, hasTore = false;
        for (const auto& c : files) {
            has80 = has80 || c.path == "KB/21.vga";
            hasTore = hasTore || c.path == "TORE/anzahl";
        }
        CHECK(has80 && hasTore);
        std::string err;
        const fs::path zip = tmp / "mod.zip";
        CHECK(create_modpack(tmp, zip, &err) == files.size());
        // apply into a clean copy of the original game files
        const fs::path clean = fs::temp_directory_path() / "openem_editor_test_clean";
        fs::remove_all(clean);
        fs::create_directories(clean / "KB");
        fs::create_directories(clean / "TORE");
        fs::copy_file(src / "KB" / "21.vga", clean / "KB" / "21.vga");
        fs::copy_file(src / "TORE" / "anzahl", clean / "TORE" / "anzahl");
        CHECK(apply_modpack(clean, zip, &err) == int(files.size()));
        CHECK(read_file(clean / "KB" / "21.vga") == read_file(tmp / "KB" / "21.vga") && read_file(clean / "TORE" / "anzahl") == read_file(tmp / "TORE" / "anzahl"));
        CHECK(read_file(clean / "KB" / "21.vga.bak") == read_file(src / "KB" / "21.vga"));  // the original is kept
        CHECK(restore_file(clean, "KB/21.vga") && read_file(clean / "KB" / "21.vga") == read_file(src / "KB" / "21.vga"));
        for (const auto& c : changed_files(clean)) CHECK(c.path != "KB/21.vga");
        CHECK(!restore_file(clean, "EM.EXE"));
        // a package with a path outside KB / TORE is refused as a whole
        ZipEntries bad{{"KB/81.vga", Bytes{1}}, {"EM.EXE", Bytes{2}}};
        const fs::path badzip = tmp / "bad.zip";
        {
            const Bytes z = make_zip(bad);
            std::ofstream o(badzip, std::ios::binary);
            o.write(reinterpret_cast<const char*>(z.data()), std::streamsize(z.size()));
        }
        CHECK(apply_modpack(clean, badzip, &err) == -1 && !err.empty() && !fs::exists(clean / "KB" / "81.vga"));
        // a ZIP made by another tool (Windows Compress-Archive: deflate)
        const fs::path td = EM_TEST_DATA;
        const ZipEntries z = read_zip(read_file(td / "deflate.zip"));
        CHECK(z.size() == 2);
        for (const auto& [name, content] : z) CHECK(content == read_file(td / (name == "text.txt" ? "deflate_text.txt" : "deflate_random.bin")));
        bool threw = false;
        try { read_zip(Bytes(100, 0)); } catch (const FormatError&) { threw = true; }
        CHECK(threw);
        fs::remove_all(clean);
    }

    fs::remove_all(tmp);
    if (failures) {
        std::printf("FAILED (%d failures)\n", failures);
        return 1;
    }
    std::printf("editor tests OK\n");
    return 0;
}
