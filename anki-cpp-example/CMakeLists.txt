cmake_minimum_required(VERSION 3.20)
project(anki_cpp_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

add_executable(anki-decks main.cpp)
target_link_libraries(anki-decks PRIVATE CURL::libcurl SQLite3::SQLite3 PkgConfig::ZSTD)
