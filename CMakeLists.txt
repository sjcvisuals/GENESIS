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
include(cmake/OnnxRuntime.cmake)

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
    shaders/cull_instances.comp
    shaders/finalize_cull.comp
    shaders/fluid_advect.comp
    shaders/fluid_divergence.comp
    shaders/fluid_jacobi.comp
    shaders/fluid_gradient.comp
    shaders/fluid_splat.comp
    shaders/fluid_present.comp
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
)
target_link_libraries(genesis_platform_core PUBLIC
    SDL3::SDL3-static
    spdlog::spdlog
    glm::glm
)
target_compile_definitions(genesis_platform_core PUBLIC
    GENESIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders/spv"
)

add_library(genesis_platform STATIC
    platform/Window.cpp
    platform/Input.cpp
    platform/ImGuiLayer.cpp
    platform/EditorPaths.cpp
    platform/FileDialog.cpp
)
target_include_directories(genesis_platform PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(genesis_platform PUBLIC
    genesis_platform_core
    imgui_lib
    genesis_vulkan
)

# -----------------------------------------------------------------------------
# Renderer — core (offscreen + shared Vulkan) vs windowed (editor / player)
# -----------------------------------------------------------------------------
add_library(genesis_renderer_core STATIC
    renderer/OffscreenRenderer.cpp
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

# Background removal / live video matte (shared by ExtGenesis + Genesis Remover).
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

# Thin matte + Vulkan shared-image stack (Pixera Remover; also used by full runtime).
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
    )
    target_link_libraries(genesis_gltf_viewer PRIVATE genesis_engine genesis_protocol genesis_protocol_ui genesis_audio imguizmo_lib)
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

    # Standalone webcam + RVM cutout (isolates matte from PIXERA I/O).
    add_executable(genesis_remover_camera_debug examples/remover_camera_debug/main.cpp)
    target_link_libraries(genesis_remover_camera_debug PRIVATE genesis_matting)
    if(GENESIS_HAS_ONNXRUNTIME AND UNIX)
        set_target_properties(genesis_remover_camera_debug PROPERTIES
            BUILD_RPATH "${GENESIS_ONNXRUNTIME_ROOT}/lib"
            INSTALL_RPATH "${GENESIS_ONNXRUNTIME_ROOT}/lib")
        add_custom_command(TARGET genesis_remover_camera_debug POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GENESIS_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.1"
                "$<TARGET_FILE_DIR:genesis_remover_camera_debug>/"
            COMMENT "Copying ONNX Runtime next to genesis_remover_camera_debug")
    endif()
    if(GENESIS_HAS_ONNXRUNTIME AND WIN32)
        add_custom_command(TARGET genesis_remover_camera_debug POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:genesis_remover_camera_debug>/onnxruntime.dll"
            COMMENT "Copying onnxruntime.dll next to genesis_remover_camera_debug")
        if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/lib/DirectML.dll")
            add_custom_command(TARGET genesis_remover_camera_debug POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GENESIS_ONNXRUNTIME_ROOT}/lib/DirectML.dll"
                    "$<TARGET_FILE_DIR:genesis_remover_camera_debug>/DirectML.dll"
                COMMENT "Copying DirectML.dll next to genesis_remover_camera_debug")
        endif()
    endif()
    add_custom_command(TARGET genesis_remover_camera_debug POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:genesis_remover_camera_debug>/assets/matting"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/matting"
            "$<TARGET_FILE_DIR:genesis_remover_camera_debug>/assets/matting"
        COMMENT "Copying matting assets next to genesis_remover_camera_debug")

    # Header-only particle-module authoring smoke (no Vulkan / GPU).
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
# PIXERA Genesis Remover — matte-only fork (same RemoverPipeline)
# -----------------------------------------------------------------------------
option(GENESIS_BUILD_REMOVER_EXT "Build PIXERA Genesis Remover DLL (ExtGenesisRemover)" ON)
if(GENESIS_BUILD_REMOVER_EXT AND WIN32)
    set(RX_SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/render-bridge-sdk/RenderBridge")
    set(EXT_REMOVER_DIR "${CMAKE_SOURCE_DIR}/extensions/ExtGenesisRemover")

    add_library(ExtGenesisRemover SHARED
        ${EXT_REMOVER_DIR}/src/Extension.cpp
        ${EXT_REMOVER_DIR}/src/Device.cpp
        ${EXT_REMOVER_DIR}/src/AdapterGpu.cpp
        ${EXT_REMOVER_DIR}/src/Input.cpp
        ${EXT_REMOVER_DIR}/src/D3d11SharedTexture.cpp
        ${EXT_REMOVER_DIR}/src/D3d11Passthrough.cpp
        ${EXT_REMOVER_DIR}/src/SharedD3d11.cpp
        ${EXT_REMOVER_DIR}/src/MatteHostClient.cpp
        ${EXT_REMOVER_DIR}/src/LiveDelayRing.cpp
        ${EXT_REMOVER_DIR}/src/ModulePaths.cpp
        ${EXT_REMOVER_DIR}/src/RemoverLog.cpp
        ${RX_SDK_DIR}/libs/common/string.cpp
        ${EXT_REMOVER_DIR}/ExtGenesisRemover.def
    )
    target_include_directories(ExtGenesisRemover PRIVATE
        ${EXT_REMOVER_DIR}/src
        ${RX_SDK_DIR}/include
        ${RX_SDK_DIR}/libs
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
    )
    # v44: D3D11 live blit in PIXERA; ORT runs in GenesisRemoverMatteHost.exe (not linked here).
    target_link_libraries(ExtGenesisRemover PRIVATE genesis_platform_core d3d11 dxgi d3dcompiler)
    target_compile_definitions(ExtGenesisRemover PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(ExtGenesisRemover PRIVATE
            "/FI${EXT_REMOVER_DIR}/src/WinPrefix.h")
    endif()
    set_target_properties(ExtGenesisRemover PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rxExt/ExtGenesisRemover"
    )

    add_executable(GenesisRemoverMatteHost
        ${EXT_REMOVER_DIR}/matte_host/main.cpp
    )
    target_include_directories(GenesisRemoverMatteHost PRIVATE
        ${EXT_REMOVER_DIR}/src
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
    )
    target_link_libraries(GenesisRemoverMatteHost PRIVATE genesis_matting d3d11 dxgi)
    target_compile_definitions(GenesisRemoverMatteHost PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(GenesisRemoverMatteHost PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rxExt/ExtGenesisRemover"
    )
    if(MSVC)
        # No console window (blank cmd flash). Entry stays main().
        target_link_options(GenesisRemoverMatteHost PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
    if(MSVC AND GENESIS_HAS_ONNXRUNTIME)
        target_link_options(GenesisRemoverMatteHost PRIVATE "/DELAYLOAD:onnxruntime.dll")
        target_link_libraries(GenesisRemoverMatteHost PRIVATE delayimp)
    endif()

    add_custom_command(TARGET ExtGenesisRemover POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:ExtGenesisRemover>/assets/matting"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/matting"
            "$<TARGET_FILE_DIR:ExtGenesisRemover>/assets/matting"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/deploy/GenesisRemover.manifest.json"
            "$<TARGET_FILE_DIR:ExtGenesisRemover>/GenesisRemover.manifest.json"
        COMMENT "Copying ExtGenesisRemover assets next to DLL"
    )
    add_dependencies(ExtGenesisRemover GenesisRemoverMatteHost)
    if(GENESIS_HAS_ONNXRUNTIME)
        add_custom_command(TARGET GenesisRemoverMatteHost POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:GenesisRemoverMatteHost>/onnxruntime.dll"
            COMMENT "Copying ONNX Runtime next to MatteHost"
        )
        if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/lib/DirectML.dll")
            add_custom_command(TARGET GenesisRemoverMatteHost POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GENESIS_ONNXRUNTIME_ROOT}/lib/DirectML.dll"
                    "$<TARGET_FILE_DIR:GenesisRemoverMatteHost>/DirectML.dll"
                COMMENT "Copying DirectML.dll next to MatteHost"
            )
        endif()
    endif()

    add_custom_target(GenesisRemoverDeploy DEPENDS ExtGenesisRemover GenesisRemoverMatteHost)
    if(TARGET GenesisDeploy)
        add_dependencies(GenesisDeploy ExtGenesisRemover GenesisRemoverMatteHost)
    endif()
endif()

# -----------------------------------------------------------------------------
# Windows installer (Inno Setup — run scripts/package-installer.ps1)
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
