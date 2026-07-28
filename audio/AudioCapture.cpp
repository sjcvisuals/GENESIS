#include "audio/AudioCapture.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace genesis {

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::ensureAudioSubsystem(std::string* out_error) {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const std::string msg = std::string("SDL_InitSubSystem(AUDIO) failed: ") + SDL_GetError();
            if (out_error) {
                *out_error = msg;
            }
            return false;
        }
    }
    return true;
}

void AudioCapture::destroyStreams() {
    if (capture_stream_) {
        SDL_DestroyAudioStream(capture_stream_);
        capture_stream_ = nullptr;
    }
    if (playback_stream_) {
        SDL_DestroyAudioStream(playback_stream_);
        playback_stream_ = nullptr;
    }
}

void AudioCapture::stop() {
    destroyStreams();
    mode_ = AudioInputMode::None;
    active_device_id_ = 0;
    active_device_name_.clear();
    wav_samples_.clear();
    wav_cursor_ = 0;
    wav_path_.clear();
}

std::vector<AudioCapture::DeviceInfo> AudioCapture::listRecordingDevices() {
    std::vector<DeviceInfo> out;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return out;
    }

    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
    if (!ids) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        DeviceInfo info{};
        info.id = ids[i];
        info.is_recording = true;
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        info.name = name ? name : "Recording device";
        out.push_back(std::move(info));
    }
    SDL_free(ids);
    return out;
}

bool AudioCapture::startDefaultCapture(std::string* out_error) {
    return startCapture(static_cast<std::uint32_t>(SDL_AUDIO_DEVICE_DEFAULT_RECORDING), out_error);
}

bool AudioCapture::startCapture(std::uint32_t device_id, std::string* out_error) {
    if (!ensureAudioSubsystem(out_error)) {
        return false;
    }
    stop();

    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;

    capture_stream_ = SDL_OpenAudioDeviceStream(static_cast<SDL_AudioDeviceID>(device_id), &spec,
                                                nullptr, nullptr);
    if (!capture_stream_) {
        const std::string msg = std::string("Open recording device failed: ") + SDL_GetError();
        if (out_error) {
            *out_error = msg;
        }
        return false;
    }

    sample_rate_ = static_cast<float>(spec.freq);
    if (!SDL_ResumeAudioStreamDevice(capture_stream_)) {
        const std::string msg = std::string("Resume recording failed: ") + SDL_GetError();
        destroyStreams();
        if (out_error) {
            *out_error = msg;
        }
        return false;
    }

    active_device_id_ = device_id;
    if (device_id == static_cast<std::uint32_t>(SDL_AUDIO_DEVICE_DEFAULT_RECORDING)) {
        active_device_name_ = "System default";
    } else if (const char* name = SDL_GetAudioDeviceName(static_cast<SDL_AudioDeviceID>(device_id))) {
        active_device_name_ = name;
    } else {
        active_device_name_ = "Recording device";
    }

    mode_ = AudioInputMode::LiveCapture;
    spdlog::info("Audio capture started on '{}' ({} Hz mono)", active_device_name_, sample_rate_);
    return true;
}

bool AudioCapture::loadWavFile(const std::filesystem::path& path, bool play_through_speakers,
                               std::string* out_error) {
    if (!ensureAudioSubsystem(out_error)) {
        return false;
    }
    stop();

    SDL_AudioSpec file_spec{};
    Uint8* raw = nullptr;
    Uint32 raw_len = 0;
    if (!SDL_LoadWAV(path.string().c_str(), &file_spec, &raw, &raw_len)) {
        const std::string msg = std::string("SDL_LoadWAV failed: ") + SDL_GetError();
        if (out_error) {
            *out_error = msg;
        }
        return false;
    }

    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = SDL_AUDIO_F32;
    want.channels = 1;

    SDL_AudioStream* convert = SDL_CreateAudioStream(&file_spec, &want);
    if (!convert) {
        SDL_free(raw);
        const std::string msg = std::string("CreateAudioStream failed: ") + SDL_GetError();
        if (out_error) {
            *out_error = msg;
        }
        return false;
    }

    if (!SDL_PutAudioStreamData(convert, raw, static_cast<int>(raw_len)) ||
        !SDL_FlushAudioStream(convert)) {
        SDL_DestroyAudioStream(convert);
        SDL_free(raw);
        const std::string msg = std::string("WAV convert failed: ") + SDL_GetError();
        if (out_error) {
            *out_error = msg;
        }
        return false;
    }
    SDL_free(raw);

    const int avail = SDL_GetAudioStreamAvailable(convert);
    if (avail <= 0) {
        SDL_DestroyAudioStream(convert);
        if (out_error) {
            *out_error = "WAV convert produced no samples";
        }
        return false;
    }

    wav_samples_.assign(static_cast<std::size_t>(avail) / sizeof(float), 0.0f);
    const int got = SDL_GetAudioStreamData(convert, wav_samples_.data(), avail);
    SDL_DestroyAudioStream(convert);
    if (got <= 0) {
        wav_samples_.clear();
        if (out_error) {
            *out_error = "Failed to read converted WAV samples";
        }
        return false;
    }
    wav_samples_.resize(static_cast<std::size_t>(got) / sizeof(float));
    wav_cursor_ = 0;
    wav_path_ = path;
    sample_rate_ = 44100.0f;

    if (play_through_speakers) {
        playback_stream_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, nullptr, nullptr);
        if (!playback_stream_) {
            spdlog::warn("WAV loaded but playback open failed: {}", SDL_GetError());
        } else if (!SDL_ResumeAudioStreamDevice(playback_stream_)) {
            spdlog::warn("WAV playback resume failed: {}", SDL_GetError());
            SDL_DestroyAudioStream(playback_stream_);
            playback_stream_ = nullptr;
        }
    }

    mode_ = AudioInputMode::WavFile;
    spdlog::info("Audio WAV loaded ({} samples, {} Hz mono): {}", wav_samples_.size(), sample_rate_,
                 path.string());
    return true;
}

void AudioCapture::pump(AudioAnalyser& analyser, float dt_seconds) {
    if (mode_ == AudioInputMode::None) {
        return;
    }

    AudioAnalyser::Settings as = analyser.settings();
    as.sample_rate = sample_rate_;
    analyser.setSettings(as);

    if (mode_ == AudioInputMode::LiveCapture && capture_stream_) {
        constexpr int kMaxBytes = 4096 * static_cast<int>(sizeof(float));
        alignas(16) float scratch[4096];
        int remaining = kMaxBytes;
        while (remaining > 0) {
            const int got = SDL_GetAudioStreamData(capture_stream_, scratch,
                                                   std::min(remaining, static_cast<int>(sizeof(scratch))));
            if (got <= 0) {
                break;
            }
            analyser.pushMono(scratch, static_cast<std::size_t>(got) / sizeof(float));
            remaining -= got;
        }
        return;
    }

    if (mode_ == AudioInputMode::WavFile && !wav_samples_.empty()) {
        const std::size_t want =
            static_cast<std::size_t>(std::max(1.0f, dt_seconds * sample_rate_));
        std::vector<float> chunk(want, 0.0f);
        for (std::size_t i = 0; i < want; ++i) {
            if (wav_cursor_ >= wav_samples_.size()) {
                if (loop_wav_) {
                    wav_cursor_ = 0;
                } else {
                    break;
                }
            }
            chunk[i] = wav_samples_[wav_cursor_++];
        }
        analyser.pushMono(chunk.data(), chunk.size());
        if (playback_stream_) {
            SDL_PutAudioStreamData(playback_stream_, chunk.data(),
                                   static_cast<int>(chunk.size() * sizeof(float)));
        }
    }
}

} // namespace genesis
