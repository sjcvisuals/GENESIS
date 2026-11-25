#pragma once

#include "protocol/OscTypes.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace genesis {

struct OscParseResult {
    std::vector<OscMessage> messages;
    std::vector<OscDebugLogEntry> debug_log; // nest_depth set; time/packet filled by caller
};

std::optional<OscMessage> parseOscMessage(std::span<const std::uint8_t> data, std::string* out_error = nullptr);
std::vector<OscMessage> parseOscPackets(std::span<const std::uint8_t> data, std::string* out_error = nullptr);
// Full walk: flat messages for ingest + nested debug rows (bundles keep depth).
OscParseResult parseOscPacketDetailed(std::span<const std::uint8_t> data, std::string* out_error = nullptr);
std::string oscArgToString(const OscArg& arg);
float oscArgAsFloat(const OscArg& arg);
std::string oscTypeTagsFromArgs(const std::vector<OscArg>& args);

} // namespace genesis
