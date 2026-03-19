# CMakeLists.txt — the build script for your plugin
# CMake reads this file to know how to compile your C++ code into a .vst3 file

cmake_minimum_required(VERSION 3.22)

# The name of your project (change this to whatever you like)
project(SimpleGain VERSION 1.0.0)

# Use C++17 (a modern version of C++)
set(CMAKE_CXX_STANDARD 17)

# ── Download JUCE automatically ───────────────────────────────────────────────
# FetchContent downloads JUCE from GitHub the first time you build.
# You don't need to install it manually.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12   # a stable release of JUCE
    GIT_SHALLOW    TRUE     # faster download (no full history)
)
FetchContent_MakeAvailable(JUCE)

# ── Define your plugin ────────────────────────────────────────────────────────
juce_add_plugin(SimpleGain

    # Basic info shown in your DAW
    PLUGIN_NAME         "Simple Gain"
    COMPANY_NAME        "MyCompany"
    PLUGIN_MANUFACTURER_CODE "MyCo"   # 4-character ID for your company
    PLUGIN_CODE         "SGai"        # 4-character ID for this plugin

    # Plugin type: we want VST3 (and also standalone so you can run it without a DAW)
    FORMATS             VST3 Standalone

    # This is an audio effect (not an instrument)
    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    NEEDS_MIDI_OUTPUT   FALSE
    IS_MIDI_EFFECT      FALSE

    # Where your source files live
    PRODUCT_NAME        "SimpleGain"
)

# ── Tell CMake which source files to compile ──────────────────────────────────
target_sources(SimpleGain PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# ── Link JUCE modules your plugin uses ───────────────────────────────────────
target_link_libraries(SimpleGain PRIVATE
    juce::juce_audio_utils      # Audio processing utilities
    juce::juce_audio_plugin_client  # VST3/plugin wrapper code
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
