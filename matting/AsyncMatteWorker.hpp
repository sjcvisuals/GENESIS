#pragma once

#include "matting/BgRemover.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace genesis {

    // One-inflight worker with a latest-wins pending mailbox: render never blocks on
    // Ort::Run. Frames arriving while busy replace pending (not dropped cold), so the
    // worker starts the newest frame as soon as the current infer finishes.
class AsyncMatteWorker {
public:
    AsyncMatteWorker();
    ~AsyncMatteWorker();

    AsyncMatteWorker(const AsyncMatteWorker&) = delete;
    AsyncMatteWorker& operator=(const AsyncMatteWorker&) = delete;

    void start();
    void stop();

    // Applied on the worker before the next infer (drains pending first).
    void setSettings(const BgRemoverSettings& settings);

    // Drop pending + latest; reset temporal state. Call on resize.
    void reset(std::uint32_t width, std::uint32_t height);

    // Non-blocking. Copies into the latest-wins pending slot (even if busy).
    // Returns false only if stopped / invalid. Superseded pending frames count as skipped.
    bool trySubmit(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);

    // Copies the last completed matte (same size). Returns false if none yet.
    bool copyLatest(std::uint8_t* out_rgba, std::uint32_t width, std::uint32_t height) const;

    bool busy() const { return busy_.load(std::memory_order_acquire); }
    bool hasLatest() const { return has_latest_.load(std::memory_order_acquire); }
    std::uint64_t submitted() const { return submitted_.load(std::memory_order_relaxed); }
    std::uint64_t completed() const { return completed_.load(std::memory_order_relaxed); }
    std::uint64_t skipped() const { return skipped_.load(std::memory_order_relaxed); }
    // Last successful process() duration on the worker thread (milliseconds).
    double lastInferMs() const { return last_infer_ms_.load(std::memory_order_relaxed); }
    std::uint32_t lastInferWidth() const { return last_infer_w_.load(std::memory_order_relaxed); }
    std::uint32_t lastInferHeight() const { return last_infer_h_.load(std::memory_order_relaxed); }

    // Mirror of worker-side status (best-effort; may lag one frame).
    std::string status() const;
    bool isRvmReady() const;
    BgRemoverBackend activeBackend() const;

private:
    void threadMain();
    void applySettingsLocked();

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
    bool stop_ = true;
    bool has_pending_ = false;
    bool settings_dirty_ = false;

    std::atomic<bool> busy_{false};
    std::atomic<bool> has_latest_{false};
    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> skipped_{0};
    std::atomic<double> last_infer_ms_{0.0};
    std::atomic<std::uint32_t> last_infer_w_{0};
    std::atomic<std::uint32_t> last_infer_h_{0};

    BgRemoverSettings settings_{};
    BgRemover remover_;
    std::string status_ = "idle";
    bool rvm_ready_ = false;
    BgRemoverBackend active_backend_ = BgRemoverBackend::ChromaKey;

    std::vector<std::uint8_t> pending_in_;
    std::uint32_t pending_w_ = 0;
    std::uint32_t pending_h_ = 0;

    std::vector<std::uint8_t> work_in_;
    std::vector<std::uint8_t> work_out_;
    std::vector<std::uint8_t> latest_out_;
    std::uint32_t latest_w_ = 0;
    std::uint32_t latest_h_ = 0;
    std::uint32_t expected_w_ = 0;
    std::uint32_t expected_h_ = 0;
};

} // namespace genesis
