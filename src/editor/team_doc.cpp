// SPDX-License-Identifier: GPL-3.0-only
#include "editor/team_doc.hpp"

#include <algorithm>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace em::editor {

namespace {
constexpr size_t kClubsBase = 0x4AD, kClubRecord = 0x20B, kFirstPlayer = 0x17, kPlayerRecord = 20;
// The foreigner byte of a player is the byte right behind its name (the flag of the next record).
size_t foreign_offset(int t, int p) {
    return kClubsBase + size_t(t) * kClubRecord + kFirstPlayer + size_t(p + 1) * kPlayerRecord;
}

std::string upper(std::string s) {  // ASCII only; umlauts keep their case (the game uses upper case)
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}
// Length in characters of a UTF-8 text
size_t chars(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}
}  // namespace

bool TeamDoc::load() {
    try {
        raw_ = read_file(file_);
        db_ = decode_db(raw_);
        td_ = decode_team_data(raw_);
        for (int t = kEuropeFirst; t < kAllTeams; t++) clubs_[size_t(t)] = db_.europe_clubs.at(size_t(t - kEuropeFirst));
        for (int t = 0; t < kTeams; t++) {
            clubs_[size_t(t)] = db_.clubs[size_t(t)].name;
            for (int p = 0; p < kSquad; p++) {
                names_[size_t(t)][size_t(p)] = db_.clubs[size_t(t)].players[size_t(p)].name;
                foreign_[size_t(t)][size_t(p)] = raw_[foreign_offset(t, p)];
            }
        }
        dirty_ = false;
        revision_++;
        status_ = "KB23 geladen: 64 Mannschaften";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
}

void TeamDoc::revert() { load(); }

bool TeamDoc::save() {
    try {
        Database db = db_;
        for (int t = 0; t < kTeams; t++) {
            db.clubs[size_t(t)].name = clubs_[size_t(t)];
            for (int p = 0; p < kSquad; p++) db.clubs[size_t(t)].players[size_t(p)].name = names_[size_t(t)][size_t(p)];
        }
        for (int t = kEuropeFirst; t < kAllTeams; t++) db.europe_clubs[size_t(t - kEuropeFirst)] = clubs_[size_t(t)];
        Bytes out = encode_db(raw_, db);
        out = encode_team_data(out, td_);
        for (int t = 0; t < kTeams; t++)
            for (int p = 0; p < kSquad; p++) out[foreign_offset(t, p)] = foreign_[size_t(t)][size_t(p)];
        fs::path bak = file_;
        bak += ".bak";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(file_, bak, ec);
        std::ofstream f(file_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        if (!f) throw std::runtime_error("Schreiben fehlgeschlagen");
        raw_ = std::move(out);
        db_ = decode_db(raw_);
        dirty_ = false;
        status_ = "KB23 gespeichert (Sicherung als .bak)";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Speichern fehlgeschlagen: ") + e.what();
        return false;
    }
}

bool TeamDoc::set_club_name(int t, const std::string& utf8) {
    const std::string s = upper(utf8);
    try {
        if (chars(s) > size_t(kClubLimit)) throw FormatError("Vereinsname: höchstens 22 Zeichen");
        try {
            (void)utf8_to_iso646de(s);
        } catch (const FormatError&) {
            throw FormatError("Zeichen nicht erlaubt: das Spiel kennt nur ASCII und Ä Ö Ü ä ö ü ß");
        }
    } catch (const std::exception& e) {
        status_ = e.what();
        return false;
    }
    if (clubs_[size_t(t)] != s) {
        clubs_[size_t(t)] = s;
        touch();
    }
    return true;
}

bool TeamDoc::set_player_name(int t, int p, const std::string& utf8) {
    if (!has_players(t)) return false;
    const std::string s = upper(utf8);
    try {
        if (chars(s) > size_t(kNameLimit)) throw FormatError("Spielername: höchstens 19 Zeichen");
        try {
            (void)utf8_to_iso646de(s);
        } catch (const FormatError&) {
            throw FormatError("Zeichen nicht erlaubt: das Spiel kennt nur ASCII und Ä Ö Ü ä ö ü ß");
        }
    } catch (const std::exception& e) {
        status_ = e.what();
        return false;
    }
    if (names_[size_t(t)][size_t(p)] != s) {
        names_[size_t(t)][size_t(p)] = s;
        touch();
    }
    return true;
}

void TeamDoc::set_foreign(int t, int p, bool on) {
    if (!has_players(t)) return;
    uint8_t& f = foreign_[size_t(t)][size_t(p)];
    if ((f != 0) == on) return;
    f = on ? 1 : 0;
    touch();
}

void TeamDoc::set_crest(int t, int crest) {
    crest = std::clamp(crest, 0, 63);
    if (t >= kTeamDataCrests || td_[size_t(t)].crest == crest) return;
    td_[size_t(t)].crest = crest;
    touch();
}

void TeamDoc::set_place(int t, int place) {
    place = std::clamp(place, 0, 12);
    if (t >= kTeamDataPlaces || td_[size_t(t)].place == place) return;
    td_[size_t(t)].place = place;
    touch();
}

void TeamDoc::set_jersey(int t, int r, int g, int b) {
    const uint16_t v = uint16_t(std::clamp(r, 0, 15) << 8 | std::clamp(g, 0, 15) << 4 | std::clamp(b, 0, 15));
    if (td_[size_t(t)].jersey == v) return;
    td_[size_t(t)].jersey = v;
    touch();
}

void TeamDoc::set_strength(int t, int k, int value) {
    value = std::clamp(value, 1, 99);
    uint8_t& s = td_[size_t(t)].strength[size_t(k)];
    if (s == value) return;
    s = uint8_t(value);
    touch();
}

void TeamDoc::swap_teams(int a, int b) {
    if (a == b || !editable(a) || !editable(b) || has_players(a) != has_players(b)) return;
    if (!has_players(a)) {  // European clubs: name, jersey and strengths
        std::swap(clubs_[size_t(a)], clubs_[size_t(b)]);
        std::swap(td_[size_t(a)], td_[size_t(b)]);
        touch();
        return;
    }
    std::swap(clubs_[size_t(a)], clubs_[size_t(b)]);
    std::swap(names_[size_t(a)], names_[size_t(b)]);
    std::swap(foreign_[size_t(a)], foreign_[size_t(b)]);
    std::swap(td_[size_t(a)], td_[size_t(b)]);
    // crest and place exist only for the first teams: where one side has no slot, the values stay at their index
    if (a >= kTeamDataCrests || b >= kTeamDataCrests) std::swap(td_[size_t(a)].crest, td_[size_t(b)].crest);
    if (a >= kTeamDataPlaces || b >= kTeamDataPlaces) std::swap(td_[size_t(a)].place, td_[size_t(b)].place);
    touch();
}

std::vector<std::string> TeamDoc::duplicate_names() const {
    std::map<std::string, int> count;
    for (int t = 0; t < kTeams; t++) {
        if (!editable(t)) continue;
        if (!clubs_[size_t(t)].empty()) count[clubs_[size_t(t)]]++;
        for (int p = 0; p < kSquad; p++)
            if (!names_[size_t(t)][size_t(p)].empty()) count[names_[size_t(t)][size_t(p)]]++;
    }
    std::vector<std::string> out;
    for (const auto& [n, c] : count)
        if (c > 1) out.push_back(n);
    return out;
}

}  // namespace em::editor
