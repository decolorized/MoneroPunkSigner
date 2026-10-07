set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_COMPONENTS
        Core
        Widgets
        Gui
        Network
        Svg
        WebSockets
        SvgWidgets
        Concurrent
)

if (WITH_SCANNER)
    list(APPEND QT_COMPONENTS
            Multimedia
            MultimediaWidgets)
endif()

IF (UNIX AND NOT APPLE)
    list(APPEND QT_COMPONENTS WaylandClient)
ENDIF()

find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

if (CHECK_UPDATES)
    add_subdirectory(openpgp)
endif()

add_subdirectory(hid)

qt_add_resources(RESOURCES assets.qrc assets_tor.qrc assets_docs.qrc)

# Compile source files (.h/.cpp)
file(GLOB SOURCE_FILES
        "*.h"
        "*.cpp"
        "api/*.h"
        "api/*.cpp"
        "utils/*.h"
        "utils/*.cpp"
        "utils/os/*.h"
        "utils/os/*.cpp"
        "libwalletqt/*.h"
        "libwalletqt/*.cpp"
        "libwalletqt/rows/*.h"
        "libwalletqt/rows/*.cpp"
        "daemon/*.h"
        "daemon/*.cpp"
        "model/*.h"
        "model/*.cpp"
        "qt/*.h"
        "qt/*.cpp"
        "ui/qdarkstyle/style.qrc"
        "ui/BreezeStyleSheets/breeze.qrc"
        "widgets/*.h"
        "widgets/*.cpp"
        "wizard/*.h"
        "wizard/*.cpp"
        "wallet/*.h"
        "wallet/*.cpp"
        "qrcode/*.h"
        "qrcode/*.cpp"
        "dialog/*.h"
        "dialog/*.cpp"
        "polyseed/*.h"
        "polyseed/*.cpp"
        "polyseed/*.c"
        "monero_seed/argon2/blake2/*.c"
        "monero_seed/argon2/*.c"
        "monero_seed/*.cpp"
        "monero_seed/*.c"
        "monero_seed/*.hpp"
        "plugins/*.cpp"
        "plugins/*.h"
        )

get_cmake_property(_vars VARIABLES)
set(PLUGIN_PREFIX "WITH_PLUGIN_")

foreach (_var ${_vars})
    string(REGEX MATCH "^${PLUGIN_PREFIX}" _isPlugin ${_var})

    if (NOT ${_var})
        continue()
    endif()

    if(_isPlugin)
        string(REPLACE "${PLUGIN_PREFIX}" "" _suffix ${_var})
        string(TOLOWER "${_suffix}" _plugin)
        message(STATUS "Adding plugin: ${_plugin}")
        file (GLOB PLUGIN_FILES
                "plugins/${_plugin}/*.cpp"
                "plugins/${_plugin}/*.h"
        )
        list (APPEND SOURCE_FILES
                ${PLUGIN_FILES}
        )
    endif()
endforeach()

if (CHECK_UPDATES)
    file(GLOB UPDATER_FILES
            "utils/updater/*.h"
            "utils/updater/*.cpp")
endif()

# ---------------------------------------------------------------------------
# Offline transaction signing wizard
# ---------------------------------------------------------------------------
# Файлы визарда офлайн-подписи нужны ВСЕГДА, включая сборку без сканера:
# HID-подпись работает и при WITH_SCANNER=OFF.

file(GLOB OTS_FILES
        "wizard/offline_tx_signing/*.h"
        "wizard/offline_tx_signing/*.cpp")

if (WITH_SCANNER)
    file(GLOB QRCODE_UTILS_FILES
            "qrcode/utils/*.h"
            "qrcode/utils/*.cpp")
endif()

if (WITH_SCANNER)
    file(GLOB SCANNER_FILES
            "qrcode/scanner/*.h"
            "qrcode/scanner/*.cpp")
endif()

list(APPEND SOURCE_FILES
        ${UPDATER_FILES}
        ${OTS_FILES}
        ${QRCODE_UTILS_FILES}
        ${SCANNER_FILES})

# ---------------------------------------------------------------------------
# HID support (mwlink protocol)
# ---------------------------------------------------------------------------
# Файлы src/hid/*.{h,cpp} собираются отдельной библиотекой feather_hid,
# которая подключается как зависимость (см. ниже). В SOURCE_FILES
# их добавлять НЕ нужно — они не часть target feather напрямую.
#
# Здесь только проверяем, определена ли цель feather_hid (создаётся
# в src/hid/CMakeLists.txt через add_subdirectory в корневом CMakeLists).

set(FEATHER_HAVE_HID OFF)
if (TARGET feather_hid)
    set(FEATHER_HAVE_HID ON)
    message(STATUS "Feather: HID support enabled (feather_hid target found)")
else()
    message(STATUS "Feather: HID support disabled (no feather_hid target)")
endif()

set(EXECUTABLE_FLAG)
if(MINGW)
    set(EXECUTABLE_FLAG WIN32)

    set(ICON ${PROJECT_SOURCE_DIR}/src/assets/images/appicons/appicon.ico)
    set(ICON_RC ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
    set(ICON_RES ${CMAKE_CURRENT_BINARY_DIR}/icon.o)
    file(WRITE ${ICON_RC} "IDI_ICON1 ICON DISCARDABLE \"${ICON}\"")
    find_program(Qt_WINDRES_EXECUTABLE NAMES windres x86_64-w64-mingw32-windres REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)
    add_custom_command(OUTPUT ${ICON_RES} COMMAND ${Qt_WINDRES_EXECUTABLE} ${ICON_RC} ${ICON_RES} MAIN_DEPENDENCY ${ICON_RC})
    list(APPEND RESOURCES ${ICON_RES})
endif()

add_executable(feather ${EXECUTABLE_FLAG} main.cpp
        ${SOURCE_FILES}
        ${RESOURCES}
        ${ASSETS_TOR}
)

set_target_properties(feather PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LINK_FLAGS_RELEASE -s
)

if(STACK_TRACE)
    message(STATUS "Stack Trace Enabled")
    if (STATIC)
        set_property(TARGET feather APPEND PROPERTY LINK_FLAGS "-Wl,--wrap=__cxa_throw")
    endif()
endif()

target_include_directories(feather PUBLIC
        ${CMAKE_BINARY_DIR}/src/feather_autogen/include
        ${CMAKE_SOURCE_DIR}/monero/include
        ${CMAKE_SOURCE_DIR}/monero/src
        ${CMAKE_SOURCE_DIR}/monero/external
        ${CMAKE_SOURCE_DIR}/monero/external/easylogging++
        ${CMAKE_SOURCE_DIR}/monero/contrib/epee/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libwalletqt
        ${CMAKE_CURRENT_SOURCE_DIR}/model
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/qrcode
        ${Boost_INCLUDE_DIRS}
        ${QtCore_INCLUDE_DIRS}
        ${QtWidgets_INCLUDE_DIRS}
        ${QtGui_INCLUDE_DIRS}
        ${QtNetwork_INCLUDE_DIRS}
        ${QtSvg_INCLUDE_DIRS}
        ${QtSvgWidgets_INCLUDE_DIRS}
        ${QtXml_INCLUDE_DIRS}
        ${QtWebSockets_INCLUDE_DIRS}
        ${LIBZIP_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${POLYSEED_INCLUDE_DIR}
        ${BCUR_INCLUDE_DIR}
)

# ---------------------------------------------------------------------------
# HID include directories
# ---------------------------------------------------------------------------
# Feather включает MwLink.h и MwWallet.h из src/hid/.
# Публичный include feather_hid уже добавляет ${CMAKE_CURRENT_SOURCE_DIR}/hid,
# но для читаемости продублируем здесь (или можно убрать — Public-инклюд
# работает через target_link_libraries).
if (FEATHER_HAVE_HID)
    target_include_directories(feather PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/hid
    )
endif()

target_compile_definitions(feather PRIVATE FEATHER_VERSION="${PROJECT_VERSION}")
target_compile_definitions(feather PRIVATE FEATHER_TARGET_TRIPLET="${FEATHER_TARGET_TRIPLET}")
target_compile_definitions(feather PRIVATE TOR_VERSION="${TOR_VERSION}")

if(WITH_SCANNER)
    target_include_directories(feather PUBLIC
            ${QtMultimedia_INCLUDE_DIRS}
            ${QtMultimediaWidgets_INCLUDE_DIRS}
            ${ZXING_INCLUDE_DIRS}
    )
endif()

if(SELF_CONTAINED)
    target_compile_definitions(feather PRIVATE SELF_CONTAINED=1)
endif()

if(DONATE_BEG)
    target_compile_definitions(feather PRIVATE DONATE_BEG=1)
endif()

if (CHECK_UPDATES)
    target_compile_definitions(feather PRIVATE CHECK_UPDATES=1)
endif()

if(TOR_DIR)
    target_compile_definitions(feather PRIVATE HAS_TOR_BIN=1)
endif()

if(WITH_SCANNER)
    target_compile_definitions(feather PRIVATE WITH_SCANNER=1)
endif()

# ---------------------------------------------------------------------------
# HID compile definition
# ---------------------------------------------------------------------------
# FEATHER_HAVE_HID=1 — код Feather может использовать
#   #ifdef FEATHER_HAVE_HID
#       // HID-ветка визарда
#   #endif
# Если макрос не определён, HID-ветка не компилируется, комбобокс
# «HID device» можно не показывать (или показывать с сообщением).
if (FEATHER_HAVE_HID)
    target_compile_definitions(feather PRIVATE FEATHER_HAVE_HID=1)
endif()

# TODO: PLACEHOLDER
target_compile_definitions(feather PRIVATE HAS_WEBSOCKET=1)

if(PLATFORM_INSTALLER)
    target_compile_definitions(feather PRIVATE PLATFORM_INSTALLER=1)
endif()

if(TOR_INSTALLED)
    target_compile_definitions(feather PRIVATE TOR_INSTALLED=1)
endif()

if(STACK_TRACE)
    target_compile_definitions(feather PRIVATE STACK_TRACE=1)
endif()

if(STATIC)
    target_compile_definitions(feather PRIVATE STATIC=1)
endif()

if(OFFICIAL_BUILD)
    target_compile_definitions(feather PRIVATE OFFICIAL_BUILD=1)
endif()

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(feather PRIVATE QT_NO_DEBUG=1)
endif()

target_compile_definitions(feather
        PUBLIC
        ${QtCore_DEFINITIONS}
        ${QtWidgets_DEFINITIONS}
        ${QtGui_DEFINITIONS}
        ${QtNetwork_DEFINITIONS}
        ${QtSvg_DEFINITIONS}
        ${QtSvgWidgets_DEFINITIONS}
        ${QtXml_DEFINITIONS}
        ${QtWebSockets_DEFINITIONS}
)

if (WITH_SCANNER)
    target_compile_definitions(feather PUBLIC
            ${QtMultimedia_DEFINITIONS}
            ${QtMultimediaWidgets_DEFINITIONS}
    )
endif()

if (DEPENDS)
    target_link_directories(feather PRIVATE "${LIB_DIR}")
endif()

target_link_libraries(feather PRIVATE
        wallet_api
        epee
        easylogging
        ringct
        version
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${EXTRA_LIBRARIES}
        Qt::Core
        Qt::Widgets
        Qt::Gui
        Qt::Network
        Qt::Svg
        Qt::WebSockets
        Qt::SvgWidgets
        Threads::Threads
        ${QRENCODE_LIBRARY}
        ${POLYSEED_LIBRARY}
        ${ICU_LIBRARIES}
        ${LIBZIP_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${BCUR_LIBRARY}
)

# ---------------------------------------------------------------------------
# HID library
# ---------------------------------------------------------------------------
# feather_hid — статическая библиотека из src/hid/CMakeLists.txt.
# Подключается условно: если hidapi/zlib не найдены, цели нет.
if (FEATHER_HAVE_HID)
    target_link_libraries(feather PRIVATE feather_hid)
    message(STATUS "Feather: linked against feather_hid")
endif()

if(CHECK_UPDATES)
    target_link_libraries(feather PRIVATE openpgp)
endif()

if(DEVICE_TREZOR_READY)
    find_package(Protobuf CONFIG)
    if (NOT Protobuf_FOUND)
        find_package(Protobuf REQUIRED)
    endif()
    target_link_libraries(feather PRIVATE ${TREZOR_DEP_LIBS})
endif()

if (WITH_SCANNER)
    target_link_libraries(feather PRIVATE
            Qt::Multimedia
            Qt::MultimediaWidgets
            ${ZXING_LIBRARIES}
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(feather PRIVATE Qt::WaylandClient)
endif()

if(STATIC AND APPLE)
    target_link_libraries(feather PRIVATE Qt6::QDarwinCameraPermissionPlugin)
endif()

if(STATIC AND UNIX AND NOT APPLE)
    target_link_libraries(feather PRIVATE Qt6::QComposePlatformInputContextPlugin)
    target_link_libraries(feather PRIVATE Qt6::QWaylandIntegrationPlugin)
endif()

if(DEPENDS AND UNIX AND NOT APPLE)
    # workaround for: undefined reference to `XauGetBestAuthByAddr'
    find_library(LIBXAU_LIBRARY Xau REQUIRED)
    message(STATUS "libXau: libraries at ${LIBXAU_LIBRARY}")
    target_link_libraries(XCB::XCB INTERFACE ${LIBXAU_LIBRARY})

    # make sure Wayland::Client links to libffi
    find_library(LIBFFI_LIBRARY ffi REQUIRED)
    message(STATUS "libffi: libraries at ${LIBFFI_LIBRARY}")
    target_link_libraries(Wayland::Client INTERFACE ${LIBFFI_LIBRARY})
endif()

if(STACK_TRACE AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(feather PRIVATE -rdynamic)
endif()

if(DEPENDS)
    install(TARGETS feather
            DESTINATION ${CMAKE_INSTALL_PREFIX}
    )
else()
    install(TARGETS feather
            DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    )

    if (UNIX AND NOT APPLE)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/feather.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/32x32.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/32x32/apps RENAME "feather.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/48x48.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/48x48/apps RENAME "feather.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/64x64.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/64x64/apps RENAME "feather.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/96x96.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/96x96/apps RENAME "feather.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/128x128.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/128x128/apps RENAME "feather.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/256x256.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps RENAME "feather.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/appicons/512x512.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/512x512/apps RENAME "feather.png")
    endif()
endif()

if(APPLE)
    configure_file(${CMAKE_SOURCE_DIR}/contrib/macdeploy/Info.plist.in ${CMAKE_SOURCE_DIR}/contrib/macdeploy/Info.plist @ONLY)

    set_target_properties(feather PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/contrib/macdeploy/Info.plist"
            LINK_FLAGS_RELEASE -s
    )

    file(COPY "${CMAKE_SOURCE_DIR}/src/assets/images/appicons/appicon.icns" DESTINATION "${CMAKE_SOURCE_DIR}/installed/feather.app/Contents/Resources/" )
endif()

qt_finalize_executable(feather)