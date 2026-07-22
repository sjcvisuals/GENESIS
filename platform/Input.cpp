#include "platform/Input.hpp"

#include <cstring>

namespace genesis {

void Input::beginFrame() {
  std::memcpy(keys_previous_, keys_current_, sizeof(keys_current_));
  std::memcpy(mouse_buttons_previous_, mouse_buttons_, sizeof(mouse_buttons_));
}

void Input::onEvent(const SDL_Event& event) {
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
    if (!event.key.repeat) {
      keys_current_[event.key.scancode] = true;
    }
    break;
  case SDL_EVENT_KEY_UP:
    keys_current_[event.key.scancode] = false;
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button < kMouseButtonCount) {
      mouse_buttons_[event.button.button] = true;
    }
    mouse_x_ = event.button.x;
    mouse_y_ = event.button.y;
    break;
  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button < kMouseButtonCount) {
      mouse_buttons_[event.button.button] = false;
    }
    mouse_x_ = event.button.x;
    mouse_y_ = event.button.y;
    break;
  case SDL_EVENT_MOUSE_MOTION:
    mouse_delta_x_ += event.motion.xrel;
    mouse_delta_y_ += event.motion.yrel;
    mouse_x_ = event.motion.x;
    mouse_y_ = event.motion.y;
    break;
  default:
    break;
  }
}

bool Input::keyDown(SDL_Scancode scancode) const {
  return keys_current_[scancode];
}

bool Input::keyPressed(SDL_Scancode scancode) const {
  return keys_current_[scancode] && !keys_previous_[scancode];
}

bool Input::keyReleased(SDL_Scancode scancode) const {
  return !keys_current_[scancode] && keys_previous_[scancode];
}

bool Input::mouseButtonDown(int button) const {
  return button >= 0 && button < kMouseButtonCount && mouse_buttons_[button];
}

bool Input::mouseButtonPressed(int button) const {
  return button >= 0 && button < kMouseButtonCount && mouse_buttons_[button] &&
         !mouse_buttons_previous_[button];
}

void Input::resetMouseDelta() {
  mouse_delta_x_ = 0.0f;
  mouse_delta_y_ = 0.0f;
}

void Input::setMouseCaptured(bool captured) {
    setMouseCaptured(SDL_GetMouseFocus(), captured);
}

void Input::setMouseCaptured(SDL_Window* window, bool captured) {
    if (window != nullptr) {
        SDL_SetWindowRelativeMouseMode(window, captured);
    }
}

} // namespace genesis
