#include "engine/Application.hpp"
#include "scene/Camera.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>

namespace {

class CameraApp final : public genesis::Application {
public:
    explicit CameraApp(const genesis::ApplicationConfig& config) : genesis::Application(config) {
        // Start behind the demo triangle so it is immediately visible.
        camera_.transform.position = {0.0f, 0.0f, 2.0f};
    }

protected:
    void onUpdate(float dt) override {
        // Right mouse button toggles mouse capture for look.
        if (input().mouseButtonDown(SDL_BUTTON_RIGHT)) {
            input().setMouseCaptured(true);
            const float sensitivity = 0.0025f;
            camera_.yaw_radians += input().mouseDeltaX() * sensitivity;
            camera_.pitch_radians += input().mouseDeltaY() * sensitivity;
            camera_.pitch_radians = glm::clamp(camera_.pitch_radians, -1.4f, 1.4f);
            camera_.rebuildRotationFromYawPitch();
        } else {
            input().setMouseCaptured(false);
        }

        const float speed = (input().keyDown(SDL_SCANCODE_LSHIFT) ? 6.0f : 2.5f);

        glm::vec3 move(0.0f);
        if (input().keyDown(SDL_SCANCODE_W)) move += camera_.forward();
        if (input().keyDown(SDL_SCANCODE_S)) move -= camera_.forward();
        if (input().keyDown(SDL_SCANCODE_D)) move += camera_.right();
        if (input().keyDown(SDL_SCANCODE_A)) move -= camera_.right();
        if (input().keyDown(SDL_SCANCODE_E)) move += glm::vec3(0.0f, 1.0f, 0.0f);
        if (input().keyDown(SDL_SCANCODE_Q)) move -= glm::vec3(0.0f, 1.0f, 0.0f);

        if (glm::length(move) > 0.0f) {
            camera_.transform.position += glm::normalize(move) * speed * dt;
        }

        const float aspect = (window().height() > 0) ? (float)window().width() / (float)window().height() : 1.0f;
        const glm::mat4 vp = camera_.viewProjection(aspect);
        renderer().setViewProjection(&vp[0][0]);
    }

    void onImGui() override {
        genesis::Application::onImGui();
        ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("WASD move, QE up/down, RMB look");
        ImGui::Text("Pos: %.2f %.2f %.2f", camera_.transform.position.x, camera_.transform.position.y,
                    camera_.transform.position.z);
        ImGui::Text("Yaw/Pitch: %.2f %.2f", camera_.yaw_radians, camera_.pitch_radians);
        ImGui::End();
    }

private:
    genesis::Camera camera_{};
};

} // namespace

int main() {
    genesis::ApplicationConfig config{};
    config.title = "GENESIS — Camera";
    config.width = 1280;
    config.height = 720;

    CameraApp app(config);
    return app.run();
}
