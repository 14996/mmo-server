/**
 * @file ServerConfig.cpp
 */
#include "ServerConfig.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

std::string Trim(const std::string &value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

bool ParseInt64(const std::string &text, int64_t &out) {
  if (text.empty()) {
    return false;
  }
  errno = 0;
  char *end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0') {
    return false;
  }
  out = static_cast<int64_t>(value);
  return true;
}

bool ParseInto(const std::string &key, const std::string &value, int64_t min_value,
               int64_t max_value, int64_t &target, int line_no,
               std::string &error) {
  int64_t parsed = 0;
  if (!ParseInt64(value, parsed) || parsed < min_value || parsed > max_value) {
    std::ostringstream out;
    out << "line " << line_no << ": invalid value for " << key << ": " << value
        << " (expected " << min_value << ".." << max_value << ")";
    error = out.str();
    return false;
  }
  target = parsed;
  return true;
}

}  // namespace

bool LoadServerConfig(const std::string &path, ServerConfig &config,
                      std::string &error, bool allow_missing) {
  error.clear();
  std::ifstream in(path);
  if (!in.is_open()) {
    if (allow_missing) {
      return true;
    }
    error = "cannot open config file: " + path;
    return false;
  }

  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    line = Trim(line);
    if (line.empty()) {
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      std::ostringstream out;
      out << "line " << line_no << ": expected key=value";
      error = out.str();
      return false;
    }
    const std::string key = Trim(line.substr(0, eq));
    const std::string value = Trim(line.substr(eq + 1));
    if (key.empty() || value.empty()) {
      std::ostringstream out;
      out << "line " << line_no << ": empty key or value";
      error = out.str();
      return false;
    }

    int64_t parsed = 0;
    if (key == "server.ip") {
      config.ip = value;
    } else if (key == "server.port") {
      if (!ParseInto(key, value, 1, 65535, parsed, line_no, error)) {
        return false;
      }
      config.port = static_cast<uint16_t>(parsed);
    } else if (key == "net.heartbeat_timeout_ms") {
      if (!ParseInto(key, value, 1000, 3600000, parsed, line_no, error)) {
        return false;
      }
      config.heartbeat_timeout_ms = parsed;
    } else if (key == "net.max_packets_per_sec") {
      if (!ParseInto(key, value, 1, 1000000, parsed, line_no, error)) {
        return false;
      }
      config.max_packets_per_sec = static_cast<uint32_t>(parsed);
    } else if (key == "game.save_interval_ms") {
      if (!ParseInto(key, value, 1000, 3600000, parsed, line_no, error)) {
        return false;
      }
      config.save_interval_ms = parsed;
    } else if (key == "game.offline_retain_ms") {
      if (!ParseInto(key, value, 0, 86400000, parsed, line_no, error)) {
        return false;
      }
      config.offline_retain_ms = parsed;
    } else if (key == "game.aoi_cell_size") {
      if (!ParseInto(key, value, 1, 64, parsed, line_no, error)) {
        return false;
      }
      config.aoi_cell_size = static_cast<int>(parsed);
    } else {
      std::ostringstream out;
      out << "line " << line_no << ": unknown key: " << key;
      error = out.str();
      return false;
    }
  }
  return true;
}
