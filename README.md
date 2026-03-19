# Simple Gain — VST3 Plugin

A beginner-friendly VST3 audio effect plugin built with JUCE.
It has a single **Gain** knob that controls the volume of incoming audio.

---

## What's in this project?

```
VST3/
├── CMakeLists.txt          ← Build script (tells CMake how to compile your plugin)
└── Source/
    ├── PluginProcessor.h   ← Declares the audio processing class
    ├── PluginProcessor.cpp ← Audio processing logic (where sound gets modified)
    ├── PluginEditor.h      ← Declares the UI class
    └── PluginEditor.cpp    ← UI layout and drawing code
```

---

## How to Build (Step by Step)

### 1. Install the required tools

You need three things installed on your computer:

| Tool | What it does | Download |
|------|-------------|----------|
| **Git** | Downloads JUCE automatically | https://git-scm.com |
| **CMake** (3.22+) | Builds the project | https://cmake.org/download |
| **A C++ compiler** | Compiles the code | See below |

**Compiler by platform:**
- **Windows**: Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community is free) — check "Desktop development with C++"
- **macOS**: Run `xcode-select --install` in Terminal
- **Linux**: Run `sudo apt install build-essential` (Ubuntu/Debian)

### 2. Build the plugin

Open a Terminal (or Command Prompt on Windows) in this folder and run:

```bash
# Step 1: Configure the project (downloads JUCE automatically — takes a few minutes)
cmake -B build

# Step 2: Compile the plugin
cmake --build build --config Release
```

### 3. Find your built plugin

After building, find your `.vst3` file here:

- **Windows**: `build\SimpleGain_artefacts\Release\VST3\SimpleGain.vst3`
- **macOS**: `build/SimpleGain_artefacts/Release/VST3/SimpleGain.vst3`
- **Linux**: `build/SimpleGain_artefacts/Release/VST3/SimpleGain.vst3`

Copy it to your DAW's VST3 folder:

- **Windows**: `C:\Program Files\Common Files\VST3\`
- **macOS**: `/Library/Audio/Plug-Ins/VST3/`
- **Linux**: `~/.vst3/`

Then rescan plugins in your DAW — Simple Gain will appear!

---

## How to Customize

### Change the gain range
In `Source/PluginProcessor.cpp`, find:
```cpp
juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f)
```
Change `2.0f` to a higher number for more gain (e.g. `4.0f` for up to 4x volume).

### Change the UI colour
In `Source/PluginEditor.cpp`, find:
```cpp
gainSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colours::cornflowerblue);
```
Replace `cornflowerblue` with any colour name from the [JUCE colour list](https://docs.juce.com/master/classColours.html).

### Add a new parameter (e.g. a Pan knob)
1. In `PluginProcessor.cpp` → `createParameterLayout()`, add a new `AudioParameterFloat`
2. In `PluginEditor.h`, add a new `juce::Slider` and `SliderAttachment`
3. In `PluginEditor.cpp`, set up and position the new slider

---

## Learning Resources

- [JUCE Tutorials](https://juce.com/learn/tutorials) — official beginner guides
- [The Audio Programmer (YouTube)](https://www.youtube.com/@TheAudioProgrammer) — excellent video tutorials
- [JUCE Forum](https://forum.juce.com) — friendly community for questions
