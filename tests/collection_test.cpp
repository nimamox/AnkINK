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

std::size_t occurrences(const std::string &value, const std::string &needle) {
  std::size_t count = 0;
  for (std::size_t position = 0;
       (position = value.find(needle, position)) != std::string::npos;
       position += needle.size())
    ++count;
  return count;
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
                      "did INTEGER, queue INTEGER, due INTEGER, flags INTEGER)");
    execute(database,
            "CREATE TABLE revlog(id INTEGER PRIMARY KEY, type INTEGER NOT NULL)");
    execute(database, "INSERT INTO decks VALUES(42, 'Languages')");
    execute(database,
            "INSERT INTO notes VALUES(100, 'bonjour' || char(31) || 'hello')");
    execute(database, "INSERT INTO cards VALUES(200, 100, 42, 2, 1, 1)");
    sqlite3_close(database);

    ankink::Collection collection;
    std::string error;
    require(collection.open(path.string(), error),
            "could not open test collection");
    require(collection.decks_json().find("Languages") != std::string::npos,
            "deck is missing from JSON");
    require(collection.next_card_json(42).find("bonjour") != std::string::npos,
            "front is missing from card JSON");
    require(collection.next_card_json(42).find("\"flag\":1") !=
                std::string::npos,
            "flag value is missing from card JSON");

    const auto empty_activity = collection.review_activity_json();
    require(empty_activity.find("\"todayCount\":0") != std::string::npos,
            "empty activity has a nonzero today count");
    require(empty_activity.find("\"streak\":0") != std::string::npos,
            "empty activity has a nonzero streak");
    require(empty_activity.find("\"total\":0") != std::string::npos,
            "empty activity has a nonzero total");
    require(occurrences(empty_activity, "\"date\":") == 365,
            "activity range must contain exactly 365 calendar days");

    require(sqlite3_open(path.c_str(), &database) == SQLITE_OK,
            "could not reopen test collection for review history");
    execute(database,
            "INSERT INTO revlog VALUES"
            "((strftime('%s',date('now','localtime'))+43200)*1000+1,1),"
            "((strftime('%s',date('now','localtime'))+43200)*1000+2,0),"
            "((strftime('%s',date('now','localtime','-1 day'))+43200)*1000+3,2),"
            "((strftime('%s',date('now','localtime','-3 days'))+43200)*1000+4,1),"
            "((strftime('%s',date('now','localtime','-3 days'))+43200)*1000+5,1),"
            "((strftime('%s',date('now','localtime','-3 days'))+43200)*1000+6,1),"
            "((strftime('%s',date('now','localtime','-3 days'))+43200)*1000+7,3),"
            "((strftime('%s',date('now','localtime'))+43200)*1000+8,4),"
            "((strftime('%s',date('now','localtime'))+43200)*1000+9,5),"
            "((strftime('%s',date('now','localtime','-365 days'))+43200)*1000+10,1)");
    sqlite3_close(database);
    database = nullptr;
    const auto activity = collection.review_activity_json();
    require(activity.find("\"todayCount\":2") != std::string::npos,
            "today count must include actual answers and exclude maintenance entries");
    require(activity.find("\"streak\":2") != std::string::npos,
            "streak must end at today and stop at the first empty day");
    require(activity.find("\"total\":7") != std::string::npos,
            "365-day total is incorrect");
    require(occurrences(activity, "\"date\":") == 365,
            "populated activity range must remain exactly 365 days");

    const auto flag = collection.card_action_json(200, 0, "flag-red");
    require(flag.find("\"flag\":0") != std::string::npos,
            "existing red flag was not cleared");
    require(collection.next_card_json(42).find("\"flag\":0") !=
                std::string::npos,
            "cleared red flag was not persisted");
    require(collection.card_action_json(200, 0, "flag-red")
                .find("\"flag\":1") != std::string::npos,
            "unflagged card was not flagged red");
    require(collection.next_card_json(42).find("bonjour") != std::string::npos,
            "toggling the flag unexpectedly retired the current card");
    require(collection.card_action_json(200, 0, "unknown")
                .find("Unknown card action") != std::string::npos,
            "unknown card action was accepted");

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
