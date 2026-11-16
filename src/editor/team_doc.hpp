// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the team editor for KB\23.vga: club and player names, foreigner flags, crest, league place,
// jersey colour and strength values of the 64 teams (48 clubs, LEER1-4, 12 national teams).
// Layout: docs/formats/HOK_TORE.md; tables: src/formats/db.hpp and teamdata.hpp.
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/db.hpp"
#include "formats/teamdata.hpp"

namespace em::editor {

class TeamDoc {
public:
    static constexpr int kTeams = 64, kSquad = 25;
    static constexpr int kEuropeFirst = 64, kAllTeams = 150;  // teams 64..149: European club names, with jersey and strengths only
    static constexpr int kNameLimit = 19, kClubLimit = 22;
    explicit TeamDoc(const std::filesystem::path& data) : file_(data / "KB" / "23.vga") {}

    bool load();
    bool save();    // writes KB\23.vga, first time with a .bak copy
    void revert();  // reload from disk
    bool dirty() const { return dirty_; }
    unsigned revision() const { return revision_; }
    const std::string& status() const { return status_; }

    // Teams 48..51 are the empty LEER1..LEER4 entries; the editor does not offer them
    static bool editable(int team) { return team >= 0 && team < kAllTeams && (team < 48 || team > 51); }
    static bool has_players(int team) { return team >= 0 && team < kTeams; }

    const std::string& club_name(int t) const { return clubs_[size_t(t)]; }
    const std::string& player_name(int t, int p) const { return names_[size_t(t)][size_t(p)]; }
    bool foreign(int t, int p) const { return foreign_[size_t(t)][size_t(p)] != 0; }
    const TeamData& data(int t) const { return td_[size_t(t)]; }

    // Setters return false (and set the status) when the value cannot be stored; names are upper-cased.
    bool set_club_name(int t, const std::string& utf8);
    bool set_player_name(int t, int p, const std::string& utf8);
    void set_foreign(int t, int p, bool on);
    void set_crest(int t, int crest);             // 0..63
    void set_place(int t, int place);             // 0..12
    void set_jersey(int t, int r, int g, int b);  // 0..15 per channel
    void set_strength(int t, int k, int value);   // k 0..2, 1..99
    // The whole team (names, players, flags, crest, place, jersey, strengths) changes position
    void swap_teams(int a, int b);

    // Names used more than once (players and clubs), for the warning before saving
    std::vector<std::string> duplicate_names() const;

    static std::array<int, 3> jersey_rgb(const TeamData& d) { return {(d.jersey >> 8) & 15, (d.jersey >> 4) & 15, d.jersey & 15}; }

private:
    std::filesystem::path file_;
    Bytes raw_;
    Database db_;
    std::vector<TeamData> td_;
    std::array<std::string, kAllTeams> clubs_;
    std::array<std::array<std::string, kSquad>, kTeams> names_;
    std::array<std::array<uint8_t, kSquad>, kTeams> foreign_{};
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;

    void touch() {
        dirty_ = true;
        revision_++;
    }
};

}  // namespace em::editor
