// Standalone webcam background-remover debug tool.
// Isolates RVM/matte from PIXERA: if this works, the bug is in ExtGenesisRemover I/O.
//
// Left = camera (opaque). Right = cutout over checkerboard.
// Keys: 1=Fast 2=Balanced 3=Quality  P=passthrough  Esc=quit
// Log: remover-camera-debug.log next to the exe (and stdout).

#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::ofstream g_log_file;

void logLine(const std::string& line) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    const std::string full = std::to_string(ms) + " " + line;
    std::printf("%s\n", full.c_str());
    std::fflush(stdout);
    if (g_log_file) {
        g_log_file << full << '\n';
        g_log_file.flush();
    }
}

void fillOpaque(const std::uint8_t* in, std::uint8_t* out, std::size_t bytes) {
    std::memcpy(out, in, bytes);
    for (std::size_t i = 3; i < bytes; i += 4) {
        out[i] = 255;
    }
}

void drawCheckerboard(SDL_Renderer* renderer, int x, int y, int w, int h, int cell) {
    for (int cy = 0; cy < h; cy += cell) {
        for (int cx = 0; cx < w; cx += cell) {
            const bool light = (((cx / cell) + (cy / cell)) & 1) == 0;
            if (light) {
                SDL_SetRenderDrawColor(renderer, 55, 55, 60, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
            }
            const SDL_FRect r{static_cast<float>(x + cx), static_cast<float>(y + cy),
                             static_cast<float>(std::min(cell, w - cx)),
                             static_cast<float>(std::min(cell, h - cy))};
            SDL_RenderFillRect(renderer, &r);
        }
    }
}

bool surfaceToRgba(SDL_Surface* src, std::vector<std::uint8_t>& out_rgba, std::uint32_t& w,
                   std::uint32_t& h) {
    if (!src || src->w <= 0 || src->h <= 0) {
        return false;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
    if (!rgba) {
        logLine(std::string("SDL_ConvertSurface failed: ") + SDL_GetError());
        return false;
    }
    w = static_cast<std::uint32_t>(rgba->w);
    h = static_cast<std::uint32_t>(rgba->h);
    out_rgba.resize(static_cast<std::size_t>(w) * h * 4u);
    const auto* pixels = static_cast<const std::uint8_t*>(rgba->pixels);
    for (std::uint32_t y = 0; y < h; ++y) {
        std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * w * 4u,
                    pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch),
                    static_cast<std::size_t>(w) * 4u);
    }
    SDL_DestroySurface(rgba);
    return true;
}

std::filesystem::path exeDirectory() {
    if (const char* base = SDL_GetBasePath()) {
        // SDL3: GetBasePath is owned by SDL — do not SDL_free (unlike SDL2).
        return std::filesystem::path(base);
    }
    return std::filesystem::current_path();
}

void printUsage() {
    std::printf(
        "genesis_remover_camera_debug — standalone webcam BG remover (no PIXERA)\n"
        "  --camera N     Use camera index N (default 0)\n"
        "  --list         List cameras and exit\n"
        "  --passthrough  Start in opaque passthrough (skip matte)\n"
        "  --help         Show this help\n"
        "Keys while running: 1=Fast 2=Balanced 3=Quality  P=passthrough  Esc=quit\n");
}

} // namespace

int main(int argc, char** argv) {
    int camera_index = 0;
    bool list_only = false;
    bool start_passthrough = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--list") {
            list_only = true;
        } else if (arg == "--passthrough") {
            start_passthrough = true;
        } else if (arg == "--camera" && i + 1 < argc) {
            camera_index = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "Unknown arg: %s\n", arg.c_str());
            printUsage();
            return 1;
        }
    }

    const std::filesystem::path exe_dir = exeDirectory();
    const std::filesystem::path roots[] = {exe_dir, exe_dir / "assets",
                                           std::filesystem::current_path(),
                                           std::filesystem::current_path() / "assets"};
    genesis::setAssetSearchRoots(roots);

    g_log_file.open(exe_dir / "remover-camera-debug.log", std::ios::app);
    logLine("=== genesis_remover_camera_debug start ===");
    logLine(std::string("exe_dir=") + exe_dir.string());

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_CAMERA | SDL_INIT_EVENTS)) {
        logLine(std::string("SDL_Init failed: ") + SDL_GetError());
        return 1;
    }

    int cam_count = 0;
    SDL_CameraID* cameras = SDL_GetCameras(&cam_count);
    if (!cameras || cam_count <= 0) {
        logLine("No cameras found. Connect a webcam / enable the integrated camera.");
        SDL_free(cameras);
        SDL_Quit();
        return 1;
    }
    for (int i = 0; i < cam_count; ++i) {
        const char* name = SDL_GetCameraName(cameras[i]);
        logLine(std::string("camera[") + std::to_string(i) + "] id=" +
                std::to_string(static_cast<unsigned long long>(cameras[i])) + " name=" +
                (name ? name : "(unnamed)"));
    }
    if (list_only) {
        SDL_free(cameras);
        SDL_Quit();
        return 0;
    }
    if (camera_index < 0 || camera_index >= cam_count) {
        logLine(std::string("Invalid --camera ") + std::to_string(camera_index) +
                " (have " + std::to_string(cam_count) + ")");
        SDL_free(cameras);
        SDL_Quit();
        return 1;
    }

    SDL_Camera* camera = SDL_OpenCamera(cameras[camera_index], nullptr);
    SDL_free(cameras);
    if (!camera) {
        logLine(std::string("SDL_OpenCamera failed: ") + SDL_GetError());
        SDL_Quit();
        return 1;
    }
    logLine(std::string("Opened camera[") + std::to_string(camera_index) + "]");

    genesis::BgRemoverSettings settings{};
    settings.backend = genesis::BgRemoverBackend::Auto;
    settings.quality = genesis::BgRemoverQuality::Balanced;
    settings.temporal_alpha = 0.25f;
    try {
        settings.model_path =
            genesis::resolveAssetPath("assets/matting/rvm_mobilenetv3_fp16.onnx");
        logLine(std::string("RVM model: ") + settings.model_path.string());
    } catch (const std::exception& ex) {
        logLine(std::string("RVM model resolve failed: ") + ex.what());
    }

    // Probe sync init once so we know if ORT/RVM is available before the UI loop.
    {
        genesis::BgRemover probe;
        probe.setSettings(settings);
        std::string st;
        probe.initialize(&st);
        logLine(std::string("probe backend=") + genesis::bgRemoverBackendLabel(probe.activeBackend()) +
                " rvm=" + (probe.isRvmReady() ? "1" : "0") + " status=" + st);
        if (!probe.isRvmReady()) {
            logLine("WARNING: RVM not ready — will fall back to chroma (green key). "
                    "Coffee-shop backgrounds will NOT cut out correctly until ORT+model work.");
        }
    }

    genesis::AsyncMatteWorker worker;
    worker.setSettings(settings);
    worker.start();
    logLine("AsyncMatteWorker started");

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("GENESIS Remover Camera Debug — Left: camera | Right: cutout",
                                     1280, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        logLine(std::string("CreateWindowAndRenderer failed: ") + SDL_GetError());
        worker.stop();
        SDL_CloseCamera(camera);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* tex_cam = nullptr;
    SDL_Texture* tex_matte = nullptr;
    std::uint32_t tex_w = 0;
    std::uint32_t tex_h = 0;
    std::uint32_t frame_w = 0;
    std::uint32_t frame_h = 0;
    std::vector<std::uint8_t> rgba_in;
    std::vector<std::uint8_t> rgba_out;
    bool passthrough_only = start_passthrough;
    std::uint64_t frames = 0;
    std::uint64_t last_status_frames = 0;
    bool running = true;
    bool saw_first_frame = false;
    const auto start_tp = std::chrono::steady_clock::now();

    logLine(passthrough_only ? "UI ready (passthrough). Keys: 1/2/3 quality, P matte, Esc quit"
                             : "UI ready. Keys: 1/2/3 quality, P passthrough toggle, Esc quit");

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (ev.key.key == SDLK_1) {
                    settings.quality = genesis::BgRemoverQuality::Fast;
                    worker.setSettings(settings);
                    logLine("quality=Fast");
                } else if (ev.key.key == SDLK_2) {
                    settings.quality = genesis::BgRemoverQuality::Balanced;
                    worker.setSettings(settings);
                    logLine("quality=Balanced");
                } else if (ev.key.key == SDLK_3) {
                    settings.quality = genesis::BgRemoverQuality::Quality;
                    worker.setSettings(settings);
                    logLine("quality=Quality");
                } else if (ev.key.key == SDLK_P) {
                    passthrough_only = !passthrough_only;
                    logLine(passthrough_only ? "mode=opaque_passthrough" : "mode=matte");
                }
            } else if (ev.type == SDL_EVENT_CAMERA_DEVICE_DENIED) {
                logLine("Camera permission denied");
                running = false;
            } else if (ev.type == SDL_EVENT_CAMERA_DEVICE_APPROVED) {
                logLine("Camera permission approved");
            }
        }

        Uint64 ts = 0;
        SDL_Surface* frame = SDL_AcquireCameraFrame(camera, &ts);
        if (frame) {
            if (surfaceToRgba(frame, rgba_in, frame_w, frame_h)) {
                if (!saw_first_frame) {
                    saw_first_frame = true;
                    logLine(std::string("first camera frame ok size=") + std::to_string(frame_w) +
                            "x" + std::to_string(frame_h) + " format=" +
                            std::to_string(static_cast<unsigned>(frame->format)));
                }
                if (frame_w != tex_w || frame_h != tex_h || !tex_cam || !tex_matte) {
                    if (tex_cam) {
                        SDL_DestroyTexture(tex_cam);
                        tex_cam = nullptr;
                    }
                    if (tex_matte) {
                        SDL_DestroyTexture(tex_matte);
                        tex_matte = nullptr;
                    }
                    tex_cam = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                static_cast<int>(frame_w),
                                                static_cast<int>(frame_h));
                    tex_matte = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  static_cast<int>(frame_w),
                                                  static_cast<int>(frame_h));
                    if (tex_matte) {
                        SDL_SetTextureBlendMode(tex_matte, SDL_BLENDMODE_BLEND);
                    }
                    tex_w = frame_w;
                    tex_h = frame_h;
                    worker.reset(frame_w, frame_h);
                    logLine(std::string("camera frame size ") + std::to_string(frame_w) + "x" +
                            std::to_string(frame_h));
                    SDL_SetWindowSize(window, static_cast<int>(frame_w) * 2,
                                      static_cast<int>(frame_h));
                }

                const std::size_t bytes = static_cast<std::size_t>(frame_w) * frame_h * 4u;
                rgba_out.resize(bytes);
                if (passthrough_only) {
                    fillOpaque(rgba_in.data(), rgba_out.data(), bytes);
                } else {
                    worker.trySubmit(rgba_in.data(), frame_w, frame_h);
                    if (!worker.copyLatest(rgba_out.data(), frame_w, frame_h)) {
                        fillOpaque(rgba_in.data(), rgba_out.data(), bytes);
                    }
                }

                if (tex_cam) {
                    SDL_UpdateTexture(tex_cam, nullptr, rgba_in.data(),
                                      static_cast<int>(frame_w * 4u));
                }
                if (tex_matte) {
                    SDL_UpdateTexture(tex_matte, nullptr, rgba_out.data(),
                                      static_cast<int>(frame_w * 4u));
                }
                ++frames;
            }
            SDL_ReleaseCameraFrame(camera, frame);
        } else if (!saw_first_frame) {
            const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - start_tp)
                                    .count();
            if (waited >= 8 && frames == 0) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    logLine("Still waiting for first camera frame after 8s — check OS camera "
                            "permission / that another app is not locking the camera.");
                }
            }
        }

        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(window, &win_w, &win_h);
        const int half = std::max(1, win_w / 2);

        SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
        SDL_RenderClear(renderer);

        if (tex_cam) {
            const SDL_FRect left{0.f, 0.f, static_cast<float>(half), static_cast<float>(win_h)};
            SDL_RenderTexture(renderer, tex_cam, nullptr, &left);
        }
        drawCheckerboard(renderer, half, 0, win_w - half, win_h, 24);
        if (tex_matte) {
            const SDL_FRect right{static_cast<float>(half), 0.f, static_cast<float>(win_w - half),
                                  static_cast<float>(win_h)};
            SDL_RenderTexture(renderer, tex_matte, nullptr, &right);
        }
        SDL_RenderPresent(renderer);

        if (frames - last_status_frames >= 60) {
            last_status_frames = frames;
            logLine(std::string("status backend=") +
                    genesis::bgRemoverBackendLabel(worker.activeBackend()) +
                    " rvm=" + (worker.isRvmReady() ? "1" : "0") +
                    " submitted=" + std::to_string(worker.submitted()) +
                    " completed=" + std::to_string(worker.completed()) +
                    " skipped=" + std::to_string(worker.skipped()) +
                    " busy=" + (worker.busy() ? "1" : "0") + " msg=" + worker.status());
        }
    }

    worker.stop();
    if (tex_cam) {
        SDL_DestroyTexture(tex_cam);
    }
    if (tex_matte) {
        SDL_DestroyTexture(tex_matte);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_CloseCamera(camera);
    SDL_Quit();
    logLine("=== genesis_remover_camera_debug exit ===");
    return 0;
}
