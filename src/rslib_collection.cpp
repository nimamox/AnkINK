#include "ankink/collection.hpp"

#include "ankink/anki_backend.h"

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace ankink {
namespace {

std::string take_json(char *value) {
  if (!value)
    return R"({"type":"error","message":"Anki backend returned no response"})";
  std::string result(value);
  ankink_anki_string_free(value);
  return result;
}

std::string data_directory() {
  const char *configured = std::getenv("ANKINK_DATA_DIR");
  return configured && *configured ? configured : "/var/local/ankink";
}

std::string host_key_path() { return data_directory() + "/host-key"; }

std::string read_host_key() {
  std::ifstream input(host_key_path(), std::ios::binary);
  std::string key;
  std::getline(input, key);
  return key;
}

bool write_host_key(const std::string &key) {
  const std::string directory = data_directory();
  if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
    return false;
  const std::string path = host_key_path();
  const std::string temporary = path + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      return false;
    output << key << '\n';
    if (!output)
      return false;
  }
  ::chmod(temporary.c_str(), 0600);
  return ::rename(temporary.c_str(), path.c_str()) == 0;
}

} // namespace

std::string json_string(const std::string &value) {
  std::ostringstream output;
  output << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character < 0x20)
        output << "\\u00" << hex[character >> 4U]
               << hex[character & 0x0fU];
      else
        output << static_cast<char>(character);
    }
  }
  output << '"';
  return output.str();
}

class Collection::Impl {
public:
  Impl() : backend_(ankink_anki_backend_new()) {
    const std::string key = read_host_key();
    if (backend_ && !key.empty())
      ankink_anki_string_free(
          ankink_anki_set_host_key(backend_, key.c_str()));
  }
  ~Impl() { ankink_anki_backend_free(backend_); }

  bool open(const std::string &path, std::string &error) {
    path_.clear();
    open_ = false;
    const std::string response = take_json(ankink_anki_open(backend_, path.c_str()));
    if (response.find(R"("type":"error")") != std::string::npos) {
      error = response;
      return false;
    }
    path_ = path;
    open_ = true;
    error.clear();
    return true;
  }

  AnkinkAnkiBackend *backend_{};
  std::string path_;
  bool open_{};
};

Collection::Collection() : impl_(std::make_unique<Impl>()) {}
Collection::~Collection() = default;

bool Collection::open(const std::string &path, std::string &error) {
  return impl_->open(path, error);
}

bool Collection::is_open() const noexcept { return impl_->open_; }
bool Collection::is_authenticated() const noexcept {
  char *key = ankink_anki_host_key(impl_->backend_);
  if (!key)
    return false;
  ankink_anki_string_free(key);
  return true;
}
std::string Collection::path() const { return impl_->path_; }

std::string Collection::decks_json() const {
  return take_json(ankink_anki_decks(impl_->backend_));
}

std::string Collection::next_card_json(std::int64_t deck_id) {
  return take_json(ankink_anki_next_card(impl_->backend_, deck_id));
}

std::string Collection::answer_json(std::int64_t card_id, int rating) {
  return take_json(ankink_anki_answer(impl_->backend_, card_id, rating));
}

std::string Collection::undo_json() {
  return take_json(ankink_anki_undo(impl_->backend_));
}

std::string Collection::login_json(const std::string &username,
                                   const std::string &password) {
  const std::string response = take_json(
      ankink_anki_login(impl_->backend_, username.c_str(), password.c_str()));
  if (response.find(R"("type":"error")") == std::string::npos) {
    char *raw_key = ankink_anki_host_key(impl_->backend_);
    if (raw_key) {
      const std::string key(raw_key);
      ankink_anki_string_free(raw_key);
      if (!write_host_key(key))
        return R"({"type":"error","message":"Signed in, but could not securely save the AnkiWeb host key"})";
    }
  }
  return response;
}

std::string Collection::logout_json() {
  const std::string path = host_key_path();
  ::unlink(path.c_str());
  return take_json(ankink_anki_logout(impl_->backend_));
}

std::string Collection::sync_json() {
  return take_json(ankink_anki_sync(impl_->backend_));
}

std::string Collection::full_download_json() {
  return take_json(ankink_anki_full_download(impl_->backend_));
}

} // namespace ankink
