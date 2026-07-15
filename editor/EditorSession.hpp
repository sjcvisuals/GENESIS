#pragma once

#include "scene/SceneRuntime.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

class EditorSession {
public:
    static constexpr std::size_t kMaxRecentFiles = 12;

    bool dirty = false;
    std::filesystem::path document_path;
    std::vector<std::filesystem::path> recent_files;

    void loadRecentList();
    void saveRecentList() const;
    void touchRecent(const std::filesystem::path& path);

    std::filesystem::path bundledScenePath(const char* file_name) const;

    void loadBundledScene(SceneRuntime& runtime, const char* file_name);
    void loadDocument(SceneRuntime& runtime, const std::filesystem::path& path);
    void saveDocument(SceneRuntime& runtime, const std::filesystem::path& path);

    std::string displayName() const;
    std::string windowTitle() const;

    bool isBundledScenePath(const std::filesystem::path& path) const;
    bool requiresSaveAs() const;

private:
    void afterLoad(const std::filesystem::path& path);
};

} // namespace genesis
