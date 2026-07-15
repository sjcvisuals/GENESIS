#include "assets/GridMesh.hpp"
#include "assets/PrimitiveMeshes.hpp"
#include "editor/EditorLayout.hpp"
#include "editor/EditorSession.hpp"
#include "editor/SceneViewport.hpp"
#include "engine/Application.hpp"
#include "platform/EditorPaths.hpp"
#include "platform/Paths.hpp"
#include "renderer/GridDrawPushConstants.hpp"
#include "scene/Camera.hpp"
#include "scene/Lighting.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneCamera.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include "scene/SceneDrawBatch.hpp"
#include "scene/ScenePicker.hpp"
#include "scene/MeshBounds.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr float kGridHalfExtentM = 12.0f;
constexpr int kGridDivisions = 24;

class GltfViewerApp final : public genesis::Application {
public:
    explicit GltfViewerApp(const genesis::ApplicationConfig& config)
        : genesis::Application(config), runtime_(renderer().context()) {
        setRenderStatsEnabled(true);
        genesis::ensureEditorDataDirs();
        session_.loadRecentList();

        grid_mesh_ = genesis::createEditorGridMesh(renderer().context(), kGridHalfExtentM, kGridDivisions);
        std::snprintf(path_input_, sizeof(path_input_), "%s",
                      genesis::editorProjectsDir().string().c_str());
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

    void loadDefaultDemoScene() {
        try {
            loadSceneDocument(session_.bundledScenePath("showcase.scene.json"));
            if (genesis::Node* hero = runtime_.findNodeByPath("Showcase/Stage/Hero Sphere")) {
                selectNode(hero->id);
            }
            return;
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to load showcase.scene.json, trying demo.scene.json: {}", ex.what());
        }

        try {
            loadSceneDocument(session_.bundledScenePath("demo.scene.json"));
            if (genesis::Node* sphere = runtime_.findNodeByPath("Demo Scene/Props/Sphere")) {
                selectNode(sphere->id);
            }
            return;
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to load demo.scene.json, using built-in demo: {}", ex.what());
        }

        newBlankScene();
    }

    void loadSceneDocument(const std::filesystem::path& path) {
        session_.loadDocument(runtime_, path);
        selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        syncEditorAfterDocumentLoad();
        updateWindowTitle();
    }

    void loadGnsPackage(const std::filesystem::path& path) {
        runtime_.loadGnsFile(path);
        session_.document_path = std::filesystem::absolute(path);
        session_.touchRecent(session_.document_path);
        session_.dirty = false;
        selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        syncEditorAfterDocumentLoad();
        updateWindowTitle();
    }

protected:
    void onUpdate(float dt) override {
        handleEditorShortcuts();

        if (window().shouldClose() && session_.dirty && !exit_confirmed_) {
            window().setShouldClose(false);
            show_exit_confirm_ = true;
        }

        if (input().mouseButtonDown(SDL_BUTTON_RIGHT) && viewport_hovered_ && !ImGuizmo::IsUsing()) {
            input().setMouseCaptured(true);
            const float sensitivity = 0.0025f;
            editor_camera_.yaw_radians += input().mouseDeltaX() * sensitivity;
            editor_camera_.pitch_radians += input().mouseDeltaY() * sensitivity;
            editor_camera_.pitch_radians = glm::clamp(editor_camera_.pitch_radians, -1.4f, 1.4f);
            editor_camera_.rebuildRotationFromYawPitch();

            const float speed = (input().keyDown(SDL_SCANCODE_LSHIFT) ? 6.0f : 2.5f);
            glm::vec3 move(0.0f);
            if (input().keyDown(SDL_SCANCODE_W)) move += editor_camera_.forward();
            if (input().keyDown(SDL_SCANCODE_S)) move -= editor_camera_.forward();
            if (input().keyDown(SDL_SCANCODE_D)) move += editor_camera_.right();
            if (input().keyDown(SDL_SCANCODE_A)) move -= editor_camera_.right();
            if (input().keyDown(SDL_SCANCODE_E)) move += glm::vec3(0.0f, 1.0f, 0.0f);
            if (input().keyDown(SDL_SCANCODE_Q)) move -= glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::length(move) > 0.0f) {
                editor_camera_.transform.position += glm::normalize(move) * speed * dt;
            }
        } else {
            input().setMouseCaptured(false);

            if (!ImGuizmo::IsUsing() && !viewport_hovered_) {
                if (input().keyPressed(SDL_SCANCODE_W)) gizmo_operation_ = ImGuizmo::TRANSLATE;
                if (input().keyPressed(SDL_SCANCODE_E)) gizmo_operation_ = ImGuizmo::ROTATE;
                if (input().keyPressed(SDL_SCANCODE_R)) gizmo_operation_ = ImGuizmo::SCALE;
            }
        }

        const float aspect = refreshOutputAspect();
        const glm::mat4 vp = outputViewProjection(renderAspect());
        const genesis::FrameUniforms frame =
            genesis::buildFrameUniforms(runtime_.scene(), &vp[0][0], outputCameraPosition());
        renderer().setFrameUniforms(frame);
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

        const genesis::GridDrawPushConstants grid_pc =
            genesis::makeGridDrawPushConstants(nullptr, 0.28f, 0.32f, 0.38f, 0.95f);
        renderer().drawGrid(grid_mesh_, grid_pc);

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
        if (show_debug_window_) drawDebugWindow();

        drawFileModals(scene);

        if (show_viewport_) {
            ImVec2 rect_min{};
            ImVec2 rect_max{};
            genesis::drawViewportInputOverlay(
                dockspace_id_, show_viewport_, rect_min, rect_max, viewport_hovered_,
                [&]() { drawViewportGizmo(scene); });
            viewport_.x = rect_min.x;
            viewport_.y = rect_min.y;
            viewport_.w = std::max(rect_max.x - rect_min.x, 1.0f);
            viewport_.h = std::max(rect_max.y - rect_min.y, 1.0f);
            viewport_.hovered = viewport_hovered_;

            handleViewportPick(scene);
        } else {
            viewport_.w = 0.0f;
            viewport_.h = 0.0f;
            viewport_hovered_ = false;
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
            open_open_modal_ = true;
        }
        if (ctrl && input().keyPressed(SDL_SCANCODE_N)) {
            requestNewScene();
        }
        if (input().keyPressed(SDL_SCANCODE_F)) {
            focusEditorCameraOnSelection();
        }
    }

    void initEditorStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.WindowPadding = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(10.0f, 7.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 5.0f);
        style.IndentSpacing = 18.0f;

        ImVec4* colors = style.Colors;
        const ImVec4 bg_dark(0.09f, 0.10f, 0.12f, 1.0f);
        const ImVec4 bg_panel(0.12f, 0.13f, 0.16f, 0.98f);
        const ImVec4 accent(0.22f, 0.48f, 0.82f, 1.0f);
        const ImVec4 accent_dim(0.18f, 0.36f, 0.62f, 1.0f);

        colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.60f, 1.0f);
        colors[ImGuiCol_WindowBg] = bg_panel;
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
        colors[ImGuiCol_PopupBg] = bg_panel;
        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.28f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.26f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.30f, 1.0f);
        colors[ImGuiCol_TitleBg] = bg_dark;
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.15f, 0.18f, 1.0f);
        colors[ImGuiCol_MenuBarBg] = bg_dark;
        colors[ImGuiCol_Header] = accent_dim;
        colors[ImGuiCol_HeaderHovered] = accent;
        colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.54f, 0.88f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = accent_dim;
        colors[ImGuiCol_ButtonActive] = accent;
        colors[ImGuiCol_CheckMark] = accent;
        colors[ImGuiCol_SliderGrab] = accent_dim;
        colors[ImGuiCol_SliderGrabActive] = accent;
        colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.26f, 0.30f, 1.0f);
        colors[ImGuiCol_ResizeGrip] = accent_dim;
        colors[ImGuiCol_ResizeGripHovered] = accent;
        colors[ImGuiCol_ResizeGripActive] = accent;
        colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.15f, 0.18f, 1.0f);
        colors[ImGuiCol_TabHovered] = accent_dim;
        colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.38f, 0.64f, 1.0f);
        colors[ImGuiCol_DockingPreview] = ImVec4(0.22f, 0.48f, 0.82f, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void drawMenuBar(genesis::Scene& scene) {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) requestNewScene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) open_open_modal_ = true;
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveCurrentDocument();
            if (ImGui::MenuItem("Save Scene As...")) {
                open_save_as_modal_ = true;
                std::snprintf(path_input_, sizeof(path_input_), "%s",
                              session_.document_path.empty()
                                  ? genesis::uniqueProjectPath("Untitled Scene").string().c_str()
                                  : session_.document_path.string().c_str());
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Bundled")) {
                if (ImGui::MenuItem("Showcase")) {
                    try {
                        loadSceneDocument(session_.bundledScenePath("showcase.scene.json"));
                    } catch (const std::exception& ex) {
                        spdlog::error("Load showcase failed: {}", ex.what());
                    }
                }
                if (ImGui::MenuItem("Demo")) {
                    try {
                        loadSceneDocument(session_.bundledScenePath("demo.scene.json"));
                    } catch (const std::exception& ex) {
                        spdlog::error("Load demo failed: {}", ex.what());
                    }
                }
                ImGui::EndMenu();
            }
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
            if (ImGui::MenuItem("Exit")) {
                if (session_.dirty) {
                    show_exit_confirm_ = true;
                } else {
                    window().setShouldClose(true);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_);
            ImGui::MenuItem("Inspector", nullptr, &show_inspector_);
            ImGui::MenuItem("Viewport", nullptr, &show_viewport_);
            ImGui::MenuItem("Toolbar", nullptr, &show_toolbar_);
            ImGui::MenuItem("Viewport Help", nullptr, &show_viewport_help_);
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

        ImGui::TextColored(ImVec4(0.55f, 0.70f, 0.95f, 1.0f), "SCENE");
        ImGui::Separator();
        ImGui::TextUnformatted("Create");
        if (ImGui::Button("Empty", ImVec2(62, 0))) {
            selectNode(scene.createEmptyNode("Empty Node", createParent()));
            markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cube", ImVec2(62, 0))) {
            addPrimitiveMesh("Cube", genesis::createCubeMesh(renderer().context()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Sphere", ImVec2(62, 0))) {
            addPrimitiveMesh("Sphere", genesis::createSphereMesh(renderer().context()));
        }
        if (ImGui::Button("Camera", ImVec2(128, 0))) {
            selectNode(scene.createCameraNode("Camera", createParent()));
            markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Light", ImVec2(128, 0))) {
            addLightNode("Directional Light", genesis::SceneLightType::Directional);
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Selection");

        ImGui::BeginDisabled(selected_ == 0);
        if (ImGui::Button("Duplicate")) {
            if (const genesis::NodeId copy_id = scene.duplicateNode(selected_)) {
                selectNode(copy_id);
                markDirty();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            const genesis::NodeId to_delete = selected_;
            const bool was_output = !use_editor_output_ && scene.active_output_camera == to_delete;
            if (scene.deleteNode(to_delete)) {
                selectNode(0);
                if (was_output) use_editor_output_ = true;
                markDirty();
            }
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.70f, 0.95f, 1.0f), "OUTLINER");

        auto drawNode = [&](auto&& self, genesis::NodeId id) -> void {
            const genesis::Node* n = scene.find(id);
            if (!n) return;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
            if (n->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            if (selected_ == id) flags |= ImGuiTreeNodeFlags_Selected;

            std::string label = n->name;
            if (n->mesh_resource.has_value()) label += "   M";
            if (n->camera.has_value()) label += "   C";
            if (n->light.has_value()) label += "   L";

            const bool open = ImGui::TreeNodeEx((void*)(uintptr_t)n->id, flags, "%s", label.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                selectNode(id);
            }
            if (open) {
                for (auto child : n->children) self(self, child);
                ImGui::TreePop();
            }
        };

        for (auto root : scene.roots) drawNode(drawNode, root);
        ImGui::End();
    }

    void drawInspectorPanel(genesis::Scene& scene) {
        ImGui::Begin("Inspector", &show_inspector_);

        ImGui::TextColored(ImVec4(0.55f, 0.70f, 0.95f, 1.0f), "INSPECTOR");
        ImGui::Separator();

        if (selected_ == 0) {
            ImGui::TextWrapped("Select an actor in the viewport or outliner.");
            ImGui::Spacing();
            ImGui::TextDisabled("LMB pick  |  RMB+WASD fly  |  W/E/R gizmo");
        } else if (genesis::Node* n = scene.find(selected_)) {
            char name_buf[128]{};
            std::snprintf(name_buf, sizeof(name_buf), "%s", n->name.c_str());
            if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
                n->name = name_buf;
                markDirty();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Transform");

            bool changed = false;
            if (ImGui::Checkbox("Visible", &n->visible)) changed = true;
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

            glm::vec3 scale_pct = n->local.scale * 100.0f;
            if (ImGui::DragFloat3("Scale (%)", &scale_pct.x, 1.0f, 0.01f, 100000.0f, "%.1f")) {
                n->local.scale = scale_pct / 100.0f;
                changed = true;
            }

            if (rot_edit_node_ != selected_) {
                rot_edit_node_ = selected_;
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

            if (changed) {
                runtime_.updateTransforms();
                markDirty();
            }

            drawMaterialInspector(*n);
            drawCameraInspector(scene, *n);
            drawLightInspector(*n);
        } else {
            ImGui::TextUnformatted("Selected node not found.");
        }
        ImGui::End();
    }

    void drawMaterialInspector(genesis::Node& n) {
        if (!n.mesh_resource.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Material");
        if (!n.material_resource.has_value()) {
            if (ImGui::Button("Assign Default Material")) {
                n.has_material = true;
                n.material_resource = 0;
                markDirty();
            }
            return;
        }
        if (*n.material_resource >= runtime_.materials().size()) return;

        genesis::SceneMaterial& mat = *runtime_.material(*n.material_resource);
        ImGui::Text("%s", runtime_.materialNames()[*n.material_resource].c_str());
        if (ImGui::ColorEdit4("Base color", &mat.base_color.x)) markDirty();
        if (ImGui::DragFloat("Metallic", &mat.metallic, 0.01f, 0.0f, 1.0f)) markDirty();
        if (ImGui::DragFloat("Roughness", &mat.roughness, 0.01f, 0.0f, 1.0f)) markDirty();
    }

    void drawCameraInspector(genesis::Scene& scene, genesis::Node& n) {
        if (!n.camera.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Camera");
        genesis::SceneCamera& cam = *n.camera;
        float fov_deg = glm::degrees(cam.fov_y_radians);
        if (ImGui::DragFloat("FOV (deg)", &fov_deg, 0.5f, 1.0f, 170.0f)) {
            cam.fov_y_radians = glm::radians(fov_deg);
            markDirty();
        }
        if (ImGui::Button("Use as Output Camera")) {
            use_editor_output_ = false;
            scene.setActiveOutputCamera(n.id);
        }
    }

    void drawLightInspector(genesis::Node& n) {
        if (!n.light.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Light");
        genesis::SceneLight& light = *n.light;
        if (ImGui::Checkbox("Enabled", &light.enabled)) markDirty();
        if (ImGui::ColorEdit3("Color", &light.color.x)) markDirty();
        if (ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f)) markDirty();
    }

    void drawToolbarPanel(genesis::Scene& scene) {
        ImGui::Begin("Toolbar", &show_toolbar_);

        if (ImGui::RadioButton("Move (W)", gizmo_operation_ == ImGuizmo::TRANSLATE)) {
            gizmo_operation_ = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", gizmo_operation_ == ImGuizmo::ROTATE)) {
            gizmo_operation_ = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", gizmo_operation_ == ImGuizmo::SCALE)) {
            gizmo_operation_ = ImGuizmo::SCALE;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", gizmo_mode_ == ImGuizmo::LOCAL)) gizmo_mode_ = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton("World", gizmo_mode_ == ImGuizmo::WORLD)) gizmo_mode_ = ImGuizmo::WORLD;

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (selected_ != 0 && scene.find(selected_)) {
            ImGui::Text("Selected: %s", scene.find(selected_)->name.c_str());
        } else {
            ImGui::TextUnformatted("No selection");
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        const genesis::FrameStats& stats = frameStats();
        ImGui::Text("FPS %.0f  |  %u tris  |  %zu batches  |  %u culled", timer().fps(), stats.triangles,
                    last_draw_batches_, last_culled_nodes_);

        ImGui::End();
    }

    void drawViewportHelpPanel() {
        ImGui::Begin("Viewport Help", &show_viewport_help_);
        ImGui::TextUnformatted("LMB: select actor");
        ImGui::TextUnformatted("Drag gizmo: transform");
        ImGui::TextUnformatted("RMB + WASD/QE: fly (in viewport)");
        ImGui::TextUnformatted("F: focus camera on selection");
        const char* camera_label = use_editor_output_ ? "Editor Camera" : "Scene Camera";
        const glm::vec3 cam_pos = outputCameraPosition();
        ImGui::Text("Camera: %s", camera_label);
        ImGui::Text("Eye: %.1f, %.1f, %.1f m", cam_pos.x, cam_pos.y, cam_pos.z);
        if (selected_ != 0) {
            if (const genesis::Node* n = runtime_.scene().find(selected_)) {
                ImGui::Text("Active: %s", n->name.c_str());
            }
        }
        ImGui::TextDisabled("%u visible meshes", frameStats().visible_mesh_nodes);
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

        if (open_open_modal_) {
            ImGui::OpenPopup("Open Scene");
            open_open_modal_ = false;
        }
        if (open_save_as_modal_) {
            ImGui::OpenPopup("Save Scene As");
            open_save_as_modal_ = false;
        }
        if (show_new_scene_confirm_) {
            ImGui::OpenPopup("Discard Changes?");
            show_new_scene_confirm_ = false;
        }
        if (show_exit_confirm_) {
            ImGui::OpenPopup("Exit GENESIS?");
            show_exit_confirm_ = false;
        }

        if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Path", path_input_, sizeof(path_input_));
            ImGui::TextDisabled("Example: %s", genesis::editorProjectsDir().string().c_str());
            if (!session_.recent_files.empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted("Recent");
                for (const std::filesystem::path& recent : session_.recent_files) {
                    if (ImGui::Selectable(recent.string().c_str())) {
                        std::snprintf(path_input_, sizeof(path_input_), "%s", recent.string().c_str());
                    }
                }
            }
            if (ImGui::Button("Open")) {
                try {
                    loadSceneDocument(path_input_);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& ex) {
                    spdlog::error("Open scene failed: {}", ex.what());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Path", path_input_, sizeof(path_input_));
            ImGui::TextDisabled("Saved under your GENESIS projects folder by default.");
            if (ImGui::Button("Save")) {
                try {
                    session_.saveDocument(runtime_, path_input_);
                    updateWindowTitle();
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& ex) {
                    spdlog::error("Save scene failed: {}", ex.what());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
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

        if (ImGui::BeginPopupModal("Exit GENESIS?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save changes before exiting?");
            if (ImGui::Button("Save")) {
                saveCurrentDocument();
                exit_confirmed_ = true;
                window().setShouldClose(true);
                ImGui::CloseCurrentPopup();
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
        if (selected_ == 0) return;
        genesis::Node* node = scene.find(selected_);
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
        ImGuizmo::SetID(static_cast<int>(selected_));

        if (ImGuizmo::Manipulate(view_m, proj_m, gizmo_operation_, gizmo_mode_, matrix_m)) {
            if (applyGizmoWorldToLocal(scene, *node, matrix_m)) {
                runtime_.updateTransforms();
                rot_edit_node_ = 0;
                markDirty();
            }
        }
    }

    bool applyGizmoWorldToLocal(genesis::Scene& scene, genesis::Node& node, const float* matrix_m) {
        float translation[3]{};
        float rotation[3]{};
        float scale[3]{};
        float world_f[16]{};
        std::memcpy(world_f, matrix_m, sizeof(world_f));

        ImGuizmo::DecomposeMatrixToComponents(world_f, translation, rotation, scale);
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, world_f);

        const glm::mat4 new_world = genesis::float16ToMat4(world_f);
        const glm::mat4 new_local_mat = genesis::worldToLocalMatrix(scene, node, new_world);

        float local_f[16]{};
        genesis::mat4ToFloat16(new_local_mat, local_f);
        ImGuizmo::DecomposeMatrixToComponents(local_f, translation, rotation, scale);

        node.local.position = glm::vec3(translation[0], translation[1], translation[2]);
        node.local.scale = glm::vec3(scale[0], scale[1], scale[2]);

        const glm::vec3 euler_rad = glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2]));
        const glm::quat qx = glm::angleAxis(euler_rad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::quat qy = glm::angleAxis(euler_rad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat qz = glm::angleAxis(euler_rad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        node.local.rotation = glm::normalize(qx * qy * qz);
        return true;
    }

    void handleViewportPick(genesis::Scene& scene) {
        if (!viewport_hovered_) return;
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
        if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) return;

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        pickAtViewportMouse(mouse, scene);
    }

    void pickAtViewportMouse(const ImVec2& mouse, genesis::Scene& scene) {
        (void)scene;
        if (!viewport_.contains(mouse.x, mouse.y)) return;

        const float aspect = renderAspect();
        const float local_x = mouse.x - viewport_.x;
        const float local_y = mouse.y - viewport_.y;

        const genesis::ScreenRay screen_ray = genesis::rayFromScreen(
            outputViewMatrix(), outputProjectionMatrix(aspect), aspect, local_x, local_y, viewport_.w,
            viewport_.h);

        const genesis::PickRay ray{screen_ray.origin, screen_ray.direction};

        genesis::ScenePickParams params{};
        params.meshes_only = true;
        params.exclude_cameras = true;
        if (std::optional<genesis::ScenePickResult> picked = runtime_.pickNodeAtRay(ray, params)) {
            selectNode(picked->node_id);
        } else {
            selectNode(0);
        }
    }

    void selectNode(const genesis::NodeId id) {
        selected_ = id;
        rot_edit_node_ = 0;
        genesis::Scene& scene = runtime_.scene();
        if (id == 0) return;
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

        const genesis::NodeId root = runtime_.scene().createEmptyNode("Scene");
        const genesis::NodeId light =
            runtime_.scene().createLightNode("Directional Light", genesis::SceneLightType::Directional, root);
        if (genesis::Node* n = runtime_.scene().find(light)) {
            n->local.rotation =
                glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            n->light->color = glm::vec3(1.0f, 0.98f, 0.94f);
            n->light->intensity = 1.0f;
        }

        const genesis::NodeId camera = runtime_.scene().createCameraNode("Main Camera", root);
        if (genesis::Node* n = runtime_.scene().find(camera)) {
            n->local.position = glm::vec3(0.0f, 2.5f, 7.0f);
            n->camera->fov_y_radians = glm::radians(55.0f);
        }

        runtime_.scene().setActiveOutputCamera(camera);
        session_.document_path = genesis::uniqueProjectPath("Untitled Scene");
        session_.dirty = true;
        selectNode(root);
        use_editor_output_ = true;
        syncEditorAfterDocumentLoad();
        updateWindowTitle();
    }

    void saveCurrentDocument() {
        if (session_.requiresSaveAs()) {
            open_save_as_modal_ = true;
            std::snprintf(path_input_, sizeof(path_input_), "%s",
                          session_.document_path.empty()
                              ? genesis::uniqueProjectPath("Untitled Scene").string().c_str()
                              : session_.document_path.string().c_str());
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
        if (selected_ == 0) return std::nullopt;
        return selected_;
    }

    void addPrimitiveMesh(const char* prefix, genesis::Mesh mesh) {
        static int counter = 1;
        const std::string name = std::string(prefix) + " " + std::to_string(counter++);
        const std::uint32_t mesh_id = runtime_.addMesh(name, std::move(mesh));
        const std::uint32_t material_id = runtime_.addMaterial(name + " Material", genesis::SceneMaterial{});
        selectNode(runtime_.scene().createMeshNode(name, mesh_id, createParent(), material_id));
        markDirty();
    }

    void addLightNode(const char* prefix, genesis::SceneLightType type) {
        static int counter = 1;
        const std::string name = std::string(prefix) + " " + std::to_string(counter++);
        selectNode(runtime_.scene().createLightNode(name, type, createParent()));
        markDirty();
    }

    glm::vec3 outputCameraPosition() const {
        if (!use_editor_output_) {
            if (std::optional<glm::vec3> pos = runtime_.outputCameraPosition()) return *pos;
        }
        return editor_camera_.transform.position;
    }

    void resetEditorCamera() {
        const glm::vec3 target(0.0f, 0.8f, 0.0f);
        const glm::vec3 eye(8.0f, 4.5f, 9.0f);
        editor_camera_.transform.position = eye;
        editor_camera_.setRotation(genesis::cameraRotationFromLookAt(eye, target));
        genesis::extractYawPitchFromForward(editor_camera_.forward(), editor_camera_.yaw_radians,
                                            editor_camera_.pitch_radians);
        editor_camera_.fov_y_radians = glm::radians(55.0f);
        editor_camera_.near_plane = 0.1f;
        editor_camera_.far_plane = 250.0f;
    }

    void focusEditorCameraOnSelection() {
        if (selected_ == 0) return;
        const genesis::Node* node = runtime_.scene().find(selected_);
        if (!node) return;

        runtime_.updateTransforms();

        glm::vec3 target = glm::vec3(node->world[3]);
        float focus_distance = 3.0f;

        if (node->mesh_resource.has_value()) {
            const std::uint32_t mesh_id = *node->mesh_resource;
            if (mesh_id < runtime_.meshes().size()) {
                const genesis::Mesh& mesh = runtime_.meshes()[mesh_id];
                glm::vec3 center{};
                glm::vec3 half_extent{};
                genesis::worldBoundsFromMesh(node->world, mesh.local_aabb_min, mesh.local_aabb_max, center,
                                               half_extent);
                target = center;
                focus_distance = std::max(glm::length(half_extent) * 3.5f, 1.5f);
            }
        }

        glm::vec3 offset = editor_camera_.transform.position - target;
        if (glm::length(offset) < 0.25f) {
            offset = glm::normalize(glm::vec3(1.0f, 0.55f, 1.0f));
        } else {
            offset = glm::normalize(offset);
        }

        editor_camera_.transform.position = target + offset * focus_distance;
        editor_camera_.setRotation(
            genesis::cameraRotationFromLookAt(editor_camera_.transform.position, target));
        genesis::extractYawPitchFromForward(editor_camera_.forward(), editor_camera_.yaw_radians,
                                            editor_camera_.pitch_radians);
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

    void syncEditorAfterDocumentLoad() {
        use_editor_output_ = true;
        orientSceneOutputCameras(runtime_.scene());
        resetEditorCamera();
    }

    genesis::EditorSession session_;
    genesis::SceneRuntime runtime_;
    genesis::Mesh grid_mesh_{};
    genesis::Camera editor_camera_{};
    genesis::ViewportRect viewport_{};
    bool use_editor_output_ = true;
    float output_aspect_ = 1.0f;
    bool viewport_hovered_ = false;
    bool ui_style_initialized_ = false;
    genesis::NodeId selected_ = 0;
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
    bool show_viewport_help_ = true;
    bool show_debug_window_ = false;

    bool open_open_modal_ = false;
    bool open_save_as_modal_ = false;
    bool show_new_scene_confirm_ = false;
    bool show_exit_confirm_ = false;
    bool exit_confirmed_ = false;
    char path_input_[512]{};
};

} // namespace

int main(int argc, char** argv) {
    try {
        genesis::ApplicationConfig config{};
        config.title = "GENESIS — Scene Editor";
        config.width = 1680;
        config.height = 1050;

        GltfViewerApp app(config);

        if (argc >= 2) {
            const std::filesystem::path arg_path = argv[1];
            if (arg_path.extension() == ".gns") {
                app.loadGnsPackage(genesis::resolveAssetPath(arg_path));
            } else {
                const std::filesystem::path asset_path = genesis::resolveAssetPath(arg_path);
                const std::string ext = asset_path.extension().string();
                if (ext == ".json" || ext == ".scene") {
                    app.loadSceneDocument(asset_path);
                } else {
                    app.load(asset_path);
                }
            }
        } else {
            app.loadDefaultDemoScene();
        }
        return app.run();
    } catch (const std::exception& ex) {
        spdlog::critical("Fatal error: {}", ex.what());
        return 1;
    }
}
