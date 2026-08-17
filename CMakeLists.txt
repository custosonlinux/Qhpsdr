cmake_minimum_required(VERSION 3.16)
project(Qhpsdr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 Packages
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Network Multimedia)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)

include_directories(include core core/wdsp-2.00)

# WDSP (core/wdsp-2.00): framework-agnostic DSP engine, built as-is except
# for two vendored noise-reduction modules (sbnr.c/rnnr.c) that normally
# link against external libspecbleach/RNNoise libraries. Those aren't wired
# up yet, so NR3/NR4 compile as inert passthroughs (see the
# WDSP_HAVE_SPECBLEACH / WDSP_HAVE_RNNOISE guards in sbnr.c/h, rnnr.c/h) -
# define both and link the real libraries to enable them.
file(GLOB WDSP_SOURCES CONFIGURE_DEPENDS core/wdsp-2.00/*.c)
# make_calculus/make_interface/make_zetahat are standalone lookup-table
# generator tools (each has its own int main()), not part of the library.
list(FILTER WDSP_SOURCES EXCLUDE REGEX ".*/(make_calculus|make_interface|make_zetahat)\\.c$")

add_library(wdsp STATIC ${WDSP_SOURCES})
target_compile_definitions(wdsp PRIVATE _GNU_SOURCE)
target_link_libraries(wdsp PUBLIC PkgConfig::FFTW3F PkgConfig::FFTW3 m pthread)

# Qt application shell.
#
# core/ also holds a pristine, unmodified snapshot of the deskHPSDR source
# tree under core/deskhpsdr-src/ (see the project README for provenance).
# None of that snapshot is wired into this target: most of it still depends
# on GTK/GLib/cairo and needs to be ported file by file.
#
# Files directly under core/ (not core/deskhpsdr-src/) are the actual
# ported, GTK-free replacements as they get written.
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    include/mainwindow.h
    src/discoverydialog.cpp
    include/discoverydialog.h
    core/discovery.cpp
    core/discovery.h
    core/discovered.h
    core/oldprotocol.cpp
    core/oldprotocol.h
    core/rxaudio.cpp
    core/rxaudio.h
)

qt_add_executable(Qhpsdr ${SOURCES})

target_link_libraries(Qhpsdr PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
    Qt6::Network
    Qt6::Multimedia
    wdsp
)
