#include "matting/AsyncMatteWorker.hpp"

#include <cstring>

namespace genesis {

AsyncMatteWorker::AsyncMatteWorker() = default;

AsyncMatteWorker::~AsyncMatteWorker() {
    stop();
}

void AsyncMatteWorker::start() {
    std::lock_guard lock(mu_);
    if (!stop_) {
        return;
    }
    stop_ = false;
    thread_ = std::thread([this] { threadMain(); });
}

void AsyncMatteWorker::stop() {
    {
        std::lock_guard lock(mu_);
        if (stop_ && !thread_.joinable()) {
            return;
        }
        stop_ = true;
        has_pending_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    busy_.store(false, std::memory_order_release);
}

void AsyncMatteWorker::setSettings(const BgRemoverSettings& settings) {
    {
        std::lock_guard lock(mu_);
        // Identical settings must be a no-op. Clearing has_latest every call
        // (e.g. from applyMatteSettings each render) permanently blocks matte output.
        if (!settings_dirty_ && settings_.backend == settings.backend &&
            settings_.quality == settings.quality &&
            settings_.key_r == settings.key_r && settings_.key_g == settings.key_g &&
            settings_.key_b == settings.key_b &&
            settings_.key_threshold == settings.key_threshold &&
            settings_.key_softness == settings.key_softness &&
            settings_.temporal_alpha == settings.temporal_alpha &&
            settings_.rvm_downsample == settings.rvm_downsample &&
            settings_.model_path == settings.model_path) {
            return;
        }
        settings_ = settings;
        settings_dirty_ = true;
        has_pending_ = false;
        has_latest_.store(false, std::memory_order_release);
        latest_out_.clear();
        latest_w_ = 0;
        latest_h_ = 0;
    }
    cv_.notify_all();
}

void AsyncMatteWorker::reset(std::uint32_t width, std::uint32_t height) {
    {
        std::lock_guard lock(mu_);
        expected_w_ = width;
        expected_h_ = height;
        has_pending_ = false;
        pending_in_.clear();
        pending_w_ = 0;
        pending_h_ = 0;
        has_latest_.store(false, std::memory_order_release);
        latest_out_.clear();
        latest_w_ = 0;
        latest_h_ = 0;
        remover_.resetTemporalState();
    }
    cv_.notify_all();
}

bool AsyncMatteWorker::trySubmit(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height) {
    if (!rgba || width == 0 || height == 0) {
        return false;
    }

    std::lock_guard lock(mu_);
    if (stop_) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    // One in-flight: skip while processing. If idle with unconsumed pending, replace
    // (latest-wins) and count the superseded frame as skipped.
    if (busy_.load(std::memory_order_relaxed)) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (has_pending_) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
    }

    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4u;
    pending_in_.resize(bytes);
    std::memcpy(pending_in_.data(), rgba, bytes);
    pending_w_ = width;
    pending_h_ = height;
    has_pending_ = true;
    submitted_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
    return true;
}

bool AsyncMatteWorker::copyLatest(std::uint8_t* out_rgba, std::uint32_t width, std::uint32_t height) const {
    if (!out_rgba || width == 0 || height == 0) {
        return false;
    }
    std::lock_guard lock(mu_);
    if (!has_latest_.load(std::memory_order_relaxed) || latest_w_ != width || latest_h_ != height) {
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4u;
    if (latest_out_.size() < bytes) {
        return false;
    }
    std::memcpy(out_rgba, latest_out_.data(), bytes);
    return true;
}

std::string AsyncMatteWorker::status() const {
    std::lock_guard lock(mu_);
    return status_;
}

bool AsyncMatteWorker::isRvmReady() const {
    std::lock_guard lock(mu_);
    return rvm_ready_;
}

BgRemoverBackend AsyncMatteWorker::activeBackend() const {
    std::lock_guard lock(mu_);
    return active_backend_;
}

void AsyncMatteWorker::applySettingsLocked() {
    remover_.setSettings(settings_);
    std::string st;
    remover_.initialize(&st);
    status_ = st;
    rvm_ready_ = remover_.isRvmReady();
    active_backend_ = remover_.activeBackend();
    settings_dirty_ = false;
}

void AsyncMatteWorker::threadMain() {
    for (;;) {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [&] { return stop_ || has_pending_ || settings_dirty_; });
            if (stop_) {
                return;
            }
            if (settings_dirty_) {
                applySettingsLocked();
                if (!has_pending_) {
                    continue;
                }
            }
            if (!has_pending_) {
                continue;
            }
            w = pending_w_;
            h = pending_h_;
            work_in_.swap(pending_in_);
            has_pending_ = false;
            pending_w_ = 0;
            pending_h_ = 0;
            busy_.store(true, std::memory_order_release);
        }

        const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
        work_out_.resize(bytes);
        const bool ok = remover_.process(work_in_.data(), work_out_.data(), w, h);

        {
            std::lock_guard lock(mu_);
            if (ok) {
                latest_out_.swap(work_out_);
                latest_w_ = w;
                latest_h_ = h;
                has_latest_.store(true, std::memory_order_release);
                status_ = remover_.status();
                rvm_ready_ = remover_.isRvmReady();
                active_backend_ = remover_.activeBackend();
                completed_.fetch_add(1, std::memory_order_relaxed);
            }
            busy_.store(false, std::memory_order_release);
        }
        cv_.notify_all();
    }
}

} // namespace genesis
