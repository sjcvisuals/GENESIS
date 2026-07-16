#pragma once

#include "runtime/ProductionTopology.hpp"

#include <optional>
#include <string>
#include <vector>

namespace genesis {

// Identifies which node in a distributed topology this Player / ExtGenesis instance simulates.
// Editor authoring does not use node assignment — it always runs on one local monitor.
struct NodeAssignment {
    std::string machine_id;
    std::string gpu_id;
    // Empty = all enabled outputs assigned to gpu_id on machine_id.
    std::vector<std::string> output_ids;
};

struct ResolvedOutputAssignment {
    std::string machine_id;
    std::string gpu_id;
    OutputTopology output;
    std::optional<std::string> camera_node_path;
};

std::vector<ResolvedOutputAssignment> resolveOutputAssignments(const ProductionTopology& topology,
                                                               const NodeAssignment& assignment);

} // namespace genesis
