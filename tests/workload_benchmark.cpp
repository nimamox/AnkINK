#include "workload_benchmark.hpp"
#include "ankink/collection.hpp"
#include <cstdlib>
#include <fstream>
#include <memory>
// The caller supplies a fresh writable copy of the generated fixture per run.
namespace {
uint64_t number(const std::string &s, const std::string &key) {
  auto p = s.find('"' + key + '"');
  bench::require(p != std::string::npos, "missing response field");
  p = s.find(':', p);
  return std::stoull(s.substr(p + 1));
}
void response_ok(const std::string &s) {
  bench::require(s.find("\"type\":\"error\"") == std::string::npos,
                 "Anki operation failed");
}
} // namespace
int main(int argc, char **argv) {
  try {
    bench::require(argc == 3 ||
                       (argc == 4 && std::string(argv[3]) == "--startup-only"),
                   "usage: workload_benchmark DISPOSABLE_COLLECTION DECK_ID "
                   "[--startup-only]");
    const std::string path = argv[1];
    const auto deck = std::stoll(argv[2]);
    std::string parent = path.substr(0, path.find_last_of('/'));
    setenv("ANKINK_DATA_DIR", parent.c_str(), 1);
    bench::measure("collection_construct_open", 7, [&] {
      ankink::Collection c;
      std::string error;
      bench::require(c.open(path, error), "collection open failed");
      return c.decks_json();
    });
    ankink::Collection c;
    std::string error;
    bench::require(c.open(path, error), "collection open failed");
    bench::measure("deck_tree", 21, [&] {
      auto s = c.decks_json();
      response_ok(s);
      return s;
    });
    bench::measure("review_activity", 9, [&] {
      auto s = c.review_activity_json();
      response_ok(s);
      return std::to_string(s.size());
    });
    if (argc == 4) {
      auto start = bench::Clock::now();
      auto card = c.next_card_json(deck);
      const auto elapsed = bench::ms(start);
      response_ok(card);
      bench::require(card.find("\"type\":\"card\"") != std::string::npos,
                     "fixture cards exhausted");
      bench::report("first_card_after_open", {elapsed}, bench::hash(card));
      return 0;
    }
    std::vector<double> cold, next, answer, undo;
    uint64_t content = 0;
    std::ofstream examples(path + ".cards.jsonl");
    // First twelve distinct cards exercise cold template/math rendering.
    // Answering changes only this fixture copy. Remaining daily allowance
    // supports warm runs.
    for (int i = 0; i < 12; ++i) {
      auto t = bench::Clock::now();
      auto card = c.next_card_json(deck);
      cold.push_back(bench::ms(t));
      response_ok(card);
      bench::require(card.find("\"type\":\"card\"") != std::string::npos,
                     "fixture cards exhausted");
      examples << card << '\n';
      content += card.size();
      auto id = number(card, "id"), token = number(card, "reviewToken");
      response_ok(c.answer_json(id, token, 3));
    }
    bench::report("first_card_after_open", {cold.front()}, content);
    bench::report("next_card_cold", cold, content);
    for (int i = 0; i < 41; ++i) {
      auto t = bench::Clock::now();
      auto card = c.next_card_json(deck);
      auto n = bench::ms(t);
      response_ok(card);
      bench::require(card.find("\"type\":\"card\"") != std::string::npos,
                     "fixture cards exhausted");
      auto id = number(card, "id"), token = number(card, "reviewToken");
      t = bench::Clock::now();
      auto result = c.answer_json(id, token, 3);
      auto a = bench::ms(t);
      response_ok(result);
      t = bench::Clock::now();
      result = c.undo_json();
      auto u = bench::ms(t);
      response_ok(result);
      if (i) {
        next.push_back(n);
        answer.push_back(a);
        undo.push_back(u);
      }
    }
    bench::report("next_card_warm", next);
    bench::report("answer_good", answer);
    bench::report("undo_review", undo);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
