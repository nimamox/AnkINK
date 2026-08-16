#include "ankink/application.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  try {
    auto options = ankink::parse_options(argc, argv);
    ankink::Application application(std::move(options));
    return application.run();
  } catch (const std::invalid_argument &error) {
    if (std::string(error.what()) != "help")
      std::cerr << "AnkINK: " << error.what() << "\n\n";
    std::cerr << ankink::usage(argv[0]);
    return std::string(error.what()) == "help" ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception &error) {
    std::cerr << "AnkINK: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
