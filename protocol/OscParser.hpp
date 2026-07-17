#pragma once

#include "protocol/OscTypes.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace genesis {

std::optional<OscMessage> parseOscMessage(std::span<const std::uint8_t> data, std::string* out_error = nullptr);
std::vector<OscMessage> parseOscPackets(std::span<const std::uint8_t> data, std::string* out_error = nullptr);
std::string oscArgToString(const OscArg& arg);
float oscArgAsFloat(const OscArg& arg);

} // namespace genesis
