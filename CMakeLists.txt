cmake_minimum_required(VERSION 3.20)

project(AnkINK VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(CMAKE_INSTALL_BINDIR bin CACHE PATH "Executable installation directory")
set(CMAKE_INSTALL_DATADIR share CACHE PATH "Data installation directory")

option(ANKINK_BUILD_APP "Build the AnkINK HTTP daemon" ON)
option(ANKINK_BUILD_TESTS "Build AnkINK tests" ON)

find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)
    set(ANKINK_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(ANKINK_SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(ankink_collection src/collection.cpp)
target_include_directories(ankink_collection PUBLIC include)
target_link_libraries(ankink_collection PUBLIC ${ANKINK_SQLITE_TARGET})
target_compile_options(ankink_collection PRIVATE -Wall -Wextra -Wpedantic)

if(ANKINK_BUILD_APP)
    add_executable(ankinkd src/daemon_main.cpp src/http_server.cpp)
    target_include_directories(ankinkd PRIVATE include)
    target_link_libraries(ankinkd PRIVATE ankink_collection)
    target_compile_definitions(ankinkd PRIVATE ANKINK_VERSION="${PROJECT_VERSION}")
    target_compile_options(ankinkd PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(ankinkd PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

    install(TARGETS ankinkd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES assets/index.html assets/app.css assets/app.js assets/config.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ankink)
    install(DIRECTORY assets/vendor DESTINATION ${CMAKE_INSTALL_DATADIR}/ankink)
    install(PROGRAMS scripts/run-kindle.sh DESTINATION . RENAME ankink.sh)
endif()

if(ANKINK_BUILD_TESTS)
    add_executable(ankink_collection_tests tests/collection_test.cpp)
    target_link_libraries(ankink_collection_tests PRIVATE ankink_collection)
    target_compile_options(ankink_collection_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ankink_collection_tests COMMAND ankink_collection_tests)
endif()
