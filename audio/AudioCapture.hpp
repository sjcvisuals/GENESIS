#pragma once

#include "audio/AudioAnalyser.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace genesis {

enum class AudioInputMode : std::uint8_t {
    None = 0,
    LiveCapture,
    WavFile,
};

// SDL3 mono capture / WAV playback that feeds AudioAnalyser.
// Analysis stays on the main thread (poll samples each frame) for cheap locking.
class AudioCapture {
public:
    struct DeviceInfo {
        std::uint32_t id = 0;
        std::string name;
        bool is_recording = true;
    };

    ~AudioCapture();

    AudioCapture() = default;
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    static std::vector<DeviceInfo> listRecordingDevices();

    bool startCapture(std::uint32_t device_id, std::string* out_error = nullptr);
    bool startDefaultCapture(std::string* out_error = nullptr);
    bool loadWavFile(const std::filesystem::path& path, bool play_through_speakers,
                     std::string* out_error = nullptr);

    void stop();
    bool isActive() const { return mode_ != AudioInputMode::None; }
    AudioInputMode mode() const { return mode_; }
    const std::filesystem::path& wavPath() const { return wav_path_; }
    float sampleRate() const { return sample_rate_; }
    std::uint32_t activeDeviceId() const { return active_device_id_; }
    const std::string& activeDeviceName() const { return active_device_name_; }

    // Pull new samples into the analyser. Call once per frame.
    void pump(AudioAnalyser& analyser, float dt_seconds);

private:
    bool ensureAudioSubsystem(std::string* out_error);
    void destroyStreams();

    AudioInputMode mode_ = AudioInputMode::None;
    SDL_AudioStream* capture_stream_ = nullptr;
    SDL_AudioStream* playback_stream_ = nullptr;
    float sample_rate_ = 44100.0f;
    std::uint32_t active_device_id_ = 0;
    std::string active_device_name_;

    std::filesystem::path wav_path_;
    std::vector<float> wav_samples_;
    std::size_t wav_cursor_ = 0;
    bool loop_wav_ = true;
};

} // namespace genesis
