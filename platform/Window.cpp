#include "platform/Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

namespace genesis {

Window::Window(const WindowConfig& config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.start_maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }

    window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    width_ = config.width;
    height_ = config.height;

    spdlog::info("Window created ({}x{})", width_, height_);
}

Window::~Window() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Window::pollEvents() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        processWindowEvent(*this, event);
    }
}

void Window::handleResize(int width, int height) {
    width_ = width;
    height_ = height;
    resized_ = true;
}

void Window::setTitle(const std::string& title) {
    if (window_) {
        SDL_SetWindowTitle(window_, title.c_str());
    }
}

void processWindowEvent(Window& window, const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        window.setShouldClose(true);
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        window.setShouldClose(true);
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window.handle(), &w, &h);
        window.handleResize(w, h);
        break;
    }
    default:
        break;
    }
}

} // namespace genesis
