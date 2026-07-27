#include "audio/AudioAnalyser.hpp"
#include "audio/AudioFeatures.hpp"

#include <algorithm>
#include <cmath>

namespace genesis {

namespace {

void bitReversePermute(std::vector<float>& re, std::vector<float>& im) {
    const int n = static_cast<int>(re.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[static_cast<std::size_t>(i)], re[static_cast<std::size_t>(j)]);
            std::swap(im[static_cast<std::size_t>(i)], im[static_cast<std::size_t>(j)]);
        }
    }
}

void fftRadix2(std::vector<float>& re, std::vector<float>& im) {
    const int n = static_cast<int>(re.size());
    bitReversePermute(re, im);
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
        const float wlen_re = std::cos(ang);
        const float wlen_im = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (int j = 0; j < len / 2; ++j) {
                const int u = i + j;
                const int v = i + j + len / 2;
                const float t_re = w_re * re[static_cast<std::size_t>(v)] - w_im * im[static_cast<std::size_t>(v)];
                const float t_im = w_re * im[static_cast<std::size_t>(v)] + w_im * re[static_cast<std::size_t>(v)];
                re[static_cast<std::size_t>(v)] = re[static_cast<std::size_t>(u)] - t_re;
                im[static_cast<std::size_t>(v)] = im[static_cast<std::size_t>(u)] - t_im;
                re[static_cast<std::size_t>(u)] += t_re;
                im[static_cast<std::size_t>(u)] += t_im;
                const float next_w_re = w_re * wlen_re - w_im * wlen_im;
                w_im = w_re * wlen_im + w_im * wlen_re;
                w_re = next_w_re;
            }
        }
    }
}

float bandEnergy(const std::vector<float>& mag, int bin_lo, int bin_hi) {
    float sum = 0.0f;
    const int hi = std::min(bin_hi, static_cast<int>(mag.size()) - 1);
    const int lo = std::max(bin_lo, 1);
    if (hi < lo) {
        return 0.0f;
    }
    for (int i = lo; i <= hi; ++i) {
        sum += mag[static_cast<std::size_t>(i)];
    }
    return sum / static_cast<float>(hi - lo + 1);
}

float compress(float v) {
    return 1.0f - std::exp(-std::max(v, 0.0f) * 6.0f);
}

} // namespace

const char* audioFeatureChannelLabel(AudioFeatureChannel channel) {
    switch (channel) {
    case AudioFeatureChannel::Bass:
        return "Bass";
    case AudioFeatureChannel::Mid:
        return "Mid";
    case AudioFeatureChannel::Treble:
        return "Treble";
    case AudioFeatureChannel::Rms:
        return "RMS";
    case AudioFeatureChannel::Onset:
        return "Onset";
    case AudioFeatureChannel::Beat:
        return "Beat";
    default:
        return "Unknown";
    }
}

float audioFeatureValue(const AudioFeatures& features, AudioFeatureChannel channel) {
    switch (channel) {
    case AudioFeatureChannel::Bass:
        return features.bass;
    case AudioFeatureChannel::Mid:
        return features.mid;
    case AudioFeatureChannel::Treble:
        return features.treble;
    case AudioFeatureChannel::Rms:
        return features.rms;
    case AudioFeatureChannel::Onset:
        return features.onset;
    case AudioFeatureChannel::Beat:
        return features.beat;
    default:
        return 0.0f;
    }
}

void AudioAnalyser::ensureBuffers() {
    if (!ring_.empty()) {
        return;
    }
    ring_.assign(kRingCapacity, 0.0f);
    window_buf_.assign(static_cast<std::size_t>(kFftSize), 0.0f);
    fft_re_.assign(static_cast<std::size_t>(kFftSize), 0.0f);
    fft_im_.assign(static_cast<std::size_t>(kFftSize), 0.0f);
    hann_.resize(static_cast<std::size_t>(kFftSize));
    for (int i = 0; i < kFftSize; ++i) {
        hann_[static_cast<std::size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f * static_cast<float>(i) /
                                    static_cast<float>(kFftSize - 1)));
    }
    prev_mag_.assign(static_cast<std::size_t>(kFftSize / 2 + 1), 0.0f);
}

void AudioAnalyser::clear() {
    ensureBuffers();
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    ring_write_ = 0;
    ring_filled_ = 0;
    features_ = {};
    std::fill(prev_mag_.begin(), prev_mag_.end(), 0.0f);
    prev_bass_raw_ = 0.0f;
}

void AudioAnalyser::pushMono(const float* samples, std::size_t count) {
    if (!samples || count == 0) {
        return;
    }
    ensureBuffers();
    const float gain = settings_.input_gain;
    for (std::size_t i = 0; i < count; ++i) {
        ring_[ring_write_] = samples[i] * gain;
        ring_write_ = (ring_write_ + 1) % kRingCapacity;
        if (ring_filled_ < kRingCapacity) {
            ++ring_filled_;
        }
    }
}

void AudioAnalyser::smooth(float& state, float target, float dt, float attack, float release) const {
    const float tau = target > state ? std::max(attack, 1.0e-4f) : std::max(release, 1.0e-4f);
    const float alpha = 1.0f - std::exp(-dt / tau);
    state += (target - state) * alpha;
}

bool AudioAnalyser::tick(float dt_seconds) {
    ensureBuffers();
    const float dt = std::max(dt_seconds, 1.0e-4f);

    float rms_t = 0.0f;
    float bass_t = 0.0f;
    float mid_t = 0.0f;
    float treble_t = 0.0f;
    float onset_t = 0.0f;
    bool analysed = false;

    if (ring_filled_ >= static_cast<std::size_t>(kFftSize)) {
        std::size_t idx =
            (ring_write_ + kRingCapacity - static_cast<std::size_t>(kFftSize)) % kRingCapacity;
        for (int i = 0; i < kFftSize; ++i) {
            window_buf_[static_cast<std::size_t>(i)] = ring_[idx];
            idx = (idx + 1) % kRingCapacity;
        }

        float rms_acc = 0.0f;
        for (int i = 0; i < kFftSize; ++i) {
            const float s = window_buf_[static_cast<std::size_t>(i)] * hann_[static_cast<std::size_t>(i)];
            fft_re_[static_cast<std::size_t>(i)] = s;
            fft_im_[static_cast<std::size_t>(i)] = 0.0f;
            rms_acc += s * s;
        }
        const float rms_raw = std::sqrt(rms_acc / static_cast<float>(kFftSize));
        fftRadix2(fft_re_, fft_im_);

        const int bins = kFftSize / 2 + 1;
        std::vector<float> mag(static_cast<std::size_t>(bins), 0.0f);
        float flux = 0.0f;
        for (int i = 0; i < bins; ++i) {
            const float re = fft_re_[static_cast<std::size_t>(i)];
            const float im = fft_im_[static_cast<std::size_t>(i)];
            const float m = std::sqrt(re * re + im * im) * (2.0f / static_cast<float>(kFftSize));
            mag[static_cast<std::size_t>(i)] = m;
            const float diff = m - prev_mag_[static_cast<std::size_t>(i)];
            if (diff > 0.0f) {
                flux += diff;
            }
            prev_mag_[static_cast<std::size_t>(i)] = m;
        }

        const float hz_per_bin = settings_.sample_rate / static_cast<float>(kFftSize);
        auto hzToBin = [&](float hz) -> int {
            return std::clamp(static_cast<int>(hz / hz_per_bin + 0.5f), 1, bins - 1);
        };

        const float bass_raw =
            bandEnergy(mag, hzToBin(20.0f), hzToBin(150.0f)) * settings_.bass_gain;
        const float mid_raw =
            bandEnergy(mag, hzToBin(150.0f), hzToBin(2000.0f)) * settings_.mid_gain;
        const float treble_raw =
            bandEnergy(mag, hzToBin(2000.0f), hzToBin(12000.0f)) * settings_.treble_gain;

        bass_t = compress(bass_raw);
        mid_t = compress(mid_raw);
        treble_t = compress(treble_raw);
        rms_t = compress(rms_raw * 2.5f);

        const float bass_jump = std::max(0.0f, bass_t - prev_bass_raw_);
        prev_bass_raw_ = bass_t;
        onset_t = compress((flux * 0.35f + bass_jump * 2.5f) * settings_.onset_sensitivity);

        for (int b = 0; b < AudioFeatures::kSpectrumBars; ++b) {
            const float t0 = static_cast<float>(b) / static_cast<float>(AudioFeatures::kSpectrumBars);
            const float t1 = static_cast<float>(b + 1) / static_cast<float>(AudioFeatures::kSpectrumBars);
            const float f0 = 40.0f * std::pow(22050.0f / 40.0f, t0);
            const float f1 = 40.0f * std::pow(22050.0f / 40.0f, t1);
            features_.spectrum[static_cast<std::size_t>(b)] =
                compress(bandEnergy(mag, hzToBin(f0), hzToBin(f1)));
        }
        analysed = true;
    }

    smooth(features_.rms, rms_t, dt, settings_.attack_seconds, settings_.release_seconds);
    smooth(features_.bass, bass_t, dt, settings_.attack_seconds, settings_.release_seconds);
    smooth(features_.mid, mid_t, dt, settings_.attack_seconds, settings_.release_seconds);
    smooth(features_.treble, treble_t, dt, settings_.attack_seconds, settings_.release_seconds);
    smooth(features_.onset, onset_t, dt, 0.005f, settings_.release_seconds);
    smooth(features_.beat, bass_t, dt, settings_.attack_seconds, settings_.beat_release_seconds);
    return analysed;
}

} // namespace genesis
