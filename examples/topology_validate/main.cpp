#include "runtime/ProductionTopology.hpp"

#include <iostream>
#include <string>

namespace {

int validatePath(const std::filesystem::path& path) {
    try {
        const genesis::ProductionTopology topology = genesis::loadProductionTopology(path);
        const genesis::TopologyValidationResult result = genesis::validateProductionTopology(topology);
        const std::string hash = genesis::topologyContentHash(topology);

        std::cout << path.filename().string() << '\n';
        std::cout << "  production_id: " << topology.production_id << '\n';
        std::cout << "  logical canvas: " << topology.logical_canvas.width << " x "
                  << topology.logical_canvas.height << " @ " << topology.logical_canvas.frame_rate_hz
                  << " Hz\n";
        std::cout << "  topology hash: " << hash << '\n';

        for (const genesis::TopologyValidationIssue& warning : result.warnings) {
            std::cout << "  warning: " << warning.message << '\n';
        }
        for (const genesis::TopologyValidationIssue& error : result.errors) {
            std::cout << "  error: " << error.message << '\n';
        }

        if (!result.ok()) {
            return 1;
        }
        std::cout << "  status: ok\n\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << path.filename().string() << " failed: " << ex.what() << '\n';
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: genesis_topology_validate <topology.json> [...]\n";
        return 1;
    }

    int exit_code = 0;
    for (int i = 1; i < argc; ++i) {
        exit_code |= validatePath(argv[i]);
    }
    return exit_code;
}
