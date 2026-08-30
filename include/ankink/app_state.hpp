#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace ankink {

class AppState {
public:
  explicit AppState(std::string data_directory);

  [[nodiscard]] std::string settings_json() const;
  [[nodiscard]] std::string setting(const std::string &key) const;
  [[nodiscard]] std::uint64_t pending_reviews() const;
  bool set_setting(const std::string &key, const std::string &value,
                   std::string &error);
  bool record_answer();
  bool record_undo();
  bool record_sync();

private:
  [[nodiscard]] bool valid_setting(const std::string &key,
                                   const std::string &value) const;
  bool save_locked() const;
  void load();

  std::string data_directory_;
  std::string path_;
  mutable std::mutex mutex_;
  std::map<std::string, std::string> settings_;
  std::uint64_t pending_reviews_{};
};

} // namespace ankink
