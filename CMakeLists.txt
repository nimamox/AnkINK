cmake_minimum_required(VERSION 3.20)

project(AnkINK VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ANKINK_BUILD_APP_DEFAULT ON)
else()
    set(ANKINK_BUILD_APP_DEFAULT OFF)
endif()
option(ANKINK_BUILD_APP "Build the Kindle WPE/FBInk application" ${ANKINK_BUILD_APP_DEFAULT})
option(ANKINK_BUILD_TESTS "Build dependency-free core tests" ON)

add_library(ankink_core
    src/frame.cpp
)
target_include_directories(ankink_core PUBLIC include)
target_compile_options(ankink_core PRIVATE -Wall -Wextra -Wpedantic)

if(ANKINK_BUILD_TESTS)
    add_executable(ankink_core_tests tests/frame_test.cpp)
    target_link_libraries(ankink_core_tests PRIVATE ankink_core)
    target_compile_options(ankink_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ankink_core_tests COMMAND ankink_core_tests)

    find_package(SQLite3 QUIET)
    if(SQLite3_FOUND)
        if(TARGET SQLite3::SQLite3)
            set(ANKINK_SQLITE_TARGET SQLite3::SQLite3)
        else()
            set(ANKINK_SQLITE_TARGET SQLite::SQLite3)
        endif()
        add_executable(ankink_collection_tests src/collection.cpp tests/collection_test.cpp)
        target_include_directories(ankink_collection_tests PRIVATE include)
        target_link_libraries(ankink_collection_tests PRIVATE ${ANKINK_SQLITE_TARGET})
        target_compile_options(ankink_collection_tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ankink_collection_tests COMMAND ankink_collection_tests)
    endif()
endif()

if(ANKINK_BUILD_APP)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The AnkINK application requires Linux framebuffer and input APIs")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_search_module(WPE_WEBKIT REQUIRED IMPORTED_TARGET
        wpe-webkit-2.0>=2.38
        wpe-webkit-1.1>=2.38
        wpe-webkit-1.0>=2.38
    )
    pkg_check_modules(LIBWPE REQUIRED IMPORTED_TARGET wpe-1.0>=1.14)
    pkg_check_modules(WPE_FDO REQUIRED IMPORTED_TARGET wpebackend-fdo-1.0>=1.14)
    pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server>=1.18)
    pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm>=2.4)
    find_package(SQLite3 REQUIRED)
    if(TARGET SQLite3::SQLite3)
        set(ANKINK_SQLITE_TARGET SQLite3::SQLite3)
    else()
        set(ANKINK_SQLITE_TARGET SQLite::SQLite3)
    endif()

    find_path(FBINK_INCLUDE_DIR fbink.h REQUIRED)
    find_library(FBINK_LIBRARY NAMES fbink REQUIRED)

    add_library(ankink_compat SHARED src/memfd_compat.c)
    set_target_properties(ankink_compat PROPERTIES
        OUTPUT_NAME "ankink-compat"
    )
    target_compile_options(ankink_compat PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(ankink_compat PRIVATE ${CMAKE_DL_LIBS})

    add_executable(ankink_legacy_layout src/legacy_layout_exec.c)
    set_target_properties(ankink_legacy_layout PROPERTIES
        OUTPUT_NAME "ankink-legacy-layout"
    )
    target_compile_options(ankink_legacy_layout PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ankink_loading_screen src/loading_screen.c)
    set_target_properties(ankink_loading_screen PROPERTIES
        OUTPUT_NAME "ankink-loading-screen"
    )
    target_include_directories(ankink_loading_screen PRIVATE "${FBINK_INCLUDE_DIR}")
    target_link_libraries(ankink_loading_screen PRIVATE "${FBINK_LIBRARY}")
    target_compile_options(ankink_loading_screen PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ankink
        src/application.cpp
        src/collection.cpp
        src/fbink_presenter.cpp
        src/input.cpp
        src/main.cpp
        src/wpe_view.cpp
    )
    target_include_directories(ankink PRIVATE include "${FBINK_INCLUDE_DIR}")
    target_link_libraries(ankink PRIVATE
        ankink_core
        PkgConfig::WPE_WEBKIT
        PkgConfig::LIBWPE
        PkgConfig::WPE_FDO
        PkgConfig::WAYLAND_SERVER
        PkgConfig::LIBDRM
        ${ANKINK_SQLITE_TARGET}
        "${FBINK_LIBRARY}"
    )
    target_compile_definitions(ankink PRIVATE
        ANKINK_VERSION="${PROJECT_VERSION}"
        ANKINK_INSTALL_ASSET_DIR="${CMAKE_INSTALL_FULL_DATADIR}/ankink"
    )
    if(WPE_WEBKIT_MODULE_NAME STREQUAL "wpe-webkit-2.0")
        target_compile_definitions(ankink PRIVATE ANKINK_WPE_API_2=1)
    endif()
    target_compile_options(ankink PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(ankink PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
    )

    add_executable(ankink_wpe_runtime_smoke tests/wpe_runtime_smoke.cpp)
    target_link_libraries(ankink_wpe_runtime_smoke PRIVATE
        PkgConfig::LIBWPE
        PkgConfig::WPE_FDO
    )
    target_compile_options(ankink_wpe_runtime_smoke PRIVATE
        -Wall -Wextra -Wpedantic
    )

    find_library(ANKINK_EGL_LIBRARY NAMES EGL REQUIRED)
    find_library(ANKINK_GLES2_LIBRARY NAMES GLESv2 REQUIRED)
    add_executable(ankink_egl_runtime_smoke tests/egl_runtime_smoke.c)
    target_link_libraries(ankink_egl_runtime_smoke PRIVATE
        "${ANKINK_EGL_LIBRARY}"
        "${ANKINK_GLES2_LIBRARY}"
    )
    target_compile_options(ankink_egl_runtime_smoke PRIVATE
        -Wall -Wextra -Wpedantic
    )

    install(TARGETS ankink RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS ankink_legacy_layout RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS ankink_loading_screen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS ankink_compat LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/ankink)
    install(PROGRAMS scripts/run-kindle.sh DESTINATION . RENAME ankink.sh)
endif()
