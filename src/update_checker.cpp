#include "ankink/update_checker.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>

namespace ankink {
namespace {

constexpr std::size_t maximum_response_bytes = 16 * 1024;
std::once_flag curl_once;
bool curl_ready = false;

std::string trim(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' ||
          value.back() == ' ' || value.back() == '\t'))
    value.pop_back();
  std::size_t first = 0;
  while (first < value.size() &&
         (value[first] == ' ' || value[first] == '\t' ||
          value[first] == '\n' || value[first] == '\r'))
    ++first;
  if (first) value.erase(0, first);
  if (value.size() > 128) value.resize(128);
  for (char &character : value)
    if (static_cast<unsigned char>(character) < 0x20U) character = ' ';
  return value;
}

std::string read_small_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  std::string value(256, '\0');
  input.read(&value[0], static_cast<std::streamsize>(value.size()));
  value.resize(static_cast<std::size_t>(input.gcount()));
  return trim(std::move(value));
}

bool atomic_write(const std::string &path, const std::string &body,
                  std::string &error) {
  const std::string temporary = path + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = std::strerror(errno);
      return false;
    }
    output << body;
    if (!output) {
      error = "Could not finish the update setting";
      return false;
    }
  }
  ::chmod(temporary.c_str(), 0600);
  if (::rename(temporary.c_str(), path.c_str()) != 0) {
    error = std::strerror(errno);
    ::unlink(temporary.c_str());
    return false;
  }
  error.clear();
  return true;
}

bool valid_uuid_v4(const std::string &value) {
  if (value.size() != 36 || value[8] != '-' || value[13] != '-' ||
      value[18] != '-' || value[23] != '-' || value[14] != '4' ||
      (value[19] != '8' && value[19] != '9' &&
       value[19] != 'a' && value[19] != 'b'))
    return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) continue;
    const char character = value[index];
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f')))
      return false;
  }
  return true;
}

std::string create_uuid_v4() {
  std::array<unsigned char, 16> bytes{};
  std::ifstream random("/dev/urandom", std::ios::binary);
  if (!random.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
    return {};
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) output << '-';
    output << std::setw(2) << static_cast<unsigned>(bytes[index]);
  }
  return output.str();
}

std::string installation_id(const std::string &directory) {
  const std::string path = directory + "/install-id";
  std::string value = read_small_file(path);
  if (valid_uuid_v4(value)) return value;
  value = create_uuid_v4();
  if (value.empty()) return {};
  if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) return {};
  ::chmod(directory.c_str(), 0700);
  std::string error;
  return atomic_write(path, value + "\n", error) ? value : std::string{};
}

std::string command_output(const char *command) {
  FILE *pipe = ::popen(command, "r");
  if (!pipe) return {};
  std::string output;
  char buffer[160];
  while (output.size() < 256 && std::fgets(buffer, sizeof(buffer), pipe))
    output += buffer;
  const int status = ::pclose(pipe);
  return status == 0 ? trim(std::move(output)) : std::string{};
}

DeviceTelemetry detect_device(bool simulator) {
  DeviceTelemetry result;
  struct utsname system{};
  if (::uname(&system) == 0) {
    result.kernel = trim(system.release);
    result.architecture = trim(system.machine);
  }
  if (simulator) {
    result.model = "Simulator";
    result.firmware = "Development host";
    return result;
  }
  result.model = command_output(
      "/usr/bin/lipc-get-prop com.lab126.deviceInfo productName 2>/dev/null");
  if (result.model.empty())
    result.model = command_output(
        "/usr/bin/lipc-get-prop com.lab126.deviceInfo deviceType 2>/dev/null");
  result.firmware = command_output(
      "/usr/bin/lipc-get-prop com.lab126.deviceInfo firmwareVersion 2>/dev/null");
  if (result.firmware.empty())
    result.firmware = read_small_file("/etc/prettyversion.txt");
  if (result.firmware.empty())
    result.firmware = read_small_file("/etc/version.txt");
  if (result.model.empty()) result.model = "Unknown";
  if (result.firmware.empty()) result.firmware = "Unknown";
  if (result.kernel.empty()) result.kernel = "Unknown";
  if (result.architecture.empty()) result.architecture = "Unknown";
  return result;
}

bool parse_version(const std::string &value,
                   std::array<unsigned long long, 3> &parts) noexcept {
  std::size_t start = 0;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    const std::size_t end = value.find('.', start);
    const std::size_t stop = end == std::string::npos ? value.size() : end;
    if (stop == start || (part < 2 && end == std::string::npos) ||
        (part == 2 && end != std::string::npos))
      return false;
    unsigned long long number = 0;
    for (std::size_t index = start; index < stop; ++index) {
      if (value[index] < '0' || value[index] > '9') return false;
      const unsigned digit = static_cast<unsigned>(value[index] - '0');
      if (number >
          (std::numeric_limits<unsigned long long>::max() - digit) / 10)
        return false;
      number = number * 10 + digit;
    }
    parts[part] = number;
    start = stop + 1;
  }
  return start == value.size() + 1;
}

std::string json_quote(const std::string &value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char character : value) {
    switch (character) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (character < 0x20U)
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned>(character) << std::dec;
        else
          output << static_cast<char>(character);
    }
  }
  output << '"';
  return output.str();
}

std::size_t json_value_start(const std::string &source,
                             const std::string &key) {
  const std::size_t key_at = source.find(json_quote(key));
  if (key_at == std::string::npos) return std::string::npos;
  std::size_t at = source.find(':', key_at + key.size() + 2);
  if (at == std::string::npos) return at;
  do { ++at; } while (at < source.size() &&
                      (source[at] == ' ' || source[at] == '\t' ||
                       source[at] == '\r' || source[at] == '\n'));
  return at;
}

bool json_true(const std::string &source, const std::string &key) {
  const std::size_t at = json_value_start(source, key);
  return at != std::string::npos && source.compare(at, 4, "true") == 0;
}

bool json_string_field(const std::string &source, const std::string &key,
                       std::string &value) {
  std::size_t at = json_value_start(source, key);
  if (at == std::string::npos || at >= source.size() || source[at++] != '"')
    return false;
  std::string parsed;
  while (at < source.size() && source[at] != '"') {
    const char character = source[at++];
    if (character == '\\' || static_cast<unsigned char>(character) < 0x20U)
      return false;
    parsed.push_back(character);
    if (parsed.size() > 64) return false;
  }
  if (at >= source.size() || source[at] != '"') return false;
  value = std::move(parsed);
  return true;
}

struct CurlDownload {
  std::string body;
  bool overflow{};
  const std::atomic<bool> *stopping{};
};

std::size_t receive_body(char *data, std::size_t size, std::size_t count,
                         void *opaque) {
  auto &download = *static_cast<CurlDownload *>(opaque);
  const std::size_t bytes = size * count;
  if (bytes > maximum_response_bytes - download.body.size()) {
    download.overflow = true;
    return 0;
  }
  download.body.append(data, bytes);
  return bytes;
}

int transfer_progress(void *opaque, curl_off_t, curl_off_t, curl_off_t,
                      curl_off_t) {
  const auto *download = static_cast<CurlDownload *>(opaque);
  return download->stopping->load() ? 1 : 0;
}

} // namespace

bool update_version_is_newer(const std::string &candidate,
                             const std::string &current) noexcept {
  std::array<unsigned long long, 3> left{}, right{};
  if (!parse_version(candidate, left) || !parse_version(current, right))
    return false;
  return left > right;
}

UpdateChecker::UpdateChecker(UpdateCheckerOptions options)
    : options_(std::move(options)),
      dismissed_version_(read_small_file(options_.data_dir +
                                         "/dismissed-update-version")) {}

UpdateChecker::~UpdateChecker() { stop(); }

void UpdateChecker::start() noexcept {
  bool expected = false;
  if (!started_.compare_exchange_strong(expected, true)) return;
  try {
    thread_ = std::thread(&UpdateChecker::run, this);
  } catch (...) {
    started_.store(false);
  }
}

void UpdateChecker::stop() noexcept {
  stopping_.store(true);
  if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
    thread_.join();
}

bool UpdateChecker::post(const std::string &body,
                         std::string &response) noexcept {
  if (options_.post) {
    try {
      return options_.post(options_.endpoint, body, response);
    } catch (...) {
      return false;
    }
  }
  std::call_once(curl_once, [] {
    curl_ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  });
  if (!curl_ready) return false;
  CURL *curl = curl_easy_init();
  if (!curl) return false;
  CurlDownload download;
  download.stopping = &stopping_;
  curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, options_.endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "AnkINK/" ANKINK_VERSION " UpdateCheck");
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &download);
  if (!options_.ca_bundle.empty())
    curl_easy_setopt(curl, CURLOPT_CAINFO, options_.ca_bundle.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
  const CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK || download.overflow || status < 200 || status >= 300)
    return false;
  response = std::move(download.body);
  return true;
}

void UpdateChecker::run() noexcept {
  try {
    const std::string id = installation_id(options_.data_dir);
    if (id.empty() || stopping_.load()) return;
    const DeviceTelemetry device = options_.device_telemetry
        ? options_.device_telemetry() : detect_device(options_.simulator);
    const std::string body =
        R"({"installationId":)" + json_quote(id) +
        R"(,"app":)" + json_quote(options_.app) +
        R"(,"appVersion":)" + json_quote(options_.current_version) +
        R"(,"deviceModel":)" + json_quote(device.model) +
        R"(,"firmwareVersion":)" + json_quote(device.firmware) +
        R"(,"kernelVersion":)" + json_quote(device.kernel) +
        R"(,"architecture":)" + json_quote(device.architecture) +
        R"(,"buildCommit":)" + json_quote(options_.build_commit) +
        R"(,"buildType":)" + json_quote(options_.build_type) + "}";
    std::string response;
    if (stopping_.load() || !post(body, response) || stopping_.load()) return;
    std::string current, latest;
    if (!json_true(response, "checked") ||
        !json_string_field(response, "currentVersion", current) ||
        current != options_.current_version ||
        !json_string_field(response, "latestVersion", latest))
      return;
    std::array<unsigned long long, 3> ignored{};
    if (!parse_version(latest, ignored)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    checked_ = true;
    latest_version_ = latest;
  } catch (...) {
    // Update discovery must never affect application startup or use.
  }
}

std::string UpdateChecker::status_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool available = checked_ &&
      update_version_is_newer(latest_version_, options_.current_version);
  const bool dismissed = available && latest_version_ == dismissed_version_;
  return R"({"type":"update-status","checked":)" +
      std::string(checked_ ? "true" : "false") +
      R"(,"currentVersion":)" + json_quote(options_.current_version) +
      R"(,"latestVersion":)" + json_quote(latest_version_) +
      R"(,"updateAvailable":)" + (available ? "true" : "false") +
      R"(,"dismissed":)" + (dismissed ? "true" : "false") + "}";
}

bool UpdateChecker::dismiss_latest(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!checked_ ||
      !update_version_is_newer(latest_version_, options_.current_version)) {
    error = "No newer version is available";
    return false;
  }
  if (::mkdir(options_.data_dir.c_str(), 0700) != 0 && errno != EEXIST) {
    error = std::strerror(errno);
    return false;
  }
  ::chmod(options_.data_dir.c_str(), 0700);
  if (!atomic_write(options_.data_dir + "/dismissed-update-version",
                    latest_version_ + "\n", error))
    return false;
  dismissed_version_ = latest_version_;
  return true;
}

} // namespace ankink
