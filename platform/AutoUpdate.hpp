#pragma once

#include <string>

namespace genesis {

struct AutoUpdateConfig {
    // Sparkle/WinSparkle appcast URL. Empty → GENESIS_UPDATE_APPCAST_URL_DEFAULT / env.
    std::string appcast_url;
    // DSA public key PEM (WinSparkle 0.8.x). Empty → unsigned feed (dev only).
    std::string dsa_pub_pem;
    bool check_on_startup = true;
    int check_interval_seconds = 86400; // 24h
    std::string company_name = "GENESIS";
    std::string app_name = "GENESIS";
};

// Adobe-like software updates (WinSparkle on Windows; ImGui demo elsewhere).
class AutoUpdate {
public:
    static AutoUpdate& instance();

    void configure(const AutoUpdateConfig& config);
    // Call after the main window exists (WinSparkle may show UI).
    void init();
    void shutdown();

    // On-demand check with progress UI (Help → Check for Updates…).
    void checkWithUi();
    // Background check without interrupting unless an update is found.
    void checkInBackground();

    bool available() const { return available_; }
    bool initialized() const { return initialized_; }
    const std::string& appcastUrl() const { return config_.appcast_url; }
    const std::string& versionString() const;

    // ImGui fallback / demo panel (non-Windows or GENESIS_UPDATE_UI_DEMO=1).
    void drawDemoUi(bool* open);

private:
    AutoUpdate() = default;
    AutoUpdateConfig config_{};
    bool available_ = false;
    bool initialized_ = false;
    bool demo_open_ = false;
    int demo_phase_ = 0; // 0 idle, 1 checking, 2 found, 3 downloading, 4 ready
    float demo_progress_ = 0.0f;
    double demo_phase_t0_ = 0.0;
};

} // namespace genesis
