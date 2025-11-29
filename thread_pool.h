#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
  explicit ThreadPool(size_t thread_num);
  ~ThreadPool();

  void submit(std::function<void()> task);
  void stop();

private:
  void worker();

private:
  std::vector<std::thread> threads_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cond_var_;
  std::atomic<bool> stop_flag_ = false;
};

#endif // THREAD_POOL_H