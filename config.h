#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"
#include <cstdint>
#include <string>
#include <unordered_map>

class Config {
public:
  static Config &get_instance();

  bool load(const std::string &file_path);

  std::string get_string(const std::string &section, const std::string &key,
                         const std::string &default_val = "");
  int get_int(const std::string &section, const std::string &key,
              int default_val = 0);
  LogLevel get_log_level(const std::string &section, const std::string &key,
                         LogLevel default_val = LogLevel::INFO);

  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;

private:
  Config() = default;

  void parse_line(const std::string &line, std::string &current_section);
  std::string trim(const std::string &str);

private:
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      data_;
};

#endif // CONFIG_H