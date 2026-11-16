// SPDX-License-Identifier: GPL-3.0-only
#include "formats/voc.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace em {

namespace {
constexpr char kSig[] = "Creative Voice File\x1A";
constexpr size_t kHeader = 0x1A;

void put16(Bytes& b, unsigned v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(Bytes& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
int rate_of(int tc) { return 1000000 / (256 - tc); }
int nearest_tc(int wanted) {
    int best = 1, err = 1 << 30;
    for (int tc = 1; tc <= 255; tc++) {
        const int e = std::abs(rate_of(tc) - wanted);
        if (e < err) {
            err = e;
            best = tc;
        }
    }
    return best;
}
}  // namespace

VocSound decode_voc(const Bytes& d) {
    if (d.size() < kHeader || std::memcmp(d.data(), kSig, 20) != 0) throw FormatError("VOC: missing signature");
    const uint16_t version = le16(d, 0x16), check = le16(d, 0x18);
    if (uint16_t(~version + 0x1234) != check) throw FormatError("VOC: bad version check word");
    VocSound v;
    auto set_rate = [&](int rate) {
        if (rate <= 0) throw FormatError("VOC: bad sample rate");
        if (v.sample_rate && v.sample_rate != rate) throw FormatError("VOC: sample rate changes inside the file");
        v.sample_rate = rate;
    };
    size_t pos = le16(d, 0x14);
    size_t repeat_start = 0;
    int repeat_count = -1;
    while (pos < d.size()) {
        const uint8_t type = d[pos];
        if (type == 0) break;
        if (pos + 4 > d.size()) throw FormatError("VOC: truncated block header");
        const size_t len = size_t(d[pos + 1]) | size_t(d[pos + 2]) << 8 | size_t(d[pos + 3]) << 16;
        const size_t body = pos + 4;
        if (body + len > d.size()) throw FormatError("VOC: block runs past the end");
        switch (type) {
            case 1:
                if (len < 2) throw FormatError("VOC: short sound block");
                if (d[body + 1] != 0) throw FormatError("VOC: only 8-bit PCM is supported");
                set_rate(rate_of(d[body]));
                v.pcm.insert(v.pcm.end(), d.begin() + long(body + 2), d.begin() + long(body + len));
                break;
            case 2: v.pcm.insert(v.pcm.end(), d.begin() + long(body), d.begin() + long(body + len)); break;
            case 3:
                if (len < 3) throw FormatError("VOC: short silence block");
                set_rate(rate_of(d[body + 2]));
                v.pcm.insert(v.pcm.end(), size_t(le16(d, body)) + 1, uint8_t(0x80));
                break;
            case 6:
                if (len < 2) throw FormatError("VOC: short repeat block");
                repeat_start = v.pcm.size();
                repeat_count = le16(d, body);
                break;
            case 7:
                if (repeat_count < 0) throw FormatError("VOC: repeat end without start");
                if (repeat_count == 0xFFFF) {
                    v.loops = true;
                } else {
                    const std::vector<uint8_t> once(v.pcm.begin() + long(repeat_start), v.pcm.end());
                    for (int n = 0; n < repeat_count; n++) v.pcm.insert(v.pcm.end(), once.begin(), once.end());
                }
                repeat_count = -1;
                break;
            case 4:
            case 5: break;
            default: throw FormatError("VOC: unsupported block type " + std::to_string(type));
        }
        pos = body + len;
    }
    if (!v.sample_rate) throw FormatError("VOC: no sound data");
    return v;
}

int voc_nearest_rate(int wanted) { return rate_of(nearest_tc(wanted)); }

size_t voc_file_size(size_t samples, bool loops) { return kHeader + (loops ? 6 + 4 : 0) + 4 + 2 + samples + 1; }

Bytes encode_voc(const VocSound& s) {
    if (s.pcm.empty() || s.pcm.size() + 2 > 0xFFFFFF) throw FormatError("VOC: no sound or too long");
    Bytes out(kSig, kSig + 20);
    put16(out, kHeader);
    put16(out, 0x010A);
    put16(out, uint16_t(~0x010A + 0x1234));
    if (s.loops) {
        out.push_back(6);
        out.push_back(2);
        out.push_back(0);
        out.push_back(0);
        put16(out, 0xFFFF);
    }
    out.push_back(1);
    const size_t len = s.pcm.size() + 2;
    out.push_back(uint8_t(len));
    out.push_back(uint8_t(len >> 8));
    out.push_back(uint8_t(len >> 16));
    out.push_back(uint8_t(nearest_tc(s.sample_rate)));
    out.push_back(0);
    out.insert(out.end(), s.pcm.begin(), s.pcm.end());
    if (s.loops) {
        out.push_back(7);
        out.push_back(0);
        out.push_back(0);
        out.push_back(0);
    }
    out.push_back(0);
    return out;
}

// ------------------------------------------------------------------------------------------------------------ WAV

WavSound decode_wav(const Bytes& d) {
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        throw FormatError("WAV: not a RIFF/WAVE file");
    int format = 0, channels = 0, bits = 0, rate = 0;
    size_t data = 0, data_len = 0;
    for (size_t p = 12; p + 8 <= d.size();) {
        const uint32_t len = le32(d, p + 4);
        const size_t body = p + 8;
        if (std::memcmp(d.data() + p, "fmt ", 4) == 0 && len >= 16 && body + 16 <= d.size()) {
            format = le16(d, body);
            channels = le16(d, body + 2);
            rate = int(le32(d, body + 4));
            bits = le16(d, body + 14);
            if (format == 0xFFFE && len >= 26 && body + 26 <= d.size()) format = le16(d, body + 24);  // extensible: sub format
        } else if (std::memcmp(d.data() + p, "data", 4) == 0) {
            data = body;
            data_len = std::min<size_t>(len, d.size() - body);
            break;
        }
        p = body + len + (len & 1);
    }
    if (!data || channels < 1 || rate <= 0) throw FormatError("WAV: no format or data chunk");
    const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32), flt = format == 3 && bits == 32;
    if (!pcm && !flt) throw FormatError("WAV: only PCM (8, 16, 24, 32 bit) and 32 bit float are supported");
    const size_t bytes = size_t(bits / 8), frame = bytes * size_t(channels), frames = data_len / frame;
    WavSound w;
    w.sample_rate = rate;
    w.mono.resize(frames);
    for (size_t i = 0; i < frames; i++) {
        double sum = 0;
        for (int c = 0; c < channels; c++) {
            const uint8_t* q = d.data() + data + i * frame + size_t(c) * bytes;
            double v;
            if (flt) {
                float f;
                std::memcpy(&f, q, 4);
                v = f;
            } else if (bits == 8) {
                v = (q[0] - 128) / 128.0;
            } else if (bits == 16) {
                v = int16_t(q[0] | q[1] << 8) / 32768.0;
            } else if (bits == 24) {
                v = (int32_t(uint32_t(q[0]) << 8 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 24) >> 8) / 8388608.0;
            } else {
                v = int32_t(uint32_t(q[0]) | uint32_t(q[1]) << 8 | uint32_t(q[2]) << 16 | uint32_t(q[3]) << 24) / 2147483648.0;
            }
            sum += v;
        }
        w.mono[i] = float(sum / channels);
    }
    return w;
}

Bytes encode_wav8(const VocSound& s) {
    Bytes out{'R', 'I', 'F', 'F'};
    put32(out, uint32_t(36 + s.pcm.size()));
    for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}) out.push_back(uint8_t(c));
    put32(out, 16);
    put16(out, 1);
    put16(out, 1);
    put32(out, uint32_t(s.sample_rate));
    put32(out, uint32_t(s.sample_rate));
    put16(out, 1);
    put16(out, 8);
    for (char c : {'d', 'a', 't', 'a'}) out.push_back(uint8_t(c));
    put32(out, uint32_t(s.pcm.size()));
    out.insert(out.end(), s.pcm.begin(), s.pcm.end());
    return out;
}

// ------------------------------------------------------------------------------------------------------------ fit

namespace {
// Rate conversion: box average when going down (nothing above the new Nyquist folds back), linear interpolation otherwise
std::vector<float> resample(const std::vector<float>& in, int from, int to) {
    const size_t n = size_t(std::ceil(double(in.size()) * to / from));
    std::vector<float> out(n);
    const double step = double(from) / to;
    for (size_t i = 0; i < n; i++) {
        const double t0 = double(i) * step;
        if (step > 1.0) {
            const double t1 = std::min(double(in.size()), t0 + step);
            double sum = 0, w = 0;
            for (size_t k = size_t(t0); k < in.size() && double(k) < t1; k++) {
                const double a = std::max(double(k), t0), b = std::min(double(k) + 1, t1);
                sum += in[k] * (b - a);
                w += b - a;
            }
            out[i] = w > 0 ? float(sum / w) : 0.f;
        } else {
            const size_t k = std::min(in.size() - 1, size_t(t0));
            const double f = t0 - double(k);
            const float a = in[k], b = k + 1 < in.size() ? in[k + 1] : in[k];
            out[i] = float(a + (b - a) * f);
        }
    }
    return out;
}
}  // namespace

VocFit fit_voc(const WavSound& wav, size_t capacity, bool loops, const VocFitOptions& opt) {
    if (wav.mono.empty() || wav.sample_rate <= 0) throw FormatError("WAV: no sound data");
    VocFit r;
    r.source_rate = wav.sample_rate;
    r.capacity = capacity;
    r.seconds_in = double(wav.mono.size()) / wav.sample_rate;
    // the highest rate the time constant allows (not above the source and the limit) at which the whole sound fits
    int rate = 0;
    for (int tc = 255; tc >= 1 && !rate; tc--) {
        const int candidate = rate_of(tc);
        if (candidate > opt.max_rate || candidate > wav.sample_rate + wav.sample_rate / 20) continue;
        if (voc_file_size(size_t(std::ceil(r.seconds_in * candidate)), loops) <= capacity) rate = candidate;
    }
    if (rate < opt.min_rate) rate = voc_nearest_rate(opt.min_rate);  // too coarse: lowest allowed rate, the end is cut
    std::vector<float> x = wav.mono;
    if (rate != wav.sample_rate) x = resample(x, wav.sample_rate, rate);
    const size_t overhead = voc_file_size(0, loops), limit = capacity > overhead ? capacity - overhead : 0;
    if (x.size() > limit) {
        r.trimmed = true;
        x.resize(limit);
        const size_t fade = std::min(x.size(), size_t(rate / 200 + 1));  // 5 ms
        for (size_t i = 0; i < fade; i++) x[x.size() - 1 - i] *= float(i) / float(fade);
    }
    if (x.empty()) throw FormatError("sound is empty after fitting");
    float peak = 0;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    r.gain = opt.normalize && peak > 0 ? 1.0 / peak : 1.0;
    uint32_t seed = 12345;
    auto noise = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1 << 24) - 0.5f;
    };
    r.sound.sample_rate = rate;
    r.sound.loops = loops;
    r.sound.pcm.resize(x.size());
    for (size_t i = 0; i < x.size(); i++) {
        float v = x[i] * float(r.gain) * 127.f;
        if (opt.dither) v += noise() + noise();
        const float q = std::round(v);
        if (std::fabs(q) > 127.f) r.clipped = true;
        r.sound.pcm[i] = uint8_t(std::clamp(q, -127.f, 127.f) + 128.f);
    }
    r.seconds_out = double(r.sound.pcm.size()) / rate;
    r.bytes = voc_file_size(r.sound.pcm.size(), loops);
    return r;
}

}  // namespace em
