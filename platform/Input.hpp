#pragma once

#include <SDL3/SDL.h>

namespace genesis {

class Input {
public:
    void beginFrame();
    void onEvent(const SDL_Event& event);

    bool keyDown(SDL_Scancode scancode) const;
    bool keyPressed(SDL_Scancode scancode) const;
    bool keyReleased(SDL_Scancode scancode) const;

    bool mouseButtonDown(int button) const;
    bool mouseButtonPressed(int button) const;
    float mouseX() const { return mouse_x_; }
    float mouseY() const { return mouse_y_; }
    float mouseDeltaX() const { return mouse_delta_x_; }
    float mouseDeltaY() const { return mouse_delta_y_; }
    void resetMouseDelta();

    void setMouseCaptured(bool captured);

private:
    static constexpr int kKeyCount = SDL_SCANCODE_COUNT;
    static constexpr int kMouseButtonCount = 8;

    bool keys_current_[kKeyCount]{};
    bool keys_previous_[kKeyCount]{};
    bool mouse_buttons_[kMouseButtonCount]{};
    bool mouse_buttons_previous_[kMouseButtonCount]{};
    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;
    float mouse_delta_x_ = 0.0f;
    float mouse_delta_y_ = 0.0f;
};

} // namespace genesis
