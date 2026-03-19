// PluginEditor.cpp
// Visual interface for RolyPolyFix.
// Layout (top → bottom):
//   Title
//   [Key dropdown]  [Scale dropdown]
//   [Strength knob] [Stabilization knob]
//   Note display: Detected | Target | Status

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Colour palette ────────────────────────────────────────────────────────────
static const juce::Colour BG       { 0xff1a1a2e };   // dark navy
static const juce::Colour PANEL    { 0xff16213e };   // slightly lighter navy
static const juce::Colour ACCENT   { 0xff0f3460 };   // blue accent
static const juce::Colour ACTIVE   { 0xff53d8fb };   // cyan — "correcting" indicator
static const juce::Colour IDLE     { 0xff4ecca3 };   // teal — "on target"
static const juce::Colour TEXT     { 0xffeaeaea };   // near-white

static void styleLabel (juce::Label& l, float size = 13.0f, bool bold = false)
{
    l.setFont (juce::Font (size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour (juce::Label::textColourId, TEXT);
    l.setJustificationType (juce::Justification::centred);
}

static void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId,  PANEL);
    c.setColour (juce::ComboBox::outlineColourId,     ACCENT);
    c.setColour (juce::ComboBox::textColourId,         TEXT);
    c.setColour (juce::ComboBox::arrowColourId,        IDLE);
}

static void styleSlider (juce::Slider& s, juce::Colour fill)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId,   fill);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, ACCENT);
    s.setColour (juce::Slider::thumbColourId,               fill.brighter (0.3f));
    s.setColour (juce::Slider::textBoxTextColourId,         TEXT);
    s.setColour (juce::Slider::textBoxBackgroundColourId,   PANEL);
    s.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
}

// ── Constructor ───────────────────────────────────────────────────────────────
RolyPolyFixAudioProcessorEditor::RolyPolyFixAudioProcessorEditor (RolyPolyFixAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // ── Key dropdown ──────────────────────────────────────────────────────
    keyBox.addItem ("C",  1); keyBox.addItem ("C#", 2); keyBox.addItem ("D",  3);
    keyBox.addItem ("D#", 4); keyBox.addItem ("E",  5); keyBox.addItem ("F",  6);
    keyBox.addItem ("F#", 7); keyBox.addItem ("G",  8); keyBox.addItem ("G#", 9);
    keyBox.addItem ("A", 10); keyBox.addItem ("A#",11); keyBox.addItem ("B", 12);
    styleCombo (keyBox);
    addAndMakeVisible (keyBox);

    keyLabel.setText ("Key", juce::dontSendNotification);
    styleLabel (keyLabel, 12.0f);
    addAndMakeVisible (keyLabel);

    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "key", keyBox);

    // ── Scale dropdown ────────────────────────────────────────────────────
    scaleBox.addItem ("Chromatic",       1);
    scaleBox.addItem ("Major",           2);
    scaleBox.addItem ("Minor",           3);
    scaleBox.addItem ("Pentatonic Major",4);
    styleCombo (scaleBox);
    addAndMakeVisible (scaleBox);

    scaleLabel.setText ("Scale", juce::dontSendNotification);
    styleLabel (scaleLabel, 12.0f);
    addAndMakeVisible (scaleLabel);

    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "scale", scaleBox);

    // ── Strength knob ─────────────────────────────────────────────────────
    styleSlider (strengthSlider, IDLE);
    addAndMakeVisible (strengthSlider);

    strengthLabel.setText ("Correction", juce::dontSendNotification);
    styleLabel (strengthLabel, 12.0f);
    addAndMakeVisible (strengthLabel);

    strengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "strength", strengthSlider);

    // ── Stabilization knob ────────────────────────────────────────────────
    styleSlider (stabilizeSlider, ACTIVE);
    addAndMakeVisible (stabilizeSlider);

    stabilizeLabel.setText ("Stabilize", juce::dontSendNotification);
    styleLabel (stabilizeLabel, 12.0f);
    addAndMakeVisible (stabilizeLabel);

    stabilizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "stabilize", stabilizeSlider);

    // ── Note display labels ───────────────────────────────────────────────
    detectedNoteLabel.setText ("--", juce::dontSendNotification);
    styleLabel (detectedNoteLabel, 22.0f, true);
    addAndMakeVisible (detectedNoteLabel);

    targetNoteLabel.setText ("--", juce::dontSendNotification);
    styleLabel (targetNoteLabel, 22.0f, true);
    targetNoteLabel.setColour (juce::Label::textColourId, IDLE);
    addAndMakeVisible (targetNoteLabel);

    statusLabel.setText ("No signal", juce::dontSendNotification);
    styleLabel (statusLabel, 11.0f);
    statusLabel.setColour (juce::Label::textColourId, TEXT.withAlpha (0.5f));
    addAndMakeVisible (statusLabel);

    setSize (380, 330);

    // Refresh the note display ~10 times per second
    startTimerHz (10);
}

RolyPolyFixAudioProcessorEditor::~RolyPolyFixAudioProcessorEditor()
{
    stopTimer();
}

// ── timerCallback: update the note readout from the audio thread ──────────────
void RolyPolyFixAudioProcessorEditor::timerCallback()
{
    int dn = processor.detectedNote.load();
    int tn = processor.targetNote.load();
    bool cor = processor.correcting.load();

    detectedNoteLabel.setText (
        RolyPolyFixAudioProcessor::midiNoteToName (dn),
        juce::dontSendNotification);

    targetNoteLabel.setText (
        RolyPolyFixAudioProcessor::midiNoteToName (tn),
        juce::dontSendNotification);

    if (dn < 0)
    {
        statusLabel.setText ("No signal", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, TEXT.withAlpha (0.4f));
    }
    else if (cor)
    {
        statusLabel.setText ("Correcting", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, ACTIVE);
    }
    else
    {
        statusLabel.setText ("On target", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, IDLE);
    }
}

// ── paint: draw the background and panel areas ────────────────────────────────
void RolyPolyFixAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (BG);

    // Title bar
    g.setColour (ACCENT);
    g.fillRect (0, 0, getWidth(), 36);

    g.setFont (juce::Font (18.0f, juce::Font::bold));
    g.setColour (TEXT);
    g.drawText ("RolyPoly Fix", 0, 0, getWidth(), 36, juce::Justification::centred);

    // Section label: Controls
    g.setFont (juce::Font (10.0f));
    g.setColour (TEXT.withAlpha (0.4f));
    g.drawText ("CONTROLS", 10, 42, 100, 14, juce::Justification::left);

    // Section label: Note monitor
    g.drawText ("DETECTED  →  TARGET", 0, 230, getWidth(), 14, juce::Justification::centred);
}

// ── resized: lay out all controls ────────────────────────────────────────────
void RolyPolyFixAudioProcessorEditor::resized()
{
    // ── Row 1: Key + Scale dropdowns ────────────────────────────────────
    int rowY  = 58;
    int rowH  = 22;
    int labelH = 14;
    int colW  = (getWidth() - 30) / 2;

    keyLabel .setBounds (10,       rowY,          colW, labelH);
    scaleLabel.setBounds (20+colW, rowY,          colW, labelH);
    keyBox   .setBounds (10,       rowY+labelH+2, colW, rowH);
    scaleBox .setBounds (20+colW,  rowY+labelH+2, colW, rowH);

    // ── Row 2: Strength + Stabilize knobs ───────────────────────────────
    int knobY    = 118;
    int knobSize = 90;
    int knobLH   = 16;
    int kx1 = (getWidth() / 2 - knobSize) / 2 + 10;
    int kx2 = getWidth() / 2 + kx1 - 10;

    strengthLabel .setBounds (kx1, knobY,              knobSize, knobLH);
    strengthSlider.setBounds (kx1, knobY + knobLH,     knobSize, knobSize);

    stabilizeLabel .setBounds (kx2, knobY,              knobSize, knobLH);
    stabilizeSlider.setBounds (kx2, knobY + knobLH,     knobSize, knobSize);

    // ── Note display ────────────────────────────────────────────────────
    int noteY  = 248;
    int noteW  = 100;
    int noteH  = 32;
    int midX   = getWidth() / 2;

    detectedNoteLabel.setBounds (midX - noteW - 10, noteY, noteW, noteH);
    targetNoteLabel  .setBounds (midX + 10,          noteY, noteW, noteH);

    statusLabel.setBounds (0, noteY + noteH + 4, getWidth(), 18);
}
