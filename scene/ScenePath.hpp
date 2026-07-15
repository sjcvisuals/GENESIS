#pragma once

#include "scene/Scene.hpp"

#include <optional>
#include <string>

namespace genesis {

std::string nodePath(const Scene& scene, NodeId id);
std::optional<NodeId> findNodeByPath(const Scene& scene, const std::string& path);

} // namespace genesis
