#pragma once

#include <filesystem>
#include <string>

namespace genesis {

// Persistent editor data (layout, recent files, user projects). Uses SDL_GetPrefPath.
std::filesystem::path editorDataRoot();
std::filesystem::path editorProjectsDir();
std::filesystem::path editorRecentFilePath();
std::filesystem::path editorLayoutIniPath();

// Stable C string for ImGui::GetIO().IniFilename (valid for process lifetime).
const char* editorLayoutIniCString();

void ensureEditorDataDirs();

std::filesystem::path uniqueProjectPath(const std::string& base_name);

} // namespace genesis
