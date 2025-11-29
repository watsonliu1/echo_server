#include "logger.h"
#include "config.h"
#include <ctime>
#include <iostream>
#include <sstream>

Logger &Logger::get_instance() {
  static Logger instance;
  return instance;
}

void Logger::set_log_file(const std::string &file_path) {
  log_file_.open(file_path, std::ios::out | std::ios::app);
  if (log_file_.is_open()) {
    file_output_enabled_ = true;
    LOG_INFO("日志文件已打开: " + file_path);
  } else {
    LOG_ERROR("日志文件打开失败: " + file_path);
  }
}

void Logger::set_log_level(LogLevel level) {
  log_level_ = level;
  LOG_INFO("日志级别设置为: " + level_to_str(level));
}

void Logger::debug(const std::string &msg) {
  if (is_level_enabled(LogLevel::DEBUG)) {
    log(LogLevel::DEBUG, msg);
  }
}

void Logger::info(const std::string &msg) {
  if (is_level_enabled(LogLevel::INFO)) {
    log(LogLevel::INFO, msg);
  }
}

void Logger::error(const std::string &msg) {
  if (is_level_enabled(LogLevel::ERROR)) {
    log(LogLevel::ERROR, msg);
  }
}

Logger::~Logger() {
  if (log_file_.is_open()) {
    log_file_.close();
  }
}

void Logger::log(LogLevel level, const std::string &msg) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string log_str = "[" + get_current_time() + "] [" + level_to_str(level) +
                        "] " + msg + "\n";

  std::cout << log_str;
  if (file_output_enabled_ && log_file_.is_open()) {
    log_file_ << log_str;
    log_file_.flush();
  }
}

std::string Logger::get_current_time() {
  time_t now = time(nullptr);
  struct tm tm_info;
  localtime_r(&now, &tm_info);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
  return std::string(buf);
}

std::string Logger::level_to_str(LogLevel level) {
  switch (level) {
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

bool Logger::is_level_enabled(LogLevel level) {
  return static_cast<int>(level) >= static_cast<int>(log_level_);
}