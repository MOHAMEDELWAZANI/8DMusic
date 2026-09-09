cmake_minimum_required(VERSION 3.22.1)
project(eightd_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The effect is NOT copied into this project.  It is compiled straight out of
# the desktop tree, so the Android build can never quietly drift into being a
# different effect -- a fix in cpp/src/dsp lands here on the next build.
set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../cpp/src/dsp)

if(NOT EXISTS ${DSP_DIR}/Processor.cpp)
    message(FATAL_ERROR "Cannot find the shared DSP at ${DSP_DIR}. "
                        "The android/ project must sit next to cpp/ in the repo.")
endif()

add_library(eightd SHARED
    jni_bridge.cpp
    ${DSP_DIR}/Processor.cpp
)

target_include_directories(eightd PRIVATE ${DSP_DIR})

# aaudio is the NDK's native audio API -- no third-party dependency, and it
# puts the DSP on the audio thread the same way PipeWire does on the desktop.
target_link_libraries(eightd android aaudio log)
