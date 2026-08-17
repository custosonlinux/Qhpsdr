# Qhpsdr

Next-Generation SDR application for Hermes, Anan, and Hermes-Lite 2 devices.

## Overview

**Qhpsdr** is a modern, high-performance software-defined radio (SDR) application built with **Qt6** and **OpenGL**. It is a major evolution of the [deskHPSDR](https://github.com/dl1bz/deskhpsdr) project, aimed at providing a fluid, intuitive, and modern user experience for radio amateurs.

## Key Goals

- **Qt6 Migration:** Moving away from GTK to a modern, cross-platform UI framework.
- **OpenGL Acceleration:** Offloading spectrum and waterfall rendering to the GPU for ultra-smooth performance even at high sample rates.
- **Intuitive UI:** Redesigning the menu structure from the ground up to be more intuitive and user-friendly.
- **Modular Core:** Keeping the robust SDR-core (Metis protocol, WDSP) while wrapping it in clean C++ for better maintainability.

## Current Status (Step 1)

The project is currently in its early stages:
- [x] Initial Qt6 project skeleton.
- [x] GitHub Repository setup.
- [ ] Integration of SDR-Core from deskHPSDR.
- [ ] First basic hardware discovery.

## Build Requirements

- **Qt 6.5+** (Widgets, OpenGLWidgets)
- **CMake 3.16+**
- **FFTW3** (Float version)
- **libsamplerate**
- **PulseAudio / ALSA** (Linux)

## Development

This project was initiated as a fork of deskHPSDR by dl1bz.

---
**Note:** This is an active work in progress. Expect frequent changes.
