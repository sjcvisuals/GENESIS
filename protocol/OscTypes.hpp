#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

enum class OscArgType : std::uint8_t {
    Int32,
    Float32,
    String,
    Bool,
    Unknown,
};

struct OscArg {
    OscArgType type = OscArgType::Unknown;
    std::int32_t i32 = 0;
    float f32 = 0.0f;
    bool boolean = false;
    std::string string;
};

struct OscMessage {
    std::string address;
    std::vector<OscArg> args;
};

enum class OscParamTarget : std::uint8_t {
    NodePositionX,
    NodePositionY,
    NodePositionZ,
    NodeScaleX,
    NodeScaleY,
    NodeScaleZ,
    NodeRotationX,
    NodeRotationY,
    NodeRotationZ,
    NodeVisible,
    MaterialBaseColorR,
    MaterialBaseColorG,
    MaterialBaseColorB,
    MaterialBaseColorA,
    MaterialMetallic,
    MaterialRoughness,
    MaterialEmissiveStrength,
    LightEnabled,
    LightIntensity,
    LightColorR,
    LightColorG,
    LightColorB,
    ParticleRate,
    CameraFov,
    // GPU fluid volume (node must have particle_fluid).
    FluidSourceDensity,
    FluidSourceRadius,
    FluidSourceVelocityY,
    FluidSource2Density,
    FluidSource2VelocityY,
    FluidDensityDissipation,
    FluidDyeColorR,
    FluidDyeColorG,
    FluidDyeColorB,
};

struct OscBinding {
    std::string address;
    int arg_index = 0;
    OscParamTarget target = OscParamTarget::NodePositionX;
    std::string node_path;
    std::uint32_t material_id = 0;
};

struct OscLearnedMessage {
    std::string address;
    std::vector<std::string> arg_preview;
    std::uint32_t receive_count = 0;
    double last_seen_seconds = 0.0;
};

const char* oscParamTargetLabel(OscParamTarget target);
std::string oscBindingTargetSummary(const OscBinding& binding);

} // namespace genesis
