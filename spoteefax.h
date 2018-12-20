#pragma once

#include <chrono>
#include <string>
#include <curl/curl.h>

namespace spoteefax {

class Spoteefax {
 private:
  enum PollResult {
    kPollSuccess = 0,
    kPollWait,
    kPollError,
  };

 public:
  Spoteefax();
  ~Spoteefax();

  int run();

 private:
  // spotify device code auth flow
  void authenticate();
  bool authenticateCode(const std::string &device_code,
                        const std::string &user_code,
                        const std::string &verification_url,
                        const std::chrono::seconds &interval);
  bool poll(const std::string &device_code,
            const std::chrono::seconds &interval,
            std::string &auth_code);
  PollResult tryAuth(const std::string &device_code, std::string &auth_code);
  bool fetchTokens(const std::string &auth_code);

  // update loop
  void loop();

  void displayCode(const std::string &user_code, const std::string &verification_url);
  void displayNPV();

  // todo: image handling

  CURL *_curl{nullptr};
  std::string _access_token;
  std::string _refresh_token;
  std::chrono::seconds _expires_in;
};

}
