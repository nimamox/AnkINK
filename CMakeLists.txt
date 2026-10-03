cmake_minimum_required(VERSION 3.20)

project(AnkINK VERSION 0.4.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ANKINK_ENABLE_IPO "Enable checked C/C++ IPO for Kindle release builds" OFF)
option(ANKINK_VECTOR_REPORT "Report GCC vectorization decisions" OFF)
option(ANKINK_BUILD_BENCHMARKS "Build standalone workload benchmarks (not packaged)" OFF)
if(ANKINK_ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES C CXX)
  if(NOT IPO_SUPPORTED)
    message(FATAL_ERROR "IPO unavailable: ${IPO_ERROR}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
if(ANKINK_VECTOR_REPORT)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Vector reports require GCC")
  endif()
  add_compile_options(-fopt-info-vec-optimized -fopt-info-vec-missed)
  add_link_options(-fopt-info-vec-optimized -fopt-info-vec-missed)
endif()

include(CTest)
find_package(Threads REQUIRED)

set(ANKINK_BUILD_COMMIT "$ENV{ANKINK_BUILD_COMMIT}")
if(NOT ANKINK_BUILD_COMMIT)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE ANKINK_GIT_RESULT
            OUTPUT_VARIABLE ANKINK_BUILD_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT ANKINK_GIT_RESULT EQUAL 0)
            set(ANKINK_BUILD_COMMIT "unknown")
        else()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE ANKINK_GIT_DIRTY)
            if(NOT ANKINK_GIT_DIRTY EQUAL 0)
                string(APPEND ANKINK_BUILD_COMMIT "-dirty")
            endif()
        endif()
    else()
        set(ANKINK_BUILD_COMMIT "unknown")
    endif()
endif()
if(CMAKE_CROSSCOMPILING)
    set(ANKINK_BUILD_TYPE "release")
else()
    set(ANKINK_BUILD_TYPE "development")
endif()

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
    set(CURL_USE_STATIC_LIBS ON)
    set(OPENSSL_USE_STATIC_LIBS ON)
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # This preload runs inside system Mesquite, not under our bundled loader.
        # Leave its external references for the Kindle's native ABI.
        add_library(ankink_mesquite_whisper_touch SHARED
            src/mesquite_whisper_touch.c)
        set_target_properties(ankink_mesquite_whisper_touch PROPERTIES
            OUTPUT_NAME mesquite-whisper-touch
            LINK_FLAGS "-nostdlib")
    endif()

    add_library(ankink_http_server src/http_server.cpp src/app_state.cpp
        src/orientation.cpp src/kindle_display.cpp src/update_checker.cpp)
    target_include_directories(ankink_http_server PUBLIC include)
    target_link_libraries(ankink_http_server PUBLIC ankink_collection Threads::Threads
        CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ankink_http_server PUBLIC dl m rt)
    endif()
    target_compile_definitions(ankink_http_server PRIVATE
        ANKINK_VERSION="${PROJECT_VERSION}"
        ANKINK_BUILD_COMMIT="${ANKINK_BUILD_COMMIT}"
        ANKINK_BUILD_TYPE="${ANKINK_BUILD_TYPE}")
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
  add_executable(ankink_display_tests tests/kindle_display_test.cpp src/kindle_display.cpp)
  target_include_directories(ankink_display_tests PRIVATE include)
  target_link_libraries(ankink_display_tests PRIVATE Threads::Threads)
  add_test(NAME ankink_display_tests COMMAND ankink_display_tests)
        add_executable(ankink_native_night_api_tests tests/native_night_api_test.cpp)
        target_link_libraries(ankink_native_night_api_tests PRIVATE ankink_http_server Threads::Threads)
        add_test(NAME ankink_native_night_api_tests COMMAND ankink_native_night_api_tests)
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
      add_test(NAME ankink_native_night_frontend_tests
        COMMAND "${ANKINK_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/native_night_frontend_test.js"
                "${CMAKE_SOURCE_DIR}/assets/app-settings.js" "ankink")
        find_program(ANKINK_CARGO_EXECUTABLE cargo REQUIRED)
        set(ANKINK_MATH_TEST_TARGET "${CMAKE_BINARY_DIR}/math-test-target")
        set(ANKINK_MATH_FIXTURES "${ANKINK_MATH_TEST_TARGET}/release/ankink_math_fixtures")
        add_custom_command(
            OUTPUT "${ANKINK_MATH_FIXTURES}"
            COMMAND "${ANKINK_CARGO_EXECUTABLE}" build --locked --release
                    --manifest-path "${CMAKE_SOURCE_DIR}/tests/native_math/Cargo.toml"
                    --target-dir "${ANKINK_MATH_TEST_TARGET}"
            DEPENDS tests/native_math/Cargo.toml tests/native_math/Cargo.lock
                    tests/native_math/src/main.rs backend/rust/src/math.rs
            COMMENT "Building the host-only native card-math fixture bridge"
            VERBATIM)
        add_custom_target(ankink_math_fixtures ALL DEPENDS "${ANKINK_MATH_FIXTURES}")
        add_test(NAME ankink_native_math_tests
            COMMAND "${ANKINK_CARGO_EXECUTABLE}" test --locked --release
                    --manifest-path "${CMAKE_SOURCE_DIR}/tests/native_math/Cargo.toml"
                    --target-dir "${ANKINK_MATH_TEST_TARGET}")
        add_test(NAME ankink_math_layout_tests
            COMMAND "${ANKINK_NODE_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/math_layout_test.js"
                    "${CMAKE_SOURCE_DIR}/assets/app-settings.js"
                    "${ANKINK_MATH_FIXTURES}"
                    "${CMAKE_SOURCE_DIR}/tests/math_cases.json")
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

if(ANKINK_BUILD_BENCHMARKS)
  if(NOT ANKINK_USE_RSLIB)
    message(FATAL_ERROR "Workload benchmarks require the official Rust backend")
  endif()
  add_executable(workload_benchmark tests/workload_benchmark.cpp)
  target_link_libraries(workload_benchmark PRIVATE ankink_collection)
endif()
