#include "RemoverLog.h"

#include "ModulePaths.h"

#include <chrono>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rxext::genesis_remover {
namespace {

std::mutex g_log_mutex;

void appendToFile(const std::filesystem::path& path, const std::string& line) {
    if (path.empty()) {
        return;
    }
    try {
        std::ofstream out(path, std::ios::app);
        if (!out) {
            return;
        }
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count();
        out << ms << " [" << kRemoverBuildStamp << "] " << line << '\n';
    } catch (...) {
    }
}

} // namespace

std::filesystem::path removerTempLogPath() {
#if defined(_WIN32)
    char temp[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(temp) / "ExtGenesisRemover.log";
#else
    return std::filesystem::path("/tmp/ExtGenesisRemover.log");
#endif
}

void removerLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    appendToFile(removerTempLogPath(), line);
    const auto module_dir = extensionModuleDirectory();
    if (!module_dir.empty()) {
        appendToFile(module_dir / "remover-init.log", line);
    }
}

} // namespace rxext::genesis_remover
