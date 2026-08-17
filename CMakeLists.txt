cmake_minimum_required(VERSION 3.16)
project(Qhpsdr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 Packages
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)
qt_standard_project_setup()

# External Dependencies (inherited from deskHPSDR)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3f)
pkg_check_modules(LIBSAMPLERATE REQUIRED samplerate)

# Include Directories
include_directories(include core core/wdsp-2.00)

# Source Files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    # Core files will be added here
)

add_executable(Qhpsdr ${SOURCES})

target_link_libraries(Qhpsdr PRIVATE 
    Qt6::Widgets 
    Qt6::OpenGLWidgets 
    Qt6::OpenGL
    ${FFTW3_LIBRARIES}
    ${LIBSAMPLERATE_LIBRARIES}
    pthread
    m
)

target_include_directories(Qhpsdr PRIVATE ${FFTW3_INCLUDE_DIRS} ${LIBSAMPLERATE_INCLUDE_DIRS})
