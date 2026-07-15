cmake_minimum_required(VERSION 3.24)
project(GENESIS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GENESIS_BUILD_EXAMPLES "Build example applications" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/VulkanSetup.cmake)
include(cmake/Dependencies.cmake)
include(cmake/CompileShaders.cmake)

add_library(genesis_vulkan INTERFACE)
target_include_directories(genesis_vulkan INTERFACE "${GENESIS_VULKAN_INCLUDE_DIR}")
target_link_libraries(genesis_vulkan INTERFACE volk::volk)
if(WIN32)
    target_compile_definitions(genesis_vulkan INTERFACE VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
endif()

# Shaders
genesis_compile_shaders(genesis_shaders
    shaders/triangle.vert
    shaders/triangle.frag
    shaders/mesh.vert
    shaders/mesh.frag
    shaders/grid.vert
    shaders/grid.frag
)

# -----------------------------------------------------------------------------
# Platform
# -----------------------------------------------------------------------------
add_library(genesis_platform STATIC
    platform/Window.cpp
    platform/Input.cpp
    platform/Timer.cpp
    platform/ImGuiLayer.cpp
    platform/Paths.cpp
)
target_include_directories(genesis_platform PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(genesis_platform PUBLIC
    SDL3::SDL3-static
    imgui_lib
    spdlog::spdlog
    glm::glm
    genesis_vulkan
)

# -----------------------------------------------------------------------------
# Renderer
# -----------------------------------------------------------------------------
add_library(genesis_renderer STATIC
    renderer/Renderer.cpp
    renderer/OffscreenRenderer.cpp
    renderer/MaterialBinding.cpp
    renderer/vulkan/VulkanContext.cpp
    renderer/vulkan/Swapchain.cpp
    renderer/vulkan/Buffer.cpp
    renderer/vulkan/Pipeline.cpp
    renderer/vulkan/ShaderModule.cpp
    renderer/vulkan/Texture.cpp
    renderer/vulkan/ExternalRenderTarget.cpp
    renderer/vulkan/ExternalSync.cpp
    renderer/vulkan/VulkanInterop.cpp
)
target_include_directories(genesis_renderer PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(genesis_renderer PUBLIC
    genesis_platform
    genesis_vulkan
)
add_dependencies(genesis_renderer genesis_shaders)
target_sources(genesis_renderer PRIVATE ${GENESIS_SHADER_SPV})
set_source_files_properties(${GENESIS_SHADER_SPV} PROPERTIES GENERATED TRUE)

target_compile_definitions(genesis_renderer PRIVATE
    GENESIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders/spv"
)

# -----------------------------------------------------------------------------
# Engine
# -----------------------------------------------------------------------------
add_library(genesis_engine STATIC
    engine/Application.cpp
    scene/Transform.cpp
    scene/Camera.cpp
    scene/Scene.cpp
    scene/SceneCamera.cpp
    scene/Lighting.cpp
    scene/ScenePath.cpp
    scene/SceneSerializer.cpp
    scene/GnsPackage.cpp
    scene/SceneRuntime.cpp
    host/HostBridge.cpp
    assets/GltfLoader.cpp
    assets/GltfTextures.cpp
    assets/MeshUpload.cpp
    assets/PrimitiveMeshes.cpp
    assets/GridMesh.cpp
)
target_include_directories(genesis_engine PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_engine PUBLIC genesis_renderer fastgltf::fastgltf nlohmann_json::nlohmann_json)

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
if(GENESIS_BUILD_EXAMPLES)
    add_executable(genesis_triangle examples/triangle/main.cpp)
    target_link_libraries(genesis_triangle PRIVATE genesis_engine)
    add_dependencies(genesis_triangle genesis_shaders)

    add_executable(genesis_camera examples/camera/main.cpp)
    target_link_libraries(genesis_camera PRIVATE genesis_engine)
    add_dependencies(genesis_camera genesis_shaders)

    add_executable(genesis_gltf_viewer examples/gltf_viewer/main.cpp)
    target_link_libraries(genesis_gltf_viewer PRIVATE genesis_engine imguizmo_lib)
    add_dependencies(genesis_gltf_viewer genesis_shaders)

    add_custom_command(TARGET genesis_gltf_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:genesis_gltf_viewer>/assets"
        COMMENT "Copying assets next to genesis_gltf_viewer"
    )

    add_executable(genesis_bridge_host examples/bridge_host/main.cpp)
    target_link_libraries(genesis_bridge_host PRIVATE genesis_engine)
    add_dependencies(genesis_bridge_host genesis_shaders)

    add_custom_command(TARGET genesis_bridge_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:genesis_bridge_host>/assets"
        COMMENT "Copying assets next to genesis_bridge_host"
    )
endif()

# -----------------------------------------------------------------------------
# PIXERA RenderBridge extension
# -----------------------------------------------------------------------------
option(GENESIS_BUILD_RENDERBRIDGE_EXT "Build PIXERA RenderBridge extension DLL (ExtGenesis)" ON)

if(GENESIS_BUILD_RENDERBRIDGE_EXT AND WIN32)
    set(RX_SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/render-bridge-sdk/RenderBridge")
    set(EXT_GENESIS_DIR "${CMAKE_SOURCE_DIR}/extensions/ExtGenesis")

    add_library(ExtGenesis SHARED
        ${EXT_GENESIS_DIR}/src/Extension.cpp
        ${EXT_GENESIS_DIR}/src/Device.cpp
        ${EXT_GENESIS_DIR}/src/Input.cpp
        ${EXT_GENESIS_DIR}/src/GenesisSession.cpp
        ${EXT_GENESIS_DIR}/src/ModulePaths.cpp
        ${EXT_GENESIS_DIR}/src/ResourceLoader.cpp
        ${EXT_GENESIS_DIR}/src/SceneSelectors.cpp
        ${EXT_GENESIS_DIR}/src/SceneParameterCatalog.cpp
        ${EXT_GENESIS_DIR}/src/ActorBindings.cpp
        ${RX_SDK_DIR}/libs/common/string.cpp
    )

    target_include_directories(ExtGenesis PRIVATE
        ${EXT_GENESIS_DIR}/src
        ${RX_SDK_DIR}/include
        ${RX_SDK_DIR}/libs
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
    )

    target_link_libraries(ExtGenesis PRIVATE
        genesis_engine
    )

    target_compile_definitions(ExtGenesis PRIVATE
        GENESIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders/spv"
    )

    add_dependencies(ExtGenesis genesis_shaders)

    set(RX_TEST_DIR "${RX_SDK_DIR}/RX")
    set(RX_DEPLOY_DIR "${RX_TEST_DIR}/modules/ExtGenesis")
    set_target_properties(ExtGenesis PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rxExt/ExtGenesis"
        VS_DEBUGGER_COMMAND "${RX_TEST_DIR}/RX.exe"
        VS_DEBUGGER_WORKING_DIRECTORY "${RX_TEST_DIR}"
        VS_DEBUGGER_COMMAND_ARGUMENTS "-script=${EXT_GENESIS_DIR}/run.lua"
    )

    add_custom_command(TARGET ExtGenesis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RX_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ExtGenesis>"
            "${RX_DEPLOY_DIR}/ExtGenesis.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ExtGenesis>"
            "${RX_TEST_DIR}/modules/ExtGenesis.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${RX_DEPLOY_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:ExtGenesis>/assets"
        COMMENT "Deploying ExtGenesis to RX modules for local testing"
    )
endif()
