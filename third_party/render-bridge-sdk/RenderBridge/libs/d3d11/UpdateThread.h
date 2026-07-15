#pragma once

#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>

class UpdateThread {
private:
  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_callback_set;
  std::condition_variable m_callback_done;
  bool m_shutdown{ };
  std::function<void()> m_callback;

public:
  UpdateThread()
    : m_thread(&UpdateThread::thread_func, this) {
  }

  ~UpdateThread() {
    auto lock = std::unique_lock(m_mutex);
    m_shutdown = true;
    lock.unlock();
    m_callback_set.notify_one();
    m_thread.join();
  }

  void dispatch(std::function<void()>&& callback) {
    auto lock = std::unique_lock(m_mutex);
    m_callback = std::move(callback);
    m_callback_set.notify_one();
    m_callback_done.wait(lock, [&]() { return !static_cast<bool>(m_callback); });
  }

private:
  void thread_func() {
    auto lock = std::unique_lock(m_mutex);
    for (;;) {
      m_callback_set.wait(lock, [&]() { return (m_shutdown || static_cast<bool>(m_callback)); });
      if (m_shutdown)
        break;
      lock.unlock();

      m_callback();

      lock.lock();
      m_callback = nullptr;
      m_callback_done.notify_one();
    }
  }
};
