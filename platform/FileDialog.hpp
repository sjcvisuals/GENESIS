#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;

namespace genesis {

struct FileDialogFilter {
    const char* name = "All Files";
    const char* pattern = "*"; // e.g. "png;jpg;jpeg"
};

// Non-blocking SDL3 file dialogs. Results are queued and delivered on the main
// thread when pollFileDialogResults() runs — never touch Vulkan from the SDL callback.
// `default_path` may be a file or directory; empty uses the platform default.
void openFileDialog(SDL_Window* window, const std::vector<FileDialogFilter>& filters,
                    std::function<void(std::filesystem::path)> on_result,
                    const std::filesystem::path& default_path = {});

void saveFileDialog(SDL_Window* window, const std::vector<FileDialogFilter>& filters,
                    std::function<void(std::filesystem::path)> on_result,
                    const std::filesystem::path& default_path = {});

// Invoke pending dialog callbacks on the calling (main) thread. Call once per frame.
void pollFileDialogResults();

} // namespace genesis
