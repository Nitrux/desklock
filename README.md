# Desklock | [![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

Desklock is a native QML session locker for Wayland environments. It adapts QMLGreet’s MauiKit visual architecture for an already-running user session, with no dependency on greetd, systemd, or logind.

## Features

- Secure `ext-session-lock-v1` lifecycle with one lock surface for every connected output.
- Direct Linux PAM authentication of the user running Desklock.
- CPU, memory, and network monitors backed by `/proc/stat`, `/proc/meminfo`, and `/proc/net/dev`.
- Native MPRIS discovery, metadata, album art, and playback controls over the session D-Bus.
- MauiKit-native lock-screen controls for the clock, status chips, avatar, password entry, media card, and wallpaper surface.
- Configurable fade-in/fade-out durations and clock formats.

## Runtime requirements

- Qt 6.9 or newer (`Core`, `DBus`, `Gui`, `Qml`, `Quick`, `QuickControls2`, and `WaylandClient`)
- MauiKit 4.0.4 or newer
- KDE Frameworks 6 I18n (provides MauiKit's QML translation context)
- Linux PAM
- Wayland and `wayland-protocols`
- An OpenRC-compatible `zzz` command when using the provided automatic suspend hook
- A compositor implementing `ext-session-lock-v1`, such as Hyprland

## Configuration

On first launch, Desklock creates `desklock/desklock.conf` below the XDG user configuration directory from its embedded defaults. With the usual XDG environment this is `~/.config/desklock/desklock.conf`. The file remains owned and editable by the current user; `desklock --config <path>` selects another user-owned file.

The available sections are:

- `[Appearance]`: wallpaper, blur and overlay levels, and an optional avatar override.
- `[Clock]`: time/date formats and optional lowercase date text.
- `[Battery]`: visibility and sysfs polling interval.
- `[Media]`: MPRIS card visibility.
- `[SystemMonitor]`: resource-chip visibility and procfs polling interval.
- `[Behavior]`: lock/unlock fade durations.

Disabled battery, media, and monitor sections do not start their background polling or D-Bus discovery. Polling intervals and fade durations are expressed in milliseconds. Changes take effect on the next launch. Desklock creates the parent directory when necessary and gives a newly created configuration owner-only read/write permissions.

The PAM policy is installed as `/etc/pam.d/desklock`. Distributions without a `login` PAM stack must adapt that file to their local policy.

## Hyprland

Desklock is designed to be launched directly by `hypridle`:

```ini
general {
    lock_cmd = pidof desklock || desklock
    before_sleep_cmd = pidof desklock || desklock
    after_sleep_cmd = hyprctl dispatch dpms on
}

listener {
    timeout = 300
    on-timeout = pidof desklock || desklock
}
```

`before_sleep_cmd` intentionally starts the protocol locker directly rather than delegating locking to a service manager.

## Building

Desklock uses Meson. On Debian-based systems, install the development packages `libpam0g-dev`, `libkf6i18n-dev`, `qt6-base-private-dev`, and `qt6-wayland-private-dev` before configuring. MauiKit4 is a required linked dependency because its shared library embeds the QML component resource bundle. Desklock keeps that library loaded even though it does not call MauiKit C++ APIs directly.

```sh
meson setup build
meson compile -C build
```

The project uses a small Qt Wayland shell integration, following LayerShellQt’s integration pattern, to assign the `ext-session-lock-v1` role before the first buffer, suppress Qt's otherwise-invalid initial null-buffer commit, and synchronize configure acknowledgements with Qt's render thread.

## Logging

Desklock forwards Qt, QML, PAM-result, output-hotplug, and session-lock lifecycle messages to the standard syslog API using the `LOG_USER` facility while retaining stderr output. On systems running rsyslog with its usual rules these records appear in `/var/log/syslog` and are tagged `desklock`. No passwords or PAM conversation contents are logged.

# Licensing

The license for this repository and its contents is **BSD-3-Clause**.

# Issues

If you find problems with the contents of this repository, please create an issue and use the **🐞 Bug report** template.

## Submitting a bug report

Before submitting a bug, check the existing bug reports to verify that it has not already been reported.

©2026 Nitrux Latinoamericana S.C.
