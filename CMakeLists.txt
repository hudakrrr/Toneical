cmake_minimum_required(VERSION 3.22)
project(Toneical VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  Toneical – standalone guitar rig for Linux and Windows (JUCE + Neural Amp Modeler)
#
#  Windows: build with Visual Studio 2022 (or packaging/windows/build.ps1, which also
#  makes the installer). Optional ASIO: -DTONEICAL_ASIO_SDK_DIR=C:/path/to/asiosdk
#
#  Dependencies are downloaded automatically (FetchContent) unless you point
#  CMake at local copies:
#     -DTONEICAL_JUCE_DIR=/path/to/JUCE
#     -DTONEICAL_NAM_DIR=/path/to/NeuralAmpModelerCore
#     -DTONEICAL_EIGEN_DIR=/path/to/eigen
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(TONEICAL_JUCE_DIR  "" CACHE PATH "Local JUCE checkout (empty = download)")
set(TONEICAL_NAM_DIR   "" CACHE PATH "Local NeuralAmpModelerCore checkout (empty = download)")
set(TONEICAL_EIGEN_DIR "" CACHE PATH "Local Eigen checkout (empty = download)")
if(WIN32)
    set(TONEICAL_FORMATS Standalone VST3 CACHE STRING "Plugin formats to build")
else()
    set(TONEICAL_FORMATS Standalone VST3 LV2 CACHE STRING "Plugin formats to build")
endif()
option(TONEICAL_BUILD_TESTS "Build the offline render test" ON)
option(TONEICAL_MINIMAL_X11 "Build without Xrandr/Xinerama/Xcursor/XInput headers (CI only)" OFF)
# Linux builds are made on the machine they run on; a Windows installer must run on any x64 CPU.
if(WIN32)
    option(TONEICAL_NATIVE "Optimise for this CPU (/arch:AVX2 on MSVC, -march=native elsewhere)" OFF)
else()
    option(TONEICAL_NATIVE "Optimise for this CPU (-march=native)" ON)
endif()
set(TONEICAL_ASIO_SDK_DIR "" CACHE PATH "Windows: Steinberg ASIO SDK folder - enables ASIO drivers (empty = WASAPI/DirectSound only)")
option(TONEICAL_LTO "Link-time optimisation (slower builds, slightly smaller/faster binary)" OFF)
set(TONEICAL_LOGO_FONT "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Arizonia-Regular.ttf" CACHE FILEPATH
    "Font file used for the 'Toneical' logo (e.g. your licensed Cattalague .ttf/.otf)")

# MSVC: link the C++ runtime statically, so Toneical.exe needs no Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# compiler-specific flags used below
if(MSVC)
    set(TONEICAL_OPT_FLAGS /O2 /bigobj /utf-8)
    set(TONEICAL_WARN_FLAGS /utf-8 /bigobj)
    set(TONEICAL_ARCH_FLAGS /arch:AVX2)
else()
    set(TONEICAL_OPT_FLAGS -O3)
    set(TONEICAL_WARN_FLAGS -Wno-switch-enum)   # "default:" branches in switches over shapes are intentional
    set(TONEICAL_ARCH_FLAGS -march=native)
endif()
if(WIN32)
    set(TONEICAL_PLATFORM_DEFS _USE_MATH_DEFINES NOMINMAX)   # M_PI on MSVC; no min/max macros from windows.h
endif()

include(FetchContent)
# Downloaded dependencies are fetched once; re-configuring an existing build (updates)
# must not hit the network again.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't re-download dependencies on every configure")

# Use ccache when it's installed - repeated builds of unchanged files become instant.
find_program(TONEICAL_CCACHE ccache)
if(TONEICAL_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER   ${TONEICAL_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${TONEICAL_CCACHE})
endif()

# --- JUCE ------------------------------------------------------------------
if(TONEICAL_JUCE_DIR)
    add_subdirectory(${TONEICAL_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    # pinned: the version Toneical is tested with. Archives (not git) so no Git is needed to build.
    FetchContent_Declare(JUCE
        URL      https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
        URL_HASH SHA256=e2bee25d5c776118aea550ad168a3dc9520c0669a79bd9f5a72b9f404ee1c40a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Eigen (header only, needed by NAM) -------------------------------------
if(NOT TONEICAL_EIGEN_DIR)
    # SOURCE_SUBDIR points nowhere on purpose: download only, don't add Eigen's own build
    FetchContent_Declare(eigen
        URL      https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
        URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR  do-not-build)
    FetchContent_MakeAvailable(eigen)
    set(TONEICAL_EIGEN_DIR ${eigen_SOURCE_DIR})
endif()

# --- NeuralAmpModelerCore (MIT) --------------------------------------------
if(NOT TONEICAL_NAM_DIR)
    FetchContent_Declare(namcore           # we fetch Eigen ourselves; nlohmann/json is vendored
        URL      https://github.com/sdatkinson/NeuralAmpModelerCore/archive/refs/tags/v0.6.0.tar.gz
        URL_HASH SHA256=d55f3c49278a06e5bf5978ea60ae8384603b843d7fdee22a0e9ba71c95108450
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR  do-not-build)
    FetchContent_MakeAvailable(namcore)
    set(TONEICAL_NAM_DIR ${namcore_SOURCE_DIR})
endif()

file(GLOB NAM_SOURCES "${TONEICAL_NAM_DIR}/NAM/*.cpp" "${TONEICAL_NAM_DIR}/NAM/*/*.cpp")
add_library(namcore STATIC ${NAM_SOURCES})
target_include_directories(namcore SYSTEM PUBLIC
    ${TONEICAL_NAM_DIR}
    ${TONEICAL_EIGEN_DIR}
    ${TONEICAL_NAM_DIR}/Dependencies/nlohmann)
target_compile_definitions(namcore PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST ${TONEICAL_PLATFORM_DEFS})
set_target_properties(namcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(namcore PRIVATE ${TONEICAL_OPT_FLAGS} $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
if(TONEICAL_NATIVE)
    target_compile_options(namcore PRIVATE ${TONEICAL_ARCH_FLAGS})
endif()

# --- Shared DSP / GUI sources -----------------------------------------------
set(TONEICAL_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Chain.cpp
    Source/modules/Module.cpp
    Source/modules/ModuleFactory.cpp
    Source/modules/NamModule.cpp
    Source/modules/CapturedAmp.cpp
    Source/modules/CapturedPedals.cpp
    Source/Presets.cpp
    Source/gui/PedalComponent.cpp
    Source/gui/ChainStrip.cpp
    Source/gui/GearBrowser.cpp
    Source/gui/PedalArt.cpp
    Source/gui/TransportBar.cpp
    Source/gui/TrackView.cpp
    Source/recorder/Session.cpp)

juce_add_plugin(Toneical
    PRODUCT_NAME           "Toneical"
    COMPANY_NAME           "komoj"
    BUNDLE_ID              hu.komoj.toneical
    PLUGIN_MANUFACTURER_CODE Komj
    PLUGIN_CODE            Tncl
    FORMATS                ${TONEICAL_FORMATS}
    IS_SYNTH               FALSE
    NEEDS_MIDI_INPUT       FALSE
    NEEDS_MIDI_OUTPUT      FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    LV2URI                 "https://komoj.hu/toneical"
    VST3_CATEGORIES        Fx Distortion
    ICON_BIG               "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/toneical-256.png"
    ICON_SMALL             "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/toneical-32.png")

target_sources(Toneical PRIVATE ${TONEICAL_SOURCES})

# Logo font, embedded into the binary. Copied to a fixed name so the code can always
# refer to BinaryData::LogoFont_ttf, whatever font file you choose.
if(NOT EXISTS "${TONEICAL_LOGO_FONT}")
    message(FATAL_ERROR "TONEICAL_LOGO_FONT not found: ${TONEICAL_LOGO_FONT}")
endif()
configure_file("${TONEICAL_LOGO_FONT}" "${CMAKE_BINARY_DIR}/assets/LogoFont.ttf" COPYONLY)
file(GLOB TONEICAL_CAB_IRS "${CMAKE_CURRENT_SOURCE_DIR}/assets/irs/*.wav")
file(GLOB TONEICAL_NAM_CAPTURES "${CMAKE_CURRENT_SOURCE_DIR}/assets/nam/*.nam" "${CMAKE_CURRENT_SOURCE_DIR}/assets/pedals/*.nam" "${CMAKE_CURRENT_SOURCE_DIR}/assets/pedals/*.json")
juce_add_binary_data(ToneicalAssets SOURCES "${CMAKE_BINARY_DIR}/assets/LogoFont.ttf" ${TONEICAL_CAB_IRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory.json" ${TONEICAL_NAM_CAPTURES})
set_target_properties(ToneicalAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(Toneical PRIVATE Source)

set(TONEICAL_JUCE_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    ${TONEICAL_PLATFORM_DEFS})
if(WIN32)
    list(APPEND TONEICAL_JUCE_DEFS JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
    if(TONEICAL_ASIO_SDK_DIR)
        if(NOT EXISTS "${TONEICAL_ASIO_SDK_DIR}/common/iasiodrv.h")
            message(FATAL_ERROR "TONEICAL_ASIO_SDK_DIR: no common/iasiodrv.h in ${TONEICAL_ASIO_SDK_DIR}")
        endif()
        list(APPEND TONEICAL_JUCE_DEFS JUCE_ASIO=1)
        message(STATUS "Toneical: ASIO enabled (${TONEICAL_ASIO_SDK_DIR})")
    endif()
else()
    list(APPEND TONEICAL_JUCE_DEFS JUCE_ALSA=1 JUCE_JACK=1)
endif()
if(TONEICAL_MINIMAL_X11)
    list(APPEND TONEICAL_JUCE_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
endif()
target_compile_definitions(Toneical PUBLIC ${TONEICAL_JUCE_DEFS})
if(WIN32 AND TONEICAL_ASIO_SDK_DIR)
    target_include_directories(Toneical PRIVATE "${TONEICAL_ASIO_SDK_DIR}/common")
endif()

target_link_libraries(Toneical
    PRIVATE
        namcore
        ToneicalAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(TONEICAL_LTO)
    target_link_libraries(Toneical PUBLIC juce::juce_recommended_lto_flags)
endif()

target_compile_options(Toneical PRIVATE ${TONEICAL_WARN_FLAGS})
if(TONEICAL_NATIVE)
    target_compile_options(Toneical PRIVATE ${TONEICAL_ARCH_FLAGS})
endif()

# --- Offline test: renders a synthetic guitar through every module ----------
if(TONEICAL_BUILD_TESTS)
    juce_add_console_app(ToneicalRenderTest PRODUCT_NAME "ToneicalRenderTest")
    target_sources(ToneicalRenderTest PRIVATE
        tests/RenderTest.cpp
        Source/Chain.cpp
        Source/modules/Module.cpp
        Source/modules/ModuleFactory.cpp
        Source/modules/NamModule.cpp
        Source/modules/CapturedAmp.cpp
        Source/modules/CapturedPedals.cpp
        Source/Presets.cpp)
    target_include_directories(ToneicalRenderTest PRIVATE Source)
    target_compile_options(ToneicalRenderTest PRIVATE ${TONEICAL_WARN_FLAGS})
    target_compile_definitions(ToneicalRenderTest PRIVATE ${TONEICAL_JUCE_DEFS} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(ToneicalRenderTest PRIVATE
        namcore ToneicalAssets juce::juce_audio_formats juce::juce_dsp juce::juce_audio_utils
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # ToneicalRender: run a WAV through presets/modules/NAM captures (for tuning the DSP)
    juce_add_console_app(ToneicalRender PRODUCT_NAME "ToneicalRender")
    target_sources(ToneicalRender PRIVATE
        tests/RenderCli.cpp
        Source/Chain.cpp
        Source/modules/Module.cpp
        Source/modules/ModuleFactory.cpp
        Source/modules/NamModule.cpp
        Source/modules/CapturedAmp.cpp
        Source/modules/CapturedPedals.cpp
        Source/Presets.cpp)
    target_include_directories(ToneicalRender PRIVATE Source)
    target_compile_options(ToneicalRender PRIVATE ${TONEICAL_WARN_FLAGS})
    target_compile_definitions(ToneicalRender PRIVATE ${TONEICAL_JUCE_DEFS} JUCE_STANDALONE_APPLICATION=1 TONEICAL_TUNING=1)
    target_link_libraries(ToneicalRender PRIVATE
        namcore ToneicalAssets juce::juce_audio_formats juce::juce_dsp juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
