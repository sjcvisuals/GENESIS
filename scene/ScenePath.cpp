#include "scene/ScenePath.hpp"

#include <algorithm>
#include <sstream>

namespace genesis {

namespace {

std::string joinPath(const std::vector<std::string>& parts) {
    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out << '/';
        out << parts[i];
    }
    return out.str();
}

} // namespace

std::string nodePath(const Scene& scene, NodeId id) {
    std::vector<std::string> parts;
    const Node* node = scene.find(id);
    while (node) {
        parts.push_back(node->name);
        if (!node->parent.has_value()) break;
        node = scene.find(*node->parent);
    }
    std::reverse(parts.begin(), parts.end());
    return joinPath(parts);
}

std::optional<NodeId> findNodeByPath(const Scene& scene, const std::string& path) {
    if (path.empty()) return std::nullopt;

    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    if (parts.empty()) return std::nullopt;

    for (NodeId root : scene.roots) {
        if (const Node* root_node = scene.find(root); root_node && root_node->name == parts.front()) {
            const Node* current = root_node;
            for (std::size_t i = 1; i < parts.size(); ++i) {
                const Node* next = nullptr;
                for (NodeId child_id : current->children) {
                    if (const Node* child = scene.find(child_id); child && child->name == parts[i]) {
                        next = child;
                        break;
                    }
                }
                if (!next) return std::nullopt;
                current = next;
            }
            return current->id;
        }
    }
    return std::nullopt;
}

} // namespace genesis
