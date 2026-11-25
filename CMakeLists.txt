cmake_minimum_required(VERSION 3.24)
project(GENESIS VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GENESIS_BUILD_EXAMPLES "Build example applications" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/VulkanSetup.cmake)
include(cmake/Dependencies.cmake)
include(cmake/CompileShaders.cmake)
include(cmake/OnnxRuntime.cmake)
include(cmake/WinSparkle.cmake)

configure_file(
    "${CMAKE_SOURCE_DIR}/platform/Version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/platform/Version.hpp"
    @ONLY
)
if(WIN32)
    configure_file(
        "${CMAKE_SOURCE_DIR}/platform/genesis_version.rc.in"
        "${CMAKE_BINARY_DIR}/generated/platform/genesis_version.rc"
        @ONLY
    )
endif()

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
    shaders/mesh_depth.frag
    shaders/grid.vert
    shaders/grid.frag
    shaders/selection_outline.vert
    shaders/selection_outline.frag
    shaders/debug_hud.vert
    shaders/debug_hud.frag
    shaders/cull_instances.comp
    shaders/finalize_cull.comp
    shaders/fluid_advect.comp
    shaders/fluid_divergence.comp
    shaders/fluid_jacobi.comp
    shaders/fluid_gradient.comp
    shaders/fluid_splat.comp
    shaders/fluid_present.comp
    shaders/particle_update.comp
)

# -----------------------------------------------------------------------------
# Platform
# -----------------------------------------------------------------------------
add_library(genesis_platform_core STATIC
    platform/Timer.cpp
    platform/Paths.cpp
)
target_include_directories(genesis_platform_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(genesis_platform_core PUBLIC
    SDL3::SDL3-static
    spdlog::spdlog
    glm::glm
)
target_compile_definitions(genesis_platform_core PUBLIC
    GENESIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders/spv"
    GENESIS_HAS_WINSPARKLE=$<BOOL:${GENESIS_HAS_WINSPARKLE}>
)

add_library(genesis_platform STATIC
    platform/Window.cpp
    platform/Input.cpp
    platform/ImGuiLayer.cpp
    platform/EditorPaths.cpp
    platform/FileDialog.cpp
    platform/AutoUpdate.cpp
)
target_include_directories(genesis_platform PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(genesis_platform PUBLIC
    genesis_platform_core
    imgui_lib
    genesis_vulkan
)
if(GENESIS_HAS_WINSPARKLE)
    target_link_libraries(genesis_platform PUBLIC winsparkle)
endif()

# -----------------------------------------------------------------------------
# Renderer — core (offscreen + shared Vulkan) vs windowed (editor / player)
# -----------------------------------------------------------------------------
add_library(genesis_renderer_core STATIC
    renderer/OffscreenRenderer.cpp
    renderer/DebugHudOverlay.cpp
    renderer/MeshBatchDrawer.cpp
    renderer/BindlessTextures.cpp
    renderer/DisplayLutBinding.cpp
    renderer/MaterialBinding.cpp
    renderer/vulkan/ComputePipeline.cpp
    renderer/vulkan/VulkanContext.cpp
    renderer/vulkan/Buffer.cpp
    renderer/vulkan/Pipeline.cpp
    renderer/vulkan/ShaderModule.cpp
    renderer/vulkan/Texture.cpp
    renderer/vulkan/ExternalRenderTarget.cpp
    renderer/vulkan/ExternalSharedImage.cpp
    renderer/vulkan/ExternalSync.cpp
    renderer/vulkan/VulkanInterop.cpp
)
target_include_directories(genesis_renderer_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(genesis_renderer_core PUBLIC
    genesis_platform_core
    genesis_vulkan
)
add_dependencies(genesis_renderer_core genesis_shaders)
target_sources(genesis_renderer_core PRIVATE ${GENESIS_SHADER_SPV})
set_source_files_properties(${GENESIS_SHADER_SPV} PROPERTIES GENERATED TRUE)
target_compile_definitions(genesis_renderer_core PUBLIC
    GENESIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders/spv"
)

add_library(genesis_renderer_windowed STATIC
    renderer/Renderer.cpp
    renderer/vulkan/Swapchain.cpp
)
target_include_directories(genesis_renderer_windowed PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(genesis_renderer_windowed PUBLIC genesis_renderer_core)
target_link_libraries(genesis_renderer_windowed PRIVATE genesis_platform)

add_library(genesis_renderer ALIAS genesis_renderer_windowed)

# -----------------------------------------------------------------------------
# Engine (runtime core — shared by editor, player, and RenderBridge extension)
# -----------------------------------------------------------------------------
add_library(genesis_engine_runtime STATIC
    scene/Transform.cpp
    scene/Camera.cpp
    scene/Scene.cpp
    scene/SceneCamera.cpp
    scene/Lighting.cpp
    scene/ScenePath.cpp
    scene/SceneSerializer.cpp
    scene/GnsPackage.cpp
    scene/SceneRuntime.cpp
    scene/ParamBinding.cpp
    protocol/OscParamLabels.cpp
    color/ColorManagement.cpp
    animation/AnimationClip.cpp
    animation/AnimationAuthoring.cpp
    animation/AnimationGraph.cpp
    animation/AnimationEvaluator.cpp
    particles/ParticleWorld.cpp
    particles/ParticleGpuSim.cpp
    particles/FluidGpuGrid.cpp
    particles/FluidWorld.cpp
    assets/GltfLoader.cpp
    assets/GltfTextures.cpp
    assets/TextureLoader.cpp
    assets/MeshUpload.cpp
    assets/PrimitiveMeshes.cpp
    assets/GridMesh.cpp
    assets/ModelImporter.cpp
)
target_include_directories(genesis_engine_runtime PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_engine_runtime PUBLIC
    genesis_renderer_core
    fastgltf::fastgltf
    nlohmann_json::nlohmann_json
    assimp::assimp
)

# Background removal / live video matte (ExtGenesis Background Remover mode).
find_package(Threads REQUIRED)
add_library(genesis_matting STATIC
    matting/BgRemover.cpp
    matting/RvmOnnxSession.cpp
    matting/AsyncMatteWorker.cpp
)
target_include_directories(genesis_matting PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_matting PUBLIC genesis_platform_core Threads::Threads)
if(GENESIS_HAS_ONNXRUNTIME)
    target_compile_definitions(genesis_matting PUBLIC GENESIS_HAS_ONNXRUNTIME=1)
    if(GENESIS_HAS_ORT_DIRECTML)
        target_compile_definitions(genesis_matting PUBLIC GENESIS_HAS_ORT_DIRECTML=1)
    endif()
    target_include_directories(genesis_matting PUBLIC "${GENESIS_ONNXRUNTIME_ROOT}/include")
    if(WIN32)
        target_link_libraries(genesis_matting PUBLIC "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    else()
        target_link_libraries(genesis_matting PUBLIC "${GENESIS_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
        set_target_properties(genesis_matting PROPERTIES
            BUILD_RPATH "${GENESIS_ONNXRUNTIME_ROOT}/lib"
            INSTALL_RPATH "${GENESIS_ONNXRUNTIME_ROOT}/lib")
    endif()
endif()

# Thin matte + Vulkan shared-image stack (ExtGenesis Background Remover + runtime).
add_library(genesis_remover_core STATIC
    runtime/RemoverPipeline.cpp
)
target_include_directories(genesis_remover_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_remover_core PUBLIC
    genesis_matting
    genesis_renderer_core
)

add_library(genesis_runtime STATIC
    runtime/GenesisSession.cpp
    runtime/GenesisFrameTime.cpp
    runtime/HostTimingAdapter.cpp
    runtime/PerfDebug.cpp
    runtime/ScenePlaybackPipeline.cpp
    runtime/SceneStateHash.cpp
    runtime/DeterministicAnimation.cpp
    runtime/RuntimePaths.cpp
    runtime/RuntimeScene.cpp
    runtime/ProductionTopology.cpp
    runtime/NodeAssignment.cpp
    runtime/MultiViewSession.cpp
)
target_include_directories(genesis_runtime PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_runtime PUBLIC
    genesis_engine_runtime
    genesis_remover_core
)

add_library(genesis_engine STATIC
    engine/Application.cpp
    host/HostBridge.cpp
    host/HostFrameTiming.cpp
    host/MultiViewHostSession.cpp
    editor/PerfDebugUi.cpp
)
target_include_directories(genesis_engine PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_engine PUBLIC
    genesis_engine_runtime
    genesis_runtime
    genesis_platform
    genesis_renderer_windowed
)

# Editor protocol input (OSC) + audio-reactive analysis — editor-facing for now.
add_library(genesis_audio STATIC
    audio/AudioAnalyser.cpp
    audio/AudioCapture.cpp
    audio/AudioReactive.cpp
    editor/AudioReactiveUi.cpp
)
target_include_directories(genesis_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_audio PUBLIC genesis_engine_runtime genesis_platform imgui_lib)

# OSC receive + particle emit (no ImGui — safe for ExtGenesis / headless).
add_library(genesis_protocol STATIC
    protocol/OscParser.cpp
    protocol/OscUdpReceiver.cpp
    protocol/OscProtocol.cpp
    protocol/OscParticleEmit.cpp
    protocol/OscRoomPerson.cpp
)
target_include_directories(genesis_protocol PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_protocol PUBLIC genesis_runtime)
if(WIN32)
    target_link_libraries(genesis_protocol PRIVATE ws2_32)
endif()

add_library(genesis_protocol_ui STATIC
    editor/OscProtocolUi.cpp
)
target_include_directories(genesis_protocol_ui PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(genesis_protocol_ui PUBLIC genesis_protocol imgui_lib)

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

    add_executable(genesis_gltf_viewer
        examples/gltf_viewer/main.cpp
        editor/EditorSession.cpp
        editor/EditorTheme.cpp
        editor/ParticleEmitterUi.cpp
        editor/TugelaEditor.cpp
    )
    if(WIN32)
        target_sources(genesis_gltf_viewer PRIVATE
            "${CMAKE_BINARY_DIR}/generated/platform/genesis_version.rc"
        )
    endif()
    target_link_libraries(genesis_gltf_viewer PRIVATE genesis_engine genesis_protocol genesis_protocol_ui genesis_audio imguizmo_lib)
    add_dependencies(genesis_gltf_viewer genesis_shaders)

    add_custom_command(TARGET genesis_gltf_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:genesis_gltf_viewer>/assets"
        COMMENT "Copying assets next to genesis_gltf_viewer"
    )
    if(GENESIS_HAS_WINSPARKLE AND DEFINED GENESIS_WINSPARKLE_DLL)
        add_custom_command(TARGET genesis_gltf_viewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GENESIS_WINSPARKLE_DLL}"
                "$<TARGET_FILE_DIR:genesis_gltf_viewer>/WinSparkle.dll"
            COMMENT "Copying WinSparkle.dll next to genesis_gltf_viewer"
        )
    endif()

    add_executable(genesis_bridge_host examples/bridge_host/main.cpp)
    target_link_libraries(genesis_bridge_host PRIVATE genesis_engine)
    add_dependencies(genesis_bridge_host genesis_shaders)

    add_executable(genesis_player examples/player/main.cpp)
    target_link_libraries(genesis_player PRIVATE genesis_engine)
    add_dependencies(genesis_player genesis_shaders)

    add_executable(genesis_topology_validate examples/topology_validate/main.cpp)
    target_link_libraries(genesis_topology_validate PRIVATE genesis_runtime)

    add_executable(genesis_determinism_harness examples/determinism_harness/main.cpp)
    target_link_libraries(genesis_determinism_harness PRIVATE genesis_runtime nlohmann_json::nlohmann_json)

    add_executable(genesis_osc_smoke examples/osc_smoke/main.cpp)
    target_link_libraries(genesis_osc_smoke PRIVATE genesis_protocol)

    add_executable(genesis_audio_smoke examples/audio_smoke/main.cpp)
    target_link_libraries(genesis_audio_smoke PRIVATE genesis_audio)

    add_executable(genesis_matting_smoke examples/matting_smoke/main.cpp)
    target_link_libraries(genesis_matting_smoke PRIVATE genesis_matting)
    if(GENESIS_HAS_ONNXRUNTIME AND UNIX)
        set_target_properties(genesis_matting_smoke PROPERTIES
            BUILD_RPATH "${GENESIS_ONNXRUNTIME_ROOT}/lib"
            INSTALL_RPATH "${GENESIS_ONNXRUNTIME_ROOT}/lib")
        add_custom_command(TARGET genesis_matting_smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GENESIS_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.1"
                "$<TARGET_FILE_DIR:genesis_matting_smoke>/"
            COMMENT "Copying ONNX Runtime next to genesis_matting_smoke")
    endif()
    add_custom_command(TARGET genesis_matting_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:genesis_matting_smoke>/assets/matting"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/matting"
            "$<TARGET_FILE_DIR:genesis_matting_smoke>/assets/matting"
        COMMENT "Copying matting assets next to genesis_matting_smoke")

    # Header-only Tugela-module authoring smoke (no Vulkan / GPU).
    add_executable(genesis_particle_smoke examples/particle_smoke/main.cpp)
    target_include_directories(genesis_particle_smoke PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(genesis_particle_smoke PRIVATE glm::glm)

    # CPU ParticleWorld determinism + sprite batch smoke (links engine runtime).
    add_executable(genesis_particle_world_smoke examples/particle_smoke/world_main.cpp)
    target_link_libraries(genesis_particle_world_smoke PRIVATE genesis_engine_runtime)

    add_executable(genesis_multiview_player examples/multiview_player/main.cpp)
    target_link_libraries(genesis_multiview_player PRIVATE genesis_engine)
    add_dependencies(genesis_multiview_player genesis_shaders)

    add_custom_command(TARGET genesis_multiview_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:genesis_multiview_player>/assets"
        COMMENT "Copying assets next to genesis_multiview_player"
    )

    add_custom_command(TARGET genesis_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:genesis_player>/assets"
        COMMENT "Copying assets next to genesis_player"
    )

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
        genesis_runtime
        genesis_protocol
    )
    if(MSVC AND GENESIS_HAS_ONNXRUNTIME)
        target_link_options(ExtGenesis PRIVATE "/DELAYLOAD:onnxruntime.dll")
        target_link_libraries(ExtGenesis PRIVATE delayimp)
    endif()

    target_compile_definitions(ExtGenesis PRIVATE
        GENESIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders/spv"
    )

    add_dependencies(ExtGenesis genesis_shaders)

    set(RX_TEST_DIR "${RX_SDK_DIR}/RX")
    set_target_properties(ExtGenesis PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rxExt/ExtGenesis"
        VS_DEBUGGER_COMMAND "${RX_TEST_DIR}/RX.exe"
        VS_DEBUGGER_WORKING_DIRECTORY "${RX_TEST_DIR}"
        VS_DEBUGGER_COMMAND_ARGUMENTS "-script=${EXT_GENESIS_DIR}/run.lua"
    )

    # Keep assets next to the built DLL for MSVC debugging. Day-to-day WIP
    # packages live under repo out/ (filled by scripts/build.ps1).
    add_custom_command(TARGET ExtGenesis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:ExtGenesis>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/scenes"
            "$<TARGET_FILE_DIR:ExtGenesis>/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/deploy/Genesis.manifest.json"
            "$<TARGET_FILE_DIR:ExtGenesis>/Genesis.manifest.json"
        COMMENT "Copying ExtGenesis assets next to DLL"
    )
    if(GENESIS_HAS_ONNXRUNTIME)
        add_custom_command(TARGET ExtGenesis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:ExtGenesis>/onnxruntime.dll"
            COMMENT "Copying ONNX Runtime next to ExtGenesis"
        )
        if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/lib/DirectML.dll")
            add_custom_command(TARGET ExtGenesis POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GENESIS_ONNXRUNTIME_ROOT}/lib/DirectML.dll"
                    "$<TARGET_FILE_DIR:ExtGenesis>/DirectML.dll"
                COMMENT "Copying DirectML.dll next to ExtGenesis"
            )
        endif()
    endif()

    add_custom_target(GenesisDeploy DEPENDS ExtGenesis)
endif()

# -----------------------------------------------------------------------------
# Windows installer (Inno Setup wizard — run scripts/package-installer.ps1)
# Primary artifact: GenesisSetup-<version>.exe  (see docs/install-windows.md)
# -----------------------------------------------------------------------------
if(WIN32)
    add_custom_target(GenesisInstaller
        COMMAND powershell -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/package-installer.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Stage Release build and compile GenesisSetup.exe (requires Inno Setup 6)"
    )
endif()
