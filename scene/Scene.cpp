#include "scene/Scene.hpp"

#include <algorithm>
#include <glm/mat4x4.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace genesis {

Node* Scene::find(NodeId id) {
    for (auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const Node* Scene::find(NodeId id) const {
    for (const auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

NodeId Scene::generateId() const {
    NodeId max_id = 0;
    for (const auto& n : nodes) {
        max_id = std::max(max_id, n.id);
    }
    return max_id + 1;
}

void Scene::detachFromParent(Node& node) {
    if (node.parent.has_value()) {
        if (Node* parent = find(*node.parent)) {
            auto& children = parent->children;
            children.erase(std::remove(children.begin(), children.end(), node.id), children.end());
        }
    } else {
        roots.erase(std::remove(roots.begin(), roots.end(), node.id), roots.end());
    }
}

void Scene::updateWorldTransforms() {
    for (auto& n : nodes) n.world = glm::mat4(1.0f);

    struct StackItem {
        NodeId id;
        glm::mat4 parent_world;
    };
    std::vector<StackItem> stack;
    stack.reserve(nodes.size());

    for (NodeId root_id : roots) {
        stack.push_back({root_id, glm::mat4(1.0f)});
        while (!stack.empty()) {
            const StackItem item = stack.back();
            stack.pop_back();

            Node* n = find(item.id);
            if (!n) continue;

            n->world = item.parent_world * n->local.matrix();

            for (NodeId child : n->children) {
                stack.push_back({child, n->world});
            }
        }
    }
}

NodeId Scene::createEmptyNode(const std::string& name, std::optional<NodeId> parent) {
    Node node{};
    node.id = generateId();
    node.name = name;

    if (parent.has_value()) {
        Node* parent_node = find(*parent);
        if (!parent_node) {
            throw std::runtime_error("createEmptyNode: parent not found");
        }
        node.parent = parent;
        parent_node->children.push_back(node.id);
    } else {
        roots.push_back(node.id);
    }

    nodes.push_back(std::move(node));
    updateWorldTransforms();
    return nodes.back().id;
}

NodeId Scene::createMeshNode(const std::string& name, std::uint32_t mesh_resource,
                             std::optional<NodeId> parent, std::optional<std::uint32_t> material_resource) {
    const NodeId id = createEmptyNode(name, parent);
    if (Node* node = find(id)) {
        node->has_mesh = true;
        node->mesh_resource = mesh_resource;
        if (material_resource.has_value()) {
            node->has_material = true;
            node->material_resource = material_resource;
        }
    }
    return id;
}

NodeId Scene::createCameraNode(const std::string& name, std::optional<NodeId> parent) {
    const NodeId id = createEmptyNode(name, parent);
    if (Node* node = find(id)) {
        node->has_camera = true;
        node->camera = SceneCamera{};
        node->pickable = false;
    }
    return id;
}

NodeId Scene::createLightNode(const std::string& name, SceneLightType type, std::optional<NodeId> parent) {
    const NodeId id = createEmptyNode(name, parent);
    if (Node* node = find(id)) {
        node->has_light = true;
        SceneLight light{};
        light.type = type;
        node->light = light;
    }
    return id;
}

NodeId Scene::createParticleEmitterNode(const std::string& name, std::optional<NodeId> parent,
                                        ParticleEmitter emitter) {
    const NodeId id = createEmptyNode(name, parent);
    if (Node* node = find(id)) {
        node->has_particle_emitter = true;
        node->particle_emitter = std::move(emitter);
    }
    return id;
}

void Scene::setActiveOutputCamera(std::optional<NodeId> node_id) {
    if (!node_id.has_value()) {
        active_output_camera.reset();
        return;
    }
    const Node* node = find(*node_id);
    if (!node || !node->camera.has_value()) {
        return;
    }
    active_output_camera = node_id;
}

const Node* Scene::activeOutputCameraNode() const {
    if (!active_output_camera.has_value()) return nullptr;
    const Node* node = find(*active_output_camera);
    if (!node || !node->camera.has_value()) return nullptr;
    return node;
}

NodeId Scene::duplicateNode(NodeId id) {
    const Node* src_root = find(id);
    if (!src_root) return 0;

    std::vector<NodeId> subtree;
    std::vector<NodeId> stack = {id};
    while (!stack.empty()) {
        const NodeId cur = stack.back();
        stack.pop_back();
        subtree.push_back(cur);
        if (const Node* n = find(cur)) {
            for (NodeId child : n->children) {
                stack.push_back(child);
            }
        }
    }

    std::unordered_map<NodeId, NodeId> id_map;
    NodeId next_id = generateId();
    for (NodeId old_id : subtree) {
        id_map[old_id] = next_id++;
    }

    NodeId new_root_id = 0;
    for (NodeId old_id : subtree) {
        const Node* src = find(old_id);
        if (!src) continue;

        Node copy = *src;
        copy.id = id_map[old_id];
        copy.children.clear();

        if (old_id == id) {
            copy.name += " (Copy)";
            copy.parent = src_root->parent;
            new_root_id = copy.id;
        } else if (src->parent.has_value()) {
            copy.parent = id_map[*src->parent];
        }

        nodes.push_back(std::move(copy));
    }

    for (NodeId old_id : subtree) {
        const Node* src = find(old_id);
        Node* dst = find(id_map[old_id]);
        if (!src || !dst) continue;
        dst->children.reserve(src->children.size());
        for (NodeId old_child : src->children) {
            dst->children.push_back(id_map[old_child]);
        }
    }

    if (src_root->parent.has_value()) {
        if (Node* parent = find(*src_root->parent)) {
            parent->children.push_back(new_root_id);
        }
    } else {
        roots.push_back(new_root_id);
    }

    updateWorldTransforms();
    return new_root_id;
}

bool Scene::deleteNode(NodeId id) {
    if (!find(id)) return false;

    std::unordered_set<NodeId> to_delete;
    std::vector<NodeId> stack = {id};
    while (!stack.empty()) {
        const NodeId cur = stack.back();
        stack.pop_back();
        if (!to_delete.insert(cur).second) continue;
        if (const Node* n = find(cur)) {
            for (NodeId child : n->children) {
                stack.push_back(child);
            }
        }
    }

    if (Node* node = find(id)) {
        detachFromParent(*node);
    }

    if (active_output_camera.has_value() && to_delete.contains(*active_output_camera)) {
        active_output_camera.reset();
    }

    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [&](const Node& n) { return to_delete.contains(n.id); }),
                nodes.end());

    for (auto& n : nodes) {
        n.children.erase(std::remove_if(n.children.begin(), n.children.end(),
                                        [&](NodeId child) { return to_delete.contains(child); }),
                         n.children.end());
        if (n.parent.has_value() && to_delete.contains(*n.parent)) {
            n.parent.reset();
        }
    }

    roots.erase(std::remove_if(roots.begin(), roots.end(),
                               [&](NodeId root) { return to_delete.contains(root); }),
                roots.end());

    updateWorldTransforms();
    return true;
}

bool Scene::isDescendantOf(NodeId node, NodeId potential_ancestor) const {
    if (node == potential_ancestor) return true;
    const Node* n = find(node);
    if (!n || !n->parent.has_value()) return false;
    return isDescendantOf(*n->parent, potential_ancestor);
}

bool Scene::reparentNode(NodeId child_id, std::optional<NodeId> new_parent_id) {
    if (new_parent_id.has_value() && *new_parent_id == child_id) return false;
    if (new_parent_id.has_value() && isDescendantOf(*new_parent_id, child_id)) return false;

    Node* child = find(child_id);
    if (!child) return false;
    if (new_parent_id.has_value() && !find(*new_parent_id)) return false;

    detachFromParent(*child);
    child->parent = new_parent_id;

    if (new_parent_id.has_value()) {
        find(*new_parent_id)->children.push_back(child_id);
    } else {
        roots.push_back(child_id);
    }

    updateWorldTransforms();
    return true;
}

} // namespace genesis
