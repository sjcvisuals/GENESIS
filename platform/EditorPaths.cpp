#include "platform/EditorPaths.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace genesis {

namespace {

std::string& prefPathStorage() {
    static std::string path;
    return path;
}

std::string& iniPathStorage() {
    static std::string path;
    return path;
}

void initPrefPath() {
    if (!prefPathStorage().empty()) {
        return;
    }

    if (char* pref = SDL_GetPrefPath("Genesis", "Editor")) {
        prefPathStorage() = pref;
        SDL_free(pref);
    } else {
        prefPathStorage() = "./.genesis_editor/";
    }

    iniPathStorage() = (editorDataRoot() / "imgui.ini").string();
}

} // namespace

std::filesystem::path editorDataRoot() {
    initPrefPath();
    return prefPathStorage();
}

std::filesystem::path editorProjectsDir() {
    return editorDataRoot() / "projects";
}

std::filesystem::path editorRecentFilePath() {
    return editorDataRoot() / "recent.json";
}

std::filesystem::path editorLayoutIniPath() {
    initPrefPath();
    return iniPathStorage();
}

const char* editorLayoutIniCString() {
    initPrefPath();
    return iniPathStorage().c_str();
}

void ensureEditorDataDirs() {
    std::filesystem::create_directories(editorProjectsDir());
}

std::filesystem::path uniqueProjectPath(const std::string& base_name) {
    ensureEditorDataDirs();
    const std::filesystem::path dir = editorProjectsDir();
    std::filesystem::path candidate = dir / (base_name + ".scene.json");
    if (!std::filesystem::exists(candidate)) {
        return candidate;
    }

    for (int i = 2; i < 1000; ++i) {
        candidate = dir / (base_name + " " + std::to_string(i) + ".scene.json");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error("Could not allocate a unique project file name");
}

} // namespace genesis
