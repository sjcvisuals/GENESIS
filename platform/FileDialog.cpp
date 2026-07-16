#include "platform/FileDialog.hpp"

#include <SDL3/SDL.h>

#include <mutex>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace genesis {

namespace {

struct DialogState {
    std::function<void(std::filesystem::path)> callback;
    // SDL reads filters asynchronously after Show*FileDialog returns — keep alive until callback.
    std::vector<std::string> filter_names;
    std::vector<std::string> filter_patterns;
    std::vector<SDL_DialogFileFilter> sdl_filters;
    std::string default_location;
};

struct PendingDialogResult {
    std::function<void(std::filesystem::path)> callback;
    std::filesystem::path path;
};

std::mutex& pendingMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<PendingDialogResult>& pendingResults() {
    static std::vector<PendingDialogResult> pending;
    return pending;
}

void enqueueResult(std::function<void(std::filesystem::path)> callback, std::filesystem::path path) {
    if (!callback) {
        return;
    }
    std::lock_guard<std::mutex> lock(pendingMutex());
    pendingResults().push_back(PendingDialogResult{std::move(callback), std::move(path)});
}

void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int /*filter*/) {
    std::unique_ptr<DialogState> state(static_cast<DialogState*>(userdata));
    if (!state) {
        return;
    }
    std::filesystem::path path;
    if (filelist != nullptr && filelist[0] != nullptr) {
        path = std::filesystem::path(filelist[0]);
    }
    enqueueResult(std::move(state->callback), std::move(path));
}

std::unique_ptr<DialogState> makeDialogState(const std::vector<FileDialogFilter>& filters,
                                             std::function<void(std::filesystem::path)> on_result,
                                             const std::filesystem::path& default_path) {
    auto state = std::make_unique<DialogState>();
    state->callback = std::move(on_result);
    state->filter_names.reserve(filters.size());
    state->filter_patterns.reserve(filters.size());
    state->sdl_filters.reserve(filters.size());

    for (const FileDialogFilter& filter : filters) {
        state->filter_names.emplace_back(filter.name ? filter.name : "Files");
        state->filter_patterns.emplace_back(filter.pattern ? filter.pattern : "*");
    }
    for (std::size_t i = 0; i < state->filter_names.size(); ++i) {
        state->sdl_filters.push_back(
            SDL_DialogFileFilter{state->filter_names[i].c_str(), state->filter_patterns[i].c_str()});
    }
    if (!default_path.empty()) {
        state->default_location = default_path.string();
    }
    return state;
}

} // namespace

void openFileDialog(SDL_Window* window, const std::vector<FileDialogFilter>& filters,
                    std::function<void(std::filesystem::path)> on_result,
                    const std::filesystem::path& default_path) {
    auto state = makeDialogState(filters, std::move(on_result), default_path);
    DialogState* raw = state.release();
    const char* default_location =
        raw->default_location.empty() ? nullptr : raw->default_location.c_str();
    SDL_ShowOpenFileDialog(dialogCallback, raw, window,
                           raw->sdl_filters.empty() ? nullptr : raw->sdl_filters.data(),
                           static_cast<int>(raw->sdl_filters.size()), default_location, false);
}

void saveFileDialog(SDL_Window* window, const std::vector<FileDialogFilter>& filters,
                    std::function<void(std::filesystem::path)> on_result,
                    const std::filesystem::path& default_path) {
    auto state = makeDialogState(filters, std::move(on_result), default_path);
    DialogState* raw = state.release();
    const char* default_location =
        raw->default_location.empty() ? nullptr : raw->default_location.c_str();
    SDL_ShowSaveFileDialog(dialogCallback, raw, window,
                           raw->sdl_filters.empty() ? nullptr : raw->sdl_filters.data(),
                           static_cast<int>(raw->sdl_filters.size()), default_location);
}

void pollFileDialogResults() {
    std::vector<PendingDialogResult> local;
    {
        std::lock_guard<std::mutex> lock(pendingMutex());
        local.swap(pendingResults());
    }
    for (PendingDialogResult& entry : local) {
        if (entry.callback) {
            entry.callback(std::move(entry.path));
        }
    }
}

} // namespace genesis
