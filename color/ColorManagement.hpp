#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

// Display encoding for PIXERA / window output. Default matches live video Rec.709.
enum class DisplayColorSpace : std::uint8_t {
    Rec709 = 0, // ITU-R BT.709 OETF (PIXERA-friendly default)
    sRGB = 1,   // IEC 61966-2-1
    AcesOcio = 2, // OCIO Display/View (ACES studio config by default)
};

enum class TextureColorSpace : std::uint8_t {
    Auto = 0,   // Albedo/emissive → sRGB, data maps → linear
    sRGB = 1,
    Linear = 2,
};

struct ColorManagementSettings {
    DisplayColorSpace display = DisplayColorSpace::Rec709;
    std::string ocio_config_path; // empty → bundled ACES studio / ocio://studio-config-latest
    std::string ocio_input_space = "Linear Rec.709";
    std::string ocio_display = "Rec.709";
    std::string ocio_view = "ACES 2.0 - Rec.709";
    float exposure = 1.0f;
    bool use_filmic_pre_display = true; // soft highlight compression before OETF (non-OCIO modes)
    // Notch/Unreal-inspired finishing — all evaluated in-shader (no HDR post cost).
    float bloom_strength = 0.55f;    // dual-lobe specular glow (Physical Glow stand-in)
    float vignette_strength = 0.32f; // lens falloff
    float grain_strength = 0.028f;   // film grain (hides banding)
};

struct ColorLut3D {
    static constexpr int kSize = 32;
    std::vector<float> rgba; // size^3 * 4, linear display-referred RGB + A=1
    int size = kSize;
};

// Scene-linear working space uses Rec.709/sRGB primaries (real-time PBR standard).
// PIXERA receives display-referred Rec.709 LDR by default.
class ColorManagement {
public:
    ColorManagement();

    ColorManagementSettings& settings() { return settings_; }
    const ColorManagementSettings& settings() const { return settings_; }

    // Resolves bundled ACES studio config when path empty.
    std::filesystem::path resolvedConfigPath() const;

    bool loadConfig(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadDefaultAcesConfig(std::string* error = nullptr);
    bool hasOcio() const { return ocio_available_; }
    bool configLoaded() const { return config_loaded_; }

    const std::vector<std::string>& displays() const { return displays_; }
    const std::vector<std::string>& views() const { return views_; }
    const std::vector<std::string>& colorSpaces() const { return color_spaces_; }
    const std::string& configDescription() const { return config_description_; }

    // Rebuild display LUT after settings/config change. Always succeeds (analytic fallback).
    const ColorLut3D& rebuildDisplayLut();
    const ColorLut3D& displayLut() const { return lut_; }
    bool lutDirty() const { return lut_dirty_; }
    void markLutDirty() { lut_dirty_ = true; }

    // Apply display transform on CPU (editor tools / bake verification).
    std::array<float, 3> transformDisplay(float r, float g, float b) const;

    static float linearToRec709(float v);
    static float linearToSrgb(float v);
    static float srgbToLinear(float v);

private:
    void clearOcioLists();
    void bakeAnalyticRec709Lut();
    bool bakeOcioLut(std::string* error);

    ColorManagementSettings settings_{};
    ColorLut3D lut_{};
    bool lut_dirty_ = true;
    bool ocio_available_ = false;
    bool config_loaded_ = false;
    std::string config_description_;
    std::vector<std::string> displays_;
    std::vector<std::string> views_;
    std::vector<std::string> color_spaces_;
};

std::filesystem::path defaultAcesStudioConfigPath();

} // namespace genesis
