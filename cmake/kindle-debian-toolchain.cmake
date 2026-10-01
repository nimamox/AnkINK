set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

if(NOT DEFINED ENV{KINDLE_SDK_ROOT})
    message(FATAL_ERROR "Set KINDLE_SDK_ROOT to the SDK directory (for example /home/nima/ankink-sdk)")
endif()
if(NOT DEFINED ENV{KINDLE_ABI})
    message(FATAL_ERROR "Set KINDLE_ABI to armel or armhf")
endif()

set(ANKINK_SDK_ROOT "$ENV{KINDLE_SDK_ROOT}")
set(ANKINK_KINDLE_ABI "$ENV{KINDLE_ABI}")

if(ANKINK_KINDLE_ABI STREQUAL "armel")
    set(ANKINK_TRIPLET arm-linux-gnueabi)
    set(ANKINK_FLOAT_FLAGS "-mfpu=neon -mfloat-abi=softfp")
elseif(ANKINK_KINDLE_ABI STREQUAL "armhf")
    set(ANKINK_TRIPLET arm-linux-gnueabihf)
    set(ANKINK_FLOAT_FLAGS "-mfpu=neon -mfloat-abi=hard")
else()
    message(FATAL_ERROR "KINDLE_ABI must be armel or armhf, not '${ANKINK_KINDLE_ABI}'")
endif()

set(CMAKE_SYSROOT "${ANKINK_SDK_ROOT}/${ANKINK_KINDLE_ABI}")
set(CMAKE_LIBRARY_ARCHITECTURE "${ANKINK_TRIPLET}")
set(CMAKE_C_COMPILER "${ANKINK_SDK_ROOT}/bin/${ANKINK_TRIPLET}-gcc")
set(CMAKE_CXX_COMPILER "${ANKINK_SDK_ROOT}/bin/${ANKINK_TRIPLET}-g++")
set(CMAKE_AR "${ANKINK_SDK_ROOT}/bin/${ANKINK_TRIPLET}-ar")
set(CMAKE_RANLIB "${ANKINK_SDK_ROOT}/bin/${ANKINK_TRIPLET}-ranlib")
set(CMAKE_STRIP "${ANKINK_SDK_ROOT}/bin/${ANKINK_TRIPLET}-strip")

set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mtune=generic-armv7-a ${ANKINK_FLOAT_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mtune=generic-armv7-a ${ANKINK_FLOAT_FLAGS}")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/${ANKINK_TRIPLET}/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
