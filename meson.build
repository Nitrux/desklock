project(
    'Desklock',
    ['c', 'cpp'],
    version: '0.1.0',
    license: 'BSD-3-Clause',
    meson_version: '>=0.60.0',
    default_options: [
        'cpp_std=c++17',
        'warning_level=2',
        'werror=false',
        'buildtype=release',
    ],
)

if get_option('buildtype') == 'release'
    add_project_arguments('-DQT_NO_DEBUG_OUTPUT', language: 'cpp')
endif

qt_mod = import('qt6')
qt_deps = dependency(
    'qt6',
    version: '>=6.9',
    modules: [
        'Core',
        'DBus',
        'Gui',
        'Network',
        'Qml',
        'Quick',
        'QuickControls2',
        'WaylandClient',
    ],
    required: true,
)

kf6_i18n_dep = dependency('KF6I18n', version: '>=6.0', required: true, include_type: 'system')

# MauiKit embeds its QML component files in the MauiKit4 shared library.
# Keep it linked even when Desklock does not call a C++ symbol directly.
mauikit_dep = dependency('MauiKit4', version: '>=4.0.4', required: true)
mauikit_runtime_dep = declare_dependency(
    dependencies: mauikit_dep,
    link_args: ['-Wl,--no-as-needed'],
)
cpp_compiler = meson.get_compiler('cpp')

pam_dep = dependency('pam', required: false)
if not pam_dep.found()
    pam_dep = cpp_compiler.find_library('pam', required: false)
endif
if not pam_dep.found() or not cpp_compiler.has_header('security/pam_appl.h')
    error('Linux PAM development files were not found. On Debian-based systems, install libpam0g-dev.')
endif
threads_dep = dependency('threads')
wayland_client_dep = dependency('wayland-client', required: true)
wayland_protocols_dep = dependency('wayland-protocols', required: true)

qt6_gui = dependency('qt6', version: '>=6.9', modules: ['Gui'])
qt6_version = qt6_gui.version()
qt6_includedir = qt6_gui.get_variable(pkgconfig: 'includedir')
qt_private_include = include_directories(
    qt6_includedir / 'QtCore' / qt6_version,
    qt6_includedir / 'QtCore' / qt6_version / 'QtCore',
    qt6_includedir / 'QtGui' / qt6_version,
    qt6_includedir / 'QtGui' / qt6_version / 'QtGui',
    qt6_includedir / 'QtWaylandClient' / qt6_version,
    qt6_includedir / 'QtWaylandClient' / qt6_version / 'QtWaylandClient',
    qt6_includedir / 'QtWaylandGlobal' / qt6_version,
    qt6_includedir / 'QtWaylandGlobal' / qt6_version / 'QtWaylandGlobal',
)

if not cpp_compiler.has_header('qpa/qplatformnativeinterface.h',
                               dependencies: qt_deps,
                               include_directories: qt_private_include)
    error('Qt private GUI headers were not found. On Debian-based systems, install qt6-base-private-dev.')
endif
if not cpp_compiler.has_header('QtWaylandClient/private/qwaylandwindow_p.h',
                               dependencies: qt_deps,
                               include_directories: qt_private_include)
    error('Qt Wayland private headers were not found. On Debian-based systems, install qt6-wayland-private-dev.')
endif
if not cpp_compiler.has_header('QtWaylandGlobal/private/qtwaylandglobal-config_p.h',
                               dependencies: qt_deps,
                               include_directories: qt_private_include)
    error('Qt Wayland global private headers were not found. On Debian-based systems, install qt6-wayland-private-dev.')
endif

wayland_scanner = find_program('wayland-scanner', required: true)
wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
session_lock_xml = wayland_protocols_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml'

wayland_scanner_code = generator(
    wayland_scanner,
    output: '@BASENAME@-protocol.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)
wayland_scanner_header = generator(
    wayland_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)
protocol_sources = [
    wayland_scanner_code.process(session_lock_xml),
    wayland_scanner_header.process(session_lock_xml),
]

sources = files(
    'src/main.cpp',
    'src/backend/AuthBackend.cpp',
    'src/backend/CurrentUser.cpp',
    'src/backend/MprisController.cpp',
    'src/backend/ScreenRegistry.cpp',
    'src/backend/SessionLock.cpp',
    'src/backend/SystemBattery.cpp',
    'src/backend/SystemMonitor.cpp',
)

moc_headers = files(
    'src/backend/AuthBackend.h',
    'src/backend/CurrentUser.h',
    'src/backend/MprisController.h',
    'src/backend/ScreenRegistry.h',
    'src/backend/SessionLock.h',
    'src/backend/SystemBattery.h',
    'src/backend/SystemMonitor.h',
)
moc_files = qt_mod.preprocess(moc_headers: moc_headers)

qml_resources = qt_mod.compile_resources(
    name: 'desklock_resources',
    sources: 'resources.qrc',
)

executable(
    'desklock',
    sources + protocol_sources + moc_files,
    qml_resources,
    dependencies: [
        qt_deps,
        mauikit_runtime_dep,
        kf6_i18n_dep,
        pam_dep,
        threads_dep,
        wayland_client_dep,
    ],
    include_directories: qt_private_include,
    install: true,
)

install_data('pam.d/desklock', install_dir: get_option('sysconfdir') / 'pam.d')
