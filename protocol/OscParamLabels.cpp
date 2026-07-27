#include "protocol/OscTypes.hpp"

namespace genesis {

const char* oscParamTargetLabel(OscParamTarget target) {
    switch (target) {
    case OscParamTarget::NodePositionX:
        return "Position X";
    case OscParamTarget::NodePositionY:
        return "Position Y";
    case OscParamTarget::NodePositionZ:
        return "Position Z";
    case OscParamTarget::NodeScaleX:
        return "Scale X";
    case OscParamTarget::NodeScaleY:
        return "Scale Y";
    case OscParamTarget::NodeScaleZ:
        return "Scale Z";
    case OscParamTarget::NodeRotationX:
        return "Rotation X";
    case OscParamTarget::NodeRotationY:
        return "Rotation Y";
    case OscParamTarget::NodeRotationZ:
        return "Rotation Z";
    case OscParamTarget::NodeVisible:
        return "Visible";
    case OscParamTarget::MaterialBaseColorR:
        return "Base Color R";
    case OscParamTarget::MaterialBaseColorG:
        return "Base Color G";
    case OscParamTarget::MaterialBaseColorB:
        return "Base Color B";
    case OscParamTarget::MaterialBaseColorA:
        return "Base Color A";
    case OscParamTarget::MaterialMetallic:
        return "Metallic";
    case OscParamTarget::MaterialRoughness:
        return "Roughness";
    case OscParamTarget::MaterialEmissiveStrength:
        return "Emissive Strength";
    case OscParamTarget::LightEnabled:
        return "Light Enabled";
    case OscParamTarget::LightIntensity:
        return "Light Intensity";
    case OscParamTarget::LightColorR:
        return "Light Color R";
    case OscParamTarget::LightColorG:
        return "Light Color G";
    case OscParamTarget::LightColorB:
        return "Light Color B";
    case OscParamTarget::ParticleRate:
        return "Particle Rate";
    case OscParamTarget::CameraFov:
        return "Camera FOV";
    case OscParamTarget::FluidSourceDensity:
        return "Fluid Source Density";
    case OscParamTarget::FluidSourceRadius:
        return "Fluid Source Radius";
    case OscParamTarget::FluidSourceVelocityY:
        return "Fluid Source Velocity Y";
    case OscParamTarget::FluidSource2Density:
        return "Fluid Source 2 Density";
    case OscParamTarget::FluidSource2VelocityY:
        return "Fluid Source 2 Velocity Y";
    case OscParamTarget::FluidDensityDissipation:
        return "Fluid Density Dissipation";
    case OscParamTarget::FluidDyeColorR:
        return "Fluid Dye R";
    case OscParamTarget::FluidDyeColorG:
        return "Fluid Dye G";
    case OscParamTarget::FluidDyeColorB:
        return "Fluid Dye B";
    default:
        return "Unknown";
    }
}

std::string oscBindingTargetSummary(const OscBinding& binding) {
    std::string summary = binding.node_path.empty() ? "(scene)" : binding.node_path;
    summary += " · ";
    summary += oscParamTargetLabel(binding.target);
    return summary;
}

} // namespace genesis
