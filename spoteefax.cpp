#include "spoteefax.h"

#include <iostream>
#include <thread>
#include "jspextractor.h"
#include "spclient.h"

namespace spoteefax {
namespace {

const auto kAuthDeviceCodeUrl = "https://accounts.spotify.com/api/device/code";
const auto kAuthTokenUrl = "https://accounts.spotify.com/api/token";

const auto kContentTypeXWWWFormUrlencoded = "Content-Type: application/x-www-form-urlencoded";

struct DeviceFlowData {
  jsproperty::extractor device_code{"device_code"};
  jsproperty::extractor user_code{"user_code"};
  // jsproperty::extractor expires_in{"expires_in"};  // 3599
  jsproperty::extractor verification_url{"verification_url"};
  // jsproperty::extractor verification_url_prefilled{"verification_url_prefilled"};  // https://spotify.com/pair?code\u003dXXXXXX
  jsproperty::extractor interval_str{"interval"};
  operator bool() { return device_code && user_code && verification_url && interval_str; }

  std::chrono::seconds interval() const {
    return std::chrono::seconds{std::stoi(interval_str->c_str())};
  }
};

size_t extractDeviceFlowData(char *ptr, size_t size, size_t nmemb, void *obj) {
  auto &data = *static_cast<DeviceFlowData*>(obj);
  size *= nmemb;
  data.device_code.feed(ptr, size);
  data.user_code.feed(ptr, size);
  data.verification_url.feed(ptr, size);
  data.interval_str.feed(ptr, size);
  return size;
}

struct AuthCodeData {
  jsproperty::extractor error{"error"};
  jsproperty::extractor auth_code{"auth_code"};
  operator bool() { return error || auth_code; }
};

size_t extractAuthCodeData(char *ptr, size_t size, size_t nmemb, void *obj) {
  auto &data = *static_cast<AuthCodeData*>(obj);
  size *= nmemb;
  data.error.feed(ptr, size);
  data.auth_code.feed(ptr, size);
  return size;
}

struct TokenData {
  jsproperty::extractor access_token{"access_token"};
  // jsproperty::extractor token_type{"token_type"};  // "Bearer";
  // jsproperty::extractor expires_in{"expires_in"};  // std::chrono::seconds{3600};
  jsproperty::extractor refresh_token{"refresh_token"};
  // jsproperty::extractor scope{"scope"};
  // jsproperty::extractor client_id{"client_id"};
  // jsproperty::extractor client_secret{"client_secret"};
  operator bool() { return access_token; }
};

size_t extractTokenData(char *ptr, size_t size, size_t nmemb, void *obj) {
  auto &data = *static_cast<TokenData*>(obj);
  size *= nmemb;
  data.access_token.feed(ptr, size);
  data.refresh_token.feed(ptr, size);
  return size;
}

}  // namespace

Spoteefax::Spoteefax() {
  _curl = curl_easy_init();
}

Spoteefax::~Spoteefax() {
  if (_curl) {
    curl_easy_cleanup(_curl);
    _curl = nullptr;
  }
}

int Spoteefax::run() {
  if (!_curl) {
    return 1;
  }
  authenticate();
  loop();
  return 0;
}

void Spoteefax::authenticate() {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
      "&scope=user-read-playback-state"
      "&description=spoteefax";

  for (;;) {
    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
    curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(_curl, CURLOPT_URL, kAuthDeviceCodeUrl);

    DeviceFlowData res;
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &res);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, extractDeviceFlowData);

    long status;
    if (curl_easy_perform(_curl) || curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status) || !res || status != 200) {
      std::cerr << "failed to get device_code" << std::endl;
      return;
    }
    std::cerr << "device_code: " << res.device_code->c_str() << std::endl;
    std::cerr << "user_code: " << res.user_code->c_str() << std::endl;
    std::cerr << "interval_str: " << res.interval_str->c_str() << std::endl;

    if (authenticateCode(*res.device_code, *res.user_code, *res.verification_url, res.interval())) {
      return;
    }
  }
}

bool Spoteefax::authenticateCode(const std::string &device_code,
                                 const std::string &user_code,
                                 const std::string &verification_url,
                                 const std::chrono::seconds &interval) {
  displayCode(user_code, verification_url);

  std::string auth_code;
  if (!poll(device_code, interval, auth_code)) {
    return false;
  }
  if (!fetchTokens(auth_code)) {
    return false;
  }
  return true;
}

bool Spoteefax::poll(const std::string &device_code,
                     const std::chrono::seconds &interval,
                     std::string &auth_code) {
  while (auto res = tryAuth(device_code, auth_code)) {
    if (res == kPollError) {
      return false;
    }
    std::this_thread::sleep_for(interval);
  }
  return true;
}

Spoteefax::PollResult Spoteefax::tryAuth(const std::string &device_code, std::string &auth_code) {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
      "&code=" + device_code +
      "&scope=user-read-playback-state"
      "&grant_type=http://spotify.com/oauth2/device/1";

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, data.c_str());
  curl_easy_setopt(_curl, CURLOPT_URL, kAuthTokenUrl);

  AuthCodeData res;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &res);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, extractAuthCodeData);

  if (curl_easy_perform(_curl) || !res) {
    std::cerr << "failed to get auth_code or error" << std::endl;
    return kPollError;
  }
  std::cerr << "auth_code: " << res.auth_code->c_str() << " error: " << res.error->c_str() << std::endl;

  if (res.error) {
    return *res.error == "authorization_pending" ? kPollWait : kPollError;
  }
  auth_code = *res.auth_code;
  return kPollSuccess;
}

bool Spoteefax::fetchTokens(const std::string &code) {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
      "&client_secret=" + credentials::kClientSecret +
      "&code=" + code +
      "&grant_type=authorization_code";

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, data.c_str());
  curl_easy_setopt(_curl, CURLOPT_URL, kAuthTokenUrl);

  TokenData res;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &res);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, extractTokenData);

  long status;
  if (curl_easy_perform(_curl) || curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status) || !res || status != 200) {
    std::cerr << "failed to get token" << std::endl;
    return false;
  }
  std::cerr << "access_token: " << res.access_token->c_str() << std::endl;
  std::cerr << "refresh_token: " << res.refresh_token->c_str() << std::endl;

  _access_token = *res.access_token;
  _refresh_token = *res.refresh_token;
  return true;
}

void Spoteefax::loop() {
  for (;;) {
    displayNPV();
    std::this_thread::sleep_for(std::chrono::seconds{5});
  }
}

void Spoteefax::displayCode(const std::string &user_code, const std::string &verification_url) {
  // todo: display code
  std::cerr << "display: " << user_code.c_str() << std::endl;
}

void Spoteefax::displayNPV() {
  // todo: display Now Playing view
}


}  // namespace spoteefax
