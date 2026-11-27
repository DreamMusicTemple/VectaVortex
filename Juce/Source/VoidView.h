// Vektur Void, the hidden Easter-egg shoot-em-up. Rules: Core/include/vv/vektur_void.hpp (verified frame for frame against the web game).
// Purely its own game: it never touches an audio parameter. Opened from the EASTER EGG button at the bottom of Settings.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "vv/vektur_void.hpp"

namespace vv {

class VoidView : public juce::Component, private juce::Timer {
public:
    VoidView();
    ~VoidView() override;
    vvoid::Game& game() { return G; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    // Opens the game in its own modal window (X closes it completely; the next open builds a fresh game).
    static void openWindow(juce::Component* owner);
    void stepFrames(int n, const vvoid::Input& in) { for (int i = 0; i < n; i++) vvoid::step(G, in, 1.0 / 60, [this] { return rng(); }); }   // tests
private:
    void timerCallback() override;
    juce::AffineTransform toScreen() const;
    juce::Point<float> toGame(juce::Point<int>) const;
    void drawShip(juce::Graphics&, float x, float y, float thrust) const;
    void drawEnemy(juce::Graphics&, const vvoid::Enemy&) const;
    void drawBoss(juce::Graphics&) const;
    void panel(juce::Graphics&, const juce::StringArray& lines) const;
    void button(juce::Graphics&, const juce::String& label, float y) const;
    bool menuHit(juce::Point<float>) const;
    vvoid::Game G; vvoid::Lcg rng; double last = 0, acc = 0;
    float stickX = 0, stickY = 0; bool plasmaHeld = false, bombHeld = false, startPulse = false; int stickPtr = -1, plasmaPtr = -1, bombPtr = -1;
    mutable juce::Random rand;
};

} // namespace vv
