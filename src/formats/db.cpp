// SPDX-License-Identifier: GPL-3.0-only
#include "formats/db.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kClubs = 0x4AD;
constexpr size_t kClubRecord = 0x20B;
constexpr size_t kClubName = 23;     // flag + 22 characters
constexpr size_t kPlayerRecord = 20;  // flag + 19 characters
constexpr size_t kEuropeRecord = 23;

std::string text(const Bytes& d, size_t off, size_t len) {
    std::string raw;
    for (size_t i = off; i < off + len && d[i] != 0; i++) raw += char(d[i]);
    return iso646de_to_utf8(raw.c_str());
}

// The European club names follow the 64 entries, one byte later (0x876E).
constexpr size_t kEurope = kClubs + kDbClubs * kClubRecord + 1;
}  // namespace

Database decode_db(const Bytes& d) {
    if (d.size() != kDbSize) throw FormatError("DB: KB\\23.vga must be 36648 bytes");
    Database db;
    for (int c = 0; c < kDbClubs; c++) {
        size_t base = kClubs + size_t(c) * kClubRecord;
        DbClub club;
        club.flag = d[base];
        club.name = text(d, base + 1, kClubName - 1);
        for (int p = 0; p < kDbSquad; p++) {
            size_t o = base + kClubName + size_t(p) * kPlayerRecord;
            DbPlayer pl;
            pl.flag = d[o];
            pl.name = text(d, o + 1, kPlayerRecord - 1);
            std::copy(d.begin() + long(o + 1), d.begin() + long(o + 1 + pl.copy.size()), pl.copy.begin());
            club.players.push_back(pl);
        }
        db.clubs.push_back(std::move(club));
    }
    for (size_t o = kEurope; o + kEuropeRecord <= d.size(); o += kEuropeRecord) {
        std::string n = text(d, o, kEuropeRecord);
        if (!n.empty()) db.europe_clubs.push_back(n);
    }
    return db;
}

namespace {
void put_text(Bytes& d, size_t off, size_t width, const std::string& utf8, bool clear_only_first) {
    if (utf8.empty()) {
        if (clear_only_first) d[off] = 0;
        else std::fill(d.begin() + long(off), d.begin() + long(off + width), uint8_t(0));
        return;
    }
    std::string iso = utf8_to_iso646de(utf8);
    if (iso.size() > width) throw FormatError("DB: name too long: " + utf8);
    std::fill(d.begin() + long(off), d.begin() + long(off + width), uint8_t(0));
    std::copy(iso.begin(), iso.end(), d.begin() + long(off));
}
}  // namespace

Bytes encode_db(const Bytes& original, const Database& db) {
    Database old = decode_db(original);
    if (db.clubs.size() != size_t(kDbClubs)) throw FormatError("DB: need 64 clubs");
    Bytes out = original;
    for (int c = 0; c < kDbClubs; c++) {
        const DbClub& nc = db.clubs[c];
        const DbClub& oc = old.clubs[c];
        if (nc.players.size() != size_t(kDbSquad)) throw FormatError("DB: need 25 players per club");
        size_t base = kClubs + size_t(c) * kClubRecord;
        out[base] = nc.flag;
        if (nc.name != oc.name) put_text(out, base + 1, kClubName - 1, nc.name, false);
        for (int p = 0; p < kDbSquad; p++) {
            size_t o = base + kClubName + size_t(p) * kPlayerRecord;
            out[o] = nc.players[p].flag;
            if (nc.players[p].name != oc.players[p].name)
                put_text(out, o + 1, kPlayerRecord - 1, nc.players[p].name, true);
        }
    }
    if (db.europe_clubs.size() == old.europe_clubs.size()) {  // fixed table of 23-byte records: 22 characters + NUL
        for (size_t i = 0; i < db.europe_clubs.size(); i++) {
            if (db.europe_clubs[i] == old.europe_clubs[i]) continue;
            if (db.europe_clubs[i].empty()) throw FormatError("DB: European club name must not be empty");
            const std::string iso = utf8_to_iso646de(db.europe_clubs[i]);
            if (iso.size() > 22) throw FormatError("DB: European club name too long: " + db.europe_clubs[i]);
            put_text(out, kEurope + i * kEuropeRecord, kEuropeRecord, db.europe_clubs[i], false);
        }
    }
    return out;
}

}  // namespace em
