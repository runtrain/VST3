# RolyPoly Fix — VST3 Pitch Correction Plugin

Automatically fixes the "roly-poly" effect caused by hard autotune (retune speed 0).

When your autotune snaps a vocal to the **wrong** note, this plugin detects it and
silently shifts it to the nearest **correct** note in your chosen scale — in realtime,
without you having to do it manually in Melodyne.

Works with **Audacity 3.2+** realtime effects.

---

## How it works (plain English)

1. Your vocal comes in — already processed by your hard autotune
2. The plugin listens and detects which note is being sung
3. It checks: is this note in the scale you picked?
4. If **yes** → passes through untouched
5. If **no** → smoothly shifts the pitch to the nearest correct scale note
6. The **Stabilization** knob stops it from flickering between two notes (= no roly-polies)

---

## Controls

| Control | What it does |
|---------|-------------|
| **Key** | The root note of your song (e.g. C, F#, A) |
| **Scale** | Which notes are "correct" (Major, Minor, Pentatonic, or Chromatic for any key) |
| **Correction** | How hard it corrects. 0% = off, 80% = natural-sounding, 100% = fully snapped |
| **Stabilize** | How many times a note must be detected before the plugin locks onto it. Higher = less roly-poly, but slower to track fast runs |

### Note Display (bottom of plugin)
- **Left number**: what note the plugin is currently hearing
- **Right number**: what it's correcting to
- **"Correcting"** (cyan): a correction is being applied right now
- **"On target"** (teal): the note is already correct, nothing changed
- **"No signal"**: silence detected

---

## Suggested starting settings

For most vocal correction use cases:
- **Key**: match your beat's key
- **Scale**: Major or Minor (whichever your beat uses)
- **Correction**: 75–85%
- **Stabilize**: 3–5

If you still get occasional roly-polies → increase **Stabilize**
If the plugin is too slow to track fast vocal runs → decrease **Stabilize**

---

## How to Build

### Step 1: Install tools

| Tool | Download |
|------|----------|
| **Git** | https://git-scm.com |
| **CMake 3.22+** | https://cmake.org/download |
| **C++ compiler** | Windows: [Visual Studio 2022](https://visualstudio.microsoft.com/) (check "Desktop dev with C++") · macOS: `xcode-select --install` · Linux: `sudo apt install build-essential` |

### Step 2: Build

Open a terminal in this folder:

```bash
# Configure — downloads JUCE and SoundTouch automatically (~5 min first time)
cmake -B build

# Compile
cmake --build build --config Release
```

### Step 3: Install

Copy the `.vst3` file to your DAW's plugin folder:

| OS | Built file location | Install to |
|----|---------------------|------------|
| Windows | `build\RolyPolyFix_artefacts\Release\VST3\RolyPolyFix.vst3` | `C:\Program Files\Common Files\VST3\` |
| macOS | `build/RolyPolyFix_artefacts/Release/VST3/RolyPolyFix.vst3` | `/Library/Audio/Plug-Ins/VST3/` |
| Linux | `build/RolyPolyFix_artefacts/Release/VST3/RolyPolyFix.vst3` | `~/.vst3/` |

### Step 4: Use in Audacity

1. Open Audacity
2. Go to **Effects → Add / Remove Plug-ins**
3. Scan for new VST3 plugins — RolyPoly Fix should appear
4. Select a track, go to **Effects → Realtime Effects**
5. Add **RolyPoly Fix** from the list
6. Press play — the plugin works in realtime

---

## Project structure

```
VST3/
├── CMakeLists.txt              ← Build script
├── README.md                   ← This file
└── Source/
    ├── PitchDetector.h         ← YIN pitch detection algorithm
    ├── PluginProcessor.h/cpp   ← Audio processing & pitch correction logic
    └── PluginEditor.h/cpp      ← Plugin UI
```

---

## Troubleshooting

**"The plugin doesn't appear in Audacity"**
→ Make sure the `.vst3` file is in the correct folder and you've re-scanned.
→ Audacity must be version 3.2 or newer for VST3 support.

**"I still hear roly-polies"**
→ Increase the **Stabilize** knob (try 6–8).
→ Make sure you set the correct Key and Scale for your song.

**"The vocals sound slightly off-pitch"**
→ Lower the **Correction** knob to around 60–70%.
→ Double-check that your Key/Scale settings match the beat.

**"Build fails with errors about SoundTouch"**
→ Make sure you have an internet connection when running `cmake -B build` (it downloads SoundTouch).
→ On Windows, open the `.sln` file in `build/` with Visual Studio and build from there.
