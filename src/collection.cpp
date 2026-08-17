#include "ankink/collection.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ankink {

namespace {

class Statement {
public:
  Statement(sqlite3 *db, const char *sql) {
    const int result = sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr);
    if (result != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~Statement() { sqlite3_finalize(statement_); }
  sqlite3_stmt *get() const noexcept { return statement_; }

private:
  sqlite3_stmt *statement_{};
};

std::string text_column(sqlite3_stmt *statement, int column) {
  const auto *value = sqlite3_column_text(statement, column);
  const int size = sqlite3_column_bytes(statement, column);
  return value ? std::string(reinterpret_cast<const char *>(value),
                             static_cast<std::size_t>(size))
               : std::string{};
}

std::vector<std::string> split_fields(const std::string &fields) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start <= fields.size()) {
    const auto separator = fields.find('\x1f', start);
    result.push_back(fields.substr(start, separator == std::string::npos
                                              ? std::string::npos
                                              : separator - start));
    if (separator == std::string::npos)
      break;
    start = separator + 1;
  }
  return result;
}

int compare_unicase(void *, int left_size, const void *left_value,
                    int right_size, const void *right_value) {
  const auto *left = static_cast<const unsigned char *>(left_value);
  const auto *right = static_cast<const unsigned char *>(right_value);
  const int common = std::min(left_size, right_size);
  for (int index = 0; index < common; ++index) {
    // Anki requires this collation to parse its schema. AnkINK's current
    // read-only queries do not use the associated note-field indexes, but an
    // ASCII case fold preserves their expected behavior for common content.
    const unsigned char left_folded =
        left[index] < 0x80 ? static_cast<unsigned char>(std::tolower(left[index]))
                           : left[index];
    const unsigned char right_folded =
        right[index] < 0x80
            ? static_cast<unsigned char>(std::tolower(right[index]))
            : right[index];
    if (left_folded != right_folded)
      return left_folded < right_folded ? -1 : 1;
  }
  return (left_size > right_size) - (left_size < right_size);
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
        output << "\\u00" << hex[character >> 4U] << hex[character & 0x0fU];
      else
        output << static_cast<char>(character);
    }
  }
  output << '"';
  return output.str();
}

class Collection::Impl {
public:
  ~Impl() {
    if (database_)
      sqlite3_close(database_);
  }

  bool open(const std::string &path, std::string &error) {
    if (database_) {
      sqlite3_close(database_);
      database_ = nullptr;
    }
    reviewed_.clear();
    const int result =
        sqlite3_open_v2(path.c_str(), &database_,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    if (result != SQLITE_OK) {
      error = database_ ? sqlite3_errmsg(database_)
                        : "SQLite could not allocate a database handle";
      if (database_) {
        sqlite3_close(database_);
        database_ = nullptr;
      }
      return false;
    }

    const int collation_result = sqlite3_create_collation_v2(
        database_, "unicase", SQLITE_UTF8, nullptr, &compare_unicase, nullptr);
    if (collation_result != SQLITE_OK) {
      error = sqlite3_errmsg(database_);
      sqlite3_close(database_);
      database_ = nullptr;
      return false;
    }

    try {
      Statement check(database_, "SELECT id, name FROM decks LIMIT 1");
      sqlite3_step(check.get());
    } catch (const std::exception &exception) {
      error = std::string("unsupported collection schema: ") + exception.what();
      sqlite3_close(database_);
      database_ = nullptr;
      return false;
    }
    path_ = path;
    return true;
  }

  std::string decks_json() const {
    if (!database_)
      return R"({"type":"decks","decks":[],"error":"No collection is open"})";

    Statement statement(database_,
                        "SELECT d.id, d.name, count(c.id) "
                        "FROM decks d LEFT JOIN cards c ON c.did = d.id "
                        "GROUP BY d.id, d.name ORDER BY d.name COLLATE NOCASE");
    std::ostringstream output;
    output << R"({"type":"decks","path":)" << json_string(path_)
           << R"(,"decks":[)";
    bool first = true;
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      if (!first)
        output << ',';
      first = false;
      output << R"({"id":)" << sqlite3_column_int64(statement.get(), 0)
             << R"(,"name":)" << json_string(text_column(statement.get(), 1))
             << R"(,"cards":)" << sqlite3_column_int64(statement.get(), 2)
             << '}';
    }
    output << "]}";
    return output.str();
  }

  std::string next_card_json(std::int64_t deck_id) {
    if (!database_)
      return R"({"type":"error","message":"No collection is open"})";

    Statement statement(
        database_,
        "SELECT c.id, n.flds FROM cards c JOIN notes n ON n.id = c.nid "
        "WHERE c.did = ?1 AND c.queue >= 0 ORDER BY c.due, c.id LIMIT 256");
    sqlite3_bind_int64(statement.get(), 1, deck_id);
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      const auto card_id = sqlite3_column_int64(statement.get(), 0);
      if (reviewed_.count(card_id))
        continue;
      auto fields = split_fields(text_column(statement.get(), 1));
      const std::string front = fields.empty() ? std::string{} : fields.front();
      std::ostringstream back;
      for (std::size_t index = 1; index < fields.size(); ++index) {
        if (index != 1)
          back << "<hr>";
        back << fields[index];
      }
      return std::string(R"({"type":"card","deckId":)") +
             std::to_string(deck_id) + R"(,"id":)" + std::to_string(card_id) +
             R"(,"front":)" + json_string(front) + R"(,"back":)" +
             json_string(back.str()) + "}";
    }
    return std::string(R"({"type":"complete","deckId":)") +
           std::to_string(deck_id) + "}";
  }

  std::string answer_json(std::int64_t card_id, int rating) {
    if (!database_)
      return R"({"type":"error","message":"No collection is open"})";
    if (rating < 1 || rating > 4)
      return R"({"type":"error","message":"Rating must be between 1 and 4"})";
    reviewed_.insert(card_id);
    return std::string(R"({"type":"answered","id":)") +
           std::to_string(card_id) + R"(,"rating":)" + std::to_string(rating) +
           "}";
  }

  sqlite3 *database_{};
  std::string path_;
  std::unordered_set<std::int64_t> reviewed_;
};

Collection::Collection() : impl_(std::make_unique<Impl>()) {}
Collection::~Collection() = default;
bool Collection::open(const std::string &path, std::string &error) {
  return impl_->open(path, error);
}
bool Collection::is_open() const noexcept {
  return impl_->database_ != nullptr;
}
bool Collection::is_authenticated() const noexcept { return false; }
std::string Collection::path() const { return impl_->path_; }
std::string Collection::decks_json() const { return impl_->decks_json(); }
std::string Collection::next_card_json(std::int64_t deck_id) {
  return impl_->next_card_json(deck_id);
}
std::string Collection::answer_json(std::int64_t card_id, int rating) {
  return impl_->answer_json(card_id, rating);
}
std::string Collection::login_json(const std::string &, const std::string &) {
  return R"({"type":"error","message":"This build does not include AnkiWeb sync"})";
}
std::string Collection::logout_json() {
  return R"({"type":"error","message":"This build does not include AnkiWeb sync"})";
}
std::string Collection::sync_json() {
  return R"({"type":"error","message":"This build does not include AnkiWeb sync"})";
}
std::string Collection::full_download_json() {
  return R"({"type":"error","message":"This build does not include AnkiWeb sync"})";
}

} // namespace ankink
