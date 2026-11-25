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

namespace {

void appendDebugMessage(OscParseResult& result, const OscMessage& message, int depth) {
    OscDebugLogEntry entry{};
    entry.nest_depth = depth;
    entry.kind = OscDebugKind::Message;
    entry.address = message.address;
    entry.type_tags = oscTypeTagsFromArgs(message.args);
    for (const OscArg& arg : message.args) {
        entry.arg_preview.push_back(oscArgToString(arg));
    }
    result.debug_log.push_back(std::move(entry));
}

void parseOscPacketDetailedInto(std::span<const std::uint8_t> data, int depth, OscParseResult& result,
                                std::string* out_error) {
    if (data.empty()) {
        OscDebugLogEntry entry{};
        entry.nest_depth = depth;
        entry.kind = OscDebugKind::Error;
        entry.address = "(error)";
        entry.note = "Empty OSC packet";
        result.debug_log.push_back(std::move(entry));
        if (out_error && out_error->empty()) {
            *out_error = "Empty OSC packet";
        }
        return;
    }

    if (data[0] == '#') {
        std::size_t offset = 0;
        std::string header;
        if (!readPaddedString(data, offset, header) || header != "#bundle") {
            OscDebugLogEntry entry{};
            entry.nest_depth = depth;
            entry.kind = OscDebugKind::Error;
            entry.address = "(error)";
            entry.note = "Invalid OSC bundle header";
            result.debug_log.push_back(std::move(entry));
            if (out_error) *out_error = "Invalid OSC bundle header";
            return;
        }
        if (offset + 8 > data.size()) {
            OscDebugLogEntry entry{};
            entry.nest_depth = depth;
            entry.kind = OscDebugKind::Error;
            entry.address = "#bundle";
            entry.note = "Truncated OSC bundle timetag";
            result.debug_log.push_back(std::move(entry));
            if (out_error) *out_error = "Truncated OSC bundle timetag";
            return;
        }

        const std::uint32_t hi = readUInt32Be(data, offset);
        offset += 4;
        const std::uint32_t lo = readUInt32Be(data, offset);
        offset += 4;
        const std::uint64_t tag = (static_cast<std::uint64_t>(hi) << 32) | lo;

        OscDebugLogEntry bundle{};
        bundle.nest_depth = depth;
        bundle.kind = OscDebugKind::Bundle;
        bundle.address = "#bundle";
        if (tag == 1) {
            bundle.note = "timetag=immediate";
        } else {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "timetag=0x%016llx",
                          static_cast<unsigned long long>(tag));
            bundle.note = buf;
        }
        const std::size_t bundle_index = result.debug_log.size();
        result.debug_log.push_back(std::move(bundle));

        int element_count = 0;
        while (offset + 4 <= data.size()) {
            std::int32_t element_size = 0;
            if (!readInt32(data, offset, element_size) || element_size <= 0) {
                break;
            }
            const std::size_t element_size_u = static_cast<std::size_t>(element_size);
            if (offset + element_size_u > data.size()) {
                OscDebugLogEntry entry{};
                entry.nest_depth = depth + 1;
                entry.kind = OscDebugKind::Error;
                entry.address = "(error)";
                entry.note = "Truncated OSC bundle element";
                result.debug_log.push_back(std::move(entry));
                if (out_error) *out_error = "Truncated OSC bundle element";
                break;
            }

            const std::span<const std::uint8_t> element(data.data() + offset, element_size_u);
            offset += element_size_u;
            ++element_count;

            std::string element_error;
            parseOscPacketDetailedInto(element, depth + 1, result, &element_error);
            if (!element_error.empty() && out_error && out_error->empty()) {
                *out_error = element_error;
            }
        }

        result.debug_log[bundle_index].note +=
            std::string("  elements=") + std::to_string(element_count);
        return;
    }

    std::string message_error;
    if (std::optional<OscMessage> message = parseOscMessage(data, &message_error)) {
        appendDebugMessage(result, *message, depth);
        result.messages.push_back(std::move(*message));
    } else {
        OscDebugLogEntry entry{};
        entry.nest_depth = depth;
        entry.kind = OscDebugKind::Error;
        entry.address = "(error)";
        entry.note = message_error.empty() ? "Failed to parse OSC message" : message_error;
        result.debug_log.push_back(std::move(entry));
        if (out_error) *out_error = entry.note;
    }
}

} // namespace

std::vector<OscMessage> parseOscPackets(std::span<const std::uint8_t> data, std::string* out_error) {
    return parseOscPacketDetailed(data, out_error).messages;
}

OscParseResult parseOscPacketDetailed(std::span<const std::uint8_t> data, std::string* out_error) {
    OscParseResult result{};
    parseOscPacketDetailedInto(data, 0, result, out_error);
    return result;
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

std::string oscTypeTagsFromArgs(const std::vector<OscArg>& args) {
    std::string tags = ",";
    tags.reserve(1 + args.size());
    for (const OscArg& arg : args) {
        switch (arg.type) {
        case OscArgType::Int32:
            tags.push_back('i');
            break;
        case OscArgType::Float32:
            tags.push_back('f');
            break;
        case OscArgType::String:
            tags.push_back('s');
            break;
        case OscArgType::Bool:
            tags.push_back(arg.boolean ? 'T' : 'F');
            break;
        default:
            tags.push_back('?');
            break;
        }
    }
    return tags;
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

std::string formatOscDebugLogEntry(const OscDebugLogEntry& entry) {
    std::string line;
    line.reserve(128);
    char head[96];
    std::snprintf(head, sizeof(head), "[%s] t=%.3f pkt=%llu d=%d ",
                  entry.wall_clock.empty() ? "-" : entry.wall_clock.c_str(), entry.time_seconds,
                  static_cast<unsigned long long>(entry.packet_seq), entry.nest_depth);
    line += head;
    for (int i = 0; i < entry.nest_depth; ++i) {
        line += "  ";
    }
    switch (entry.kind) {
    case OscDebugKind::Bundle:
        line += "BUNDLE ";
        line += entry.address;
        if (!entry.note.empty()) {
            line += "  ";
            line += entry.note;
        }
        break;
    case OscDebugKind::Error:
        line += "ERROR ";
        line += entry.note.empty() ? entry.address : entry.note;
        break;
    case OscDebugKind::Message:
    default:
        line += "MSG ";
        line += entry.address;
        if (!entry.type_tags.empty()) {
            line += "  ";
            line += entry.type_tags;
        }
        if (!entry.arg_preview.empty()) {
            line += "  args=[";
            for (std::size_t i = 0; i < entry.arg_preview.size(); ++i) {
                if (i > 0) line += ", ";
                line += entry.arg_preview[i];
            }
            line += "]";
        }
        break;
    }
    return line;
}

std::string formatOscDebugLog(const std::vector<OscDebugLogEntry>& entries) {
    std::string out;
    out.reserve(entries.size() * 96);
    for (const OscDebugLogEntry& entry : entries) {
        out += formatOscDebugLogEntry(entry);
        out += '\n';
    }
    return out;
}

} // namespace genesis
