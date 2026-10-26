cmake_minimum_required(VERSION 3.22)
project(Toneical VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  Toneical – standalone guitar rig for Linux (JUCE + Neural Amp Modeler)
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
set(TONEICAL_FORMATS Standalone VST3 LV2 CACHE STRING "Plugin formats to build")
option(TONEICAL_BUILD_TESTS "Build the offline render test" ON)
option(TONEICAL_MINIMAL_X11 "Build without Xrandr/Xinerama/Xcursor/XInput headers (CI only)" OFF)
option(TONEICAL_NATIVE "Optimise for this CPU (-march=native)" ON)
option(TONEICAL_LTO "Link-time optimisation (slower builds, slightly smaller/faster binary)" OFF)
set(TONEICAL_LOGO_FONT "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Arizonia-Regular.ttf" CACHE FILEPATH
    "Font file used for the 'Toneical' logo (e.g. your licensed Cattalague .ttf/.otf)")

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
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4               # pinned: the version Toneical is tested with
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Eigen (header only, needed by NAM) -------------------------------------
if(NOT TONEICAL_EIGEN_DIR)
    # SOURCE_SUBDIR points nowhere on purpose: download only, don't add Eigen's own build
    FetchContent_Declare(eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  do-not-build)
    FetchContent_MakeAvailable(eigen)
    set(TONEICAL_EIGEN_DIR ${eigen_SOURCE_DIR})
endif()

# --- NeuralAmpModelerCore (MIT) --------------------------------------------
if(NOT TONEICAL_NAM_DIR)
    FetchContent_Declare(namcore
        GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
        GIT_TAG        v0.6.0              # pinned
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES ""            # we fetch Eigen ourselves; nlohmann/json is vendored
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
target_compile_definitions(namcore PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
set_target_properties(namcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(namcore PRIVATE -O3 -w)
if(TONEICAL_NATIVE)
    target_compile_options(namcore PRIVATE -march=native)
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
    Source/gui/PedalArt.cpp)

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
    VST3_CATEGORIES        Fx Distortion)

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
    JUCE_ALSA=1
    JUCE_JACK=1
    JUCE_MODAL_LOOPS_PERMITTED=0)
if(TONEICAL_MINIMAL_X11)
    list(APPEND TONEICAL_JUCE_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
endif()
target_compile_definitions(Toneical PUBLIC ${TONEICAL_JUCE_DEFS})

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

target_compile_options(Toneical PRIVATE -Wno-switch-enum)   # "default:" branches in switches over shapes are intentional
if(TONEICAL_NATIVE)
    target_compile_options(Toneical PRIVATE -march=native)
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
    target_compile_options(ToneicalRenderTest PRIVATE -Wno-switch-enum)
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
    target_compile_options(ToneicalRender PRIVATE -Wno-switch-enum)
    target_compile_definitions(ToneicalRender PRIVATE ${TONEICAL_JUCE_DEFS} JUCE_STANDALONE_APPLICATION=1 TONEICAL_TUNING=1)
    target_link_libraries(ToneicalRender PRIVATE
        namcore ToneicalAssets juce::juce_audio_formats juce::juce_dsp juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
