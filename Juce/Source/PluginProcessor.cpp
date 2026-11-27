#include "PluginProcessor.h"
#include "PowerSettings.h"
#include "PluginEditor.h"
#include "Assets.h"
#include "vv/params_generated.hpp"

namespace vv {

// A host-visible parameter that forwards value changes (from any thread) to the engine.
class HostParam : public juce::AudioParameterFloat {
public:
    HostParam(Processor& p, const vv::ParamInfo& pi)
        : juce::AudioParameterFloat(juce::ParameterID{ pi.address, 1 }, pi.label,
              juce::NormalisableRange<float>((float) pi.minValue, (float) pi.maxValue, pi.steps > 0 ? (float) ((pi.maxValue - pi.minValue) / pi.steps) : 0.f),
              (float) pi.defaultValue, juce::AudioParameterFloatAttributes().withLabel(pi.unit ? pi.unit : "")),
          owner(p), id(pi.id) {}
protected:
    void valueChanged(float v) override { owner.hostParamChanged(id, v); }
private:
    Processor& owner; unsigned id;
};

Processor::Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true).withInput("Vocoder / Sidechain In", juce::AudioChannelSet::stereo(), false)),
      engine_(vv_engine_create(48000)), store_(engine_) {
    std::map<juce::String, juce::AudioProcessorParameterGroup*> groups;
    juce::AudioProcessorParameterGroup tree;
    std::map<juce::String, std::unique_ptr<juce::AudioProcessorParameterGroup>> gm; juce::StringArray order;
    for (int i = 0; i < vv::kParamCount; ++i) {
        const auto& pi = vv::kParams[i]; juce::String g(pi.group);
        if (gm.find(g) == gm.end()) { gm[g] = std::make_unique<juce::AudioProcessorParameterGroup>(g.toLowerCase(), g, "|"); order.add(g); }
        auto* hp = new HostParam(*this, pi); hostParams.add(hp); hostIds.push_back(pi.id); gm[g]->addChild(std::unique_ptr<juce::AudioProcessorParameter>(hp));
    }
    for (auto& g : order) tree.addChild(std::move(gm[g]));
    setParameterTree(std::move(tree));

    store_.onPresetLoaded = [this] { refreshHostParams(); };
    store_.onEdited = [this] { editTicks_ = 12; };          // ~0.4 s after the last UI edit, host knobs follow
    loader_ = std::thread([this] { loadAssetsThread(); });
    vv::PowerSettings::load().apply(engine_);   // Battery Saving Mode is a device setting: restored here, never taken from a preset
    startTimerHz(30);
}

Processor::~Processor() {
    stopTimer();
    if (loader_.joinable()) loader_.join();
    vv_engine_destroy(engine_);
}

juce::File Processor::userPresetDir() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Vektur Vortex").getChildFile("Presets");
}

juce::AudioParameterFloat* Processor::paramByAddress(const juce::String& a) const {
    for (auto* p : hostParams) if (p->paramID == a) return p;
    return nullptr;
}

void Processor::hostParamChanged(unsigned id, float value) { if (suppress_.load() == 0) vv_engine_set_param(engine_, id, value); }

void Processor::loadAssetsThread() {
    const auto root = assets::root();
    for (auto& f : root.getChildFile("Wavetables").findChildFiles(juce::File::findFiles, false, "*.vvwt")) {
        juce::MemoryBlock mb; if (f.loadFileAsData(mb)) vv_engine_load_wavetable(engine_, f.getFileNameWithoutExtension().toRawUTF8(), (const uint8_t*) mb.getData(), mb.getSize());
    }
    auto list = juce::JSON::parse(root.getChildFile("builtin_samples.json"));
    if (auto* a = list.getArray()) for (auto& e : *a) {
        juce::MemoryBlock mb; if (root.getChildFile("Samples").getChildFile(e.getProperty("file", {}).toString()).loadFileAsData(mb))
            vv_engine_load_sample_wav(engine_, e.getProperty("name", {}).toString().toRawUTF8(), (const uint8_t*) mb.getData(), mb.getSize());
    }
    assetsReady_ = true;
    juce::MessageManager::callAsync([this] { applyPendingOrDefault(); });
}

void Processor::applyPendingOrDefault() {
    if (presetApplied_) return;
    bool ok = false;
    if (pendingState_.isNotEmpty()) ok = store_.loadJSON(pendingState_);
    if (!ok) ok = store_.loadJSON(assets::root().getChildFile("default_preset.json").loadFileAsString());
    store_.applyGlobals();
    presetApplied_ = true;
}

void Processor::waitUntilLoaded(int timeoutMs) {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (!presetApplied_ && juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

void Processor::refreshHostParams() {
    ++suppress_;
    for (int i = 0; i < hostParams.size(); ++i) {
        auto* p = hostParams[i]; const float v = vv_engine_get_param(engine_, hostIds[(size_t) i]);
        if (std::abs(p->get() - v) > 1e-6f) p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    --suppress_;
}

void Processor::timerCallback() {
    if (!presetApplied_) return;
    if (vv_engine_pump_params(engine_) > 0 && ++reloadTicks_ >= 0) { reloadTicks_ = -6; store_.reload(); }   // host automation changed the preset
    if (vv_engine_poll_cc(engine_) > 0) store_.reload();   // MIDI CCs that drive preset parameters (web CC map / MIDI LEARN)
    if (reloadTicks_ < 0) ++reloadTicks_;
    if (editTicks_ > 0 && --editTicks_ == 0) { vv_engine_sync_params_from_preset(engine_); refreshHostParams(); }
}

void Processor::prepareToPlay(double sr, int blockSize) { sampleRate_ = sr; vv_engine_prepare(engine_, sr); vv_engine_set_display_latency(engine_, 2 * juce::jmax(64, blockSize));   // playheads follow what is audible (~2 blocks of output buffering) if (presetApplied_) store_.applyGlobals(); }

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet();
    return (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono()) && (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer(midi, 0, n, true);
    for (const auto meta : midi) { const auto m = meta.getMessage(); if (m.isNoteOn()) lastNoteOn.store(m.getNoteNumber(), std::memory_order_relaxed); }
    // Standalone: the master tempo and PLAY button drive the transport. Hosted (AU / VST3): the host is transport, tempo and sync for everything
    // that follows the beat clock - piano roll, automation lanes, arpeggiator, sequencers.
    if (wrapperType != wrapperType_Standalone) {
        bool hosted = false;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
            const double bpm = pos->getBpm().orFallback(120.0), ppq = pos->getPpqPosition().orFallback(0.0); const int playing = pos->getIsPlaying() ? 1 : 0;
            vv_engine_set_host_transport(engine_, 1, playing, ppq, bpm); hosted = true; lastPlaying_ = playing; lastBpm_ = bpm;
        }
        if (!hosted && lastPlaying_ != -2) { vv_engine_set_host_transport(engine_, 0, 0, 0, 120.0); lastPlaying_ = -2; }
    }
    for (const auto meta : midi) { const auto m = meta.getMessage(); if (m.getRawDataSize() >= 1 && m.getRawDataSize() <= 3) vv_engine_midi(engine_, m.getRawData(), m.getRawDataSize()); }
    midi.clear();
    if (getTotalNumInputChannels() > 0) { const float* il = buffer.getReadPointer(0); const float* ir = getTotalNumInputChannels() > 1 ? buffer.getReadPointer(1) : nullptr; vv_engine_feed_input(engine_, il, ir, n); }   // vocoder external input (copied before the output overwrites the buffer)
    float* l = buffer.getWritePointer(0); float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    outputIsMono.store(r == nullptr, std::memory_order_relaxed);
    if (r) vv_engine_render(engine_, l, r, n);   // Left and Right stay discrete (also while sendToVectaSonic is on)
    else { scratch.setSize(1, n, false, false, true); float* s = scratch.getWritePointer(0); vv_engine_render(engine_, l, s, n); for (int i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + s[i]); }
    if (scopeWantMask.load(std::memory_order_relaxed) != 0) {   // Scope OFF (the default): no tap copy at all
        int w = scopeWrite.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { scopeL[w] = l[i]; scopeR[w] = r ? r[i] : l[i]; w = (w + 1) & (kScope - 1); }
        scopeWrite.store(w, std::memory_order_release);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto* o = new juce::DynamicObject(); juce::var root(o);
    // v2: 242-param table (SID/DX7/LFO additions). v1 states load unchanged: missing params keep defaults.
    o->setProperty("v", 2); o->setProperty("preset", store_.exportPresetFile()); o->setProperty("send", sendToVectaSonic.load());
    { auto* ui = new juce::DynamicObject(); ui->setProperty("scopeMode", scopeMode.load()); ui->setProperty("tab", uiTab.load()); ui->setProperty("w", uiWidth.load()); ui->setProperty("h", uiHeight.load()); o->setProperty("ui", juce::var(ui)); }
    auto* g = new juce::DynamicObject(); for (auto& kv : store_.globals()) g->setProperty(juce::Identifier(kv.first), kv.second); o->setProperty("globals", juce::var(g));
    auto* pr = new juce::DynamicObject(); for (auto* p : hostParams) pr->setProperty(juce::Identifier(p->paramID), p->get()); o->setProperty("params", juce::var(pr));
    dest.append(juce::JSON::toString(root, true).toRawUTF8(), juce::JSON::toString(root, true).getNumBytesAsUTF8());
}

void Processor::setStateInformation(const void* data, int size) {
    auto v = juce::JSON::parse(juce::String::fromUTF8((const char*) data, size));
    if (!v.getDynamicObject()) return;
    if (auto* g = v["globals"].getDynamicObject()) for (auto& kv : g->getProperties()) store_.globalsMutable()[kv.name.toString()] = (double) kv.value;
    sendToVectaSonic.store((bool) v.getProperty("send", false));
    if (auto* ui = v["ui"].getDynamicObject()) { scopeMode.store(juce::jlimit(0, 4, (int) ui->getProperty("scopeMode"))); uiTab.store(juce::jlimit(0, 8, (int) ui->getProperty("tab")));
        uiWidth.store(juce::jlimit(420, 2000, (int) ui->getProperty("w"))); uiHeight.store(juce::jlimit(480, 1400, (int) ui->getProperty("h"))); }
    pendingState_ = v["preset"].toString();
    if (presetApplied_ && pendingState_.isNotEmpty()) { store_.loadJSON(pendingState_); store_.applyGlobals(); }
    if (auto* pr = v["params"].getDynamicObject()) for (auto& kv : pr->getProperties()) if (auto* p = paramByAddress(kv.name.toString())) p->setValueNotifyingHost(p->convertTo0to1((float) (double) kv.value));
}

} // namespace vv

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vv::Processor(); }
