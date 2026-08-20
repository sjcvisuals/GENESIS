#pragma once

#include <filesystem>
#include <string>

namespace rxext::genesis_remover {

// Always-on diagnostics for PIXERA failures. Writes to:
//   %TEMP%\ExtGenesisRemover.log
//   <dll-dir>\remover-init.log  (when module path is known)
void removerLog(const std::string& line);

std::filesystem::path removerTempLogPath();

// Bump when diagnosing timeline load so the log proves the new DLL is running.
inline constexpr const char* kRemoverBuildStamp = "remover-timeline-v46-oop-settle-20260820";

} // namespace rxext::genesis_remover
