#include "engine/Application.hpp"
#include "scene/Camera.hpp"

#include <spdlog/spdlog.h>

#include <exception>
#include <string>

namespace {

class TriangleApp final : public genesis::Application {
public:
    using genesis::Application::Application;

protected:
    void onUpdate(float dt) override {
        // Static camera so the triangle stays visible.
        camera_.transform.position = {0.0f, 0.0f, 2.0f};
        camera_.yaw_radians = 0.0f;
        camera_.pitch_radians = 0.0f;

        const float aspect = (window().height() > 0) ? (float)window().width() / (float)window().height() : 1.0f;
        const glm::mat4 vp = camera_.viewProjection(aspect);
        renderer().setViewProjection(&vp[0][0]);
        (void)dt;
    }

private:
    genesis::Camera camera_{};
};

} // namespace

int main() {
    try {
        genesis::ApplicationConfig config{};
        config.title = "GENESIS — Triangle";
        config.width = 1280;
        config.height = 720;

        TriangleApp app(config);
        return app.run();
    } catch (const std::exception& ex) {
        spdlog::critical("Fatal error: {}", ex.what());
        return 1;
    }
}
