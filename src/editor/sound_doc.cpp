// SPDX-License-Identifier: GPL-3.0-only
#include "editor/sound_doc.hpp"

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace em::editor {

const std::vector<int>& SoundDoc::numbers() {
    static const std::vector<int> n{237, 238, 239, 240, 241};
    return n;
}

std::wstring SoundDoc::label(int number) {
    switch (number) {
        case 237: return L"KB237  Publikum (Dauerschleife)";
        case 238: return L"KB238  Szenen-Ton 1";
        case 239: return L"KB239  Szenen-Ton 2";
        case 240: return L"KB240  Szenen-Ton 3";
        default: return L"KB241  Szenen-Ton 4";
    }
}

size_t SoundDoc::file_size() const { return sound_.pcm.empty() ? 0 : voc_file_size(sound_.pcm.size(), sound_.loops); }

bool SoundDoc::load(int number) {
    try {
        const fs::path p = kb_ / (std::to_string(number) + ".vga");
        sound_ = decode_voc(read_file(p));
        fs::path bak = p;
        bak += ".bak";
        std::error_code ec;
        // the buffer of the game has the size of the original file
        capacity_ = fs::exists(bak, ec) ? size_t(fs::file_size(bak, ec)) : size_t(fs::file_size(p, ec));
        current_ = number;
        dirty_ = false;
        revision_++;
        status_ = "KB" + std::to_string(number) + " geladen: " + std::to_string(sound_.sample_rate) + " Hz, " + std::to_string(sound_.pcm.size()) + " Samples";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Fehler: ") + e.what();
        return false;
    }
}

void SoundDoc::close() {
    current_ = -1;
    sound_ = VocSound();
    dirty_ = false;
    revision_++;
}

void SoundDoc::revert() {
    if (current_ >= 0) load(current_);
}

bool SoundDoc::save() {
    if (current_ < 0) return false;
    try {
        if (file_size() > capacity_) throw std::runtime_error("Ton ist groesser als der Puffer des Spiels");
        const Bytes d = encode_voc(sound_);
        const fs::path p = kb_ / (std::to_string(current_) + ".vga");
        fs::path bak = p;
        bak += ".bak";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(p, bak, ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
        if (!f) throw std::runtime_error("Schreiben fehlgeschlagen");
        dirty_ = false;
        status_ = "KB" + std::to_string(current_) + " gespeichert (Sicherung als .bak)";
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Speichern fehlgeschlagen: ") + e.what();
        return false;
    }
}

bool SoundDoc::import_wav(const Bytes& wav_file, const VocFitOptions& options) {
    if (current_ < 0) return false;
    try {
        VocFit fit = fit_voc(decode_wav(wav_file), capacity_, sound_.loops, options);
        sound_ = fit.sound;
        fit_ = fit;
        dirty_ = true;
        revision_++;
        status_ = "Importiert: " + std::to_string(fit.source_rate) + " Hz -> " + std::to_string(sound_.sample_rate) + " Hz, " +
                  std::to_string(fit.bytes) + " von " + std::to_string(fit.capacity) + " Byte" + (fit.trimmed ? ", Ende abgeschnitten" : "");
        return true;
    } catch (const std::exception& e) {
        status_ = std::string("Import fehlgeschlagen: ") + e.what();
        return false;
    }
}

void SoundDoc::set_loops(bool on) {
    if (current_ < 0 || sound_.loops == on) return;
    sound_.loops = on;
    dirty_ = true;
    revision_++;
}

void SoundDoc::trim(size_t from, size_t to) {
    if (current_ < 0) return;
    to = std::min(to, sound_.pcm.size());
    if (from >= to) return;
    sound_.pcm = std::vector<uint8_t>(sound_.pcm.begin() + long(from), sound_.pcm.begin() + long(to));
    dirty_ = true;
    revision_++;
}

}  // namespace em::editor
