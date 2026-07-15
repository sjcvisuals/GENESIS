#include "editor/EditorSession.hpp"

#include "platform/EditorPaths.hpp"
#include "platform/Paths.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>

namespace genesis {

namespace {

std::filesystem::path normalizePath(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

} // namespace

void EditorSession::loadRecentList() {
    recent_files.clear();
    const std::filesystem::path recent_path = editorRecentFilePath();
    if (!std::filesystem::exists(recent_path)) {
        return;
    }

    try {
        std::ifstream in(recent_path);
        const nlohmann::json root = nlohmann::json::parse(in);
        if (!root.contains("recent") || !root["recent"].is_array()) {
            return;
        }
        for (const nlohmann::json& entry : root["recent"]) {
            if (!entry.is_string()) continue;
            const std::filesystem::path path = entry.get<std::string>();
            if (std::filesystem::exists(path)) {
                recent_files.push_back(path);
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to read recent files list: {}", ex.what());
    }
}

void EditorSession::saveRecentList() const {
    ensureEditorDataDirs();
    nlohmann::json root = nlohmann::json::object();
    nlohmann::json recent = nlohmann::json::array();
    for (const std::filesystem::path& path : recent_files) {
        recent.push_back(path.string());
    }
    root["recent"] = std::move(recent);

    std::ofstream out(editorRecentFilePath());
    if (!out) {
        spdlog::warn("Failed to write recent files list");
        return;
    }
    out << root.dump(2);
}

void EditorSession::touchRecent(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    const std::filesystem::path normalized = normalizePath(path);
    recent_files.erase(std::remove(recent_files.begin(), recent_files.end(), normalized),
                       recent_files.end());
    recent_files.insert(recent_files.begin(), normalized);
    if (recent_files.size() > kMaxRecentFiles) {
        recent_files.resize(kMaxRecentFiles);
    }
    saveRecentList();
}

std::filesystem::path EditorSession::bundledScenePath(const char* file_name) const {
    return resolveAssetPath(std::filesystem::path("assets/scenes") / file_name);
}

void EditorSession::loadBundledScene(SceneRuntime& runtime, const char* file_name) {
    loadDocument(runtime, bundledScenePath(file_name));
}

void EditorSession::loadDocument(SceneRuntime& runtime, const std::filesystem::path& path) {
    runtime.loadDocument(path);
    afterLoad(path);
}

void EditorSession::saveDocument(SceneRuntime& runtime, const std::filesystem::path& path) {
    ensureEditorDataDirs();
    const std::filesystem::path normalized = normalizePath(path);
    runtime.saveDocument(normalized);
    afterLoad(normalized);
    dirty = false;
}

void EditorSession::afterLoad(const std::filesystem::path& path) {
    document_path = normalizePath(path);
    touchRecent(document_path);
    dirty = false;
}

std::string EditorSession::displayName() const {
    if (document_path.empty()) {
        return "Untitled Scene";
    }
    return document_path.filename().string();
}

std::string EditorSession::windowTitle() const {
    std::string title = "GENESIS — Scene Editor";
    if (!document_path.empty()) {
        title += " — ";
        title += document_path.string();
    } else {
        title += " — Untitled Scene";
    }
    if (dirty) {
        title += " *";
    }
    return title;
}

bool EditorSession::isBundledScenePath(const std::filesystem::path& path) const {
    if (path.empty()) {
        return false;
    }
    try {
        const std::filesystem::path bundled_root = resolveAssetPath("assets/scenes");
        const std::filesystem::path normalized = normalizePath(path);
        const std::filesystem::path root = normalizePath(bundled_root);
        const std::string norm = normalized.string();
        const std::string root_str = root.string();
        if (norm.size() < root_str.size()) {
            return false;
        }
        return norm.compare(0, root_str.size(), root_str) == 0;
    } catch (const std::exception&) {
        return false;
    }
}

bool EditorSession::requiresSaveAs() const {
    return document_path.empty() || isBundledScenePath(document_path);
}

} // namespace genesis
