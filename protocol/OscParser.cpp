#include "protocol/OscParser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace genesis {

namespace {

std::size_t pad4(std::size_t size) {
    return (size + 3) & ~std::size_t{3};
}

bool readPaddedString(std::span<const std::uint8_t> data, std::size_t& offset, std::string& out) {
    if (offset >= data.size()) {
        return false;
    }
    const std::size_t start = offset;
    while (offset < data.size() && data[offset] != 0) {
        ++offset;
    }
    if (offset >= data.size()) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data.data() + start), offset - start);
    offset = pad4(offset + 1);
    return offset <= data.size();
}

std::uint32_t readUInt32Be(std::span<const std::uint8_t> data, std::size_t& offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

bool readInt32(std::span<const std::uint8_t> data, std::size_t& offset, std::int32_t& out) {
    if (offset + 4 > data.size()) {
        return false;
    }
    out = static_cast<std::int32_t>(readUInt32Be(data, offset));
    offset += 4;
    return true;
}

bool readFloat32(std::span<const std::uint8_t> data, std::size_t& offset, float& out) {
    if (offset + 4 > data.size()) {
        return false;
    }
    const std::uint32_t bits = readUInt32Be(data, offset);
    offset += 4;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

} // namespace

std::optional<OscMessage> parseOscMessage(std::span<const std::uint8_t> data, std::string* out_error) {
    if (data.size() < 4) {
        if (out_error) *out_error = "OSC packet too small";
        return std::nullopt;
    }

    OscMessage message{};
    std::size_t offset = 0;
    if (!readPaddedString(data, offset, message.address) || message.address.empty() ||
        message.address.front() != '/') {
        if (out_error) *out_error = "Invalid OSC address";
        return std::nullopt;
    }

    if (offset >= data.size()) {
        return message;
    }

    std::string type_tags;
    if (!readPaddedString(data, offset, type_tags) || type_tags.empty() || type_tags.front() != ',') {
        if (out_error) *out_error = "Invalid OSC type tag string";
        return std::nullopt;
    }

    for (std::size_t i = 1; i < type_tags.size(); ++i) {
        switch (type_tags[i]) {
        case 'i': {
            OscArg arg{};
            arg.type = OscArgType::Int32;
            if (!readInt32(data, offset, arg.i32)) {
                if (out_error) *out_error = "Truncated int32 OSC argument";
                return std::nullopt;
            }
            message.args.push_back(arg);
            break;
        }
        case 'f': {
            OscArg arg{};
            arg.type = OscArgType::Float32;
            if (!readFloat32(data, offset, arg.f32)) {
                if (out_error) *out_error = "Truncated float OSC argument";
                return std::nullopt;
            }
            message.args.push_back(arg);
            break;
        }
        case 's': {
            OscArg arg{};
            arg.type = OscArgType::String;
            if (!readPaddedString(data, offset, arg.string)) {
                if (out_error) *out_error = "Truncated string OSC argument";
                return std::nullopt;
            }
            message.args.push_back(arg);
            break;
        }
        case 'T': {
            OscArg arg{};
            arg.type = OscArgType::Bool;
            arg.boolean = true;
            message.args.push_back(arg);
            break;
        }
        case 'F': {
            OscArg arg{};
            arg.type = OscArgType::Bool;
            arg.boolean = false;
            message.args.push_back(arg);
            break;
        }
        default:
            if (out_error) *out_error = std::string("Unsupported OSC type tag: ") + type_tags[i];
            return std::nullopt;
        }
    }

    return message;
}

std::vector<OscMessage> parseOscPackets(std::span<const std::uint8_t> data, std::string* out_error) {
    std::vector<OscMessage> messages;
    if (data.empty()) {
        return messages;
    }

    if (data[0] == '#') {
        std::size_t offset = 0;
        std::string header;
        if (!readPaddedString(data, offset, header) || header != "#bundle") {
            if (out_error) *out_error = "Invalid OSC bundle header";
            return messages;
        }
        if (offset + 8 > data.size()) {
            if (out_error) *out_error = "Truncated OSC bundle timetag";
            return messages;
        }
        offset += 8;

        while (offset + 4 <= data.size()) {
            std::int32_t element_size = 0;
            if (!readInt32(data, offset, element_size) || element_size <= 0) {
                break;
            }
            const std::size_t element_size_u = static_cast<std::size_t>(element_size);
            if (offset + element_size_u > data.size()) {
                if (out_error) *out_error = "Truncated OSC bundle element";
                break;
            }

            const std::span<const std::uint8_t> element(data.data() + offset, element_size_u);
            offset += element_size_u;

            std::string element_error;
            std::vector<OscMessage> nested = parseOscPackets(element, &element_error);
            if (!nested.empty()) {
                messages.insert(messages.end(), nested.begin(), nested.end());
            } else if (!element_error.empty() && messages.empty() && out_error) {
                *out_error = element_error;
            }
        }
        return messages;
    }

    if (std::optional<OscMessage> message = parseOscMessage(data, out_error)) {
        messages.push_back(std::move(*message));
    }
    return messages;
}

std::string oscArgToString(const OscArg& arg) {
    switch (arg.type) {
    case OscArgType::Int32:
        return std::to_string(arg.i32);
    case OscArgType::Float32: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(arg.f32));
        return buf;
    }
    case OscArgType::String:
        return arg.string;
    case OscArgType::Bool:
        return arg.boolean ? "true" : "false";
    default:
        return "?";
    }
}

float oscArgAsFloat(const OscArg& arg) {
    switch (arg.type) {
    case OscArgType::Int32:
        return static_cast<float>(arg.i32);
    case OscArgType::Float32:
        return arg.f32;
    case OscArgType::Bool:
        return arg.boolean ? 1.0f : 0.0f;
    default:
        return 0.0f;
    }
}

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
