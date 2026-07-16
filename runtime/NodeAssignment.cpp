#include "runtime/NodeAssignment.hpp"

#include <stdexcept>
#include <unordered_set>

namespace genesis {

namespace {

bool outputIdAllowed(const NodeAssignment& assignment, const std::string& output_id) {
    if (assignment.output_ids.empty()) {
        return true;
    }
    for (const std::string& id : assignment.output_ids) {
        if (id == output_id) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<ResolvedOutputAssignment> resolveOutputAssignments(const ProductionTopology& topology,
                                                               const NodeAssignment& assignment) {
    if (assignment.machine_id.empty() || assignment.gpu_id.empty()) {
        throw std::runtime_error("Node assignment requires machine_id and gpu_id");
    }

    const MachineTopology* machine = nullptr;
    for (const MachineTopology& candidate : topology.machines) {
        if (candidate.machine_id == assignment.machine_id) {
            machine = &candidate;
            break;
        }
    }
    if (!machine) {
        throw std::runtime_error("Machine not found in topology: " + assignment.machine_id);
    }

    const GpuTopology* gpu = nullptr;
    for (const GpuTopology& candidate : machine->gpus) {
        if (candidate.gpu_id == assignment.gpu_id) {
            gpu = &candidate;
            break;
        }
    }
    if (!gpu) {
        throw std::runtime_error("GPU not found in topology: " + assignment.gpu_id);
    }

    std::unordered_set<std::string> requested_ids(assignment.output_ids.begin(), assignment.output_ids.end());
    std::vector<ResolvedOutputAssignment> resolved;
    resolved.reserve(gpu->outputs.size());

    for (const OutputTopology& output : gpu->outputs) {
        if (!output.enabled) {
            continue;
        }
        if (!outputIdAllowed(assignment, output.output_id)) {
            continue;
        }
        resolved.push_back({assignment.machine_id, assignment.gpu_id, output, std::nullopt});
        requested_ids.erase(output.output_id);
    }

    if (!assignment.output_ids.empty() && !requested_ids.empty()) {
        throw std::runtime_error("Requested output IDs not found on assigned GPU: " +
                                 *requested_ids.begin());
    }
    if (resolved.empty()) {
        throw std::runtime_error("No enabled outputs resolved for node assignment");
    }

    return resolved;
}

} // namespace genesis
