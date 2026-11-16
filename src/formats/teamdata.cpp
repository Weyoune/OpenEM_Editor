// SPDX-License-Identifier: GPL-3.0-only
#include "formats/teamdata.hpp"

namespace em {

namespace {
constexpr size_t kJersey = 0x000, kCrest = 0x12C, kStrength = 0x16C, kPlace = 0x46E;
}

std::vector<TeamData> decode_team_data(const Bytes& d) {
    if (d.size() < 0x4AD) throw FormatError("team data: file too short");
    std::vector<TeamData> out(kTeamDataTeams);
    for (size_t t = 0; t < out.size(); t++) {
        TeamData& e = out[t];
        e.jersey = uint16_t(d[kJersey + 2 * t] << 8 | d[kJersey + 2 * t + 1]);
        if (t < size_t(kTeamDataCrests)) e.crest = (d[kCrest + t] + 0xB0) & 0x3F;
        for (size_t k = 0; k < 3; k++) e.strength[k] = d[kStrength + 3 * t + k];
        if (t < size_t(kTeamDataPlaces)) e.place = d[kPlace + t];
    }
    return out;
}

Bytes encode_team_data(const Bytes& db, const std::vector<TeamData>& teams) {
    if (teams.size() != size_t(kTeamDataTeams)) throw FormatError("team data: need 150 teams");
    std::vector<TeamData> old = decode_team_data(db);
    Bytes out = db;
    for (size_t t = 0; t < teams.size(); t++) {
        const TeamData &n = teams[t], &o = old[t];
        out[kJersey + 2 * t] = uint8_t(n.jersey >> 8);
        out[kJersey + 2 * t + 1] = uint8_t(n.jersey);
        for (size_t k = 0; k < 3; k++) out[kStrength + 3 * t + k] = n.strength[k];
        if (t < size_t(kTeamDataCrests) && n.crest != o.crest) {
            if (n.crest < 0 || n.crest > 63) throw FormatError("team data: crest must be 0..63");
            out[kCrest + t] = uint8_t(n.crest + 0x50);  // (v + 0xB0) & 0x3F == crest
        }
        if (t < size_t(kTeamDataPlaces) && n.place != o.place) {
            if (n.place < 0 || n.place > 255) throw FormatError("team data: bad place");
            out[kPlace + t] = uint8_t(n.place);
        }
    }
    return out;
}

}  // namespace em
