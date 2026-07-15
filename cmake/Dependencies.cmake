include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# Use codeload.github.com archives — no Git required, reliable on Windows.

# -----------------------------------------------------------------------------
# volk — Vulkan loader
# -----------------------------------------------------------------------------
FetchContent_Declare(
    volk
    URL https://codeload.github.com/zeux/volk/zip/refs/tags/1.4.304
)
FetchContent_MakeAvailable(volk)

# -----------------------------------------------------------------------------
# GLM
# -----------------------------------------------------------------------------
FetchContent_Declare(
    glm
    URL https://codeload.github.com/g-truc/glm/zip/refs/tags/1.0.1
)
FetchContent_MakeAvailable(glm)

# -----------------------------------------------------------------------------
# spdlog
# -----------------------------------------------------------------------------
FetchContent_Declare(
    spdlog
    URL https://codeload.github.com/gabime/spdlog/zip/refs/tags/v1.14.1
)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# -----------------------------------------------------------------------------
# SDL3
# -----------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_REVISION "SDL3-release-3.2.8" CACHE STRING "" FORCE)

FetchContent_Declare(
    SDL3
    URL https://codeload.github.com/libsdl-org/SDL/zip/refs/tags/release-3.2.8
)
FetchContent_MakeAvailable(SDL3)

# -----------------------------------------------------------------------------
# Dear ImGui
# -----------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    URL https://codeload.github.com/ocornut/imgui/zip/refs/heads/docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static volk::volk)
target_compile_definitions(imgui_lib PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DEFINE_MATH_OPERATORS)

# -----------------------------------------------------------------------------
# ImGuizmo — viewport transform gizmo (editor)
# -----------------------------------------------------------------------------
FetchContent_Declare(
    imguizmo
    URL https://codeload.github.com/CedricGuillemet/ImGuizmo/zip/refs/heads/master
)
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
    # Upstream SetRect() incorrectly sets mYMax from mXMax; breaks non-fullscreen viewports.
    set(_imguizmo_cpp "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
    if(EXISTS "${_imguizmo_cpp}")
        file(READ "${_imguizmo_cpp}" _imguizmo_src)
        string(REPLACE "gContext.mYMax = gContext.mY + gContext.mXMax;"
                       "gContext.mYMax = gContext.mY + gContext.mHeight;"
                       _imguizmo_src "${_imguizmo_src}")
        file(WRITE "${_imguizmo_cpp}" "${_imguizmo_src}")
    endif()
endif()

add_library(imguizmo_lib STATIC
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(imguizmo_lib PUBLIC ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imguizmo_lib PUBLIC imgui_lib glm::glm)
target_compile_definitions(imguizmo_lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

FetchContent_Declare(
    fastgltf
    URL https://codeload.github.com/spnda/fastgltf/zip/refs/tags/v0.8.0
)
FetchContent_MakeAvailable(fastgltf)

# -----------------------------------------------------------------------------
# nlohmann/json — scene document serialization
# -----------------------------------------------------------------------------
FetchContent_Declare(
    json
    URL https://codeload.github.com/nlohmann/json/zip/refs/tags/v3.11.3
)
FetchContent_MakeAvailable(json)
