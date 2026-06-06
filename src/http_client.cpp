#include "http_client.hpp"

#include "util.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace catlight {
namespace {

constexpr const char *kStatusMarker = "__CAT_LIGHT_HTTP_STATUS__:";

std::string curl_config_escape(const std::string &value) {
  std::string out;
  for (char c : value) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      break;
    default:
      out.push_back(c);
    }
  }
  return out;
}

std::filesystem::path temp_config_path() {
  auto dir = std::filesystem::temp_directory_path() / "cat-light";
  std::filesystem::create_directories(dir);
  std::random_device rd;
  std::uniform_int_distribution<unsigned long long> dist;
  return dir / ("curl-" + std::to_string(dist(rd)) + ".conf");
}

std::string run_command_capture(const std::string &command, int &exit_code) {
  std::array<char, 4096> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    exit_code = -1;
    return "";
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output += buffer.data();
  }
  exit_code = pclose(pipe);
  return output;
}

int parse_status_and_strip(std::string &body) {
  auto marker = body.rfind(kStatusMarker);
  if (marker == std::string::npos) {
    return 0;
  }
  std::string status_text = trim(body.substr(marker + std::string(kStatusMarker).size()));
  body = body.substr(0, marker);
  body = trim(body);
  try {
    return std::stoi(status_text);
  } catch (...) {
    return 0;
  }
}

} // namespace

HttpClient::HttpClient(int timeout_seconds) : timeout_seconds_(timeout_seconds) {}

HttpResponse HttpClient::request(const std::string &method,
                                 const std::string &url,
                                 const std::map<std::string, std::string> &headers,
                                 const std::string &body) const {
  HttpResponse response;
  auto config_path = temp_config_path();
  try {
    std::ofstream config(config_path, std::ios::binary | std::ios::trunc);
    if (!config) {
      response.error = "cannot create curl config";
      return response;
    }
    config << "silent\n";
    config << "show-error\n";
    config << "location\n";
    config << "compressed\n";
    config << "request = \"" << curl_config_escape(method) << "\"\n";
    config << "url = \"" << curl_config_escape(url) << "\"\n";
    config << "connect-timeout = \"10\"\n";
    config << "max-time = \"" << timeout_seconds_ << "\"\n";
    config << "write-out = \"\\n" << kStatusMarker << "%{http_code}\"\n";
    for (const auto &header : headers) {
      config << "header = \"" << curl_config_escape(header.first + ": " + header.second) << "\"\n";
    }
    if (!body.empty()) {
      config << "data = \"" << curl_config_escape(body) << "\"\n";
    }
    config.close();

    std::string command = "curl --config " + shell_quote(config_path) + " 2>&1";
    response.body = run_command_capture(command, response.exit_code);
    response.status = parse_status_and_strip(response.body);
    if (!response.ok()) {
      response.error = response.body.empty() ? "curl request failed" : response.body;
    }
  } catch (const std::exception &e) {
    response.error = e.what();
  }
  std::error_code ec;
  std::filesystem::remove(config_path, ec);
  return response;
}

bool HttpClient::curl_available() {
  int exit_code = 0;
  std::string output = run_command_capture("curl --version 2>&1", exit_code);
  return exit_code == 0 && output.find("curl") != std::string::npos;
}

} // namespace catlight
