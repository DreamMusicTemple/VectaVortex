// Wavetable authoring (EDIT HARMONICS / FORMULA / DRAW WAVEFORM / IMPORT AUDIO) and the sampler slot tools (IMPORT / STRETCH / WARP / SPECTRAL).
// The DSP lives in Core (wt_tools.hpp, sample_tools.hpp) behind the C ABI; this file is the web app's dialogs on top of it.
// Results are stored in the preset like the web does: customWavetables entries ("custom:<id>") and sampleLibrary entries (WAV, base64).
#pragma once
#include "Widgets.h"

namespace vv { namespace authoring {

struct TableList { juce::StringArray ids, labels; };
TableList tableChoices(Store& s);                       // built-in wavetables + the preset's custom tables
void openHarmonics(Store& s, const juce::String& oscPath, juce::Component& anchor, std::function<void()> changed);
void openFormula(Store& s, const juce::String& oscPath, juce::Component& anchor, std::function<void()> changed);
void openDraw(Store& s, const juce::String& oscPath, juce::Component& anchor, std::function<void()> changed);
void importWavetableAudio(Store& s, const juce::String& oscPath, std::function<void()> changed);
void openRecord(Store& s, int slot, juce::Component& anchor, std::function<void()> changed);
void openImageAudio(Store& s, int slot, juce::Component& anchor, std::function<void()> changed);
void openSampleTools(Store& s, int slot, juce::Component& anchor, std::function<void()> changed);

// the dialog components themselves (the open* functions show these in a call-out); exposed so tests can drive and snapshot them
std::unique_ptr<juce::Component> makeHarmonics(Store&, const juce::String& oscPath, std::function<void()> changed);
std::unique_ptr<juce::Component> makeFormula(Store&, const juce::String& oscPath, std::function<void()> changed);
std::unique_ptr<juce::Component> makeDraw(Store&, const juce::String& oscPath, std::function<void()> changed);
std::unique_ptr<juce::Component> makeRecord(Store&, int slot, std::function<void()> changed);
std::unique_ptr<juce::Component> makeImageAudio(Store&, int slot, std::function<void()> changed);
std::unique_ptr<juce::Component> makeSampleTools(Store&, int slot, std::function<void()> changed);

// shared helpers (also used by the tests)
void putCustomTable(Store& s, const juce::String& oscPath, const juce::String& idKey, const juce::String& name, const std::vector<std::vector<float>>& frames);
juce::MemoryBlock sampleWav(Store& s, const juce::String& name);                       // bytes of a library sample (embedded or built-in), empty if unknown
void putSample(Store& s, const juce::String& name, const juce::MemoryBlock& wav);     // add / replace an entry of the preset's sampleLibrary and register it with the engine
juce::String uniqueSampleName(Store& s, const juce::String& base);

}}
