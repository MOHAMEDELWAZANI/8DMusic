cmake_minimum_required(VERSION 3.20)
project(8DMusicWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR
        "The APO needs the Windows SDK's audioenginebaseapo.h and MSVC's COM "
        "support. Build this with Visual Studio 2022, not MinGW.")
endif()

# The effect is NOT copied here. It is compiled straight out of the desktop
# tree, exactly as the Android build does, so the three platforms can never
# quietly diverge into three different effects.
set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/src/dsp)
if(NOT EXISTS ${DSP_DIR}/Processor.cpp)
    message(FATAL_ERROR "Cannot find the shared DSP at ${DSP_DIR}. "
                        "windows/ must sit next to cpp/ in the repo.")
endif()

# Matching cpp/Makefile: /O2 /fp:fast is MSVC's nearest equivalent to
# -O3 -ffast-math, so the arithmetic stays as close as two compilers allow.
add_compile_options(/W4 /O2 /fp:fast /permissive-)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# ---------------------------------------------------------------- the effect
add_library(8DMusicAPO SHARED
    src/apo/EightDApo.cpp
    src/apo/DllMain.cpp
    src/shared/SharedState.cpp
    ${DSP_DIR}/Processor.cpp
    src/apo/EightDApo.def
)
target_include_directories(8DMusicAPO PRIVATE ${DSP_DIR} src)
target_link_libraries(8DMusicAPO PRIVATE ole32 oleaut32 uuid advapi32)
set_target_properties(8DMusicAPO PROPERTIES OUTPUT_NAME "8DMusicAPO")

# ---------------------------------------------------------------- the window
add_executable(8DMusic WIN32
    src/gui/Main.cpp
    src/shared/SharedState.cpp
)
target_include_directories(8DMusic PRIVATE ${DSP_DIR} src)
target_link_libraries(8DMusic PRIVATE comctl32 advapi32)
set_target_properties(8DMusic PROPERTIES OUTPUT_NAME "8DMusic")
