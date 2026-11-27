#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <thread>
#include "Store.h"

namespace vv {

class HostParam;

class Processor : public juce::AudioProcessor, private juce::Timer {
public:
    Processor();
    ~Processor() override;

    // ---- AudioProcessor
    const juce::String getName() const override { return "Vektur Vortex"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- shared with the editor
    VVEngine* engine() const { return engine_; }
    Store& store() { return store_; }
    juce::MidiKeyboardState keyboardState;
    bool assetsReady() const { return assetsReady_.load(); }
    juce::File userPresetDir() const;
    juce::Array<juce::AudioParameterFloat*> hostParams;
    std::vector<unsigned> hostIds;
    juce::AudioParameterFloat* paramByAddress(const juce::String& address) const;
    // scope tap (audio thread writes, UI reads)
    static constexpr int kScope = 4096;
    float scopeL[kScope] = {}, scopeR[kScope] = {};
    std::atomic<int> scopeWrite{ 0 };
    // Scope power: bit 0 = the Scope tab is switched ON, bit 1 = the Spectrogram tab is showing. While 0 the audio thread does not write the tap at all.
    std::atomic<int> scopeWantMask{ 0 };
    // "Send to Vektur Sonic": keep Left and Right as two strictly separate mono signals on the stereo output so Vektur Sonic
    // (loaded as an insert after this synth in a host such as AUM) can use them as its left / right inputs.
    std::atomic<bool> sendToVectaSonic{ false };
    // UI state that is saved with the host session (so project recall restores the view as well as the sound)
    std::atomic<int> lastNoteOn{ -1 };   // most recent MIDI note-on (keyboard or host): used by the zone LEARN buttons
    std::atomic<int> scopeMode{ 0 }, uiTab{ 0 }, uiWidth{ 1100 }, uiHeight{ 860 };   // scope: 0 wave / 1 X-Y
    std::atomic<bool> outputIsMono{ false };    // host gave us a mono output bus (cannot carry two signals)
    // test hook: block until assets + the starting preset are loaded (pumps the message loop)
    void waitUntilLoaded(int timeoutMs = 20000);

    // called by HostParam (any thread)
    void hostParamChanged(unsigned id, float value);

private:
    void timerCallback() override;
    void loadAssetsThread();
    void applyPendingOrDefault();                // message thread
    void refreshHostParams();                    // preset -> host parameters (no echo)

    VVEngine* engine_ = nullptr;
    Store store_;
    std::thread loader_;
    std::atomic<bool> assetsReady_{ false }, presetApplied_{ false };
    juce::String pendingState_;
    std::atomic<int> suppress_{ 0 };
    double lastBpm_ = -1; int lastPlaying_ = -1;
    int editTicks_ = 0, reloadTicks_ = 0;
    double sampleRate_ = 48000;
    juce::AudioBuffer<float> scratch;
    friend class HostParam;
};

} // namespace vv
