#include "ankink/app_state.hpp"

#include "ankink/collection.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace ankink {
namespace {

int hex_value(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

std::string encode(const std::string &value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string output;
  for (const unsigned char character : value) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '_' || character == '.' || character == '~') {
      output.push_back(static_cast<char>(character));
    } else {
      output.push_back('%');
      output.push_back(hex[character >> 4U]);
      output.push_back(hex[character & 0x0fU]);
    }
  }
  return output;
}

std::string decode(const std::string &value) {
  std::string output;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size()) {
      const int high = hex_value(value[index + 1]);
      const int low = hex_value(value[index + 2]);
      if (high >= 0 && low >= 0) {
        output.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        continue;
      }
    }
    output.push_back(value[index]);
  }
  return output;
}

bool one_of(const std::string &value,
            std::initializer_list<const char *> choices) {
  for (const char *choice : choices)
    if (value == choice) return true;
  return false;
}

bool unsigned_number(const std::string &value, std::uint64_t maximum) {
  if (value.empty()) return false;
  std::uint64_t result = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') return false;
    const unsigned digit = static_cast<unsigned>(character - '0');
    if (result > (maximum - digit) / 10) return false;
    result = result * 10 + digit;
  }
  return true;
}

} // namespace

AppState::AppState(std::string data_directory)
    : data_directory_(std::move(data_directory)),
      path_(data_directory_ + "/state.conf"),
      settings_({{"fontScale", "1"},
                 {"cardFont", "Bookerly"},
                 {"nightMode", "0"},
                 {"nightCardMode", "standard"},
                 {"pageButtonMode", "normal"},
                 {"rotationMode", "auto"},
                 {"fullRefreshMode", "manual"},
                 {"reviewsSinceFullRefresh", "0"},
                 {"collapsedDecks", "{}"}}) {
  load();
}

bool AppState::valid_setting(const std::string &key,
                             const std::string &value) const {
  if (key == "fontScale")
    return one_of(value, {"0.7", "0.8", "0.9", "1", "1.1", "1.25",
                          "1.4", "1.6"});
  if (key == "cardFont")
    return one_of(value, {"Amazon Ember", "Baskerville", "Bookerly",
                          "Caecilia", "Caecilia Condensed", "Futura",
                          "Helvetica", "OpenDyslexic", "Palatino"});
  if (key == "nightMode") return value == "0" || value == "1";
  if (key == "nightCardMode")
    return one_of(value, {"standard", "palette", "palette-images"});
  if (key == "pageButtonMode")
    return value == "normal" || value == "reversed";
  if (key == "rotationMode")
    return value == "auto" || value == "locked";
  if (key == "fullRefreshMode")
    return one_of(value, {"manual", "every-card", "every-five"});
  if (key == "reviewsSinceFullRefresh") return unsigned_number(value, 5);
  if (key == "collapsedDecks")
    return value.size() <= 65536 && value.size() >= 2 &&
           value.front() == '{' && value.back() == '}';
  return false;
}

void AppState::load() {
  std::ifstream input(path_, std::ios::binary);
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) continue;
    const std::string key = decode(line.substr(0, separator));
    const std::string value = decode(line.substr(separator + 1));
    if (key == "pendingReviews") {
      if (unsigned_number(value, std::numeric_limits<std::uint64_t>::max())) {
        try {
          pending_reviews_ = std::stoull(value);
        } catch (...) {
          pending_reviews_ = 0;
        }
      }
    } else if (valid_setting(key, value)) {
      settings_[key] = value;
    }
  }
}

bool AppState::save_locked() const {
  if (::mkdir(data_directory_.c_str(), 0700) != 0 && errno != EEXIST)
    return false;
  const std::string temporary = path_ + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "pendingReviews=" << pending_reviews_ << '\n';
    for (const auto &setting : settings_)
      output << encode(setting.first) << '=' << encode(setting.second) << '\n';
    if (!output) return false;
  }
  ::chmod(temporary.c_str(), 0600);
  return ::rename(temporary.c_str(), path_.c_str()) == 0;
}

std::string AppState::settings_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream output;
  output << R"({"type":"settings","pendingReviews":)" << pending_reviews_
         << R"(,"fontScale":)" << json_string(settings_.at("fontScale"))
         << R"(,"cardFont":)" << json_string(settings_.at("cardFont"))
         << R"(,"nightMode":)" << (settings_.at("nightMode") == "1" ? "true" : "false")
         << R"(,"nightCardMode":)" << json_string(settings_.at("nightCardMode"))
         << R"(,"pageButtonMode":)" << json_string(settings_.at("pageButtonMode"))
         << R"(,"rotationMode":)" << json_string(settings_.at("rotationMode"))
         << R"(,"fullRefreshMode":)" << json_string(settings_.at("fullRefreshMode"))
         << R"(,"reviewsSinceFullRefresh":)" << settings_.at("reviewsSinceFullRefresh")
         << R"(,"collapsedDecks":)" << json_string(settings_.at("collapsedDecks")) << '}';
  return output.str();
}

std::string AppState::setting(const std::string &key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = settings_.find(key);
  return found == settings_.end() ? std::string{} : found->second;
}

std::uint64_t AppState::pending_reviews() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_reviews_;
}

bool AppState::set_setting(const std::string &key, const std::string &value,
                           std::string &error) {
  if (!valid_setting(key, value)) {
    error = "Invalid setting or value";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string previous = settings_.at(key);
  settings_[key] = value;
  if (!save_locked()) {
    settings_[key] = previous;
    error = "Could not save AnkINK settings";
    return false;
  }
  error.clear();
  return true;
}

bool AppState::record_answer() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_reviews_ < std::numeric_limits<std::uint64_t>::max())
    ++pending_reviews_;
  return save_locked();
}

bool AppState::record_undo() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_reviews_ > 0) --pending_reviews_;
  return save_locked();
}

bool AppState::record_sync() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_reviews_ = 0;
  return save_locked();
}

} // namespace ankink
