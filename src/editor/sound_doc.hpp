// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the sound editor: KB\237..241.vga (Creative Voice, 8 bit mono). Import converts any WAV to 8 bit mono
// at the best sample rate that still fits into the size of the original file (the game loads each sound into a buffer of
// that size), see formats/voc.hpp. No user interface in here.
#include <filesystem>
#include <string>
#include <vector>

#include "formats/voc.hpp"

namespace em::editor {

class SoundDoc {
public:
    explicit SoundDoc(const std::filesystem::path& data) : kb_(data / "KB") {}

    static const std::vector<int>& numbers();            // 237..241
    static std::wstring label(int number);                // "KB237  Publikum (Dauerschleife)"
    bool load(int number);
    void close();
    bool save();                                          // writes KB\<n>.vga, first time with a .bak copy
    void revert();                                        // back to the file on disk
    int current() const { return current_; }
    bool dirty() const { return dirty_; }
    unsigned revision() const { return revision_; }
    const std::string& status() const { return status_; }
    void set_status(const std::string& s) { status_ = s; }

    const VocSound& sound() const { return sound_; }
    size_t capacity() const { return capacity_; }         // size of the original file = the game's buffer
    size_t file_size() const;                             // size the sound would have as a file
    double seconds() const { return sound_.sample_rate ? double(sound_.pcm.size()) / sound_.sample_rate : 0.0; }
    Bytes wav() const { return encode_wav8(sound_); }     // for export and playback

    // Replaces the sound by a WAV file (any common PCM format); the result is reported in last_fit()
    bool import_wav(const Bytes& wav_file, const VocFitOptions& options = {});
    const VocFit& last_fit() const { return fit_; }
    // Edits of the current sound
    void set_loops(bool on);
    void trim(size_t from, size_t to);                    // keep samples [from, to)

private:
    std::filesystem::path kb_;
    int current_ = -1;
    VocSound sound_;
    size_t capacity_ = 0;
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;
    VocFit fit_;
};

}  // namespace em::editor
