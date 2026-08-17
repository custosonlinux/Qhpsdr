cmake_minimum_required(VERSION 3.16)
project(Qhpsdr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 Packages
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)
qt_standard_project_setup()

include_directories(include)

# Qt application shell.
#
# core/ currently holds a pristine, unmodified snapshot of the deskHPSDR
# source tree (see core/deskhpsdr-src/README or the project README for
# provenance). None of it is wired into this target yet: most of it still
# depends on GTK/GLib/cairo and needs to be ported file by file. core/wdsp-2.00
# is framework-agnostic DSP code, but still needs its external dependencies
# (libspecbleach, rnnoise) built before it can be linked in.
#
# Add files here only once they've actually been ported/verified to build
# without GTK.
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    include/mainwindow.h
)

qt_add_executable(Qhpsdr ${SOURCES})

target_link_libraries(Qhpsdr PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)
