# Desklock | [![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

Desklock is a native QML session locker for Wayland environments.

# Introduction

Desklock is a modern, lightweight lock screen designed for Nitrux. Built with **[MauiKit](https://mauikit.org/)** to deliver a polished, consistent user interface.

Desklock runs natively on Wayland compositors such as Hyprland or Sway.

> [!WARNING]
> Desklock does not support X11. Desklock's main target is Nitrux OS, and using it in other distributions is not within its scope. Please do not open issues regarding this use case; they will be closed.

## Features

- Secure lifecycle with one lock surface for every connected output.
- Direct Linux PAM authentication of the user running Desklock.
- CPU, memory, and network monitors.
- Native MPRIS discovery, metadata, album art, and playback controls over the session D-Bus.
- MauiKit-native lock-screen controls.
- Configurable fade-in/fade-out durations and clock formats.

### Runtime Requirements

```
mauikit (>= 4.0.3)
qt6 (>= 6.9.2)
qt6-wayland (>= 6.9.2)
greetd
wayland
```

# Licensing

The license for this repository and its contents is **BSD-3-Clause**.

# Issues

If you find problems with the contents of this repository, please create an issue and use the **🐞 Bug report** template.

## Submitting a bug report

Before submitting a bug, check the existing bug reports to verify that it has not already been reported.

©2026 Nitrux Latinoamericana S.C.
