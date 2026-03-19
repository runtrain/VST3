cmake_minimum_required(VERSION 3.22)
project(RolyPolyFix VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

# ── Download JUCE ─────────────────────────────────────────────────────────────
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Download SoundTouch (pitch shifting library) ──────────────────────────────
# SoundTouch handles the math of shifting pitch without changing tempo.
FetchContent_Declare(
    soundtouch_src
    GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
    GIT_TAG        2.3.3
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(soundtouch_src)
if(NOT soundtouch_src_POPULATED)
    FetchContent_Populate(soundtouch_src)
endif()

# Compile SoundTouch sources directly (bypasses their CMake for reliability)
file(GLOB SOUNDTOUCH_SOURCES
    "${soundtouch_src_SOURCE_DIR}/source/SoundTouch/*.cpp"
)
add_library(SoundTouchLib STATIC ${SOUNDTOUCH_SOURCES})
target_include_directories(SoundTouchLib PUBLIC
    "${soundtouch_src_SOURCE_DIR}/include"
    "${soundtouch_src_SOURCE_DIR}/source/SoundTouch"
)
# SOUNDTOUCH_FLOAT_SAMPLES: use float (matches JUCE's audio format)
# SOUNDTOUCH_PREVENT_CLICK_AT_PITCH_CHANGE: smoother transitions
# SOUNDTOUCH_ALLOW_X86_OPTIMIZATIONS=0: disables SSE/MMX so we don't need
#   sse_optimized.cpp / mmx_optimized.cpp (avoids LNK2001 unresolved symbols)
target_compile_definitions(SoundTouchLib PUBLIC
    SOUNDTOUCH_FLOAT_SAMPLES=1
    SOUNDTOUCH_PREVENT_CLICK_AT_PITCH_CHANGE=1
)
# Suppress third-party warnings so your build output stays clean
if(MSVC)
    target_compile_options(SoundTouchLib PRIVATE /W0)
else()
    target_compile_options(SoundTouchLib PRIVATE -w)
endif()

# ── Define your plugin ────────────────────────────────────────────────────────
juce_add_plugin(RolyPolyFix
    PLUGIN_NAME         "RolyPoly Fix"
    COMPANY_NAME        "MyStudio"
    PLUGIN_MANUFACTURER_CODE "MySt"
    PLUGIN_CODE         "RPFx"

    # Build as VST3 (for Audacity) AND as a standalone app for testing
    FORMATS             VST3 Standalone

    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    NEEDS_MIDI_OUTPUT   FALSE
    IS_MIDI_EFFECT      FALSE

    PRODUCT_NAME        "RolyPolyFix"
)

target_sources(RolyPolyFix PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# This generates JuceHeader.h, which our source files include.
# Without this line you get "Cannot open include file: 'JuceHeader.h'".
juce_generate_juce_header(RolyPolyFix)

# Disable VST2 compatibility — the VST2 SDK is no longer publicly available.
# Without this, JUCE tries to include VST2 headers and the build fails.
target_compile_definitions(RolyPolyFix PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(RolyPolyFix PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    SoundTouchLib
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
