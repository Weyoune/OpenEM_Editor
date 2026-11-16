// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Per-team tables in the first 0x4AD bytes of KB\23.vga (consumers: ovl04_4d3b, see docs/logic/WELTGENERATOR.md).
//
//   0x000  150 x u16 (bytes hi, lo): jersey colour of the goal scenes -> team record +0x3B; three nibbles that
//          are 0 or 0xF in the shipped file (white = 0x0FFF); teams 64..149 (European clubs) are 0
//   0x12C  64 x u8: crest. Team record +0x34 = (value + 0xB0) & 0x3F, KB image 80 + crest. Shipped values 0x50..0x8F.
//   0x16C  150 x 3 x u8: strength values; ovl04_4d3b uses the first two (a0, a1) as the base of the team skills
//          +0x18/+0x19 and +0x1A/+0x1B (rolled with a spread of a/10); the third is not read there.
//   0x46E  63 x u8: place 1..12 inside the league, in the shipped file the rank of the team by strength within its
//          league (Bundesliga: place 1 has the largest a0). No reader found yet; the editor keeps it as plain data.
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"

namespace em {

constexpr int kTeamDataTeams = 150;
constexpr int kTeamDataCrests = 64;   // teams with a crest byte
constexpr int kTeamDataPlaces = 63;   // teams with a place byte

struct TeamData {
    uint16_t jersey = 0;              // 0x0RGB-style nibble word, as the game composes it (hi byte first)
    int crest = -1;                   // 0..63, -1 for teams >= 64
    std::array<uint8_t, 3> strength{};
    int place = -1;                   // -1 for teams >= 63
};

std::vector<TeamData> decode_team_data(const Bytes& db);
// Writes the tables into a copy of `db`; fields that equal the decoded original are not touched.
// Throws FormatError for crest outside 0..63.
Bytes encode_team_data(const Bytes& db, const std::vector<TeamData>& teams);

}  // namespace em
