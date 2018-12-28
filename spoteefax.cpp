#include "spoteefax.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <jpeglib.h>
#include <iostream>
#include <thread>
#include <vector>
#include "jsp_filter.h"
#include "jspextractor.h"
#include "spclient.h"
#include "sptemplates.h"

namespace spoteefax {
namespace {

const auto kAuthDeviceCodeUrl = "https://accounts.spotify.com/api/device/code";
const auto kAuthTokenUrl = "https://accounts.spotify.com/api/token";
const auto kPlayerUrl = "https://api.spotify.com/v1/me/player";

const auto kContentTypeXWWWFormUrlencoded = "Content-Type: application/x-www-form-urlencoded";
const auto kAuthorizationBearer = "Authorization: Bearer ";

struct DeviceFlowData {
  jsproperty::extractor device_code{"device_code"};
  jsproperty::extractor user_code{"user_code"};
  // jsproperty::extractor expires_in{"expires_in"};  // 3599
  jsproperty::extractor verification_url{"verification_url"};
  jsproperty::extractor verification_url_prefilled{"verification_url_prefilled"};  // https://spotify.com/pair?code\u003dXXXXXX
  jsproperty::extractor interval_str{"interval"};
  operator bool() { return device_code && user_code && verification_url && interval_str; }

  std::chrono::seconds interval() {
    return std::chrono::seconds{std::stoi(interval_str->c_str())};
  }
};

size_t extractDeviceFlowData(char *ptr, size_t size, size_t nmemb, void *obj) {
  auto &data = *static_cast<DeviceFlowData*>(obj);
  size *= nmemb;
  data.device_code.feed(ptr, size);
  data.user_code.feed(ptr, size);
  data.verification_url.feed(ptr, size);
  data.verification_url_prefilled.feed(ptr, size);
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

size_t bufferString(char *ptr, size_t size, size_t nmemb, void *obj) {
  size *= nmemb;
  static_cast<std::string*>(obj)->append(ptr, size);
  return size;
}

size_t bufferArray(char *ptr, size_t size, size_t nmemb, void *obj) {
  size *= nmemb;
  auto &v = *static_cast<std::vector<unsigned char>*>(obj);
  v.insert(v.end(), ptr, ptr + size);
  return size;
}

void fixWideChars(std::string &str) {
  auto index = std::string::npos;
  for (;;) {
    index = str.find("å");
    if (index != std::string::npos) {
      str.replace(index, 2, "}");
      continue;
    }
    break;
  }
  for (;;) {
    index = str.find("ä");
    if (index != std::string::npos) {
      str.replace(index, 2, "{");
      continue;
    }
    break;
  }
  for (;;) {
    index = str.find("ö");
    if (index != std::string::npos) {
      str.replace(index, 2, "|");
      continue;
    }
    break;
  }
}

}  // namespace

Spoteefax::Spoteefax(const std::string &page_dir)
    : _out_file{page_dir + "/P100-3F7F.tti"},
      _image{std::make_unique<image::Image>(20, 14)} {
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
  std::remove(_out_file.c_str());
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

    long status{};
    if (curl_easy_perform(_curl) || curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status) || !res || status != 200) {
      std::cerr << "failed to get device_code" << std::endl;
      return;
    }
    std::cerr << "user_code: " << res.user_code->c_str() << std::endl;
    std::cerr << "url: " << res.verification_url_prefilled->c_str() << std::endl;

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
  if (res.error) {
    std::cerr << "auth_code error: " << res.error->c_str() << std::endl;
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

  long status{};
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

bool Spoteefax::refreshToken() {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
      "&client_secret=" + credentials::kClientSecret +
      "&refresh_token=" + _refresh_token +
      "&grant_type=refresh_token";

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, data.c_str());
  curl_easy_setopt(_curl, CURLOPT_URL, kAuthTokenUrl);

  TokenData res;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &res);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, extractTokenData);

  long status{};
  if (curl_easy_perform(_curl) || curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status) || !res || status != 200) {
    std::cerr << "failed to refresh token" << std::endl;
    return false;
  }
  std::cerr << "access_token: " << res.access_token->c_str() << std::endl;

  _access_token = *res.access_token;

  curl_easy_reset(_curl);
  return true;
}

void Spoteefax::loop() {
  curl_easy_reset(_curl);
  for (;;) {
    if (!fetchNowPlaying(true)) {
      break;
    }
    displayNPV();
    std::this_thread::sleep_for(std::chrono::seconds{5});
  }
}

void Spoteefax::displayCode(const std::string &code, const std::string &url) {
  std::cerr << "display code: " << code.c_str() << std::endl;
  using namespace templates;
  const auto url_offset = std::max<int>(0, (36 - url.size()) / 2);
  const auto code_offset = std::max<int>(0, (36 - code.size()) / 2);
  std::ofstream file{_out_file, std::ofstream::binary};
  file.write(kPair, kPairUrlOffset);
  for (auto i = 0; i < url_offset; ++i) {
    file << " ";
  }
  file << url.c_str();
  file.write(kPair + kPairUrlOffset, kPairCodeOffset - kPairUrlOffset);
  for (auto i = 0; i < code_offset; ++i) {
    file << " ";
  }
  file << " " << code.c_str();
  file.write(kPair + kPairCodeOffset, strlen(kPair) - kPairCodeOffset);
}

bool Spoteefax::fetchNowPlaying(bool retry) {
  const auto auth_header = kAuthorizationBearer + _access_token;
  const auto header = curl_slist_append(nullptr, auth_header.c_str());

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_URL, kPlayerUrl);

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  long status{};
  bool request_error =
      curl_easy_perform(_curl) || curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status);
  if (request_error || status >= 400) {
    if (!retry) {
      std::cerr << "failed to get now playing data (" << status << ")" << std::endl;
    }
    return retry && refreshToken() && fetchNowPlaying(false);
  }
  if (status == 204) {
    std::cerr << "nothing is playing" << std::endl;
    return true;
  }

  jsproperty::extractor context{"href"}, title{"name"}, artist{"name"}, image{"url"}, image_height{"height"};
  filter(buffer, "\"context\"", 1, 0, context);
  filter(buffer, "\"item\"", 1, 0, title);
  filter(buffer, "\"artists\"", 2, 0, artist);
  filter(buffer, "\"images\"", 3, 2, image);
  filter(buffer, "\"images\"", 3, 2, image_height);

  if (context && title && artist) {
    fixWideChars(*context);
    fixWideChars(*title);
    fixWideChars(*artist);

    if (*title == _now_playing.title) {
      return true;
    }
    fetchImage(image ? *image : "");
    _now_playing = {"", *title, *artist};

    std::cerr << "context: " << _now_playing.context.c_str() << std::endl;
    std::cerr << "track_name: " << _now_playing.title.c_str() << std::endl;
    std::cerr << "artist_name: " << _now_playing.artist.c_str() << std::endl;
    std::cerr << "image: " << image->c_str() << std::endl;
  } else {
    _now_playing = {};
  }

  return true;
}

void Spoteefax::fetchImage(const std::string &url) {
  if (url.empty()) {
    _image->clear();
    return;
  }

  curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());

  std::vector<unsigned char> buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferArray);

  long status{};
  if (curl_easy_perform(_curl) || curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status) || status != 200) {
    std::cerr << "failed to fetch image (" << status << ")" << std::endl;
    return;
  }

  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, &buffer[0], buffer.size());

  auto result = jpeg_read_header(&cinfo, TRUE);
  if (result != 1) {
    return;
  }
  jpeg_start_decompress(&cinfo);

  const auto row_stride = cinfo.output_width * cinfo.output_components;
  std::vector<unsigned char> bmp_buffer(cinfo.output_height * row_stride);

  while (cinfo.output_scanline < cinfo.output_height) {
    auto *buffer = &bmp_buffer[cinfo.output_scanline * row_stride];
    jpeg_read_scanlines(&cinfo, &buffer, 1);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  buffer.clear();

  _image->setSrc(cinfo.output_width, cinfo.output_height, cinfo.output_components, &bmp_buffer[0]);

#if !(RASPBIAN)
  for (auto y = 0u; y < 3 * _image->height(); ++y) {
    // Original
    for (auto x = 0u; x < 2 * _image->width(); ++x) {
      std::cerr << "\033[1;" << +_image->get(x, y) << "m  ";
    }
    std::cerr << "\033[0m    ";

    // Reconstructed
    auto line = _image->line(y / 3);
    auto *bg = &image::kColors[0];
    auto *fg = &image::kColors[0];

    for (auto it = line.begin(); it != line.end(); ++it) {
      if (*it == '\u001b') {
        ++it;
        if (*it == ']' || *it == '\\') {
          bg = *it == '\\' ? &image::kColors[0] : fg;
          std::cerr << "\033[1;" << +bg->terminal_code << "mb" << +bg->terminal_code << ".";
        } else {
          fg = std::find_if(
              image::kColors.begin(), image::kColors.end(), [it](auto &c) { return c.code == *it; });
          std::cerr << "\033[1;" << +bg->terminal_code << "mf" << +fg->terminal_code << ".";
          std::cerr << "\033[1;" << +fg->terminal_code << "m";
        }
        continue;
      }
      if (y % 3 == 0) {
        std::cerr << "\033[1;" << (*it & 1 << 0 ? +fg->terminal_code : +bg->terminal_code) << "m  ";
        std::cerr << "\033[1;" << (*it & 1 << 1 ? +fg->terminal_code : +bg->terminal_code) << "m  ";
      } else if (y % 3 == 1) {
        std::cerr << "\033[1;" << (*it & 1 << 2 ? +fg->terminal_code : +bg->terminal_code) << "m  ";
        std::cerr << "\033[1;" << (*it & 1 << 3 ? +fg->terminal_code : +bg->terminal_code) << "m  ";
      } else if (y % 3 == 2) {
        std::cerr << "\033[1;" << (*it & 1 << 4 ? +fg->terminal_code : +bg->terminal_code) << "m  ";
        std::cerr << "\033[1;" << (*it & 1 << 6 ? +fg->terminal_code : +bg->terminal_code) << "m  ";
      }
    }
    std::cerr << "\033[0m";
    std::cerr << std::endl;
  }
#endif
}

void Spoteefax::displayNPV() {
  const auto title_offset = std::max<int>(0, (38 - _now_playing.title.size()) / 2);
  const auto artist_offset = std::max<int>(0, (40 - _now_playing.artist.size()) / 2);

  using namespace templates;
  std::ofstream file{_out_file, std::ofstream::binary};
  file.write(kNpv, kNpvContextOffset);
  file << " " << _now_playing.context;

  file.write(kNpv + kNpvContextOffset, kNpvImageOffset - kNpvContextOffset);
  for (auto i = 0; i < kNpvImageHeight; ++i) {
    file << "OL," << (4 + i) << ",         ";
    if (_image) {
      file << _image->line(i).c_str();
    }
    file << "\n";
  }

  file.write(kNpv + kNpvImageOffset, kNpvTitleOffset - kNpvImageOffset);
  for (auto i = 0; i < title_offset; ++i) {
    file << " ";
  }
  file << _now_playing.title.substr(0, 38);
  file.write(kNpv + kNpvTitleOffset, kNpvArtistOffset - kNpvTitleOffset);
  for (auto i = 0; i < artist_offset; ++i) {
    file << " ";
  }
  file << _now_playing.artist.substr(0, 40);
  file.write(kNpv + kNpvArtistOffset, strlen(kNpv) - kNpvArtistOffset);
}


}  // namespace spoteefax
