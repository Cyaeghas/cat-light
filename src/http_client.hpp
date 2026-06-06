#pragma once

#include <map>
#include <string>

namespace catlight {

struct HttpResponse {
  int status = 0;
  int exit_code = 0;
  std::string body;
  std::string error;

  bool ok() const { return exit_code == 0 && status >= 200 && status < 300; }
};

class HttpClient {
public:
  explicit HttpClient(int timeout_seconds = 20);

  HttpResponse request(const std::string &method,
                       const std::string &url,
                       const std::map<std::string, std::string> &headers,
                       const std::string &body = "") const;

  static bool curl_available();

private:
  int timeout_seconds_;
};

} // namespace catlight
