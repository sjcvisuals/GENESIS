#pragma once

#include "MatteIpc.h"

#include <filesystem>
#include <string>

namespace rxext::genesis_remover {

// Always-on diagnostics for PIXERA failures. Writes to:
//   %TEMP%\ExtGenesisRemover.log
//   <dll-dir>\remover-init.log  (when module path is known)
void removerLog(const std::string& line);

std::filesystem::path removerTempLogPath();

// Alias of matte_ipc::kBuildStamp — one stamp for DLL, host, BUILD_STAMP.txt, UI.
inline constexpr const char* kRemoverBuildStamp = ::genesis::matte_ipc::kBuildStamp;

} // namespace rxext::genesis_remover
