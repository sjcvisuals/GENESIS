#include "color/ColorManagement.hpp"

#include "platform/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

#if defined(GENESIS_WITH_OCIO)
#include <OpenColorIO/OpenColorIO.h>
namespace ocio = OCIO_NAMESPACE;
#endif

namespace genesis {

namespace {

float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

void writeLutTexel(ColorLut3D& lut, int x, int y, int z, float r, float g, float b) {
    const int size = lut.size;
    const std::size_t idx =
        (static_cast<std::size_t>(z) * size * size + static_cast<std::size_t>(y) * size +
         static_cast<std::size_t>(x)) *
        4;
    lut.rgba[idx + 0] = r;
    lut.rgba[idx + 1] = g;
    lut.rgba[idx + 2] = b;
    lut.rgba[idx + 3] = 1.0f;
}

} // namespace

float ColorManagement::linearToRec709(float v) {
    v = clampf(v, 0.0f, 1.0f);
    // ITU-R BT.709 transfer function.
    if (v < 0.018f) {
        return 4.5f * v;
    }
    return 1.099f * std::pow(v, 0.45f) - 0.099f;
}

float ColorManagement::linearToSrgb(float v) {
    v = clampf(v, 0.0f, 1.0f);
    if (v <= 0.0031308f) {
        return 12.92f * v;
    }
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

float ColorManagement::srgbToLinear(float v) {
    v = clampf(v, 0.0f, 1.0f);
    if (v <= 0.04045f) {
        return v / 12.92f;
    }
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

std::filesystem::path defaultAcesStudioConfigPath() {
    try {
        return resolveAssetPath(std::filesystem::path("ocio") /
                                "studio-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio");
    } catch (const std::exception&) {
        return std::filesystem::path("assets/ocio/studio-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio");
    }
}

ColorManagement::ColorManagement() {
#if defined(GENESIS_WITH_OCIO)
    ocio_available_ = true;
#else
    ocio_available_ = false;
#endif
    settings_.ocio_config_path = defaultAcesStudioConfigPath().string();
    bakeAnalyticRec709Lut();
    lut_dirty_ = false;

    std::string error;
    if (!loadDefaultAcesConfig(&error)) {
        spdlog::warn("GENESIS color: ACES OCIO config not active ({}) — using analytic Rec.709", error);
    }
}

std::filesystem::path ColorManagement::resolvedConfigPath() const {
    if (!settings_.ocio_config_path.empty()) {
        return std::filesystem::path(settings_.ocio_config_path);
    }
    return defaultAcesStudioConfigPath();
}

void ColorManagement::clearOcioLists() {
    displays_.clear();
    views_.clear();
    color_spaces_.clear();
    config_description_.clear();
    config_loaded_ = false;
}

bool ColorManagement::loadDefaultAcesConfig(std::string* error) {
#if defined(GENESIS_WITH_OCIO)
    // Prefer built-in latest studio config when OCIO 2.5+ is linked; fall back to shipped file.
    try {
        ocio::ConstConfigRcPtr config = ocio::Config::CreateFromFile("ocio://studio-config-latest");
        (void)config;
        settings_.ocio_config_path = "ocio://studio-config-latest";
        return loadConfig(std::filesystem::path(settings_.ocio_config_path), error);
    } catch (...) {
        return loadConfig(defaultAcesStudioConfigPath(), error);
    }
#else
    const std::filesystem::path path = settings_.ocio_config_path.empty()
                                           ? defaultAcesStudioConfigPath()
                                           : std::filesystem::path(settings_.ocio_config_path);
    if (!std::filesystem::exists(path)) {
        if (error) {
            *error = "Bundled ACES studio config missing: " + path.string();
        }
        return false;
    }
    settings_.ocio_config_path = path.string();
    config_description_ = "ACES 2.0 Studio config (file present; rebuild with GENESIS_WITH_OCIO for transforms)";
    config_loaded_ = true;
    displays_ = {"Rec.709", "sRGB"};
    views_ = {"ACES 2.0 - Rec.709", "Un-tone-mapped"};
    color_spaces_ = {"Linear Rec.709", "ACEScg", "sRGB - Texture"};
    markLutDirty();
    return true;
#endif
}

bool ColorManagement::loadConfig(const std::filesystem::path& path, std::string* error) {
    clearOcioLists();
    settings_.ocio_config_path = path.string();

#if defined(GENESIS_WITH_OCIO)
    try {
        ocio::ConstConfigRcPtr config = ocio::Config::CreateFromFile(path.string().c_str());
        config_description_ = config->getDescription() ? config->getDescription() : path.string();

        for (int i = 0; i < config->getNumDisplays(); ++i) {
            displays_.emplace_back(config->getDisplay(i));
        }
        if (displays_.empty()) {
            displays_.emplace_back(config->getDefaultDisplay());
        }

        const char* display = settings_.ocio_display.c_str();
        if (std::find(displays_.begin(), displays_.end(), settings_.ocio_display) == displays_.end()) {
            settings_.ocio_display = config->getDefaultDisplay();
            display = settings_.ocio_display.c_str();
        }

        for (int i = 0; i < config->getNumViews(display); ++i) {
            views_.emplace_back(config->getView(display, i));
        }
        if (views_.empty()) {
            views_.emplace_back(config->getDefaultView(display));
        }
        if (std::find(views_.begin(), views_.end(), settings_.ocio_view) == views_.end()) {
            settings_.ocio_view = config->getDefaultView(display);
        }

        for (int i = 0; i < config->getNumColorSpaces(); ++i) {
            color_spaces_.emplace_back(config->getColorSpaceNameByIndex(i));
        }
        if (std::find(color_spaces_.begin(), color_spaces_.end(), settings_.ocio_input_space) ==
            color_spaces_.end()) {
            if (const char* role = config->getRole("scene_linear")) {
                settings_.ocio_input_space = role;
            } else if (!color_spaces_.empty()) {
                settings_.ocio_input_space = color_spaces_.front();
            }
        }

        config_loaded_ = true;
        markLutDirty();
        spdlog::info("GENESIS color: loaded OCIO config '{}'", path.string());
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
#else
    if (path.string().rfind("ocio://", 0) == 0) {
        if (error) {
            *error = "OpenColorIO not linked; cannot resolve URI configs";
        }
        return false;
    }
    if (!std::filesystem::exists(path)) {
        if (error) {
            *error = "OCIO config not found: " + path.string();
        }
        return false;
    }
    config_description_ = path.filename().string() + " (OCIO runtime not linked)";
    config_loaded_ = true;
    displays_ = {"Rec.709"};
    views_ = {"ACES 2.0 - Rec.709"};
    color_spaces_ = {"Linear Rec.709"};
    markLutDirty();
    if (error) {
        *error = "Config file recorded; rebuild with OpenColorIO for full transforms";
    }
    return true;
#endif
}

void ColorManagement::bakeAnalyticRec709Lut() {
    lut_.size = ColorLut3D::kSize;
    lut_.rgba.assign(static_cast<std::size_t>(lut_.size) * lut_.size * lut_.size * 4, 0.0f);

    // Analytic modes encode in-shader; LUT is identity for GPU path consistency.
    for (int z = 0; z < lut_.size; ++z) {
        for (int y = 0; y < lut_.size; ++y) {
            for (int x = 0; x < lut_.size; ++x) {
                const float r = static_cast<float>(x) / static_cast<float>(lut_.size - 1);
                const float g = static_cast<float>(y) / static_cast<float>(lut_.size - 1);
                const float b = static_cast<float>(z) / static_cast<float>(lut_.size - 1);
                writeLutTexel(lut_, x, y, z, r, g, b);
            }
        }
    }
}

bool ColorManagement::bakeOcioLut(std::string* error) {
#if defined(GENESIS_WITH_OCIO)
    try {
        ocio::ConstConfigRcPtr config =
            ocio::Config::CreateFromFile(resolvedConfigPath().string().c_str());
        ocio::DisplayViewTransformRcPtr transform = ocio::DisplayViewTransform::Create();
        transform->setSrc(settings_.ocio_input_space.c_str());
        transform->setDisplay(settings_.ocio_display.c_str());
        transform->setView(settings_.ocio_view.c_str());

        ocio::ConstProcessorRcPtr processor = config->getProcessor(transform);
        ocio::ConstCPUProcessorRcPtr cpu = processor->getDefaultCPUProcessor();

        lut_.size = ColorLut3D::kSize;
        lut_.rgba.assign(static_cast<std::size_t>(lut_.size) * lut_.size * lut_.size * 4, 0.0f);

        const float exposure = 1.0f; // Exposure applied in shader before LUT sample.
        for (int z = 0; z < lut_.size; ++z) {
            for (int y = 0; y < lut_.size; ++y) {
                for (int x = 0; x < lut_.size; ++x) {
                    float pixel[3] = {
                        exposure * static_cast<float>(x) / static_cast<float>(lut_.size - 1),
                        exposure * static_cast<float>(y) / static_cast<float>(lut_.size - 1),
                        exposure * static_cast<float>(z) / static_cast<float>(lut_.size - 1),
                    };
                    cpu->applyRGB(pixel);
                    writeLutTexel(lut_, x, y, z, clampf(pixel[0], 0.0f, 1.0f),
                                  clampf(pixel[1], 0.0f, 1.0f), clampf(pixel[2], 0.0f, 1.0f));
                }
            }
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
#else
    if (error) {
        *error = "OpenColorIO not linked";
    }
    return false;
#endif
}

const ColorLut3D& ColorManagement::rebuildDisplayLut() {
    std::string error;
    if (settings_.display == DisplayColorSpace::AcesOcio) {
        if (!bakeOcioLut(&error)) {
            spdlog::warn("GENESIS color: OCIO LUT bake failed ({}) — falling back to Rec.709", error);
            bakeAnalyticRec709Lut();
        }
    } else {
        bakeAnalyticRec709Lut();
    }
    lut_dirty_ = false;
    return lut_;
}

std::array<float, 3> ColorManagement::transformDisplay(float r, float g, float b) const {
    const ColorLut3D& lut = lut_;
    const float max_i = static_cast<float>(lut.size - 1);
    const float fx = clampf(r, 0.0f, 1.0f) * max_i;
    const float fy = clampf(g, 0.0f, 1.0f) * max_i;
    const float fz = clampf(b, 0.0f, 1.0f) * max_i;
    const int x = static_cast<int>(fx + 0.5f);
    const int y = static_cast<int>(fy + 0.5f);
    const int z = static_cast<int>(fz + 0.5f);
    const std::size_t idx =
        (static_cast<std::size_t>(z) * lut.size * lut.size + static_cast<std::size_t>(y) * lut.size +
         static_cast<std::size_t>(x)) *
        4;
    return {lut.rgba[idx], lut.rgba[idx + 1], lut.rgba[idx + 2]};
}

} // namespace genesis
