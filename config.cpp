#include "config.h"
#include "logger.h"
#include <algorithm>
#include <fstream>
#include <sstream>

Config &Config::get_instance() {
  static Config instance;
  return instance;
}

bool Config::load(const std::string &file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    LOG_ERROR("配置文件打开失败: " + file_path);
    return false;
  }

  std::string line;
  std::string current_section;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }

    if (line[0] == '[' && line.back() == ']') {
      current_section = trim(line.substr(1, line.size() - 2));
      continue;
    }

    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    std::string key = trim(line.substr(0, eq_pos));
    std::string value = trim(line.substr(eq_pos + 1));
    data_[current_section][key] = value;
  }

  LOG_INFO("配置文件加载成功: " + file_path);
  return true;
}

std::string Config::get_string(const std::string &section,
                               const std::string &key,
                               const std::string &default_val) {
  auto section_it = data_.find(section);
  if (section_it == data_.end()) {
    return default_val;
  }

  auto key_it = section_it->second.find(key);
  if (key_it == section_it->second.end()) {
    return default_val;
  }

  return key_it->second;
}

int Config::get_int(const std::string &section, const std::string &key,
                    int default_val) {
  std::string value = get_string(section, key);
  if (value.empty()) {
    return default_val;
  }

  try {
    return std::stoi(value);
  } catch (...) {
    LOG_ERROR("配置项解析失败（int）: [" + section + "] " + key + "=" + value);
    return default_val;
  }
}

LogLevel Config::get_log_level(const std::string &section,
                               const std::string &key, LogLevel default_val) {
  std::string value = get_string(section, key);
  if (value.empty()) {
    return default_val;
  }

  std::transform(value.begin(), value.end(), value.begin(), ::toupper);
  if (value == "DEBUG")
    return LogLevel::DEBUG;
  if (value == "INFO")
    return LogLevel::INFO;
  if (value == "ERROR")
    return LogLevel::ERROR;

  LOG_ERROR("配置项解析失败（LogLevel）: [" + section + "] " + key + "=" +
            value);
  return default_val;
}

void Config::parse_line(const std::string &line, std::string &current_section) {
  // 已在load中实现
}

std::string Config::trim(const std::string &str) {
  size_t start = str.find_first_not_of(" \t");
  size_t end = str.find_last_not_of(" \t");
  return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
}