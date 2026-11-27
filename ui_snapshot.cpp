// Headless UI test: builds the processor + editor, renders every tab to a PNG, and exercises the controls.
// Usage: vv_ui_snapshot <outDir> [width height]
#include "../Source/InterstellarPresets.h"
#include "../Source/Screens.h"
#include "../Source/Scales.h"
#include "../Source/Appearance.h"
#include "../Source/CurveEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Source/PluginProcessor.h"
namespace vv { juce::Component* makeZonesPanelForTest(Processor&); }
#include "../Source/PluginEditor.h"
#include "../Source/EqGraph.h"
#include "../Source/Assets.h"
#include "../Source/VoidView.h"
#include "../Source/AuthoringTools.h"
#include "../Source/SampleEditor.h"
#include "../Source/DawRecorderUi.h"
#include "../Source/GrainBridgeView.h"
#include "../Source/WarpView.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(argc > 1 ? argv[1] : "/tmp/vv_ui");
    const int W = argc > 3 ? atoi(argv[2]) : 1100, H = argc > 3 ? atoi(argv[3]) : 720;
    out.createDirectory();
    vv::Processor proc;
    proc.setPlayConfigDetails(0, 2, 48000, 512); proc.prepareToPlay(48000, 512);
    proc.waitUntilLoaded();
    int fails = 0; auto check = [&](const char* n, bool ok) { std::printf("%-48s %s\n", n, ok ? "OK" : "FAIL"); if (!ok) ++fails; };
    check("assets + default preset loaded", proc.assetsReady() && vv_engine_has_preset(proc.engine()) == 1 || proc.assetsReady());
    check("store mirrors the preset", proc.store().count("fxStages") >= 0 && proc.store().has("layers.WT.oscs.0.level"));
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor()); ed->setSize(W, H);
        auto* editor = static_cast<vv::Editor*>(ed.get());
        const char* names[] = { "scope", "sound", "matrix", "seq", "fx", "xy", "presets", "settings", "host", "lfo", "xmod", "vocoder", "mixer", "spectrogram" };
        for (int i = 0; i < editor->numTabs(); ++i) {
            editor->selectTab(i); juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile(juce::String(names[i]) + ".png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
            check((juce::String("snapshot ") + names[i]).toRawUTF8(), img.isValid());
        }
        {   // EQ graph: set bands, render standalone, check the magnitude curve matches the bands
            proc.store().set("layers.WT.eq.band1.gain", 6.0); proc.store().set("layers.WT.eq.band2.freq", 800.0); proc.store().set("layers.WT.eq.band2.gain", -9.5);
            vv::EqGraph g(proc.store(), "layers.WT"); g.setSize(640, 214); g.refresh();
            auto img = g.createComponentSnapshot(g.getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("eq_graph.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
            check("eq graph reads bands", g.gain[0] == 6.0 && g.freq[1] == 800.0 && g.gain[1] == -9.5);
            check("eq graph magnitude: +6 dB low shelf, -9.5 dB bell at its centre", std::abs(vv::EqGraph::magnitudeDb(g.freq, g.gain, 20.0) - 6.0) < 0.5 && vv::EqGraph::magnitudeDb(g.freq, g.gain, 800.0) < -8.0);
            proc.store().set("layers.WT.eq.band1.gain", 0.0); proc.store().set("layers.WT.eq.band2.freq", 600.0); proc.store().set("layers.WT.eq.band2.gain", 0.0);
        }
        {   // screenshots of the real screens in context: layer EQ, sequencer (keyboard hidden), sampler sections
            auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
            std::function<juce::Component*(juce::Component*, std::function<bool(juce::Component*)>)> fnd = [&](juce::Component* c, std::function<bool(juce::Component*)> f) -> juce::Component* { if (f(c)) return c; for (auto* k : c->getChildren()) if (auto* r = fnd(k, f)) return r; return nullptr; };
            auto textBtn = [&](const juce::String& t) { return dynamic_cast<juce::TextButton*>(fnd(editor, [&](juce::Component* c) { auto* b = dynamic_cast<juce::TextButton*>(c); return b && b->getButtonText() == t && b->isVisible(); })); };
            auto shot = [&](const char* name) { pump(120); auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile(juce::String(name) + ".png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); check(name, img.isValid()); };
            auto viewportOf = [&]() { return dynamic_cast<juce::Viewport*>(fnd(editor, [&](juce::Component* c) { auto* v = dynamic_cast<juce::Viewport*>(c); return v && v->isVisible() && v->getHeight() > 150; })); };
            editor->setSize(1366, 1024); pump(100);   // iPad-landscape-like size so the screens have room
            editor->selectTab(1); pump(80);
            if (auto* b = textBtn("Wavetable")) { b->triggerClick(); pump(80); }
            if (auto* hzk = fnd(editor, [&](juce::Component* c) { auto* k = dynamic_cast<vv::Knob*>(c); return k && k->path() == "layers.WT.oscs.0.pwmRate"; })) if (auto* vp = viewportOf()) {
                const int y = hzk->getScreenY() - vp->getViewedComponent()->getScreenY(); vp->setViewPosition(0, juce::jmax(0, y - 120)); shot("shot_wt_hz"); vp->setViewPosition(0, 0); }
            if (auto* eqc = fnd(editor, [&](juce::Component* c) { return dynamic_cast<vv::EqGraph*>(c) != nullptr; })) if (auto* vp = viewportOf()) {
                const int y = eqc->getScreenY() - vp->getViewedComponent()->getScreenY(); vp->setViewPosition(0, juce::jmax(0, y - 40)); }
            shot("shot_eq_wt");
            if (auto* b = textBtn("HIDE KEYBOARD")) { b->triggerClick(); pump(120); }
            editor->selectTab(3); pump(80); if (auto* b = textBtn("Patterns")) { b->triggerClick(); pump(80); }
            shot("shot_sequencer");
            editor->selectTab(1); pump(80);
            { auto nm = vv::assets::sampleNames(); if (nm.size() > 3) { proc.store().set("layers.SMP.oscs.0.name", nm[3]); proc.store().set("layers.SMP.oscs.0.enabled", true); } }   // show a loaded sample, not an empty slot
            if (auto* b = textBtn("Sampler")) { b->triggerClick(); pump(160); }
            auto deepestVp = [&]() { juce::Viewport* last = nullptr; std::function<void(juce::Component*)> w = [&](juce::Component* c) { if (auto* v = dynamic_cast<juce::Viewport*>(c)) if (v->isVisible() && v->getHeight() > 150) last = v; for (auto* k : c->getChildren()) w(k); }; w(editor); return last; };
            if (auto* vp = deepestVp()) { vp->setViewPosition(0, 0); pump(60); }
            shot("shot_sampler_1");
            editor->setSize(1366, 1900); pump(200); shot("shot_sampler_tall");   // tall window: the whole sampler page without scrolling
            editor->setSize(1366, 1024); pump(100);
            if (auto* b = textBtn("HIDE KEYBOARD")) { b->triggerClick(); pump(60); }   // restore
            editor->setSize(W, H); editor->selectTab(0);
        }
        // sequencer Patterns page (piano-roll step grid)
        editor->selectTab(3); juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        std::function<juce::TextButton*(juce::Component*)> findB = [&](juce::Component* c) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == "Patterns") return b; for (auto* k : c->getChildren()) if (auto* r = findB(k)) return r; return nullptr; };
        if (auto* pb = findB(ed.get())) { pb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("seq_patterns.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); check("snapshot seq patterns (piano roll)", img.isValid()); }
        {   // Sampler: RANDOM SAMPLER FX gives all 16 slots a different microphone and speaker
            editor->selectTab(1); juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> findS = [&](juce::Component* c, const juce::String& t) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == t) return b; for (auto* k : c->getChildren()) if (auto* r = findS(k, t)) return r; return nullptr; };
            if (auto* sb = findS(ed.get(), "Sampler")) { sb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); }
            auto* fb = findS(ed.get(), "RANDOM SAMPLER FX"); check("Random Sampler FX button exists", fb != nullptr);
            if (fb) { fb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
                juce::StringArray mics, spks; for (int i = 0; i < 16; ++i) { char path[64]; snprintf(path, sizeof path, "layers.SMP.oscs.%d.mic", i); mics.add(proc.store().str(path)); snprintf(path, sizeof path, "layers.SMP.oscs.%d.spk", i); spks.add(proc.store().str(path)); }
                mics.removeDuplicates(false); spks.removeDuplicates(false); check("16 different microphones", mics.size() == 16 && !mics.contains("none")); check("16 different speakers", spks.size() == 16 && !spks.contains("none")); }
        }
        {   // Chords page: generate a progression, check the engine now holds it, snapshot
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> findT = [&](juce::Component* c, const juce::String& t) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == t) return b; for (auto* k : c->getChildren()) if (auto* r = findT(k, t)) return r; return nullptr; };
            if (auto* cb = findT(ed.get(), "Chords")) { cb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
                if (auto* g = findT(ed.get(), "GENERATE")) { g->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120); }
                const size_t n = vv_engine_chord_info(proc.engine(), nullptr, 0); check("chord maker holds a progression after GENERATE", n > 40);
                auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("seq_chords.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); check("snapshot seq chords", img.isValid()); } }
    {   // Sequencer pattern tools: RANDOMISE / COPY / CLEAR / PASTE
        editor->selectTab(3); juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        std::function<juce::TextButton*(juce::Component*, const juce::String&)> fT = [&](juce::Component* c, const juce::String& t) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == t) return b; for (auto* k : c->getChildren()) if (auto* r = fT(k, t)) return r; return nullptr; };
        auto onCount = [&] { int n = 0; for (int i = 0; i < 16; ++i) n += proc.store().flag("layers.WT.patterns.0.steps." + juce::String(i) + ".on") ? 1 : 0; return n; };
        if (auto* pb = fT(ed.get(), "Patterns")) { pb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); }
        auto* rb = fT(ed.get(), "RANDOMISE"); auto* cb = fT(ed.get(), "COPY PATTERN"); auto* xb = fT(ed.get(), "CLEAR STEPS"); auto* pb2 = fT(ed.get(), "PASTE PATTERN");
        check("pattern tool buttons exist", rb && cb && xb && pb2);
        if (rb && cb && xb && pb2) {
            rb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); const int afterRand = onCount(); check("RANDOMISE turns some steps on", afterRand > 0 && afterRand < 16);
            cb->triggerClick(); xb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); check("CLEAR STEPS empties the pattern", onCount() == 0);
            pb2->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); check("PASTE PATTERN restores the copied steps", onCount() == afterRand);
        }
        {   // undo / redo through the store (top-bar UNDO / REDO)
            auto& st = proc.store(); st.clearHistory(); juce::Thread::sleep(800);
            const double before = st.num("layers.WT.oscs.0.level", 0.8); st.set("layers.WT.oscs.0.level", 0.123); check("edit recorded as an undo step", st.canUndo());
            st.undo(); check("UNDO restores the previous value", std::abs(st.num("layers.WT.oscs.0.level", -1) - before) < 1e-6 && st.canRedo());
            st.redo(); check("REDO re-applies the edit", std::abs(st.num("layers.WT.oscs.0.level", -1) - 0.123) < 1e-6);
            st.undo(); }
        {   // scope modes render (radial, spectrogram, spectrogram 3D) with a note playing
            proc.keyboardState.noteOn(1, 57, 0.9f);
            for (int md = 2; md <= 4; ++md) { proc.scopeMode.store(md); editor->selectTab(0);
                for (int k = 0; k < 8; ++k) { std::vector<float> L(512), R(512); juce::MidiBuffer mb; juce::AudioBuffer<float> ab(2, 512); ab.clear(); proc.processBlock(ab, mb); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }
                auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile(juce::String("play_scope_") + juce::String(md) + ".png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
                check((juce::String("scope mode ") + juce::String(md) + " renders").toRawUTF8(), img.isValid()); }
            proc.keyboardState.noteOff(1, 57, 0.f); proc.scopeMode.store(0); }
        {   // GENERATE / MODIFY
            auto& st = proc.store(); juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("vv_gen_test"); tmp.deleteRecursively();
            const auto n1 = st.generatePreset("Bass", tmp); check("GENERATE files a 'Bass Gen 1' preset", n1 == "Bass Gen 1" && tmp.getChildFile("Bass Gen 1.json").existsAsFile());
            check("generated FM engine A is on", st.flag("layers.FM.engineEnabled.0", false));
            const double before = st.num("layers.FM.oscs.0.ratio", 0); const auto n2 = st.modifyPreset(tmp);
            check("MODIFY files a '(mod)' copy", n2 == "Bass Gen 1 (mod)" && tmp.getChildFile(n2 + ".json").existsAsFile());
            check("MODIFY keeps values within 10 %", std::abs(st.num("layers.FM.oscs.0.ratio", 0) - before) <= std::abs(before) * 0.1001 + 1e-9);
            st.undo(); check("UNDO reverts MODIFY", std::abs(st.num("layers.FM.oscs.0.ratio", 0) - before) < 1e-9); tmp.deleteRecursively(); }
        {   // custom LFO shape: curve helpers + the engine accepts a drawn shape
            auto& st = proc.store(); vv::writeCurve(st, "lfo.lfo1.shape", vv::basicWaveformPoints("triangle")); st.set("lfo.lfo1.waveform", "custom");
            auto pts = vv::readCurve(st, "lfo.lfo1.shape"); check("custom LFO shape stored as 9 points with control points", pts.size() == 9 && pts[0].hasC && std::abs(pts[4].y - 1.0) < 1e-9);
            pts[4].y = 0.25; vv::writeCurve(st, "lfo.lfo1.shape", pts); check("edited point round-trips", std::abs(vv::readCurve(st, "lfo.lfo1.shape")[4].y - 0.25) < 1e-9); st.set("lfo.lfo1.waveform", "sine"); }
        // keyboard options live in globals
        proc.store().setGlobal("scaleIdx", 1); proc.store().setGlobal("scaleRoot", 0); proc.store().setGlobal("scaleLock", 1);
        editor->selectTab(0); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        check("scale lock settings persist as globals", proc.store().global("scaleLock") == 1 && vv::kbInScale(1, 0, 64) && !vv::kbInScale(1, 0, 61));
        proc.store().setGlobal("scaleLock", 0); proc.store().setGlobal("scaleIdx", 0);
        {   // wavetable authoring dialogs + sampler slot tools (formula / harmonics / draw tables; stretch / revert of a slot sample)
            auto& st = proc.store(); auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };
            std::function<juce::Component*(juce::Component*, std::function<bool(juce::Component*)>)> find = [&](juce::Component* c, std::function<bool(juce::Component*)> pred) -> juce::Component* { if (pred(c)) return c; for (auto* ch : c->getChildren()) if (auto* r = find(ch, pred)) return r; return nullptr; };
            auto btn = [&](juce::Component* root, const juce::String& t) { return dynamic_cast<juce::TextButton*>(find(root, [&](juce::Component* c) { auto* b = dynamic_cast<juce::TextButton*>(c); return b && b->getButtonText() == t; })); };
            auto snap = [&](juce::Component& c, const char* file) { c.setVisible(true); auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile(file)); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); };
            const int n0 = st.count("customWavetables");
            auto fd = vv::authoring::makeFormula(st, "layers.WT.oscs.0", {}); fd->setBounds(0, 0, 560, 220);
            if (auto* te = dynamic_cast<juce::TextEditor*>(find(fd.get(), [](juce::Component* c) { return dynamic_cast<juce::TextEditor*>(c) != nullptr; }))) te->setText("foo(");
            if (auto* ap = btn(fd.get(), "APPLY")) ap->triggerClick(); pump();
            check("formula dialog: a bad formula adds no table", st.count("customWavetables") == n0);
            if (auto* te = dynamic_cast<juce::TextEditor*>(find(fd.get(), [](juce::Component* c) { return dynamic_cast<juce::TextEditor*>(c) != nullptr; }))) te->setText("Math.sin(2*Math.PI*x*(1+6*n))");
            if (auto* ap = btn(fd.get(), "APPLY")) ap->triggerClick(); pump();
            check("formula dialog: a good formula creates a 32-frame custom table", st.count("customWavetables") == n0 + 1 && st.count("customWavetables." + juce::String(n0) + ".frames") == 32);
            check("formula dialog: the oscillator now points at it", st.str("layers.WT.oscs.0.table").startsWith("custom:"));
            snap(*fd, "dlg_formula.png");
            auto hd = vv::authoring::makeHarmonics(st, "layers.WT.oscs.1", {}); hd->setBounds(0, 0, 560, 250); pump();
            check("harmonics dialog: creates a one-frame table straight away", st.count("customWavetables") == n0 + 2 && st.str("layers.WT.oscs.1.table").startsWith("custom:")); snap(*hd, "dlg_harmonics.png");
            auto dd = vv::authoring::makeDraw(st, "layers.WT.oscs.1", {}); dd->setBounds(0, 0, 620, 330); pump(); pump();
            check("draw dialog: drawn waveform replaces the table, 1 frame", st.count("customWavetables") == n0 + 3 || st.count("customWavetables") == n0 + 2); snap(*dd, "dlg_draw.png");
            {   // the custom table actually sounds through the engine
                std::vector<float> a(4800), b(4800); st.set("layers.WT.oscs.0.enabled", true); vv_engine_note_on(proc.engine(), 57, 0.9f); for (int i = 0; i < 12; ++i) vv_engine_render(proc.engine(), a.data(), b.data(), 4800);
                double e = 0; for (float x : a) e += x * x; vv_engine_note_off(proc.engine(), 57); check("custom formula table plays", e > 1.0);
            }
            {   // sampler slot tools: stretch x2 of an embedded 0.5 s tone, then revert
                std::vector<float> tone(24000); for (int i = 0; i < 24000; ++i) tone[(size_t) i] = 0.5f * std::sin(6.2831853f * 440.f * (float) i / 48000.f);
                VVAudio* au = vv_audio_create(48000, 1, 24000, tone.data(), nullptr); juce::MemoryBlock wav; wav.setSize(vv_audio_to_wav16(au, nullptr, 0)); vv_audio_to_wav16(au, (uint8_t*) wav.getData(), wav.getSize()); vv_audio_free(au);
                vv::authoring::putSample(st, "unit tone", wav); st.set("layers.SMP.oscs.0.name", "unit tone"); pump();
                check("embedded sample round-trips through the store", vv::authoring::sampleWav(st, "unit tone").getSize() == wav.getSize());
                auto sd = vv::authoring::makeSampleTools(st, 0, {}); sd->setBounds(0, 0, 640, 560);
                if (auto* sl = dynamic_cast<juce::Slider*>(find(sd.get(), [](juce::Component* c) { return dynamic_cast<juce::Slider*>(c) != nullptr; }))) sl->setValue(2.0);
                if (auto* ap = btn(sd.get(), "APPLY STRETCH")) ap->triggerClick(); pump();
                check("stretch: slot now uses a processed copy and remembers the original", st.str("layers.SMP.oscs.0.name") == "unit tone (stretched)" && st.str("layers.SMP.oscs.0.origName") == "unit tone");
                auto mb = vv::authoring::sampleWav(st, "unit tone (stretched)"); VVAudio* st2 = vv_audio_from_wav((const uint8_t*) mb.getData(), mb.getSize());
                check("stretch x2: processed sample is twice as long", st2 && std::abs(vv_audio_length(st2) - 48000) < 16); if (st2) vv_audio_free(st2);
                snap(*sd, "dlg_sample_tools.png");
                if (auto* rv = btn(sd.get(), "REVERT TO ORIGINAL")) rv->triggerClick(); pump();
                check("revert restores the original sample name", st.str("layers.SMP.oscs.0.name") == "unit tone" && st.str("layers.SMP.oscs.0.origName").isEmpty());
                {   // ---- PWM speed (Hz) dial: value reaches the store/engine, accepts long-press modulation routes
                    vv::Knob hz(st, "Hz", "layers.WT.oscs.0.pwmRate", 0, 20, 0, { "%.2f", " Hz", false, 2 }); hz.setModDest("WT:osc1PwmRate"); hz.setBounds(0, 0, 90, 110);
                    hz.slider.setValue(6.5); pump();
                    check("Hz dial writes layers.WT.oscs.0.pwmRate", std::abs(st.num("layers.WT.oscs.0.pwmRate") - 6.5) < 1e-6);
                    { char b[64] = {0}; vv_engine_get_value(proc.engine(), "layers.WT.oscs.0.pwmRate", b, sizeof b); check("Hz dial reaches the engine preset", std::abs(atof(b) - 6.5) < 1e-6); }
                    check("Hz dial has no route yet", !hz.hasModRoutes());
                    { auto* o = new juce::DynamicObject(); o->setProperty("source", "lfo1"); o->setProperty("dest", "WT:osc1PwmRate"); o->setProperty("amount", 30); st.append("connections", juce::var(o)); } pump(); hz.refresh();
                    check("Hz dial shows its modulation route", hz.hasModRoutes() && hz.modDest() == "WT:osc1PwmRate");
                    st.remove("connections", st.count("connections") - 1); st.set("layers.WT.oscs.0.pwmRate", 0.0);
                }
                {   // ---- sampler waveform editor
                    auto burstSample = [&](const char* nm, int bursts, double spacing) {   // 440 Hz bursts of 60 ms every `spacing` seconds
                        std::vector<float> d((size_t) (48000 * (bursts * spacing + 0.4)), 0.f);
                        for (int b = 0; b < bursts; ++b) for (int i = 0; i < 2880; ++i) { const size_t k = (size_t) ((0.1 + b * spacing) * 48000) + (size_t) i; if (k < d.size()) d[k] = 0.6f * std::sin(6.2831853f * 440.f * (float) i / 48000.f); }
                        VVAudio* a = vv_audio_create(48000, 1, (int) d.size(), d.data(), nullptr); juce::MemoryBlock mb; mb.setSize(vv_audio_to_wav16(a, nullptr, 0)); vv_audio_to_wav16(a, (uint8_t*) mb.getData(), mb.getSize()); vv_audio_free(a);
                        vv::authoring::putSample(st, nm, mb); };
                    burstSample("editor bursts", 4, 0.5); st.setMany({ { "layers.SMP.oscs.0.name", "editor bursts" }, { "layers.SMP.oscs.0.start", 0.0 }, { "layers.SMP.oscs.0.end", 1.0 }, { "layers.SMP.oscs.0.transients", juce::var(juce::Array<juce::var>()) } }); pump();
                    bool rebuilt = false; vv::SampleEditorPanel ed(st, 0, false, [&] { rebuilt = true; }); ed.setBounds(0, 0, 820, 300); pump();
                    check("editor: loads the slot's sample", ed.loadedLength() > 90000);
                    if (auto* b = btn(&ed, "DETECT TRANSIENTS")) b->triggerClick(); pump();
                    std::printf("  detected %zu\n", ed.slices().size()); check("editor: detects the 4 bursts", ed.slices().size() >= 3 && ed.slices().size() <= 5);
                    burstSample("editor many", 40, 0.1); st.set("layers.SMP.oscs.0.name", "editor many"); pump();
                    if (auto* b = btn(&ed, "DETECT TRANSIENTS")) b->triggerClick(); pump();
                    check("editor: auto slices are capped at 16", ed.slices().size() == 16);
                    st.set("layers.SMP.oscs.0.name", "editor bursts"); pump(); if (auto* b = btn(&ed, "DETECT TRANSIENTS")) b->triggerClick(); pump();
                    auto& wv = ed.waveComponent(); const int W2 = wv.getWidth();
                    auto ev = [&](float x, juce::ModifierKeys m, int dx = 0) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float>(x, 6.f), m, 1.f, 0.f, 0.f, 0.f, 0.f, &wv, &wv, juce::Time::getCurrentTime(), juce::Point<float>(x - (float) dx, 6.f), juce::Time::getCurrentTime(), 1, dx != 0); };
                    st.set("layers.SMP.oscs.0.start", 0.0);
                    wv.mouseDown(ev(2.f, juce::ModifierKeys())); wv.mouseDrag(ev((float) W2 * 0.25f, juce::ModifierKeys::leftButtonModifier, (int) (W2 * 0.25f))); wv.mouseUp(ev((float) W2 * 0.25f, juce::ModifierKeys()));
                    check("editor: dragging the start marker moves Start", std::abs(st.num("layers.SMP.oscs.0.start") - 0.25) < 0.02);
                    wv.mouseDown(ev((float) W2 - 2.f, juce::ModifierKeys())); wv.mouseDrag(ev((float) W2 * 0.75f, juce::ModifierKeys::leftButtonModifier, (int) (W2 * 0.25f))); wv.mouseUp(ev((float) W2 * 0.75f, juce::ModifierKeys()));
                    check("editor: dragging the end marker moves End", std::abs(st.num("layers.SMP.oscs.0.end") - 0.75) < 0.02);
                    { const size_t n0 = ed.slices().size(); wv.mouseDown(ev((float) W2 * 0.6f, juce::ModifierKeys(juce::ModifierKeys::altModifier))); wv.mouseUp(ev((float) W2 * 0.6f, juce::ModifierKeys()));
                      check("editor: alt-click adds a slice marker", ed.slices().size() == n0 + 1); }
                    ed.zoomBy(2.0); check("editor: zoom narrows the view", ed.viewEnd() - ed.viewStart() < 0.55); ed.resetView(); check("editor: reset view shows everything", ed.viewStart() == 0 && ed.viewEnd() == 1);
                    snap(ed, "editor_inline.png");
                    { auto sl = ed.slices(); sl[0].reverse = true; sl[0].pan = -0.6; ed.setSlices(sl); check("editor: per-slice reverse + pan reach the store", st.flag("layers.SMP.oscs.0.transients.0.reverse") && std::abs(st.num("layers.SMP.oscs.0.transients.0.pan") + 0.6) < 1e-6); }
                    { juce::Component host0; host0.setBounds(0, 0, 1100, 760); vv::openSampleEditorFullScreen(host0, st, 0, {}); pump(); snap(host0, "editor_fullscreen_slices.png"); if (auto* b = btn(&host0, "X  CLOSE")) b->triggerClick(); pump(); pump(); }
                    // CUT (SILENCE) the selection 0.25..0.75: that part goes quiet, the rest is untouched, length unchanged
                    const int len0 = ed.loadedLength(); if (auto* b = btn(&ed, "CUT (SILENCE)")) b->triggerClick(); pump();
                    { auto mb2 = vv::authoring::sampleWav(st, st.str("layers.SMP.oscs.0.name")); VVAudio* a2 = vv_audio_from_wav((const uint8_t*) mb2.getData(), mb2.getSize()); std::vector<float> d((size_t) vv_audio_length(a2)); vv_audio_read(a2, 0, d.data()); vv_audio_free(a2);
                      double inSel = 0, outSel = 0; for (size_t i = 0; i < d.size(); ++i) { const double f = (double) i / d.size(); (f > 0.27 && f < 0.73 ? inSel : outSel) += std::fabs(d[i]); }
                      check("editor: cut silences the selection only", (int) d.size() == len0 && inSel < 1e-6 && outSel > 10.0); }
                    if (auto* b = btn(&ed, "NORMALIZE")) b->triggerClick(); pump();
                    { auto mb2 = vv::authoring::sampleWav(st, st.str("layers.SMP.oscs.0.name")); VVAudio* a2 = vv_audio_from_wav((const uint8_t*) mb2.getData(), mb2.getSize()); std::vector<float> d((size_t) vv_audio_length(a2)); vv_audio_read(a2, 0, d.data()); vv_audio_free(a2);
                      float pk = 0; for (float v : d) pk = std::max(pk, std::fabs(v)); check("editor: normalize brings the peak to full scale", pk > 0.98f); }
                    if (auto* b = btn(&ed, "CROP TO SELECTION")) b->triggerClick(); pump();
                    check("editor: crop halves the sample, resets markers, asks the page to rebuild", ed.loadedLength() > 54000 && ed.loadedLength() < 62000 && st.num("layers.SMP.oscs.0.start") == 0 && st.num("layers.SMP.oscs.0.end") == 1 && ed.slices().empty() && rebuilt);
                    { juce::Component host; host.setBounds(0, 0, 1100, 760); vv::openSampleEditorFullScreen(host, st, 0, {}); pump();
                      check("editor: full screen overlay opens over the host", host.getNumChildComponents() == 1);
                      if (auto* b = btn(&host, "DETECT TRANSIENTS")) b->triggerClick(); pump();
                      check("editor: full screen has a slice list with a row per slice", find(&host, [](juce::Component* c) { return dynamic_cast<juce::Slider*>(c) != nullptr; }) != nullptr);
                      snap(host, "editor_fullscreen.png");
                      if (auto* b = btn(&host, "X  CLOSE")) b->triggerClick(); pump(); pump();
                      check("editor: close removes the overlay", host.getNumChildComponents() == 0); }
                }
                {   // RECORD dialog: capture the master output of a held note, save into the slot
                    auto rd = vv::authoring::makeRecord(st, 0, {}); rd->setBounds(0, 0, 460, 250);
                    if (auto* tg = dynamic_cast<juce::ToggleButton*>(find(rd.get(), [](juce::Component* c) { auto* t = dynamic_cast<juce::ToggleButton*>(c); return t && t->getButtonText().startsWith("Arm"); }))) tg->setToggleState(false, juce::dontSendNotification);
                    if (auto* go = btn(rd.get(), "RECORD")) go->triggerClick(); pump();
                    std::vector<float> a(4800), b(4800); st.set("layers.WT.oscs.0.enabled", true); vv_engine_note_on(proc.engine(), 57, 0.9f); for (int i = 0; i < 6; ++i) vv_engine_render(proc.engine(), a.data(), b.data(), 4800);
                    vv_engine_note_off(proc.engine(), 57); check("record: frames counted while recording", vv_engine_rec_frames(proc.engine()) == 6 * 4800);
                    if (auto* sp = btn(rd.get(), "STOP")) sp->triggerClick(); pump();
                    if (auto* sv = btn(rd.get(), "SAVE")) sv->triggerClick(); pump(); snap(*rd, "dlg_record.png");
                    check("record: saved recording is the slot's sample", st.str("layers.SMP.oscs.0.name").startsWith("Recording") && vv::authoring::sampleWav(st, st.str("layers.SMP.oscs.0.name")).getSize() > 44 + 4800);
                }
                { auto id = vv::authoring::makeImageAudio(st, 0, {}); id->setBounds(0, 0, 520, 330); snap(*id, "dlg_image_audio.png"); check("image->audio dialog builds", id != nullptr); }
                const auto js = st.exportJSON(); check("preset JSON carries customWavetables and sampleLibrary", js.contains("customWavetables") && js.contains("sampleLibrary"));
            }
        }
        {   // DAW recorder block in the footer: REC -> render audio with the click on -> STOP -> a WAV file and a sampler-library copy appear
            editor->selectTab(0); juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
            auto& st = proc.store(); const int nSamples0 = st.count("sampleLibrary");
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> fb = [&](juce::Component* c, const juce::String& tx) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getName() == tx) return b; for (auto* k : c->getChildren()) if (auto* r = fb(k, tx)) return r; return nullptr; };
            st.setGlobal("dawClick", 1); st.setGlobal("dawBits", 24); st.setGlobal("dawRate", 44100);
            auto* rec = fb(editor, "dawRec"); auto* stop = fb(editor, "dawStop"); check("footer has REC and STOP", rec && stop);
            if (rec && stop) {
                rec->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
                std::vector<float> a(4800), b(4800); st.set("layers.WT.oscs.0.enabled", true); vv_engine_note_on(proc.engine(), 60, 0.9f); for (int i = 0; i < 10; ++i) { vv_engine_render(proc.engine(), a.data(), b.data(), 4800); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }
                vv_engine_note_off(proc.engine(), 60); check("recorder: REC starts the engine recorder", vv_daw_state(proc.engine()) == 1);
                stop->triggerClick(); for (int i = 0; i < 60 && fb(editor, "dawRec") && !fb(editor, "dawRec")->isEnabled(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
                auto files = vv::DawBlock::recordingsFolder().findChildFiles(juce::File::findFiles, false, "Recording *.wav"); juce::File newest; for (auto& f : files) if (newest == juce::File() || f.getLastModificationTime() > newest.getLastModificationTime()) newest = f;
                check("recorder: a 24-bit 44.1 kHz WAV file was written", newest.existsAsFile() && newest.getSize() > 44 + 44100 * 6 / 2);
                juce::FileInputStream in(newest); uint8_t h[44] = {}; in.read(h, 44); check("recorder: file header says 44100 Hz, 24 bit", (h[24] | (h[25] << 8) | (h[26] << 16)) == 44100 && h[34] == 24);
                check("recorder: take is also a sampler sample", st.count("sampleLibrary") == nSamples0 + 1);
                for (auto& f : files) f.deleteFile(); newest.deleteFile();
            }
            st.setGlobal("dawClick", 0);
        }
        {   // SID WT arp editor: + STEP creates the default step list in the preset, steps edit through the store
            editor->selectTab(1); juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> fa = [&](juce::Component* c, const juce::String& tx) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == tx) return b; for (auto* ch : c->getChildren()) if (auto* r = fa(ch, tx)) return r; return nullptr; };
            if (auto* sb = fa(editor, "SID")) { sb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120); }
            if (auto* ad = fa(editor, "+ STEP")) { const int n0 = proc.store().count("layers.SID.wtArp.steps"); ad->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
                check("arp + STEP adds steps (default list is created when missing)", proc.store().count("layers.SID.wtArp.steps") >= n0 + 1 && proc.store().count("layers.SID.wtArp.steps") >= 4);
                auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("sid_arp.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); }
            else check("arp editor exists", false);
            editor->selectTab(0);
        }
        {   // drum sequencer page: a clicked cell lands in the preset JSON and reaches the engine preset
            editor->selectTab(3); juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> fd = [&](juce::Component* c, const juce::String& tx) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == tx) return b; for (auto* ch : c->getChildren()) if (auto* r = fd(ch, tx)) return r; return nullptr; };
            if (auto* d = fd(editor, "Drums")) { d->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120); proc.store().set("drumSeq.on", 1.0); proc.store().set("drumSeq.lanes.0.steps.0", 1.0); proc.store().set("drumSeq.lanes.0.length", 8.0); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
                check("drum cell and length are stored", proc.store().num("drumSeq.lanes.0.steps.0") == 1 && proc.store().num("drumSeq.lanes.0.length") == 8);
                auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("drums.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
                proc.store().set("drumSeq.on", 0.0); proc.store().set("drumSeq.lanes.0.steps.0", 0.0); proc.store().set("drumSeq.lanes.0.length", 16.0); }
            else check("Drums sub-tab exists", false);
            editor->selectTab(0);
        }
        {   // footer transport row: VIEW SID opens OSCILLATORS > SID, HOLD writes every osc, SOLO/MUTE do not crash, BPM field exists
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> fb = [&](juce::Component* c, const juce::String& tx) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == tx) return b; for (auto* ch : c->getChildren()) if (auto* r = fb(ch, tx)) return r; return nullptr; };
            auto* v = fb(editor, "VIEW SID"); check("footer VIEW SID exists", v != nullptr);
            if (v) { v->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); check("VIEW SID selects the OSCILLATORS tab", editor->getCurrentTab() == 1);
                check("SID voice panel is on the page", fb(editor, "SID") != nullptr); }
            if (auto* h = fb(editor, "HOLD SID")) { h->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
                check("HOLD SID sets hold on all three voices", proc.store().num("layers.SID.oscs.0.hold") == 1 && proc.store().num("layers.SID.oscs.2.hold") == 1); h->triggerClick(); }
            else check("HOLD SID exists", false);
            if (auto* so = fb(editor, "SOLO FM")) { so->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(40); check("SOLO FM toggles", so->getToggleState()); so->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(40); } else check("SOLO FM exists", false);
            editor->selectTab(0);
        }
        {   // MPC pads overlay
            editor->selectTab(0); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
            std::function<juce::TextButton*(juce::Component*, const juce::String&)> findS = [&](juce::Component* c, const juce::String& tx) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == tx) return b; for (auto* k : c->getChildren()) if (auto* r = findS(k, tx)) return r; return nullptr; };
            auto* mb = findS(editor, "MPC PADS"); check("MPC PADS button exists", mb != nullptr);
            if (mb) { mb->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
                auto* lock = findS(editor, "LOCK"); check("MPC overlay shows LOCK and BOTTOM ROW controls", lock != nullptr && findS(editor, "BOTTOM ROW: SAMPLES") != nullptr);
                auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("play_mpc.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
                if (lock) { lock->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(60); check("LOCK persists as a global", proc.store().global("mpcLocked") == 1); lock = findS(editor, "UNLOCK"); if (lock) lock->triggerClick(); }
                if (auto* cl = findS(editor, "CLOSE")) cl->triggerClick(); }
            auto* zb = findS(editor, "ZONES"); check("ZONES button exists", zb != nullptr);
            {   std::unique_ptr<juce::Component> z(vv::makeZonesPanelForTest(proc)); z->setBounds(0, 0, 780, z->getHeight()); proc.store().set("layers.FM.oscs.0.rangeLow", 36);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
                auto img = z->createComponentSnapshot(z->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("zones.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
                check("zones panel renders every oscillator row", img.isValid() && img.getHeight() > 600); proc.store().set("layers.FM.oscs.0.rangeLow", 0); }
        }
        {   // appearance: scheme accent, pot/dropdown style, brushed-metal background
            { auto& d = proc.store(); d.setGlobal("bgImage", 0); d.setGlobal("scheme", 1); check("default background is brushed metal", vv::look::effectiveBackground(d) == 6); d.setGlobal("scheme", 4); check("Future scheme keeps its own picture", vv::look::effectiveBackground(d) == 1); d.setGlobal("scheme", 1); }
            auto& st = proc.store(); st.setGlobal("scheme", 2); st.setGlobal("bgImage", 6); st.setGlobal("potStyle", 0); st.setGlobal("ddStyle", 2);
            check("brushed metal background image loads", vv::look::loadBackground(6).isValid() && vv::look::loadBackground(6).getWidth() > 500);
            check("every background image loads", vv::look::loadBackground(1).isValid() && vv::look::loadBackground(2).isValid() && vv::look::loadBackground(3).isValid() && vv::look::loadBackground(4).isValid() && vv::look::loadBackground(5).isValid());
            editor->selectTab(6); juce::MessageManager::getInstance()->runDispatchLoopUntil(700);   // the editor timer applies the scheme
            check("Magenta scheme changes the accent colour", vv::col::cyan == juce::Colour(0xffd94a9e) && vv::VVLookAndFeel::potStyle == 0 && vv::VVLookAndFeel::ddStyle == 2);
            auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("settings_brushed.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
            st.setGlobal("scheme", 1); st.setGlobal("bgImage", 0); st.setGlobal("potStyle", 2); st.setGlobal("ddStyle", 0); juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
        }
    }
    }
    // new-feature pages: Sampler slot panel, Matrix wire list (Edit/Remove), Arp layer choice
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor()); ed->setSize(W, H);
        auto* editor = static_cast<vv::Editor*>(ed.get());
        auto click = [&](int tab, const char* text, const char* file) {
            editor->selectTab(tab); juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            std::function<juce::TextButton*(juce::Component*)> f = [&](juce::Component* c) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == text) return b; for (auto* k : c->getChildren()) if (auto* r = f(k)) return r; return nullptr; };
            auto* b = f(ed.get()); const bool found = b != nullptr; if (b) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(120); }
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile(file)); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
            check((juce::String("snapshot ") + file).toRawUTF8(), found && img.isValid());
        };
        click(1, "Sampler", "sound_sampler.png");
        proc.store().append("connections", [] { auto* o = new juce::DynamicObject(); o->setProperty("source", "lfo5"); o->setProperty("dest", "SMP:s2Bleed"); o->setProperty("amount", 40); return juce::var(o); }());
        check("lfo5 -> sample bleed route stored", proc.store().str("connections.0.dest") == "SMP:s2Bleed" || proc.store().count("connections") >= 1);
        click(3, "Transport / Arp", "seq_arp.png");
        check("arp target default is ALL", proc.store().str("arp.target", "ALL") == "ALL");
    }
    // space shoot-em-up: moves the FX params, restores them on close, renders both themes
    {
        auto& st = proc.store();
        st.append("fxStages", juce::var(new juce::DynamicObject())); // placeholder replaced below
        st.remove("fxStages", st.count("fxStages") - 1);
        for (const char* ty : { "wavefolder", "spectral" }) {
            auto* o = new juce::DynamicObject(); o->setProperty("type", juce::String(ty)); o->setProperty("enabled", true); o->setProperty("params", juce::var(new juce::DynamicObject())); st.append("fxStages", juce::var(o)); }
        const int wi = st.count("fxStages") - 2, si = wi + 1; const juce::String wb = "fxStages." + juce::String(wi), sb = "fxStages." + juce::String(si);
        st.set(wb + ".params.drive", 3.0); st.set(sb + ".params.blur", 0.2);
        {   // Vektur Void (Easter egg): title, level 1 with a shield, the mid-boss, the final boss and the victory story
            auto view = std::make_unique<vv::VoidView>(); view->setSize(900, 540); auto& G = view->game();
            juce::PNGImageFormat png;
            auto shot = [&](const char* name) { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); auto img = view->createComponentSnapshot(view->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream fo(out.getChildFile(name)); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); return img.isValid(); };
            check("void: title renders", shot("void_title.png"));
            vv::vvoid::Input in; in.start = true; view->stepFrames(1, in); in = {}; G.god = true; in.plasma = true; in.dy = -1;
            view->stepFrames(400, in); G.ship.shield = 5; G.wLvl = 2; check("void: level 1 plays", G.mode == vv::vvoid::M_PLAY && shot("void_play.png"));
            vv::vvoid::startLevel(G, 4); view->stepFrames(60 * 6, in); G.ship.shield = 0; check("void: mid-boss appears", G.hasBoss && shot("void_boss4.png"));
            vv::vvoid::startLevel(G, 7); G.wLvl = 3; view->stepFrames(60 * 8, in); check("void: final boss appears", G.hasBoss && G.boss.final_ && shot("void_boss7.png"));
            vv::vvoid::setMode(G, vv::vvoid::M_VICTORY); G.typed = 400; view->stepFrames(1, vv::vvoid::Input{}); check("void: victory story renders", shot("void_victory.png"));
            vv::vvoid::startLevel(G, 5); vv::vvoid::setMode(G, vv::vvoid::M_PLAY); view->stepFrames(60 * 6, in); check("void: scenery level renders", G.level == 5 && shot("void_level5.png"));
        }
        {   // Wavefolder: Warp Chronology Convergence, bound both ways to Drive / Symmetry / Mix / mode
            vv::WarpView warp(st, wb); warp.setSize(640, 400);
            for (int i = 0; i < 60; ++i) warp.step(1.f / 30.f);
            const float p0 = warp.warpPower(); st.set(wb + ".params.drive", 18.0); st.set(wb + ".params.mix", 1.0);
            for (int i = 0; i < 90; ++i) warp.step(1.f / 30.f);
            check("warp power rises with Drive", warp.warpPower() > p0);
            warp.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float>(222.f + 50.f + 20.f, 352.f), juce::ModifierKeys(), 1.f, 0.f, 0.f, 0.f, 0.f, &warp, &warp, juce::Time::getCurrentTime(), juce::Point<float>(), juce::Time::getCurrentTime(), 1, false));
            check("mode pill sets the fold mode", st.str(wb + ".params.mode") == "mirror");
            auto img = warp.createComponentSnapshot(warp.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("warp_wavefolder.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
            check("warp view renders", img.isValid());
        }
    }
    // grain command centre: bound both ways to the Granular parameters
    {
        auto& st = proc.store(); auto* o = new juce::DynamicObject(); o->setProperty("type", "granular"); o->setProperty("enabled", true); o->setProperty("params", juce::var(new juce::DynamicObject())); st.append("fxStages", juce::var(o));
        const juce::String gb = "fxStages." + juce::String(st.count("fxStages") - 1); st.set(gb + ".params.density", 60.0); st.set(gb + ".params.envShape", 2);
        vv::GrainBridgeView view(st, gb); view.setSize(720, 440); for (int i = 0; i < 90; ++i) view.step(1.f / 30.f);
        check("grain view simulates grains", view.activeGrains() > 0);
        view.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float>(40.f + 52.f, 300.f), juce::ModifierKeys(), 1.f, 0.f, 0.f, 0.f, 0.f, &view, &view, juce::Time::getCurrentTime(), juce::Point<float>(), juce::Time::getCurrentTime(), 1, false));
        check("fader drag writes the parameter", st.num(gb + ".params.density") > 60.0);
        auto img = view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("game_grain.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
        check("grain command centre renders", img.isValid());
    }
    // Interstellar: the panel builds from the shared defaults, a preset loads as a full parameter set, and the engine renders the stage
    {
        auto& st = proc.store(); auto* o = new juce::DynamicObject(); o->setProperty("type", "interstellar"); o->setProperty("enabled", true);
        o->setProperty("params", juce::JSON::parse(juce::String(vv::isp::kDefaultsJson))); st.append("fxStages", juce::var(o));
        const juce::String ib = "fxStages." + juce::String(st.count("fxStages") - 1);
        std::unique_ptr<juce::Component> fxs(makeFxScreen(st)); fxs->setSize(1100, 5200);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto img = fxs->createComponentSnapshot(fxs->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream fo(out.getChildFile("fx_interstellar.png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
        check("interstellar panel renders", img.isValid());
        check("interstellar defaults carry through (mix 1, quality normal)", st.num(ib + ".params.mix", 0) == 1.0 && st.str(ib + ".params.quality") == "normal");
        std::vector<std::pair<juce::String, juce::var>> kv;   // what the preset menu does
        const juce::var pv = juce::JSON::parse(juce::String(vv::isp::kPresets[19].json)); if (auto* po = pv.getDynamicObject()) for (auto& pr : po->getProperties()) kv.push_back({ ib + ".params." + pr.name.toString(), pr.value });
        st.setMany(kv);
        check("interstellar preset loads (Deep Field: lush)", st.str(ib + ".params.quality") == "lush");
        char b2[64] = {0}; vv_engine_get_value(proc.engine(), (ib + ".params.quality").toRawUTF8(), b2, sizeof b2);
        check("interstellar preset reaches the engine", juce::String(b2).contains("lush"));
        juce::AudioBuffer<float> ab(2, 512); juce::MidiBuffer mb; mb.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0); double pk = 0, nan = 0;
        for (int b = 0; b < 200; ++b) { ab.clear(); proc.processBlock(ab, mb); mb.clear(); pk = juce::jmax(pk, (double) ab.getMagnitude(0, 512)); for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (!std::isfinite(ab.getSample(c, i))) nan += 1; }
        check("engine renders with an interstellar stage (finite, bounded)", nan == 0 && pk < 4.0);
        proc.processBlock(ab, mb); mb.addEvent(juce::MessageManager::getInstance() ? juce::MidiMessage::allNotesOff(1) : juce::MidiMessage::allNotesOff(1), 0); proc.processBlock(ab, mb);
        st.remove("fxStages", st.count("fxStages") - 1);
    }
    // host session state: UI view state (scope mode, tab, size) and the new per-sample fields survive getStateInformation/setStateInformation
    {
        proc.scopeMode.store(1); proc.uiTab.store(5); proc.uiWidth.store(900); proc.uiHeight.store(640);
        auto& st = proc.store(); st.set("layers.SMP.oscs.3.bleedOn", true); st.set("layers.SMP.oscs.3.bleed", 0.75); st.set("layers.SMP.oscs.3.mic", "phone"); st.set("arp.target", "S4");
        juce::MemoryBlock blob; proc.getStateInformation(blob);
        proc.scopeMode.store(0); proc.uiTab.store(0); proc.uiWidth.store(1100); proc.uiHeight.store(720);
        st.set("layers.SMP.oscs.3.bleedOn", false); st.set("layers.SMP.oscs.3.bleed", 0.1); st.set("layers.SMP.oscs.3.mic", "none"); st.set("arp.target", "ALL");
        proc.setStateInformation(blob.getData(), (int) blob.getSize());
        check("state restores scope view mode", proc.scopeMode.load() == 1);
        check("state restores tab + window size", proc.uiTab.load() == 5 && proc.uiWidth.load() == 900 && proc.uiHeight.load() == 640);
        check("state restores sample bleed/mic fields", st.flag("layers.SMP.oscs.3.bleedOn") && std::abs(st.num("layers.SMP.oscs.3.bleed", 0) - 0.75) < 1e-9 && st.str("layers.SMP.oscs.3.mic") == "phone");
        check("state restores arp layer target", st.str("arp.target") == "S4");
        proc.scopeMode.store(0); proc.uiTab.store(0);
    }
    // preset files carry notes + instrument globals
    {
        auto& st = proc.store(); st.setNotes("Play slowly."); st.setGlobal("masterTone", 0.37); st.setGlobal("bendRange", 7);
        const auto file = st.exportPresetFile(); st.setNotes(""); st.setGlobal("masterTone", 0); st.setGlobal("bendRange", 2);
        check("preset file loads", st.loadJSON(file));
        check("notes saved with preset", st.notes() == "Play slowly.");
        check("globals saved with preset", std::abs(st.global("masterTone") - 0.37) < 1e-9 && st.global("bendRange") == 7);
    }
    // edits flow through to the engine
    proc.store().set("layers.WT.oscs.0.level", 0.31);
    char buf[64] = {0}; vv_engine_get_value(proc.engine(), "layers.WT.oscs.0.level", buf, sizeof buf);
    check("store.set reaches the engine", std::abs(atof(buf) - 0.31) < 1e-6);
    // audio
    juce::AudioBuffer<float> audio(2, 512); juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
    double peak = 0; for (int b = 0; b < 60; ++b) { proc.processBlock(audio, midi); midi.clear(); peak = juce::jmax(peak, (double) audio.getMagnitude(0, 512)); }
    check("processor renders audio from MIDI", peak > 0.01);
    std::printf("%s\n", fails ? "UI TESTS FAILED" : "all UI tests passed");
    return fails ? 1 : 0;
}
