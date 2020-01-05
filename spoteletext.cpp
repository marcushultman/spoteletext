#include "spoteletext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <vector>
#include "spclient.h"
#include "spotify_tag_generator.h"
#include "sptemplates.h"

namespace teletext {
namespace {

const auto kAuthDeviceCodeUrl = "https://accounts.spotify.com/api/device/code";
const auto kAuthTokenUrl = "https://accounts.spotify.com/api/token";
const auto kPlayerUrl = "https://api.spotify.com/v1/me/player";

const auto kContentTypeXWWWFormUrlencoded = "Content-Type: application/x-www-form-urlencoded";
const auto kAuthorizationBearer = "Authorization: Bearer ";

bool curl_perform_and_check(CURL *curl, long &status) {
  return curl_easy_perform(curl) || curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) ||
         status >= 400;
}

bool curl_perform_and_check(CURL *curl) {
  long status{0};
  return curl_perform_and_check(curl, status);
}

std::string nextStr(jq_state *jq) {
  const auto jv = jq_next(jq);
  if (jv_get_kind(jv) != JV_KIND_STRING) {
    jv_free(jv);
    return "";
  }
  const auto val = std::string{jv_string_value(jv)};
  jv_free(jv);
  return val;
}

double nextNumber(jq_state *jq) {
  const auto jv = jq_next(jq);
  if (jv_get_kind(jv) != JV_KIND_NUMBER) {
    jv_free(jv);
    return 0;
  }
  const auto val = jv_number_value(jv);
  jv_free(jv);
  return val;
}

std::chrono::seconds nextSeconds(jq_state *jq) {
  using sec = std::chrono::seconds;
  return sec{static_cast<sec::rep>(nextNumber(jq))};
}

std::chrono::milliseconds nextMs(jq_state *jq) {
  using ms = std::chrono::milliseconds;
  return ms{static_cast<ms::rep>(nextNumber(jq))};
}

struct DeviceFlowData {
  std::string device_code;
  std::string user_code;
  std::chrono::seconds expires_in;
  std::string verification_url;
  std::string verification_url_prefilled;
  std::chrono::seconds interval;
};

DeviceFlowData parseDeviceFlowData(jq_state *jq, const std::string &buffer) {
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq,
             ".device_code, .user_code, .expires_in, .verification_url, "
             ".verification_url_prefilled, .interval");
  jq_start(jq, input, 0);
  return {nextStr(jq), nextStr(jq), nextSeconds(jq), nextStr(jq), nextStr(jq), nextSeconds(jq)};
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
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".access_token, .refresh_token");
  jq_start(jq, input, 0);
  return {nextStr(jq), nextStr(jq)};
}

NowPlaying parseNowPlaying(jq_state *jq, const std::string &buffer) {
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq,
             ".item.id,"
             ".context.href,"
             ".item.name,"
             "(.item.artists | map(.name) | join(\", \")),"
             "(.item.album.images | min_by(.width)).url,"
             ".item.uri,"
             ".progress_ms,"
             ".item.duration_ms");
  jq_start(jq, input, 0);
  return {nextStr(jq),
          nextStr(jq),
          "",
          nextStr(jq),
          nextStr(jq),
          nextStr(jq),
          nextStr(jq),
          nextMs(jq),
          nextMs(jq)};
}

std::string parseContext(jq_state *jq, const std::string &buffer) {
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".name");
  jq_start(jq, input, 0);
  return nextStr(jq);
}

Scannable parseScannable(jq_state *jq, const std::string &buffer) {
  const auto input = jv_parse(buffer.c_str());
  jq_compile(jq, ".id0, .id1");
  jq_start(jq, input, 0);
  const auto id0 = nextStr(jq), id1 = nextStr(jq);
  if (id0.empty() || id1.empty()) {
    return {};
  }
  return {std::stoull(id0), std::stoull(id1)};
}

size_t bufferString(char *ptr, size_t size, size_t nmemb, void *obj) {
  size *= nmemb;
  static_cast<std::string *>(obj)->append(ptr, size);
  return size;
}

size_t bufferArray(char *ptr, size_t size, size_t nmemb, void *obj) {
  size *= nmemb;
  auto &v = *static_cast<std::vector<unsigned char> *>(obj);
  v.insert(v.end(), ptr, ptr + size);
  return size;
}

// NRC Swedish
std::string toCP1106(std::string str) {
  static const std::map<std::string, char> CP1106 = {
      // {"#", '#'},
      {"É", '@'},
      {"Ä", '['},
      {"Ö", '\\'},
      {"Å", ']'},
      {"Ü", '^'},
      // {"_", '_'},
      {"é", '`'},
      {"ä", '{'},
      {"ö", '|'},
      {"å", '}'},
      {"ü", '~'},
  };
  auto index = std::string::npos;
  for (auto it = CP1106.begin(); it != CP1106.end();) {
    index = str.find(it->first);
    if (index != std::string::npos) {
      str.replace(index, it->first.size(), 1, it->second);
      continue;
    }
    ++it;
  }
  return str;
}

std::tm chronoToTm(std::chrono::milliseconds duration) {
  using namespace std::chrono;
  const auto s = static_cast<int>(duration_cast<seconds>(duration % minutes{1}).count());
  const auto m = static_cast<int>(duration_cast<minutes>(duration).count());
  return {s, m, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

unsigned char renderScannableBarCell(int bar, int y) {
  unsigned char out = (1 << 5) | (1 << 1) | (1 << 3) | (1 << 6);
  y -= 3;
  y *= 3;
  bar += y < 0 ? 1 : -1;
  if (std::abs(y + 0) > bar) out |= (1 << 0);
  if (std::abs(y + 1) > bar) out |= (1 << 2);
  if (std::abs(y + 2) > bar) out |= (1 << 4);
  return out;
}

}  // namespace

Spoteletext::Spoteletext(CURL *curl, jq_state *jq, const std::string &page_dir)
    : _curl{curl},
      _jq{jq},
      _out_file{page_dir + "/P100-3F7F.tti"},
      _scannable_file{page_dir + "/P101-3F7F.tti"},
      _image{std::make_unique<teletext::Image>(20, 14)} {}

int Spoteletext::run() {
  std::remove(_out_file.c_str());
  std::remove(_scannable_file.c_str());
  for (;;) {
    authenticate();
    loop();
  }
  return 0;
}

void Spoteletext::authenticate() {
  curl_easy_reset(_curl);
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
                    "&scope=user-read-playback-state"
                    "&description=spoteletext";

  for (;;) {
    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
    curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(_curl, CURLOPT_URL, kAuthDeviceCodeUrl);

    std::string buffer;
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

    if (curl_perform_and_check(_curl)) {
      std::cerr << "failed to get device_code" << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds{5});
      std::cerr << "retrying..." << std::endl;
      continue;
    }
    auto res = parseDeviceFlowData(_jq, buffer);
    std::cerr << "url: " << res.verification_url_prefilled << std::endl;

    auto err = authenticateCode(
        res.device_code, res.user_code, res.verification_url, res.expires_in, res.interval);
    if (err == kAuthSuccess) {
      return;
    }
    std::cerr << "failed to authenticate. retrying..." << std::endl;
  }
}

Spoteletext::AuthResult Spoteletext::authenticateCode(const std::string &device_code,
                                                      const std::string &user_code,
                                                      const std::string &verification_url,
                                                      const std::chrono::seconds &expires_in,
                                                      const std::chrono::seconds &interval) {
  displayCode(user_code, verification_url);

  std::string auth_code;
  if (auto err = getAuthCode(device_code, expires_in, interval, auth_code)) {
    return err;
  }
  if (!fetchTokens(auth_code)) {
    return kAuthError;
  }
  return kAuthSuccess;
}

Spoteletext::AuthResult Spoteletext::getAuthCode(const std::string &device_code,
                                                 const std::chrono::seconds &expires_in,
                                                 const std::chrono::seconds &interval,
                                                 std::string &auth_code) {
  using std::chrono::system_clock;
  auto expiry = system_clock::now() + expires_in;
  while (auto err = pollAuthCode(device_code, auth_code)) {
    if (err == kPollError) {
      return kAuthError;
    }
    std::this_thread::sleep_for(interval);
    if (system_clock::now() >= expiry) {
      return kAuthError;
    }
  }
  return kAuthSuccess;
}

Spoteletext::PollResult Spoteletext::pollAuthCode(const std::string &device_code,
                                                  std::string &auth_code) {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId + "&code=" + device_code +
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

bool Spoteletext::fetchTokens(const std::string &code) {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
                    "&client_secret=" + credentials::kClientSecret + "&code=" + code +
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

bool Spoteletext::refreshToken() {
  const auto header = curl_slist_append(nullptr, kContentTypeXWWWFormUrlencoded);
  const auto data = std::string{"client_id="} + credentials::kClientId +
                    "&client_secret=" + credentials::kClientSecret +
                    "&refresh_token=" + _refresh_token + "&grant_type=refresh_token";

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

  return true;
}

void Spoteletext::loop() {
  for (;;) {
    if (fetchNowPlaying(true)) {
      displayNPV();
      displayScannable();
    }
    if (_has_played && _now_playing.track_id.empty()) {
      _access_token = {};
      _refresh_token = {};
      _has_played = false;
      std::remove(_scannable_file.c_str());
      break;
    }
    for (auto i = 0; i < 5; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds{1});
      _now_playing.progress += std::chrono::seconds{1};
      displayNPV();
    }
  }
}

bool Spoteletext::fetchNowPlaying(bool retry) {
  curl_easy_reset(_curl);
  const auto auth_header = kAuthorizationBearer + _access_token;
  const auto header = curl_slist_append(nullptr, auth_header.c_str());

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_URL, kPlayerUrl);

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  long status{0};
  if (curl_perform_and_check(_curl, status)) {
    if (!retry) {
      std::cerr << "failed to get player state" << std::endl;
    }
    return retry && refreshToken() && fetchNowPlaying(false);
  }
  if (status == 204) {
    if (!_now_playing.track_id.empty()) {
      std::cerr << "nothing is playing" << std::endl;
      _now_playing = {};
      _image->clear();
    }
    return true;
  }
  _has_played = true;
  auto now_playing = parseNowPlaying(_jq, buffer);

  if (now_playing.track_id == _now_playing.track_id) {
    _now_playing.progress = now_playing.progress;
    return true;
  }
  _now_playing = now_playing;

  fetchContext(_now_playing.context_href);
  fetchImage(_now_playing.image);
  fetchScannable(_now_playing.uri);

  _now_playing.title = toCP1106(_now_playing.title);
  _now_playing.artist = toCP1106(_now_playing.artist);
  _now_playing.context = toCP1106(_now_playing.context);

  std::cerr << "context: " << _now_playing.context << "\n";
  std::cerr << "title: " << _now_playing.title << "\n";
  std::cerr << "artist: " << _now_playing.artist << "\n";
  std::cerr << "image: " << _now_playing.image << "\n";
  std::cerr << "uri: " << _now_playing.uri << "\n";
  std::cerr << "scannable id0: " << _scannable.id0 << ", id1: " << _scannable.id1 << "\n";
  std::cerr << "duration: " << _now_playing.duration.count() << std::endl;

  return true;
}

void Spoteletext::fetchContext(const std::string &url) {
  if (url.empty()) {
    return;
  }
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

void Spoteletext::fetchImage(const std::string &url) {
  if (url.empty()) {
    _image->clear();
    return;
  }
  curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());

  std::vector<unsigned char> encoded;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &encoded);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferArray);

  if (curl_perform_and_check(_curl)) {
    std::cerr << "failed to fetch image" << std::endl;
    _image->clear();
    return;
  }

  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, &encoded[0], encoded.size());

  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    _image->clear();
    return;
  }
  jpeg_start_decompress(&cinfo);

  const auto row_stride = cinfo.output_width * cinfo.output_components;
  std::vector<unsigned char> buffer(cinfo.output_height * row_stride);

  while (cinfo.output_scanline < cinfo.output_height) {
    auto *row = &buffer[cinfo.output_scanline * row_stride];
    jpeg_read_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  encoded.clear();

  _image->setSrc(cinfo.output_width, cinfo.output_height, cinfo.output_components, &buffer[0]);

#if !(RASPBIAN)
  for (auto y = 0u; y < 3 * _image->height(); ++y) {
    // Original
    for (auto x = 0u; x < 2 * _image->width(); ++x) {
      std::cerr << "\033[1;" << +_image->get(x, y) << "m  ";
    }
    std::cerr << "\033[0m    ";

    // Reconstructed
    auto line = _image->line(y / 3);
    auto *bg = &teletext::kColors[0];
    auto *fg = &teletext::kColors[0];

    for (auto it = line.begin(); it != line.end(); ++it) {
      if (*it == '\u001b') {
        ++it;
        if (*it == ']' || *it == '\\') {
          bg = *it == '\\' ? &teletext::kColors[0] : fg;
          std::cerr << "\033[1;" << +bg->terminal_code << "mb" << +bg->terminal_code << ".";
        } else {
          fg = std::find_if(teletext::kColors.begin(), teletext::kColors.end(), [it](auto &c) {
            return c.code == *it;
          });
          std::cerr << "\033[1;" << +bg->terminal_code << "mf" << +fg->terminal_code << ".";
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

void Spoteletext::fetchScannable(const std::string &uri) {
  curl_easy_reset(_curl);
  const auto auth_header = kAuthorizationBearer + _access_token;
  const auto header = curl_slist_append(nullptr, auth_header.c_str());
  const auto url = credentials::kScannablesUrl + uri + "?format=json";

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, header);
  curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, "");
  curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());

  std::string buffer;
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, bufferString);

  if (curl_perform_and_check(_curl)) {
    std::cerr << "failed to fetch scannable id" << std::endl;
    _scannable = {};
    return;
  }
  _scannable = parseScannable(_jq, buffer);
}

void Spoteletext::displayCode(const std::string &code, const std::string &url) {
  std::cerr << "display code: " << code.c_str() << std::endl;
  using namespace templates;
  const auto url_offset = (38 - url.substr(0, 38).size()) / 2;
  const auto code_offset = (38 - code.substr(0, 38).size()) / 2;
  std::ofstream file{_out_file, std::ofstream::binary};
  file.write(kPair, kPairUrlOffset);
  for (auto i = 0u; i < url_offset; ++i) {
    file << " ";
  }
  file << url.c_str();
  file.write(kPair + kPairUrlOffset, kPairCodeOffset - kPairUrlOffset);
  for (auto i = 0u; i < code_offset; ++i) {
    file << " ";
  }
  file << code.c_str();
  file.write(kPair + kPairCodeOffset, strlen(kPair) - kPairCodeOffset);
}

void Spoteletext::displayNPV() {
  const auto title_offset = (38 - _now_playing.title.substr(0, 38).size()) / 2;
  const auto artist_offset = (40 - _now_playing.artist.substr(0, 40).size()) / 2;

  using namespace templates;
  std::ofstream file{_out_file, std::ofstream::binary};
  file.write(kNpv, kNpvContextOffset);
  file << " " << _now_playing.context.substr(0, 33).c_str();
  file.write(kNpv + kNpvContextOffset, kNpvImageOffset - kNpvContextOffset);
  for (auto line = kNpvImageLineBegin, i = 0; line < kNpvImageLineEnd; ++line, ++i) {
    file << "OL," << line << ",         " << _image->line(i) << "\n";
  }
  file.write(kNpv + kNpvImageOffset, kNpvTitleOffset - kNpvImageOffset);
  for (auto i = 0u; i < title_offset; ++i) {
    file << " ";
  }
  file << _now_playing.title.substr(0, 38).c_str();
  file.write(kNpv + kNpvTitleOffset, kNpvArtistOffset - kNpvTitleOffset);
  for (auto i = 0u; i < artist_offset; ++i) {
    file << " ";
  }
  file << _now_playing.artist.substr(0, 40).c_str();
  file.write(kNpv + kNpvArtistOffset, kNpvProgressOffset - kNpvArtistOffset);

  auto progress_label = std::string{"00:00"};
  auto duration_label = std::string{"00:00"};
  const auto progress_tm = chronoToTm(_now_playing.progress);
  const auto duration_tm = chronoToTm(_now_playing.duration);
  std::strftime(&progress_label[0], 6, "%M:%S", &progress_tm);
  std::strftime(&duration_label[0], 6, "%M:%S", &duration_tm);

  auto progress_width = kNpvProgressWidth * static_cast<double>(_now_playing.progress.count()) /
                        _now_playing.duration.count();
  file << "  " << progress_label << "\u001bW";
  for (auto i = 0; i < kNpvProgressWidth; ++i) {
    unsigned char out = 1 << 5;
    if (progress_width > i) {
      out |= 1 << 2 | 1 << 3;
    } else if (progress_width > i - 0.5) {
      out |= 1 << 2;
    }
    file << out;
  }
  file << "\u001bG" << duration_label;
}

void Spoteletext::displayScannable() {
  if (!_scannable.id0) {
    return;
  }
  static unsigned char kFull = 0x7f;

  std::vector<uint8_t> lengths0, lengths1;
  lengths0 = makeLineLengthsFromId(_scannable.id0);
  if (_scannable.id1) {
    lengths1 = makeLineLengthsFromId(_scannable.id1);
  }

  using namespace templates;
  std::ofstream file{_scannable_file, std::ofstream::binary};
  file.write(kScannable, kScannableRows[0]);
  for (auto row = 0; row < 6; ++row) {
    const auto &lengths = row < 3 || !_scannable.id1 ? lengths0 : lengths1;
    for (auto length : lengths) {
      file << renderScannableBarCell(length, row);
    }
    file << kFull << kFull << kFull;
    if (row < 5) {
      file.write(kScannable + kScannableRows[row], kScannableRows[row + 1] - kScannableRows[row]);
    }
  }
  file.write(kScannable + kScannableRows[5], strlen(kScannable) - kScannableRows[5]);
}

}  // namespace teletext
