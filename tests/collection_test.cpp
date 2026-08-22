#include "ankink/collection.hpp"

#include <sqlite3.h>
#include <unistd.h>

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void execute(sqlite3 *database, const char *sql) {
  char *error = nullptr;
  if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : "unknown SQLite error";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  const auto path =
      std::filesystem::temp_directory_path() /
      ("ankink-collection-test-" + std::to_string(getpid()) + ".anki2");
  try {
    sqlite3 *database = nullptr;
    require(sqlite3_open(path.c_str(), &database) == SQLITE_OK,
            "could not create test collection");
    require(sqlite3_create_collation(
                database, "unicase", SQLITE_UTF8, nullptr,
                [](void *, int left_size, const void *left, int right_size,
                   const void *right) {
                  const int common = std::min(left_size, right_size);
                  const int compared = std::memcmp(left, right, common);
                  return compared ? compared
                                  : (left_size > right_size) -
                                        (left_size < right_size);
                }) == SQLITE_OK,
            "could not register test unicase collation");
    execute(database,
            "CREATE TABLE decks(id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL COLLATE unicase)");
    execute(database,
            "CREATE TABLE notes(id INTEGER PRIMARY KEY, flds TEXT NOT NULL)");
    execute(database, "CREATE TABLE cards(id INTEGER PRIMARY KEY, nid INTEGER, "
                      "did INTEGER, queue INTEGER, due INTEGER)");
    execute(database, "INSERT INTO decks VALUES(42, 'Languages')");
    execute(database,
            "INSERT INTO notes VALUES(100, 'bonjour' || char(31) || 'hello')");
    execute(database, "INSERT INTO cards VALUES(200, 100, 42, 2, 1)");
    sqlite3_close(database);

    ankink::Collection collection;
    std::string error;
    require(collection.open(path.string(), error),
            "could not open test collection");
    require(collection.decks_json().find("Languages") != std::string::npos,
            "deck is missing from JSON");
    require(collection.next_card_json(42).find("bonjour") != std::string::npos,
            "front is missing from card JSON");
    const auto answer = collection.answer_json(200, 0, 3);
    require(answer.find("answered") != std::string::npos,
            "answer was not acknowledged");
    require(collection.next_card_json(42).find("complete") != std::string::npos,
            "answered card was not retired");
    std::filesystem::remove(path);
    std::cout << "AnkINK collection tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::filesystem::remove(path);
    std::cerr << "Test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
