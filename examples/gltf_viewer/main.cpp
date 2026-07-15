#include "assets/GridMesh.hpp"
#include "assets/PrimitiveMeshes.hpp"
#include "editor/SceneViewport.hpp"
#include "engine/Application.hpp"
#include "platform/Paths.hpp"
#include "renderer/GridDrawPushConstants.hpp"
#include "scene/Camera.hpp"
#include "scene/Lighting.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneCamera.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include "scene/SceneRuntime.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr float kLeftPanelW = 260.0f;
constexpr float kRightPanelW = 320.0f;
constexpr float kStatusBarH = 30.0f;
constexpr float kGridHalfExtentM = 5.0f;
constexpr int kGridDivisions = 10;

class GltfViewerApp final : public genesis::Application {
public:
    explicit GltfViewerApp(const genesis::ApplicationConfig& config)
        : genesis::Application(config), runtime_(renderer().context()) {
        setRenderStatsEnabled(true);
        grid_mesh_ = genesis::createEditorGridMesh(renderer().context(), kGridHalfExtentM, kGridDivisions);
    }

    void load(const std::filesystem::path& path) {
        runtime_.loadGltf(path);
        loaded_path_ = path.string();
        selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        use_editor_output_ = true;
        runtime_.scene().active_output_camera.reset();
        resetEditorCamera();
    }

    void loadDefaultDemoScene() {
        try {
            loadSceneDocument(genesis::resolveAssetPath("assets/scenes/demo.scene.json"));
            if (genesis::Node* sphere = runtime_.findNodeByPath("Demo Scene/Props/Sphere")) {
                selectNode(sphere->id);
            }
            return;
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to load demo.scene.json, using built-in demo: {}", ex.what());
        }

        runtime_.loadDemo();
        loaded_path_ = "<built-in demo>";
        if (genesis::Node* light = runtime_.findNodeByPath("Demo Scene/Directional Light")) {
            selectNode(light->id);
        } else {
            selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        }
        use_editor_output_ = true;
        runtime_.scene().active_output_camera.reset();
        resetEditorCamera();
    }

    void saveSceneDocument(const std::filesystem::path& path) { runtime_.saveDocument(path); }

    void loadSceneDocument(const std::filesystem::path& path) {
        runtime_.loadDocument(path);
        loaded_path_ = path.string();
        selectNode(runtime_.scene().roots.empty() ? 0 : runtime_.scene().roots.front());
        syncEditorAfterDocumentLoad();
    }

protected:
    void onUpdate(float dt) override {
        if (input().mouseButtonDown(SDL_BUTTON_RIGHT)) {
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

            if (!ImGuizmo::IsUsing()) {
                if (input().keyPressed(SDL_SCANCODE_W)) gizmo_operation_ = ImGuizmo::TRANSLATE;
                if (input().keyPressed(SDL_SCANCODE_E)) gizmo_operation_ = ImGuizmo::ROTATE;
                if (input().keyPressed(SDL_SCANCODE_R)) gizmo_operation_ = ImGuizmo::SCALE;
            }
        }

        const float aspect = viewport_.aspect();
        const glm::mat4 vp = outputViewProjection(aspect);
        const genesis::FrameUniforms frame =
            genesis::buildFrameUniforms(runtime_.scene(), &vp[0][0], outputCameraPosition());
        renderer().setFrameUniforms(frame);
    }

    void onDraw() override {
        draw_commands_.clear();
        genesis::SceneRenderStats render_stats{};
        runtime_.collectDrawCommands(draw_commands_, &render_stats);

        genesis::FrameStats& stats = frameStats();
        stats.draw_calls = render_stats.draw_calls;
        stats.triangles = render_stats.triangles;
        stats.visible_mesh_nodes = render_stats.visible_mesh_nodes;
        stats.scene_nodes = render_stats.scene_nodes;
        stats.active_lights = render_stats.active_lights;

        const genesis::GridDrawPushConstants grid_pc =
            genesis::makeGridDrawPushConstants(nullptr, 0.28f, 0.32f, 0.38f, 0.95f);
        renderer().drawGrid(grid_mesh_, grid_pc);

        for (const genesis::SceneDrawCommand& cmd : draw_commands_) {
            if (cmd.node_id == selected_) continue;
            renderer().drawMesh(*cmd.mesh, cmd.constants, cmd.base_color_texture);
        }
        for (const genesis::SceneDrawCommand& cmd : draw_commands_) {
            if (cmd.node_id != selected_) continue;
            renderer().drawMesh(*cmd.mesh, cmd.constants, cmd.base_color_texture);
        }
    }

    void onImGui() override {
        if (!ui_style_initialized_) {
            initEditorStyle();
            ui_style_initialized_ = true;
        }

        ImGuizmo::BeginFrame();
        genesis::Scene& scene = runtime_.scene();

        layoutViewport(scene);
        drawMenuBar(scene);
        drawHierarchyPanel(scene);
        drawInspectorPanel(scene);
        drawStatusBar(scene);
        drawViewportOverlay();
        drawViewportGizmo(scene);
    }

private:
    void initEditorStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 0.94f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.42f, 0.72f, 0.55f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.48f, 0.80f, 0.75f);
    }

    void layoutViewport(genesis::Scene& scene) {
        const ImGuiViewport* main_vp = ImGui::GetMainViewport();
        const float top = ImGui::GetFrameHeight();
        const ImVec2 display = main_vp->Size;

        viewport_.x = kLeftPanelW;
        viewport_.y = top;
        viewport_.w = std::max(display.x - kLeftPanelW - kRightPanelW, 64.0f);
        viewport_.h = std::max(display.y - top - kStatusBarH, 64.0f);
        viewport_.hovered = false;

        ImGui::SetNextWindowPos(ImVec2(viewport_.x, viewport_.y));
        ImGui::SetNextWindowSize(ImVec2(viewport_.w, viewport_.h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::Begin("##Viewport", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::InvisibleButton("##ViewportInput", ImVec2(viewport_.w, viewport_.h));
        viewport_.hovered = ImGui::IsItemHovered();
        if (viewport_.hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() &&
            !ImGuizmo::IsUsing()) {
            pickAtViewportMouse(ImGui::GetIO().MousePos, scene);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void drawMenuBar(genesis::Scene& scene) {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load Demo Scene")) {
                try {
                    loadSceneDocument(genesis::resolveAssetPath("assets/scenes/demo.scene.json"));
                } catch (const std::exception& ex) {
                    spdlog::error("Load scene failed: {}", ex.what());
                }
            }
            if (ImGui::MenuItem("Save Scene")) {
                try {
                    saveSceneDocument(genesis::resolveAssetPath("assets/scenes/demo.scene.json"));
                } catch (const std::exception& ex) {
                    spdlog::error("Save scene failed: {}", ex.what());
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
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

        ImGui::EndMainMenuBar();
    }

    void drawHierarchyPanel(genesis::Scene& scene) {
        const float top = ImGui::GetFrameHeight();
        const ImVec2 display = ImGui::GetMainViewport()->Size;

        ImGui::SetNextWindowPos(ImVec2(0.0f, top));
        ImGui::SetNextWindowSize(ImVec2(kLeftPanelW, display.y - top - kStatusBarH));
        ImGui::Begin("Hierarchy", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (ImGui::Button("Empty")) {
            selectNode(scene.createEmptyNode("Empty Node", createParent()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cube")) {
            addPrimitiveMesh("Cube", genesis::createCubeMesh(renderer().context()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Sphere")) {
            addPrimitiveMesh("Sphere", genesis::createSphereMesh(renderer().context()));
        }

        if (ImGui::Button("Camera")) selectNode(scene.createCameraNode("Camera", createParent()));
        ImGui::SameLine();
        if (ImGui::Button("Light")) addLightNode("Directional Light", genesis::SceneLightType::Directional);

        ImGui::BeginDisabled(selected_ == 0);
        if (ImGui::Button("Duplicate")) {
            if (const genesis::NodeId copy_id = scene.duplicateNode(selected_)) selectNode(copy_id);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            const genesis::NodeId to_delete = selected_;
            const bool was_output = !use_editor_output_ && scene.active_output_camera == to_delete;
            if (scene.deleteNode(to_delete)) {
                selectNode(0);
                if (was_output) use_editor_output_ = true;
            }
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        auto drawNode = [&](auto&& self, genesis::NodeId id) -> void {
            const genesis::Node* n = scene.find(id);
            if (!n) return;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
            if (n->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            if (selected_ == id) flags |= ImGuiTreeNodeFlags_Selected;

            std::string label = n->name;
            if (n->mesh_resource.has_value()) label += "  [mesh]";
            if (n->camera.has_value()) label += "  [cam]";
            if (n->light.has_value()) label += "  [light]";

            const bool open = ImGui::TreeNodeEx((void*)(uintptr_t)n->id, flags, "%s", label.c_str());
            if (ImGui::IsItemClicked()) selectNode(id);
            if (open) {
                for (auto child : n->children) self(self, child);
                ImGui::TreePop();
            }
        };

        for (auto root : scene.roots) drawNode(drawNode, root);
        ImGui::End();
    }

    void drawInspectorPanel(genesis::Scene& scene) {
        const float top = ImGui::GetFrameHeight();
        const ImVec2 display = ImGui::GetMainViewport()->Size;

        ImGui::SetNextWindowPos(ImVec2(display.x - kRightPanelW, top));
        ImGui::SetNextWindowSize(ImVec2(kRightPanelW, display.y - top - kStatusBarH));
        ImGui::Begin("Inspector", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (selected_ == 0) {
            ImGui::TextWrapped("Click an object in the 3D view or pick one from the Hierarchy.");
        } else if (genesis::Node* n = scene.find(selected_)) {
            ImGui::Text("Name");
            ImGui::TextUnformatted(n->name.c_str());
            ImGui::Separator();

            bool changed = false;
            changed |= ImGui::Checkbox("Visible", &n->visible);

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

            if (changed) runtime_.updateTransforms();

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
            }
            return;
        }
        if (*n.material_resource >= runtime_.materials().size()) return;

        genesis::SceneMaterial& mat = *runtime_.material(*n.material_resource);
        ImGui::Text("%s", runtime_.materialNames()[*n.material_resource].c_str());
        ImGui::ColorEdit4("Base color", &mat.base_color.x);
        ImGui::DragFloat("Metallic", &mat.metallic, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Roughness", &mat.roughness, 0.01f, 0.0f, 1.0f);
    }

    void drawCameraInspector(genesis::Scene& scene, genesis::Node& n) {
        if (!n.camera.has_value()) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Camera");
        genesis::SceneCamera& cam = *n.camera;
        float fov_deg = glm::degrees(cam.fov_y_radians);
        if (ImGui::DragFloat("FOV (deg)", &fov_deg, 0.5f, 1.0f, 170.0f)) {
            cam.fov_y_radians = glm::radians(fov_deg);
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
        ImGui::Checkbox("Enabled", &light.enabled);
        ImGui::ColorEdit3("Color", &light.color.x);
        ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
    }

    void drawStatusBar(genesis::Scene& scene) {
        const ImVec2 display = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(0.0f, display.y - kStatusBarH));
        ImGui::SetNextWindowSize(ImVec2(display.x, kStatusBarH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
        ImGui::Begin("Status", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar);

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
        ImGui::Text("FPS %.0f  |  %u tris  |  Grid 10x10 (100 cm/cell)",
                    timer().fps(), stats.triangles);

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void drawViewportOverlay() {
        ImGui::SetNextWindowPos(ImVec2(viewport_.x + 8.0f, viewport_.y + 8.0f));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("##ViewportHelp", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        ImGui::TextUnformatted("3D View");
        ImGui::TextUnformatted("LMB: select object");
        ImGui::TextUnformatted("Drag gizmo: transform");
        ImGui::TextUnformatted("RMB + WASD/QE: fly camera");
        ImGui::Text("Units: centimeters (100 cm = 1 m)");
        ImGui::End();
    }

    void drawViewportGizmo(genesis::Scene& scene) {
        if (selected_ == 0) return;
        genesis::Node* node = scene.find(selected_);
        if (!node) return;

        const glm::mat4 view = outputViewMatrix();
        const glm::mat4 projection =
            genesis::projectionForImGuizmo(outputProjectionMatrix(viewport_.aspect()));

        float view_m[16]{};
        float proj_m[16]{};
        float matrix_m[16]{};
        genesis::mat4ToFloat16(view, view_m);
        genesis::mat4ToFloat16(projection, proj_m);
        genesis::mat4ToFloat16(node->world, matrix_m);

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(viewport_.x, viewport_.y, viewport_.w, viewport_.h);
        ImGuizmo::Enable(true);

        if (ImGuizmo::Manipulate(view_m, proj_m, gizmo_operation_, gizmo_mode_, matrix_m)) {
            const glm::mat4 new_world = genesis::float16ToMat4(matrix_m);
            const glm::mat4 new_local = genesis::worldToLocalMatrix(scene, *node, new_world);
            genesis::Transform local{};
            if (genesis::decomposeTransform(new_local, local)) {
                node->local = local;
                runtime_.updateTransforms();
                rot_edit_node_ = 0;
            }
        }
    }

    void pickAtViewportMouse(const ImVec2& mouse, genesis::Scene& scene) {
        if (!viewport_.contains(mouse.x, mouse.y)) return;

        const genesis::ScreenRay ray = genesis::rayFromScreen(
            outputViewMatrix(), outputProjectionMatrix(viewport_.aspect()), viewport_.aspect(),
            viewport_.localMouseX(mouse.x), viewport_.localMouseY(mouse.y), viewport_.w, viewport_.h);

        const auto skip = [this](const genesis::Node& node) {
            if (node.name == "Floor") return true;
            if (!node.mesh_resource.has_value()) return false;
            if (*node.mesh_resource >= runtime_.meshNames().size()) return false;
            return runtime_.meshNames()[*node.mesh_resource] == "Plane";
        };

        if (std::optional<genesis::NodeId> picked = genesis::pickNodeAtScreen(scene, ray, skip)) {
            selectNode(*picked);
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
    }

    void addLightNode(const char* prefix, genesis::SceneLightType type) {
        static int counter = 1;
        const std::string name = std::string(prefix) + " " + std::to_string(counter++);
        selectNode(runtime_.scene().createLightNode(name, type, createParent()));
    }

    glm::vec3 outputCameraPosition() const {
        if (!use_editor_output_) {
            if (std::optional<glm::vec3> pos = runtime_.outputCameraPosition()) return *pos;
        }
        return editor_camera_.transform.position;
    }

    void syncEditorCameraFromNode(const genesis::Node& node) {
        editor_camera_.transform.position = node.local.position;
        editor_camera_.setRotation(node.local.rotation);
        genesis::extractYawPitchFromForward(editor_camera_.forward(), editor_camera_.yaw_radians,
                                            editor_camera_.pitch_radians);
        if (node.camera.has_value()) {
            editor_camera_.fov_y_radians = node.camera->fov_y_radians;
            editor_camera_.near_plane = node.camera->near_plane;
            editor_camera_.far_plane = node.camera->far_plane;
        }
    }

    void resetEditorCamera() {
        const glm::vec3 target(0.0f, 0.5f, 0.0f);
        const glm::vec3 eye(6.0f, 3.5f, 6.0f);
        editor_camera_.transform.position = eye;
        editor_camera_.setRotation(genesis::rotationLookAt(eye, target));
        genesis::extractYawPitchFromForward(editor_camera_.forward(), editor_camera_.yaw_radians,
                                            editor_camera_.pitch_radians);
        editor_camera_.fov_y_radians = glm::radians(55.0f);
        editor_camera_.near_plane = 0.1f;
        editor_camera_.far_plane = 200.0f;
    }

    void syncEditorAfterDocumentLoad() {
        use_editor_output_ = true;
        resetEditorCamera();
    }

    genesis::SceneRuntime runtime_;
    genesis::Mesh grid_mesh_{};
    genesis::Camera editor_camera_{};
    genesis::ViewportRect viewport_{};
    bool use_editor_output_ = true;
    bool ui_style_initialized_ = false;
    genesis::NodeId selected_ = 0;
    genesis::NodeId rot_edit_node_ = 0;
    float rot_euler_deg_[3] = {0.0f, 0.0f, 0.0f};
    genesis::NodeId reparent_target_ = 0;
    std::string reparent_preview_ = "(Root)";
    std::string loaded_path_;
    std::vector<genesis::SceneDrawCommand> draw_commands_;
    ImGuizmo::OPERATION gizmo_operation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmo_mode_ = ImGuizmo::LOCAL;
};

} // namespace

int main(int argc, char** argv) {
    try {
        genesis::ApplicationConfig config{};
        config.title = "GENESIS — Scene Editor";
        config.width = 1440;
        config.height = 900;

        GltfViewerApp app(config);

        if (argc >= 2) {
            const std::filesystem::path asset_path = genesis::resolveAssetPath(argv[1]);
            const std::string ext = asset_path.extension().string();
            if (ext == ".json" || ext == ".scene") {
                app.loadSceneDocument(asset_path);
            } else {
                app.load(asset_path);
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
