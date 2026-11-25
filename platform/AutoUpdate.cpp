#include "platform/AutoUpdate.hpp"
#include "platform/Version.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32) && defined(GENESIS_HAS_WINSPARKLE) && GENESIS_HAS_WINSPARKLE
#define GENESIS_USE_WINSPARKLE 1
#include <winsparkle.h>
#else
#define GENESIS_USE_WINSPARKLE 0
#endif

namespace genesis {

namespace {

bool envTruthy(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

std::string resolveAppcastUrl(const AutoUpdateConfig& config) {
    if (const char* env = std::getenv("GENESIS_UPDATE_FEED"); env && env[0] != '\0') {
        return env;
    }
    if (!config.appcast_url.empty()) {
        return config.appcast_url;
    }
    return GENESIS_UPDATE_APPCAST_URL_DEFAULT;
}

} // namespace

AutoUpdate& AutoUpdate::instance() {
    static AutoUpdate g;
    return g;
}

const std::string& AutoUpdate::versionString() const {
    static const std::string kVersion = GENESIS_VERSION_STRING;
    return kVersion;
}

void AutoUpdate::configure(const AutoUpdateConfig& config) {
    config_ = config;
    config_.appcast_url = resolveAppcastUrl(config_);
#if GENESIS_USE_WINSPARKLE
    available_ = true;
#else
    available_ = envTruthy("GENESIS_UPDATE_UI_DEMO");
#endif
}

void AutoUpdate::init() {
    if (initialized_) {
        return;
    }
    config_.appcast_url = resolveAppcastUrl(config_);

#if GENESIS_USE_WINSPARKLE
    available_ = true;
    win_sparkle_set_appcast_url(config_.appcast_url.c_str());
    win_sparkle_set_app_details(L"GENESIS", L"GENESIS", GENESIS_VERSION_WSTRING);
    win_sparkle_set_registry_path("Software\\GENESIS\\Updates");
    win_sparkle_set_automatic_check_for_updates(config_.check_on_startup ? 1 : 0);
    win_sparkle_set_update_check_interval(config_.check_interval_seconds);
    if (!config_.dsa_pub_pem.empty()) {
        if (win_sparkle_set_dsa_pub_pem(config_.dsa_pub_pem.c_str()) == 0) {
            spdlog::warn("WinSparkle: DSA public key rejected");
        }
    } else if (const char* pem = std::getenv("GENESIS_UPDATE_DSA_PEM"); pem && pem[0] != '\0') {
        win_sparkle_set_dsa_pub_pem(pem);
    } else {
        spdlog::info(
            "WinSparkle: no DSA public key set — use a signed appcast for production "
            "(see docs/windows-auto-update.md)");
    }
    win_sparkle_init();
    initialized_ = true;
    spdlog::info("WinSparkle ready (feed={}, version={})", config_.appcast_url, GENESIS_VERSION_STRING);
#else
    available_ = envTruthy("GENESIS_UPDATE_UI_DEMO");
    initialized_ = true;
    if (available_) {
        spdlog::info("AutoUpdate demo UI enabled (WinSparkle is Windows-only)");
    } else {
        spdlog::debug("AutoUpdate stub (non-Windows build)");
    }
#endif
}

void AutoUpdate::shutdown() {
#if GENESIS_USE_WINSPARKLE
    if (initialized_) {
        win_sparkle_cleanup();
    }
#endif
    initialized_ = false;
}

void AutoUpdate::checkWithUi() {
    if (!initialized_) {
        init();
    }
#if GENESIS_USE_WINSPARKLE
    win_sparkle_check_update_with_ui();
#else
    demo_open_ = true;
    demo_phase_ = 1;
    demo_progress_ = 0.0f;
    demo_phase_t0_ = ImGui::GetTime();
#endif
}

void AutoUpdate::checkInBackground() {
    if (!initialized_) {
        init();
    }
#if GENESIS_USE_WINSPARKLE
    win_sparkle_check_update_without_ui();
#else
    (void)0;
#endif
}

void AutoUpdate::drawDemoUi(bool* open) {
    if (!open) {
        return;
    }
    // User closed via window X — clear demo state so we don't force-reopen.
    if (!*open) {
        if (demo_open_) {
            demo_open_ = false;
            demo_phase_ = 0;
            demo_progress_ = 0.0f;
        }
        return;
    }
    if (demo_open_) {
        *open = true;
    }

    const double now = ImGui::GetTime();
    if (demo_phase_ == 1 && now - demo_phase_t0_ > 1.2) {
        demo_phase_ = 2;
        demo_phase_t0_ = now;
    } else if (demo_phase_ == 3) {
        demo_progress_ = static_cast<float>(std::min(1.0, (now - demo_phase_t0_) / 2.5));
        if (demo_progress_ >= 1.0f) {
            demo_phase_ = 4;
            demo_progress_ = 1.0f;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Software Update", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!*open) {
        demo_open_ = false;
        demo_phase_ = 0;
        demo_progress_ = 0.0f;
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("GENESIS");
    ImGui::TextDisabled("Version %s", GENESIS_VERSION_STRING);
    ImGui::Separator();

    if (demo_phase_ <= 1) {
        ImGui::TextUnformatted("Checking for updates…");
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0), "");
    } else if (demo_phase_ == 2) {
        ImGui::TextUnformatted("A new version of GENESIS is available.");
        ImGui::TextDisabled("A newer build is ready — quieter installs, improved updater.");
        ImGui::Spacing();
        if (ImGui::Button("Download update", ImVec2(180, 0))) {
            demo_phase_ = 3;
            demo_progress_ = 0.0f;
            demo_phase_t0_ = now;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remind me later")) {
            demo_open_ = false;
            *open = false;
            demo_phase_ = 0;
        }
    } else if (demo_phase_ == 3) {
        ImGui::TextUnformatted("Downloading update…");
        ImGui::ProgressBar(demo_progress_, ImVec2(-1, 0));
        ImGui::TextDisabled("%.0f%%", demo_progress_ * 100.0f);
    } else if (demo_phase_ == 4) {
        ImGui::TextUnformatted("Update ready to install.");
        ImGui::TextWrapped(
            "GENESIS will quit and apply the update quietly, then relaunch — "
            "similar to Adobe Creative Cloud desktop apps.");
        if (ImGui::Button("Install and relaunch", ImVec2(200, 0))) {
            demo_open_ = false;
            *open = false;
            demo_phase_ = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            demo_open_ = false;
            *open = false;
            demo_phase_ = 0;
        }
    } else {
        ImGui::TextUnformatted("You're up to date.");
        if (ImGui::Button("Close")) {
            *open = false;
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Feed: %s", config_.appcast_url.c_str());
#if !GENESIS_USE_WINSPARKLE
    ImGui::TextDisabled("Demo UI (WinSparkle runs on Windows builds).");
#endif
    ImGui::End();
}

} // namespace genesis
