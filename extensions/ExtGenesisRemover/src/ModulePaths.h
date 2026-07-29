#pragma once

#include <filesystem>

namespace rxext::genesis_remover {

// Absolute directory containing ExtGenesisRemover.dll (empty if unknown).
std::filesystem::path extensionModuleDirectory();

} // namespace rxext::genesis_remover
