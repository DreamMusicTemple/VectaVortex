// Grain Cloud Command Centre: interactive control-room animation for the Granular FX (port of the web app's buildGrainBridgeUI).
// Seven faders + four envelope rockers are bound both ways to fxStages.N.params.* in the Store.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PowerSettings.h"
#include "Store.h"

namespace vv {

class GrainBridgeView : public juce::Component, private juce::Timer {
public:
    GrainBridgeView(Store& s, juce::String stageBase, std::function<float()> inputLevel = {});
    ~GrainBridgeView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { drag = -1; repaint(); }
    int activeGrains() const { return (int) grains.size(); }
    void step(float dt);                           // advances the simulation (also used by the headless test)
    static constexpr float W = 720.f, H = 440.f;
private:
    struct Spec { const char* key; const char* label; double lo, hi; bool exp; double step; int kind; };
    struct Grain { int tile; float age, dur, pos, pan, rate; };
    void timerCallback() override { VV_UI_THROTTLE(30); step(1.f / 30.f * (float) vv::uiDivider()); repaint(); }
    double get(int i) const; void setFromY(int i, float y);
    juce::Point<float> toLocal(juce::Point<int> p) const;
    float env(float pos, int shape) const;
    Store& store; juce::String base; std::function<float()> levelFn;
    std::vector<Grain> grains; float spawnAcc = 0, level = 0; std::array<float, 180> hist{}; int histPos = 0; float histAcc = 0; int drag = -1; juce::Random rng;
};

} // namespace vv
