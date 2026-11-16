// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Sounds of the goal scenes (KB\237..241.vga): Creative Voice files, 8 bit unsigned mono PCM (0x80 = silence), plus
// conversion from and to WAV for the editor. See docs/FORMATS.md.
//
// The game loads every sound into a buffer of the size of the original file, so a replaced sound must not be larger than
// that file. fit_voc() converts any WAV to 8 bit mono and chooses the highest sample rate (of those the Sound Blaster
// time constant allows) at which the whole sound still fits; only when even the lowest allowed rate does not fit, the
// end is cut off with a short fade.
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"

namespace em {

struct VocSound {
    int sample_rate = 0;       // 1000000 / (256 - time constant)
    std::vector<uint8_t> pcm;  // unsigned 8 bit, 0x80 = silence
    bool loops = false;        // endless repeat block around the sound (KB237, the crowd)
};

VocSound decode_voc(const Bytes& data);
// Header, optional repeat block (count 0xFFFF), one sound block, terminator. The sample rate is rounded to the
// nearest value the time constant can express.
Bytes encode_voc(const VocSound& sound);
// Size of encode_voc() for n samples
size_t voc_file_size(size_t samples, bool loops);
int voc_nearest_rate(int wanted);  // 1000000 / (256 - tc) closest to wanted, tc 1..255

// WAV: 8/16/24/32 bit integer or 32 bit float PCM, any channel count (mixed down to mono), as floats -1..1
struct WavSound {
    int sample_rate = 0;
    std::vector<float> mono;
};
WavSound decode_wav(const Bytes& data);
Bytes encode_wav8(const VocSound& sound);  // 8 bit mono WAV

struct VocFit {
    VocSound sound;
    int source_rate = 0;
    double seconds_in = 0, seconds_out = 0;
    size_t bytes = 0, capacity = 0;  // file size of the result and of the buffer
    bool trimmed = false;            // the end was cut off
    bool clipped = false;            // normalisation was off and samples exceeded full scale
    double gain = 1;                 // factor applied by the normalisation
};
struct VocFitOptions {
    int max_rate = 16000;   // never above this (and never above the source rate)
    int min_rate = 4000;    // below this the sound is trimmed instead
    bool normalize = true;  // scale the peak to full scale
    bool dither = false;    // +-1 step triangular noise before rounding to 8 bit
};
VocFit fit_voc(const WavSound& wav, size_t capacity, bool loops, const VocFitOptions& options = {});

}  // namespace em
