// Draggable-band EQ graph, the same as the web app's Vektur Vocal-method equaliser: LOW shelf / BELL / BELL / HIGH shelf as numbered nodes 1-4 on a magenta response curve.
// X = frequency (log, drawn 30 Hz..20 kHz, dragged within 40..16000 Hz), Y = gain +-15 dB in 0.5 steps (+-18 dB drawn range). Nearest band by horizontal distance, double-click resets a gain, arrow up/down nudge 0.5 dB.
// (The web also draws a live spectrum behind the curve; the plugin has no analyser tap yet, so that part is not ported.) Bound to <layerPath>.eq.band1..4.{freq,gain}.
#pragma once
#include "Widgets.h"
namespace vv {
class EqGraph : public Control {
public:
    EqGraph(Store& s, const juce::String& layerPath);
    void refresh() override;
    juce::Point<int> preferredSize() const override { return { 600, 214 }; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override; void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override { }
    double freq[4], gain[4];
    static double magnitudeDb(const double* f, const double* g, double hz, double sr = 48000.0);   // RBJ cascade, same coefficients as the engine EQ
private:
    float xOf(double hz) const; float yOf(double db) const; double hzAt(float x) const; double dbAt(float y) const;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedBottom(24.f); }   // the bottom strip is the readout line
    int hit(juce::Point<float>) const;
    juce::String base; int drag = -1, lastBand = 0;
};
}
