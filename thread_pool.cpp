#include "thread_pool.h"
#include "logger.h"

ThreadPool::ThreadPool(size_t thread_num) {
  for (size_t i = 0; i < thread_num; ++i) {
    threads_.emplace_back(&ThreadPool::worker, this);
    LOG_DEBUG("线程池创建线程: " + std::to_string(i));
  }
  LOG_INFO("线程池初始化完成，线程数: " + std::to_string(thread_num));
}

ThreadPool::~ThreadPool() { stop(); }

void ThreadPool::submit(std::function<void()> task) {
  if (stop_flag_) {
    LOG_ERROR("线程池已停止，无法提交任务");
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  tasks_.emplace(std::move(task));
  cond_var_.notify_one();
}

void ThreadPool::stop() {
  stop_flag_ = true;
  cond_var_.notify_all();

  for (auto &thread : threads_) {
    if (thread.joinable()) {
      thread.join();
      LOG_DEBUG("线程已退出");
    }
  }

  LOG_INFO("线程池已停止");
}

void ThreadPool::worker() {
  while (!stop_flag_) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      cond_var_.wait(lock, [this]() { return stop_flag_ || !tasks_.empty(); });

      if (stop_flag_ && tasks_.empty()) {
        return;
      }

      task = std::move(tasks_.front());
      tasks_.pop();
    }

    try {
      task();
    } catch (const std::exception &e) {
      LOG_ERROR("线程执行任务异常: " + std::string(e.what()));
    } catch (...) {
      LOG_ERROR("线程执行任务未知异常");
    }
  }
}