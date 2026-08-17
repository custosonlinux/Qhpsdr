# Qhpsdr

Next-Generation SDR application for Hermes, Anan, and Hermes-Lite 2 devices.

## Overview

**Qhpsdr** is a modern, high-performance software-defined radio (SDR) application built with **Qt6** and **OpenGL**. It is a major evolution of the [deskHPSDR](https://github.com/dl1bz/deskhpsdr) project, aimed at providing a fluid, intuitive, and modern user experience for radio amateurs.

## Key Goals

- **Qt6 Migration:** Moving away from GTK to a modern, cross-platform UI framework.
- **OpenGL Acceleration:** Offloading spectrum and waterfall rendering to the GPU for ultra-smooth performance even at high sample rates.
- **Intuitive UI:** Redesigning the menu structure from the ground up to be more intuitive and user-friendly.
- **Modular Core:** Keeping the robust SDR-core (Metis protocol, WDSP) while wrapping it in clean C++ for better maintainability.

## Repository Layout

- `src/`, `include/` — the Qt6 application shell. Currently a minimal `MainWindow` skeleton; this is where the ported, GTK-free logic and Qt/OpenGL UI will live.
- `core/deskhpsdr-src/` — a **pristine, unmodified snapshot** of the deskHPSDR C source tree (imported 2026-08-17). It still depends on GTK/GLib/cairo throughout and is **not** part of the CMake build. It exists as the reference/staging area we port from: files move out of here (or get rewritten) into `src/`/`core/` proper, one at a time, GTK-free and verified to compile, instead of in one large unverified pass.
- `core/wdsp-2.00/` — the WDSP DSP engine. Framework-agnostic (no GTK/GLib), but has external build-time dependencies (`libspecbleach`, `rnnoise`, see deskHPSDR's `update_libs.sh`) that aren't wired up yet, so it's not part of the CMake build yet either.

## Current Status

Restarted 2026-08-17 from a clean deskHPSDR baseline after an earlier partial GTK-cleanup attempt turned out to leave GTK/GLib/cairo code (and non-compiling headers) throughout `core/`. That attempt is preserved under the git tag `archive/gtk-partial-cleanup` for reference.

- [x] Qt6 project skeleton (builds and runs, no core logic wired in yet).
- [x] Pristine deskHPSDR source imported as a porting reference (`core/deskhpsdr-src/`).
- [ ] Port discovery/protocol/radio logic out of GTK, file by file, into the Qt build.
- [ ] Wire up WDSP (build external deps, link into CMake target).
- [ ] First basic hardware discovery in the Qt UI.

## Build Requirements

- **Qt 6.5+** (Widgets, OpenGLWidgets)
- **CMake 3.16+**

Additional requirements (FFTW3, libsamplerate, libspecbleach, rnnoise) will be reintroduced as WDSP and the protocol/audio layers get wired back into the build.

## Development

This project was initiated as a fork of deskHPSDR by dl1bz.

---
**Note:** This is an active work in progress. Expect frequent changes.
