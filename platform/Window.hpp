#pragma once

#include <cstdint>
#include <string>

struct SDL_Window;
union SDL_Event;

namespace genesis {

struct WindowConfig {
    std::string title = "GENESIS";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool start_maximized = false;
};

class Window {
public:
    explicit Window(const WindowConfig& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void pollEvents();
    bool shouldClose() const { return should_close_; }
    void setShouldClose(bool value) { should_close_ = value; }

    SDL_Window* handle() const { return window_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool wasResized() const { return resized_; }
    void clearResizedFlag() { resized_ = false; }
    void handleResize(int width, int height);

private:
    SDL_Window* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool should_close_ = false;
    bool resized_ = false;
};

void processWindowEvent(Window& window, const SDL_Event& event);

} // namespace genesis
