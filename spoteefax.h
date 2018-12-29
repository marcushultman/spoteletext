#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <curl/curl.h>
#include "spoteefax_image.h"

extern "C" {
#include <jq.h>
}

namespace spoteefax {

class Spoteefax {
 private:
  enum PollResult {
    kPollSuccess = 0,
    kPollWait,
    kPollError,
  };

  struct NowPlaying {
    std::string context;
    std::string title;
    std::string artist;
  };

 public:
  Spoteefax(const std::string &page_dir);
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
  bool refreshToken();

  // update loop
  void loop();

  bool fetchNowPlaying(bool retry);
  void fetchContext(const std::string &url);
  void fetchImage(const std::string &url);
  void displayCode(const std::string &code, const std::string &verification_url);
  void displayNPV();

  std::string _access_token;
  std::string _refresh_token;
  std::chrono::seconds _expires_in;

  std::string _out_file;

  NowPlaying _now_playing;
  std::unique_ptr<image::Image> _image;

  CURL *_curl{nullptr};
  jq_state *_jq{nullptr};
};

}
