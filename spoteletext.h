#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <curl/curl.h>
#include <jpeglib.h>
#include "image.h"

extern "C" {
#include <jq.h>
}

namespace teletext {

struct NowPlaying {
  std::string track_id;
  std::string context_href;
  std::string context;
  std::string title;
  std::string artist;
  std::string image;
};

class Spoteletext {
 private:
  enum AuthResult {
    kAuthSuccess = 0,
    kAuthError,
  };
  enum PollResult {
    kPollSuccess = 0,
    kPollWait,
    kPollError,
  };

 public:
  Spoteletext(CURL *curl, jq_state *jq, const std::string &page_dir);
  ~Spoteletext() = default;

  int run();

 private:
  // spotify device code auth flow
  void authenticate();
  AuthResult authenticateCode(const std::string &device_code,
                              const std::string &user_code,
                              const std::string &verification_url,
                              const std::chrono::seconds &expires_in,
                              const std::chrono::seconds &interval);
  AuthResult getAuthCode(const std::string &device_code,
                         const std::chrono::seconds &expires_in,
                         const std::chrono::seconds &interval,
                   std::string &auth_code);
  PollResult pollAuthCode(const std::string &device_code, std::string &auth_code);
  bool fetchTokens(const std::string &auth_code);
  bool refreshToken();

  // update loop
  void loop();

  bool fetchNowPlaying(bool retry);
  void fetchContext(const std::string &url);
  void fetchImage(const std::string &url);

  void displayCode(const std::string &code, const std::string &verification_url);
  void displayNPV();

  CURL *_curl{nullptr};
  jq_state *_jq{nullptr};

  std::string _access_token;
  std::string _refresh_token;
  bool _has_played{false};

  std::string _out_file;

  NowPlaying _now_playing;
  std::unique_ptr<teletext::Image> _image;
};

}  // namespace teletext
