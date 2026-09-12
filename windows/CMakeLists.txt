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

# /fp:precise, NOT /fp:fast -- and this was measured, not assumed.
#
# The obvious choice is /fp:fast, on the reasoning that it is MSVC's answer to
# GCC's -ffast-math. Running the shared parity probe both ways says otherwise:
#
#   GCC -O3 -ffast-math  vs  MSVC /O2 /fp:fast      corr 0.999871  max diff 0.008558
#   GCC -O3 -ffast-math  vs  MSVC /O2 /fp:precise   corr 1.000000  max diff 0.000006
#
# The two flags are not equivalent in effect. GCC's -ffast-math turns out to
# change nothing measurable here -- GCC with and without it agrees to 0.000006 --
# while MSVC's /fp:fast diverges by about 1500 times that. The error is largest
# during the opening transient and then steady, so it is a systematic difference
# in the arithmetic rather than feedback slowly pulling the two apart.
#
# 0.000006 is the bar Android hit against the desktop build, and /fp:precise is
# what reaches it. It costs almost nothing: 0.62% -> 0.64% of realtime on the
# probe, against a block budget the effect uses one eightieth of.
#
# If you are tempted to put /fp:fast back for speed, re-run windows/parity.ps1
# first and look at what it costs.
add_compile_options(/W4 /O2 /fp:precise /permissive-)

# The sources are UTF-8. Without this MSVC reads them in the system codepage,
# and every degree sign and middot in the window renders as mojibake ("0Â°").
add_compile_options(/utf-8)

# Static CRT.
#
# The APO is loaded into audiodg.exe, a restricted process in session 0 that we
# do not control. Linking the dynamic CRT makes the DLL depend on
# VCRUNTIME140.dll, VCRUNTIME140_1.dll and the UCRT forwarders; every APO that
# actually loads on this machine (Realtek's, Microsoft's) imports nothing but
# system DLLs and audioeng.dll.
#
# It also removes a shipping problem: with the dynamic CRT, users would need the
# Visual C++ redistributable installed before the effect could load at all, and
# the failure would look like "8D Music does nothing" rather than a missing
# dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# M_PI is not in the C++ standard; MSVC only defines it with this.
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)

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

# ------------------------------------------------------------ the path probe
# No DSP, no shared memory: just enough to answer "can our DLL get into the
# audio path at all". The brief insists this is proven before the effect is
# wired in, because a do-nothing APO that cannot load makes all DSP work moot.
add_library(8DMusicPassthrough SHARED
    src/apo/PassthroughApo.cpp
    src/apo/PassthroughApo.def
)
# audioeng provides AERT_Allocate/Free; audiobaseprocessingobject provides
# CBaseAudioProcessingObject itself. Both are needed.
target_link_libraries(8DMusicPassthrough PRIVATE ole32 oleaut32 uuid advapi32
    audioeng audiobaseprocessingobject audiomediatypecrt)
set_target_properties(8DMusicPassthrough PROPERTIES OUTPUT_NAME "8DMusicPassthrough")

# ---------------------------------------------------------------- the window
add_executable(8DMusic WIN32
    src/gui/Main.cpp
    src/gui/NowPlaying.cpp
    src/shared/SharedState.cpp
    res/8dmusic.rc
)
target_include_directories(8DMusic PRIVATE ${DSP_DIR} src)
# WindowsApp.lib is the umbrella import library for WinRT; the now-playing
# panel reads GlobalSystemMediaTransportControlsSessionManager through it.
target_link_libraries(8DMusic PRIVATE comctl32 advapi32 ole32 oleaut32 uuid
    user32 gdi32 shell32 gdiplus WindowsApp)
set_target_properties(8DMusic PROPERTIES OUTPUT_NAME "8DMusic")

# ----------------------------------------------------------------- the probe
# The parity probe is the same source file the Linux and Android builds use.
# Compiled in place from cpp/tests, for the same reason the DSP is: a Windows
# copy of the probe would let the two drift and still report a pass.
set(PROBE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/tests/dsp_probe.cpp)
if(NOT EXISTS ${PROBE_SRC})
    message(FATAL_ERROR "Cannot find the shared parity probe at ${PROBE_SRC}.")
endif()

add_executable(dsp_probe ${PROBE_SRC} ${DSP_DIR}/Processor.cpp)
target_include_directories(dsp_probe PRIVATE ${DSP_DIR})
set_target_properties(dsp_probe PROPERTIES OUTPUT_NAME "dsp_probe")

# ------------------------------------------------------------------ test tool
# Opens a stream on one chosen endpoint so an APO registered against it is
# actually instantiated -- without changing the machine's default output.
add_executable(play_to_endpoint tools/play_to_endpoint.cpp)
target_link_libraries(play_to_endpoint PRIVATE ole32 oleaut32 uuid winmm)
set_target_properties(play_to_endpoint PROPERTIES OUTPUT_NAME "play_to_endpoint")

# --------------------------------------------------------------- listening tool
# Plays a file through the shared DSP so the effect can actually be heard on
# Windows while the APO route is still blocked. Uses the same Processor the APO
# does, compiled in place.
add_executable(8dplay tools/8dplay.cpp ${DSP_DIR}/Processor.cpp)
target_include_directories(8dplay PRIVATE ${DSP_DIR})
target_link_libraries(8dplay PRIVATE ole32 oleaut32 uuid)
set_target_properties(8dplay PROPERTIES OUTPUT_NAME "8dplay")
