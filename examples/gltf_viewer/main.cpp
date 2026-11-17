#include "assets/GridMesh.hpp"
#include "assets/ModelImporter.hpp"
#include "assets/PrimitiveMeshes.hpp"
#include "assets/TextureLoader.hpp"
#include "animation/AnimationAuthoring.hpp"
#include "animation/AnimationEvaluator.hpp"
#include "color/ColorManagement.hpp"
#include "editor/EditorLayout.hpp"
#include "editor/EditorSession.hpp"
#include "editor/OscProtocolUi.hpp"
#include "editor/AudioReactiveUi.hpp"
#include "editor/EditorTheme.hpp"
#include "editor/ParticleEmitterUi.hpp"
#include "editor/SceneViewport.hpp"
#include "engine/Application.hpp"
#include "platform/EditorPaths.hpp"
#include "platform/FileDialog.hpp"
#include "platform/Paths.hpp"
#include "renderer/GridDrawPushConstants.hpp"
#include "scene/Camera.hpp"
#include "scene/Lighting.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneCamera.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include "scene/DefaultMaterial.hpp"
#include "scene/SceneDrawBatch.hpp"
#include "scene/ScenePicker.hpp"
#include "scene/ScenePath.hpp"
#include "particles/ParticleEmitter.hpp"
#include "protocol/OscProtocol.hpp"
#include "audio/AudioReactive.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Room-scale meter grid (~6 m install footprint + margin).
constexpr float kGridHalfExtentM = 24.0f;
constexpr int kGridDivisions = 48;

struct ExampleLoadOptions {
    bool auto_play_animation = false;
    bool import_fbx = false;
    bool wire_audio_reactive = false;
};

class GltfViewerApp final : public genesis::Application {
public:
    explicit GltfViewerApp(const genesis::ApplicationConfig& config)
        : genesis::Application(config), runtime_(renderer().context()) {
        setRenderStatsEnabled(true);
        genesis::ensureEditorDataDirs();
        session_.loadRecentList();
        // Black unlit stage — only self-illuminated particles read as glow (prefs).
        renderer().setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        color_mgmt_.settings().exposure = 1.15f;
        color_mgmt_.settings().bloom_strength = 0.04f; // not bloom-as-blur; additive sprites carry glow
        color_mgmt_.settings().grain_strength = 0.0f;
        color_mgmt_.settings().vignette_strength = 0.0f;
        show_grid_ = true;

        grid_mesh_ = genesis::createEditorGridMesh(renderer().context(), kGridHalfExtentM, kGridDivisions);
        if (color_mgmt_.lutDirty()) {
            renderer().uploadDisplayLut(color_mgmt_.rebuildDisplayLut());
        }
    }

    ~GltfViewerApp() override {
        // Release scene GPU objects before Application destroys the Vulkan device.
        draw_commands_.clear();
        draw_batches_.clear();
        grid_mesh_ = {};
        runtime_.clear();
        if (renderer().context().device() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(renderer().context().device());
        }
    }

    void load(const std::filesystem::path& path) {
        runtime_.loadGltf(path);
        session_.document_path = std::filesystem::absolute(path);
        session_.dirty = true;
        selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        use_editor_output_ = true;
        runtime_.scene().active_output_camera.reset();
        resetEditorCamera();
        updateWindowTitle();
    }

    void loadDefaultScene() {
        newBlankScene();
        session_.dirty = false;
        updateWindowTitle();
    }

    void enableSmokeExit(int frames) {
        smoke_exit_frames_ = frames;
        exit_confirmed_ = true;
        session_.dirty = false;
    }

    void loadSceneDocument(const std::filesystem::path& path) {
        draw_commands_.clear();
        draw_batches_.clear();
        selection_.clear();
        session_.loadDocument(runtime_, path);
        selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        syncEditorAfterDocumentLoad();
        updateWindowTitle();
    }

    void loadExampleScene(const char* bundled_relative) {
        loadExampleScene(bundled_relative, ExampleLoadOptions{});
    }

    void loadExampleScene(const char* bundled_relative, ExampleLoadOptions options) {
        try {
            loadSceneDocument(session_.bundledScenePath(bundled_relative));
            session_.dirty = false;

            if (options.import_fbx) {
                const std::filesystem::path fbx =
                    genesis::resolveAssetPath("assets/models/examples/spinning_cube.fbx");
                const genesis::ModelImportResult result = genesis::ModelImporter::importInto(runtime_, fbx);
                if (!result.ok) {
                    spdlog::error("Example FBX import failed: {}", result.error);
                } else {
                    // ASCII FBX is authored in centimeters. Scale the import folder and
                    // focus a mesh node (not the empty RootNode).
                    genesis::NodeId focus = result.root_node;
                    for (genesis::Node& node : runtime_.scene().nodes) {
                        if (node.name == "spinning_cube") {
                            node.local.scale = glm::vec3(0.01f);
                        }
                        if (node.has_mesh && (node.name == "AnimatedCube" || focus == result.root_node)) {
                            focus = node.id;
                        }
                    }
                    runtime_.updateTransforms();
                    selectNode(focus != 0 ? focus : result.root_node);
                    focusEditorCameraOnSelection();
                    show_timeline_ = true;
                    show_animation_graph_ = true;
                    spdlog::info("Example FBX imported ({} meshes, {} clips)", result.mesh_ids.size(),
                                 result.clip_indices.size());
                }
            }

            if (options.auto_play_animation) {
                genesis::AnimationPlayback& playback = runtime_.animationPlayback();
                if (playback.clip_index < 0 && !runtime_.animationClips().empty()) {
                    playback.clip_index = 0;
                }
                if (playback.clip_index >= 0) {
                    playback.playing = true;
                    playback.loop = true;
                    playback.time_seconds = 0.0f;
                    show_timeline_ = true;
                }
            }

            if (options.wire_audio_reactive) {
                audio_reactive_.clearBindings();
                audio_reactive_.addLavaLampBassPreset("Scene/Lava Lamp");
                audio_reactive_.settings().enabled = true;
                show_audio_ = true;
                spdlog::info(
                    "Audio-Reactive Lava: bass/beat/onset wired to Scene/Lava Lamp. "
                    "Open View → Audio and Start live capture (or Open WAV).");
            }

            updateWindowTitle();
        } catch (const std::exception& ex) {
            spdlog::error("Failed to load example '{}': {}", bundled_relative, ex.what());
        }
    }

    void loadGnsPackage(const std::filesystem::path& path) {
        loadSceneDocument(path);
    }

protected:
    void maybeRunEdgeDemoHook() {
        const char* mode = std::getenv("GENESIS_EDGE_DEMO");
        if (!mode || mode[0] == '\0') {
            return;
        }
        ++edge_demo_frames_;
        if (edge_demo_frames_ != 45) {
            return;
        }
        const std::string m(mode);
        if (m == "mesh") {
            game_view_mode_ = false;
            show_actor_icons_ = true;
            addPrimitiveMesh("Cube", genesis::createCubeMesh(renderer().context()));
            if (genesis::Node* node = runtime_.scene().find(primarySelected())) {
                if (node->material_resource.has_value()) {
                    if (genesis::SceneMaterial* mat = runtime_.material(*node->material_resource)) {
                        // Mid-gray body so the thin white silhouette rim contrasts.
                        mat->base_color = glm::vec4(0.28f, 0.30f, 0.34f, 1.0f);
                        mat->emissive = glm::vec3(0.12f, 0.13f, 0.15f);
                        mat->emissive_strength = 1.0f;
                        mat->roughness = 0.65f;
                        mat->metallic = 0.0f;
                    }
                }
                node->local.position = glm::vec3(0.0f, 0.55f, 0.0f);
                node->local.scale = glm::vec3(0.9f);
            }
            runtime_.updateTransforms();
            focusEditorCameraOnSelection();
            // Pull camera out so the white silhouette rim reads clearly.
            placeEditorCameraAt(glm::vec3(2.4f, 1.6f, 2.8f), glm::vec3(0.0f, 0.55f, 0.0f));
        } else if (m == "emitter") {
            game_view_mode_ = false;
            show_actor_icons_ = true;
            std::optional<genesis::NodeId> id =
                genesis::findNodeByPath(runtime_.scene(), "Scene/Particle System/Soft Circles");
            if (!id) {
                for (const genesis::Node& n : runtime_.scene().nodes) {
                    if (n.particle_emitter.has_value()) {
                        id = n.id;
                        break;
                    }
                }
            }
            if (id) {
                selectNode(*id);
                focusEditorCameraOnSelection();
                placeEditorCameraAt(glm::vec3(1.8f, 1.4f, 2.2f), glm::vec3(0.0f, 0.2f, 0.0f));
            }
        } else if (m == "game") {
            show_actor_icons_ = true;
            game_view_mode_ = true;
            std::optional<genesis::NodeId> id =
                genesis::findNodeByPath(runtime_.scene(), "Scene/Particle System/Soft Circles");
            if (id) {
                selectNode(*id);
            }
            placeEditorCameraAt(glm::vec3(1.8f, 1.4f, 2.2f), glm::vec3(0.0f, 0.2f, 0.0f));
        }
    }

    void onUpdate(float dt) override {
        maybeRunEdgeDemoHook();
        if (smoke_exit_frames_ >= 0) {
            if (--smoke_exit_frames_ < 0) {
                exit_confirmed_ = true;
                session_.dirty = false;
                window().setShouldClose(true);
            }
        }

        processPendingDeletes();
        runtime_.tickAnimations(dt);
        osc_time_ += static_cast<double>(dt);
        osc_protocol_.poll(osc_time_);
        osc_protocol_.applyBindings(runtime_);
        osc_protocol_.applyParticleEmits(runtime_, osc_particle_cursor_);
        osc_protocol_.applyRoomPerson(runtime_, osc_room_person_cursor_);
        audio_reactive_.tick(runtime_, dt);
        runtime_.updateTransforms();
        // Allow catch-up on slow software GPUs so gentle rise/trails stay readable.
        runtime_.tickParticles(dt, 0.01f, 24);
        last_frame_dt_ = dt;
        handleEditorShortcuts();

        if (window().shouldClose() && session_.dirty && !exit_confirmed_) {
            window().setShouldClose(false);
            show_exit_confirm_ = true;
        }

        updateEditorCameraNavigation(dt);

        const float aspect = refreshOutputAspect();
        const glm::mat4 vp = outputViewProjection(renderAspect());
        genesis::FrameUniforms frame =
            genesis::buildFrameUniforms(runtime_.scene(), &vp[0][0], outputCameraPosition(),
                                        &color_mgmt_.settings(), look_time_seconds_);
        // Particle look demos: pure black unlit stage (no ambient wash / fog fill).
        frame.ibl_intensity = 0.0f;
        frame.ambient_sky[0] = 0.0f;
        frame.ambient_sky[1] = 0.0f;
        frame.ambient_sky[2] = 0.0f;
        frame.ambient_ground[0] = 0.0f;
        frame.ambient_ground[1] = 0.0f;
        frame.ambient_ground[2] = 0.0f;
        frame.fog_density = 0.0f;
        frame.fog_color[0] = 0.0f;
        frame.fog_color[1] = 0.0f;
        frame.fog_color[2] = 0.0f;
        renderer().setFrameUniforms(frame);
        look_time_seconds_ += last_frame_dt_;
    }

    void onDraw() override {
        runtime_.updateTransforms();
        if (show_viewport_ && viewport_.w > 1.0f && viewport_.h > 1.0f) {
            renderer().setViewportDrawRegion(viewport_.x, viewport_.y, viewport_.w, viewport_.h);
        } else {
            renderer().clearViewportDrawRegion();
        }

        draw_commands_.clear();
        genesis::SceneRenderStats render_stats{};
        const glm::mat4 vp = outputViewProjection(renderAspect());
        genesis::SceneDrawCollectParams collect_params{};
        collect_params.view_proj_col_major = &vp[0][0];
        collect_params.enable_cpu_culling = true;
        collect_params.enable_gpu_culling = false;
        runtime_.collectDrawCommands(draw_commands_, &render_stats, collect_params);
        {
            // Niagara-style sprite facing from the active view camera.
            const glm::mat4 view = outputViewMatrix();
            const glm::mat3 inv = glm::inverse(glm::mat3(view));
            runtime_.setParticleViewBasis(glm::normalize(glm::vec3(inv[0])),
                                         glm::normalize(glm::vec3(inv[1])));
        }
        runtime_.appendParticleDrawCommands(draw_commands_);
        render_stats.draw_calls = static_cast<std::uint32_t>(draw_commands_.size());
        // Recount triangles roughly including particles.
        render_stats.triangles = 0;
        for (const genesis::SceneDrawCommand& cmd : draw_commands_) {
            if (cmd.mesh) {
                render_stats.triangles += cmd.mesh->index_count / 3;
            }
        }

        genesis::FrameStats& stats = frameStats();
        stats.draw_calls = render_stats.draw_calls;
        stats.triangles = render_stats.triangles;
        stats.visible_mesh_nodes = render_stats.visible_mesh_nodes;
        stats.scene_nodes = render_stats.scene_nodes;
        stats.active_lights = render_stats.active_lights;

        genesis::MeshDrawOptions draw_options{};
        draw_options.view_proj_col_major = &vp[0][0];
        draw_options.enable_gpu_cull = false;
        draw_options.enable_depth_prepass = true;

        if (!draw_commands_.empty()) {
            draw_batches_.clear();
            genesis::buildDrawBatches(draw_commands_, draw_batches_, &render_stats);
            renderer().drawMeshBatches(draw_batches_, draw_options);
            last_draw_batches_ = draw_batches_.size();
        } else {
            last_draw_batches_ = 0;
        }

        if (show_grid_) {
            // Cool readable lines on black unlit stage (1 m major / 0.25 m minor).
            const genesis::GridDrawPushConstants grid_pc =
                genesis::makeGridDrawPushConstants(nullptr, 0.38f, 0.48f, 0.52f, 0.72f);
            renderer().drawGrid(grid_mesh_, grid_pc);
        }

        drawSelectionEdgeOutlines(draw_options, &vp[0][0]);

        last_culled_nodes_ = render_stats.culled_mesh_nodes;
    }

    void onImGui() override {
        if (!ui_style_initialized_) {
            initEditorStyle();
            dockspace_id_ = ImGui::GetID("GenesisEditorDockSpace");
            ui_style_initialized_ = true;
        }

        ImGuizmo::BeginFrame();
        genesis::Scene& scene = runtime_.scene();

        genesis::beginEditorDockSpace(dockspace_id_, dock_layout_first_time_);

        drawMenuBar(scene);
        if (show_hierarchy_) drawHierarchyPanel(scene);
        if (show_inspector_) drawInspectorPanel(scene);
        if (show_toolbar_) drawToolbarPanel(scene);
        if (show_viewport_help_) drawViewportHelpPanel();
        if (show_timeline_) drawTimelinePanel();
        if (show_protocol_) drawProtocolPanel();
        if (show_audio_) drawAudioPanel(scene);
        if (show_animation_graph_) drawAnimationGraphPanel();
        if (show_debug_window_) drawDebugWindow();

        drawFileModals(scene);

        if (show_viewport_) {
            ImVec2 rect_min{};
            ImVec2 rect_max{};
            bool viewport_input_active = false;
            genesis::drawViewportInputOverlay(
                dockspace_id_, show_viewport_, rect_min, rect_max, viewport_hovered_,
                viewport_input_active, [&]() { drawViewportGizmo(scene); });
            viewport_.x = rect_min.x;
            viewport_.y = rect_min.y;
            viewport_.w = std::max(rect_max.x - rect_min.x, 1.0f);
            viewport_.h = std::max(rect_max.y - rect_min.y, 1.0f);
            viewport_.hovered = viewport_hovered_;

            handleViewportSelection(scene);
            drawSelectionOverlays(scene);
            drawActorIcons(scene);
        } else {
            viewport_.w = 0.0f;
            viewport_.h = 0.0f;
            viewport_hovered_ = false;
            marquee_ = {};
        }
    }

private:
    void markDirty() {
        session_.dirty = true;
        updateWindowTitle();
    }

    void updateWindowTitle() { window().setTitle(session_.windowTitle()); }

    void handleEditorShortcuts() {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            return;
        }

        const bool ctrl = input().keyDown(SDL_SCANCODE_LCTRL) || input().keyDown(SDL_SCANCODE_RCTRL);
        if (ctrl && input().keyPressed(SDL_SCANCODE_S)) {
            saveCurrentDocument();
        }
        if (ctrl && input().keyPressed(SDL_SCANCODE_O)) {
            requestOpenScene();
        }
        if (ctrl && input().keyPressed(SDL_SCANCODE_N)) {
            requestNewScene();
        }
        if (input().keyPressed(SDL_SCANCODE_F)) {
            focusEditorCameraOnSelection();
        }
        // Unreal-like G: toggle game view (hides actor icons / editor helpers).
        if (input().keyPressed(SDL_SCANCODE_G) && !ctrl) {
            game_view_mode_ = !game_view_mode_;
        }
        // Demo / QA hotkeys for edge-selection screenshots (black-stage visible cube).
        if (input().keyPressed(SDL_SCANCODE_F6)) {
            addPrimitiveMesh("Cube", genesis::createCubeMesh(renderer().context()));
            if (genesis::Node* node = runtime_.scene().find(primarySelected())) {
                if (node->material_resource.has_value()) {
                    if (genesis::SceneMaterial* mat = runtime_.material(*node->material_resource)) {
                        mat->base_color = glm::vec4(0.55f, 0.58f, 0.62f, 1.0f);
                        mat->emissive = glm::vec3(0.35f, 0.37f, 0.40f);
                        mat->emissive_strength = 1.0f;
                        mat->roughness = 0.55f;
                    }
                }
                node->local.position = glm::vec3(0.0f, 0.5f, 0.0f);
                node->local.scale = glm::vec3(0.85f);
            }
            runtime_.updateTransforms();
            focusEditorCameraOnSelection();
        }
        if (input().keyPressed(SDL_SCANCODE_F7)) {
            std::optional<genesis::NodeId> id =
                genesis::findNodeByPath(runtime_.scene(), "Scene/Particle System/Soft Circles");
            if (!id) {
                for (const genesis::Node& n : runtime_.scene().nodes) {
                    if (n.particle_emitter.has_value() &&
                        n.name.find("Soft Circles") != std::string::npos) {
                        id = n.id;
                        break;
                    }
                }
            }
            if (id) {
                selectNode(*id);
                focusEditorCameraOnSelection();
            } else {
                spdlog::warn("F7: Soft Circles emitter not found");
            }
        }
        if (input().keyPressed(SDL_SCANCODE_F8)) {
            clearSelection();
        }
        if (!selection_.empty() && !ImGuizmo::IsUsing()) {
            if (input().keyPressed(SDL_SCANCODE_DELETE)) {
                queueDeleteSelectedNodes();
            }
        }
    }

    void initEditorStyle() {
        genesis::applyGenesisEditorTheme();
    }

    void drawMenuBar(genesis::Scene& scene) {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) requestNewScene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) requestOpenScene();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveCurrentDocument();
            if (ImGui::MenuItem("Save Scene As...")) {
                saveSceneAsDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import Model...")) {
                importModelDialog();
            }
            if (ImGui::BeginMenu("Examples")) {
                if (ImGui::MenuItem("Particle Fountain")) {
                    loadExampleScene("examples/particles_fountain.scene.json");
                }
                if (ImGui::MenuItem("Lava Fluid")) {
                    loadExampleScene("examples/lava_fluid.scene.json");
                }
                if (ImGui::MenuItem("Animated Primitives")) {
                    ExampleLoadOptions opts;
                    opts.auto_play_animation = true;
                    loadExampleScene("examples/animated_primitives.scene.json", opts);
                }
                if (ImGui::MenuItem("Imported FBX Animation")) {
                    ExampleLoadOptions opts;
                    opts.auto_play_animation = true;
                    opts.import_fbx = true;
                    loadExampleScene("examples/fbx_stage.scene.json", opts);
                }
                if (ImGui::MenuItem("Audio-Reactive Lava")) {
                    ExampleLoadOptions opts;
                    opts.wire_audio_reactive = true;
                    loadExampleScene("examples/audio_reactive_lava.scene.json", opts);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (!session_.recent_files.empty() && ImGui::BeginMenu("Open Recent")) {
                for (const std::filesystem::path& recent : session_.recent_files) {
                    if (ImGui::MenuItem(recent.string().c_str())) {
                        try {
                            loadSceneDocument(recent);
                        } catch (const std::exception& ex) {
                            spdlog::error("Open recent failed: {}", ex.what());
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Examples")) {
                if (ImGui::MenuItem("Niagara Soft Circles + OSC")) {
                    const std::filesystem::path example =
                        genesis::resolveAssetPath("assets/scenes/bubble_ring_osc.scene.json");
                    try {
                        loadSceneDocument(example);
                        genesis::OscParticleEmitSettings emit = osc_protocol_.particleEmitSettings();
                        emit.emitter_path = "Scene/Particle System/OSC Tracker";
                        emit.track_emitter_position = true;
                        emit.burst_on_message = false;
                        emit.plane_height = 0.15f;
                        osc_protocol_.setParticleEmitSettings(emit);
                    } catch (const std::exception& ex) {
                        spdlog::error("Open example failed: {}", ex.what());
                    }
                }
                if (ImGui::MenuItem("Room OSC Particles")) {
                    const std::filesystem::path example =
                        genesis::resolveAssetPath("assets/scenes/room_osc_particles.scene.json");
                    try {
                        loadSceneDocument(example);
                        genesis::OscRoomPersonSettings room = osc_protocol_.roomPersonSettings();
                        room.enabled = true;
                        room.address = "/genesis/room/person";
                        room.feet_emitter_path = "Scene/Particle System/Person Feet";
                        room.wall_emitter_path = "Scene/Particle System/Wall Trace";
                        room.ray_emitter_path = "Scene/Particle System/Center Ray";
                        room.room_half_x = 4.0f;
                        room.room_half_z = 4.0f;
                        osc_protocol_.setRoomPersonSettings(room);
                        genesis::OscParticleEmitSettings emit = osc_protocol_.particleEmitSettings();
                        emit.emitter_path = "Scene/Particle System/Person Feet";
                        emit.track_emitter_position = true;
                        emit.burst_on_message = false;
                        osc_protocol_.setParticleEmitSettings(emit);
                        if (!osc_protocol_.isListening()) {
                            osc_protocol_.startListening();
                        }
                    } catch (const std::exception& ex) {
                        spdlog::error("Open room OSC example failed: {}", ex.what());
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (session_.dirty) {
                    show_exit_confirm_ = true;
                } else {
                    window().setShouldClose(true);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Color")) {
            drawColorMenu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_);
            ImGui::MenuItem("Inspector", nullptr, &show_inspector_);
            ImGui::MenuItem("Viewport", nullptr, &show_viewport_);
            ImGui::MenuItem("Toolbar", nullptr, &show_toolbar_);
            ImGui::MenuItem("Shortcuts", nullptr, &show_viewport_help_);
            ImGui::MenuItem("Timeline", nullptr, &show_timeline_);
            ImGui::MenuItem("Protocol", nullptr, &show_protocol_);
            ImGui::MenuItem("Audio", nullptr, &show_audio_);
            ImGui::MenuItem("Animation Graph", nullptr, &show_animation_graph_);
            ImGui::MenuItem("Show Grid", nullptr, &show_grid_);
            ImGui::BeginDisabled(game_view_mode_);
            ImGui::MenuItem("Actor Icons", nullptr, &show_actor_icons_);
            ImGui::EndDisabled();
            ImGui::MenuItem("Game View", "G", &game_view_mode_);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Hide actor icons (Unreal-style game / show-flags).");
            }
            ImGui::Separator();
            ImGui::MenuItem("Performance / Debug", nullptr, &show_debug_window_);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Window Layout")) {
                dock_layout_first_time_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Editor Camera", nullptr, use_editor_output_)) {
                use_editor_output_ = true;
                scene.active_output_camera.reset();
            }
            for (const auto& node : scene.nodes) {
                if (!node.camera.has_value()) continue;
                const bool active = !use_editor_output_ && scene.active_output_camera == node.id;
                if (ImGui::MenuItem(node.name.c_str(), nullptr, active)) {
                    use_editor_output_ = false;
                    scene.setActiveOutputCamera(node.id);
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::TextDisabled("%s%s", session_.displayName().c_str(), session_.dirty ? " *" : "");
        ImGui::EndMainMenuBar();
    }

    void drawHierarchyPanel(genesis::Scene& scene) {
        ImGui::Begin("Hierarchy", &show_hierarchy_);

        genesis::drawEditorSectionLabel("Create");

        const float full = ImGui::GetContentRegionAvail().x;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float half = (full - gap) * 0.5f;

        auto createBtn = [&](const char* label, float width, auto&& fn) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            if (ImGui::Button(label, ImVec2(width, 0))) {
                fn();
            }
            ImGui::PopStyleVar();
        };

        createBtn("Empty", half, [&] {
            selectNode(scene.createEmptyNode("Empty Node"));
            markDirty();
        });
        ImGui::SameLine();
        createBtn("Cube", half, [&] {
            addPrimitiveMesh("Cube", genesis::createCubeMesh(renderer().context()));
        });
        createBtn("Sphere", half, [&] {
            addPrimitiveMesh("Sphere", genesis::createSphereMesh(renderer().context()));
        });
        ImGui::SameLine();
        createBtn("Cylinder", half, [&] {
            addPrimitiveMesh("Cylinder", genesis::createCylinderMesh(renderer().context()));
        });
        createBtn("Plane", half, [&] {
            addPrimitiveMesh("Plane", genesis::createPlaneMesh(renderer().context()));
        });
        ImGui::SameLine();
        createBtn("Camera", half, [&] {
            selectNode(scene.createCameraNode("Camera"));
            markDirty();
        });
        createBtn("Dir Light", half, [&] {
            addLightNode("Directional Light", genesis::SceneLightType::Directional);
        });
        ImGui::SameLine();
        createBtn("Point Light", half, [&] {
            addLightNode("Point Light", genesis::SceneLightType::Point);
        });
        createBtn("Spot Light", half, [&] {
            addLightNode("Spot Light", genesis::SceneLightType::Spot);
        });

        genesis::drawEditorSectionLabel("Particles · Niagara");
        createBtn("Particle System", half, [&] {
            std::optional<genesis::NodeId> scene_root =
                genesis::findNodeByPath(scene, "Scene");
            const genesis::NodeId system =
                scene.createEmptyNode("Particle System", scene_root);
            selectNode(scene.createParticleEmitterNode("Soft Circles", system,
                                                       genesis::makeGentleSoftEmitter()));
            markDirty();
        });
        ImGui::SameLine();
        createBtn("Emitter", half, [&] {
            std::optional<genesis::NodeId> parent =
                genesis::findNodeByPath(scene, "Scene/Particle System");
            if (!parent.has_value()) {
                parent = genesis::findNodeByPath(scene, "Scene");
            }
            selectNode(scene.createParticleEmitterNode("Emitter", parent,
                                                       genesis::makeGentleCirclesEmitter()));
            markDirty();
        });
        createBtn("OSC Tracker", half, [&] {
            std::optional<genesis::NodeId> system =
                genesis::findNodeByPath(scene, "Scene/Particle System");
            if (!system.has_value()) {
                std::optional<genesis::NodeId> scene_root =
                    genesis::findNodeByPath(scene, "Scene");
                system = scene.createEmptyNode("Particle System", scene_root);
            }
            selectNode(scene.createParticleEmitterNode("OSC Tracker", system,
                                                       genesis::makeOscTrackerBubbleEmitter()));
            markDirty();
            genesis::OscParticleEmitSettings emit = osc_protocol_.particleEmitSettings();
            emit.emitter_path = "Scene/Particle System/OSC Tracker";
            emit.track_emitter_position = true;
            emit.burst_on_message = false;
            osc_protocol_.setParticleEmitSettings(emit);
        });

        genesis::drawEditorSectionLabel("Actions");

        ImGui::BeginDisabled(selection_.empty());
        if (ImGui::Button("Duplicate", ImVec2(half, 0))) {
            std::vector<genesis::NodeId> created;
            created.reserve(selection_.size());
            for (const genesis::NodeId id : selection_) {
                if (const genesis::NodeId copy_id = scene.duplicateNode(id)) {
                    created.push_back(copy_id);
                }
            }
            if (!created.empty()) {
                setSelection(created);
                markDirty();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(half, 0))) {
            queueDeleteSelectedNodes();
        }
        ImGui::EndDisabled();

        genesis::drawEditorSectionLabel("Outliner");

        auto drawNode = [&](auto&& self, genesis::NodeId id) -> void {
            const genesis::Node* n = scene.find(id);
            if (!n) return;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_FramePadding |
                                       ImGuiTreeNodeFlags_DefaultOpen;
            if (n->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
            if (isSelected(id)) flags |= ImGuiTreeNodeFlags_Selected;

            std::string label = n->name;
            if (n->mesh_resource.has_value()) label += "  · M";
            if (n->camera.has_value()) label += "  · C";
            if (n->light.has_value()) label += "  · L";
            if (const char* badge = genesis::particleHierarchyBadge(scene, *n);
                badge[0] != '\0') {
                label += "  · ";
                label += badge;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
            const bool open = ImGui::TreeNodeEx((void*)(uintptr_t)n->id, flags, "%s", label.c_str());
            ImGui::PopStyleVar();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                const bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                if (additive) {
                    toggleSelected(id);
                } else {
                    selectNode(id);
                }
            }
            if (ImGui::BeginPopupContextItem("HierarchyNodeMenu")) {
                const bool can_parent_selection =
                    !selection_.empty() &&
                    std::none_of(selection_.begin(), selection_.end(),
                                 [&](const genesis::NodeId sel) {
                                     return sel == id || scene.isDescendantOf(id, sel);
                                 });
                ImGui::BeginDisabled(!can_parent_selection);
                if (ImGui::MenuItem("Parent selection here")) {
                    for (const genesis::NodeId sel : selection_) {
                        if (scene.reparentNode(sel, id)) {
                            markDirty();
                        }
                    }
                    runtime_.updateTransforms();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !can_parent_selection) {
                    ImGui::SetTooltip("Select one or more actors first (cannot parent under itself).");
                }

                const bool can_unparent = n->parent.has_value();
                ImGui::BeginDisabled(!can_unparent);
                if (ImGui::MenuItem("Unparent (move to root)")) {
                    if (scene.reparentNode(id, std::nullopt)) {
                        markDirty();
                        runtime_.updateTransforms();
                    }
                }
                ImGui::EndDisabled();

                if (!selection_.empty() && isSelected(id) && selection_.size() > 1) {
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Parent selected to...")) {
                        for (const genesis::Node& candidate : scene.nodes) {
                            if (std::any_of(selection_.begin(), selection_.end(),
                                            [&](const genesis::NodeId sel) {
                                                return sel == candidate.id ||
                                                       scene.isDescendantOf(candidate.id, sel);
                                            })) {
                                continue;
                            }
                            if (ImGui::MenuItem(candidate.name.c_str())) {
                                for (const genesis::NodeId sel : selection_) {
                                    scene.reparentNode(sel, candidate.id);
                                }
                                markDirty();
                                runtime_.updateTransforms();
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndPopup();
            }
            if (open) {
                for (auto child : n->children) self(self, child);
                ImGui::TreePop();
            }
        };

        for (auto root : scene.roots) drawNode(drawNode, root);
        ImGui::End();
    }

    void drawProtocolPanel() {
        genesis::drawOscProtocolPanel(osc_protocol_, &show_protocol_);
    }

    void drawAudioPanel(genesis::Scene& scene) {
        std::string particle_path;
        if (selection_.size() == 1) {
            if (const genesis::Node* n = scene.find(selection_.front())) {
                if (n->particle_emitter.has_value()) {
                    particle_path = genesis::nodePath(scene, n->id);
                }
            }
        }
        genesis::drawAudioReactivePanel(audio_reactive_, &show_audio_, particle_path);
    }

    void drawInspectorPanel(genesis::Scene& scene) {
        ImGui::Begin("Inspector", &show_inspector_);

        if (selection_.empty()) {
            genesis::drawEditorSectionLabel("Inspector");
            ImGui::TextWrapped("Select an actor in the viewport or outliner.");
            ImGui::Spacing();
            ImGui::TextDisabled("LMB pick / drag marquee");
            ImGui::TextDisabled("Alt+LMB orbit · MMB pan · Alt+RMB dolly · RMB fly");
        } else if (genesis::Node* n = scene.find(primarySelected())) {
            genesis::drawEditorSectionLabel(n->name.c_str());
            const std::string node_path = genesis::nodePath(scene, n->id);
            genesis::OscLinkUiContext link_ctx{&osc_protocol_};
            genesis::AudioLinkUiContext audio_link_ctx{&audio_reactive_};

            if (osc_protocol_.isAwaitingParameterLink()) {
                ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                                   "OSC link active — click Link beside a parameter.");
                ImGui::Separator();
            }
            if (audio_reactive_.isAwaitingParameterLink()) {
                ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.85f, 1.0f),
                                   "Audio link active — click Audio beside a parameter.");
                ImGui::Separator();
            }

            char name_buf[128]{};
            std::snprintf(name_buf, sizeof(name_buf), "%s", n->name.c_str());
            if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
                n->name = name_buf;
                markDirty();
            }
            ImGui::TextDisabled("Transform");
            ImGui::Spacing();

            bool changed = false;
            if (ImGui::Checkbox("Visible", &n->visible)) changed = true;
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeVisible, node_path, 0,
                                       "visible");
            if (n->camera.has_value()) {
                ImGui::BeginDisabled();
                bool camera_pickable = false;
                ImGui::Checkbox("Pickable", &camera_pickable);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Cameras are selected from the Hierarchy only.");
                }
            } else if (ImGui::Checkbox("Pickable", &n->pickable)) {
                changed = true;
            }

            glm::vec3 pos_cm = n->local.position * genesis::kCentimetersPerUnit;
            if (ImGui::DragFloat3("Position (cm)", &pos_cm.x, 1.0f, -100000.0f, 100000.0f, "%.1f")) {
                n->local.position = pos_cm / genesis::kCentimetersPerUnit;
                changed = true;
            }
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodePositionX, node_path, 0,
                                       "pos_x");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodePositionY, node_path, 0,
                                       "pos_y");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodePositionZ, node_path, 0,
                                       "pos_z");

            glm::vec3 scale_pct = n->local.scale * 100.0f;
            if (ImGui::DragFloat3("Scale (%)", &scale_pct.x, 1.0f, 0.01f, 100000.0f, "%.1f")) {
                n->local.scale = scale_pct / 100.0f;
                changed = true;
            }
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeScaleX, node_path, 0,
                                       "scale_x");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeScaleY, node_path, 0,
                                       "scale_y");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeScaleZ, node_path, 0,
                                       "scale_z");

            if (rot_edit_node_ != primarySelected()) {
                rot_edit_node_ = primarySelected();
                const glm::vec3 euler = glm::degrees(glm::eulerAngles(n->local.rotation));
                rot_euler_deg_[0] = euler.x;
                rot_euler_deg_[1] = euler.y;
                rot_euler_deg_[2] = euler.z;
            }

            if (ImGui::DragFloat3("Rotation (deg)", rot_euler_deg_, 0.5f)) {
                const float x = glm::radians(rot_euler_deg_[0]);
                const float y = glm::radians(rot_euler_deg_[1]);
                const float z = glm::radians(rot_euler_deg_[2]);
                const glm::quat qx = glm::angleAxis(x, glm::vec3(1, 0, 0));
                const glm::quat qy = glm::angleAxis(y, glm::vec3(0, 1, 0));
                const glm::quat qz = glm::angleAxis(z, glm::vec3(0, 0, 1));
                n->local.rotation = glm::normalize(qy * qx * qz);
                changed = true;
            }
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeRotationX, node_path, 0,
                                       "rot_x");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeRotationY, node_path, 0,
                                       "rot_y");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::NodeRotationZ, node_path, 0,
                                       "rot_z");

            if (changed) {
                runtime_.updateTransforms();
                markDirty();
                maybeAutoKey();
            }

            if (ImGui::Button("Key Transform @ Playhead")) {
                keyframeSelection(genesis::AnimationTrack::Transform);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stores Position/Rotation/Scale of the selection into the active clip.");
            }

            drawMaterialInspector(*n, node_path, link_ctx);
            drawCameraInspector(scene, *n, node_path, link_ctx);
            drawLightInspector(*n, node_path, link_ctx);

            genesis::ParticleEmitterUiContext particle_ui{};
            particle_ui.scene = &scene;
            particle_ui.osc = &link_ctx;
            particle_ui.audio = &audio_link_ctx;
            particle_ui.mark_dirty = [this]() { markDirty(); };
            particle_ui.reset_particles = [this]() { runtime_.resetParticles(); };
            particle_ui.select_node = [this](genesis::NodeId id) { selectNode(id); };
            particle_ui.stats = [this]() {
                const auto s = runtime_.particleWorld().stats();
                return genesis::ParticleWorldStatsLite{s.alive, s.emitters, s.steps};
            };
            if (n->particle_emitter.has_value()) {
                genesis::drawParticleEmitterInspector(*n, node_path, particle_ui);
            } else if (genesis::isParticleSystemNode(scene, *n)) {
                genesis::drawParticleSystemInspector(*n, particle_ui);
            }
        } else {
            ImGui::TextUnformatted("Selected node not found.");
        }
        ImGui::End();
    }

    void drawColorMenu() {
        auto& settings = color_mgmt_.settings();
        bool dirty = false;

        if (ImGui::RadioButton("Display: Rec.709 (PIXERA default)",
                               settings.display == genesis::DisplayColorSpace::Rec709)) {
            settings.display = genesis::DisplayColorSpace::Rec709;
            dirty = true;
        }
        if (ImGui::RadioButton("Display: sRGB", settings.display == genesis::DisplayColorSpace::sRGB)) {
            settings.display = genesis::DisplayColorSpace::sRGB;
            dirty = true;
        }
        if (ImGui::RadioButton("Display: ACES OCIO view",
                               settings.display == genesis::DisplayColorSpace::AcesOcio)) {
            settings.display = genesis::DisplayColorSpace::AcesOcio;
            dirty = true;
        }

        if (ImGui::DragFloat("Exposure", &settings.exposure, 0.01f, 0.05f, 8.0f)) {
            dirty = true;
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Look (Notch / Unreal inspired)");
        ImGui::TextDisabled("In-shader finishing — no HDR post cost");
        if (ImGui::DragFloat("Bloom / glow", &settings.bloom_strength, 0.01f, 0.0f, 2.0f)) {
            dirty = true;
        }
        if (ImGui::DragFloat("Vignette", &settings.vignette_strength, 0.01f, 0.0f, 1.0f)) {
            dirty = true;
        }
        if (ImGui::DragFloat("Film grain", &settings.grain_strength, 0.001f, 0.0f, 0.15f)) {
            dirty = true;
        }

        ImGui::Separator();
        ImGui::TextWrapped("%s", color_mgmt_.configDescription().empty()
                                     ? "ACES 2.0 Studio config"
                                     : color_mgmt_.configDescription().c_str());
        ImGui::TextDisabled("%s", color_mgmt_.resolvedConfigPath().string().c_str());

        if (ImGui::Button("Reload ACES Studio (latest)")) {
            std::string error;
            if (!color_mgmt_.loadDefaultAcesConfig(&error)) {
                spdlog::warn("Reload ACES config: {}", error);
            }
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Import OCIO...")) {
            genesis::openFileDialog(
                window().handle(),
                {{"OCIO Config", "ocio"}, {"All Files", "*"}},
                [this](std::filesystem::path path) {
                    if (path.empty()) return;
                    std::string error;
                    if (!color_mgmt_.loadConfig(path, &error)) {
                        spdlog::error("OCIO import failed: {}", error);
                        return;
                    }
                    color_mgmt_.settings().display = genesis::DisplayColorSpace::AcesOcio;
                    renderer().uploadDisplayLut(color_mgmt_.rebuildDisplayLut());
                });
        }

        if (!color_mgmt_.displays().empty() &&
            ImGui::BeginCombo("OCIO Display", settings.ocio_display.c_str())) {
            for (const std::string& display : color_mgmt_.displays()) {
                const bool selected = display == settings.ocio_display;
                if (ImGui::Selectable(display.c_str(), selected)) {
                    settings.ocio_display = display;
                    dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (!color_mgmt_.views().empty() && ImGui::BeginCombo("OCIO View", settings.ocio_view.c_str())) {
            for (const std::string& view : color_mgmt_.views()) {
                const bool selected = view == settings.ocio_view;
                if (ImGui::Selectable(view.c_str(), selected)) {
                    settings.ocio_view = view;
                    dirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (!color_mgmt_.colorSpaces().empty() &&
            ImGui::BeginCombo("Working space", settings.ocio_input_space.c_str())) {
            for (const std::string& space : color_mgmt_.colorSpaces()) {
                const bool selected = space == settings.ocio_input_space;
                if (ImGui::Selectable(space.c_str(), selected)) {
                    settings.ocio_input_space = space;
                    dirty = true;
                }
            }
            ImGui::EndCombo();
        }

#if !defined(GENESIS_WITH_OCIO)
        ImGui::TextWrapped(
            "OpenColorIO runtime not linked — Rec.709/sRGB analytic displays work. "
            "Build with GENESIS_WITH_OCIO for full ACES view transforms.");
#endif

        if (dirty) {
            color_mgmt_.markLutDirty();
            renderer().uploadDisplayLut(color_mgmt_.rebuildDisplayLut());
        }
    }

    void importMaterialMap(const std::uint32_t material_id, const char* slot) {
        genesis::openFileDialog(
            window().handle(),
            {{"Images", "png;jpg;jpeg;tga;bmp;gif"}, {"All Files", "*"}},
            [this, material_id, slot = std::string(slot)](std::filesystem::path path) {
                if (path.empty()) return;
                genesis::SceneMaterial* mat = runtime_.material(material_id);
                if (!mat) return;
                try {
                    const bool color_map = slot == "base" || slot == "emissive";
                    auto loaded = genesis::loadImageTexture(
                        renderer().context(), path, genesis::TextureColorSpace::Auto, color_map);
                    const std::uint32_t id =
                        runtime_.addTexture(loaded.label, std::move(loaded.texture));
                    if (slot == "base") {
                        mat->base_color_texture = path.filename().string();
                        mat->base_color_texture_resource = id;
                    } else if (slot == "mr") {
                        mat->metallic_roughness_texture = path.filename().string();
                        mat->metallic_roughness_texture_resource = id;
                    } else if (slot == "normal") {
                        mat->normal_texture = path.filename().string();
                        mat->normal_texture_resource = id;
                    } else if (slot == "emissive") {
                        mat->emissive_texture = path.filename().string();
                        mat->emissive_texture_resource = id;
                    }
                    markDirty();
                } catch (const std::exception& ex) {
                    spdlog::error("Texture import failed: {}", ex.what());
                }
            });
    }

    void drawMaterialInspector(genesis::Node& n, const std::string& node_path,
                               genesis::OscLinkUiContext& link_ctx) {
        if (!n.mesh_resource.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Material");
        if (!n.material_resource.has_value()) {
            if (ImGui::Button("Assign Default Material")) {
                n.has_material = true;
                n.material_resource = runtime_.addDefaultPbrMaterial("Default PBR");
                markDirty();
            }
            return;
        }
        if (*n.material_resource >= runtime_.materials().size()) return;

        genesis::SceneMaterial& mat = *runtime_.material(*n.material_resource);
        const std::uint32_t material_id = *n.material_resource;
        ImGui::Text("%s", runtime_.materialNames()[material_id].c_str());
        ImGui::TextDisabled("OpenPBR Surface");

        if (ImGui::CollapsingHeader("Base", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat("Base weight", &mat.base_weight, 0.01f, 0.0f, 1.0f)) markDirty();
            if (ImGui::ColorEdit4("Base color", &mat.base_color.x)) markDirty();
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialBaseColorR, node_path,
                                       material_id, "base_r");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialBaseColorG, node_path,
                                       material_id, "base_g");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialBaseColorB, node_path,
                                       material_id, "base_b");
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialBaseColorA, node_path,
                                       material_id, "base_a");
            if (ImGui::DragFloat("Metalness", &mat.metallic, 0.01f, 0.0f, 1.0f)) markDirty();
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialMetallic, node_path,
                                       material_id, "metallic");
            if (ImGui::DragFloat("Diffuse roughness", &mat.base_diffuse_roughness, 0.01f, 0.0f, 1.0f)) {
                markDirty();
            }
        }

        if (ImGui::CollapsingHeader("Specular", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat("Specular weight", &mat.specular_weight, 0.01f, 0.0f, 1.0f)) markDirty();
            if (ImGui::ColorEdit3("Specular color", &mat.specular_color.x)) markDirty();
            if (ImGui::DragFloat("Roughness", &mat.roughness, 0.01f, 0.0f, 1.0f)) markDirty();
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialRoughness, node_path,
                                       material_id, "roughness");
            if (ImGui::DragFloat("IOR", &mat.specular_ior, 0.01f, 1.0f, 3.0f)) markDirty();
        }

        if (ImGui::CollapsingHeader("Transmission")) {
            if (ImGui::DragFloat("Transmission weight", &mat.transmission_weight, 0.01f, 0.0f, 1.0f)) {
                markDirty();
            }
            if (ImGui::ColorEdit3("Transmission color", &mat.transmission_color.x)) markDirty();
            if (ImGui::DragFloat("Transmission depth", &mat.transmission_depth, 0.01f, 0.0f, 10.0f)) {
                markDirty();
            }
            if (ImGui::ColorEdit3("Transmission scatter", &mat.transmission_scatter.x)) markDirty();
            if (ImGui::DragFloat("Scatter anisotropy", &mat.transmission_scatter_anisotropy, 0.01f, -1.0f,
                                1.0f)) {
                markDirty();
            }
            if (ImGui::DragFloat("Dispersion scale", &mat.transmission_dispersion_scale, 0.01f, 0.0f,
                                1.0f)) {
                markDirty();
            }
            if (ImGui::DragFloat("Abbe number", &mat.transmission_dispersion_abbe, 0.5f, 9.0f, 91.0f)) {
                markDirty();
            }
            if (ImGui::Checkbox("Thin walled", &mat.thin_walled)) markDirty();
        }

        if (ImGui::CollapsingHeader("Subsurface")) {
            if (ImGui::DragFloat("Subsurface weight", &mat.subsurface_weight, 0.01f, 0.0f, 1.0f)) {
                markDirty();
            }
            if (ImGui::ColorEdit3("Subsurface color", &mat.subsurface_color.x)) markDirty();
            if (ImGui::DragFloat("Subsurface radius", &mat.subsurface_radius, 0.01f, 0.0f, 10.0f)) {
                markDirty();
            }
            if (ImGui::ColorEdit3("Radius scale", &mat.subsurface_radius_scale.x)) markDirty();
            if (ImGui::DragFloat("SSS anisotropy", &mat.subsurface_scatter_anisotropy, 0.01f, -1.0f,
                                1.0f)) {
                markDirty();
            }
        }

        if (ImGui::CollapsingHeader("Coat")) {
            if (ImGui::DragFloat("Coat weight", &mat.coat_weight, 0.01f, 0.0f, 1.0f)) markDirty();
            if (ImGui::ColorEdit3("Coat color", &mat.coat_color.x)) markDirty();
            if (ImGui::DragFloat("Coat roughness", &mat.coat_roughness, 0.01f, 0.0f, 1.0f)) markDirty();
            if (ImGui::DragFloat("Coat IOR", &mat.coat_ior, 0.01f, 1.0f, 3.0f)) markDirty();
            if (ImGui::DragFloat("Coat darkening", &mat.coat_darkening, 0.01f, 0.0f, 1.0f)) markDirty();
        }

        if (ImGui::CollapsingHeader("Fuzz")) {
            if (ImGui::DragFloat("Fuzz weight", &mat.fuzz_weight, 0.01f, 0.0f, 1.0f)) markDirty();
            if (ImGui::ColorEdit3("Fuzz color", &mat.fuzz_color.x)) markDirty();
            if (ImGui::DragFloat("Fuzz roughness", &mat.fuzz_roughness, 0.01f, 0.0f, 1.0f)) markDirty();
        }

        if (ImGui::CollapsingHeader("Thin Film")) {
            if (ImGui::DragFloat("Thin film weight", &mat.thin_film_weight, 0.01f, 0.0f, 1.0f)) {
                markDirty();
            }
            if (ImGui::DragFloat("Thickness (um)", &mat.thin_film_thickness, 0.01f, 0.0f, 2.0f)) {
                markDirty();
            }
            if (ImGui::DragFloat("Thin film IOR", &mat.thin_film_ior, 0.01f, 1.0f, 3.0f)) markDirty();
        }

        if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::ColorEdit3("Emissive", &mat.emissive.x)) markDirty();
            if (ImGui::DragFloat("Emissive strength", &mat.emissive_strength, 0.05f, 0.0f, 32.0f)) {
                markDirty();
            }
            genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::MaterialEmissiveStrength, node_path,
                                       material_id, "emissive_strength");
            if (ImGui::DragFloat("Normal scale", &mat.normal_scale, 0.01f, 0.0f, 4.0f)) markDirty();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Textures");
        auto mapRow = [&](const char* label, const char* slot, std::string& name,
                          std::optional<std::uint32_t>& resource) {
            ImGui::Text("%s: %s", label, name.empty() ? "(none)" : name.c_str());
            ImGui::SameLine();
            const std::string import_id = std::string("Import##") + slot;
            const std::string clear_id = std::string("Clear##") + slot;
            if (ImGui::SmallButton(import_id.c_str())) {
                importMaterialMap(*n.material_resource, slot);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(clear_id.c_str())) {
                name.clear();
                resource.reset();
                markDirty();
            }
        };
        mapRow("Base", "base", mat.base_color_texture, mat.base_color_texture_resource);
        mapRow("MR", "mr", mat.metallic_roughness_texture, mat.metallic_roughness_texture_resource);
        mapRow("Normal", "normal", mat.normal_texture, mat.normal_texture_resource);
        mapRow("Emissive", "emissive", mat.emissive_texture, mat.emissive_texture_resource);
    }

    void drawCameraInspector(genesis::Scene& scene, genesis::Node& n, const std::string& node_path,
                             genesis::OscLinkUiContext& link_ctx) {
        if (!n.camera.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Camera");
        genesis::SceneCamera& cam = *n.camera;
        float fov_deg = glm::degrees(cam.fov_y_radians);
        if (ImGui::DragFloat("FOV (deg)", &fov_deg, 0.5f, 1.0f, 170.0f)) {
            cam.fov_y_radians = glm::radians(fov_deg);
            markDirty();
        }
        genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::CameraFov, node_path, 0, "fov");
        if (ImGui::Button("Use as Output Camera")) {
            use_editor_output_ = false;
            scene.setActiveOutputCamera(n.id);
        }
    }

    void drawLightInspector(genesis::Node& n, const std::string& node_path,
                            genesis::OscLinkUiContext& link_ctx) {
        if (!n.light.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Light");
        genesis::SceneLight& light = *n.light;
        if (ImGui::Checkbox("Enabled", &light.enabled)) markDirty();
        genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::LightEnabled, node_path, 0,
                                   "light_enabled");

        int type_i = static_cast<int>(light.type);
        const char* type_items[] = {"Directional", "Point", "Spot"};
        if (ImGui::Combo("Type", &type_i, type_items, 3)) {
            light.type = static_cast<genesis::SceneLightType>(type_i);
            markDirty();
        }

        if (ImGui::ColorEdit3("Color", &light.color.x)) markDirty();
        genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::LightColorR, node_path, 0,
                                   "light_r");
        genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::LightColorG, node_path, 0,
                                   "light_g");
        genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::LightColorB, node_path, 0,
                                   "light_b");
        if (ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f)) markDirty();
        genesis::drawOscLinkButton(link_ctx, genesis::OscParamTarget::LightIntensity, node_path, 0,
                                   "light_intensity");

        if (light.type == genesis::SceneLightType::Point || light.type == genesis::SceneLightType::Spot) {
            if (ImGui::DragFloat("Range", &light.range, 0.05f, 0.01f, 1000.0f)) markDirty();
        }
        if (light.type == genesis::SceneLightType::Spot) {
            float inner_deg = glm::degrees(light.inner_cone_radians);
            float outer_deg = glm::degrees(light.outer_cone_radians);
            if (ImGui::DragFloat("Inner Cone", &inner_deg, 0.25f, 0.0f, 89.0f)) {
                light.inner_cone_radians = glm::radians(std::min(inner_deg, outer_deg));
                markDirty();
            }
            if (ImGui::DragFloat("Outer Cone", &outer_deg, 0.25f, 1.0f, 89.0f)) {
                light.outer_cone_radians = glm::radians(std::max(outer_deg, inner_deg));
                markDirty();
            }
        }
    }

    void drawToolbarPanel(genesis::Scene& scene) {
        ImGui::Begin("Toolbar", &show_toolbar_);

        if (ImGui::RadioButton("Move", gizmo_operation_ == ImGuizmo::TRANSLATE)) {
            gizmo_operation_ = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", gizmo_operation_ == ImGuizmo::ROTATE)) {
            gizmo_operation_ = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", gizmo_operation_ == ImGuizmo::SCALE)) {
            gizmo_operation_ = ImGuizmo::SCALE;
        }

        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::RadioButton("Local", gizmo_mode_ == ImGuizmo::LOCAL)) gizmo_mode_ = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton("World", gizmo_mode_ == ImGuizmo::WORLD)) gizmo_mode_ = ImGuizmo::WORLD;

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::Checkbox("Grid", &show_grid_);
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::Checkbox("Game", &game_view_mode_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Game View (G) — hide actor icons");
        }

        ImGui::SameLine(0.0f, 20.0f);
        if (!selection_.empty() && scene.find(primarySelected())) {
            if (selection_.size() == 1) {
                ImGui::TextDisabled("%s", scene.find(primarySelected())->name.c_str());
            } else {
                ImGui::TextDisabled("%zu selected", selection_.size());
            }
        } else {
            ImGui::TextDisabled("Nothing selected");
        }

        const genesis::FrameStats& stats = frameStats();
        const char* fps_line = "  %.0f FPS · %u tris";
        const ImVec2 fps_size = ImGui::CalcTextSize("  000 FPS · 00000 tris");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - fps_size.x);
        ImGui::TextDisabled(fps_line, timer().fps(), stats.triangles);

        ImGui::End();
    }

    void drawViewportHelpPanel() {
        ImGui::Begin("Shortcuts", &show_viewport_help_);
        ImGui::TextDisabled("NAVIGATION (Unreal-like, slowed)");
        ImGui::BulletText("Alt + LMB — orbit around pivot");
        ImGui::BulletText("MMB / Alt + MMB — pan (slow)");
        ImGui::BulletText("Alt + RMB / wheel — dolly");
        ImGui::BulletText("RMB + WASD / QE — fly look");
        ImGui::BulletText("F — frame selection (sets orbit pivot)");
        ImGui::BulletText("Grid — 1 m major / 0.25 m minor");
        ImGui::BulletText("G — Game View (hide actor icons)");
        ImGui::Spacing();
        ImGui::TextDisabled("SELECTION");
        ImGui::BulletText("Meshes — thin white silhouette outline");
        ImGui::BulletText("Emitters / lights / cameras — viewport icons");
        ImGui::BulletText("LMB — select / marquee");
        ImGui::BulletText("Shift / Ctrl + LMB — multi");
        ImGui::BulletText("Delete — remove (children reparent)");
        ImGui::Spacing();
        ImGui::TextDisabled("TRANSFORM");
        ImGui::BulletText("W / E / R — move, rotate, scale");
        ImGui::BulletText("Drag gizmo — edit primary");
        ImGui::End();
    }

    void importModelDialog() {
        genesis::openFileDialog(
            window().handle(),
            {{"3D Models", "gltf;glb;obj;fbx"}, {"glTF", "gltf;glb"}, {"OBJ", "obj"}, {"FBX", "fbx"},
             {"All Files", "*"}},
            [this](std::filesystem::path path) {
                if (path.empty()) {
                    return;
                }
                try {
                    const genesis::ModelImportResult result = genesis::ModelImporter::importInto(runtime_, path);
                    if (!result.ok) {
                        spdlog::error("Import failed: {}", result.error);
                        return;
                    }
                    selectNode(result.root_node != 0 ? result.root_node : 0);
                    markDirty();
                    show_timeline_ = true;
                    if (!result.clip_indices.empty()) {
                        show_animation_graph_ = true;
                    }
                } catch (const std::exception& ex) {
                    spdlog::error("Import failed: {}", ex.what());
                }
            });
    }

    void drawTimelinePanel() {
        ImGui::Begin("Timeline", &show_timeline_);
        genesis::AnimationPlayback& playback = runtime_.animationPlayback();
        auto& clips = runtime_.animationClips();

        if (clips.empty()) {
            ImGui::TextWrapped(
                "No clips yet. Keyframe any object transform, or import an animated FBX/glTF.");
            if (ImGui::Button("Create Scene Clip")) {
                playback.clip_index = runtime_.ensureSceneAnimationClip();
                show_timeline_ = true;
                markDirty();
            }
            ImGui::End();
            return;
        }

        if (playback.clip_index < 0 || playback.clip_index >= static_cast<int>(clips.size())) {
            playback.clip_index = 0;
        }

        if (ImGui::BeginCombo("Clip", clips[static_cast<std::size_t>(playback.clip_index)].name.c_str())) {
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(clips.size()); ++i) {
                const bool selected = i == playback.clip_index;
                if (ImGui::Selectable(clips[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                    playback.clip_index = i;
                    playback.time_seconds = 0.0f;
                    playback.playing = false;
                    runtime_.applyActiveAnimation();
                    if (const genesis::AnimationGraphState* state = runtime_.animationGraph().findState(
                            clips[static_cast<std::size_t>(i)].name)) {
                        runtime_.animationGraph().setActiveState(state->name);
                    }
                }
            }
            ImGui::EndCombo();
        }

        genesis::AnimationClip& clip = clips[static_cast<std::size_t>(playback.clip_index)];
        ImGui::Text("Duration: %.2fs  |  Channels: %zu", clip.duration_seconds, clip.channels.size());

        if (ImGui::Button(playback.playing ? "Pause" : "Play")) {
            playback.playing = !playback.playing;
            if (playback.playing) {
                runtime_.applyActiveAnimation();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            playback.playing = false;
            playback.time_seconds = 0.0f;
            runtime_.applyActiveAnimation();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &playback.loop);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-Key", &auto_key_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When on, transform edits (inspector/gizmo) insert keys at the playhead.");
        }

        float time = playback.time_seconds;
        float duration = std::max(clip.duration_seconds, 0.001f);
        if (ImGui::DragFloat("Duration (s)", &duration, 0.05f, 0.1f, 3600.0f, "%.2f")) {
            clip.duration_seconds = duration;
            markDirty();
        }
        if (ImGui::SliderFloat("Time", &time, 0.0f, std::max(clip.duration_seconds, 0.001f), "%.3f s")) {
            playback.time_seconds = time;
            playback.playing = false;
            runtime_.applyActiveAnimation();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Keyframe selection");
        const bool has_sel = !selection_.empty();
        ImGui::BeginDisabled(!has_sel);
        if (ImGui::Button("Key All")) {
            keyframeSelection(genesis::AnimationTrack::Transform);
        }
        ImGui::SameLine();
        if (ImGui::Button("Key Pos")) {
            keyframeSelection(genesis::AnimationTrack::Translation);
        }
        ImGui::SameLine();
        if (ImGui::Button("Key Rot")) {
            keyframeSelection(genesis::AnimationTrack::Rotation);
        }
        ImGui::SameLine();
        if (ImGui::Button("Key Scale")) {
            keyframeSelection(genesis::AnimationTrack::Scale);
        }
        if (ImGui::Button("Remove Keys @ Time")) {
            removeKeysAtPlayhead(genesis::AnimationTrack::Transform);
        }
        ImGui::EndDisabled();
        if (!has_sel) {
            ImGui::TextDisabled("Select one or more objects to keyframe.");
        }

        // Scrub bar with key marks for the primary selection.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = 22.0f;
        draw->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), IM_COL32(28, 30, 36, 255));
        if (clip.duration_seconds > 1e-5f) {
            const genesis::NodeId focus = primarySelected();
            for (const genesis::AnimationChannel& channel : clip.channels) {
                if (focus != 0 && channel.target_node_id != 0 && channel.target_node_id != focus) {
                    continue;
                }
                auto mark = [&](float t, ImU32 col) {
                    const float x = p0.x + width * std::clamp(t / clip.duration_seconds, 0.0f, 1.0f);
                    draw->AddLine(ImVec2(x, p0.y + 2.0f), ImVec2(x, p0.y + height - 2.0f), col, 1.5f);
                };
                for (const auto& k : channel.translations) mark(k.time_seconds, IM_COL32(80, 180, 255, 220));
                for (const auto& k : channel.rotations) mark(k.time_seconds, IM_COL32(180, 120, 255, 220));
                for (const auto& k : channel.scales) mark(k.time_seconds, IM_COL32(120, 220, 140, 220));
            }
            const float x =
                p0.x + width * std::clamp(playback.time_seconds / clip.duration_seconds, 0.0f, 1.0f);
            draw->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + height), IM_COL32(255, 196, 64, 255), 2.0f);
        }
        ImGui::Dummy(ImVec2(width, height));
        ImGui::TextDisabled("Keys: pos (blue) rot (purple) scale (green). Pause to edit, then Key.");
        ImGui::End();
    }

    void keyframeSelection(genesis::AnimationTrack tracks) {
        if (selection_.empty()) {
            return;
        }
        runtime_.ensureSceneAnimationClip();
        if (runtime_.animationPlayback().clip_index < 0) {
            runtime_.animationPlayback().clip_index = runtime_.ensureSceneAnimationClip();
        }
        runtime_.keyframeNodes(selection_, runtime_.animationPlayback().time_seconds, tracks);
        markDirty();
    }

    void removeKeysAtPlayhead(genesis::AnimationTrack tracks) {
        auto& clips = runtime_.animationClips();
        auto& playback = runtime_.animationPlayback();
        if (playback.clip_index < 0 || playback.clip_index >= static_cast<int>(clips.size())) {
            return;
        }
        genesis::AnimationClip& clip = clips[static_cast<std::size_t>(playback.clip_index)];
        for (genesis::NodeId id : selection_) {
            genesis::removeKeysAtTime(clip, id, playback.time_seconds, tracks);
        }
        markDirty();
    }

    void maybeAutoKey() {
        if (!auto_key_ || selection_.empty() || runtime_.animationPlayback().playing) {
            return;
        }
        keyframeSelection(genesis::AnimationTrack::Transform);
    }

    void drawAnimationGraphPanel() {
        ImGui::Begin("Animation Graph", &show_animation_graph_);
        genesis::AnimationGraph& graph = runtime_.animationGraph();
        graph.ensureDefaults();

        ImGui::TextWrapped(
            "States select which clip the Timeline drives. Transitions are listed for authoring; "
            "blend evaluation comes next.");
        ImGui::Separator();

        char entry_buf[128]{};
        std::snprintf(entry_buf, sizeof(entry_buf), "%s", graph.entry_state.c_str());
        if (ImGui::InputText("Entry state", entry_buf, sizeof(entry_buf))) {
            graph.entry_state = entry_buf;
        }

        if (ImGui::BeginCombo("Active state", graph.active_state.c_str())) {
            for (const genesis::AnimationGraphState& state : graph.states) {
                const bool selected = state.name == graph.active_state;
                if (ImGui::Selectable(state.name.c_str(), selected)) {
                    graph.setActiveState(state.name);
                    genesis::AnimationPlayback& playback = runtime_.animationPlayback();
                    playback.clip_index = state.clip_index;
                    playback.loop = state.loop;
                    playback.time_seconds = 0.0f;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("States");
        for (std::size_t i = 0; i < graph.states.size(); ++i) {
            genesis::AnimationGraphState& state = graph.states[i];
            ImGui::PushID(static_cast<int>(i));
            char name_buf[128]{};
            std::snprintf(name_buf, sizeof(name_buf), "%s", state.name.c_str());
            if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
                state.name = name_buf;
            }
            ImGui::DragInt("Clip index", &state.clip_index, 1, -1,
                           static_cast<int>(runtime_.animationClips().size()) - 1);
            ImGui::Checkbox("Loop", &state.loop);
            if (ImGui::Button("Activate")) {
                graph.setActiveState(state.name);
                runtime_.animationPlayback().clip_index = state.clip_index;
                runtime_.animationPlayback().loop = state.loop;
            }
            ImGui::Separator();
            ImGui::PopID();
        }

        if (ImGui::Button("Add State")) {
            genesis::AnimationGraphState state{};
            state.name = "State " + std::to_string(graph.states.size() + 1);
            graph.states.push_back(state);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Transitions");
        for (std::size_t i = 0; i < graph.transitions.size(); ++i) {
            genesis::AnimationGraphTransition& tr = graph.transitions[i];
            ImGui::PushID(static_cast<int>(1000 + i));
            ImGui::Text("%s -> %s (%.2fs)", tr.from_state.c_str(), tr.to_state.c_str(), tr.blend_seconds);
            ImGui::PopID();
        }
        if (ImGui::Button("Add Transition (active -> entry)")) {
            genesis::AnimationGraphTransition tr{};
            tr.from_state = graph.active_state;
            tr.to_state = graph.entry_state;
            graph.transitions.push_back(tr);
        }

        ImGui::End();
    }

    void drawDebugWindow() {
        ImGui::Begin("Performance / Debug", &show_debug_window_);

        ImGui::Text("FPS: %.1f", timer().fps());
        ImGui::Text("Frame: %.3f ms", frameStats().frame_ms);
        ImGui::Text("  Update: %.3f ms", frameStats().update_ms);
        ImGui::Text("  UI: %.3f ms", frameStats().imgui_ms);
        ImGui::Text("  Draw: %.3f ms", frameStats().draw_ms);
        ImGui::Separator();
        ImGui::Text("Draw calls: %u", frameStats().draw_calls);
        ImGui::Text("Triangles: %u", frameStats().triangles);
        ImGui::Text("Visible meshes: %u", frameStats().visible_mesh_nodes);
        ImGui::Text("Scene nodes: %u", frameStats().scene_nodes);
        ImGui::Text("Active lights: %u", frameStats().active_lights);
        ImGui::Text("GPU: %s", renderer().deviceName());
        ImGui::Text("Resolution: %d x %d", window().width(), window().height());
        ImGui::Separator();
        ImGui::TextWrapped("Projects: %s", genesis::editorProjectsDir().string().c_str());
        ImGui::TextWrapped("Layout: %s", genesis::editorLayoutIniPath().string().c_str());
        if (!session_.document_path.empty()) {
            ImGui::TextWrapped("Document: %s", session_.document_path.string().c_str());
        }

        ImGui::End();
    }

    void drawFileModals(genesis::Scene& scene) {
        (void)scene;

        if (show_new_scene_confirm_) {
            ImGui::OpenPopup("Discard Changes?");
            show_new_scene_confirm_ = false;
        }
        if (show_open_confirm_) {
            ImGui::OpenPopup("Open Scene?");
            show_open_confirm_ = false;
        }
        if (show_exit_confirm_) {
            ImGui::OpenPopup("Exit GENESIS?");
            show_exit_confirm_ = false;
        }

        if (ImGui::BeginPopupModal("Discard Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Discard unsaved changes and create a new scene?");
            if (ImGui::Button("Discard")) {
                newBlankScene();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Open Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Discard unsaved changes and open another scene?");
            if (ImGui::Button("Open...")) {
                ImGui::CloseCurrentPopup();
                openSceneDialog();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Exit GENESIS?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save changes before exiting?");
            if (ImGui::Button("Save")) {
                ImGui::CloseCurrentPopup();
                saveAndExit();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save")) {
                exit_confirmed_ = true;
                window().setShouldClose(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void drawViewportGizmo(genesis::Scene& scene) {
        const genesis::NodeId primary = primarySelected();
        if (primary == 0) return;
        genesis::Node* node = scene.find(primary);
        if (!node) return;

        runtime_.updateTransforms();

        const float aspect = renderAspect();
        const glm::mat4 view = outputViewMatrix();
        const glm::mat4 projection = genesis::projectionForImGuizmo(outputProjectionMatrix(aspect));

        float view_m[16]{};
        float proj_m[16]{};
        float matrix_m[16]{};
        genesis::mat4ToFloat16(view, view_m);
        genesis::mat4ToFloat16(projection, proj_m);
        genesis::mat4ToFloat16(node->world, matrix_m);

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(viewport_.x, viewport_.y, viewport_.w, viewport_.h);
        ImGuizmo::Enable(true);
        ImGuizmo::SetID(static_cast<int>(primary));

        if (ImGuizmo::Manipulate(view_m, proj_m, gizmo_operation_, gizmo_mode_, matrix_m)) {
            if (applyGizmoWorldToLocal(scene, *node, matrix_m)) {
                runtime_.updateTransforms();
                rot_edit_node_ = 0;
                markDirty();
                maybeAutoKey();
            }
        }
    }

    bool applyGizmoWorldToLocal(genesis::Scene& scene, genesis::Node& node, const float* matrix_m) {
        const glm::mat4 new_world = genesis::float16ToMat4(matrix_m);
        const glm::mat4 new_local = genesis::worldToLocalMatrix(scene, node, new_world);
        if (!genesis::decomposeTransform(new_local, node.local)) {
            return false;
        }
        node.local.rotation = glm::normalize(node.local.rotation);
        return true;
    }

    void handleViewportSelection(genesis::Scene& scene) {
        (void)scene;
        const bool alt_nav = ImGui::GetIO().KeyAlt;
        if (fly_camera_active_ || navigating_active_ || alt_nav) {
            marquee_ = {};
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;
        const bool over_viewport = viewport_.contains(mouse.x, mouse.y);
        const bool gizmo_blocks = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

        if (over_viewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmo_blocks) {
            marquee_.active = true;
            marquee_.dragging = false;
            marquee_.start = mouse;
            marquee_.current = mouse;
        }

        if (marquee_.active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            marquee_.current = mouse;
            const float dx = marquee_.current.x - marquee_.start.x;
            const float dy = marquee_.current.y - marquee_.start.y;
            if ((dx * dx + dy * dy) >= kMarqueeDragThresholdPx * kMarqueeDragThresholdPx) {
                marquee_.dragging = true;
            }
        }

        if (marquee_.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const bool additive = io.KeyShift || io.KeyCtrl;
            if (marquee_.dragging) {
                applyMarqueeSelection(additive);
            } else if (over_viewport && !gizmo_blocks) {
                applyClickSelection(mouse, additive);
            }
            marquee_ = {};
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Cancel stale marquee if mouse was released outside our handlers.
            if (marquee_.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                marquee_ = {};
            }
        }
    }

    void applyClickSelection(const ImVec2& mouse, const bool additive) {
        if (!viewport_.contains(mouse.x, mouse.y)) {
            return;
        }

        const float aspect = renderAspect();
        const float local_x = mouse.x - viewport_.x;
        const float local_y = mouse.y - viewport_.y;

        const genesis::ScreenRay screen_ray = genesis::rayFromScreen(
            outputViewMatrix(), outputProjectionMatrix(aspect), aspect, local_x, local_y, viewport_.w,
            viewport_.h);

        const genesis::PickRay ray{screen_ray.origin, screen_ray.direction};
        genesis::ScenePickParams params{};
        params.meshes_only = false;
        params.exclude_cameras = true;

        if (std::optional<genesis::ScenePickResult> picked = runtime_.pickNodeAtRay(ray, params)) {
            if (additive) {
                toggleSelected(picked->node_id);
            } else {
                selectNode(picked->node_id);
            }
        } else if (!additive) {
            clearSelection();
        }
    }

    void applyMarqueeSelection(const bool additive) {
        genesis::ScreenRect rect{};
        rect.min_x = marquee_.start.x - viewport_.x;
        rect.min_y = marquee_.start.y - viewport_.y;
        rect.max_x = marquee_.current.x - viewport_.x;
        rect.max_y = marquee_.current.y - viewport_.y;
        rect.normalize();

        const float aspect = renderAspect();
        const glm::mat4 view_proj =
            genesis::projectionForImGuizmo(outputProjectionMatrix(aspect)) * outputViewMatrix();

        genesis::ScenePickParams params{};
        params.meshes_only = false;
        params.exclude_cameras = true;

        std::vector<genesis::NodeId> hits;
        genesis::pickNodesInScreenRect(runtime_.scene(), runtime_.meshes(), view_proj, viewport_.w,
                                       viewport_.h, rect, params, hits);

        if (!additive) {
            clearSelection();
        }
        for (const genesis::NodeId id : hits) {
            addToSelection(id);
        }
    }

    void drawSelectionOverlays(genesis::Scene& scene) {
        (void)scene;
        // Mesh silhouette is a 3D inverted-hull pass. Icons are drawn separately.
        // Marquee only — no AABB/cyan screen boxes.
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        if (marquee_.active && marquee_.dragging) {
            draw->AddRectFilled(marquee_.start, marquee_.current, IM_COL32(240, 240, 245, 28));
            draw->AddRect(marquee_.start, marquee_.current, IM_COL32(240, 240, 245, 200), 0.0f, 0,
                          1.25f);
        }
    }

    static bool projectWorldPointToScreen(const glm::mat4& view_proj, const glm::vec3& world,
                                          float viewport_w, float viewport_h, float& out_x,
                                          float& out_y) {
        const glm::vec4 clip = view_proj * glm::vec4(world, 1.0f);
        if (std::abs(clip.w) <= 1e-5f || clip.w < 0.0f) {
            return false;
        }
        const float inv_w = 1.0f / clip.w;
        const float ndc_x = clip.x * inv_w;
        const float ndc_y = clip.y * inv_w;
        out_x = (ndc_x * 0.5f + 0.5f) * viewport_w;
        out_y = (1.0f - ndc_y) * 0.5f * viewport_h;
        return out_x >= -40.0f && out_y >= -40.0f && out_x <= viewport_w + 40.0f &&
               out_y <= viewport_h + 40.0f;
    }

    enum class ActorIconKind { Emitter, Light, Camera };

    static void drawOneActorIcon(ImDrawList* draw, ImVec2 center, float radius, ActorIconKind kind,
                                 bool selected) {
        const ImU32 fill =
            kind == ActorIconKind::Emitter   ? IM_COL32(70, 170, 210, selected ? 230 : 160)
            : kind == ActorIconKind::Light   ? IM_COL32(230, 190, 70, selected ? 235 : 165)
                                             : IM_COL32(150, 160, 180, selected ? 230 : 155);
        const ImU32 edge = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 22, 26, 200);
        draw->AddCircleFilled(center, radius, fill, 20);
        draw->AddCircle(center, radius, edge, 20, selected ? 2.0f : 1.25f);

        if (kind == ActorIconKind::Emitter) {
            // Soft concentric rings — Niagara/emitter billboard cue.
            draw->AddCircle(center, radius * 0.55f, IM_COL32(255, 255, 255, selected ? 220 : 140), 16,
                            1.25f);
            draw->AddCircle(center, radius * 0.28f, IM_COL32(255, 255, 255, selected ? 200 : 120), 12,
                            1.0f);
        } else if (kind == ActorIconKind::Light) {
            // Simple radial ticks.
            for (int i = 0; i < 6; ++i) {
                const float a = static_cast<float>(i) * 1.04719755f;
                const ImVec2 d(std::cos(a), std::sin(a));
                draw->AddLine(ImVec2(center.x + d.x * radius * 0.35f, center.y + d.y * radius * 0.35f),
                              ImVec2(center.x + d.x * radius * 0.85f, center.y + d.y * radius * 0.85f),
                              IM_COL32(40, 36, 20, selected ? 230 : 170), 1.4f);
            }
        } else {
            // Camera body + lens.
            draw->AddRectFilled(ImVec2(center.x - radius * 0.45f, center.y - radius * 0.28f),
                                ImVec2(center.x + radius * 0.15f, center.y + radius * 0.28f),
                                IM_COL32(30, 32, 38, 220), 2.0f);
            draw->AddTriangleFilled(ImVec2(center.x + radius * 0.1f, center.y - radius * 0.22f),
                                    ImVec2(center.x + radius * 0.55f, center.y),
                                    ImVec2(center.x + radius * 0.1f, center.y + radius * 0.22f),
                                    IM_COL32(30, 32, 38, 220));
        }
        if (selected) {
            draw->AddCircle(center, radius + 3.0f, IM_COL32(255, 255, 255, 180), 22, 1.5f);
        }
    }

    void drawActorIcons(genesis::Scene& scene) {
        if (game_view_mode_ || !show_actor_icons_ || !show_viewport_ || viewport_.w < 2.0f) {
            return;
        }

        runtime_.updateTransforms();
        const float aspect = renderAspect();
        const glm::mat4 view_proj =
            genesis::projectionForImGuizmo(outputProjectionMatrix(aspect)) * outputViewMatrix();
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        constexpr float kIconRadius = 11.0f;

        for (const genesis::Node& node : scene.nodes) {
            if (!node.visible) {
                continue;
            }
            ActorIconKind kind{};
            if (node.particle_emitter.has_value()) {
                kind = ActorIconKind::Emitter;
            } else if (node.light.has_value()) {
                kind = ActorIconKind::Light;
            } else if (node.camera.has_value()) {
                kind = ActorIconKind::Camera;
            } else {
                continue;
            }

            const glm::vec3 world = glm::vec3(node.world[3]);
            float sx = 0.0f;
            float sy = 0.0f;
            if (!projectWorldPointToScreen(view_proj, world, viewport_.w, viewport_.h, sx, sy)) {
                continue;
            }
            const ImVec2 center(viewport_.x + sx, viewport_.y + sy);
            drawOneActorIcon(draw, center, kIconRadius, kind, isSelected(node.id));
        }
    }

    static genesis::MeshMaterialPushConstants makeOutlineMaterial(bool primary) {
        genesis::MeshMaterialPushConstants mat{};
        // Thin white Unreal-style silhouette — not amber, not a filled wash.
        mat.base_color[0] = 1.0f;
        mat.base_color[1] = 1.0f;
        mat.base_color[2] = 1.0f;
        mat.base_color[3] = primary ? 0.95f : 0.55f;
        // Expand scale (meters of shell per meter of camera distance).
        mat.material_params[0] = 0.006f;
        mat.material_params[2] = 0.0f;
        return mat;
    }

    void drawSelectionEdgeOutlines(const genesis::MeshDrawOptions& draw_options,
                                   const float* view_proj_col_major) {
        if (selection_.empty() || !show_viewport_ || viewport_.w < 2.0f) {
            return;
        }

        genesis::Scene& scene = runtime_.scene();
        runtime_.updateTransforms();

        std::vector<genesis::SceneMeshBatch> outline_batches;
        outline_batches.reserve(selection_.size());

        auto append_instance = [&](const genesis::Mesh* mesh, const glm::mat4& model,
                                   genesis::NodeId id) {
            if (!mesh || !mesh->vertex_buffer || !mesh->index_buffer || mesh->index_count == 0) {
                return;
            }
            const bool primary = id == primarySelected();
            genesis::SceneMeshBatch* batch = nullptr;
            for (genesis::SceneMeshBatch& existing : outline_batches) {
                if (existing.mesh == mesh &&
                    existing.material.base_color[3] == (primary ? 0.95f : 0.55f)) {
                    batch = &existing;
                    break;
                }
            }
            if (!batch) {
                outline_batches.push_back({});
                batch = &outline_batches.back();
                batch->mesh = mesh;
                batch->material = makeOutlineMaterial(primary);
            }
            genesis::GpuMeshInstance instance{};
            genesis::fillGpuMeshInstance(instance, model, mesh->local_aabb_min, mesh->local_aabb_max);
            batch->instances.push_back(instance);
            batch->node_ids.push_back(id);
        };

        // Mesh / fluid actors only — never particle sprites, never helper spheres/boxes.
        std::unordered_set<genesis::NodeId> outlined;
        for (const genesis::SceneDrawCommand& cmd : draw_commands_) {
            if (cmd.node_id == 0 || !isSelected(cmd.node_id) || !cmd.mesh) {
                continue;
            }
            // Skip GPU particle instances (node_id is 0 above; belt-and-suspenders for future tagging).
            append_instance(cmd.mesh, cmd.model, cmd.node_id);
            outlined.insert(cmd.node_id);
        }
        for (const genesis::NodeId id : selection_) {
            if (outlined.count(id) != 0) {
                continue;
            }
            const genesis::Node* node = scene.find(id);
            if (!node || !node->mesh_resource.has_value()) {
                continue;
            }
            if (const genesis::Mesh* mesh = runtime_.mesh(*node->mesh_resource)) {
                append_instance(mesh, node->world, id);
            }
        }

        if (outline_batches.empty()) {
            return;
        }

        genesis::MeshDrawOptions opts = draw_options;
        opts.view_proj_col_major = view_proj_col_major;
        opts.enable_gpu_cull = false;
        opts.enable_depth_prepass = false;
        renderer().drawSelectionOutlineBatches(outline_batches, opts);
    }

    bool isSelected(const genesis::NodeId id) const {
        return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
    }

    genesis::NodeId primarySelected() const {
        return selection_.empty() ? 0 : selection_.back();
    }

    void clearSelection() {
        selection_.clear();
        rot_edit_node_ = 0;
    }

    void deleteSelectedNodes(genesis::Scene& scene) {
        (void)scene;
        queueDeleteSelectedNodes();
    }

    void queueDeleteSelectedNodes() {
        if (selection_.empty()) {
            return;
        }
        pending_delete_.insert(pending_delete_.end(), selection_.begin(), selection_.end());
        clearSelection();
        reparent_target_ = 0;
        ImGuizmo::Enable(false);
    }

    static std::vector<genesis::NodeId> pruneDeletionTargets(const genesis::Scene& scene,
                                                             const std::vector<genesis::NodeId>& ids) {
        std::unordered_set<genesis::NodeId> unique(ids.begin(), ids.end());
        std::vector<genesis::NodeId> targets;
        targets.reserve(unique.size());
        for (const genesis::NodeId id : unique) {
            if (scene.find(id)) {
                targets.push_back(id);
            }
        }
        // Delete deepest nodes first so reparented children aren't deleted twice.
        std::sort(targets.begin(), targets.end(), [&](const genesis::NodeId a, const genesis::NodeId b) {
            auto depth = [&](genesis::NodeId id) {
                int d = 0;
                while (const genesis::Node* n = scene.find(id)) {
                    if (!n->parent.has_value()) {
                        break;
                    }
                    id = *n->parent;
                    ++d;
                }
                return d;
            };
            return depth(a) > depth(b);
        });
        return targets;
    }

    void processPendingDeletes() {
        if (pending_delete_.empty()) {
            return;
        }

        genesis::Scene& scene = runtime_.scene();
        const bool clearing_output =
            !use_editor_output_ && scene.active_output_camera.has_value() &&
            std::find(pending_delete_.begin(), pending_delete_.end(), *scene.active_output_camera) !=
                pending_delete_.end();

        const std::vector<genesis::NodeId> targets = pruneDeletionTargets(scene, pending_delete_);
        pending_delete_.clear();
        if (targets.empty()) {
            return;
        }

        // GPU resources (fluid grids, bindless samples) may still be referenced by
        // in-flight command buffers — drain the device before destroying them.
        renderer().waitIdle();

        for (const genesis::NodeId id : targets) {
            scene.deleteNode(id);
        }

        runtime_.fluidWorld().sync(scene);
        draw_commands_.clear();
        draw_batches_.clear();
        runtime_.updateTransforms();

        if (clearing_output) {
            use_editor_output_ = true;
        }
        markDirty();
    }

    void setSelection(std::vector<genesis::NodeId> ids) {
        selection_ = std::move(ids);
        rot_edit_node_ = 0;
        if (const genesis::NodeId primary = primarySelected()) {
            syncReparentFromSelection(primary);
        }
    }

    void selectNode(const genesis::NodeId id) {
        selection_.clear();
        if (id != 0) {
            selection_.push_back(id);
            syncReparentFromSelection(id);
        }
        rot_edit_node_ = 0;
    }

    void addToSelection(const genesis::NodeId id) {
        if (id == 0 || isSelected(id)) {
            return;
        }
        selection_.push_back(id);
        syncReparentFromSelection(id);
        rot_edit_node_ = 0;
    }

    void toggleSelected(const genesis::NodeId id) {
        if (id == 0) {
            return;
        }
        const auto it = std::find(selection_.begin(), selection_.end(), id);
        if (it != selection_.end()) {
            selection_.erase(it);
        } else {
            selection_.push_back(id);
            syncReparentFromSelection(id);
        }
        rot_edit_node_ = 0;
    }

    void syncReparentFromSelection(const genesis::NodeId id) {
        genesis::Scene& scene = runtime_.scene();
        if (const genesis::Node* n = scene.find(id)) {
            reparent_target_ = n->parent.value_or(0);
            reparent_preview_ =
                n->parent.has_value() && scene.find(*n->parent) ? scene.find(*n->parent)->name : "(Root)";
        }
    }

    void requestNewScene() {
        if (session_.dirty) {
            show_new_scene_confirm_ = true;
            return;
        }
        newBlankScene();
    }

    void newBlankScene() {
        runtime_.clear();
        runtime_.scene().createEmptyNode("Scene");
        runtime_.scene().active_output_camera.reset();
        session_.document_path = genesis::uniqueProjectPath("Untitled Scene");
        session_.dirty = true;
        selection_.clear();
        use_editor_output_ = true;
        resetEditorCamera();
        syncEditorAfterDocumentLoad();
        updateWindowTitle();
    }

    void requestOpenScene() {
        if (session_.dirty) {
            show_open_confirm_ = true;
            return;
        }
        openSceneDialog();
    }

    void openSceneDialog() {
        std::filesystem::path default_path = session_.document_path;
        if (default_path.empty() || session_.isBundledScenePath(default_path)) {
            default_path = genesis::editorProjectsDir();
        }

        genesis::openFileDialog(
            window().handle(),
            {{"GENESIS Scene", "genesis;gns;json"},
             {"GENESIS Package", "genesis"},
             {"GNS Package", "gns"},
             {"JSON Scene", "json"},
             {"All Files", "*"}},
            [this](std::filesystem::path path) {
                if (path.empty()) {
                    return;
                }
                try {
                    loadSceneDocument(path);
                } catch (const std::exception& ex) {
                    spdlog::error("Open scene failed: {}", ex.what());
                }
            },
            default_path);
    }

    void saveSceneAsDialog(bool exit_after_save = false) {
        std::filesystem::path default_path = session_.document_path;
        if (default_path.empty() || session_.isBundledScenePath(default_path)) {
            default_path = genesis::uniqueProjectPath("Untitled Scene");
        }

        genesis::saveFileDialog(
            window().handle(),
            {{"GENESIS Package", "genesis"},
             {"GNS Package", "gns"},
             {"JSON Scene", "json"},
             {"All Files", "*"}},
            [this, exit_after_save](std::filesystem::path path) {
                if (path.empty()) {
                    return;
                }
                if (!path.has_extension()) {
                    path += ".genesis";
                }
                try {
                    session_.saveDocument(runtime_, path);
                    updateWindowTitle();
                    if (exit_after_save) {
                        exit_confirmed_ = true;
                        window().setShouldClose(true);
                    }
                } catch (const std::exception& ex) {
                    spdlog::error("Save scene failed: {}", ex.what());
                }
            },
            default_path);
    }

    void saveAndExit() {
        if (session_.requiresSaveAs()) {
            saveSceneAsDialog(true);
            return;
        }
        try {
            session_.saveDocument(runtime_, session_.document_path);
            updateWindowTitle();
            exit_confirmed_ = true;
            window().setShouldClose(true);
        } catch (const std::exception& ex) {
            spdlog::error("Save scene failed: {}", ex.what());
        }
    }

    void saveCurrentDocument() {
        if (session_.requiresSaveAs()) {
            saveSceneAsDialog(false);
            return;
        }

        try {
            session_.saveDocument(runtime_, session_.document_path);
            updateWindowTitle();
        } catch (const std::exception& ex) {
            spdlog::error("Save scene failed: {}", ex.what());
        }
    }

    float refreshOutputAspect() {
        const int height = window().height();
        output_aspect_ = (height > 0) ? static_cast<float>(window().width()) / static_cast<float>(height) : 1.0f;
        return output_aspect_;
    }

    float outputAspect() const { return output_aspect_; }

    float renderAspect() const {
        if (use_editor_output_ && viewport_.h > 1.0f) {
            return viewport_.aspect();
        }
        return output_aspect_;
    }

    glm::mat4 outputViewMatrix() const {
        if (use_editor_output_) return editor_camera_.viewMatrix();
        if (const genesis::Node* cam_node = runtime_.scene().activeOutputCameraNode();
            cam_node && cam_node->camera.has_value()) {
            return cam_node->camera->viewMatrix(cam_node->world);
        }
        return editor_camera_.viewMatrix();
    }

    glm::mat4 outputProjectionMatrix(float aspect) const {
        if (use_editor_output_) return editor_camera_.projectionMatrix(aspect);
        if (const genesis::Node* cam_node = runtime_.scene().activeOutputCameraNode();
            cam_node && cam_node->camera.has_value()) {
            return cam_node->camera->projectionMatrix(aspect);
        }
        return editor_camera_.projectionMatrix(aspect);
    }

    glm::mat4 outputViewProjection(float aspect) const {
        return outputProjectionMatrix(aspect) * outputViewMatrix();
    }

    std::optional<genesis::NodeId> createParent() const {
        // New actors always spawn at the scene root. Use Hierarchy right-click to parent.
        (void)this;
        return std::nullopt;
    }

    void addPrimitiveMesh(const char* prefix, genesis::Mesh mesh) {
        static int counter = 1;
        const std::string name = std::string(prefix) + " " + std::to_string(counter++);
        const std::uint32_t mesh_id = runtime_.addMesh(name, std::move(mesh));
        const std::uint32_t material_id = runtime_.addDefaultPbrMaterial(name + " Material");
        selectNode(runtime_.scene().createMeshNode(name, mesh_id, std::nullopt, material_id));
        markDirty();
    }

    void addLightNode(const char* prefix, genesis::SceneLightType type) {
        static int counter = 1;
        const std::string name = std::string(prefix) + " " + std::to_string(counter++);
        const genesis::NodeId id = runtime_.scene().createLightNode(name, type, std::nullopt);
        if (genesis::Node* n = runtime_.scene().find(id)) {
            if (type == genesis::SceneLightType::Directional) {
                n->local.rotation =
                    glm::angleAxis(glm::radians(-52.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::angleAxis(glm::radians(38.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                n->light->intensity = 3.0f;
            } else if (type == genesis::SceneLightType::Point) {
                n->local.position = glm::vec3(0.0f, 2.0f, 0.0f);
                n->light->intensity = 8.0f;
                n->light->range = 12.0f;
            } else {
                n->local.position = glm::vec3(0.0f, 2.5f, 2.5f);
                n->local.rotation =
                    glm::angleAxis(glm::radians(-35.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                n->light->intensity = 12.0f;
                n->light->range = 16.0f;
            }
        }
        selectNode(id);
        markDirty();
    }

    glm::vec3 outputCameraPosition() const {
        if (!use_editor_output_) {
            if (std::optional<glm::vec3> pos = runtime_.outputCameraPosition()) return *pos;
        }
        return editor_camera_.transform.position;
    }

    void placeEditorCameraAt(const glm::vec3& eye, const glm::vec3& pivot) {
        camera_pivot_ = pivot;
        editor_camera_.transform.position = eye;
        editor_camera_.setRotation(genesis::cameraRotationFromLookAt(eye, pivot));
        genesis::extractYawPitchFromForward(editor_camera_.forward(), editor_camera_.yaw_radians,
                                            editor_camera_.pitch_radians);
    }

    float editorCameraPivotDistance() const {
        return std::max(glm::length(editor_camera_.transform.position - camera_pivot_), 0.35f);
    }

    void applyEditorCameraOrbit(const float dx, const float dy) {
        // Unreal-like Alt+LMB orbit around pivot (slower than stock UE feel).
        constexpr float kOrbit = 0.0032f;
        editor_camera_.yaw_radians -= dx * kOrbit;
        editor_camera_.pitch_radians -= dy * kOrbit;
        editor_camera_.pitch_radians = glm::clamp(editor_camera_.pitch_radians, -1.45f, 1.45f);
        editor_camera_.rebuildRotationFromYawPitch();
        const float dist = editorCameraPivotDistance();
        editor_camera_.transform.position = camera_pivot_ - editor_camera_.forward() * dist;
    }

    void applyEditorCameraPan(const float dx, const float dy) {
        // Unreal Alt+MMB / MMB track — intentionally slow for install-scale work.
        const float dist = editorCameraPivotDistance();
        const float pan_scale = dist * 0.00055f;
        const glm::vec3 delta = (-editor_camera_.right() * dx + editor_camera_.up() * dy) * pan_scale;
        editor_camera_.transform.position += delta;
        camera_pivot_ += delta;
    }

    void applyEditorCameraDolly(const float amount) {
        // Positive amount dollies in toward pivot (Unreal Alt+RMB / wheel).
        float dist = editorCameraPivotDistance();
        dist = std::clamp(dist * std::exp(-amount * 0.12f), 0.35f, 120.0f);
        editor_camera_.rebuildRotationFromYawPitch();
        editor_camera_.transform.position = camera_pivot_ - editor_camera_.forward() * dist;
    }

    void updateEditorCameraNavigation(const float dt) {
        const bool typing = ImGui::GetIO().WantTextInput;
        const bool alt = input().keyDown(SDL_SCANCODE_LALT) || input().keyDown(SDL_SCANCODE_RALT);
        const bool over_viewport =
            viewport_hovered_ || viewport_.contains(input().mouseX(), input().mouseY());
        const bool lmb = input().mouseButtonDown(SDL_BUTTON_LEFT);
        const bool mmb = input().mouseButtonDown(SDL_BUTTON_MIDDLE);
        const bool rmb = input().mouseButtonDown(SDL_BUTTON_RIGHT);
        const float dx = input().mouseDeltaX();
        const float dy = input().mouseDeltaY();
        const float wheel = input().mouseWheelY();

        // Unreal Maya-style: Alt+LMB orbit, Alt+MMB (or MMB) pan, Alt+RMB dolly.
        const bool orbiting = alt && lmb && !ImGuizmo::IsUsing();
        const bool panning = (mmb || (alt && mmb)) && !ImGuizmo::IsUsing();
        const bool dollying = alt && rmb && !ImGuizmo::IsUsing();
        const bool navigating = orbiting || panning || dollying;

        if (navigating && (navigating_active_ || over_viewport)) {
            navigating_active_ = true;
            use_editor_output_ = true;
            runtime_.scene().active_output_camera.reset();
            input().setMouseCaptured(window().handle(), true);
            fly_camera_active_ = false;

            if (orbiting) {
                applyEditorCameraOrbit(dx, dy);
            } else if (panning) {
                applyEditorCameraPan(dx, dy);
            } else if (dollying) {
                // Drag up/right → dolly in (match Unreal Alt+RMB feel, slowed).
                applyEditorCameraDolly((dx + dy) * 0.045f);
            }
        } else {
            navigating_active_ = false;
        }

        // Mouse wheel dolly when pointer is over the viewport (not captured by ImGui widgets).
        if (over_viewport && !ImGui::GetIO().WantCaptureMouse && std::abs(wheel) > 0.0f &&
            !ImGuizmo::IsUsing()) {
            use_editor_output_ = true;
            runtime_.scene().active_output_camera.reset();
            applyEditorCameraDolly(wheel * 0.85f);
        }

        // RMB fly look (no Alt) — slower / less twitchy than before.
        if (!alt && rmb && !ImGuizmo::IsUsing()) {
            if (fly_camera_active_ || over_viewport) {
                fly_camera_active_ = true;
            }
        } else if (!rmb || alt) {
            fly_camera_active_ = false;
        }

        if (fly_camera_active_ && !navigating_active_) {
            use_editor_output_ = true;
            input().setMouseCaptured(window().handle(), true);
            constexpr float kLook = 0.00155f;
            editor_camera_.yaw_radians += dx * kLook;
            editor_camera_.pitch_radians += dy * kLook;
            editor_camera_.pitch_radians = glm::clamp(editor_camera_.pitch_radians, -1.4f, 1.4f);
            editor_camera_.rebuildRotationFromYawPitch();

            const float speed = (input().keyDown(SDL_SCANCODE_LSHIFT) ? 3.2f : 1.35f);
            glm::vec3 move(0.0f);
            if (input().keyDown(SDL_SCANCODE_W)) move += editor_camera_.forward();
            if (input().keyDown(SDL_SCANCODE_S)) move -= editor_camera_.forward();
            if (input().keyDown(SDL_SCANCODE_D)) move += editor_camera_.right();
            if (input().keyDown(SDL_SCANCODE_A)) move -= editor_camera_.right();
            if (input().keyDown(SDL_SCANCODE_E)) move += glm::vec3(0.0f, 1.0f, 0.0f);
            if (input().keyDown(SDL_SCANCODE_Q)) move -= glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::length(move) > 0.0f) {
                const glm::vec3 step = glm::normalize(move) * speed * dt;
                editor_camera_.transform.position += step;
                camera_pivot_ += step;
            }
        } else if (!navigating_active_) {
            input().setMouseCaptured(window().handle(), false);

            // W/E/R switch tools even over the viewport; RMB+WASD is reserved for fly above.
            if (!ImGuizmo::IsUsing() && !typing && !alt) {
                if (input().keyPressed(SDL_SCANCODE_W)) gizmo_operation_ = ImGuizmo::TRANSLATE;
                if (input().keyPressed(SDL_SCANCODE_E)) gizmo_operation_ = ImGuizmo::ROTATE;
                if (input().keyPressed(SDL_SCANCODE_R)) gizmo_operation_ = ImGuizmo::SCALE;
            }
        }
    }

    void resetEditorCamera() {
        // Calm three-quarter view for soft rising circles + trails.
        const glm::vec3 target(0.0f, 0.85f, 0.0f);
        const glm::vec3 eye(2.55f, 1.55f, 3.35f);
        placeEditorCameraAt(eye, target);
        editor_camera_.fov_y_radians = glm::radians(40.0f);
        editor_camera_.near_plane = 0.1f;
        editor_camera_.far_plane = 250.0f;
    }

    void focusEditorCameraOnSelection() {
        if (selection_.empty()) return;

        runtime_.updateTransforms();

        glm::vec3 bounds_min(std::numeric_limits<float>::max());
        glm::vec3 bounds_max(std::numeric_limits<float>::lowest());
        bool any = false;

        for (const genesis::NodeId id : selection_) {
            const genesis::Node* node = runtime_.scene().find(id);
            if (!node) continue;
            glm::vec3 aabb_min{};
            glm::vec3 aabb_max{};
            const genesis::Mesh* mesh =
                node->mesh_resource.has_value() ? runtime_.mesh(*node->mesh_resource) : nullptr;
            if (!genesis::nodeWorldPickAabb(*node, mesh, {}, aabb_min, aabb_max)) {
                continue;
            }
            bounds_min = glm::min(bounds_min, aabb_min);
            bounds_max = glm::max(bounds_max, aabb_max);
            any = true;
        }
        if (!any) return;

        const glm::vec3 target = (bounds_min + bounds_max) * 0.5f;
        const float focus_distance = std::max(glm::length(bounds_max - bounds_min) * 1.35f, 1.15f);

        glm::vec3 offset = editor_camera_.transform.position - target;
        if (glm::length(offset) < 0.25f) {
            offset = glm::normalize(glm::vec3(1.0f, 0.55f, 1.0f));
        } else {
            offset = glm::normalize(offset);
        }

        placeEditorCameraAt(target + offset * focus_distance, target);
        use_editor_output_ = true;
        runtime_.scene().active_output_camera.reset();
    }

    void orientSceneOutputCameras(genesis::Scene& scene) {
        const glm::vec3 target(0.0f, 0.8f, 0.0f);
        auto orient = [&](genesis::Node& cam) {
            cam.local.rotation = genesis::cameraRotationFromLookAt(cam.local.position, target);
        };

        if (scene.active_output_camera.has_value()) {
            if (genesis::Node* cam = scene.find(*scene.active_output_camera)) {
                orient(*cam);
            }
        } else {
            for (genesis::Node& node : scene.nodes) {
                if (node.camera.has_value()) {
                    orient(node);
                }
            }
        }
        runtime_.updateTransforms();
    }

    void configureRoomOscDemoIfNeeded() {
        const std::string path = session_.document_path.filename().string();
        if (path.find("room_osc_particles") == std::string::npos) {
            return;
        }
        genesis::OscRoomPersonSettings room = osc_protocol_.roomPersonSettings();
        room.enabled = true;
        room.address = "/genesis/room/person";
        room.feet_emitter_path = "Scene/Particle System/Person Feet";
        room.wall_emitter_path = "Scene/Particle System/Wall Trace";
        room.ray_emitter_path = "Scene/Particle System/Center Ray";
        room.room_half_x = 4.0f;
        room.room_half_z = 4.0f;
        osc_protocol_.setRoomPersonSettings(room);
        genesis::OscParticleEmitSettings emit = osc_protocol_.particleEmitSettings();
        emit.emitter_path = "Scene/Particle System/Person Feet";
        emit.track_emitter_position = true;
        emit.burst_on_message = false;
        osc_protocol_.setParticleEmitSettings(emit);
        if (!osc_protocol_.isListening()) {
            std::string err;
            if (!osc_protocol_.startListening(&err) && !err.empty()) {
                spdlog::warn("Room OSC listen failed: {}", err);
            }
        }
    }

    void syncEditorAfterDocumentLoad() {
        use_editor_output_ = true;
        orientSceneOutputCameras(runtime_.scene());
        resetEditorCamera();
        configureRoomOscDemoIfNeeded();

        // Auto-frame the particle system (room demos: prefer Ambient Field for overview).
        genesis::NodeId focus = 0;
        for (const genesis::Node& node : runtime_.scene().nodes) {
            if (!node.particle_emitter.has_value()) {
                continue;
            }
            if (focus == 0) {
                focus = node.id;
            }
            if (node.name == "Ambient Field" || node.name == "Soft Circles") {
                focus = node.id;
                break;
            }
        }
        if (focus != 0) {
            selectNode(focus);
            focusEditorCameraOnSelection();
        }
    }

    genesis::EditorSession session_;
    genesis::SceneRuntime runtime_;
    genesis::ColorManagement color_mgmt_;
    genesis::Mesh grid_mesh_{};
    genesis::Camera editor_camera_{};
    glm::vec3 camera_pivot_{0.0f, 0.85f, 0.0f};
    genesis::ViewportRect viewport_{};
    bool use_editor_output_ = true;
    float output_aspect_ = 1.0f;
    bool viewport_hovered_ = false;
    bool fly_camera_active_ = false;
    bool navigating_active_ = false;
    bool ui_style_initialized_ = false;
    std::vector<genesis::NodeId> selection_;
    std::vector<genesis::NodeId> pending_delete_;
    struct MarqueeState {
        bool active = false;
        bool dragging = false;
        ImVec2 start{};
        ImVec2 current{};
    } marquee_{};
    static constexpr float kMarqueeDragThresholdPx = 5.0f;
    genesis::NodeId rot_edit_node_ = 0;
    float rot_euler_deg_[3] = {0.0f, 0.0f, 0.0f};
    genesis::NodeId reparent_target_ = 0;
    std::string reparent_preview_ = "(Root)";

    std::vector<genesis::SceneDrawCommand> draw_commands_;
    std::vector<genesis::SceneMeshBatch> draw_batches_;
    std::size_t last_draw_batches_ = 0;
    std::uint32_t last_culled_nodes_ = 0;
    ImGuizmo::OPERATION gizmo_operation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmo_mode_ = ImGuizmo::LOCAL;

    ImGuiID dockspace_id_ = 0;
    bool dock_layout_first_time_ = true;

    bool show_hierarchy_ = true;
    bool show_inspector_ = true;
    bool show_viewport_ = true;
    bool show_toolbar_ = true;
    bool show_viewport_help_ = false;
    bool show_timeline_ = false;
    bool show_protocol_ = false;
    bool show_audio_ = false;
    bool show_animation_graph_ = false;
    bool auto_key_ = false;
    bool show_debug_window_ = false;
    bool show_grid_ = true;
    bool show_actor_icons_ = true;
    bool game_view_mode_ = false; // Unreal-like: hide actor icons when true
    int edge_demo_frames_ = 0;

    bool show_new_scene_confirm_ = false;
    bool show_open_confirm_ = false;
    bool show_exit_confirm_ = false;
    bool exit_confirmed_ = false;
    int smoke_exit_frames_ = -1;

    genesis::OscProtocol osc_protocol_{};
    genesis::AudioReactive audio_reactive_{};
    double osc_time_ = 0.0;
    std::uint64_t osc_particle_cursor_ = 0;
    std::uint64_t osc_room_person_cursor_ = 0;
    float last_frame_dt_ = 1.0f / 60.0f;
    float look_time_seconds_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    try {
        genesis::ApplicationConfig config{};
        config.title = "GENESIS — Scene Editor";
        config.width = 1680;
        config.height = 1050;

        int smoke_frames = -1;
        std::filesystem::path open_path;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--smoke" || arg.rfind("--smoke=", 0) == 0) {
                smoke_frames = 3;
                if (arg.rfind("--smoke=", 0) == 0) {
                    smoke_frames = std::max(1, std::atoi(arg.c_str() + 8));
                }
            } else if (open_path.empty()) {
                open_path = arg;
            }
        }

        GltfViewerApp app(config);

        if (!open_path.empty()) {
            const std::filesystem::path asset_path = genesis::resolveAssetPath(open_path);
            const std::string ext = asset_path.extension().string();
            if (ext == ".gns" || ext == ".GNS" || ext == ".genesis" || ext == ".GENESIS" ||
                ext == ".json" || ext == ".scene") {
                app.loadSceneDocument(asset_path);
            } else {
                app.load(asset_path);
            }
        } else {
            app.loadDefaultScene();
        }

        if (smoke_frames > 0) {
            app.enableSmokeExit(smoke_frames);
        }
        return app.run();
    } catch (const std::exception& ex) {
        spdlog::critical("Fatal error: {}", ex.what());
#if defined(_WIN32)
        MessageBoxA(nullptr, ex.what(), "GENESIS Editor", MB_ICONERROR | MB_OK);
#endif
        return 1;
    }
}
