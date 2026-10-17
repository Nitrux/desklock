cmake_minimum_required(VERSION 3.24)

project(desklock VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(CheckCXXCompilerFlag)
if(NOT CMAKE_CROSSCOMPILING)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" DESKLOCK_HOST_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" DESKLOCK_TARGET_PROCESSOR)
    if(DESKLOCK_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$"
       AND DESKLOCK_TARGET_PROCESSOR MATCHES "^(x86_64|amd64)$")
        check_cxx_compiler_flag("-march=x86-64-v3" DESKLOCK_HAS_X86_64_V3)
        if(DESKLOCK_HAS_X86_64_V3)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=x86-64-v3>)
        endif()
    endif()
endif()

set(QT_NO_PRIVATE_MODULE_WARNING ON CACHE BOOL
    "MauiKit uses Qt's private WaylandClient API" FORCE)

find_package(ECM 6.13 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMFindQmlModule)
include(FeatureSummary)

find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core DBus Gui GuiPrivate Network Qml Quick QuickControls2
    WaylandClient WaylandClientPrivate)
find_package(KF6 6.13 REQUIRED COMPONENTS CoreAddons I18n)
find_package(MauiKit4 4.0.5 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)
pkg_check_modules(PAM REQUIRED IMPORTED_TARGET pam)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
ecm_find_qmlmodule(org.mauikit.controls 1.0)

set(SESSION_LOCK_PROTOCOL
    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml")
set(GENERATED_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(SESSION_LOCK_PROTOCOL_C
    "${GENERATED_PROTOCOL_DIR}/ext-session-lock-v1-protocol.c")
set(SESSION_LOCK_PROTOCOL_H
    "${GENERATED_PROTOCOL_DIR}/ext-session-lock-v1-client-protocol.h")

add_custom_command(
    OUTPUT "${SESSION_LOCK_PROTOCOL_C}" "${SESSION_LOCK_PROTOCOL_H}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_PROTOCOL_DIR}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${SESSION_LOCK_PROTOCOL}" "${SESSION_LOCK_PROTOCOL_C}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${SESSION_LOCK_PROTOCOL}" "${SESSION_LOCK_PROTOCOL_H}"
    DEPENDS "${SESSION_LOCK_PROTOCOL}")

add_executable(desklock
    src/main.cpp
    src/backend/AuthBackend.cpp
    src/backend/AuthBackend.h
    src/backend/CurrentUser.cpp
    src/backend/CurrentUser.h
    src/backend/MprisController.cpp
    src/backend/MprisController.h
    src/backend/ScreenRegistry.cpp
    src/backend/ScreenRegistry.h
    src/backend/SessionLock.cpp
    src/backend/SessionLock.h
    src/backend/SystemBattery.cpp
    src/backend/SystemBattery.h
    src/backend/SystemMonitor.cpp
    src/backend/SystemMonitor.h
    resources.qrc
    "${SESSION_LOCK_PROTOCOL_C}"
    "${SESSION_LOCK_PROTOCOL_H}")

target_include_directories(desklock PRIVATE "${GENERATED_PROTOCOL_DIR}")
target_link_options(desklock PRIVATE -Wl,--no-as-needed)
target_link_libraries(desklock PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    MauiKit4
    PkgConfig::PAM
    Threads::Threads
    Wayland::Client)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(desklock PRIVATE QT_NO_DEBUG_OUTPUT)
endif()

install(TARGETS desklock RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES pam.d/desklock DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/pam.d)

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
