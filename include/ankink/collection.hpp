#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

namespace ankink {

class Collection {
public:
  Collection();
  ~Collection();

  Collection(const Collection &) = delete;
  Collection &operator=(const Collection &) = delete;

  bool open(const std::string &path, std::string &error);
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] bool is_authenticated() const noexcept;
  [[nodiscard]] std::string path() const;

  // All returned values are JSON objects ready for the JavaScript bridge.
  [[nodiscard]] std::string decks_json() const;
  [[nodiscard]] std::string next_card_json(std::int64_t deck_id);
  [[nodiscard]] std::string answer_json(std::int64_t card_id,
                                        std::uint64_t review_token, int rating);
  [[nodiscard]] std::string undo_json();
  [[nodiscard]] std::string login_json(const std::string &username,
                                       const std::string &password);
  [[nodiscard]] std::string logout_json();
  [[nodiscard]] std::string sync_json();
  [[nodiscard]] std::string full_download_json();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::string json_string(const std::string &value);

} // namespace ankink
