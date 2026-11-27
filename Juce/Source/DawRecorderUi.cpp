#include "DawRecorderUi.h"
#include "Appearance.h"
#include "AuthoringTools.h"
#include "Widgets.h"
#include <thread>
namespace vv {

namespace {
struct Take { juce::String name, sampleName; juce::File file; };
std::vector<Take>& takes() { static std::vector<Take> t; return t; }   // this session's recordings
}

struct DawBlock::Impl {
    Impl(DawBlock& o, Store& s) : owner(o), store(s) {}
    DawBlock& owner; Store& store;
    juce::TextButton recB{ "REC" }, stopB{ "STOP" }, gearB{ "SET" }; juce::Label timeL; bool recording = false, busy = false; double elapsed = 0;
    int bits() const { return store.global("dawBits", 16) >= 24 ? 24 : 16; }
    int rate() const { return (int) store.global("dawRate", 48000); }
    int click() const { return juce::jlimit(0, 2, (int) store.global("dawClick", 0)); }
    VVEngine* eng() const { return store.engine(); }
    void applyClick() { if (auto* e = eng()) vv_daw_set_click(e, click()); }
    static juce::String fmt(double sec) { const int s = (int) sec; return juce::String(s / 60).paddedLeft('0', 2) + ":" + juce::String(s % 60).paddedLeft('0', 2); }
    void updateButtons() { recB.setToggleState(recording, juce::dontSendNotification); recB.setEnabled(!recording && !busy); stopB.setEnabled(recording); }
};

DawBlock::DawBlock(Store& s) : impl(new Impl(*this, s)) {
    auto& m = *impl; m.recB.setName("dawRec"); m.stopB.setName("dawStop");
    m.recB.setClickingTogglesState(false); m.recB.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc21a0a)); m.recB.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff6655));
    m.timeL.setText("00:00", juce::dontSendNotification); m.timeL.setFont(VVLookAndFeel::mono(12.f)); m.timeL.setColour(juce::Label::textColourId, col::cyan); m.timeL.setJustificationType(juce::Justification::centred);
    for (auto* c : std::initializer_list<juce::Component*>{ &m.recB, &m.stopB, &m.timeL, &m.gearB }) addAndMakeVisible(*c);
    m.recB.onClick = [this] { startRecording(); }; m.stopB.onClick = [this] { stopRecording(); };
    m.gearB.onClick = [this] {
        struct Pop : juce::Component {
            Pop(DawBlock::Impl& i) : im(i) {
                auto lab = [this](juce::Label& l, const char* t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); };
                lab(bL, "Bit depth"); lab(rL, "Sample rate"); lab(cL, "Click track");
                bits.addItem("16-bit", 16); bits.addItem("24-bit", 24); bits.setSelectedId(im.bits(), juce::dontSendNotification);
                rate.addItem("44.1 kHz", 44100); rate.addItem("48 kHz", 48000); rate.addItem("96 kHz", 96000); rate.setSelectedId(im.rate(), juce::dontSendNotification);
                click.addItem("Off", 1); click.addItem("Beep", 2); click.addItem("Rimshot", 3); click.setSelectedId(im.click() + 1, juce::dontSendNotification);
                bits.onChange = [this] { im.store.setGlobal("dawBits", bits.getSelectedId()); }; rate.onChange = [this] { im.store.setGlobal("dawRate", rate.getSelectedId()); };
                click.onChange = [this] { im.store.setGlobal("dawClick", click.getSelectedId() - 1); im.applyClick(); };
                for (auto* c : std::initializer_list<juce::Component*>{ &bits, &rate, &click }) addAndMakeVisible(*c);
                list.setModel(&model); model.owner = this; list.setRowHeight(26); addAndMakeVisible(list); lab(tL, "Takes (this session)");
                setSize(420, 250); resized();
            }
            void resized() override {
                auto r = getLocalBounds().reduced(10); auto row = [&](juce::Label& l, juce::Component& c) { auto q = r.removeFromTop(26); l.setBounds(q.removeFromLeft(100)); c.setBounds(q.removeFromLeft(140)); r.removeFromTop(4); };
                row(bL, bits); row(rL, rate); row(cL, click); tL.setBounds(r.removeFromTop(18)); list.setBounds(r);
            }
            void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff111418)); }
            struct Model : juce::ListBoxModel {
                Pop* owner = nullptr; int getNumRows() override { return (int) takes().size(); }
                void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
                juce::Component* refreshComponentForRow(int row, bool, juce::Component* ex) override {
                    delete ex; if (row < 0 || row >= (int) takes().size()) return nullptr;
                    struct Row : juce::Component {
                        Row(Pop* p, int r) : pop(p), idx(r) {
                            ed.setText(takes()[(size_t) r].name, false); save.setButtonText("SAVE AS"); del.setButtonText("X");
                            ed.onTextChange = [this] { rename(); }; save.onClick = [this] { saveAs(); }; del.onClick = [this] { remove(); };
                            addAndMakeVisible(ed); addAndMakeVisible(save); addAndMakeVisible(del);
                        }
                        void resized() override { auto r = getLocalBounds().reduced(1); del.setBounds(r.removeFromRight(28)); save.setBounds(r.removeFromRight(70)); ed.setBounds(r); }
                        void rename() { if (idx >= (int) takes().size()) return; auto& t = takes()[(size_t) idx]; t.name = ed.getText(); }
                        void saveAs() {
                            if (idx >= (int) takes().size()) return; chooser = std::make_shared<juce::FileChooser>("Save recording", takes()[(size_t) idx].file, "*.wav");
                            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, alive = juce::Component::SafePointer<Row>(this)](const juce::FileChooser& fc) {
                                if (!alive) return; auto f = fc.getResult(); if (f != juce::File() && idx < (int) takes().size()) takes()[(size_t) idx].file.copyFileTo(f.withFileExtension("wav")); }); }
                        void remove() {
                            if (idx >= (int) takes().size()) return; auto t = takes()[(size_t) idx]; t.file.deleteFile();
                            auto& st = pop->im.store; auto lib = st.get("sampleLibrary");
                            if (lib.isArray() && t.sampleName.isNotEmpty()) for (int i = 0; i < lib.size(); ++i) if (lib[i].getProperty("name", {}).toString() == t.sampleName) { st.remove("sampleLibrary", i); break; }
                            takes().erase(takes().begin() + idx); pop->list.updateContent(); }
                        Pop* pop; int idx; juce::TextEditor ed; juce::TextButton save, del; std::shared_ptr<juce::FileChooser> chooser;
                    };
                    return new Row(owner, row);
                }
            } model;
            DawBlock::Impl& im; juce::Label bL, rL, cL, tL; juce::ComboBox bits, rate, click; juce::ListBox list;
        };
        juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(new Pop(*impl)), impl->gearB.getScreenBounds(), nullptr); };
    m.updateButtons(); m.applyClick(); startTimerHz(10);
}
DawBlock::~DawBlock() { stopTimer(); if (impl->recording) if (auto* e = impl->store.engine()) vv_daw_stop(e); }
void DawBlock::resized() { auto r = getLocalBounds(); auto& m = *impl; m.recB.setBounds(r.removeFromLeft(46).reduced(1)); m.stopB.setBounds(r.removeFromLeft(50).reduced(1)); m.timeL.setBounds(r.removeFromLeft(54)); m.gearB.setBounds(r.removeFromLeft(44).reduced(1)); }
void DawBlock::paint(juce::Graphics&) {}
bool DawBlock::isRecording() const { return impl->recording; }
int DawBlock::numTakes() const { return (int) takes().size(); }
juce::File DawBlock::takeFile(int i) const { return i >= 0 && i < (int) takes().size() ? takes()[(size_t) i].file : juce::File(); }
juce::File DawBlock::recordingsFolder() { return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Vektur Vortex Recordings"); }

void DawBlock::startRecording() {
    auto& m = *impl; auto* e = m.eng(); if (!e || m.recording || m.busy) return;
    vv_daw_set_click(e, m.click()); vv_daw_start(e); m.recording = true; m.elapsed = 0; m.updateButtons();
}
void DawBlock::stopRecording() {
    auto& m = *impl; auto* e = m.eng(); if (!e || !m.recording) return;
    vv_daw_stop(e); m.recording = false; m.busy = true; m.updateButtons(); m.timeL.setText("...", juce::dontSendNotification);
    const double secs = vv_daw_drain(e); const int bits = m.bits(), rate = m.rate(); const int dropped = vv_daw_dropped(e);
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"); const juce::String name = "Recording " + stamp;
    juce::Component::SafePointer<DawBlock> alive(this); Store* st = &m.store;
    std::thread([alive, e, st, secs, bits, rate, dropped, name] {   // resampling a long take takes a moment: off the message thread
        const size_t n = vv_daw_take_wav(e, bits, rate, nullptr, 0); juce::MemoryBlock wav; wav.setSize(n);
        if (n) vv_daw_take_wav(e, bits, rate, (uint8_t*) wav.getData(), n);
        juce::MemoryBlock lib; if (secs <= 60.0 && n) { const size_t k = vv_daw_take_wav(e, 16, 0, nullptr, 0); lib.setSize(k); vv_daw_take_wav(e, 16, 0, (uint8_t*) lib.getData(), k); }
        juce::MessageManager::callAsync([alive, st, secs, dropped, name, wav, lib] {
            if (!alive) return; auto& im = *alive->impl; im.busy = false; im.timeL.setText("00:00", juce::dontSendNotification);
            if (wav.getSize() == 0) { im.updateButtons(); return; }
            auto dir = recordingsFolder(); dir.createDirectory(); auto f = dir.getNonexistentChildFile(name, ".wav"); f.replaceWithData(wav.getData(), wav.getSize());
            Take t; t.name = name; t.file = f;
            if (lib.getSize()) { t.sampleName = authoring::uniqueSampleName(*st, name); authoring::putSample(*st, t.sampleName, lib); }   // usable straight away as a sampler sample
            takes().push_back(t); im.updateButtons();
            if (dropped > 0) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Recording", "The recorder fell behind and dropped " + juce::String(dropped) + " samples. The take is saved, but may have a gap.");
        });
    }).detach();
}
void DawBlock::timerCallback() {
    auto& m = *impl; auto* e = m.eng(); if (!e) return;
    if (m.recording) { m.elapsed = vv_daw_drain(e); m.timeL.setText(Impl::fmt(m.elapsed), juce::dontSendNotification); if (vv_daw_state(e) == 2) { m.recording = true; stopRecording(); } }
}
}
