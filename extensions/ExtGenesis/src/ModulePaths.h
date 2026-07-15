#pragma once

#include <filesystem>

namespace rxext::genesis_ext {

// Directory containing ExtGenesis.dll (used to locate bundled assets/).
std::filesystem::path extensionModuleDirectory();

} // namespace rxext::genesis_ext
