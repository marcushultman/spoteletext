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

bool curl_perform_and_check(CURL *curl) {
  long status{0};
  return curl_easy_perform(curl) ||
         curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) ||
         status != 200;
}

std::string nextStr(jq_state *jq) {
  auto jv = jq_next(jq);
  if (jv_get_kind(jv) != JV_KIND_STRING) {
    jv_free(jv);
    return "";
  }
  auto val = std::string{jv_string_value(jv)};
  jv_free(jv);
  return val;
}

double nextNumber(jq_state *jq) {
  auto jv = jq_next(jq);
  if (jv_get_kind(jv) != JV_KIND_NUMBER) {
    jv_free(jv);
    return 0;
  }
  auto val = jv_number_value(jv);
  jv_free(jv);
  return val;
}

std::chrono::seconds nextSeconds(jq_state *jq) {
  using sec = std::chrono::seconds;
  return sec{static_cast<sec::rep>(nextNumber(jq))};
}

struct DeviceFlowData {
  std::string device_code;
  std::string user_code;
  // std::chrono::seconds expires_in;  // 3599
  std::string verification_url;
  std::string verification_url_prefilled;
  std::chrono::seconds interval;
};

DeviceFlowData parseDeviceFlowData(jq_state *jq, const std::string &buffer) {
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".device_code, .user_code, .verification_url, .verification_url_prefilled, .interval");
  jq_start(jq, input, 0);
  return {nextStr(jq), nextStr(jq), nextStr(jq), nextStr(jq), nextSeconds(jq)};
}

struct AuthCodeData {
  std::string error;
  std::string auth_code;
};

AuthCodeData parseAuthCodeData(jq_state *jq, const std::string &buffer) {
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".error, .auth_code");
  jq_start(jq, input, 0);
  return {nextStr(jq), nextStr(jq)};
}

struct TokenData {
  std::string access_token;
  // std::string token_type;  // "Bearer";
  // std::chrono::seconds expires_in;  // 3600;
  std::string refresh_token;
  // std::string scope;
  // std::string client_id;
  // std::string client_secret;
};

TokenData parseTokenData(jq_state *jq, const std::string &buffer) {
  auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".access_token, .refresh_token");
  jq_start(jq, input, 0);
  return {nextStr(jq), nextStr(jq)};
}

std::string parseContext(jq_state *jq, const std::string &buffer) {
  auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".name");
  jq_start(jq, input, 0);
  return nextStr(jq);
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
  for (;;) {
    index = str.find("é");
    if (index != std::string::npos) {
      str.replace(index, 2, "`");
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
  _jq = jq_init();
}

Spoteefax::~Spoteefax() {
  if (_curl) {
    curl_easy_cleanup(_curl);
    _curl = nullptr;
  }
  if (_jq) {
    jq_teardown(&_jq);
  }
}

int Spoteefax::run() {
  if (!_curl || !_jq) {
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

    std::string buffer;
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

    if (curl_perform_and_check(_curl)) {
      std::cerr << "failed to get device_code" << std::endl;
      return;
    }
    auto res = parseDeviceFlowData(_jq, buffer);
    std::cerr << "url: " << res.verification_url_prefilled << std::endl;

    if (authenticateCode(res.device_code, res.user_code, res.verification_url, res.interval)) {
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

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  if (curl_easy_perform(_curl)) {
    std::cerr << "failed to get auth_code or error" << std::endl;
    return kPollError;
  }
  auto res = parseAuthCodeData(_jq, buffer);
  if (!res.error.empty()) {
    std::cerr << "auth_code error: " << res.error << std::endl;
    return res.error == "authorization_pending" ? kPollWait : kPollError;
  }
  auth_code = res.auth_code;
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

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  if (curl_perform_and_check(_curl)) {
    std::cerr << "failed to get token" << std::endl;
    return false;
  }
  auto res = parseTokenData(_jq, buffer);
  std::cerr << "access_token: " << res.access_token << std::endl;
  std::cerr << "refresh_token: " << res.refresh_token << std::endl;

  _access_token = res.access_token;
  _refresh_token = res.refresh_token;
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

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  if (curl_perform_and_check(_curl)) {
    std::cerr << "failed to refresh token" << std::endl;
    return false;
  }
  auto res = parseTokenData(_jq, buffer);
  std::cerr << "access_token: " << res.access_token << std::endl;

  _access_token = res.access_token;

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

  jsproperty::extractor context{"href"},
                        title{"name"},
                        artist{"name"},
                        image{"url"},
                        image_height{"height"};
  filter(buffer, "\"context\"", 1, 0, context);
  filter(buffer, "\"item\"", 1, 0, title);
  filter(buffer, "\"artists\"", 2, 0, artist);
  filter(buffer, "\"images\"", 3, 2, image);
  filter(buffer, "\"images\"", 3, 2, image_height);

  if (context && title && artist) {
    fixWideChars(*title);
    fixWideChars(*artist);

    if (*title == _now_playing.title) {
      return true;
    }

    _now_playing = {"", *title, *artist};
    fetchContext(*context);
    fetchImage(image ? *image : "");

    std::cerr << "context: " << _now_playing.context.c_str() << std::endl;
    std::cerr << "track_name: " << _now_playing.title.c_str() << std::endl;
    std::cerr << "artist_name: " << _now_playing.artist.c_str() << std::endl;
    std::cerr << "image: " << image->c_str() << std::endl;
  } else {
    _now_playing = {};
  }

  return true;
}

void Spoteefax::fetchContext(const std::string &url) {
  const auto auth_header = kAuthorizationBearer + _access_token;
  const auto header = curl_slist_append(nullptr, auth_header.c_str());

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  if (curl_perform_and_check(_curl)) {
    std::cerr << "failed to fetch context" << std::endl;
    return;
  }
  _now_playing.context = parseContext(_jq, buffer);
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
  const auto title_offset = std::max<int>(0, (38 - _now_playing.title.substr(0, 38).size()) / 2);
  const auto artist_offset = std::max<int>(0, (40 - _now_playing.artist.substr(0, 40).size()) / 2);

  using namespace templates;
  std::ofstream file{_out_file, std::ofstream::binary};
  file.write(kNpv, kNpvContextOffset);
  file << " " << _now_playing.context.c_str();

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
  file << _now_playing.title.substr(0, 38).c_str();
  file.write(kNpv + kNpvTitleOffset, kNpvArtistOffset - kNpvTitleOffset);
  for (auto i = 0; i < artist_offset; ++i) {
    file << " ";
  }
  file << _now_playing.artist.substr(0, 40).c_str();
  file.write(kNpv + kNpvArtistOffset, strlen(kNpv) - kNpvArtistOffset);
}


}  // namespace spoteefax
