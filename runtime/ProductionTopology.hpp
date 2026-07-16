#pragma once

// Production output topology for Player / PIXERA playback across machines, GPUs, and outputs.
// The editor does not consume this model — authoring remains single-monitor.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

struct Rect2D {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct LogicalCanvas {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double frame_rate_hz = 0.0;
};

struct OutputTopology {
    std::string output_id;
    std::string name;
    std::int32_t output_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double refresh_rate_hz = 0.0;
    bool enabled = true;
    Rect2D logical_region{};
    Rect2D render_viewport{};
    std::optional<double> frame_budget_ms;
    std::optional<std::string> pixel_format;
    std::optional<std::string> colour_format;
    std::optional<std::string> surface_id;
    std::optional<Rect2D> overscan_crop;
};

struct GpuTopology {
    std::string gpu_id;
    std::string name;
    std::optional<std::string> model;
    std::optional<std::uint64_t> vram_mb;
    std::optional<double> frame_budget_ms;
    std::vector<OutputTopology> outputs;
};

struct MachineTopology {
    std::string machine_id;
    std::string name;
    std::vector<GpuTopology> gpus;
};

struct ProductionTopology {
    int version = 1;
    std::string production_id;
    std::string name;
    LogicalCanvas logical_canvas{};
    std::vector<MachineTopology> machines;
    std::optional<std::string> performance_profile;
    std::optional<std::string> budget_policy; // strict | warn | adaptive_approved | unlocked_development
};

struct TopologyValidationIssue {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Error;
    std::string message;
};

struct TopologyValidationResult {
    bool ok() const { return errors.empty(); }
    std::vector<TopologyValidationIssue> errors;
    std::vector<TopologyValidationIssue> warnings;
};

ProductionTopology loadProductionTopology(const std::filesystem::path& path);
ProductionTopology parseProductionTopologyJson(std::string_view json_text);
void saveProductionTopology(const ProductionTopology& topology, const std::filesystem::path& path);
std::string serializeProductionTopologyJson(const ProductionTopology& topology);

TopologyValidationResult validateProductionTopology(const ProductionTopology& topology);
std::string topologyContentHash(const ProductionTopology& topology);

} // namespace genesis
