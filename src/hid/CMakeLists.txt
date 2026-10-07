# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: The Feather Wallet Project
#
# Сборка модуля feather_hid: транспорт для протокола mwlink
# (HID-обмен с ESP32-S3 cold wallet).
#
# Модуль предоставляет:
#   • MwLink::buildMsg / parseMsg / detectKind / resultName / describeReset
#   • MwLink::Hid          — низкоуровневый HID-транспорт
#   • MwLink::Wallet       — клиент протокола (QObject, сигналы)
#   • MwLink::HidOperation — фоновый сценарий обмена (QObject)
#
# Зависимости:
#   • Qt6 Core, Concurrent
#   • hidapi (hidapi-hidraw на Linux, hidapi.lib на Windows,
#             -framework IOKit на macOS)
#   • zlib (для CRC32)

# ---------------------------------------------------------------------------
# Зависимости
# ---------------------------------------------------------------------------

# --- hidapi ---
#
# На Linux предпочитаем hidraw-бэкенд (работает через udev, не требует libusb).
# На macOS и Windows — обычный hidapi.

set(HIDAPI_FOUND FALSE)
set(HIDAPI_INCLUDE_DIRS "")
set(HIDAPI_LIBRARIES "")

if (UNIX AND NOT APPLE)
    # Linux: hidraw в приоритете.
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(HIDAPI QUIET hidapi-hidraw)
    endif()

    if (NOT HIDAPI_FOUND)
        find_package(hidapi CONFIG QUIET COMPONENTS hidraw)
        if (hidapi_FOUND)
            set(HIDAPI_FOUND TRUE)
            set(HIDAPI_LIBRARIES hidapi::hidapi-hidraw)
        endif()
    endif()

    if (NOT HIDAPI_FOUND)
        find_path(HIDAPI_INCLUDE_DIRS
            NAMES hidapi/hidapi.h
            PATHS /usr/include /usr/local/include $ENV{HIDAPI_ROOT}/include
        )
        find_library(HIDAPI_LIBRARIES
            NAMES hidapi-hidraw
            PATHS /usr/lib /usr/local/lib $ENV{HIDAPI_ROOT}/lib
        )
        if (HIDAPI_INCLUDE_DIRS AND HIDAPI_LIBRARIES)
            set(HIDAPI_FOUND TRUE)
        endif()
    endif()

else()
    # macOS / Windows: обычный hidapi.
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(HIDAPI QUIET hidapi)
    endif()

    if (NOT HIDAPI_FOUND)
        find_package(hidapi CONFIG QUIET)
        if (hidapi_FOUND)
            set(HIDAPI_FOUND TRUE)
            set(HIDAPI_LIBRARIES hidapi::hidapi)
        endif()
    endif()

    if (NOT HIDAPI_FOUND)
        find_path(HIDAPI_INCLUDE_DIRS
            NAMES hidapi/hidapi.h
            PATHS
                /usr/include
                /usr/local/include
                /opt/homebrew/include
                $ENV{HIDAPI_ROOT}/include
        )
        find_library(HIDAPI_LIBRARIES
            NAMES hidapi
            PATHS
                /usr/lib
                /usr/local/lib
                /opt/homebrew/lib
                $ENV{HIDAPI_ROOT}/lib
        )
        if (HIDAPI_INCLUDE_DIRS AND HIDAPI_LIBRARIES)
            set(HIDAPI_FOUND TRUE)
        endif()
    endif()
endif()

if (NOT HIDAPI_FOUND)
    message(WARNING
        "hidapi not found. feather_hid will not be built.\n"
        "  Linux:   sudo apt install libhidapi-dev\n"
        "  macOS:   brew install hidapi\n"
        "  Windows: vcpkg install hidapi, then -DCMAKE_TOOLCHAIN_FILE=...")
    return()
endif()

# --- zlib ---
find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# Источники
# ---------------------------------------------------------------------------

set(hid_sources
    MwLink.cpp
    MwHid.cpp
    MwWallet.cpp
    HidOperation.cpp
)

set(hid_headers
    MwLink.h
    MwHid.h
    MwWallet.h
    HidOperation.h
)

# ---------------------------------------------------------------------------
# Библиотека
# ---------------------------------------------------------------------------

add_library(feather_hid STATIC
    ${hid_sources}
    ${hid_headers}
)

# Автоматическая обработка moc — нужна для Wallet и HidOperation (QObject).
set_target_properties(feather_hid PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)

# ---------------------------------------------------------------------------
# Include-директории
# ---------------------------------------------------------------------------

target_include_directories(feather_hid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${HIDAPI_INCLUDE_DIRS}
)

# ---------------------------------------------------------------------------
# Зависимости
# ---------------------------------------------------------------------------
#
# ВАЖНО: Qt6::Core и Qt6::Concurrent — это конкретные цели Qt6.
# Не используйте Qt6${QT_VERSION_MAJOR}::Core — это даёт Qt66::Core и ошибку
# "The Concurrent target is mentioned as a dependency ... but not declared".

target_link_libraries(feather_hid
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
    PRIVATE
        ${HIDAPI_LIBRARIES}
        ZLIB::ZLIB
)

# На некоторых системах hidapi — статическая библиотека, и линковка
# требует явных зависимостей.
if (WIN32)
    target_link_libraries(feather_hid PRIVATE setupapi)
elseif (APPLE)
    target_link_libraries(feather_hid PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
    )
elseif (UNIX)
    # hidraw-бэкенд на Linux требует libudev.
    find_library(UDEV_LIBRARY udev)
    if (UDEV_LIBRARY)
        target_link_libraries(feather_hid PRIVATE ${UDEV_LIBRARY})
    endif()
endif()

# ---------------------------------------------------------------------------
# Настройки компиляции
# ---------------------------------------------------------------------------

target_compile_definitions(feather_hid PRIVATE
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x050F00
)

# Если Feather использует C++17 — модуль тоже.
target_compile_features(feather_hid PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# Отладочная информация
# ---------------------------------------------------------------------------

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
    message(STATUS "feather_hid: HIDAPI include = ${HIDAPI_INCLUDE_DIRS}")
    message(STATUS "feather_hid: HIDAPI libs    = ${HIDAPI_LIBRARIES}")
    message(STATUS "feather_hid: ZLIB version   = ${ZLIB_VERSION_STRING}")
endif()