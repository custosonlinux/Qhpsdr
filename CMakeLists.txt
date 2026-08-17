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
    core/discovery.c
    core/new_protocol.c
    core/old_protocol.c
    core/radio.c
    core/receiver.c
    core/transmitter.c
    core/discovered.c
    core/audio.c
    core/vfo.c
    core/band.c
    # WDSP files will be added in next step
    core/wdsp-2.00/FDnoiseIQ.c
    core/wdsp-2.00/RXA.c
    core/wdsp-2.00/TXA.c
    core/wdsp-2.00/amd.c
    core/wdsp-2.00/ammod.c
    core/wdsp-2.00/amsq.c
    core/wdsp-2.00/analyzer.c
    core/wdsp-2.00/anf.c
    core/wdsp-2.00/anr.c
    core/wdsp-2.00/apfshadow.c
    core/wdsp-2.00/bandpass.c
    core/wdsp-2.00/calcc.c
    core/wdsp-2.00/calculus.c
    core/wdsp-2.00/cblock.c
    core/wdsp-2.00/cfcomp.c
    core/wdsp-2.00/cfir.c
    core/wdsp-2.00/channel.c
    core/wdsp-2.00/cmath.c
    core/wdsp-2.00/compress.c
    core/wdsp-2.00/delay.c
    core/wdsp-2.00/dexp.c
    core/wdsp-2.00/div.c
    core/wdsp-2.00/doublepole.c
    core/wdsp-2.00/eer.c
    core/wdsp-2.00/emnr.c
    core/wdsp-2.00/emph.c
    core/wdsp-2.00/eq.c
    core/wdsp-2.00/extrapolate.c
    core/wdsp-2.00/fcurve.c
    core/wdsp-2.00/fir.c
    core/wdsp-2.00/firmin.c
    core/wdsp-2.00/fmd.c
    core/wdsp-2.00/fmmod.c
    core/wdsp-2.00/fmsq.c
    core/wdsp-2.00/gain.c
    core/wdsp-2.00/gaussian.c
    core/wdsp-2.00/gen.c
    core/wdsp-2.00/icfir.c
    core/wdsp-2.00/iir.c
    core/wdsp-2.00/impulse_cache.c
    core/wdsp-2.00/iobuffs.c
    core/wdsp-2.00/iqc.c
    core/wdsp-2.00/linux_port.c
    core/wdsp-2.00/lmath.c
    core/wdsp-2.00/make_calculus.c
    core/wdsp-2.00/make_interface.c
    core/wdsp-2.00/make_zetahat.c
    core/wdsp-2.00/matchedCW.c
    core/wdsp-2.00/meter.c
    core/wdsp-2.00/meterlog10.c
    core/wdsp-2.00/nbp.c
    core/wdsp-2.00/nob.c
    core/wdsp-2.00/nobII.c
    core/wdsp-2.00/nurbs.c
    core/wdsp-2.00/nurbs_fit.c
    core/wdsp-2.00/nurbs_spline.c
    core/wdsp-2.00/osctrl.c
    core/wdsp-2.00/patchpanel.c
    core/wdsp-2.00/phrot.c
    core/wdsp-2.00/resample.c
    core/wdsp-2.00/reshb.c
    core/wdsp-2.00/rmatch.c
    core/wdsp-2.00/rnnr.c
    core/wdsp-2.00/sbnr.c
    core/wdsp-2.00/sender.c
    core/wdsp-2.00/shift.c
    core/wdsp-2.00/siphon.c
    core/wdsp-2.00/slew.c
    core/wdsp-2.00/snb.c
    core/wdsp-2.00/snoop.c
    core/wdsp-2.00/ssql.c
    core/wdsp-2.00/syncbuffs.c
    core/wdsp-2.00/utilities.c
    core/wdsp-2.00/varsamp.c
    core/wdsp-2.00/version.c
    core/wdsp-2.00/wbfm.c
    core/wdsp-2.00/wcpAGC.c
    core/wdsp-2.00/wisdom.c
    core/wdsp-2.00/zetaHat.c
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
