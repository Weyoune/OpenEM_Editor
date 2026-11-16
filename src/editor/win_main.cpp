// SPDX-License-Identifier: GPL-3.0-only
// OpenEM Editor - Windows front end (Win32 with Common Controls 6: menu, toolbar, list view, status bar,
// task and file dialogs). Document logic: pixel_editor.cpp.
//
//   em_editor [--data <EM folder>] [--file <KB number>] [--shot out.bmp]
// --shot opens the window, saves a picture of it and exits (for checks).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <windowsx.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "editor/map_doc.hpp"
#include "editor/modpack.hpp"
#include "editor/palette_doc.hpp"
#include "editor/pixel_editor.hpp"
#include "editor/scene_doc.hpp"
#include "editor/sound_doc.hpp"
#include "editor/team_doc.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winmm.lib")

namespace fs = std::filesystem;
using em::editor::MapDoc;
using em::editor::PaletteDoc;
using em::editor::PixelEditor;
using em::editor::SceneDoc;
using em::editor::SoundDoc;
using em::Bytes;
using em::VocFit;
using em::VocSound;
using em::editor::TeamDoc;

namespace {

enum Cmd : UINT {
    ID_FILE_SAVE = 101, ID_FILE_EXPORT, ID_FILE_EXIT, ID_FILE_IMPORT,
    ID_EDIT_UNDO = 110, ID_EDIT_REDO,
    ID_TOOL_PENCIL = 120, ID_TOOL_LINE, ID_TOOL_RECT, ID_TOOL_FILLRECT, ID_TOOL_FILL, ID_TOOL_PICKER, ID_TOOL_SELECT,
    ID_VIEW_GRID = 130, ID_VIEW_FIT, ID_VIEW_ZOOMIN, ID_VIEW_ZOOMOUT,
    ID_HELP_ABOUT = 140,
    ID_EDIT_COPY = 150, ID_EDIT_CUT, ID_EDIT_PASTE, ID_EDIT_DELETE, ID_EDIT_SELECTALL, ID_EDIT_FLIPH, ID_EDIT_FLIPV, ID_EDIT_ROTATE,
    ID_FILE_EXPORTALL = 160, ID_FILE_IMPORTDIR, ID_FILE_MODPACK, ID_FILE_MODAPPLY, ID_FILE_RESTORE, ID_FILE_NEWSCENE,
    ID_VIEW_DEVIATION = 170, ID_VIEW_CHANGES,
    IDC_TOOLBAR = 201, IDC_LIST, IDC_STATUS, IDC_TAB,
    IDC_TRACK_R = 210, IDC_TRACK_G, IDC_TRACK_B,
    IDC_MIRROR = 430,
    IDC_PALSEL = 400, IDC_CREST_PEN = 420, IDC_CREST_FILL, IDC_CREST_UNDO, IDC_CREST_SAVE, IDC_CREST_ZOUT, IDC_CREST_ZIN,
    IDC_SC_LIST = 500, IDC_SC_TRACK, IDC_SC_PREV, IDC_SC_PLAY, IDC_SC_NEXT, IDC_SC_MODE, IDC_SC_ARENA, IDC_SC_SPRITE, IDC_SC_TOEND,
    IDC_SC_X, IDC_SC_Y, IDC_SC_SOUND, IDC_SC_KEEP, IDC_SC_INS, IDC_SC_DUP, IDC_SC_DEL,
    IDC_SN_PLAY = 600, IDC_SN_STOP, IDC_SN_EXPORT, IDC_SN_IMPORT, IDC_SN_LOOP, IDC_SN_QUALITY, IDC_SN_NORM, IDC_SN_DITHER, IDC_SN_TRIM,
    IDC_SC_RTO = 520, IDC_SC_RX, IDC_SC_RY, IDC_SC_MOVE, IDC_SC_SMOOTH, IDC_SC_HOME, IDC_SC_AWAY,
    IDC_CLUB = 300, IDC_NAME0 = 310, IDC_FOR0 = 340, IDC_CREST = 370, IDC_PLACE, IDC_STR0 = 372, IDC_JER0 = 380, IDC_SWAP_BTN = 390, IDC_SWAP_COMBO,
};

const wchar_t kCrestClass[] = L"OpenEMEditorCrest", kMapClass[] = L"OpenEMEditorMap", kTilesClass[] = L"OpenEMEditorTiles";
const wchar_t kWaveClass[] = L"OpenEMEditorWave", kSceneClass[] = L"OpenEMEditorScene", kPickerClass[] = L"OpenEMEditorPicker";
const wchar_t kMainClass[] = L"OpenEMEditorMain", kCanvasClass[] = L"OpenEMEditorCanvas", kPaletteClass[] = L"OpenEMEditorPalette", kPalGridClass[] = L"OpenEMEditorPalGrid";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct MapUi {
    HWND view = nullptr, tiles = nullptr, mirror = nullptr, lbl = nullptr;
};

struct TeamUi {
    HWND club = nullptr, name[25] = {}, foreign[25] = {}, crest = nullptr, crest_ud = nullptr, place = nullptr, place_ud = nullptr;
    HWND str[3] = {}, str_ud[3] = {}, jer[3] = {}, jer_lbl[3] = {}, preview = nullptr, swap_combo = nullptr, swap_btn = nullptr;
    HWND crest_view = nullptr, cr_pen = nullptr, cr_fill = nullptr, cr_undo = nullptr, cr_save = nullptr, cr_zout = nullptr, cr_zin = nullptr;
    HWND lbl_club = nullptr, lbl_crest = nullptr, lbl_place = nullptr, lbl_str[3] = {}, lbl_jersey = nullptr, lbl_swap = nullptr, lbl_players = nullptr;
    std::vector<HWND> all;  // every control of the page, for show/hide and fonts
};

struct SceneGeo {
    double scale = 1;
    int x0 = 0, y0 = 0, rw = 0, rh = 0;  // the rink region shown
    int ox = 0, oy = 0, dw = 0, dh = 0;  // where it lands in the window
};

struct SceneUi {
    HWND view = nullptr, picker = nullptr, lbl = nullptr, track = nullptr, prev = nullptr, play = nullptr, next = nullptr;
    HWND mode = nullptr, arena = nullptr, subjects = nullptr, lbl_sprite = nullptr, sprite = nullptr, sprite_ud = nullptr, to_end = nullptr;
    HWND lbl_x = nullptr, x = nullptr, x_ud = nullptr, lbl_y = nullptr, y = nullptr, y_ud = nullptr, lbl_sound = nullptr, sound = nullptr, sound_ud = nullptr;
    HWND keep = nullptr, ins = nullptr, dup = nullptr, del = nullptr;
    HWND lbl_route = nullptr, route_to = nullptr, lbl_rx = nullptr, route_x = nullptr, lbl_ry = nullptr, route_y = nullptr, move_btn = nullptr, smooth_btn = nullptr;
    HWND lbl_home = nullptr, home = nullptr, lbl_away = nullptr, away = nullptr;
    std::vector<HWND> all;  // every control of the page, for show/hide and fonts
};

struct SoundUi {
    HWND view = nullptr, info = nullptr, play = nullptr, stop = nullptr, exp = nullptr, imp = nullptr, loop = nullptr, lbl_q = nullptr, quality = nullptr;
    HWND norm = nullptr, dither = nullptr, trim = nullptr, note = nullptr;
    std::vector<HWND> all;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, toolbar = nullptr, list = nullptr, status = nullptr, canvas = nullptr, palette = nullptr;
    HWND psel = nullptr;
    HWND tab = nullptr, pgrid = nullptr, track[3] = {}, tlabel[3] = {}, pinfo = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    std::unique_ptr<PixelEditor> ed;
    std::unique_ptr<PixelEditor> crest_ed;  // the crest of the selected team (team page)
    std::unique_ptr<PaletteDoc> pal;
    std::unique_ptr<MapDoc> map;
    MapUi mu;
    std::unique_ptr<SoundDoc> sound;
    SoundUi sn_ui;
    int sn_sel0 = -1, sn_sel1 = -1, sn_drag = -1, sn_orig_rate = 8000;
    bool sn_loading = false;
    Bytes sn_wav;  // the sound being played (PlaySound reads it directly)
    bool view_deviation = false, view_changes = false;  // overlays of the image canvas
    std::vector<uint8_t> changes_mask;
    unsigned changes_rev = ~0u;
    int changes_for = -1;
    std::unique_ptr<SceneDoc> scene;
    SceneUi su;
    std::vector<std::string> sc_names;  // list item number -> script name
    int sc_pos = 0, sc_sel = 0, sc_mode = 1, sc_pick_scroll = 0;
    bool sc_playing = false, sc_loading = false, sc_drag = false, sc_keep = true;
    SceneGeo sc_lock;
    POINT sc_grab{};
    std::wstring sc_hover;
    int sc_route_key = -1;
    std::vector<uint32_t> sc_bits;
    int map_tile = 0, map_zoom = 1, mscroll_x = 0, mscroll_y = 0, tscroll_y = 0, map_hc = -1, map_hr = -1;
    bool map_mirror = false;
    std::wstring map_hover;
    std::vector<uint32_t> map_bits;
    unsigned map_bits_rev = ~0u;
    int tab_index = 0;        // 0 Bilder, 1 Torszenen, 2 Paletten
    int pal_sel = 0;          // selected colour in the palette editor
    bool setting_sliders = false;
    int hover_cell = -1;
    std::vector<int> list_numbers;  // list row -> KB number or team index
    std::unique_ptr<TeamDoc> team;
    TeamUi tu;
    int team_sel = 0;
    bool crest_auto = true;  // zoom follows the available space until the user picks one
    int crest_zoom = 5, cscroll_x = 0, cscroll_y = 0;  // crest editor view
    bool loading_team = false;
    em::Image crest_img;
    int crest_no = -1;
    em::Palette crest_pal;
    bool crest_pal_ok = false;
    fs::path data;

    int zoom = 4;
    bool grid = true;
    int scroll_x = 0, scroll_y = 0;
    int hover_x = -1, hover_y = -1, hover_color = -1;
    bool selecting = false;  // guard against list-view feedback
    unsigned shown_revision = ~0u;
    std::vector<uint32_t> bits;  // canvas picture as 32-bit pixels
    bool shot_mode = false;

    int px(int v) const { return MulDiv(v, int(dpi), 96); }
};

App* g_app = nullptr;

// ------------------------------------------------------------------------------------------- helpers

void make_font(App& a) {
    NONCLIENTMETRICSW m{sizeof m};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0, a.dpi);
    if (a.font) DeleteObject(a.font);
    a.font = CreateFontIndirectW(&m.lfMessageFont);
}

std::wstring dialog_path(const wchar_t* title, bool folder, bool save, const std::wstring& name, HWND owner) {
    std::wstring result;
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&d))))
        return result;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    d->SetTitle(title);
    if (save) {
        static const COMDLG_FILTERSPEC spec[] = {{L"Bitmap (*.bmp)", L"*.bmp"}};
        d->SetFileTypes(1, spec);
        d->SetDefaultExtension(L"bmp");
        d->SetFileName(name.c_str());
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    d->Release();
    return result;
}

// ------------------------------------------------------------------------------------- PNG and other formats (WIC)

// Open or save dialog for pictures; the chosen extension decides the format (png default, bmp)
std::wstring image_dialog(bool save, const std::wstring& name, HWND owner) {
    std::wstring result;
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d))))
        return result;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_FORCEFILESYSTEM);
    if (save) {
        static const COMDLG_FILTERSPEC spec[] = {{L"PNG-Bild (*.png)", L"*.png"}, {L"Bitmap (*.bmp)", L"*.bmp"}};
        d->SetTitle(L"Bild exportieren");
        d->SetFileTypes(2, spec);
        d->SetDefaultExtension(L"png");
        d->SetFileName(name.c_str());
    } else {
        static const COMDLG_FILTERSPEC spec[] = {{L"Bilder (PNG, BMP, GIF, JPEG)", L"*.png;*.bmp;*.gif;*.jpg;*.jpeg"}, {L"Alle Dateien", L"*.*"}};
        d->SetTitle(L"Bild importieren");
        d->SetFileTypes(2, spec);
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    d->Release();
    return result;
}

std::wstring file_dialog(bool save, const wchar_t* title, const wchar_t* filter_name, const wchar_t* pattern, const wchar_t* def_ext, const std::wstring& name, HWND owner) {
    std::wstring result;
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d))))
        return result;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_FORCEFILESYSTEM);
    d->SetTitle(title);
    const COMDLG_FILTERSPEC spec[] = {{filter_name, pattern}};
    d->SetFileTypes(1, spec);
    if (def_ext) d->SetDefaultExtension(def_ext);
    if (save) d->SetFileName(name.c_str());
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    d->Release();
    return result;
}

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

bool export_png(const fs::path& to, const em::Image& img, const em::Palette& pal) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> st;
    if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromFilename(to.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(st.p, WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> bag;
    if (FAILED(enc->CreateNewFrame(&fr, &bag)) || FAILED(fr->Initialize(bag.p))) return false;
    WICPixelFormatGUID pf = GUID_WICPixelFormat8bppIndexed;
    if (FAILED(fr->SetSize(UINT(img.width), UINT(img.height))) || FAILED(fr->SetPixelFormat(&pf))) return false;
    WICColor cols[256];
    for (size_t i = 0; i < 256; i++) cols[i] = 0xFF000000u | uint32_t(pal.rgb[i][0]) << 16 | uint32_t(pal.rgb[i][1]) << 8 | pal.rgb[i][2];
    ComPtr<IWICPalette> wp;
    if (FAILED(f->CreatePalette(&wp)) || FAILED(wp->InitializeCustom(cols, 256)) || FAILED(fr->SetPalette(wp.p))) return false;
    std::vector<uint8_t> px = img.pixels;
    if (FAILED(fr->WritePixels(UINT(img.height), UINT(img.width), UINT(px.size()), px.data())) || FAILED(fr->Commit()) || FAILED(enc->Commit()))
        return false;
    return true;
}

// Any picture format WIC knows -> 0xAARRGGBB pixels
bool load_argb(const fs::path& from, std::vector<uint32_t>& out, int& w, int& h) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(from.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> fr;
    if (FAILED(dec->GetFrame(0, &fr))) return false;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(fr.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT uw = 0, uh = 0;
    if (FAILED(conv->GetSize(&uw, &uh)) || uw == 0 || uh == 0 || uw > 4096 || uh > 4096) return false;
    out.assign(size_t(uw) * uh, 0);
    if (FAILED(conv->CopyPixels(nullptr, uw * 4, UINT(out.size() * 4), reinterpret_cast<BYTE*>(out.data())))) return false;
    w = int(uw);
    h = int(uh);
    return true;
}

// ------------------------------------------------------------------------------------------- UI state

void update_scroll(App& a);
void sync_sliders(App& a);
void sync_scene(App& a);

bool pal_tab(const App& a) { return a.tab_index == 2; }
bool team_tab(const App& a) { return a.tab_index == 3; }
bool map_tab(const App& a) { return a.tab_index == 4; }
bool scene_tab(const App& a) { return a.tab_index == 5; }
bool sound_tab(const App& a) { return a.tab_index == 6; }

void refresh(App& a) {
    PixelEditor& e = *a.ed;
    const bool tt = team_tab(a), mt = map_tab(a), sc = scene_tab(a), sn = sound_tab(a), pt = pal_tab(a) || tt || mt || sc || sn;  // pt: a page without the pixel tools
    const int cur = sn ? a.sound->current() : sc ? (a.scene->loaded() ? 1 : -1) : mt ? a.map->current() : tt ? 23 : pt ? a.pal->current() : e.current();
    const bool dirty = sn ? a.sound->dirty() : sc ? a.scene->dirty() : mt ? a.map->dirty() : tt ? a.team->dirty() || a.crest_ed->dirty() : pt ? a.pal->dirty() : e.dirty();
    std::wstring title = cur < 0 ? L"OpenEM Editor" : sc ? L"TORE\\" + widen(a.scene->name()) + (dirty ? L" *" : L"") + L" – OpenEM Editor" : L"KB" + std::to_wstring(cur) + (dirty ? L" *" : L"") + L" – OpenEM Editor";
    SetWindowTextW(a.main, title.c_str());
    // toolbar
    SendMessage(a.toolbar, TB_CHECKBUTTON, ID_TOOL_PENCIL + int(e.tool()), TRUE);
    SendMessage(a.toolbar, TB_CHECKBUTTON, ID_VIEW_GRID, a.grid);
    for (int id = ID_TOOL_PENCIL; id <= ID_TOOL_SELECT; id++)  // hall maps: paint, fill and pick only
        SendMessage(a.toolbar, TB_ENABLEBUTTON, id, !pt || (mt && (id == ID_TOOL_PENCIL || id == ID_TOOL_FILL || id == ID_TOOL_PICKER)));
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_EDIT_UNDO, sn ? dirty : sc ? a.scene->can_undo() : mt ? a.map->can_undo() : pt ? dirty : e.can_undo());
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_EDIT_REDO, sn ? false : sc ? a.scene->can_redo() : mt ? a.map->can_redo() : !pt && e.can_redo());
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_FILE_SAVE, cur >= 0 && dirty);
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_VIEW_GRID, !pt || mt);
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_VIEW_FIT, !pt);
    // menu
    HMENU m = GetMenu(a.main);
    EnableMenuItem(m, ID_FILE_SAVE, MF_BYCOMMAND | (cur >= 0 && dirty ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_FILE_EXPORT, MF_BYCOMMAND | (!pt && e.current() >= 0 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_FILE_IMPORT, MF_BYCOMMAND | (!pt && e.current() >= 0 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_EDIT_UNDO, MF_BYCOMMAND | ((sn ? dirty : sc ? a.scene->can_undo() : mt ? a.map->can_undo() : pt ? dirty : e.can_undo()) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_EDIT_REDO, MF_BYCOMMAND | ((sn ? false : sc ? a.scene->can_redo() : mt ? a.map->can_redo() : !pt && e.can_redo()) ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(m, ID_VIEW_GRID, MF_BYCOMMAND | (a.grid ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(m, ID_TOOL_PENCIL, ID_TOOL_SELECT, ID_TOOL_PENCIL + int(e.tool()), MF_BYCOMMAND);
    CheckMenuItem(m, ID_VIEW_DEVIATION, MF_BYCOMMAND | (a.view_deviation ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, ID_VIEW_CHANGES, MF_BYCOMMAND | (a.view_changes ? MF_CHECKED : MF_UNCHECKED));
    for (UINT id : {ID_EDIT_COPY, ID_EDIT_CUT, ID_EDIT_PASTE, ID_EDIT_DELETE, ID_EDIT_SELECTALL, ID_EDIT_FLIPH, ID_EDIT_FLIPV, ID_EDIT_ROTATE})
        EnableMenuItem(m, id, MF_BYCOMMAND | (!pt && e.current() >= 0 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_EDIT_PASTE, MF_BYCOMMAND | (!pt && e.current() >= 0 && e.has_clipboard() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_FILE_EXPORTALL, MF_BYCOMMAND | (a.tab_index <= 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_FILE_IMPORTDIR, MF_BYCOMMAND | (a.tab_index <= 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_FILE_NEWSCENE, MF_BYCOMMAND | (sc && a.scene->loaded() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_VIEW_DEVIATION, MF_BYCOMMAND | (!pt ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_VIEW_CHANGES, MF_BYCOMMAND | (!pt ? MF_ENABLED : MF_GRAYED));
    // status bar
    SendMessage(a.status, SB_SETTEXT, 0, (LPARAM)widen(sn ? a.sound->status() : sc ? a.scene->status() : mt ? a.map->status() : tt ? a.team->status() : pt ? a.pal->status() : e.status()).c_str());
    std::wstring right = mt ? a.map_hover + L"   Zoom " + std::to_wstring(a.map_zoom) + L"×" : L"";
    if (sc) right = a.sc_hover;
    if (!pt) {
        if (a.hover_x >= 0) {
            right = L"x " + std::to_wstring(a.hover_x) + L"   y " + std::to_wstring(a.hover_y) + L"   Farbe " + std::to_wstring(a.hover_color);
            if (a.hover_cell >= 0) right = (e.kind() == PixelEditor::Kind::Tiles ? L"Kachel " : L"Sprite ") + std::to_wstring(a.hover_cell) + L"   " + right;
        }
        right += L"   Zoom " + std::to_wstring(a.zoom) + L"×";
    }
    SendMessage(a.status, SB_SETTEXT, 1, (LPARAM)right.c_str());
    for (int i = 0, n = int(SendMessage(a.psel, CB_GETCOUNT, 0, 0)); i < n; i++)
        if (int(SendMessage(a.psel, CB_GETITEMDATA, WPARAM(i), 0)) == e.palette_source()) SendMessage(a.psel, CB_SETCURSEL, WPARAM(i), 0);
    EnableWindow(a.psel, e.current() >= 0 && !e.has_own_palette());
    InvalidateRect(a.palette, nullptr, FALSE);
    InvalidateRect(a.pgrid, nullptr, FALSE);
    if (mt) InvalidateRect(a.mu.view, nullptr, FALSE);
    if (sc) sync_scene(a);
    if (a.shown_revision != e.revision()) {
        a.shown_revision = e.revision();
        InvalidateRect(a.canvas, nullptr, FALSE);
    }
}

bool team_save(App& a);
bool crest_confirm(App& a);
void load_crest(App& a);
void crest_autofit(App& a);
void load_team_controls(App& a);
void layout_team(App& a, int x, int y, int w, int h, int rx, int rw);
void show_team(App& a, bool on);
void layout_map(App& a, int x, int y, int w, int h, int rx, int rw);
void show_map(App& a, bool on);
void open_map(App& a, int number);
void sync_map_controls(App& a);
void open_scene(App& a, int index);
void show_scene(App& a, bool on);
void layout_scene(App& a, int x, int y, int w, int h, int rx, int rw);
void scene_stop(App& a);
void sync_sound(App& a);
void show_sound(App& a, bool on);
void layout_sound(App& a, int x, int y, int w, int h, int rx, int rw);
void open_sound(App& a, int number);
void sound_stop();
bool scene_route_command(App& a, UINT id, UINT code);
std::wstring file_dialog(bool save, const wchar_t* title, const wchar_t* filter_name, const wchar_t* pattern, const wchar_t* def_ext, const std::wstring& name, HWND owner);

bool confirm_discard(App& a) {
    const bool pt = pal_tab(a), tt = team_tab(a), mt = map_tab(a), sc = scene_tab(a), sn = sound_tab(a);
    if (tt && !crest_confirm(a)) return false;
    const bool dirty = sn ? a.sound->dirty() : sc ? a.scene->dirty() : mt ? a.map->dirty() : tt ? a.team->dirty() : pt ? a.pal->dirty() : a.ed->dirty();
    if (!dirty) return true;
    const int cur = sn ? a.sound->current() : mt ? a.map->current() : tt ? 23 : pt ? a.pal->current() : a.ed->current();
    std::wstring head = (sc ? L"TORE\\" + widen(a.scene->name()) : L"KB" + std::to_wstring(cur)) + L" wurde geändert.";
    TASKDIALOG_BUTTON buttons[] = {{101, L"Speichern"}, {102, L"Verwerfen"}};
    TASKDIALOGCONFIG cfg{sizeof cfg};
    cfg.hwndParent = a.main;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle = L"OpenEM Editor";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = head.c_str();
    cfg.pszContent = L"Sollen die Änderungen gespeichert werden?";
    cfg.pButtons = buttons;
    cfg.cButtons = 2;
    cfg.nDefaultButton = 101;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr))) return false;
    if (pressed == 101) return sn ? a.sound->save() : sc ? a.scene->save() : mt ? a.map->save() : tt ? team_save(a) : pt ? a.pal->save() : a.ed->save();
    if (pressed == 102 && sn) { a.sound->revert(); sync_sound(a); }
    else if (pressed == 102 && sc) { a.scene->load(a.scene->name()); sync_scene(a); }
    else if (pressed == 102 && mt) a.map->load(a.map->current());
    else if (pressed == 102 && tt) a.team->revert();
    else if (pressed == 102 && pt) a.pal->revert();
    return pressed == 102;
}

int current_number(const App& a) { return pal_tab(a) ? a.pal->current() : a.ed->current(); }

void select_in_list(App& a, int number) {
    int idx = -1;
    for (size_t i = 0; i < a.list_numbers.size(); i++)
        if (a.list_numbers[i] == number) idx = int(i);
    a.selecting = true;
    ListView_SetItemState(a.list, -1, 0, LVIS_SELECTED);
    if (idx >= 0) {
        ListView_SetItemState(a.list, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(a.list, idx, FALSE);
    }
    a.selecting = false;
}

std::wstring team_label(const App& a, int t) {
    return std::to_wstring(t < 64 ? t + 1 : t - 63) + L"  " + widen(a.team->club_name(t));
}

int team_group(int t) { return t < 12 ? 0 : t < 24 ? 1 : t < 36 ? 2 : t < 48 ? 3 : t < 64 ? 4 : 5; }

void fill_list(App& a) {
    a.selecting = true;
    ListView_DeleteAllItems(a.list);
    ListView_RemoveAllGroups(a.list);
    ListView_EnableGroupView(a.list, team_tab(a) || scene_tab(a));
    a.list_numbers.clear();
    auto add = [&](int number, const std::wstring& label, const std::wstring& size, int group = -1) {
        const int i = int(a.list_numbers.size());
        LVITEMW it{LVIF_TEXT | (group >= 0 ? LVIF_GROUPID : 0u)};
        it.iGroupId = group;
        it.iItem = i;
        it.pszText = const_cast<wchar_t*>(label.c_str());
        ListView_InsertItem(a.list, &it);
        ListView_SetItemText(a.list, i, 1, const_cast<wchar_t*>(size.c_str()));
        a.list_numbers.push_back(number);
    };
    LVCOLUMNW hc{LVCF_TEXT};
    hc.pszText = const_cast<wchar_t*>(team_tab(a) ? L"Mannschaft" : scene_tab(a) ? L"Szene" : L"Datei");
    ListView_SetColumn(a.list, 0, &hc);
    hc.pszText = const_cast<wchar_t*>(team_tab(a) ? L"Platz" : scene_tab(a) ? L"Datei" : L"Größe");
    ListView_SetColumn(a.list, 1, &hc);
    if (team_tab(a)) {
        const wchar_t* heads[] = {L"Liga 1 (Teams 1–12)", L"Liga 2 (Teams 13–24)", L"Liga 3 (Teams 25–36)", L"Weitere Vereine (Teams 37–48)",
                                  L"Nationalmannschaften (Teams 53–64)", L"Europapokal-Vereine"};
        for (int g = 0; g < 6; g++) {
            LVGROUP lg{sizeof lg, LVGF_HEADER | LVGF_GROUPID};
            lg.pszHeader = const_cast<wchar_t*>(heads[g]);
            lg.iGroupId = g;
            ListView_InsertGroup(a.list, -1, &lg);
        }
        for (int t = 0; t < TeamDoc::kAllTeams; t++) {
            if (!TeamDoc::editable(t)) continue;
            const int place = a.team->data(t).place;
            add(t, team_label(a, t), place >= 0 ? std::to_wstring(place) : L"–", team_group(t));
        }
    } else if (sound_tab(a)) {
        for (int n : SoundDoc::numbers()) add(n, SoundDoc::label(n), L"");
    } else if (scene_tab(a)) {
        const wchar_t* heads[] = {L"Heim greift an · Tor", L"Heim greift an · vergebene Chance", L"Gast greift an · Tor", L"Gast greift an · vergebene Chance"};
        for (int g = 0; g < 4; g++) {
            LVGROUP lg{sizeof lg, LVGF_HEADER | LVGF_GROUPID};
            lg.pszHeader = const_cast<wchar_t*>(heads[g]);
            lg.iGroupId = g;
            ListView_InsertGroup(a.list, -1, &lg);
        }
        a.sc_names = SceneDoc::scene_names(a.data);
        for (size_t i = 0; i < a.sc_names.size(); i++) {
            const std::string& n = a.sc_names[i];
            const size_t dot = n.find('.');
            const int group = (n[dot + 1] == 'h' ? 0 : 2) + (n[dot + 2] == 't' ? 0 : 1);
            std::wstring label = widen(n.substr(0, dot)) + L"  " + (n.size() > dot + 3 ? (n[dot + 3] == 'e' ? L"Strafschuss" : L"sechs Feldspieler") : L"Szene");
            add(int(i), label, widen(n), group);
        }
    } else if (map_tab(a)) {
        const wchar_t* names[] = {L"KB236  Standardhalle", L"KB242  Halle 1", L"KB243  Halle 2", L"KB244  Halle 3", L"KB245  Halle 4"};
        for (size_t i = 0; i < MapDoc::numbers().size(); i++) add(MapDoc::numbers()[i], names[i], L"42 × 72");
    } else if (pal_tab(a)) {
        add(21, L"KB21  Spielpalette", L"255 Farben");
        add(247, L"KB247  Torszenen", L"255 Farben");
    } else {
        for (const auto& f : a.ed->files()) {
            const bool image = f.kind == PixelEditor::Kind::Image;
            if (image != (a.tab_index == 0)) continue;
            add(f.number, widen(f.label) + (f.embedded ? L" ●" : L""), std::to_wstring(f.w) + L" × " + std::to_wstring(f.h));
        }
    }
    a.selecting = false;
}

void fit_zoom(App& a) {
    RECT r;
    GetClientRect(a.canvas, &r);
    const auto& img = a.ed->image();
    if (a.ed->current() < 0) return;
    const int m = a.px(24);
    a.zoom = std::clamp(std::min(int(r.right - m) / std::max(1, img.width), int(r.bottom - m) / std::max(1, img.height)), 1, 32);
    a.scroll_x = a.scroll_y = 0;
    update_scroll(a);
}

void open_image(App& a, int number) {
    if (sound_tab(a)) {
        if (number == a.sound->current()) return;
        if (!confirm_discard(a)) {
            select_in_list(a, a.sound->current());
            return;
        }
        open_sound(a, number);
        select_in_list(a, a.sound->current());
        refresh(a);
        return;
    }
    if (scene_tab(a)) {  // the number is an index into the list of scenes
        if (a.sc_names.empty() || number < 0 || size_t(number) >= a.sc_names.size() || a.sc_names[size_t(number)] == a.scene->name()) return;
        if (!confirm_discard(a)) {
            select_in_list(a, int(std::find(a.sc_names.begin(), a.sc_names.end(), a.scene->name()) - a.sc_names.begin()));
            return;
        }
        open_scene(a, number);
        refresh(a);
        return;
    }
    if (map_tab(a)) {
        if (number == a.map->current()) return;
        if (!confirm_discard(a)) {
            select_in_list(a, a.map->current());
            return;
        }
        open_map(a, number);
        select_in_list(a, a.map->current());
        refresh(a);
        return;
    }
    if (team_tab(a)) {  // the number is a team index; edits are applied live, nothing to confirm
        if (number != a.team_sel && !crest_confirm(a)) {
            select_in_list(a, a.team_sel);
            return;
        }
        a.team_sel = number;
        load_team_controls(a);
        refresh(a);
        return;
    }
    if (pal_tab(a)) {
        if (number == a.pal->current()) return;
        if (!confirm_discard(a)) {
            select_in_list(a, a.pal->current());
            return;
        }
        if (a.pal->load(number)) {
            a.pal_sel = std::min(a.pal_sel, PaletteDoc::kColors);
            sync_sliders(a);
        }
        select_in_list(a, a.pal->current());
        refresh(a);
        return;
    }
    if (number == a.ed->current()) return;
    if (!confirm_discard(a)) {
        select_in_list(a, a.ed->current());
        return;
    }
    if (a.ed->load(number)) fit_zoom(a);
    select_in_list(a, a.ed->current());
    refresh(a);
}

void set_zoom(App& a, int z) {
    a.zoom = std::clamp(z, 1, 32);
    update_scroll(a);
    InvalidateRect(a.canvas, nullptr, FALSE);
    refresh(a);
}

void sync_sliders(App& a) {
    const auto c = a.pal->get(a.pal_sel);
    const bool on = a.pal->current() >= 0 && a.pal_sel < PaletteDoc::kColors;
    a.setting_sliders = true;
    for (int i = 0; i < 3; i++) {
        SendMessage(a.track[i], TBM_SETPOS, TRUE, c[size_t(i)]);
        EnableWindow(a.track[i], on);
        const wchar_t* names[] = {L"Rot", L"Grün", L"Blau"};
        std::wstring t = std::wstring(names[i]) + L"  " + std::to_wstring(c[size_t(i)]);
        SetWindowTextW(a.tlabel[i], t.c_str());
    }
    std::wstring info = a.pal->current() < 0 ? L"Palette links wählen"
                        : a.pal_sel >= PaletteDoc::kColors ? L"Farbe 255 wird nicht gespeichert"
                                                           : L"Farbe " + std::to_wstring(a.pal_sel);
    SetWindowTextW(a.pinfo, info.c_str());
    a.setting_sliders = false;
}

void show_tab_controls(App& a) {
    const bool pt = pal_tab(a), tt = team_tab(a), mt = map_tab(a), sc = scene_tab(a), sn = sound_tab(a);
    ShowWindow(a.canvas, pt || tt || mt || sc || sn ? SW_HIDE : SW_SHOW);
    ShowWindow(a.palette, pt || tt || mt || sc || sn ? SW_HIDE : SW_SHOW);
    ShowWindow(a.psel, pt || tt || mt || sc || sn ? SW_HIDE : SW_SHOW);
    show_sound(a, sn);
    show_team(a, tt);
    show_map(a, mt);
    show_scene(a, sc);
    ShowWindow(a.pgrid, pt ? SW_SHOW : SW_HIDE);
    ShowWindow(a.pinfo, pt ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < 3; i++) {
        ShowWindow(a.track[i], pt ? SW_SHOW : SW_HIDE);
        ShowWindow(a.tlabel[i], pt ? SW_SHOW : SW_HIDE);
    }
}

void layout(App& a) {
    RECT r;
    GetClientRect(a.main, &r);
    SendMessage(a.toolbar, TB_AUTOSIZE, 0, 0);
    SendMessage(a.status, WM_SIZE, 0, 0);
    RECT tr, sr;
    GetWindowRect(a.toolbar, &tr);
    GetWindowRect(a.status, &sr);
    const int tab_h = a.px(30);
    const int top0 = tr.bottom - tr.top, bottom = r.bottom - (sr.bottom - sr.top);
    MoveWindow(a.tab, 0, top0, r.right, tab_h, TRUE);
    const int top = top0 + tab_h;
    const int lw = a.px(270), pw = a.px(250);
    MoveWindow(a.list, 0, top, lw, bottom - top, TRUE);
    MoveWindow(a.canvas, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, TRUE);
    MoveWindow(a.psel, r.right - pw + a.px(10), top + a.px(8), pw - a.px(20), a.px(300), TRUE);
    MoveWindow(a.palette, r.right - pw, top + a.px(40), pw, bottom - top - a.px(40), TRUE);
    // palette page: grid in the middle, sliders on the right
    MoveWindow(a.pgrid, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, TRUE);
    const int cx = r.right - pw + a.px(12), cw = pw - a.px(24);
    int y = top + a.px(14);
    MoveWindow(a.pinfo, cx, y, cw, a.px(20), TRUE);
    y += a.px(34);
    for (int i = 0; i < 3; i++) {
        MoveWindow(a.tlabel[i], cx, y, cw, a.px(20), TRUE);
        MoveWindow(a.track[i], cx, y + a.px(20), cw, a.px(32), TRUE);
        y += a.px(64);
    }
    layout_map(a, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, r.right - pw, pw);
    layout_team(a, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, r.right - pw, pw);
    layout_scene(a, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, r.right - pw, pw);
    layout_sound(a, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, r.right - pw, pw);
    const int w1 = a.px(260);
    int parts[2] = {std::max(w1, int(r.right) - w1), -1};
    SendMessage(a.status, SB_SETPARTS, 2, (LPARAM)parts);
    update_scroll(a);
}

void switch_tab(App& a, int index) {
    scene_stop(a);
    a.tab_index = index;
    a.hover_x = -1;
    fill_list(a);
    sound_stop();
    if (sound_tab(a)) {
        if (a.sound->current() < 0) open_sound(a, SoundDoc::numbers().front());
        select_in_list(a, a.sound->current());
        sync_sound(a);
    } else if (scene_tab(a)) {
        if (!a.scene->loaded() && !a.sc_names.empty()) open_scene(a, 0);
        select_in_list(a, a.scene->loaded() ? int(std::find(a.sc_names.begin(), a.sc_names.end(), a.scene->name()) - a.sc_names.begin()) : -1);
    } else if (map_tab(a)) {
        if (a.map->current() < 0) open_map(a, MapDoc::numbers().front());
        select_in_list(a, a.map->current());
        sync_map_controls(a);
    } else if (team_tab(a)) {
        select_in_list(a, a.team_sel);
        load_team_controls(a);
    } else if (pal_tab(a)) {
        if (a.pal->current() < 0) a.pal->load(21);
        sync_sliders(a);
        select_in_list(a, a.pal->current());
    } else {
        if (a.ed->current() >= 0 && (a.ed->kind() == PixelEditor::Kind::Image) != (a.tab_index == 0)) a.ed->close();
        select_in_list(a, a.ed->current());
    }
    show_tab_controls(a);
    refresh(a);
}

// ---------------------------------------------------------------------------------------------- canvas

struct Geo {
    int ox, oy, iw, ih, total_w, total_h, cw, ch;
};

Geo geometry(App& a) {
    RECT r;
    GetClientRect(a.canvas, &r);
    Geo g{};
    const auto& img = a.ed->image();
    g.cw = r.right;
    g.ch = r.bottom;
    g.iw = img.width * a.zoom;
    g.ih = img.height * a.zoom;
    const int m = a.px(12);
    g.total_w = g.iw + 2 * m;
    g.total_h = g.ih + 2 * m;
    g.ox = g.total_w <= g.cw ? (g.cw - g.iw) / 2 : m - a.scroll_x;
    g.oy = g.total_h <= g.ch ? (g.ch - g.ih) / 2 : m - a.scroll_y;
    return g;
}

void update_scroll(App& a) {
    if (!a.canvas) return;
    if (a.ed->current() < 0) {
        ShowScrollBar(a.canvas, SB_BOTH, FALSE);
        return;
    }
    for (int pass = 0; pass < 2; pass++) {  // adding one scroll bar may make the other necessary
        Geo g = geometry(a);
        a.scroll_x = std::clamp(a.scroll_x, 0, std::max(0, g.total_w - g.cw));
        a.scroll_y = std::clamp(a.scroll_y, 0, std::max(0, g.total_h - g.ch));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0;
        si.nMax = g.total_w - 1;
        si.nPage = UINT(g.cw);
        si.nPos = a.scroll_x;
        SetScrollInfo(a.canvas, SB_HORZ, &si, TRUE);
        si.nMax = g.total_h - 1;
        si.nPage = UINT(g.ch);
        si.nPos = a.scroll_y;
        SetScrollInfo(a.canvas, SB_VERT, &si, TRUE);
    }
}

void paint_canvas(App& a, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr;
    GetClientRect(hwnd, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(58, 60, 64));
    FillRect(mem, &cr, bg);
    DeleteObject(bg);
    PixelEditor& e = *a.ed;
    if (e.current() >= 0) {
        const auto& img = e.image();
        const auto& pal = e.palette();
        a.bits.resize(img.pixels.size());
        const std::vector<uint8_t>* dev = a.view_deviation && e.deviation().size() == img.pixels.size() ? &e.deviation() : nullptr;
        const std::vector<uint8_t>* chg = nullptr;
        if (a.view_changes) {
            if (a.changes_rev != e.revision() || a.changes_for != e.current()) {
                a.changes_mask = e.changed_pixels();
                a.changes_rev = e.revision();
                a.changes_for = e.current();
            }
            if (a.changes_mask.size() == img.pixels.size()) chg = &a.changes_mask;
        }
        const auto tint = [](uint32_t c, uint32_t t, int pct) {
            uint32_t out = 0;
            for (int sh = 0; sh <= 16; sh += 8) out |= (((c >> sh & 255) * uint32_t(100 - pct) + (t >> sh & 255) * uint32_t(pct)) / 100) << sh;
            return out;
        };
        for (size_t i = 0; i < a.bits.size(); i++) {
            const auto& c = pal.rgb[img.pixels[i]];
            a.bits[i] = uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
            if (dev && (*dev)[i]) a.bits[i] = tint(a.bits[i], 0xFF2020, 65);      // import: colour not in the palette
            if (chg && (*chg)[i]) a.bits[i] = tint(a.bits[i], 0xFFD000, 55);      // differs from the original
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = img.width;
        bi.bmiHeader.biHeight = -img.height;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        Geo g = geometry(a);
        SetStretchBltMode(mem, COLORONCOLOR);
        StretchDIBits(mem, g.ox, g.oy, g.iw, g.ih, 0, 0, img.width, img.height, a.bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        if (a.grid && a.zoom >= 6) {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(110, 110, 118));
            HGDIOBJ op = SelectObject(mem, pen);
            for (int x = 0; x <= img.width; x++) {
                MoveToEx(mem, g.ox + x * a.zoom, g.oy, nullptr);
                LineTo(mem, g.ox + x * a.zoom, g.oy + g.ih);
            }
            for (int y = 0; y <= img.height; y++) {
                MoveToEx(mem, g.ox, g.oy + y * a.zoom, nullptr);
                LineTo(mem, g.ox + g.iw, g.oy + y * a.zoom);
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        if (const int cell = e.cell_size()) {  // tile or sprite boundaries
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(232, 160, 40));
            HGDIOBJ op = SelectObject(mem, pen);
            for (int x = 0; x <= img.width; x += cell) {
                MoveToEx(mem, g.ox + x * a.zoom, g.oy, nullptr);
                LineTo(mem, g.ox + x * a.zoom, g.oy + g.ih);
            }
            for (int y = 0; y <= img.height; y += cell) {
                MoveToEx(mem, g.ox, g.oy + y * a.zoom, nullptr);
                LineTo(mem, g.ox + g.iw, g.oy + y * a.zoom);
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        RECT fr{g.ox - 1, g.oy - 1, g.ox + g.iw + 1, g.oy + g.ih + 1};
        FrameRect(mem, &fr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (e.has_selection()) {  // black line with a dotted white one on top
            const auto sel = e.selection();
            const int x0 = g.ox + sel.x0 * a.zoom, y0 = g.oy + sel.y0 * a.zoom, x1 = g.ox + (sel.x1 + 1) * a.zoom, y1 = g.oy + (sel.y1 + 1) * a.zoom;
            HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
            HPEN black = CreatePen(PS_SOLID, 1, RGB(0, 0, 0)), white = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
            HGDIOBJ op = SelectObject(mem, black);
            Rectangle(mem, x0, y0, x1 + 1, y1 + 1);
            SetBkMode(mem, TRANSPARENT);
            SelectObject(mem, white);
            Rectangle(mem, x0, y0, x1 + 1, y1 + 1);
            SelectObject(mem, op);
            SelectObject(mem, ob);
            DeleteObject(black);
            DeleteObject(white);
        }
    } else {
        SelectObject(mem, a.font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(190, 190, 195));
        DrawTextW(mem, L"Links ein Bild wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

bool to_image(App& a, int mx, int my, int& ix, int& iy) {
    Geo g = geometry(a);
    ix = floordiv(mx - g.ox, a.zoom);
    iy = floordiv(my - g.oy, a.zoom);
    const auto& img = a.ed->image();
    return a.ed->current() >= 0 && ix >= 0 && iy >= 0 && ix < img.width && iy < img.height;
}

LRESULT CALLBACK canvas_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_canvas(a, h); return 0;
        case WM_SIZE: update_scroll(a); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            int ix, iy;
            if (to_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy)) {
                SetCapture(h);
                a.ed->stroke_begin(ix, iy);
                refresh(a);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int ix, iy;
            const bool in = to_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy);
            a.hover_x = in ? ix : -1;
            a.hover_y = in ? iy : -1;
            a.hover_color = in ? a.ed->pick(ix, iy) : -1;
            a.hover_cell = in && a.ed->cell_size() ? iy / a.ed->cell_size() * a.ed->cell_columns() + ix / a.ed->cell_size() : -1;
            if (a.ed->drawing() && (wp & MK_LBUTTON)) {
                Geo g = geometry(a);
                a.ed->stroke_move(floordiv(GET_X_LPARAM(lp) - g.ox, a.zoom), floordiv(GET_Y_LPARAM(lp) - g.oy, a.zoom));
            }
            refresh(a);
            return 0;
        }
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            a.ed->stroke_end();
            refresh(a);
            return 0;
        case WM_RBUTTONDOWN: {
            int ix, iy;
            if (to_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy)) {
                a.ed->set_color(a.ed->pick(ix, iy));
                refresh(a);
            }
            return 0;
        }
        case WM_MOUSELEAVE: a.hover_x = -1; refresh(a); return 0;
        case WM_MOUSEWHEEL: {
            const int d = GET_WHEEL_DELTA_WPARAM(wp);
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
                set_zoom(a, a.zoom + (d > 0 ? 1 : -1));
            } else if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) {
                a.scroll_x -= d / 2;
                update_scroll(a);
                InvalidateRect(h, nullptr, FALSE);
            } else {
                a.scroll_y -= d / 2;
                update_scroll(a);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_HSCROLL:
        case WM_VSCROLL: {
            const bool horz = msg == WM_HSCROLL;
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, horz ? SB_HORZ : SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= a.px(16); break;
                case SB_LINEDOWN: pos += a.px(16); break;
                case SB_PAGEUP: pos -= int(si.nPage); break;
                case SB_PAGEDOWN: pos += int(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            (horz ? a.scroll_x : a.scroll_y) = pos;
            update_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// --------------------------------------------------------------------------------------------- palette

struct PaletteGeo {
    int x, y, cell;
};

PaletteGeo palette_geo(App& a, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    const int m = a.px(10), text = a.px(52);
    const int size = std::max(16, std::min(int(r.right) - 2 * m, int(r.bottom) - 2 * m - text));
    return {m, m, size / 16};
}

void paint_palette(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &cr, GetSysColorBrush(COLOR_BTNFACE));
    PixelEditor& e = *a.ed;
    PaletteGeo g = palette_geo(a, h);
    const auto& pal = e.palette();
    for (int i = 0; i < 256; i++) {
        const auto& c = pal.rgb[size_t(i)];
        HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
        RECT r{g.x + (i % 16) * g.cell, g.y + (i / 16) * g.cell, g.x + (i % 16 + 1) * g.cell, g.y + (i / 16 + 1) * g.cell};
        FillRect(mem, &r, b);
        DeleteObject(b);
    }
    // selection frame: black outside, white inside
    const int s = e.color();
    RECT sr{g.x + (s % 16) * g.cell, g.y + (s / 16) * g.cell, g.x + (s % 16 + 1) * g.cell, g.y + (s / 16 + 1) * g.cell};
    FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
    InflateRect(&sr, 1, 1);
    FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
    InflateRect(&sr, -2, -2);
    FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
    // current colour
    const int ty = g.y + g.cell * 16 + a.px(12);
    const auto& c = pal.rgb[size_t(s)];
    HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
    RECT sw{g.x, ty, g.x + a.px(36), ty + a.px(36)};
    FillRect(mem, &sw, b);
    DeleteObject(b);
    FrameRect(mem, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(mem, a.font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
    std::wstring t1 = L"Farbe " + std::to_wstring(s), t2 = L"RGB " + std::to_wstring(c[0]) + L", " + std::to_wstring(c[1]) + L", " + std::to_wstring(c[2]);
    TextOutW(mem, g.x + a.px(46), ty, t1.c_str(), int(t1.size()));
    TextOutW(mem, g.x + a.px(46), ty + a.px(18), t2.c_str(), int(t2.size()));
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK palette_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_palette(a, h); return 0;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE: {
            PaletteGeo g = palette_geo(a, h);
            const int cx = (GET_X_LPARAM(lp) - g.x) / g.cell, cy = (GET_Y_LPARAM(lp) - g.y) / g.cell;
            if (GET_X_LPARAM(lp) >= g.x && GET_Y_LPARAM(lp) >= g.y && cx >= 0 && cy >= 0 && cx < 16 && cy < 16) {
                if (msg == WM_LBUTTONDOWN) {
                    a.ed->set_color(cy * 16 + cx);
                    refresh(a);
                } else {
                    const auto& c = a.ed->palette().rgb[size_t(cy * 16 + cx)];
                    std::wstring s = L"Farbe " + std::to_wstring(cy * 16 + cx) + L"   RGB " + std::to_wstring(c[0]) + L", " + std::to_wstring(c[1]) + L", " + std::to_wstring(c[2]);
                    SendMessage(a.status, SB_SETTEXT, 0, (LPARAM)s.c_str());
                }
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// ------------------------------------------------------------------------------------------- team editor

std::string narrow(const wchar_t* w, int n = -1) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(0, len)), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), len, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::wstring window_text(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}

COLORREF jersey_color(int r, int g, int b) {  // same scaling as the DAC: six bits, widened to eight
    auto c = [](int n) { const int v = n * 4; return (v << 2) | (v >> 4); };
    return RGB(c(r), c(g), c(b));
}

void create_team_controls(App& a) {
    TeamUi& u = a.tu;
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int id) {
        HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | style, 0, 0, 0, 0, a.main, (HMENU)UINT_PTR(id), a.inst, nullptr);
        u.all.push_back(w);
        return w;
    };
    const DWORD edit = ES_AUTOHSCROLL | WS_TABSTOP | ES_UPPERCASE;
    u.lbl_club = make(L"STATIC", L"Verein", 0, 0, 0);
    u.club = make(L"EDIT", L"", edit, WS_EX_CLIENTEDGE, IDC_CLUB);
    SendMessage(u.club, EM_LIMITTEXT, TeamDoc::kClubLimit, 0);
    u.lbl_players = make(L"STATIC", L"Spieler (Ausländer = Haken)", 0, 0, 0);
    for (int i = 0; i < 25; i++) {
        u.name[i] = make(L"EDIT", L"", edit, WS_EX_CLIENTEDGE, IDC_NAME0 + i);
        SendMessage(u.name[i], EM_LIMITTEXT, TeamDoc::kNameLimit, 0);
        u.foreign[i] = make(L"BUTTON", L"Ausl.", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_FOR0 + i);
    }
    auto numeric = [&](HWND& e, HWND& ud, int id, int lo, int hi) {
        e = make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, id);
        ud = make(UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0);
        SendMessage(ud, UDM_SETBUDDY, (WPARAM)e, 0);
        SendMessage(ud, UDM_SETRANGE32, lo, hi);
    };
    u.lbl_crest = make(L"STATIC", L"Wappen (0–63)", 0, 0, 0);
    numeric(u.crest, u.crest_ud, IDC_CREST, 0, 63);
    u.lbl_place = make(L"STATIC", L"Platz in der Liga (1–12)", 0, 0, 0);
    numeric(u.place, u.place_ud, IDC_PLACE, 0, 12);
    const wchar_t* sn[] = {L"Stärke 1", L"Stärke 2", L"Stärke 3"};
    for (int i = 0; i < 3; i++) {
        u.lbl_str[i] = make(L"STATIC", sn[i], 0, 0, 0);
        numeric(u.str[i], u.str_ud[i], IDC_STR0 + i, 1, 99);
    }
    u.lbl_jersey = make(L"STATIC", L"Trikotfarbe der Torszenen", 0, 0, 0);
    for (int i = 0; i < 3; i++) {
        u.jer_lbl[i] = make(L"STATIC", L"", 0, 0, 0);
        u.jer[i] = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, IDC_JER0 + i);
        SendMessage(u.jer[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 15));
    }
    u.preview = make(L"STATIC", L"", SS_OWNERDRAW, WS_EX_CLIENTEDGE, 0);
    u.lbl_swap = make(L"STATIC", L"Mannschaft tauschen mit", 0, 0, 0);
    u.swap_combo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SWAP_COMBO);
    u.swap_btn = make(L"BUTTON", L"Tauschen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SWAP_BTN);
    u.crest_view = CreateWindowExW(WS_EX_CLIENTEDGE, kCrestClass, nullptr, WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 0, 0, a.main, nullptr, a.inst, nullptr);
    u.all.push_back(u.crest_view);
    u.cr_pen = make(L"BUTTON", L"Stift", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CREST_PEN);
    u.cr_fill = make(L"BUTTON", L"Füllen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CREST_FILL);
    u.cr_undo = make(L"BUTTON", L"Rückgängig", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CREST_UNDO);
    u.cr_save = make(L"BUTTON", L"Wappen speichern", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CREST_SAVE);
    u.cr_zout = make(L"BUTTON", L"Zoom −", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CREST_ZOUT);
    u.cr_zin = make(L"BUTTON", L"Zoom +", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CREST_ZIN);
    for (HWND w : u.all) SendMessage(w, WM_SETFONT, (WPARAM)a.font, TRUE);
}

void show_team(App& a, bool on) {
    for (HWND w : a.tu.all) ShowWindow(w, on ? SW_SHOW : SW_HIDE);
}

void layout_team(App& a, int x, int y, int w, int h, int rx, int rw) {
    TeamUi& u = a.tu;
    if (!u.club) return;
    const int m = a.px(14), rowh = a.px(28), eh = a.px(23), prow = a.px(25);
    MoveWindow(u.lbl_club, x + m, y + m + a.px(3), a.px(50), eh, TRUE);
    MoveWindow(u.club, x + m + a.px(54), y + m, a.px(280), eh, TRUE);
    MoveWindow(u.lbl_players, x + m, y + m + rowh + a.px(8), a.px(300), eh, TRUE);
    const int colw = (w - 3 * m) / 2, top = y + m + 2 * rowh + a.px(8);
    for (int i = 0; i < 25; i++) {
        const int col = i < 13 ? 0 : 1, row = i < 13 ? i : i - 13;
        const int cx = x + m + col * (colw + m), cy = top + row * prow;
        MoveWindow(u.name[i], cx, cy, colw - a.px(64), eh, TRUE);
        MoveWindow(u.foreign[i], cx + colw - a.px(58), cy, a.px(58), eh, TRUE);
    }
    {   // crest editor under the players
        const int cy0 = top + 13 * prow + a.px(8), ch = std::max(a.px(120), y + h - cy0 - a.px(6));
        const int bw = a.px(150), cwid = std::max(a.px(300), w - 2 * m - bw - a.px(12));
        MoveWindow(u.crest_view, x + m, cy0, cwid, ch, TRUE);
        const int bx = x + m + cwid + a.px(12), half = (bw - a.px(6)) / 2;
        MoveWindow(u.cr_pen, bx, cy0, bw, eh, TRUE);
        MoveWindow(u.cr_fill, bx, cy0 + rowh, bw, eh, TRUE);
        MoveWindow(u.cr_undo, bx, cy0 + 2 * rowh, bw, eh, TRUE);
        MoveWindow(u.cr_zout, bx, cy0 + 3 * rowh, half, eh, TRUE);
        MoveWindow(u.cr_zin, bx + half + a.px(6), cy0 + 3 * rowh, half, eh, TRUE);
        MoveWindow(u.cr_save, bx, cy0 + 4 * rowh + a.px(8), bw, eh + 2, TRUE);
        crest_autofit(a);
    }
    // right column
    const int cx = rx + a.px(12), cw = rw - a.px(24), nw = a.px(70);
    int cy = y + m;
    MoveWindow(u.preview, cx, cy, cw, a.px(70), TRUE);
    cy += a.px(82);
    auto row = [&](HWND lbl, HWND e, HWND ud) {
        MoveWindow(lbl, cx, cy + a.px(3), cw - nw - a.px(8), eh, TRUE);
        MoveWindow(e, cx + cw - nw, cy, nw, eh, TRUE);
        SendMessage(ud, UDM_SETBUDDY, (WPARAM)e, 0);  // re-attach: the arrows follow the edit
        ShowWindow(ud, team_tab(a) ? SW_SHOW : SW_HIDE);
        cy += rowh;
    };
    row(u.lbl_crest, u.crest, u.crest_ud);
    row(u.lbl_place, u.place, u.place_ud);
    for (int i = 0; i < 3; i++) row(u.lbl_str[i], u.str[i], u.str_ud[i]);
    cy += a.px(6);
    MoveWindow(u.lbl_jersey, cx, cy, cw, eh, TRUE);
    cy += a.px(22);
    for (int i = 0; i < 3; i++) {
        MoveWindow(u.jer_lbl[i], cx, cy + a.px(4), a.px(60), eh, TRUE);
        MoveWindow(u.jer[i], cx + a.px(60), cy, cw - a.px(60), a.px(28), TRUE);
        cy += a.px(30);
    }
    cy += a.px(10);
    MoveWindow(u.lbl_swap, cx, cy, cw, eh, TRUE);
    cy += a.px(22);
    MoveWindow(u.swap_combo, cx, cy, cw - a.px(86), a.px(300), TRUE);
    MoveWindow(u.swap_btn, cx + cw - a.px(80), cy - 1, a.px(80), eh + 2, TRUE);
}

void fill_swap_combo(App& a) {
    HWND c = a.tu.swap_combo;
    SendMessage(c, CB_RESETCONTENT, 0, 0);
    for (int t = 0; t < TeamDoc::kAllTeams; t++) {
        if (!TeamDoc::editable(t) || t == a.team_sel || TeamDoc::has_players(t) != TeamDoc::has_players(a.team_sel)) continue;
        const std::wstring s = std::to_wstring(t + 1) + L"  " + widen(a.team->club_name(t));
        const LRESULT i = SendMessage(c, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        SendMessage(c, CB_SETITEMDATA, WPARAM(i), t);
    }
    SendMessage(c, CB_SETCURSEL, 0, 0);
}

void update_team_labels(App& a) {
    const wchar_t* n[] = {L"Rot", L"Grün", L"Blau"};
    for (int i = 0; i < 3; i++) {
        const int v = int(SendMessage(a.tu.jer[i], TBM_GETPOS, 0, 0));
        SetWindowTextW(a.tu.jer_lbl[i], (std::wstring(n[i]) + L" " + std::to_wstring(v)).c_str());
    }
    InvalidateRect(a.tu.preview, nullptr, FALSE);
}

void load_team_controls(App& a) {
    if (!a.team || !a.tu.club) return;
    TeamUi& u = a.tu;
    const int t = a.team_sel;
    const em::TeamData& d = a.team->data(t);
    a.loading_team = true;
    const bool players = TeamDoc::has_players(t);
    SetWindowTextW(u.club, widen(a.team->club_name(t)).c_str());
    for (int i = 0; i < 25; i++) {
        SetWindowTextW(u.name[i], players ? widen(a.team->player_name(t, i)).c_str() : L"");
        SendMessage(u.foreign[i], BM_SETCHECK, players && a.team->foreign(t, i) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(u.name[i], players);
        EnableWindow(u.foreign[i], players);
    }
    EnableWindow(u.crest, players);
    EnableWindow(u.crest_ud, players);
    SetWindowTextW(u.crest, std::to_wstring(std::max(0, d.crest)).c_str());
    SetWindowTextW(u.place, std::to_wstring(std::max(0, d.place)).c_str());
    EnableWindow(u.place, d.place >= 0);
    EnableWindow(u.place_ud, d.place >= 0);
    load_crest(a);
    for (int i = 0; i < 3; i++) SetWindowTextW(u.str[i], std::to_wstring(d.strength[size_t(i)]).c_str());
    const auto rgb = TeamDoc::jersey_rgb(d);
    for (int i = 0; i < 3; i++) SendMessage(u.jer[i], TBM_SETPOS, TRUE, rgb[size_t(i)]);
    update_team_labels(a);
    fill_swap_combo(a);
    a.loading_team = false;
}

bool team_save(App& a) {
    if (a.crest_ed->dirty() && !a.crest_ed->save()) return false;
    if (!a.team->dirty()) return true;
    const auto dups = a.team->duplicate_names();
    if (!dups.empty()) {
        std::wstring list;
        for (size_t i = 0; i < dups.size() && i < 8; i++) list += (i ? L", " : L"") + widen(dups[i]);
        if (dups.size() > 8) list += L", …";
        std::wstring text = L"Diese Namen kommen mehrfach vor: " + list + L".\nDas Spiel behandelt gleiche Namen als dieselbe Person. Trotzdem speichern?";
        if (MessageBoxW(a.main, text.c_str(), L"Doppelte Namen", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    }
    return a.team->save();
}

int list_row_of(const App& a, int team) {
    return int(std::find(a.list_numbers.begin(), a.list_numbers.end(), team) - a.list_numbers.begin());
}

void team_edit_changed(App& a, int id) {
    if (a.loading_team || !a.team) return;
    TeamUi& u = a.tu;
    const int t = a.team_sel;
    auto number = [&](HWND e, int dflt) {
        const std::wstring s = window_text(e);
        return s.empty() ? dflt : _wtoi(s.c_str());
    };
    if (id == IDC_CLUB) {
        if (a.team->set_club_name(t, narrow(window_text(u.club).c_str()))) {
            const std::wstring label = team_label(a, t);
            ListView_SetItemText(a.list, list_row_of(a, t), 0, const_cast<wchar_t*>(label.c_str()));
        }
    } else if (id >= IDC_NAME0 && id < IDC_NAME0 + 25) {
        a.team->set_player_name(t, id - IDC_NAME0, narrow(window_text(u.name[id - IDC_NAME0]).c_str()));
    } else if (id == IDC_CREST) {
        const int nc = std::clamp(number(u.crest, a.team->data(t).crest), 0, 63);
        if (nc != a.team->data(t).crest && !crest_confirm(a)) {  // cancelled: back to the old crest number
            a.loading_team = true;
            SetWindowTextW(u.crest, std::to_wstring(a.team->data(t).crest).c_str());
            a.loading_team = false;
            return;
        }
        a.team->set_crest(t, nc);
        load_crest(a);
    } else if (id == IDC_PLACE) {
        a.team->set_place(t, number(u.place, 0));
        const int p = a.team->data(t).place;
        const std::wstring text = p >= 0 ? std::to_wstring(p) : L"–";
        ListView_SetItemText(a.list, list_row_of(a, t), 1, const_cast<wchar_t*>(text.c_str()));
    } else if (id >= IDC_STR0 && id < IDC_STR0 + 3) {
        a.team->set_strength(t, id - IDC_STR0, number(u.str[id - IDC_STR0], a.team->data(t).strength[size_t(id - IDC_STR0)]));
    }
    refresh(a);
}

void team_jersey_changed(App& a) {
    if (a.loading_team || !a.team) return;
    const int r = int(SendMessage(a.tu.jer[0], TBM_GETPOS, 0, 0)), g = int(SendMessage(a.tu.jer[1], TBM_GETPOS, 0, 0)),
              b = int(SendMessage(a.tu.jer[2], TBM_GETPOS, 0, 0));
    a.team->set_jersey(a.team_sel, r, g, b);
    update_team_labels(a);
    refresh(a);
}

void team_swap(App& a) {
    const LRESULT i = SendMessage(a.tu.swap_combo, CB_GETCURSEL, 0, 0);
    if (i < 0) return;
    const int other = int(SendMessage(a.tu.swap_combo, CB_GETITEMDATA, WPARAM(i), 0));
    a.team->swap_teams(a.team_sel, other);
    fill_list(a);
    select_in_list(a, a.team_sel);
    load_team_controls(a);
    refresh(a);
}

void paint_team_preview(App& a, DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    if (!a.team) return;
    const em::TeamData& d = a.team->data(a.team_sel);
    // jersey colour as the goal scenes show it
    const auto rgb = TeamDoc::jersey_rgb(d);
    HBRUSH b = CreateSolidBrush(jersey_color(rgb[0], rgb[1], rgb[2]));
    RECT sw{r.right - a.px(64), r.top + a.px(8), r.right - a.px(10), r.top + a.px(62)};
    FillRect(dc, &sw, b);
    DeleteObject(b);
    FrameRect(dc, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(dc, a.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    TextOutW(dc, sw.left, sw.bottom + a.px(4), L"Trikot", 6);
}

// ------------------------------------------------------------------------------------------ crest editor
// A small pixel editor for the crest of the selected team (KB80 + crest), embedded in the team page.
// The picture has its own zoom (2x..16x) and scroll bars; the palette stays at the right edge.

struct CrestGeo {
    int x, y, zoom, view_w, view_h, pal_x, pal_y, cell;
};

CrestGeo crest_geo(App& a) {
    RECT r;
    GetClientRect(a.tu.crest_view, &r);
    const auto& img = a.crest_ed->image();
    CrestGeo g{};
    g.zoom = a.px(a.crest_zoom);
    g.cell = a.px(8);
    const int m = a.px(6);
    g.pal_x = std::max(m, int(r.right) - 16 * g.cell - m);
    g.pal_y = m;
    g.view_w = std::max(1, g.pal_x - a.px(10));
    g.view_h = int(r.bottom);
    const int iw = img.width * g.zoom, ih = img.height * g.zoom;
    g.x = iw + 2 * m <= g.view_w ? m + (g.view_w - 2 * m - iw) / 2 : m - a.cscroll_x;
    g.y = ih + 2 * m <= g.view_h ? m + (g.view_h - 2 * m - ih) / 2 : m - a.cscroll_y;
    return g;
}

void update_crest_scroll(App& a);

void crest_autofit(App& a) {
    if (!a.crest_auto || !a.tu.crest_view || !a.crest_ed || a.crest_ed->current() < 0) return;
    RECT r;
    GetClientRect(a.tu.crest_view, &r);
    const auto& img = a.crest_ed->image();
    const int m = a.px(6), view_w = int(r.right) - 16 * a.px(8) - m - a.px(10) - 2 * m, view_h = int(r.bottom) - 2 * m;
    const int device = std::min(view_w / std::max(1, img.width), view_h / std::max(1, img.height));
    a.crest_zoom = std::clamp(device * 96 / int(a.dpi), 2, 16);
    update_crest_scroll(a);
}

void update_crest_scroll(App& a) {
    HWND h = a.tu.crest_view;
    if (!h) return;
    if (a.crest_ed->current() < 0) {
        ShowScrollBar(h, SB_BOTH, FALSE);
        return;
    }
    const auto& img = a.crest_ed->image();
    for (int pass = 0; pass < 2; pass++) {
        CrestGeo g = crest_geo(a);
        const int m = a.px(6), tw = img.width * g.zoom + 2 * m, th = img.height * g.zoom + 2 * m;
        a.cscroll_x = std::clamp(a.cscroll_x, 0, std::max(0, tw - g.view_w));
        a.cscroll_y = std::clamp(a.cscroll_y, 0, std::max(0, th - g.view_h));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0;
        si.nMax = tw - 1;
        si.nPage = UINT(g.view_w);
        si.nPos = a.cscroll_x;
        SetScrollInfo(h, SB_HORZ, &si, TRUE);
        si.nMax = th - 1;
        si.nPage = UINT(g.view_h);
        si.nPos = a.cscroll_y;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
    }
}

void set_crest_zoom(App& a, int z) {
    a.crest_auto = false;
    a.crest_zoom = std::clamp(z, 2, 16);
    update_crest_scroll(a);
    InvalidateRect(a.tu.crest_view, nullptr, FALSE);
}

void paint_crest(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &cr, GetSysColorBrush(COLOR_WINDOW));
    SelectObject(mem, a.font);
    SetBkMode(mem, TRANSPARENT);
    PixelEditor& e = *a.crest_ed;
    if (e.current() >= 0) {
        const CrestGeo g = crest_geo(a);
        const auto& img = e.image();
        const auto& pal = e.palette();
        std::vector<uint32_t> bits(img.pixels.size());
        for (size_t i = 0; i < bits.size(); i++) {
            const auto& c = pal.rgb[img.pixels[i]];
            bits[i] = uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = img.width;
        bi.bmiHeader.biHeight = -img.height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SaveDC(mem);
        IntersectClipRect(mem, 0, 0, g.view_w, g.view_h);  // the picture scrolls inside its viewport
        SetStretchBltMode(mem, COLORONCOLOR);
        StretchDIBits(mem, g.x, g.y, img.width * g.zoom, img.height * g.zoom, 0, 0, img.width, img.height, bits.data(), &bi,
                      DIB_RGB_COLORS, SRCCOPY);
        if (g.zoom >= 4) {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 158));
            HGDIOBJ op = SelectObject(mem, pen);
            for (int x = 0; x <= img.width; x++) {
                MoveToEx(mem, g.x + x * g.zoom, g.y, nullptr);
                LineTo(mem, g.x + x * g.zoom, g.y + img.height * g.zoom);
            }
            for (int y = 0; y <= img.height; y++) {
                MoveToEx(mem, g.x, g.y + y * g.zoom, nullptr);
                LineTo(mem, g.x + img.width * g.zoom, g.y + y * g.zoom);
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        RestoreDC(mem, -1);
        for (int i = 0; i < 256; i++) {
            const auto& c = pal.rgb[size_t(i)];
            HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
            RECT r{g.pal_x + (i % 16) * g.cell, g.pal_y + (i / 16) * g.cell, g.pal_x + (i % 16 + 1) * g.cell, g.pal_y + (i / 16 + 1) * g.cell};
            FillRect(mem, &r, b);
            DeleteObject(b);
        }
        const int s = e.color();
        RECT sr{g.pal_x + (s % 16) * g.cell, g.pal_y + (s / 16) * g.cell, g.pal_x + (s % 16 + 1) * g.cell, g.pal_y + (s / 16 + 1) * g.cell};
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
        InflateRect(&sr, 1, 1);
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(mem, GetSysColor(COLOR_GRAYTEXT));
        std::wstring t = L"Wappen " + std::to_wstring(e.current() - 80) + L" (KB" + std::to_wstring(e.current()) + L")";
        std::wstring t2 = L"Zoom " + std::to_wstring(a.crest_zoom) + L"×" + (e.dirty() ? L"  – geändert" : L"");
        TextOutW(mem, g.pal_x, g.pal_y + 16 * g.cell + a.px(6), t.c_str(), int(t.size()));
        TextOutW(mem, g.pal_x, g.pal_y + 16 * g.cell + a.px(24), t2.c_str(), int(t2.size()));
    } else {
        SetTextColor(mem, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(mem, L"Europapokal-Vereine haben kein Wappen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

bool crest_hit_image(App& a, int mx, int my, int& ix, int& iy) {
    const CrestGeo g = crest_geo(a);
    const auto& img = a.crest_ed->image();
    ix = floordiv(mx - g.x, g.zoom);
    iy = floordiv(my - g.y, g.zoom);
    return a.crest_ed->current() >= 0 && mx < g.view_w && ix >= 0 && iy >= 0 && ix < img.width && iy < img.height;
}

LRESULT CALLBACK crest_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    PixelEditor& e = *a.crest_ed;
    auto changed = [&] {
        InvalidateRect(h, nullptr, FALSE);
        refresh(a);
    };
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_crest(a, h); return 0;
        case WM_SIZE: update_crest_scroll(a); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            if (e.current() < 0) return 0;
            const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            int ix, iy;
            if (crest_hit_image(a, mx, my, ix, iy)) {
                SetCapture(h);
                e.stroke_begin(ix, iy);
            } else {
                const CrestGeo g = crest_geo(a);
                const int cx = floordiv(mx - g.pal_x, g.cell), cy = floordiv(my - g.pal_y, g.cell);
                if (cx >= 0 && cy >= 0 && cx < 16 && cy < 16) e.set_color(cy * 16 + cx);
            }
            changed();
            return 0;
        }
        case WM_MOUSEMOVE:
            if (e.drawing() && (wp & MK_LBUTTON)) {
                const CrestGeo g = crest_geo(a);
                e.stroke_move(floordiv(GET_X_LPARAM(lp) - g.x, g.zoom), floordiv(GET_Y_LPARAM(lp) - g.y, g.zoom));
                changed();
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            e.stroke_end();
            changed();
            return 0;
        case WM_RBUTTONDOWN: {
            int ix, iy;
            if (crest_hit_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy)) {
                e.set_color(e.pick(ix, iy));
                changed();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int d = GET_WHEEL_DELTA_WPARAM(wp);
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
                set_crest_zoom(a, a.crest_zoom + (d > 0 ? 1 : -1));
            } else {
                (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT ? a.cscroll_x : a.cscroll_y) -= d / 2;
                update_crest_scroll(a);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_HSCROLL:
        case WM_VSCROLL: {
            const bool horz = msg == WM_HSCROLL;
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, horz ? SB_HORZ : SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= a.px(16); break;
                case SB_LINEDOWN: pos += a.px(16); break;
                case SB_PAGEUP: pos -= int(si.nPage); break;
                case SB_PAGEDOWN: pos += int(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            (horz ? a.cscroll_x : a.cscroll_y) = pos;
            update_crest_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void load_crest(App& a) {
    if (!a.team || !a.crest_ed) return;
    const int t = a.team_sel;
    if (TeamDoc::has_players(t) && a.team->data(t).crest >= 0) a.crest_ed->load(80 + a.team->data(t).crest);
    else a.crest_ed->close();
    const BOOL on = a.crest_ed->current() >= 0;
    for (HWND w : {a.tu.cr_pen, a.tu.cr_fill, a.tu.cr_undo, a.tu.cr_save, a.tu.cr_zout, a.tu.cr_zin}) EnableWindow(w, on);
    a.cscroll_x = a.cscroll_y = 0;
    crest_autofit(a);
    update_crest_scroll(a);
    InvalidateRect(a.tu.crest_view, nullptr, FALSE);
}

// Asks what to do with unsaved crest changes; false = cancel
bool crest_confirm(App& a) {
    PixelEditor& e = *a.crest_ed;
    if (!e.dirty()) return true;
    std::wstring head = L"Wappen " + std::to_wstring(e.current() - 80) + L" wurde geändert.";
    TASKDIALOG_BUTTON buttons[] = {{101, L"Speichern"}, {102, L"Verwerfen"}};
    TASKDIALOGCONFIG cfg{sizeof cfg};
    cfg.hwndParent = a.main;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle = L"OpenEM Editor";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = head.c_str();
    cfg.pszContent = L"Das Wappen gilt für alle Vereine, die es benutzen. Änderungen speichern?";
    cfg.pButtons = buttons;
    cfg.cButtons = 2;
    cfg.nDefaultButton = 101;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr))) return false;
    if (pressed == 101) return e.save();
    if (pressed == 102) {
        e.load(e.current());  // discard
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------- hall maps
// Tab "Hallenkarten": the tile map of a goal scene rink (KB236, KB242..245) with a tile picker (KB231/232 tiles).

constexpr int kTileCell = 36;  // tile picker: 16 px tile at 2x plus a gap, in 96 dpi pixels

struct MapGeo {
    int ox, oy, iw, ih, tw, th, cw, ch, zoom;
};

MapGeo map_geo(App& a) {
    RECT r;
    GetClientRect(a.mu.view, &r);
    MapGeo g{};
    g.zoom = a.map_zoom;
    g.cw = r.right;
    g.ch = r.bottom;
    g.iw = MapDoc::kCols * MapDoc::kTile * g.zoom;
    g.ih = MapDoc::kRows * MapDoc::kTile * g.zoom;
    const int m = a.px(12);
    g.tw = g.iw + 2 * m;
    g.th = g.ih + 2 * m;
    g.ox = g.tw <= g.cw ? (g.cw - g.iw) / 2 : m - a.mscroll_x;
    g.oy = g.th <= g.ch ? (g.ch - g.ih) / 2 : m - a.mscroll_y;
    return g;
}

void update_map_scroll(App& a) {
    HWND h = a.mu.view;
    if (!h) return;
    if (a.map->current() < 0) {
        ShowScrollBar(h, SB_BOTH, FALSE);
        return;
    }
    for (int pass = 0; pass < 2; pass++) {
        MapGeo g = map_geo(a);
        a.mscroll_x = std::clamp(a.mscroll_x, 0, std::max(0, g.tw - g.cw));
        a.mscroll_y = std::clamp(a.mscroll_y, 0, std::max(0, g.th - g.ch));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0;
        si.nMax = g.tw - 1;
        si.nPage = UINT(g.cw);
        si.nPos = a.mscroll_x;
        SetScrollInfo(h, SB_HORZ, &si, TRUE);
        si.nMax = g.th - 1;
        si.nPage = UINT(g.ch);
        si.nPos = a.mscroll_y;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
    }
}

void set_map_zoom(App& a, int z) {
    a.map_zoom = std::clamp(z, 1, 4);
    update_map_scroll(a);
    InvalidateRect(a.mu.view, nullptr, FALSE);
    refresh(a);
}

uint32_t rgb32(const em::Palette& p, uint8_t i) {
    const auto& c = p.rgb[i];
    return uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
}

void paint_map(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(58, 60, 64));
    FillRect(mem, &cr, bg);
    DeleteObject(bg);
    MapDoc& m = *a.map;
    if (m.current() >= 0) {
        const auto& img = m.rink();
        if (a.map_bits_rev != m.revision() || a.map_bits.size() != img.pixels.size()) {
            a.map_bits.resize(img.pixels.size());
            for (size_t i = 0; i < a.map_bits.size(); i++) a.map_bits[i] = rgb32(m.palette(), img.pixels[i]);
            a.map_bits_rev = m.revision();
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = img.width;
        bi.bmiHeader.biHeight = -img.height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        const MapGeo g = map_geo(a);
        SetStretchBltMode(mem, COLORONCOLOR);
        StretchDIBits(mem, g.ox, g.oy, g.iw, g.ih, 0, 0, img.width, img.height, a.map_bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        if (a.grid) {
            const int cell = MapDoc::kTile * g.zoom;
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(232, 160, 40));
            HGDIOBJ op = SelectObject(mem, pen);
            const int c0 = std::max(0, floordiv(-g.ox, cell)), c1 = std::min(MapDoc::kCols, floordiv(g.cw - g.ox, cell) + 1);
            const int r0 = std::max(0, floordiv(-g.oy, cell)), r1 = std::min(MapDoc::kRows, floordiv(g.ch - g.oy, cell) + 1);
            if (g.zoom >= 2) {
                for (int c = c0; c <= c1; c++) { MoveToEx(mem, g.ox + c * cell, g.oy, nullptr); LineTo(mem, g.ox + c * cell, g.oy + g.ih); }
                for (int r = r0; r <= r1; r++) { MoveToEx(mem, g.ox, g.oy + r * cell, nullptr); LineTo(mem, g.ox + g.iw, g.oy + r * cell); }
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        RECT fr{g.ox - 1, g.oy - 1, g.ox + g.iw + 1, g.oy + g.ih + 1};
        FrameRect(mem, &fr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (a.map_hc >= 0) {  // hovered cell
            const int cell = MapDoc::kTile * g.zoom;
            RECT hr{g.ox + a.map_hc * cell, g.oy + a.map_hr * cell, g.ox + (a.map_hc + 1) * cell, g.oy + (a.map_hr + 1) * cell};
            FrameRect(mem, &hr, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
    } else {
        SelectObject(mem, a.font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(190, 190, 195));
        DrawTextW(mem, L"Links eine Hallenkarte wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

bool map_cell_at(App& a, int mx, int my, int& c, int& r) {
    const MapGeo g = map_geo(a);
    const int cell = MapDoc::kTile * g.zoom;
    c = floordiv(mx - g.ox, cell);
    r = floordiv(my - g.oy, cell);
    return a.map->current() >= 0 && c >= 0 && r >= 0 && c < MapDoc::kCols && r < MapDoc::kRows;
}

void sync_map_controls(App& a) {
    SendMessage(a.mu.mirror, BM_SETCHECK, a.map_mirror ? BST_CHECKED : BST_UNCHECKED, 0);
    const std::wstring t = L"Kachel " + std::to_wstring(a.map_tile) + (a.map_mirror ? L" (gespiegelt)" : L"");
    SetWindowTextW(a.mu.lbl, t.c_str());
    InvalidateRect(a.mu.tiles, nullptr, FALSE);
}

LRESULT CALLBACK map_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    MapDoc& m = *a.map;
    auto paint_at = [&](int c, int r) { m.set(c, r, a.map_tile, a.map_mirror); };
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_map(a, h); return 0;
        case WM_SIZE: update_map_scroll(a); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            int c, r;
            if (!map_cell_at(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), c, r)) return 0;
            const auto tool = a.ed->tool();
            if (tool == PixelEditor::Tool::Picker) {
                a.map_tile = m.tile(c, r);
                a.map_mirror = m.mirrored(c, r);
                sync_map_controls(a);
            } else if (tool == PixelEditor::Tool::Fill) {
                m.fill(c, r, a.map_tile, a.map_mirror);
            } else {
                SetCapture(h);
                m.stroke_begin();
                paint_at(c, r);
            }
            InvalidateRect(h, nullptr, FALSE);
            refresh(a);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int c, r;
            const bool in = map_cell_at(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), c, r);
            const int oc = a.map_hc, orow = a.map_hr;
            a.map_hc = in ? c : -1;
            a.map_hr = in ? r : -1;
            if (in) a.map_hover = L"Spalte " + std::to_wstring(c) + L"   Zeile " + std::to_wstring(r) + L"   Kachel " + std::to_wstring(m.tile(c, r)) + (m.mirrored(c, r) ? L" (gespiegelt)" : L"");
            else a.map_hover.clear();
            if (GetCapture() == h && (wp & MK_LBUTTON) && in) paint_at(c, r);
            if (oc != a.map_hc || orow != a.map_hr || GetCapture() == h) InvalidateRect(h, nullptr, FALSE);
            refresh(a);
            return 0;
        }
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            m.stroke_end();
            refresh(a);
            return 0;
        case WM_RBUTTONDOWN: {
            int c, r;
            if (map_cell_at(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), c, r)) {
                a.map_tile = m.tile(c, r);
                a.map_mirror = m.mirrored(c, r);
                sync_map_controls(a);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int d = GET_WHEEL_DELTA_WPARAM(wp);
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) set_map_zoom(a, a.map_zoom + (d > 0 ? 1 : -1));
            else {
                (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT ? a.mscroll_x : a.mscroll_y) -= d / 2;
                update_map_scroll(a);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_HSCROLL:
        case WM_VSCROLL: {
            const bool horz = msg == WM_HSCROLL;
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, horz ? SB_HORZ : SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= a.px(32); break;
                case SB_LINEDOWN: pos += a.px(32); break;
                case SB_PAGEUP: pos -= int(si.nPage); break;
                case SB_PAGEDOWN: pos += int(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            (horz ? a.mscroll_x : a.mscroll_y) = pos;
            update_map_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// Tile picker --------------------------------------------------------------------------------------------

struct TileGeo {
    int cell, cols, rows;
};

TileGeo tile_geo(App& a, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    TileGeo g{};
    g.cell = a.px(kTileCell);
    g.cols = std::max(1, int(r.right) / g.cell);
    g.rows = (a.map->tile_count() + g.cols - 1) / g.cols;
    return g;
}

void update_tile_scroll(App& a) {
    HWND h = a.mu.tiles;
    if (!h) return;
    for (int pass = 0; pass < 2; pass++) {
        const TileGeo g = tile_geo(a, h);
        RECT r;
        GetClientRect(h, &r);
        const int total = g.rows * g.cell;
        a.tscroll_y = std::clamp(a.tscroll_y, 0, std::max(0, total - int(r.bottom)));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0;
        si.nMax = total - 1;
        si.nPage = UINT(r.bottom);
        si.nPos = a.tscroll_y;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
    }
}

void paint_tiles(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &cr, GetSysColorBrush(COLOR_WINDOW));
    MapDoc& m = *a.map;
    if (m.current() >= 0) {
        const TileGeo g = tile_geo(a, h);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = MapDoc::kTile;
        bi.bmiHeader.biHeight = -MapDoc::kTile;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(mem, COLORONCOLOR);
        const int first = std::max(0, a.tscroll_y / g.cell) * g.cols;
        const int last = std::min(m.tile_count(), (a.tscroll_y + int(cr.bottom)) / g.cell * g.cols + g.cols + g.cols);
        const int px2 = (g.cell - a.px(4)) / MapDoc::kTile * MapDoc::kTile;  // whole-number scale of the 16 px tile
        std::vector<uint32_t> bits(size_t(MapDoc::kTile) * MapDoc::kTile);
        for (int t = first; t < last; t++) {
            const int x = (t % g.cols) * g.cell + a.px(2), y = (t / g.cols) * g.cell - a.tscroll_y + a.px(2);
            const em::Image& ti = m.tile_image(t);
            for (size_t i = 0; i < bits.size(); i++) bits[i] = rgb32(m.palette(), ti.pixels[i]);
            StretchDIBits(mem, x, y, px2, px2, 0, 0, MapDoc::kTile, MapDoc::kTile, bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
            if (t == a.map_tile) {
                RECT sr{x - a.px(2), y - a.px(2), x + px2 + a.px(2), y + px2 + a.px(2)};
                FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
                InflateRect(&sr, -1, -1);
                FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
            }
        }
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK tiles_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_tiles(a, h); return 0;
        case WM_SIZE: update_tile_scroll(a); return 0;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE: {
            if (a.map->current() < 0) return 0;
            const TileGeo g = tile_geo(a, h);
            const int cx = GET_X_LPARAM(lp) / g.cell, cy = (GET_Y_LPARAM(lp) + a.tscroll_y) / g.cell;
            const int t = cy * g.cols + cx;
            if (cx >= g.cols || t < 0 || t >= a.map->tile_count()) return 0;
            if (msg == WM_LBUTTONDOWN) {
                a.map_tile = t;
                sync_map_controls(a);
            } else {
                SendMessage(a.status, SB_SETTEXT, 0, (LPARAM)(L"Kachel " + std::to_wstring(t)).c_str());
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            a.tscroll_y -= GET_WHEEL_DELTA_WPARAM(wp) / 2;
            update_tile_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= a.px(36); break;
                case SB_LINEDOWN: pos += a.px(36); break;
                case SB_PAGEUP: pos -= int(si.nPage); break;
                case SB_PAGEDOWN: pos += int(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            a.tscroll_y = pos;
            update_tile_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void show_map(App& a, bool on) {
    for (HWND w : {a.mu.view, a.mu.tiles, a.mu.mirror, a.mu.lbl}) ShowWindow(w, on ? SW_SHOW : SW_HIDE);
}

void layout_map(App& a, int x, int y, int w, int h, int rx, int rw) {
    if (!a.mu.view) return;
    MoveWindow(a.mu.view, x, y, w, h, TRUE);
    const int m = a.px(10);
    MoveWindow(a.mu.lbl, rx + m, y + m + a.px(3), rw - 2 * m - a.px(110), a.px(22), TRUE);
    MoveWindow(a.mu.mirror, rx + rw - m - a.px(104), y + m, a.px(104), a.px(24), TRUE);
    MoveWindow(a.mu.tiles, rx + a.px(4), y + m + a.px(34), rw - a.px(8), std::max(a.px(60), h - m - a.px(40)), TRUE);
    update_map_scroll(a);
    update_tile_scroll(a);
}

// Opens a map; the old one has been confirmed by the caller
void open_map(App& a, int number) {
    if (a.map->load(number)) {
        a.mscroll_x = a.mscroll_y = 0;
        a.map_zoom = 1;
        update_map_scroll(a);
        update_tile_scroll(a);
    }
    InvalidateRect(a.mu.view, nullptr, FALSE);
    InvalidateRect(a.mu.tiles, nullptr, FALSE);
}

// ------------------------------------------------------------------------------------------ goal scene editor

constexpr int kSceneTimer = 77;
constexpr int kPickCell = 40;  // sprite picker: 32 px sprite plus a gap, in 96 dpi pixels

SceneGeo scene_geo(App& a) {
    RECT r;
    GetClientRect(a.su.view, &r);
    SceneGeo g;
    SceneDoc& s = *a.scene;
    const int cw = std::max(1, int(r.right)), ch = std::max(1, int(r.bottom));
    constexpr int kW = em::kRinkColumns * em::kRinkTile, kH = em::kRinkRows * em::kRinkTile;
    if (a.sc_mode == 0) {  // the window of the game: 320 x 200 at the camera
        const auto c = s.camera_view(a.sc_pos);
        g.x0 = c.x;
        g.y0 = c.y;
        g.rw = em::kTorViewWidth;
        g.rh = em::kTorViewHeight;
        g.scale = std::max(1, std::min(cw / g.rw, ch / g.rh));
    } else if (a.sc_mode == 1) {  // a window around the selected object, scale 2
        g.scale = std::max(1, a.px(2));
        g.rw = int(std::ceil(cw / g.scale));
        g.rh = int(std::ceil(ch / g.scale));
        int bx = 0, by = 0, bw = 0, bh = 0;
        int cx, cy;
        if (s.box(a.sc_pos, a.sc_sel, bx, by, bw, bh)) {
            cx = bx + bw / 2;
            cy = by + bh / 2;
        } else {
            const auto p = s.position(a.sc_pos, a.sc_sel);
            cx = p.x - em::kTorAnchorX + 16;
            cy = p.y - em::kTorAnchorY + 16;
        }
        g.x0 = std::clamp(cx - g.rw / 2, 0, std::max(0, kW - g.rw));
        g.y0 = std::clamp(cy - g.rh / 2, 0, std::max(0, kH - g.rh));
    } else {  // the whole hall
        g.rw = kW;
        g.rh = kH;
        g.scale = std::min(double(cw) / kW, double(ch) / kH);
    }
    g.dw = a.sc_mode == 1 ? cw : int(g.rw * g.scale);
    g.dh = a.sc_mode == 1 ? ch : int(g.rh * g.scale);
    g.ox = (cw - g.dw) / 2;
    g.oy = (ch - g.dh) / 2;
    return g;
}

POINT scene_to_screen(const SceneGeo& g, int rx, int ry) {
    return {g.ox + int(std::lround((rx - g.x0) * g.scale)), g.oy + int(std::lround((ry - g.y0) * g.scale))};
}

POINT scene_to_rink(const SceneGeo& g, int mx, int my) {
    return {g.x0 + int(std::floor((mx - g.ox) / g.scale)), g.y0 + int(std::floor((my - g.oy) / g.scale))};
}

std::wstring subject_text(const App& a, int i) {
    const SceneDoc& s = *a.scene;
    const auto p = s.position(a.sc_pos, i);
    const std::wstring xy = std::to_wstring(p.x) + L", " + std::to_wstring(p.y);
    if (i == SceneDoc::kCamera) return L"Kamera (Skript)  ·  " + xy;
    if (i == SceneDoc::kPuck) return L"Puck  ·  " + xy;
    return L"Figur " + std::to_wstring(i + 1) + L"  ·  Typ " + std::to_wstring(s.sprite(a.sc_pos, i)) + L"  ·  " + (p.x == 0 && p.y == 0 ? L"nicht sichtbar" : xy);
}

void set_number(HWND edit, int v) {
    wchar_t buf[16] = {};
    GetWindowTextW(edit, buf, 16);
    if (buf[0] && _wtoi(buf) == v) return;
    SetWindowTextW(edit, std::to_wstring(v).c_str());
}

int get_number(HWND edit, int fallback) {
    wchar_t buf[16] = {};
    GetWindowTextW(edit, buf, 16);
    return buf[0] ? _wtoi(buf) : fallback;
}

// Brings every control in line with the document and the position (not while the user is typing in it)
void sync_scene(App& a) {
    SceneUi& u = a.su;
    if (!u.view || a.sc_loading) return;
    a.sc_loading = true;
    SceneDoc& s = *a.scene;
    const bool on = s.loaded();
    if (a.sc_pos > s.frames()) a.sc_pos = s.frames();
    const int n = a.sc_pos;
    SendMessage(u.track, TBM_SETRANGE, TRUE, MAKELPARAM(0, on ? s.frames() : 0));
    SendMessage(u.track, TBM_SETPOS, TRUE, n);
    SetWindowTextW(u.lbl, !on ? L"" : n == 0 ? (L"Start  (" + std::to_wstring(s.frames()) + L" Bilder)").c_str() : (L"Bild " + std::to_wstring(n) + L" / " + std::to_wstring(s.frames())).c_str());
    SetWindowTextW(u.play, a.sc_playing ? L"Pause" : L"Abspielen");
    SendMessage(u.mode, CB_SETCURSEL, a.sc_mode, 0);
    SendMessage(u.arena, CB_SETCURSEL, s.arena(), 0);
    if (on && !a.sc_playing) {
        for (int i = 0; i < SceneDoc::kSubjects; i++) {
            const std::wstring t = subject_text(a, i);
            wchar_t cur[128] = {};
            if (SendMessage(u.subjects, LB_GETCOUNT, 0, 0) > i) SendMessage(u.subjects, LB_GETTEXT, WPARAM(i), (LPARAM)cur);
            if (t == cur) continue;
            if (SendMessage(u.subjects, LB_GETCOUNT, 0, 0) > i) SendMessage(u.subjects, LB_DELETESTRING, WPARAM(i), 0);
            SendMessage(u.subjects, LB_INSERTSTRING, WPARAM(i), (LPARAM)t.c_str());
        }
        SendMessage(u.subjects, LB_SETCURSEL, a.sc_sel, 0);
        const auto p = s.position(n, a.sc_sel);
        set_number(u.x, p.x);
        set_number(u.y, p.y);
        const bool obj = a.sc_sel < SceneDoc::kObjects;
        set_number(u.sprite, obj ? s.sprite(n, a.sc_sel) : 0);
        set_number(u.sound, s.sound(n));
        for (HWND w : {u.sprite, u.sprite_ud, u.to_end}) EnableWindow(w, obj);
        const int route_key = n * 100 + a.sc_sel;
        if (route_key != a.sc_route_key) {
            a.sc_route_key = route_key;
            set_number(u.route_x, p.x);
            set_number(u.route_y, p.y);
            set_number(u.route_to, std::min(s.frames(), n + 10));
        }
        for (HWND w : {u.route_to, u.route_x, u.route_y, u.move_btn, u.smooth_btn}) EnableWindow(w, on);
        EnableWindow(u.sound, n >= 1);
        EnableWindow(u.sound_ud, n >= 1);
        EnableWindow(u.dup, n >= 1);
        EnableWindow(u.del, n >= 1 && s.frames() > 1);
    }
    for (HWND w : {u.x, u.y, u.x_ud, u.y_ud, u.ins, u.play, u.prev, u.next, u.track, u.subjects}) EnableWindow(w, on);
    a.sc_loading = false;
    InvalidateRect(u.view, nullptr, FALSE);
    InvalidateRect(u.picker, nullptr, FALSE);
}

void scene_goto(App& a, int n) {
    a.sc_pos = std::clamp(n, 0, a.scene->frames());
    sync_scene(a);
}

void scene_stop(App& a) {
    if (!a.sc_playing) return;
    a.sc_playing = false;
    KillTimer(a.main, kSceneTimer);
    sync_scene(a);
}

void scene_play(App& a) {
    if (!a.scene->loaded()) return;
    if (a.sc_playing) return scene_stop(a);
    if (a.sc_pos >= a.scene->frames()) a.sc_pos = 0;
    a.sc_playing = true;
    SetTimer(a.main, kSceneTimer, 60, nullptr);
    sync_scene(a);
}

void paint_scene(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(58, 60, 64));
    FillRect(mem, &cr, bg);
    DeleteObject(bg);
    SceneDoc& s = *a.scene;
    if (s.loaded()) {
        const SceneGeo g = a.sc_drag ? a.sc_lock : scene_geo(a);
        const em::Image img = s.render(a.sc_pos, g.x0, g.y0, g.rw, g.rh);
        a.sc_bits.resize(img.pixels.size());
        for (size_t i = 0; i < a.sc_bits.size(); i++) a.sc_bits[i] = rgb32(s.palette(), img.pixels[i]);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = img.width;
        bi.bmiHeader.biHeight = -img.height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(mem, COLORONCOLOR);
        StretchDIBits(mem, g.ox, g.oy, g.dw, g.dh, 0, 0, img.width, img.height, a.sc_bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        RECT fr{g.ox - 1, g.oy - 1, g.ox + g.dw + 1, g.oy + g.dh + 1};
        FrameRect(mem, &fr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        IntersectClipRect(mem, g.ox, g.oy, g.ox + g.dw, g.oy + g.dh);
        if (a.sc_mode == 2) {  // the camera window
            const auto c = s.camera_view(a.sc_pos);
            const POINT p0 = scene_to_screen(g, c.x, c.y), p1 = scene_to_screen(g, c.x + em::kTorViewWidth, c.y + em::kTorViewHeight);
            RECT cam{p0.x, p0.y, p1.x, p1.y};
            HBRUSH ob = CreateSolidBrush(a.sc_sel == SceneDoc::kCamera ? RGB(255, 255, 0) : RGB(232, 160, 40));
            FrameRect(mem, &cam, ob);
            DeleteObject(ob);
        }
        // path of the selected object
        if (a.sc_sel != SceneDoc::kCamera) {
            const auto path = s.trajectory(a.sc_sel);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 220, 255));
            HGDIOBJ op = SelectObject(mem, pen);
            for (size_t i = 0; i < path.size(); i++) {
                const POINT p = scene_to_screen(g, path[i].x - em::kTorAnchorX + 16, path[i].y - em::kTorAnchorY + 16);
                if (i == 0) MoveToEx(mem, p.x, p.y, nullptr);
                else LineTo(mem, p.x, p.y);
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        // boxes: the selected one in yellow, the others white with their number
        SelectObject(mem, a.font);
        SetBkMode(mem, TRANSPARENT);
        for (int i = 0; i <= SceneDoc::kPuck; i++) {
            int bx, by, bw, bh;
            if (!s.box(a.sc_pos, i, bx, by, bw, bh)) continue;
            const POINT p0 = scene_to_screen(g, bx, by), p1 = scene_to_screen(g, bx + bw, by + bh);
            RECT b{p0.x, p0.y, p1.x, p1.y};
            const bool sel = i == a.sc_sel;
            if (bw > 8 || sel) {
                HBRUSH ob = CreateSolidBrush(sel ? RGB(255, 255, 0) : RGB(235, 235, 235));
                FrameRect(mem, &b, ob);
                DeleteObject(ob);
            }
            if (i < SceneDoc::kObjects && g.scale >= 0.9) {
                const std::wstring t = std::to_wstring(i + 1);
                SetTextColor(mem, RGB(0, 0, 0));
                TextOutW(mem, b.left + 3, b.top + 1, t.c_str(), int(t.size()));
                SetTextColor(mem, sel ? RGB(255, 255, 0) : RGB(255, 255, 255));
                TextOutW(mem, b.left + 2, b.top, t.c_str(), int(t.size()));
            }
        }
    } else {
        SelectObject(mem, a.font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(190, 190, 195));
        DrawTextW(mem, L"Links eine Szene wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK scene_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    SceneDoc& s = *a.scene;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_scene(a, h); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && s.loaded()) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(h, &pt);
                const SceneGeo g = scene_geo(a);
                const POINT r = scene_to_rink(g, pt.x, pt.y);
                SetCursor(LoadCursor(nullptr, a.sc_drag || s.hit_test(a.sc_pos, r.x, r.y) >= 0 ? IDC_SIZEALL : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            if (!s.loaded()) return 0;
            scene_stop(a);
            const SceneGeo g = scene_geo(a);
            const POINT r = scene_to_rink(g, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            int hit = s.hit_test(a.sc_pos, r.x, r.y);
            if (hit < 0 && a.sc_mode == 2) {  // the camera window of the hall overview
                const auto c = s.camera_view(a.sc_pos);
                if (r.x >= c.x && r.y >= c.y && r.x < c.x + em::kTorViewWidth && r.y < c.y + em::kTorViewHeight) hit = SceneDoc::kCamera;
            }
            if (hit < 0) return 0;
            a.sc_sel = hit;
            const auto p = s.position(a.sc_pos, hit);
            a.sc_grab = {p.x - r.x, p.y - r.y};
            a.sc_lock = g;
            a.sc_drag = true;
            s.stroke_begin();
            SetCapture(h);
            sync_scene(a);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const SceneGeo g = a.sc_drag ? a.sc_lock : scene_geo(a);
            const POINT r = scene_to_rink(g, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (a.sc_drag) {
                s.move_to(a.sc_pos, a.sc_sel, r.x + int(a.sc_grab.x), r.y + int(a.sc_grab.y), a.sc_keep);
                sync_scene(a);
            }
            a.sc_hover = L"x " + std::to_wstring(r.x) + L"   y " + std::to_wstring(r.y);
            SendMessage(a.status, SB_SETTEXT, 1, (LPARAM)a.sc_hover.c_str());
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (a.sc_drag) {
                a.sc_drag = false;
                s.stroke_end();
                if (GetCapture() == h) ReleaseCapture();
                refresh(a);
            }
            return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_LEFT: scene_stop(a); scene_goto(a, a.sc_pos - 1); break;
                case VK_RIGHT: scene_stop(a); scene_goto(a, a.sc_pos + 1); break;
                case VK_HOME: scene_stop(a); scene_goto(a, 0); break;
                case VK_END: scene_stop(a); scene_goto(a, s.frames()); break;
                case VK_SPACE: scene_play(a); break;
                default: return DefWindowProc(h, msg, wp, lp);
            }
            return 0;
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// ---- sprite picker: every object type, a click sets it for the selected object

struct PickGeo {
    int cell, cols, rows;
};

PickGeo pick_geo(App& a, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    PickGeo g{};
    g.cell = a.px(kPickCell);
    g.cols = std::max(1, int(r.right) / g.cell);
    g.rows = (SceneDoc::kMaxSprite + 1 + g.cols - 1) / g.cols;
    return g;
}

void update_pick_scroll(App& a) {
    HWND h = a.su.picker;
    if (!h) return;
    for (int pass = 0; pass < 2; pass++) {
        const PickGeo g = pick_geo(a, h);
        RECT r;
        GetClientRect(h, &r);
        const int total = g.rows * g.cell;
        a.sc_pick_scroll = std::clamp(a.sc_pick_scroll, 0, std::max(0, total - int(r.bottom)));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0;
        si.nMax = total - 1;
        si.nPage = UINT(r.bottom);
        si.nPos = a.sc_pick_scroll;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
    }
}

void paint_picker(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(58, 60, 64));
    FillRect(mem, &cr, bg);
    DeleteObject(bg);
    SceneDoc& s = *a.scene;
    if (s.loaded()) {
        const PickGeo g = pick_geo(a, h);
        const int cur = a.sc_sel < SceneDoc::kObjects ? s.sprite(a.sc_pos, a.sc_sel) : -1;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(mem, COLORONCOLOR);
        const uint32_t back = 0x3A3C40;
        const int first = std::max(0, a.sc_pick_scroll / g.cell) * g.cols;
        const int last = std::min(SceneDoc::kMaxSprite + 1, (a.sc_pick_scroll + int(cr.bottom)) / g.cell * g.cols + 2 * g.cols);
        for (int t = first; t < last; t++) {
            int size = 0;
            const uint8_t* px = s.type_pixels(t, size);
            const int x = (t % g.cols) * g.cell + a.px(2), y = (t / g.cols) * g.cell - a.sc_pick_scroll + a.px(2);
            const int sc = std::max(1, (g.cell - a.px(4)) / 32);
            if (px) {
                std::vector<uint32_t> bits(size_t(size) * size_t(size));
                for (size_t i = 0; i < bits.size(); i++) bits[i] = px[i] ? rgb32(s.palette(), px[i]) : back;
                bi.bmiHeader.biWidth = size;
                bi.bmiHeader.biHeight = -size;
                const int dsz = size == 32 ? 32 * sc : size * sc * 3;
                StretchDIBits(mem, x + (32 * sc - dsz) / 2, y + (32 * sc - dsz) / 2, dsz, dsz, 0, 0, size, size, bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
            }
            if (t == cur) {
                RECT sr{x - a.px(2), y - a.px(2), x + 32 * sc + a.px(2), y + 32 * sc + a.px(2)};
                FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
                InflateRect(&sr, -1, -1);
                FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
            }
        }
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK picker_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_picker(a, h); return 0;
        case WM_SIZE: update_pick_scroll(a); return 0;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE: {
            if (!a.scene->loaded()) return 0;
            const PickGeo g = pick_geo(a, h);
            const int cx = GET_X_LPARAM(lp) / g.cell, cy = (GET_Y_LPARAM(lp) + a.sc_pick_scroll) / g.cell;
            const int t = cy * g.cols + cx;
            if (cx >= g.cols || t < 0 || t > SceneDoc::kMaxSprite) return 0;
            if (msg == WM_LBUTTONDOWN) {
                if (a.sc_sel < SceneDoc::kObjects) {
                    scene_stop(a);
                    a.scene->set_sprite(a.sc_pos, a.sc_sel, t, SendMessage(a.su.to_end, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    refresh(a);
                }
            } else {
                const auto r = em::tor_sprite_ref(t);
                std::wstring what = r.kind == em::TorSpriteRef::Kind::Dot ? L"Punkt" : r.kind == em::TorSpriteRef::Kind::Puck ? L"Puck" : L"KB" + std::to_wstring(233 + r.sheet) + L" Sprite " + std::to_wstring(r.index);
                SendMessage(a.status, SB_SETTEXT, 0, (LPARAM)(L"Typ " + std::to_wstring(t) + L"  ·  " + what).c_str());
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            a.sc_pick_scroll -= GET_WHEEL_DELTA_WPARAM(wp) / 2;
            update_pick_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= a.px(kPickCell); break;
                case SB_LINEDOWN: pos += a.px(kPickCell); break;
                case SB_PAGEUP: pos -= int(si.nPage); break;
                case SB_PAGEDOWN: pos += int(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            a.sc_pick_scroll = pos;
            update_pick_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void create_scene_controls(App& a) {
    SceneUi& u = a.su;
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int id) {
        HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | style, 0, 0, 0, 0, a.main, (HMENU)UINT_PTR(id), a.inst, nullptr);
        u.all.push_back(w);
        return w;
    };
    u.view = CreateWindowExW(WS_EX_CLIENTEDGE, kSceneClass, nullptr, WS_CHILD, 0, 0, 0, 0, a.main, nullptr, a.inst, nullptr);
    u.picker = CreateWindowExW(WS_EX_CLIENTEDGE, kPickerClass, nullptr, WS_CHILD | WS_VSCROLL, 0, 0, 0, 0, a.main, nullptr, a.inst, nullptr);
    u.all.push_back(u.view);
    u.all.push_back(u.picker);
    u.lbl = make(L"STATIC", L"", SS_CENTERIMAGE, 0, 0);
    u.track = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, IDC_SC_TRACK);
    u.prev = make(L"BUTTON", L"‹", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_PREV);
    u.play = make(L"BUTTON", L"Abspielen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_PLAY);
    u.next = make(L"BUTTON", L"›", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_NEXT);
    u.mode = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SC_MODE);
    for (const wchar_t* t : {L"Ansicht: Spielfenster (320 × 200)", L"Ansicht: Ausschnitt um Auswahl", L"Ansicht: ganze Halle"})
        SendMessage(u.mode, CB_ADDSTRING, 0, (LPARAM)t);
    u.arena = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SC_ARENA);
    for (const wchar_t* t : {L"Halle: Standard (KB236)", L"Halle: Variante 1 (KB242)", L"Halle: Variante 2 (KB243)", L"Halle: Variante 3 (KB244)", L"Halle: Variante 4 (KB245)"})
        SendMessage(u.arena, CB_ADDSTRING, 0, (LPARAM)t);
    u.subjects = make(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_SC_LIST);
    auto numeric = [&](HWND& e, HWND& ud, int id, int lo, int hi, bool buddy_int) {
        e = make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, id);
        ud = make(UPDOWN_CLASSW, L"", (buddy_int ? UDS_SETBUDDYINT : 0u) | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, id + 1000);
        SendMessage(ud, UDM_SETBUDDY, (WPARAM)e, 0);
        SendMessage(ud, UDM_SETRANGE32, lo, hi);
    };
    u.lbl_sprite = make(L"STATIC", L"Typ", 0, 0, 0);
    numeric(u.sprite, u.sprite_ud, IDC_SC_SPRITE, 0, 255, true);
    u.to_end = make(L"BUTTON", L"bis zum Ende", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_SC_TOEND);
    u.lbl_x = make(L"STATIC", L"x", 0, 0, 0);
    numeric(u.x, u.x_ud, IDC_SC_X, 0, 65535, false);
    u.lbl_y = make(L"STATIC", L"y", 0, 0, 0);
    numeric(u.y, u.y_ud, IDC_SC_Y, 0, 65535, false);
    u.lbl_sound = make(L"STATIC", L"Ton", 0, 0, 0);
    numeric(u.sound, u.sound_ud, IDC_SC_SOUND, 0, 4, true);
    u.keep = make(L"BUTTON", L"Folgebilder bleiben", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_SC_KEEP);
    SendMessage(u.keep, BM_SETCHECK, BST_CHECKED, 0);
    u.ins = make(L"BUTTON", L"Einfügen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_INS);
    u.dup = make(L"BUTTON", L"Kopieren", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_DUP);
    u.del = make(L"BUTTON", L"Löschen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_DEL);
    SendMessage(u.track, TBM_SETPAGESIZE, 0, 10);
    u.lbl_route = make(L"STATIC", L"Bahn bis Bild", 0, 0, 0);
    u.route_to = make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_SC_RTO);
    u.lbl_rx = make(L"STATIC", L"x", 0, 0, 0);
    u.route_x = make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_SC_RX);
    u.lbl_ry = make(L"STATIC", L"y", 0, 0, 0);
    u.route_y = make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_SC_RY);
    u.move_btn = make(L"BUTTON", L"Bewegen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_MOVE);
    u.smooth_btn = make(L"BUTTON", L"Glätten", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SC_SMOOTH);
    u.lbl_home = make(L"STATIC", L"Trikot Heim", 0, 0, 0);
    u.home = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SC_HOME);
    u.lbl_away = make(L"STATIC", L"Gast", 0, 0, 0);
    u.away = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SC_AWAY);
    for (HWND c : {u.home, u.away}) {
        SendMessage(c, CB_ADDSTRING, 0, (LPARAM)L"Standardfarbe");
        SendMessage(c, CB_SETITEMDATA, 0, -1);
        for (int t = 0; t < TeamDoc::kTeams; t++) {
            if (!TeamDoc::editable(t)) continue;
            const LRESULT i = SendMessage(c, CB_ADDSTRING, 0, (LPARAM)widen(a.team->club_name(t)).c_str());
            SendMessage(c, CB_SETITEMDATA, WPARAM(i), t);
        }
        SendMessage(c, CB_SETCURSEL, 0, 0);
    }
    for (HWND w : u.all) SendMessage(w, WM_SETFONT, (WPARAM)a.font, TRUE);
}

void show_scene(App& a, bool on) {
    for (HWND w : a.su.all) ShowWindow(w, on ? SW_SHOW : SW_HIDE);
}

void layout_scene(App& a, int x, int y, int w, int h, int rx, int rw) {
    SceneUi& u = a.su;
    if (!u.view) return;
    const int bar = a.px(126), m = a.px(8), bh = a.px(24);
    MoveWindow(u.view, x, y, w, std::max(a.px(60), h - bar), TRUE);
    const int by = y + h - bar + a.px(4);
    MoveWindow(u.lbl, x + m, by, a.px(130), bh, TRUE);
    MoveWindow(u.track, x + m + a.px(134), by, std::max(a.px(60), w - 2 * m - a.px(134)), bh, TRUE);
    const int by2 = by + bh + a.px(4);
    MoveWindow(u.prev, x + m, by2, a.px(36), bh, TRUE);
    MoveWindow(u.play, x + m + a.px(40), by2, a.px(92), bh, TRUE);
    MoveWindow(u.next, x + m + a.px(136), by2, a.px(36), bh, TRUE);
    const int cw = std::max(a.px(120), (w - 2 * m - a.px(184)) / 2);
    MoveWindow(u.mode, x + m + a.px(184), by2, cw, a.px(300), TRUE);
    MoveWindow(u.arena, x + m + a.px(184) + cw + a.px(6), by2, cw, a.px(300), TRUE);
    const int by3 = by2 + bh + a.px(6);
    MoveWindow(u.lbl_route, x + m, by3 + a.px(3), a.px(84), bh, TRUE);
    MoveWindow(u.route_to, x + m + a.px(86), by3, a.px(48), bh - a.px(1), TRUE);
    MoveWindow(u.lbl_rx, x + m + a.px(144), by3 + a.px(3), a.px(14), bh, TRUE);
    MoveWindow(u.route_x, x + m + a.px(158), by3, a.px(54), bh - a.px(1), TRUE);
    MoveWindow(u.lbl_ry, x + m + a.px(222), by3 + a.px(3), a.px(14), bh, TRUE);
    MoveWindow(u.route_y, x + m + a.px(236), by3, a.px(54), bh - a.px(1), TRUE);
    MoveWindow(u.move_btn, x + m + a.px(300), by3, a.px(78), bh, TRUE);
    MoveWindow(u.smooth_btn, x + m + a.px(384), by3, a.px(70), bh, TRUE);
    const int by4 = by3 + bh + a.px(6), cw2 = std::max(a.px(100), (w - 2 * m - a.px(120)) / 2);
    MoveWindow(u.lbl_home, x + m, by4 + a.px(3), a.px(78), bh, TRUE);
    MoveWindow(u.home, x + m + a.px(80), by4, cw2, a.px(300), TRUE);
    MoveWindow(u.lbl_away, x + m + a.px(90) + cw2, by4 + a.px(3), a.px(34), bh, TRUE);
    MoveWindow(u.away, x + m + a.px(126) + cw2, by4, cw2, a.px(300), TRUE);
    // right panel
    const int px0 = rx + m, pw = rw - 2 * m;
    int py = y + m;
    const int lh = a.px(14 * 16 + 8);
    MoveWindow(u.subjects, px0, py, pw, lh, TRUE);
    py += lh + a.px(8);
    const int eh = a.px(23), row = a.px(28), lw = a.px(30), ew = a.px(62);
    MoveWindow(u.lbl_sprite, px0, py + a.px(3), lw, eh, TRUE);
    MoveWindow(u.sprite, px0 + lw, py, ew, eh, TRUE);
    SendMessage(u.sprite_ud, UDM_SETBUDDY, (WPARAM)u.sprite, 0);
    MoveWindow(u.to_end, px0 + lw + ew + a.px(10), py, pw - lw - ew - a.px(10), eh, TRUE);
    py += row;
    MoveWindow(u.lbl_x, px0, py + a.px(3), lw, eh, TRUE);
    MoveWindow(u.x, px0 + lw, py, ew, eh, TRUE);
    SendMessage(u.x_ud, UDM_SETBUDDY, (WPARAM)u.x, 0);
    MoveWindow(u.lbl_y, px0 + lw + ew + a.px(14), py + a.px(3), a.px(16), eh, TRUE);
    MoveWindow(u.y, px0 + lw + ew + a.px(30), py, ew, eh, TRUE);
    SendMessage(u.y_ud, UDM_SETBUDDY, (WPARAM)u.y, 0);
    py += row;
    MoveWindow(u.lbl_sound, px0, py + a.px(3), lw, eh, TRUE);
    MoveWindow(u.sound, px0 + lw, py, ew, eh, TRUE);
    SendMessage(u.sound_ud, UDM_SETBUDDY, (WPARAM)u.sound, 0);
    MoveWindow(u.keep, px0 + lw + ew + a.px(10), py, pw - lw - ew - a.px(10), eh, TRUE);
    py += row;
    const int bw = (pw - 2 * a.px(6)) / 3;
    MoveWindow(u.ins, px0, py, bw, bh, TRUE);
    MoveWindow(u.dup, px0 + bw + a.px(6), py, bw, bh, TRUE);
    MoveWindow(u.del, px0 + 2 * (bw + a.px(6)), py, bw, bh, TRUE);
    py += bh + a.px(8);
    MoveWindow(u.picker, rx + a.px(4), py, rw - a.px(8), std::max(a.px(60), y + h - py - a.px(4)), TRUE);
    update_pick_scroll(a);
}

// Opens a scene (index into the list of names); the old one has been confirmed by the caller
void open_scene(App& a, int index) {
    if (index < 0 || size_t(index) >= a.sc_names.size()) return;
    scene_stop(a);
    if (a.scene->load(a.sc_names[size_t(index)])) {
        a.sc_pos = 0;
        a.sc_sel = 0;
        a.sc_pick_scroll = 0;
        update_pick_scroll(a);
    }
    sync_scene(a);
}

// Menu / toolbar commands and the control notifications of the page; returns true when handled
bool scene_command(App& a, UINT id, UINT code) {
    SceneDoc& s = *a.scene;
    switch (id) {
        case IDC_SC_PLAY: scene_play(a); return true;
        case IDC_SC_PREV: scene_stop(a); scene_goto(a, a.sc_pos - 1); return true;
        case IDC_SC_NEXT: scene_stop(a); scene_goto(a, a.sc_pos + 1); return true;
        case IDC_SC_MODE:
            if (code == CBN_SELCHANGE) {
                a.sc_mode = int(SendMessage(a.su.mode, CB_GETCURSEL, 0, 0));
                sync_scene(a);
            }
            return true;
        case IDC_SC_ARENA:
            if (code == CBN_SELCHANGE) {
                s.set_arena(int(SendMessage(a.su.arena, CB_GETCURSEL, 0, 0)));
                sync_scene(a);
            }
            return true;
        case IDC_SC_LIST:
            if (code == LBN_SELCHANGE) {
                const int i = int(SendMessage(a.su.subjects, LB_GETCURSEL, 0, 0));
                if (i >= 0) a.sc_sel = i;
                sync_scene(a);
            }
            return true;
        case IDC_SC_KEEP: a.sc_keep = SendMessage(a.su.keep, BM_GETCHECK, 0, 0) == BST_CHECKED; return true;
        case IDC_SC_TOEND: return true;
        case IDC_SC_X:
        case IDC_SC_Y:
            if (code == EN_KILLFOCUS && !a.sc_loading && s.loaded()) {  // typed positions apply when the field is left
                s.move_to(a.sc_pos, a.sc_sel, get_number(a.su.x, 0), get_number(a.su.y, 0), a.sc_keep);
                refresh(a);
            }
            return true;
        case IDC_SC_SPRITE:
            if (code == EN_CHANGE && !a.sc_loading && s.loaded() && a.sc_sel < SceneDoc::kObjects) {
                s.set_sprite(a.sc_pos, a.sc_sel, std::clamp(get_number(a.su.sprite, 0), 0, 255), SendMessage(a.su.to_end, BM_GETCHECK, 0, 0) == BST_CHECKED);
                refresh(a);
            }
            return true;
        case IDC_SC_SOUND:
            if (code == EN_CHANGE && !a.sc_loading && s.loaded() && a.sc_pos >= 1) {
                s.set_sound(a.sc_pos, get_number(a.su.sound, 0));
                refresh(a);
            }
            return true;
        case IDC_SC_INS:
            if (!s.loaded()) return true;
            scene_stop(a);
            s.insert_frame(a.sc_pos);
            a.sc_pos++;
            refresh(a);
            return true;
        case IDC_SC_DUP:
            if (!s.loaded() || a.sc_pos < 1) return true;
            scene_stop(a);
            s.duplicate_frame(a.sc_pos);
            a.sc_pos++;
            refresh(a);
            return true;
        case IDC_SC_DEL:
            if (!s.loaded() || a.sc_pos < 1) return true;
            scene_stop(a);
            s.delete_frame(a.sc_pos);
            refresh(a);
            return true;
        default: return false;
    }
}

// Path tools and shirt colours
bool scene_route_command(App& a, UINT id, UINT code) {
    SceneDoc& s = *a.scene;
    SceneUi& u = a.su;
    if (id == IDC_SC_MOVE || id == IDC_SC_SMOOTH) {
        if (!s.loaded()) return true;
        scene_stop(a);
        const int to = get_number(u.route_to, a.sc_pos + 1);
        if (to <= a.sc_pos || to > s.frames()) {
            s.set_status("Bahn: das Zielbild muss nach dem aktuellen Bild liegen (höchstens Bild " + std::to_string(s.frames()) + ")");
        } else if (id == IDC_SC_MOVE) {
            s.route(a.sc_sel, a.sc_pos, to, get_number(u.route_x, 0), get_number(u.route_y, 0), a.sc_keep);
        } else {
            s.smooth(a.sc_sel, a.sc_pos, to);
        }
        a.sc_route_key = -1;
        refresh(a);
        return true;
    }
    if ((id == IDC_SC_HOME || id == IDC_SC_AWAY) && code == CBN_SELCHANGE) {
        const int side = id == IDC_SC_HOME ? 0 : 1;
        HWND c = side ? u.away : u.home;
        const LRESULT i = SendMessage(c, CB_GETCURSEL, 0, 0);
        const int team = i >= 0 ? int(SendMessage(c, CB_GETITEMDATA, WPARAM(i), 0)) : -1;
        if (team < 0) {
            if (side) s.set_shirt(1, 0xF0, 0xF0, 0xF0);
            else s.set_shirt(0, 0xE0, 0x30, 0x30);
        } else {
            s.set_shirt_jersey(side, a.team->data(team).jersey);
        }
        sync_scene(a);
        return true;
    }
    return true;
}

// The arrows of the x / y fields step one pixel at once (typing applies when the field is left)
void scene_updown(App& a, NMUPDOWN* ud, int id) {
    SceneDoc& s = *a.scene;
    if (!s.loaded() || (id != IDC_SC_X + 1000 && id != IDC_SC_Y + 1000)) return;
    const auto p = s.position(a.sc_pos, a.sc_sel);
    const int step = ud->iDelta > 0 ? 1 : -1;
    s.move_to(a.sc_pos, a.sc_sel, p.x + (id == IDC_SC_X + 1000 ? step : 0), p.y + (id == IDC_SC_Y + 1000 ? step : 0), a.sc_keep);
    refresh(a);
}

// ------------------------------------------------------------------------------------------------ sound editor

std::wstring format_seconds(double s) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.2f s", s);
    return buf;
}

void sync_sound(App& a) {
    SoundUi& u = a.sn_ui;
    if (!u.view) return;
    SoundDoc& s = *a.sound;
    const bool on = s.current() >= 0;
    std::wstring info;
    if (on) {
        const VocSound& v = s.sound();
        info = SoundDoc::label(s.current()) + L"\r\n" + std::to_wstring(v.sample_rate) + L" Hz, 8 Bit, mono\r\n" + std::to_wstring(v.pcm.size()) + L" Samples, " + format_seconds(s.seconds()) +
               L"\r\nDatei " + std::to_wstring(s.file_size()) + L" von " + std::to_wstring(s.capacity()) + L" Byte (Puffer des Spiels)";
        if (s.sound().loops) info += L"\r\nWird im Spiel endlos wiederholt";
        if (a.sn_sel0 >= 0) info += L"\r\nAuswahl: " + format_seconds(double(a.sn_sel0) / v.sample_rate) + L" – " + format_seconds(double(a.sn_sel1) / v.sample_rate);
        const VocFit& f = s.last_fit();
        if (s.dirty() && f.source_rate) {
            info += L"\r\n\r\nImport: " + std::to_wstring(f.source_rate) + L" Hz → " + std::to_wstring(v.sample_rate) + L" Hz";
            if (f.trimmed) info += L"\r\nDas Ende wurde abgeschnitten (passt nicht in den Puffer)";
            if (f.clipped) info += L"\r\nÜbersteuert";
            wchar_t g[32];
            swprintf(g, 32, L"\r\nLautstärke ×%.2f", f.gain);
            info += g;
        }
    } else {
        info = L"Links einen Ton wählen";
    }
    SetWindowTextW(u.info, info.c_str());
    a.sn_loading = true;
    SendMessage(u.loop, BM_SETCHECK, on && s.sound().loops ? BST_CHECKED : BST_UNCHECKED, 0);
    a.sn_loading = false;
    for (HWND w : {u.play, u.stop, u.exp, u.imp, u.loop, u.trim}) EnableWindow(w, on);
    EnableWindow(u.trim, on && a.sn_sel0 >= 0);
    InvalidateRect(u.view, nullptr, FALSE);
}

void sound_stop() { PlaySoundW(nullptr, nullptr, 0); }

void sound_play(App& a) {
    SoundDoc& s = *a.sound;
    if (s.current() < 0) return;
    VocSound v = s.sound();
    if (a.sn_sel0 >= 0 && a.sn_sel1 > a.sn_sel0) v.pcm = std::vector<uint8_t>(v.pcm.begin() + a.sn_sel0, v.pcm.begin() + a.sn_sel1);
    a.sn_wav = em::encode_wav8(v);
    DWORD flags = SND_MEMORY | SND_ASYNC | SND_NODEFAULT;
    if (v.loops && a.sn_sel0 < 0) flags |= SND_LOOP;
    PlaySoundW(reinterpret_cast<LPCWSTR>(a.sn_wav.data()), nullptr, flags);
}

void open_sound(App& a, int number) {
    sound_stop();
    a.sn_sel0 = a.sn_sel1 = -1;
    if (a.sound->load(number)) a.sn_orig_rate = a.sound->sound().sample_rate;
    sync_sound(a);
}

struct WaveGeo {
    int ox, oy, w, h;
    size_t samples;
};

WaveGeo wave_geo(App& a) {
    RECT r;
    GetClientRect(a.sn_ui.view, &r);
    const int m = a.px(14);
    return {m, m, std::max(10, int(r.right) - 2 * m), std::max(10, int(r.bottom) - 2 * m - a.px(18)), a.sound->sound().pcm.size()};
}

int wave_sample_at(App& a, int mx) {
    const WaveGeo g = wave_geo(a);
    if (!g.samples) return 0;
    return int(std::clamp<long long>(static_cast<long long>(mx - g.ox) * static_cast<long long>(g.samples) / g.w, 0, static_cast<long long>(g.samples)));
}

void paint_wave(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(32, 34, 38));
    FillRect(mem, &cr, bg);
    DeleteObject(bg);
    SelectObject(mem, a.font);
    SetBkMode(mem, TRANSPARENT);
    SoundDoc& s = *a.sound;
    if (s.current() >= 0 && !s.sound().pcm.empty()) {
        const WaveGeo g = wave_geo(a);
        const auto& pcm = s.sound().pcm;
        if (a.sn_sel0 >= 0) {
            const int x0 = g.ox + int(static_cast<long long>(a.sn_sel0) * g.w / static_cast<long long>(g.samples)), x1 = g.ox + int(static_cast<long long>(a.sn_sel1) * g.w / static_cast<long long>(g.samples));
            RECT sr{x0, g.oy, std::max(x1, x0 + 1), g.oy + g.h};
            HBRUSH sb = CreateSolidBrush(RGB(48, 72, 110));
            FillRect(mem, &sr, sb);
            DeleteObject(sb);
        }
        const int mid = g.oy + g.h / 2;
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(70, 74, 82));
        HGDIOBJ op = SelectObject(mem, grid);
        MoveToEx(mem, g.ox, mid, nullptr);
        LineTo(mem, g.ox + g.w, mid);
        SelectObject(mem, op);
        DeleteObject(grid);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(110, 200, 255));
        op = SelectObject(mem, pen);
        for (int x = 0; x < g.w; x++) {
            const size_t i0 = size_t(static_cast<long long>(x) * static_cast<long long>(g.samples) / g.w), i1 = std::max(i0 + 1, size_t(static_cast<long long>(x + 1) * static_cast<long long>(g.samples) / g.w));
            int lo = 255, hi = 0;
            for (size_t i = i0; i < i1 && i < g.samples; i++) {
                lo = std::min<int>(lo, pcm[i]);
                hi = std::max<int>(hi, pcm[i]);
            }
            MoveToEx(mem, g.ox + x, mid - (hi - 128) * (g.h / 2) / 128, nullptr);
            LineTo(mem, g.ox + x, mid - (lo - 128) * (g.h / 2) / 128 + 1);
        }
        SelectObject(mem, op);
        DeleteObject(pen);
        SetTextColor(mem, RGB(170, 175, 185));
        const std::wstring t0 = L"0 s", t1 = format_seconds(s.seconds());
        TextOutW(mem, g.ox, g.oy + g.h + a.px(2), t0.c_str(), int(t0.size()));
        SIZE sz;
        GetTextExtentPoint32W(mem, t1.c_str(), int(t1.size()), &sz);
        TextOutW(mem, g.ox + g.w - sz.cx, g.oy + g.h + a.px(2), t1.c_str(), int(t1.size()));
        const std::wstring hint = L"Bereich ziehen: Auswahl (Abspielen, Zuschneiden)";
        GetTextExtentPoint32W(mem, hint.c_str(), int(hint.size()), &sz);
        TextOutW(mem, g.ox + (g.w - sz.cx) / 2, g.oy + g.h + a.px(2), hint.c_str(), int(hint.size()));
    } else {
        SetTextColor(mem, RGB(190, 190, 195));
        DrawTextW(mem, L"Links einen Ton wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK wave_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_wave(a, h); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE; }
            break;
        case WM_LBUTTONDOWN:
            if (a.sound->current() < 0) return 0;
            SetFocus(h);
            SetCapture(h);
            a.sn_drag = wave_sample_at(a, GET_X_LPARAM(lp));
            a.sn_sel0 = a.sn_sel1 = -1;
            sync_sound(a);
            return 0;
        case WM_MOUSEMOVE:
            if (GetCapture() == h && a.sn_drag >= 0) {
                const int x = wave_sample_at(a, GET_X_LPARAM(lp));
                a.sn_sel0 = std::min(a.sn_drag, x);
                a.sn_sel1 = std::max(a.sn_drag, x);
                if (a.sn_sel1 - a.sn_sel0 < 2) a.sn_sel0 = a.sn_sel1 = -1;
                sync_sound(a);
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            a.sn_drag = -1;
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_SPACE) { sound_play(a); return 0; }
            if (wp == VK_ESCAPE) { sound_stop(); return 0; }
            break;
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void create_sound_controls(App& a) {
    SoundUi& u = a.sn_ui;
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int id) {
        HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | style, 0, 0, 0, 0, a.main, (HMENU)UINT_PTR(id), a.inst, nullptr);
        u.all.push_back(w);
        return w;
    };
    u.view = CreateWindowExW(WS_EX_CLIENTEDGE, kWaveClass, nullptr, WS_CHILD, 0, 0, 0, 0, a.main, nullptr, a.inst, nullptr);
    u.all.push_back(u.view);
    u.info = make(L"STATIC", L"", 0, 0, 0);
    u.play = make(L"BUTTON", L"Abspielen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SN_PLAY);
    u.stop = make(L"BUTTON", L"Stopp", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SN_STOP);
    u.exp = make(L"BUTTON", L"WAV exportieren…", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SN_EXPORT);
    u.imp = make(L"BUTTON", L"WAV importieren…", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SN_IMPORT);
    u.loop = make(L"BUTTON", L"Endlos wiederholen", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_SN_LOOP);
    u.lbl_q = make(L"STATIC", L"Qualität beim Import", 0, 0, 0);
    u.quality = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SN_QUALITY);
    for (const wchar_t* t : {L"Beste (bis 16 kHz)", L"Bis 11 kHz", L"Bis 8 kHz", L"Wie das Original"}) SendMessage(u.quality, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessage(u.quality, CB_SETCURSEL, 0, 0);
    u.norm = make(L"BUTTON", L"Lautstärke normalisieren", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_SN_NORM);
    SendMessage(u.norm, BM_SETCHECK, BST_CHECKED, 0);
    u.dither = make(L"BUTTON", L"Rauschen gegen Stufen (Dithering)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_SN_DITHER);
    u.trim = make(L"BUTTON", L"Auswahl behalten (zuschneiden)", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SN_TRIM);
    u.note = make(L"STATIC",
                  L"Das Spiel lädt jeden Ton in einen Puffer von der Größe der Originaldatei. Der Import wandelt jede WAV-Datei in 8 Bit mono und wählt die höchste Abtastrate, bei der der ganze Ton noch hineinpasst; nur wenn selbst 4 kHz nicht reichen, wird das Ende abgeschnitten.",
                  0, 0, 0);
    for (HWND w : u.all) SendMessage(w, WM_SETFONT, (WPARAM)a.font, TRUE);
}

void show_sound(App& a, bool on) {
    for (HWND w : a.sn_ui.all) ShowWindow(w, on ? SW_SHOW : SW_HIDE);
}

void layout_sound(App& a, int x, int y, int w, int h, int rx, int rw) {
    SoundUi& u = a.sn_ui;
    if (!u.view) return;
    MoveWindow(u.view, x, y, w, h, TRUE);
    const int m = a.px(10), bw = rw - 2 * m, bh = a.px(26);
    int py = y + m;
    MoveWindow(u.info, rx + m, py, bw, a.px(150), TRUE);
    py += a.px(158);
    MoveWindow(u.play, rx + m, py, (bw - a.px(6)) / 2, bh, TRUE);
    MoveWindow(u.stop, rx + m + (bw - a.px(6)) / 2 + a.px(6), py, (bw - a.px(6)) / 2, bh, TRUE);
    py += bh + a.px(6);
    MoveWindow(u.exp, rx + m, py, bw, bh, TRUE);
    py += bh + a.px(6);
    MoveWindow(u.imp, rx + m, py, bw, bh, TRUE);
    py += bh + a.px(10);
    MoveWindow(u.lbl_q, rx + m, py, bw, a.px(20), TRUE);
    py += a.px(22);
    MoveWindow(u.quality, rx + m, py, bw, a.px(200), TRUE);
    py += bh + a.px(4);
    MoveWindow(u.norm, rx + m, py, bw, a.px(22), TRUE);
    py += a.px(24);
    MoveWindow(u.dither, rx + m, py, bw, a.px(22), TRUE);
    py += a.px(30);
    MoveWindow(u.loop, rx + m, py, bw, a.px(22), TRUE);
    py += a.px(26);
    MoveWindow(u.trim, rx + m, py, bw, bh, TRUE);
    py += bh + a.px(12);
    MoveWindow(u.note, rx + m, py, bw, std::max(a.px(60), y + h - py - m), TRUE);
}

bool sound_command(App& a, UINT id, UINT code) {
    SoundDoc& s = *a.sound;
    switch (id) {
        case IDC_SN_PLAY: sound_play(a); return true;
        case IDC_SN_STOP: sound_stop(); return true;
        case IDC_SN_LOOP:
            if (code == BN_CLICKED && !a.sn_loading && s.current() >= 0) {
                s.set_loops(SendMessage(a.sn_ui.loop, BM_GETCHECK, 0, 0) == BST_CHECKED);
                refresh(a);
            }
            return true;
        case IDC_SN_TRIM:
            if (s.current() >= 0 && a.sn_sel0 >= 0) {
                sound_stop();
                s.trim(size_t(a.sn_sel0), size_t(a.sn_sel1));
                a.sn_sel0 = a.sn_sel1 = -1;
                refresh(a);
            }
            return true;
        case IDC_SN_EXPORT: {
            if (s.current() < 0) return true;
            const std::wstring p = file_dialog(true, L"Ton als WAV exportieren", L"WAV-Datei (*.wav)", L"*.wav", L"wav", L"kb" + std::to_wstring(s.current()) + L".wav", a.main);
            if (p.empty()) return true;
            const Bytes w = s.wav();
            std::ofstream f(fs::path(p), std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size()));
            s.set_status(f ? "Exportiert: " + fs::path(p).filename().string() : "Export fehlgeschlagen");
            refresh(a);
            return true;
        }
        case IDC_SN_IMPORT: {
            if (s.current() < 0) return true;
            const std::wstring p = file_dialog(false, L"WAV-Datei importieren", L"WAV-Datei (*.wav)", L"*.wav", nullptr, L"", a.main);
            if (p.empty()) return true;
            sound_stop();
            em::VocFitOptions o;
            switch (SendMessage(a.sn_ui.quality, CB_GETCURSEL, 0, 0)) {
                case 1: o.max_rate = 11025; break;
                case 2: o.max_rate = 8000; break;
                case 3: o.max_rate = std::max(a.sn_orig_rate, o.min_rate); break;
                default: o.max_rate = 16000; break;
            }
            o.normalize = SendMessage(a.sn_ui.norm, BM_GETCHECK, 0, 0) == BST_CHECKED;
            o.dither = SendMessage(a.sn_ui.dither, BM_GETCHECK, 0, 0) == BST_CHECKED;
            try {
                const bool ok = s.import_wav(em::read_file(fs::path(p)), o);
                a.sn_sel0 = a.sn_sel1 = -1;
                if (ok && s.last_fit().trimmed) {
                    TASKDIALOGCONFIG cfg{sizeof cfg};
                    cfg.hwndParent = a.main;
                    cfg.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
                    cfg.dwCommonButtons = TDCBF_OK_BUTTON;
                    cfg.pszWindowTitle = L"OpenEM Editor";
                    cfg.pszMainIcon = TD_WARNING_ICON;
                    cfg.pszMainInstruction = L"Der Ton ist zu lang für den Puffer des Spiels.";
                    cfg.pszContent = L"Selbst bei der niedrigsten Abtastrate (4 kHz) passt er nicht hinein. Das Ende wurde abgeschnitten. Kürzere WAV-Dateien behalten eine höhere Qualität.";
                    TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
                }
            } catch (const std::exception& e) {
                s.set_status(std::string("Import fehlgeschlagen: ") + e.what());
            }
            refresh(a);
            return true;
        }
        default: return false;
    }
}

// ------------------------------------------------------------------------------------ batch, mod packages, restore

bool ask_dialog(App& a, const wchar_t* head, const std::wstring& content, const wchar_t* ok_label) {
    TASKDIALOG_BUTTON buttons[] = {{101, ok_label}};
    TASKDIALOGCONFIG cfg{sizeof cfg};
    cfg.hwndParent = a.main;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle = L"OpenEM Editor";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = head;
    cfg.pszContent = content.c_str();
    cfg.pButtons = buttons;
    cfg.cButtons = 1;
    cfg.nDefaultButton = 101;
    int pressed = 0;
    return SUCCEEDED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr)) && pressed == 101;
}

void info_dialog(App& a, const wchar_t* head, const std::wstring& content, bool warning = false) {
    TASKDIALOGCONFIG cfg{sizeof cfg};
    cfg.hwndParent = a.main;
    cfg.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.dwCommonButtons = TDCBF_OK_BUTTON;
    cfg.pszWindowTitle = L"OpenEM Editor";
    cfg.pszMainIcon = warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
    cfg.pszMainInstruction = head;
    cfg.pszContent = content.c_str();
    TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
}

// Reloads every document from disk after files were replaced from outside (restore, mod package, batch import)
void reload_all(App& a) {
    sound_stop();
    if (a.ed->current() >= 0) a.ed->load(a.ed->current());
    if (a.pal->current() >= 0) a.pal->load(a.pal->current());
    if (a.map->current() >= 0) a.map->load(a.map->current());
    a.team->load();
    if (a.scene->loaded()) a.scene->load(a.scene->name());
    if (a.sound->current() >= 0) open_sound(a, a.sound->current());
    a.changes_rev = ~0u;
    switch_tab(a, a.tab_index);
}

// Plain images (tab 0) or the goal scene sheets (tab 1) of the list
std::vector<int> picture_numbers(App& a) {
    std::vector<int> out;
    for (const auto& f : a.ed->files())
        if ((f.kind == PixelEditor::Kind::Image) == (a.tab_index == 0)) out.push_back(f.number);
    return out;
}

void export_all(App& a) {
    if (a.tab_index > 1) return;
    const std::wstring dir = dialog_path(L"Ordner für die PNG-Dateien wählen", true, false, L"", a.main);
    if (dir.empty()) return;
    PixelEditor tmp(a.data);
    int ok = 0, bad = 0;
    for (int n : picture_numbers(a)) {
        if (tmp.load(n) && export_png(fs::path(dir) / ("kb" + std::to_string(n) + ".png"), tmp.image(), tmp.palette())) ok++;
        else bad++;
    }
    info_dialog(a, L"Export beendet", std::to_wstring(ok) + L" Bilder als kbNN.png nach\n" + dir + L" exportiert" + (bad ? L", " + std::to_wstring(bad) + L" fehlgeschlagen." : L"."), bad > 0);
}

void import_dir(App& a) {
    if (a.tab_index > 1 || !confirm_discard(a)) return;
    const std::wstring dir = dialog_path(L"Ordner mit kbNN.png-Dateien wählen", true, false, L"", a.main);
    if (dir.empty()) return;
    std::vector<int> found;
    for (int n : picture_numbers(a))
        if (fs::exists(fs::path(dir) / ("kb" + std::to_string(n) + ".png"))) found.push_back(n);
    if (found.empty()) {
        info_dialog(a, L"Nichts zu importieren", L"Im Ordner liegen keine Dateien kbNN.png zu den Bildern dieser Registerkarte.", true);
        return;
    }
    if (!ask_dialog(a, (std::to_wstring(found.size()) + L" Bilder importieren?").c_str(), L"Die Dateien werden in die Spieldaten geschrieben (Sicherung als .bak). Jedes Pixel erhält die nächste Farbe der Palette. Die Größe muss mit dem Bild übereinstimmen.", L"Importieren"))
        return;
    PixelEditor tmp(a.data);
    int ok = 0, approx_files = 0;
    std::wstring problems;
    for (int n : found) {
        std::vector<uint32_t> px;
        int w = 0, h = 0, approx = 0;
        const std::wstring name = L"kb" + std::to_wstring(n) + L".png";
        if (!tmp.load(n) || !load_argb(fs::path(dir) / name, px, w, h)) {
            problems += name + L": nicht lesbar\n";
        } else if (!tmp.import_argb(px, w, h, false, &approx)) {
            problems += name + L": Größe " + std::to_wstring(w) + L" × " + std::to_wstring(h) + L" passt nicht\n";
        } else if (!tmp.save()) {
            problems += name + L": Speichern fehlgeschlagen\n";
        } else {
            ok++;
            approx_files += approx > 0;
        }
    }
    reload_all(a);
    std::wstring text = std::to_wstring(ok) + L" von " + std::to_wstring(found.size()) + L" Bildern importiert";
    if (approx_files) text += L"; bei " + std::to_wstring(approx_files) + L" davon mussten Farben der Palette angenähert werden.";
    if (!problems.empty()) text += L"\n\nNicht importiert:\n" + problems;
    info_dialog(a, L"Import beendet", text, !problems.empty());
}

void modpack_create(App& a) {
    const auto files = em::editor::changed_files(a.data);
    if (files.empty()) {
        info_dialog(a, L"Keine Änderungen", L"Es gibt keine Datei, die sich von ihrer Sicherung (.bak) unterscheidet.");
        return;
    }
    const std::wstring p = file_dialog(true, L"Mod-Paket speichern", L"ZIP-Datei (*.zip)", L"*.zip", L"zip", L"em_mod.zip", a.main);
    if (p.empty()) return;
    std::string err;
    const size_t n = em::editor::create_modpack(a.data, fs::path(p), &err);
    std::wstring list;
    for (size_t i = 0; i < files.size() && i < 12; i++) list += widen(files[i].path) + L"\n";
    if (files.size() > 12) list += L"… und " + std::to_wstring(files.size() - 12) + L" weitere\n";
    if (n) info_dialog(a, (std::to_wstring(n) + L" Dateien im Mod-Paket").c_str(), widen(fs::path(p).filename().string()) + L":\n\n" + list);
    else info_dialog(a, L"Mod-Paket nicht erstellt", widen(err), true);
}

void modpack_apply(App& a) {
    if (!confirm_discard(a)) return;
    const std::wstring p = file_dialog(false, L"Mod-Paket einspielen", L"ZIP-Datei (*.zip)", L"*.zip", nullptr, L"", a.main);
    if (p.empty()) return;
    em::editor::ZipEntries entries;
    try {
        entries = em::editor::read_zip(em::read_file(fs::path(p)));
    } catch (const std::exception& e) {
        info_dialog(a, L"Paket nicht lesbar", widen(e.what()), true);
        return;
    }
    std::wstring list;
    for (size_t i = 0; i < entries.size() && i < 12; i++) list += widen(entries[i].first) + L"\n";
    if (entries.size() > 12) list += L"… und " + std::to_wstring(entries.size() - 12) + L" weitere\n";
    if (!ask_dialog(a, (std::to_wstring(entries.size()) + L" Dateien einspielen?").c_str(), L"Sie ersetzen die Dateien im Spieleordner (von jeder bisherigen Datei bleibt eine Sicherung .bak, falls noch keine da ist):\n\n" + list, L"Einspielen"))
        return;
    std::string err;
    const int n = em::editor::apply_modpack(a.data, fs::path(p), &err);
    if (n < 0) {
        info_dialog(a, L"Paket nicht eingespielt", widen(err), true);
        return;
    }
    reload_all(a);
    info_dialog(a, (std::to_wstring(n) + L" Dateien eingespielt").c_str(), L"Der Editor hat alles neu geladen.");
}

// KB/80.vga or TORE/2.at of the open document
std::string current_rel_path(App& a) {
    if (scene_tab(a)) return a.scene->loaded() ? "TORE/" + a.scene->name() : "";
    if (sound_tab(a)) return a.sound->current() >= 0 ? "KB/" + std::to_string(a.sound->current()) + ".vga" : "";
    if (team_tab(a)) return "KB/23.vga";
    const int n = map_tab(a) ? a.map->current() : pal_tab(a) ? a.pal->current() : a.ed->current();
    return n >= 0 ? "KB/" + std::to_string(n) + ".vga" : "";
}

void restore_current(App& a) {
    const std::string rel = current_rel_path(a);
    if (rel.empty()) return;
    fs::path bak = a.data / rel;
    bak += ".bak";
    std::error_code ec;
    if (!fs::exists(bak, ec)) {
        info_dialog(a, L"Keine Sicherung", widen(rel) + L" wurde vom Editor noch nie überschrieben, es gibt kein .bak. Die Datei ist unverändert.");
        return;
    }
    if (!confirm_discard(a)) return;
    if (!ask_dialog(a, (widen(rel) + L" auf das Original zurücksetzen?").c_str(), L"Die Datei wird durch die Sicherung (.bak) ersetzt, alle Änderungen daran gehen verloren.", L"Zurücksetzen")) return;
    if (!em::editor::restore_file(a.data, rel)) {
        info_dialog(a, L"Zurücksetzen fehlgeschlagen", widen(rel), true);
        return;
    }
    reload_all(a);
}

void new_scene(App& a) {
    if (!scene_tab(a) || !a.scene->loaded() || !confirm_discard(a)) return;
    const std::string from = a.scene->name();
    if (!ask_dialog(a, L"Neue Szene anlegen?", L"Die vier Skripte (Heim/Gast, Tor/vergebene Chance) der Szene " + SceneDoc::describe(from) + L" werden unter der nächsten freien Nummer kopiert und TORE\\anzahl wird erhöht, damit das Spiel die neue Szene auch auswählt. Danach lässt sie sich bearbeiten.", L"Anlegen"))
        return;
    const int n = a.scene->add_scene_set(from);
    if (n < 0) {
        info_dialog(a, L"Neue Szene nicht angelegt", widen(a.scene->status()), true);
        return;
    }
    const size_t dot = from.find('.');
    const std::string created = std::to_string(n) + from.substr(dot);
    fill_list(a);
    for (size_t i = 0; i < a.sc_names.size(); i++)
        if (a.sc_names[i] == created) open_scene(a, int(i));
    select_in_list(a, int(std::find(a.sc_names.begin(), a.sc_names.end(), created) - a.sc_names.begin()));
    refresh(a);
}

// Copy, cut, paste, delete, select all, flip, rotate of the image canvas
void image_edit_command(App& a, UINT id) {
    PixelEditor& e = *a.ed;
    if (e.current() < 0 || a.tab_index > 1) return;
    switch (id) {
        case ID_EDIT_COPY: e.copy(); break;
        case ID_EDIT_CUT: e.cut(); break;
        case ID_EDIT_PASTE: e.paste(a.hover_x >= 0 ? a.hover_x : 0, a.hover_y >= 0 ? a.hover_y : 0); break;
        case ID_EDIT_DELETE: e.delete_selection(); break;
        case ID_EDIT_SELECTALL: e.select_all(); break;
        case ID_EDIT_FLIPH: e.flip(true); break;
        case ID_EDIT_FLIPV: e.flip(false); break;
        case ID_EDIT_ROTATE: e.rotate_cw(); break;
        default: break;
    }
}

// ------------------------------------------------------------------------------------------ palette editor

struct GridGeo {
    int x, y, cell;
};

GridGeo grid_geo(App& a, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    const int m = a.px(16), text = a.px(50);
    const int size = std::max(32, std::min(int(r.right) - 2 * m, int(r.bottom) - 2 * m - text));
    const int cell = size / 16;
    return {(int(r.right) - cell * 16) / 2, m, cell};
}

void paint_pgrid(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &cr, GetSysColorBrush(COLOR_WINDOW));
    SelectObject(mem, a.font);
    SetBkMode(mem, TRANSPARENT);
    if (a.pal->current() >= 0) {
        GridGeo g = grid_geo(a, h);
        const em::Palette p = a.pal->palette();
        for (int i = 0; i < 256; i++) {
            const auto& c = p.rgb[size_t(i)];
            HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
            RECT r{g.x + (i % 16) * g.cell, g.y + (i / 16) * g.cell, g.x + (i % 16 + 1) * g.cell, g.y + (i / 16 + 1) * g.cell};
            FillRect(mem, &r, b);
            DeleteObject(b);
        }
        const int s = a.pal_sel;
        RECT sr{g.x + (s % 16) * g.cell, g.y + (s / 16) * g.cell, g.x + (s % 16 + 1) * g.cell, g.y + (s / 16 + 1) * g.cell};
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
        InflateRect(&sr, 1, 1);
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        InflateRect(&sr, -2, -2);
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        const auto& c = p.rgb[size_t(s)];
        const auto six = a.pal->get(s);
        const int ty = g.y + g.cell * 16 + a.px(12);
        HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
        RECT sw{g.x, ty, g.x + a.px(40), ty + a.px(32)};
        FillRect(mem, &sw, b);
        DeleteObject(b);
        FrameRect(mem, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
        std::wstring t1 = L"Farbe " + std::to_wstring(s) + L"   DAC " + std::to_wstring(six[0]) + L", " + std::to_wstring(six[1]) + L", " + std::to_wstring(six[2]) +
                          L"   (Anzeige " + std::to_wstring(c[0]) + L", " + std::to_wstring(c[1]) + L", " + std::to_wstring(c[2]) + L")";
        TextOutW(mem, g.x + a.px(50), ty + a.px(7), t1.c_str(), int(t1.size()));
    } else {
        SetTextColor(mem, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(mem, L"Palette links wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK pgrid_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_pgrid(a, h); return 0;
        case WM_LBUTTONDOWN: {
            if (a.pal->current() < 0) return 0;
            GridGeo g = grid_geo(a, h);
            const int x = GET_X_LPARAM(lp) - g.x, y = GET_Y_LPARAM(lp) - g.y;
            if (x >= 0 && y >= 0 && x / g.cell < 16 && y / g.cell < 16) {
                a.pal_sel = y / g.cell * 16 + x / g.cell;
                sync_sliders(a);
                refresh(a);
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void slider_changed(App& a) {
    if (a.setting_sliders || a.pal->current() < 0) return;
    const int r = int(SendMessage(a.track[0], TBM_GETPOS, 0, 0)), g = int(SendMessage(a.track[1], TBM_GETPOS, 0, 0)),
              b = int(SendMessage(a.track[2], TBM_GETPOS, 0, 0));
    a.pal->set(a.pal_sel, r, g, b);
    sync_sliders(a);
    refresh(a);
}

// ------------------------------------------------------------------------------------------ main window

void build_menu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(), tool = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_SAVE, L"&Speichern\tStrg+S");
    AppendMenuW(file, MF_STRING, ID_FILE_IMPORT, L"Bild &importieren…	Strg+I");
    AppendMenuW(file, MF_STRING, ID_FILE_EXPORT, L"Bild &exportieren… (PNG, BMP)	Strg+E");
    AppendMenuW(file, MF_STRING, ID_FILE_EXPORTALL, L"&Alle Bilder exportieren…");
    AppendMenuW(file, MF_STRING, ID_FILE_IMPORTDIR, L"Bilder aus &Ordner importieren…");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_NEWSCENE, L"&Neue Szene anlegen…");
    AppendMenuW(file, MF_STRING, ID_FILE_RESTORE, L"Datei auf &Original zurücksetzen…");
    AppendMenuW(file, MF_STRING, ID_FILE_MODPACK, L"&Mod-Paket erstellen…");
    AppendMenuW(file, MF_STRING, ID_FILE_MODAPPLY, L"Mod-Paket e&inspielen…");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"&Beenden\tAlt+F4");
    AppendMenuW(edit, MF_STRING, ID_EDIT_UNDO, L"&Rückgängig\tStrg+Z");
    AppendMenuW(edit, MF_STRING, ID_EDIT_REDO, L"&Wiederholen\tStrg+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_EDIT_CUT, L"&Ausschneiden\tStrg+X");
    AppendMenuW(edit, MF_STRING, ID_EDIT_COPY, L"&Kopieren\tStrg+C");
    AppendMenuW(edit, MF_STRING, ID_EDIT_PASTE, L"&Einfügen\tStrg+V");
    AppendMenuW(edit, MF_STRING, ID_EDIT_DELETE, L"&Löschen\tEntf");
    AppendMenuW(edit, MF_STRING, ID_EDIT_SELECTALL, L"A&lles auswählen\tStrg+A");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_EDIT_FLIPH, L"Horizontal &spiegeln");
    AppendMenuW(edit, MF_STRING, ID_EDIT_FLIPV, L"&Vertikal spiegeln");
    AppendMenuW(edit, MF_STRING, ID_EDIT_ROTATE, L"Um 90° &drehen");
    AppendMenuW(tool, MF_STRING, ID_TOOL_PENCIL, L"&Stift\tP");
    AppendMenuW(tool, MF_STRING, ID_TOOL_LINE, L"&Linie\tL");
    AppendMenuW(tool, MF_STRING, ID_TOOL_RECT, L"&Rechteck\tR");
    AppendMenuW(tool, MF_STRING, ID_TOOL_FILLRECT, L"&Gefülltes Rechteck\tB");
    AppendMenuW(tool, MF_STRING, ID_TOOL_FILL, L"&Füllen\tF");
    AppendMenuW(tool, MF_STRING, ID_TOOL_PICKER, L"&Pipette\tI");
    AppendMenuW(tool, MF_STRING, ID_TOOL_SELECT, L"&Auswahl\tM");
    AppendMenuW(view, MF_STRING, ID_VIEW_GRID, L"&Raster\tG");
    AppendMenuW(view, MF_STRING, ID_VIEW_FIT, L"Größe &anpassen");
    AppendMenuW(view, MF_STRING, ID_VIEW_ZOOMIN, L"&Vergrößern\t+");
    AppendMenuW(view, MF_STRING, ID_VIEW_ZOOMOUT, L"&Verkleinern\t-");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_VIEW_DEVIATION, L"&Palettenabweichung des Imports zeigen");
    AppendMenuW(view, MF_STRING, ID_VIEW_CHANGES, L"Änderungen gegenüber dem &Original zeigen");
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&Info");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&Datei");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Bearbeiten");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tool, L"&Werkzeug");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&Ansicht");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Hilfe");
    SetMenu(hwnd, bar);
}

void build_toolbar(App& a) {
    a.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER,
                                0, 0, 0, 0, a.main, (HMENU)IDC_TOOLBAR, a.inst, nullptr);
    SendMessage(a.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(a.toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    auto btn = [](int id, const wchar_t* text, BYTE style) {
        TBBUTTON b{};
        b.iBitmap = I_IMAGENONE;
        b.idCommand = id;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = BYTE(style | BTNS_SHOWTEXT | BTNS_AUTOSIZE);
        b.iString = (INT_PTR)text;
        return b;
    };
    auto sep = [] { TBBUTTON b{}; b.fsStyle = BTNS_SEP; return b; };
    TBBUTTON bs[] = {
        btn(ID_TOOL_PENCIL, L"Stift", BTNS_CHECKGROUP),      btn(ID_TOOL_LINE, L"Linie", BTNS_CHECKGROUP),
        btn(ID_TOOL_RECT, L"Rechteck", BTNS_CHECKGROUP),     btn(ID_TOOL_FILLRECT, L"Gefüllt", BTNS_CHECKGROUP),
        btn(ID_TOOL_FILL, L"Füllen", BTNS_CHECKGROUP),       btn(ID_TOOL_PICKER, L"Pipette", BTNS_CHECKGROUP), btn(ID_TOOL_SELECT, L"Auswahl", BTNS_CHECKGROUP),
        sep(),
        btn(ID_EDIT_UNDO, L"Rückgängig", BTNS_BUTTON),       btn(ID_EDIT_REDO, L"Wiederholen", BTNS_BUTTON),
        sep(),
        btn(ID_FILE_SAVE, L"Speichern", BTNS_BUTTON),
        sep(),
        btn(ID_VIEW_GRID, L"Raster", BTNS_CHECK),            btn(ID_VIEW_FIT, L"Anpassen", BTNS_BUTTON),
    };
    SendMessage(a.toolbar, TB_ADDBUTTONS, UINT(sizeof bs / sizeof bs[0]), (LPARAM)bs);
}

void init_list(App& a) {
    ListView_SetExtendedListViewStyle(a.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowTheme(a.list, L"Explorer", nullptr);  // modern list look
    LVCOLUMNW col{LVCF_TEXT | LVCF_WIDTH};
    col.pszText = const_cast<wchar_t*>(L"Datei");
    col.cx = a.px(160);
    ListView_InsertColumn(a.list, 0, &col);
    col.pszText = const_cast<wchar_t*>(L"Größe");
    col.cx = a.px(90);
    ListView_InsertColumn(a.list, 1, &col);
    fill_list(a);
}

void command(App& a, UINT id) {
    PixelEditor& e = *a.ed;
    switch (id) {
        case ID_FILE_SAVE:
            if (sound_tab(a)) a.sound->save();
            else if (scene_tab(a)) a.scene->save();
            else if (map_tab(a)) a.map->save();
            else if (team_tab(a)) team_save(a);
            else if (pal_tab(a)) a.pal->save();
            else e.save();
            break;
        case ID_FILE_EXPORT: {
            if (e.current() < 0 || pal_tab(a) || team_tab(a) || map_tab(a) || scene_tab(a) || sound_tab(a)) break;
            const std::wstring p = image_dialog(true, L"kb" + std::to_wstring(e.current()) + L".png", a.main);
            if (p.empty()) break;
            const fs::path path(p);
            std::wstring ext = path.extension().wstring();
            for (auto& ch : ext) ch = wchar_t(towlower(ch));
            if (ext == L".bmp") e.export_bmp(path);
            else e.set_status(export_png(path, e.image(), e.palette()) ? "Exportiert: " + path.filename().string() : "Export fehlgeschlagen");
            break;
        }
        case ID_FILE_IMPORT: {
            if (e.current() < 0 || pal_tab(a) || team_tab(a) || map_tab(a) || scene_tab(a) || sound_tab(a)) break;
            const std::wstring p = image_dialog(false, L"", a.main);
            if (p.empty()) break;
            std::vector<uint32_t> px;
            int w = 0, h = 0;
            if (!load_argb(fs::path(p), px, w, h)) {
                e.set_status("Import: Datei konnte nicht gelesen werden");
                break;
            }
            bool resize = false;
            if ((w != e.image().width || h != e.image().height) && e.picture_kind() == PixelEditor::Kind::Image) {
                const std::wstring head = L"Die Datei ist " + std::to_wstring(w) + L" × " + std::to_wstring(h) + L", das Bild " + std::to_wstring(e.image().width) + L" × " + std::to_wstring(e.image().height) + L".";
                TASKDIALOG_BUTTON buttons[] = {{101, L"Größe des Bildes ändern"}};
                TASKDIALOGCONFIG cfg{sizeof cfg};
                cfg.hwndParent = a.main;
                cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
                cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
                cfg.pszWindowTitle = L"OpenEM Editor";
                cfg.pszMainIcon = TD_WARNING_ICON;
                cfg.pszMainInstruction = head.c_str();
                cfg.pszContent = L"Das Spiel erwartet für viele Bilder feste Größen. Rückgängig ist danach für dieses Bild nicht mehr möglich.";
                cfg.pButtons = buttons;
                cfg.cButtons = 1;
                int pressed = 0;
                if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr)) || pressed != 101) break;
                resize = true;
            }
            if (e.import_argb(px, w, h, resize)) {
                fit_zoom(a);
                a.view_deviation = e.last_import().approximated > 0;  // show where colours had to be approximated
            }
            break;
        }
        case ID_FILE_EXIT: SendMessage(a.main, WM_CLOSE, 0, 0); return;
        case ID_EDIT_UNDO:
            if (sound_tab(a)) { sound_stop(); a.sound->revert(); sync_sound(a); }  // sounds: discard all changes
            else if (scene_tab(a)) { scene_stop(a); a.scene->undo(); }
            else if (map_tab(a)) a.map->undo();
            else if (team_tab(a)) { a.team->revert(); fill_list(a); select_in_list(a, a.team_sel); load_team_controls(a); }  // teams: discard all changes
            else if (pal_tab(a)) { a.pal->revert(); sync_sliders(a); }  // palettes: discard all changes
            else e.undo();
            break;
        case ID_EDIT_REDO:
            if (sound_tab(a)) break;
            if (scene_tab(a)) { scene_stop(a); a.scene->redo(); }
            else if (map_tab(a)) a.map->redo();
            else e.redo();
            break;
        case ID_TOOL_PENCIL: case ID_TOOL_LINE: case ID_TOOL_RECT: case ID_TOOL_FILLRECT: case ID_TOOL_FILL: case ID_TOOL_PICKER: case ID_TOOL_SELECT:
            e.set_tool(PixelEditor::Tool(id - ID_TOOL_PENCIL));
            break;
        case ID_VIEW_GRID: a.grid = !a.grid; InvalidateRect(a.canvas, nullptr, FALSE); break;
        case ID_VIEW_FIT: fit_zoom(a); InvalidateRect(a.canvas, nullptr, FALSE); break;
        case ID_VIEW_ZOOMIN: if (map_tab(a)) set_map_zoom(a, a.map_zoom + 1); else set_zoom(a, a.zoom + 1); return;
        case ID_VIEW_ZOOMOUT: if (map_tab(a)) set_map_zoom(a, a.map_zoom - 1); else set_zoom(a, a.zoom - 1); return;
        case ID_EDIT_COPY: case ID_EDIT_CUT: case ID_EDIT_PASTE: case ID_EDIT_DELETE: case ID_EDIT_SELECTALL: case ID_EDIT_FLIPH: case ID_EDIT_FLIPV: case ID_EDIT_ROTATE:
            image_edit_command(a, id);
            break;
        case ID_FILE_EXPORTALL: export_all(a); break;
        case ID_FILE_IMPORTDIR: import_dir(a); break;
        case ID_FILE_MODPACK: modpack_create(a); break;
        case ID_FILE_MODAPPLY: modpack_apply(a); break;
        case ID_FILE_RESTORE: restore_current(a); break;
        case ID_FILE_NEWSCENE: new_scene(a); break;
        case ID_VIEW_DEVIATION: a.view_deviation = !a.view_deviation; InvalidateRect(a.canvas, nullptr, FALSE); break;
        case ID_VIEW_CHANGES: a.view_changes = !a.view_changes; a.changes_rev = ~0u; InvalidateRect(a.canvas, nullptr, FALSE); break;
        case ID_HELP_ABOUT:
            MessageBoxW(a.main, L"OpenEM Editor 0.2\nBilder, Szenen, Töne und Mannschaften des Eishockey Managers (1993) bearbeiten.\n\nGPL-3.0, github.com/Weyoune/OpenEM_Editor", L"Info", MB_OK | MB_ICONINFORMATION);
            break;
        default: break;
    }
    refresh(a);
}

LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_CREATE: {
            a.main = h;
            a.dpi = GetDpiForWindow(h);
            make_font(a);
            build_menu(h);
            build_toolbar(a);
            a.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, (HMENU)IDC_STATUS, a.inst, nullptr);
            a.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 0, 0, h,
                                     (HMENU)IDC_LIST, a.inst, nullptr);
            a.canvas = CreateWindowExW(WS_EX_CLIENTEDGE, kCanvasClass, nullptr, WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.palette = CreateWindowExW(0, kPaletteClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.psel = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, h, (HMENU)IDC_PALSEL, a.inst, nullptr);
            for (int n : a.ed->palette_choices()) {
                std::wstring label = n == 21 ? L"Palette: Spiel (KB21)" : n == 247 ? L"Palette: Torszenen (KB247)" : L"Palette: wie KB" + std::to_wstring(n);
                const LRESULT i = SendMessage(a.psel, CB_ADDSTRING, 0, (LPARAM)label.c_str());
                SendMessage(a.psel, CB_SETITEMDATA, WPARAM(i), n);
            }
            a.tab = CreateWindowExW(0, WC_TABCONTROLW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH, 0, 0, 0, 0, h, (HMENU)IDC_TAB, a.inst, nullptr);
            const wchar_t* tabs[] = {L"Bilder", L"Torszenen", L"Paletten", L"Mannschaften", L"Hallenkarten", L"Spielszenen", L"Töne"};
            for (int i = 0; i < 7; i++) {
                TCITEMW ti{TCIF_TEXT};
                ti.pszText = const_cast<wchar_t*>(tabs[i]);
                TabCtrl_InsertItem(a.tab, i, &ti);
            }
            TabCtrl_SetItemSize(a.tab, a.px(120), a.px(26));
            a.pgrid = CreateWindowExW(WS_EX_CLIENTEDGE, kPalGridClass, nullptr, WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.pinfo = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            for (int i = 0; i < 3; i++) {
                a.tlabel[i] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
                a.track[i] = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, 0, 0, 0, h, (HMENU)UINT_PTR(IDC_TRACK_R + i), a.inst, nullptr);
                SendMessage(a.track[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 63));
                SendMessage(a.track[i], TBM_SETTICFREQ, 8, 0);
                SendMessage(a.track[i], TBM_SETPAGESIZE, 0, 4);
            }
            for (HWND c : {a.toolbar, a.status, a.list, a.tab, a.pinfo, a.psel, a.tlabel[0], a.tlabel[1], a.tlabel[2]}) SendMessage(c, WM_SETFONT, (WPARAM)a.font, TRUE);
            create_team_controls(a);
            a.mu.view = CreateWindowExW(WS_EX_CLIENTEDGE, kMapClass, nullptr, WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.mu.tiles = CreateWindowExW(WS_EX_CLIENTEDGE, kTilesClass, nullptr, WS_CHILD | WS_VSCROLL, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.mu.mirror = CreateWindowExW(0, L"BUTTON", L"Gespiegelt", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, h, (HMENU)IDC_MIRROR, a.inst, nullptr);
            a.mu.lbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            SendMessage(a.mu.mirror, WM_SETFONT, (WPARAM)a.font, TRUE);
            SendMessage(a.mu.lbl, WM_SETFONT, (WPARAM)a.font, TRUE);
            create_scene_controls(a);
            create_sound_controls(a);
            init_list(a);
            return 0;
        }
        case WM_SIZE:
            if (a.canvas) layout(a);
            return 0;
        case WM_DPICHANGED: {
            a.dpi = HIWORD(wp);
            make_font(a);
            for (HWND c : {a.toolbar, a.status, a.list, a.tab, a.pinfo, a.psel, a.tlabel[0], a.tlabel[1], a.tlabel[2]}) SendMessage(c, WM_SETFONT, (WPARAM)a.font, TRUE);
            TabCtrl_SetItemSize(a.tab, a.px(110), a.px(26));
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {a.px(1000), a.px(740)};
            return 0;
        }
        case WM_COMMAND: {
            const UINT id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDC_LIST) return 0;
            if (id >= IDC_SC_LIST && id <= IDC_SC_DEL) {
                scene_command(a, id, code);
                return 0;
            }
            if (id >= IDC_SC_RTO && id <= IDC_SC_AWAY) {
                scene_route_command(a, id, code);
                return 0;
            }
            if (id >= IDC_SN_PLAY && id <= IDC_SN_TRIM) {
                sound_command(a, id, code);
                return 0;
            }
            if (id == IDC_CLUB || (id >= IDC_NAME0 && id < IDC_NAME0 + 25) || id == IDC_CREST || id == IDC_PLACE || (id >= IDC_STR0 && id < IDC_STR0 + 3)) {
                if (code == EN_CHANGE) team_edit_changed(a, int(id));
                return 0;
            }
            if (id >= IDC_FOR0 && id < IDC_FOR0 + 25) {
                if (code == BN_CLICKED && !a.loading_team) {
                    a.team->set_foreign(a.team_sel, int(id) - IDC_FOR0, SendMessage(a.tu.foreign[id - IDC_FOR0], BM_GETCHECK, 0, 0) == BST_CHECKED);
                    refresh(a);
                }
                return 0;
            }
            if (id == IDC_MIRROR) {
                if (code == BN_CLICKED) {
                    a.map_mirror = SendMessage(a.mu.mirror, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    sync_map_controls(a);
                }
                return 0;
            }
            if (id == IDC_SWAP_BTN) { team_swap(a); return 0; }
            if (id >= IDC_CREST_PEN && id <= IDC_CREST_ZIN) {
                PixelEditor& ce = *a.crest_ed;
                if (id == IDC_CREST_PEN) ce.set_tool(PixelEditor::Tool::Pencil);
                else if (id == IDC_CREST_FILL) ce.set_tool(PixelEditor::Tool::Fill);
                else if (id == IDC_CREST_UNDO) ce.undo();
                else if (id == IDC_CREST_ZOUT) set_crest_zoom(a, a.crest_zoom - 1);
                else if (id == IDC_CREST_ZIN) set_crest_zoom(a, a.crest_zoom + 1);
                else ce.save();
                InvalidateRect(a.tu.crest_view, nullptr, FALSE);
                refresh(a);
                return 0;
            }
            if (id == IDC_SWAP_COMBO) return 0;
            if (id == IDC_PALSEL) {
                if (code == CBN_SELCHANGE) {
                    const LRESULT i = SendMessage(a.psel, CB_GETCURSEL, 0, 0);
                    if (i >= 0) a.ed->set_palette_source(int(SendMessage(a.psel, CB_GETITEMDATA, WPARAM(i), 0)));
                    refresh(a);
                }
                return 0;
            }
            command(a, id);
            return 0;
        }
        case WM_DRAWITEM:
            if (reinterpret_cast<DRAWITEMSTRUCT*>(lp)->hwndItem == a.tu.preview) paint_team_preview(a, reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED && !a.selecting) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
                    open_image(a, a.list_numbers[size_t(lv->iItem)]);
            }
            if (nm->code == UDN_DELTAPOS) scene_updown(a, reinterpret_cast<NMUPDOWN*>(lp), int(nm->idFrom));
            if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGING) return !confirm_discard(a);  // TRUE vetoes
            if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) switch_tab(a, TabCtrl_GetCurSel(a.tab));
            return 0;
        }
        case WM_HSCROLL:
            if (HWND(lp) == a.su.track) {
                if (!a.sc_loading) {
                    scene_stop(a);
                    scene_goto(a, int(SendMessage(a.su.track, TBM_GETPOS, 0, 0)));
                }
                return 0;
            }
            for (int i = 0; i < 3; i++) {
                if (HWND(lp) == a.track[i]) slider_changed(a);
                if (HWND(lp) == a.tu.jer[i]) team_jersey_changed(a);
            }
            return 0;
        case WM_SETFOCUS: SetFocus(sound_tab(a) ? a.sn_ui.view : scene_tab(a) ? a.su.view : map_tab(a) ? a.mu.view : pal_tab(a) ? a.pgrid : a.canvas); return 0;
        case WM_CLOSE:
            if (confirm_discard(a)) DestroyWindow(h);
            return 0;
        case WM_TIMER:
            if (wp == kSceneTimer) {
                if (a.sc_pos < a.scene->frames()) scene_goto(a, a.sc_pos + 1);
                else scene_stop(a);
            }
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// ------------------------------------------------------------------------------------------- start-up

bool is_game_folder(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p / "EM.EXE", ec) && fs::is_directory(p / "KB", ec);
}

fs::path find_game_folder(const fs::path& exe_dir) {
    std::vector<fs::path> c;
    wchar_t env[MAX_PATH];
    if (GetEnvironmentVariableW(L"OPENEM_DATA", env, MAX_PATH)) c.push_back(env);
    for (fs::path up = exe_dir; !up.empty(); up = up.parent_path()) {
        c.push_back(up / "work" / "EM");
        if (up == up.parent_path()) break;
    }
    for (const fs::path& base : {exe_dir, fs::current_path()})
        for (const wchar_t* sub : {L"", L"EM", L"data"}) c.push_back(*sub ? base / sub : base);
    for (const auto& p : c)
        if (is_game_folder(p)) return p;
    return {};
}

void save_shot(HWND h, const wchar_t* path) {
    RECT r;
    GetWindowRect(h, &r);
    const int w = r.right - r.left, ht = r.bottom - r.top;
    HDC dc = GetDC(h);
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof bi.bmiHeader, w, ht, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(h, mem, PW_RENDERFULLCONTENT);
    BITMAPFILEHEADER fh{0x4D42, uint32_t(sizeof fh + sizeof bi.bmiHeader + size_t(w) * ht * 4), 0, 0, uint32_t(sizeof fh + sizeof bi.bmiHeader)};
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD n;
    WriteFile(f, &fh, sizeof fh, &n, nullptr);
    WriteFile(f, &bi.bmiHeader, sizeof bi.bmiHeader, &n, nullptr);
    WriteFile(f, bits, DWORD(size_t(w) * ht * 4), &n, nullptr);
    CloseHandle(f);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(h, dc);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icc);

    App app;
    g_app = &app;
    app.inst = inst;
    fs::path data;
    int file = -1, tab_arg = -1;
    std::wstring shot, export_png_arg, import_png_arg, scene_arg;
    int frame_arg = -1, mode_arg = -1;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring o = argv[i];
        if (o == L"--data") data = argv[i + 1];
        else if (o == L"--file") file = _wtoi(argv[i + 1]);
        else if (o == L"--shot") shot = argv[i + 1];
        else if (o == L"--export-png") export_png_arg = argv[i + 1];
        else if (o == L"--import-png") import_png_arg = argv[i + 1];
        else if (o == L"--tab") tab_arg = _wtoi(argv[i + 1]);
        else if (o == L"--scene") scene_arg = argv[i + 1];
        else if (o == L"--frame") frame_arg = _wtoi(argv[i + 1]);
        else if (o == L"--mode") mode_arg = _wtoi(argv[i + 1]);
    }
    LocalFree(argv);
    if (data.empty()) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        data = find_game_folder(fs::path(exe).parent_path());
    }
    while (!is_game_folder(data)) {
        MessageBoxW(nullptr, L"Der OpenEM Editor braucht deine Kopie des Originalspiels.\nBitte den Ordner mit EM.EXE und KB wählen.", L"OpenEM Editor", MB_OK | MB_ICONINFORMATION);
        std::wstring p = dialog_path(L"Ordner des Eishockey Managers wählen", true, false, L"", nullptr);
        if (p.empty()) return 1;
        data = p;
    }
    try {
        app.ed = std::make_unique<PixelEditor>(data);
        app.pal = std::make_unique<PaletteDoc>(data);
        app.crest_ed = std::make_unique<PixelEditor>(data);
        app.map = std::make_unique<MapDoc>(data);
        app.scene = std::make_unique<SceneDoc>(data);
        app.sound = std::make_unique<SoundDoc>(data);
        app.team = std::make_unique<TeamDoc>(data);
        app.team->load();
        app.data = data;
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, widen(e.what()).c_str(), L"OpenEM Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kMainClass;
    wc.lpfnWndProc = main_proc;
    RegisterClassExW(&wc);
    wc.hIcon = nullptr;
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;  // repaint everything on resize (no stale copies of the old picture)
    wc.lpszClassName = kCanvasClass;
    wc.lpfnWndProc = canvas_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kPaletteClass;
    wc.lpfnWndProc = palette_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kPalGridClass;
    wc.lpfnWndProc = pgrid_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kCrestClass;
    wc.lpfnWndProc = crest_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kMapClass;
    wc.lpfnWndProc = map_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kSceneClass;
    wc.lpfnWndProc = scene_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kWaveClass;
    wc.lpfnWndProc = wave_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kPickerClass;
    wc.lpfnWndProc = picker_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kTilesClass;
    wc.lpfnWndProc = tiles_proc;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kMainClass, L"OpenEM Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    // size for the real DPI
    const UINT dpi = GetDpiForWindow(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, MulDiv(1180, int(dpi), 96), MulDiv(760, int(dpi), 96), SWP_NOMOVE | SWP_NOZORDER);
    layout(app);
    int start_tab = 0;
    if (tab_arg >= 0) start_tab = tab_arg;
    for (const auto& f : app.ed->files())
        if (f.number == file && f.kind != PixelEditor::Kind::Image) start_tab = 1;
    app.tab_index = start_tab;
    TabCtrl_SetCurSel(app.tab, start_tab);
    switch_tab(app, start_tab);
    if (scene_tab(app) && !scene_arg.empty()) {
        const std::string want = fs::path(scene_arg).string();
        for (size_t i = 0; i < app.sc_names.size(); i++)
            if (app.sc_names[i] == want) {
                open_scene(app, int(i));
                select_in_list(app, int(i));
            }
    }
    if (scene_tab(app)) {
        if (mode_arg >= 0 && mode_arg <= 2) app.sc_mode = mode_arg;
        if (frame_arg >= 0) scene_goto(app, frame_arg);
        else sync_scene(app);
    }
    if (file >= 0 && start_tab < 2) {
        app.ed->load(file);
        fit_zoom(app);
        select_in_list(app, file);
    } else if (file >= 0) {
        app.pal->load(file);
        sync_sliders(app);
        select_in_list(app, file);
    }
    refresh(app);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    // command line conversion (also used by the tests): --file N with --export-png out.png or --import-png in.png (saves)
    if ((!export_png_arg.empty() || !import_png_arg.empty()) && file >= 0) {
        PixelEditor& e = *app.ed;
        if (!e.load(file)) return 2;
        if (!export_png_arg.empty()) return export_png(fs::path(export_png_arg), e.image(), e.palette()) ? 0 : 3;
        std::vector<uint32_t> px;
        int w = 0, h = 0;
        if (!load_argb(fs::path(import_png_arg), px, w, h) || !e.import_argb(px, w, h, true)) return 4;
        return e.save() ? 0 : 5;
    }
    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE},   {FCONTROL | FVIRTKEY, 'E', ID_FILE_EXPORT}, {FCONTROL | FVIRTKEY, 'I', ID_FILE_IMPORT}, {FCONTROL | FVIRTKEY, 'Z', ID_EDIT_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_EDIT_REDO},   {FVIRTKEY, 'P', ID_TOOL_PENCIL},           {FVIRTKEY, 'L', ID_TOOL_LINE},
        {FVIRTKEY, 'R', ID_TOOL_RECT},              {FVIRTKEY, 'B', ID_TOOL_FILLRECT},          {FVIRTKEY, 'F', ID_TOOL_FILL},
        {FVIRTKEY, 'I', ID_TOOL_PICKER},            {FVIRTKEY, 'M', ID_TOOL_SELECT},            {FCONTROL | FVIRTKEY, 'C', ID_EDIT_COPY},   {FCONTROL | FVIRTKEY, 'X', ID_EDIT_CUT},
        {FCONTROL | FVIRTKEY, 'V', ID_EDIT_PASTE},  {FVIRTKEY, VK_DELETE, ID_EDIT_DELETE},     {FCONTROL | FVIRTKEY, 'A', ID_EDIT_SELECTALL},            {FVIRTKEY, 'G', ID_VIEW_GRID},              {FVIRTKEY, VK_ADD, ID_VIEW_ZOOMIN},
        {FVIRTKEY, VK_OEM_PLUS, ID_VIEW_ZOOMIN},    {FVIRTKEY, VK_SUBTRACT, ID_VIEW_ZOOMOUT},   {FVIRTKEY, VK_OEM_MINUS, ID_VIEW_ZOOMOUT},
    };
    HACCEL accel = CreateAcceleratorTableW(keys, int(sizeof keys / sizeof keys[0]));

    if (!shot.empty()) {
        for (int i = 0; i < 20; i++) {
            MSG m;
            while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
            Sleep(20);
        }
        save_shot(hwnd, shot.c_str());
        return 0;
    }
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        // plain letter and plus/minus accelerators must not swallow text typed into an edit field
        wchar_t cls[16] = {};
        GetClassNameW(GetFocus(), cls, 16);
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, edit_keys = msg.message == WM_KEYDOWN && (msg.wParam == 'C' || msg.wParam == 'X' || msg.wParam == 'V' || msg.wParam == 'A' || msg.wParam == 'Z' || msg.wParam == 'Y');
        const bool typing = lstrcmpiW(cls, L"EDIT") == 0 && (!ctrl || edit_keys);  // an edit field keeps its own copy, paste, select all and undo
        if (typing || !TranslateAccelerator(hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return int(msg.wParam);
}
