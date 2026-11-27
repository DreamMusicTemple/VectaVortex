// Warp Chronology Convergence: the Wavefolder FX's animation (port of the web app's buildWavefolderWarpUI).
// Drive / Symmetry / Fold mode / Mix drive a four-phase thruster and warp-vortex scene; drag inside a phase panel to turn its control,
// click a mode pill to choose the fold mode. Bound both ways to fxStages.N.params.* in the Store.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PowerSettings.h"
#include "Store.h"

namespace vv {

class WarpView : public juce::Component, private juce::Timer {
public:
    WarpView(Store& s, juce::String stageBase);
    ~WarpView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent& e) override { if (panelDrag >= 0) applyDrag(toScene(e.position)); }
    void mouseUp(const juce::MouseEvent&) override { panelDrag = -1; }
    void step(float dt);                       // advances the scene (also used by the headless test)
    float warpPower() const { return power; }
    static constexpr float W = 640.f, H = 400.f;
private:
    void timerCallback() override { VV_UI_THROTTLE(30); step(1.f / 30.f * (float) vv::uiDivider()); repaint(); }
    juce::Point<float> toScene(juce::Point<float> p) const;
    void applyDrag(juce::Point<float> p);
    juce::AffineTransform fit() const;
    Store& store; juce::String base;
    struct Star { float x, y, r, ph; };
    std::vector<Star> stars;
    float cd = 0, cs = 0, cm = 1, cw = 0.5f, spin = 0, t = 0, power = 0.1f; int panelDrag = -1; juce::String mode = "fold";
    float drive = 3, sym = 0, mix = 1, pwm = 0.5f;
};

} // namespace vv
