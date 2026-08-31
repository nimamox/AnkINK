cmake_minimum_required(VERSION 3.20)

project(AnkINK VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_INSTALL_BINDIR bin CACHE PATH "Executable installation directory")
set(CMAKE_INSTALL_DATADIR share CACHE PATH "Data installation directory")

option(ANKINK_BUILD_APP "Build the AnkINK HTTP daemon" ON)
option(ANKINK_BUILD_TESTS "Build AnkINK tests" ON)
option(ANKINK_USE_RSLIB "Use the official Anki Rust backend" OFF)
option(ANKINK_BUILD_SIMULATOR "Build the host browser-based Kindle simulator" OFF)
set(ANKINK_RSLIB_LIBRARY "" CACHE FILEPATH
    "Path to libankink_anki_backend.a when ANKINK_USE_RSLIB is enabled")

if(ANKINK_BUILD_SIMULATOR)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "ANKINK_BUILD_SIMULATOR requires a native host build")
    endif()
    if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        execute_process(COMMAND xcrun --show-sdk-version
            OUTPUT_VARIABLE ANKINK_MACOS_SDK_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${ANKINK_MACOS_SDK_VERSION}"
            CACHE STRING "Simulator macOS deployment target" FORCE)
    endif()
    set(ANKINK_USE_RSLIB ON)
    set(ANKINK_RSLIB_LIBRARY
        "${CMAKE_BINARY_DIR}/rust-target/release/${CMAKE_STATIC_LIBRARY_PREFIX}ankink_anki_backend${CMAKE_STATIC_LIBRARY_SUFFIX}")
    add_custom_command(
        OUTPUT "${ANKINK_RSLIB_LIBRARY}"
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/build-rslib-host.sh"
                "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/rust-target"
        DEPENDS backend/rust/Cargo.toml backend/rust/src/lib.rs
                scripts/build-rslib-host.sh
        COMMENT "Building the native official Anki backend"
        VERBATIM)
    add_custom_target(ankink_rslib_host DEPENDS "${ANKINK_RSLIB_LIBRARY}")
endif()

if(ANKINK_USE_RSLIB)
    if(NOT ANKINK_BUILD_SIMULATOR AND NOT EXISTS "${ANKINK_RSLIB_LIBRARY}")
        message(FATAL_ERROR "ANKINK_RSLIB_LIBRARY must name the built Rust static library")
    endif()
    add_library(ankink_anki_backend STATIC IMPORTED GLOBAL)
    set_target_properties(ankink_anki_backend PROPERTIES
        IMPORTED_LOCATION "${ANKINK_RSLIB_LIBRARY}")
    if(ANKINK_BUILD_SIMULATOR)
        add_dependencies(ankink_anki_backend ankink_rslib_host)
    endif()
    set(ANKINK_COLLECTION_SOURCE src/rslib_collection.cpp)
else()
    find_package(SQLite3 REQUIRED)
    if(TARGET SQLite3::SQLite3)
        set(ANKINK_SQLITE_TARGET SQLite3::SQLite3)
    else()
        set(ANKINK_SQLITE_TARGET SQLite::SQLite3)
    endif()
    set(ANKINK_COLLECTION_SOURCE src/collection.cpp)
endif()

add_library(ankink_collection ${ANKINK_COLLECTION_SOURCE})
target_include_directories(ankink_collection PUBLIC include)
if(ANKINK_USE_RSLIB)
    target_link_libraries(ankink_collection PUBLIC ankink_anki_backend)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ankink_collection PUBLIC dl pthread m rt)
    endif()
    if(APPLE)
        target_link_libraries(ankink_collection PUBLIC
            "-framework CoreFoundation" "-framework Security"
            "-framework SystemConfiguration" resolv iconv)
    endif()
else()
    target_link_libraries(ankink_collection PUBLIC ${ANKINK_SQLITE_TARGET})
endif()
target_compile_options(ankink_collection PRIVATE -Wall -Wextra -Wpedantic)

if(ANKINK_BUILD_APP)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(ankink_mesquite_whisper_touch SHARED
            src/mesquite_whisper_touch.c)
        set_target_properties(ankink_mesquite_whisper_touch PROPERTIES
            OUTPUT_NAME mesquite-whisper-touch)
    endif()

    add_library(ankink_http_server src/http_server.cpp src/app_state.cpp
        src/orientation.cpp)
    target_include_directories(ankink_http_server PUBLIC include)
    target_link_libraries(ankink_http_server PUBLIC ankink_collection Threads::Threads)
    target_compile_definitions(ankink_http_server PRIVATE ANKINK_VERSION="${PROJECT_VERSION}")
    target_compile_options(ankink_http_server PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ankinkd src/daemon_main.cpp)
    target_link_libraries(ankinkd PRIVATE ankink_http_server)
    target_compile_options(ankinkd PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(ankinkd PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

    if(TARGET ankink_mesquite_whisper_touch)
        add_dependencies(ankinkd ankink_mesquite_whisper_touch)
    endif()

    install(TARGETS ankinkd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES assets/index.html assets/app.css assets/app-settings.js assets/config.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ankink)
    install(DIRECTORY assets/vendor DESTINATION ${CMAKE_INSTALL_DATADIR}/ankink)
    install(PROGRAMS scripts/run-kindle.sh DESTINATION . RENAME ankink.sh)

    if(ANKINK_BUILD_SIMULATOR)
        add_custom_target(ankink_simulator
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/run-simulator.sh"
                    "$<TARGET_FILE:ankinkd>" "${CMAKE_SOURCE_DIR}/assets"
                    "${CMAKE_SOURCE_DIR}/simulator"
                    "${CMAKE_SOURCE_DIR}/.ankink-simulator"
            DEPENDS ankinkd
            USES_TERMINAL
            COMMENT "Starting the AnkINK Kindle simulator")
    endif()

    if(ANKINK_BUILD_TESTS)
        add_executable(ankink_http_server_tests tests/http_server_test.cpp)
        target_link_libraries(ankink_http_server_tests PRIVATE ankink_http_server Threads::Threads)
        target_compile_options(ankink_http_server_tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ankink_http_server_tests COMMAND ankink_http_server_tests)
    endif()
endif()

if(ANKINK_BUILD_TESTS AND NOT ANKINK_USE_RSLIB)
    add_executable(ankink_collection_tests tests/collection_test.cpp)
    target_link_libraries(ankink_collection_tests PRIVATE ankink_collection)
    target_compile_options(ankink_collection_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ankink_collection_tests COMMAND ankink_collection_tests)
endif()

if(ANKINK_BUILD_TESTS)
    find_program(ANKINK_NODE_EXECUTABLE node)
    if(ANKINK_NODE_EXECUTABLE)
        add_test(NAME ankink_reviewer_contract_tests
            COMMAND "${ANKINK_NODE_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/reviewer_contract_test.js"
                    "${CMAKE_SOURCE_DIR}/assets/app-settings.js"
                    "${CMAKE_SOURCE_DIR}/backend/rust/src/lib.rs"
                    "${CMAKE_SOURCE_DIR}/assets/index.html"
                    "${CMAKE_SOURCE_DIR}/assets/vendor/katex/katex.min.js"
                    "${CMAKE_SOURCE_DIR}/assets/vendor/katex/katex.min.css"
                    "${CMAKE_SOURCE_DIR}/assets/vendor/katex/fonts"
                    "${CMAKE_SOURCE_DIR}/assets/app.css"
                    "${CMAKE_SOURCE_DIR}/scripts/run-kindle.sh"
                    "${CMAKE_SOURCE_DIR}/simulator/index.html"
                    "${CMAKE_SOURCE_DIR}/simulator/simulator.js"
                    "${CMAKE_SOURCE_DIR}/simulator/simulator.css"
                    "${CMAKE_SOURCE_DIR}/src/http_server.cpp"
                    "${CMAKE_SOURCE_DIR}/src/collection.cpp"
                    "${CMAKE_SOURCE_DIR}/src/mesquite_whisper_touch.c")
    endif()
endif()
