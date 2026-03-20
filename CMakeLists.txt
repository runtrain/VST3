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
# Disable optional JUCE features that require unavailable system libraries on this build machine.
# These are only needed for the Standalone format (ALSA audio I/O, libcurl network, WebKit UI).
# The VST3 plugin build does not use any of them.
target_compile_definitions(RolyPolyFix PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_ALSA=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_link_libraries(RolyPolyFix PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
