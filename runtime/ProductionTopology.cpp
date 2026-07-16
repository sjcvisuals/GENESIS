#include "runtime/ProductionTopology.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace genesis {

namespace {

using json = nlohmann::json;

std::uint64_t fnv1a64(std::string_view data) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : data) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

Rect2D rectFromJson(const json& j) {
    Rect2D rect{};
    rect.x = j.value("x", 0);
    rect.y = j.value("y", 0);
    rect.width = j.value("width", 0u);
    rect.height = j.value("height", 0u);
    return rect;
}

json rectToJson(const Rect2D& rect) {
    return json{
        {"x", rect.x},
        {"y", rect.y},
        {"width", rect.width},
        {"height", rect.height},
    };
}

OutputTopology outputFromJson(const json& j) {
    OutputTopology output{};
    output.output_id = j.at("output_id").get<std::string>();
    output.name = j.value("name", output.output_id);
    output.output_index = j.value("output_index", 0);
    output.width = j.value("width", 0u);
    output.height = j.value("height", 0u);
    output.refresh_rate_hz = j.value("refresh_rate_hz", 0.0);
    output.enabled = j.value("enabled", true);
    output.logical_region = rectFromJson(j.at("logical_region"));
    output.render_viewport = rectFromJson(j.at("render_viewport"));
    if (j.contains("frame_budget_ms") && !j["frame_budget_ms"].is_null()) {
        output.frame_budget_ms = j["frame_budget_ms"].get<double>();
    }
    if (j.contains("pixel_format") && !j["pixel_format"].is_null()) {
        output.pixel_format = j["pixel_format"].get<std::string>();
    }
    if (j.contains("colour_format") && !j["colour_format"].is_null()) {
        output.colour_format = j["colour_format"].get<std::string>();
    }
    if (j.contains("surface_id") && !j["surface_id"].is_null()) {
        output.surface_id = j["surface_id"].get<std::string>();
    }
    if (j.contains("overscan_crop") && !j["overscan_crop"].is_null()) {
        output.overscan_crop = rectFromJson(j["overscan_crop"]);
    }
    return output;
}

json outputToJson(const OutputTopology& output) {
    json j = json{
        {"output_id", output.output_id},
        {"name", output.name},
        {"output_index", output.output_index},
        {"width", output.width},
        {"height", output.height},
        {"refresh_rate_hz", output.refresh_rate_hz},
        {"enabled", output.enabled},
        {"logical_region", rectToJson(output.logical_region)},
        {"render_viewport", rectToJson(output.render_viewport)},
    };
    if (output.frame_budget_ms.has_value()) {
        j["frame_budget_ms"] = *output.frame_budget_ms;
    }
    if (output.pixel_format.has_value()) {
        j["pixel_format"] = *output.pixel_format;
    }
    if (output.colour_format.has_value()) {
        j["colour_format"] = *output.colour_format;
    }
    if (output.surface_id.has_value()) {
        j["surface_id"] = *output.surface_id;
    }
    if (output.overscan_crop.has_value()) {
        j["overscan_crop"] = rectToJson(*output.overscan_crop);
    }
    return j;
}

GpuTopology gpuFromJson(const json& j) {
    GpuTopology gpu{};
    gpu.gpu_id = j.at("gpu_id").get<std::string>();
    gpu.name = j.value("name", gpu.gpu_id);
    if (j.contains("model") && !j["model"].is_null()) {
        gpu.model = j["model"].get<std::string>();
    }
    if (j.contains("vram_mb") && !j["vram_mb"].is_null()) {
        gpu.vram_mb = j["vram_mb"].get<std::uint64_t>();
    }
    if (j.contains("frame_budget_ms") && !j["frame_budget_ms"].is_null()) {
        gpu.frame_budget_ms = j["frame_budget_ms"].get<double>();
    }
    if (j.contains("outputs")) {
        for (const json& output_json : j["outputs"]) {
            gpu.outputs.push_back(outputFromJson(output_json));
        }
    }
    return gpu;
}

json gpuToJson(const GpuTopology& gpu) {
    json j = json{
        {"gpu_id", gpu.gpu_id},
        {"name", gpu.name},
        {"outputs", json::array()},
    };
    if (gpu.model.has_value()) {
        j["model"] = *gpu.model;
    }
    if (gpu.vram_mb.has_value()) {
        j["vram_mb"] = *gpu.vram_mb;
    }
    if (gpu.frame_budget_ms.has_value()) {
        j["frame_budget_ms"] = *gpu.frame_budget_ms;
    }
    for (const OutputTopology& output : gpu.outputs) {
        j["outputs"].push_back(outputToJson(output));
    }
    return j;
}

MachineTopology machineFromJson(const json& j) {
    MachineTopology machine{};
    machine.machine_id = j.at("machine_id").get<std::string>();
    machine.name = j.value("name", machine.machine_id);
    if (j.contains("gpus")) {
        for (const json& gpu_json : j["gpus"]) {
            machine.gpus.push_back(gpuFromJson(gpu_json));
        }
    }
    return machine;
}

json machineToJson(const MachineTopology& machine) {
    json j = json{
        {"machine_id", machine.machine_id},
        {"name", machine.name},
        {"gpus", json::array()},
    };
    for (const GpuTopology& gpu : machine.gpus) {
        j["gpus"].push_back(gpuToJson(gpu));
    }
    return j;
}

bool rectWithinCanvas(const Rect2D& rect, const LogicalCanvas& canvas, std::string_view label,
                      TopologyValidationResult& result) {
    const auto fits = [&](std::int64_t origin, std::uint32_t size, std::uint32_t canvas_size) {
        return origin >= 0 && static_cast<std::uint64_t>(origin) + size <= canvas_size;
    };

    if (!fits(rect.x, rect.width, canvas.width) || !fits(rect.y, rect.height, canvas.height)) {
        result.errors.push_back({TopologyValidationIssue::Severity::Error,
                                 std::string(label) + " is outside the logical canvas bounds"});
        return false;
    }
    return true;
}

void validateUniqueId(const std::string& id, std::string_view kind, std::unordered_set<std::string>& seen,
                      TopologyValidationResult& result) {
    if (id.empty()) {
        result.errors.push_back(
            {TopologyValidationIssue::Severity::Error, std::string(kind) + " ID must not be empty"});
        return;
    }
    if (!seen.insert(id).second) {
        result.errors.push_back(
            {TopologyValidationIssue::Severity::Error, "Duplicate " + std::string(kind) + " ID: " + id});
    }
}

} // namespace

ProductionTopology parseProductionTopologyJson(std::string_view json_text) {
    const json root = json::parse(json_text);
    if (root.value("format", std::string{}) != "genesis-topology") {
        throw std::runtime_error("Not a genesis-topology document");
    }

    ProductionTopology topology{};
    topology.version = root.value("version", 1);
    topology.production_id = root.at("production_id").get<std::string>();
    topology.name = root.value("name", topology.production_id);

    const json& canvas = root.at("logical_canvas");
    topology.logical_canvas.width = canvas.at("width").get<std::uint32_t>();
    topology.logical_canvas.height = canvas.at("height").get<std::uint32_t>();
    topology.logical_canvas.frame_rate_hz = canvas.at("frame_rate_hz").get<double>();

    if (root.contains("performance_profile") && !root["performance_profile"].is_null()) {
        topology.performance_profile = root["performance_profile"].get<std::string>();
    }
    if (root.contains("budget_policy") && !root["budget_policy"].is_null()) {
        topology.budget_policy = root["budget_policy"].get<std::string>();
    }

    if (root.contains("machines")) {
        for (const json& machine_json : root["machines"]) {
            topology.machines.push_back(machineFromJson(machine_json));
        }
    }
    return topology;
}

ProductionTopology loadProductionTopology(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open topology file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseProductionTopologyJson(buffer.str());
}

std::string serializeProductionTopologyJson(const ProductionTopology& topology) {
    json root = json{
        {"format", "genesis-topology"},
        {"version", topology.version},
        {"production_id", topology.production_id},
        {"name", topology.name},
        {"logical_canvas",
         {{"width", topology.logical_canvas.width},
          {"height", topology.logical_canvas.height},
          {"frame_rate_hz", topology.logical_canvas.frame_rate_hz}}},
        {"machines", json::array()},
    };
    if (topology.performance_profile.has_value()) {
        root["performance_profile"] = *topology.performance_profile;
    }
    if (topology.budget_policy.has_value()) {
        root["budget_policy"] = *topology.budget_policy;
    }
    for (const MachineTopology& machine : topology.machines) {
        root["machines"].push_back(machineToJson(machine));
    }
    return root.dump(2);
}

void saveProductionTopology(const ProductionTopology& topology, const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to write topology file: " + path.string());
    }
    out << serializeProductionTopologyJson(topology);
}

TopologyValidationResult validateProductionTopology(const ProductionTopology& topology) {
    TopologyValidationResult result;
    std::unordered_set<std::string> production_ids;
    validateUniqueId(topology.production_id, "production", production_ids, result);

    if (topology.logical_canvas.width == 0 || topology.logical_canvas.height == 0) {
        result.errors.push_back(
            {TopologyValidationIssue::Severity::Error, "Logical canvas width and height must be greater than zero"});
    }
    if (topology.logical_canvas.frame_rate_hz <= 0.0) {
        result.errors.push_back(
            {TopologyValidationIssue::Severity::Error, "Logical canvas frame_rate_hz must be greater than zero"});
    }

    if (topology.machines.empty()) {
        result.warnings.push_back(
            {TopologyValidationIssue::Severity::Warning, "Topology defines no machines"});
    }

    std::unordered_set<std::string> machine_ids;
    std::unordered_set<std::string> gpu_ids;
    std::unordered_set<std::string> output_ids;

    for (const MachineTopology& machine : topology.machines) {
        validateUniqueId(machine.machine_id, "machine", machine_ids, result);
        if (machine.gpus.empty()) {
            result.warnings.push_back({TopologyValidationIssue::Severity::Warning,
                                       "Machine '" + machine.machine_id + "' defines no GPUs"});
        }

        for (const GpuTopology& gpu : machine.gpus) {
            validateUniqueId(gpu.gpu_id, "GPU", gpu_ids, result);
            if (gpu.frame_budget_ms.has_value() && *gpu.frame_budget_ms <= 0.0) {
                result.errors.push_back({TopologyValidationIssue::Severity::Error,
                                         "GPU '" + gpu.gpu_id + "' frame_budget_ms must be positive"});
            }

            for (const OutputTopology& output : gpu.outputs) {
                validateUniqueId(output.output_id, "output", output_ids, result);
                if (!output.enabled) {
                    continue;
                }

                if (output.width == 0 || output.height == 0) {
                    result.errors.push_back({TopologyValidationIssue::Severity::Error,
                                             "Output '" + output.output_id + "' width/height must be greater than zero"});
                }
                if (output.refresh_rate_hz <= 0.0) {
                    result.errors.push_back({TopologyValidationIssue::Severity::Error,
                                             "Output '" + output.output_id +
                                                 "' refresh_rate_hz must be greater than zero"});
                }
                if (output.frame_budget_ms.has_value() && *output.frame_budget_ms <= 0.0) {
                    result.errors.push_back({TopologyValidationIssue::Severity::Error,
                                             "Output '" + output.output_id +
                                                 "' frame_budget_ms must be positive"});
                }

                rectWithinCanvas(output.logical_region, topology.logical_canvas,
                                 "Output '" + output.output_id + "' logical_region", result);
                if (output.render_viewport.width == 0 || output.render_viewport.height == 0) {
                    result.errors.push_back({TopologyValidationIssue::Severity::Error,
                                             "Output '" + output.output_id +
                                                 "' render_viewport must have non-zero size"});
                }

                const double production_hz = topology.logical_canvas.frame_rate_hz;
                if (production_hz > 0.0 &&
                    std::abs(output.refresh_rate_hz - production_hz) > 0.01) {
                    result.warnings.push_back(
                        {TopologyValidationIssue::Severity::Warning,
                         "Output '" + output.output_id + "' refresh rate (" +
                             std::to_string(output.refresh_rate_hz) + " Hz) differs from production frame rate (" +
                             std::to_string(production_hz) + " Hz)"});
                }
            }
        }
    }

    return result;
}

std::string topologyContentHash(const ProductionTopology& topology) {
    const std::uint64_t hash = fnv1a64(serializeProductionTopologyJson(topology));
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

} // namespace genesis
