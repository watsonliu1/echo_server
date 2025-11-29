#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

class Logger {
public:
  static Logger &get_instance();

  void set_log_file(const std::string &file_path);
  void set_log_level(LogLevel level);

  void debug(const std::string &msg);
  void info(const std::string &msg);
  void error(const std::string &msg);

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

private:
  Logger() = default;
  ~Logger();

  void log(LogLevel level, const std::string &msg);
  std::string get_current_time();
  std::string level_to_str(LogLevel level);
  bool is_level_enabled(LogLevel level);

private:
  std::ofstream log_file_;
  std::mutex mutex_;
  bool file_output_enabled_ = false;
  LogLevel log_level_ = LogLevel::INFO;
};

#define LOG_DEBUG(msg) Logger::get_instance().debug(msg)
#define LOG_INFO(msg) Logger::get_instance().info(msg)
#define LOG_ERROR(msg) Logger::get_instance().error(msg)

#endif // LOGGER_H