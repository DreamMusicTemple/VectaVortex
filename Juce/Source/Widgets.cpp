#include "Widgets.h"
#include "NavIcons.h"
#include "ProPlot.h"
#include "CurveEditor.h"
#include <cmath>

namespace vv {

const juce::StringArray kFormantTables{ "classic", "male", "female" };
const juce::StringArray kFormantVowels{ "a", "e", "i", "o", "u", "ae", "er", "schwa", "ih", "uh", "oh", "turn", "y", "oe" };
const juce::StringArray kFilterTypes{ "moog4pole", "diode4pole", "onepole6db", "twopole12db", "statevariable", "comb", "highpass", "bandpass", "formant", "lpg", "notch", "isem", "sid6581", "sid8580" };

void VVLookAndFeel::refreshAccent() {
    setColour(juce::Slider::thumbColourId, col::cyan); setColour(juce::ComboBox::arrowColourId, col::cyan); setColour(juce::PopupMenu::highlightedBackgroundColourId, col::cyanDim);
    setColour(juce::TextButton::buttonOnColourId, col::cyanDim); setColour(juce::TabbedButtonBar::frontTextColourId, col::cyan);
}
void VVLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    const auto b = juce::Rectangle<float>(0.f, 0.f, (float) w, (float) h).reduced(0.5f); const float rad = ddStyle == 0 ? 5.f : 1.5f;
    g.setColour(col::bg); g.fillRoundedRectangle(b, rad);   // Vektur: dark well with an inner shadow
    g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawLine(b.getX() + rad, b.getY() + 1.5f, b.getRight() - rad, b.getY() + 1.5f, 2.f);
    if (ddStyle == 2) { g.setColour(col::cyan.withAlpha(0.25f)); g.drawRoundedRectangle(b.expanded(1.f), rad, 3.f); g.setColour(col::cyan); } else g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(b, rad, 1.f);
    juce::Path p; const float ax = (float) w - 16.f, ay = (float) h * 0.5f; p.startNewSubPath(ax - 4.f, ay - 2.f); p.lineTo(ax, ay + 2.f); p.lineTo(ax + 4.f, ay - 2.f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.f : 0.3f)); g.strokePath(p, juce::PathStrokeType(2.f));
}
VVLookAndFeel::VVLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, col::bg);
    setColour(juce::Label::textColourId, col::ink);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff2b2e35));
    setColour(juce::Slider::thumbColourId, col::cyan);
    setColour(juce::ComboBox::backgroundColourId, col::panel2);
    setColour(juce::ComboBox::textColourId, col::cyan);
    setColour(juce::ComboBox::outlineColourId, col::line);
    setColour(juce::ComboBox::arrowColourId, col::cyan);
    setColour(juce::PopupMenu::backgroundColourId, col::panel);
    setColour(juce::PopupMenu::textColourId, col::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::cyanDim);
    setColour(juce::TextButton::buttonColourId, col::panel2);
    setColour(juce::TextButton::textColourOffId, col::ink);
    setColour(juce::TextButton::buttonOnColourId, col::cyanDim);
    setColour(juce::TextEditor::backgroundColourId, col::panel2);
    setColour(juce::TextEditor::textColourId, col::ink);
    setColour(juce::TextEditor::outlineColourId, col::line);
    setColour(juce::ScrollBar::thumbColourId, col::line);
    setColour(juce::TabbedButtonBar::tabTextColourId, col::dim);
    setColour(juce::TabbedButtonBar::frontTextColourId, col::cyan);
    setColour(juce::ListBox::backgroundColourId, col::panel);
    setColour(juce::ListBox::textColourId, col::ink);
    setDefaultSansSerifTypefaceName(juce::Font::getDefaultMonospacedFontName());
}

// Vektur Interstellar dial (shared design with the web and Swift apps): a ring of dots that grows from pin-pricks to large bright dots
// clockwise up to the value, a knurled top-lit skirt, a cap with a specular kiss and a white pointer. The Rustic scheme keeps its own look.
static void drawInterstellarDial(juce::Graphics& g, int x, int y, int w, int h, float pos, juce::Slider& sl, juce::Colour light, bool active) {
    const float size = (float) juce::jmin(w, h);
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    int steps = 0; const double iv = sl.getInterval(), span = sl.getMaximum() - sl.getMinimum();
    if (iv > 0 && span / iv <= 15.0) steps = (int) std::lround(span / iv) + 1;
    const int n = (steps > 1 && steps <= 16) ? steps : (size < 50.f ? 15 : 21);
    const float minR = juce::jmax(0.6f, size * 0.011f), maxR = juce::jmax(1.8f, size * 0.047f), R = size * 0.5f - maxR - 0.5f;
    auto ptAt = [&](float u) { const float a = juce::degreesToRadians(135.f + 270.f * u); return juce::Point<float>(cx + R * std::cos(a), cy + R * std::sin(a)); };
    auto dot = [&](juce::Point<float> p, float r) { g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r); };
    const float posD = pos * (float) (n - 1);
    for (int i = 0; i < n; ++i) {
        const float u = n == 1 ? 0.f : (float) i / (float) (n - 1), reveal = juce::jlimit(0.f, 1.f, posD - (float) i + 1.f); const auto p = ptAt(u);
        g.setColour(juce::Colours::white.withAlpha(0.13f)); dot(p, minR * 0.8f);
        if (reveal <= 0.f) continue;
        const float r = minR * 0.8f + (minR + (maxR - minR) * std::pow(u, 1.15f) - minR * 0.8f) * reveal;
        if (active || u > 0.7f) { g.setColour(light.withAlpha(0.10f * reveal * (active ? 1.6f : 1.f))); dot(p, r * 2.1f); }
        g.setColour(light.withAlpha((0.45f + 0.55f * u) * reveal)); dot(p, r);
        g.setColour(juce::Colours::white.withAlpha(u * 0.75f * reveal)); dot(p, r * 0.62f);
    }
    const float skIn = size * 0.15f, skR = size * 0.5f - skIn;
    { juce::DropShadow sh(juce::Colours::black.withAlpha(0.8f), (int) (size * 0.07f), { 0, (int) (size * 0.06f) }); juce::Path pc; pc.addEllipse(cx - skR, cy - skR, 2 * skR, 2 * skR); sh.drawForPath(g, pc); }
    juce::ColourGradient sk(juce::Colour::fromFloatRGBA(0.30f, 0.30f, 0.30f, 1.f), cx - skR, cy - skR, juce::Colour::fromFloatRGBA(0.09f, 0.09f, 0.09f, 1.f), cx + skR, cy + skR, false);
    sk.addColour(0.35, juce::Colour::fromFloatRGBA(0.12f, 0.12f, 0.12f, 1.f)); sk.addColour(0.6, juce::Colour::fromFloatRGBA(0.26f, 0.26f, 0.26f, 1.f));
    g.setGradientFill(sk); g.fillEllipse(cx - skR, cy - skR, 2 * skR, 2 * skR);
    { juce::Graphics::ScopedSaveState ss(g); juce::Path clip; clip.addEllipse(cx - skR, cy - skR, 2 * skR, 2 * skR); g.reduceClipRegion(clip);
      g.setGradientFill(juce::ColourGradient(light.withAlpha(0.38f), cx, cy - skR, light.withAlpha(0.f), cx, cy, false)); g.fillRect(cx - skR, cy - skR, 2 * skR, skR); }
    g.setGradientFill(juce::ColourGradient(light.withAlpha(0.75f), cx, cy - skR, light.withAlpha(0.f), cx, cy + skR, false));
    g.drawEllipse(cx - skR, cy - skR, 2 * skR, 2 * skR, juce::jmax(0.8f, size * 0.012f));
    const float capR = skR - size * 0.085f;
    g.setGradientFill(juce::ColourGradient(juce::Colour::fromFloatRGBA(0.24f, 0.24f, 0.24f, 1.f), cx, cy - capR * 0.64f, juce::Colour::fromFloatRGBA(0.05f, 0.05f, 0.05f, 1.f), cx, cy + size * 0.2f, true));
    g.fillEllipse(cx - capR, cy - capR, 2 * capR, 2 * capR);
    g.setGradientFill(juce::ColourGradient(light.withAlpha(0.45f), cx, cy - size * 0.17f, light.withAlpha(0.f), cx + size * 0.16f, cy - size * 0.17f, true));
    g.fillEllipse(cx - size * 0.17f, cy - size * 0.17f - size * 0.07f, size * 0.34f, size * 0.14f);
    const float ang = juce::degreesToRadians(-135.f + 270.f * pos);
    juce::Path ptr; ptr.addRoundedRectangle(-juce::jmax(1.f, size * 0.02f), -(size * 0.5f - skIn - size * 0.04f), juce::jmax(2.f, size * 0.04f), size * 0.16f, juce::jmax(1.f, size * 0.02f));
    g.setColour(juce::Colours::white.withAlpha(0.92f)); g.fillPath(ptr, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void VVLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& slider) {
    if (potStyle != 3) { drawInterstellarDial(g, x, y, w, h, pos, slider, (juce::Colour) col::cyan, slider.isMouseButtonDown()); return; }
    const float d = (float) juce::jmin(w, h) - 8.f, r = d * 0.5f;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const auto accent = potStyle == 3 ? juce::Colour(0xffc96b4a) : (juce::Colour) col::cyan;
    const float lw = potStyle == 0 ? 2.f : 3.f;
    juce::Path track; track.addCentredArc(cx, cy, r, r, 0.f, a0, a1, true);
    if (potStyle != 1) { g.setColour(col::line); g.strokePath(track, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    juce::Path val; val.addCentredArc(cx, cy, r, r, 0.f, a0, ang, true);
    if (potStyle == 2) { g.setColour(accent.withAlpha(0.28f)); g.strokePath(val, juce::PathStrokeType(7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }   // Future: glowing ring
    g.setColour(accent); g.strokePath(val, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (potStyle == 1) { juce::Path t; t.addRectangle(-1.2f, -(r - 2.f), 2.4f, 7.f); g.setColour(col::ink); g.fillPath(t, juce::AffineTransform::rotation(ang).translated(cx, cy)); return; }   // Simple: ring + pointer only
    g.setColour(col::panel2); g.fillEllipse(cx - r + 6, cy - r + 6, 2 * r - 12, 2 * r - 12);
    if (potStyle == 0) { g.setColour(col::line); g.drawEllipse(cx - r + 6, cy - r + 6, 2 * r - 12, 2 * r - 12, 1.f); }
    if (potStyle == 3) { g.setColour(juce::Colour(0xff6b3520)); g.drawEllipse(cx - r + 6, cy - r + 6, 2 * r - 12, 2 * r - 12, 2.f); }
    juce::Path tick; tick.addRectangle(-1.2f, -(r - 7.f), 2.4f, r * 0.45f);
    g.setColour(col::ink); g.fillPath(tick, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void VVLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider&) {
    if (st == juce::Slider::LinearVertical) {
        g.setColour(col::line); g.fillRoundedRectangle((float) x + (float) w * 0.5f - 2.f, (float) y, 4.f, (float) h, 2.f);
        g.setColour(col::cyan); g.fillRoundedRectangle((float) x + 2.f, pos - 6.f, (float) w - 4.f, 12.f, 3.f);
    } else {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour(col::line); g.fillRoundedRectangle((float) x, cy - 2.f, (float) w, 4.f, 2.f);
        g.setColour(col::cyan); g.fillRoundedRectangle((float) x, cy - 2.f, pos - (float) x, 4.f, 2.f);
        g.fillEllipse(pos - 7.f, cy - 7.f, 14.f, 14.f);
    }
}

void VVLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    if (b.getProperties().contains("sendChip")) {   // FX send source pill (web .fx-chip): LED lit + cyan edge = this source feeds the effect
        auto c = r.reduced(1.f); const float rad = c.getHeight() / 2.f; const bool xfx = b.getProperties()["sendChip"].toString() == "XFX";
        const auto led = xfx ? col::red : col::cyan;
        g.setColour(juce::Colour(0xff0e1014)); g.fillRoundedRectangle(c, rad);
        if (on) { g.setColour(col::cyan.withAlpha(0.14f)); g.fillRoundedRectangle(c, rad); }
        g.setColour(on ? col::cyan.withAlpha(0.75f) : over ? juce::Colour(0xff4b5263) : juce::Colour(0xff2f3543)); g.drawRoundedRectangle(c, rad, 1.f);
        const float cx = c.getX() + rad * 0.9f + 3.f, cy = c.getCentreY();
        if (on) { g.setColour(led.withAlpha(0.28f)); g.fillEllipse(cx - 6.f, cy - 6.f, 12.f, 12.f); g.setColour(led); g.fillEllipse(cx - 3.5f, cy - 3.5f, 7.f, 7.f); }
        else { g.setColour(xfx ? led : juce::Colour(0xff4b5263)); g.drawEllipse(cx - 3.f, cy - 3.f, 6.f, 6.f, 1.4f); }
        g.setColour(on ? juce::Colour(0xffe4f9fb) : over ? juce::Colour(0xffb3bac9) : juce::Colour(0xff7d869a)); g.setFont(mono(10.5f).boldened());
        g.drawText(b.getButtonText(), c.withTrimmedLeft(rad * 0.9f + 12.f).toNearestInt(), juce::Justification::centredLeft, false);
        return;
    }
    auto sw = r.removeFromLeft(34.f).withSizeKeepingCentre(32.f, 16.f);
    g.setColour(on ? col::cyanDim : col::line); g.fillRoundedRectangle(sw, 8.f);
    g.setColour(on ? col::cyan : col::dim); g.fillEllipse(sw.getX() + (on ? 16.f : 2.f), sw.getY() + 2.f, 12.f, 12.f);
    const bool pwr = b.getProperties().contains("pwr");   // stage power switch: reads ON (cyan) / OFF
    g.setColour(pwr ? (on ? col::cyan : col::dim) : over ? col::ink : col::dim); g.setFont(pwr ? mono(11.f).boldened() : mono(12.f));
    g.drawText(pwr ? juce::String(on ? "ON" : "OFF") : b.getButtonText(), r.withTrimmedLeft(6.f).toNearestInt(), juce::Justification::centredLeft);
}

void VVLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.f); const bool on = b.getToggleState();
    if (b.getProperties().contains("tabBar")) {   // icon-only section tab: dark recessed face; selected = faint cyan tint + cyan edge
        g.setGradientFill(juce::ColourGradient(juce::Colour(down ? 0xff090a0e : 0xff0d0f14), 0.f, r.getY(), juce::Colour(down ? 0xff0c0e12 : 0xff14171e), 0.f, r.getBottom(), false)); g.fillRoundedRectangle(r, 6.f);
        if (on) { g.setGradientFill(juce::ColourGradient(col::cyan.withAlpha(0.14f), 0.f, r.getY(), col::cyan.withAlpha(0.06f), 0.f, r.getBottom(), false)); g.fillRoundedRectangle(r, 6.f); }
        g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawLine(r.getX() + 5.f, r.getY() + 1.f, r.getRight() - 5.f, r.getY() + 1.f, 1.f);   // recessed top shadow
        g.setColour(on ? col::cyan.withAlpha(0.8f) : over ? col::cyan.withAlpha(0.45f) : juce::Colour(0xff2a2f3d)); g.drawRoundedRectangle(r, 6.f, 1.f);
        return;
    }
    const auto onC = b.findColour(juce::TextButton::buttonOnColourId), offC = b.findColour(juce::TextButton::buttonColourId);
    const auto lit = onC == col::cyanDim ? col::cyan : onC.brighter(0.7f);   // LED / edge colour of an "on" button
    const auto top = on ? onC.interpolatedWith(lit, 0.18f).withMultipliedBrightness(0.9f) : offC.brighter(over ? 0.18f : 0.06f);
    const auto bot = on ? onC.withMultipliedBrightness(0.6f) : offC.darker(0.15f);
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillRoundedRectangle(r.translated(0.f, 1.2f), 5.f);   // drop shadow
    g.setGradientFill(juce::ColourGradient(down ? bot : top, 0.f, r.getY(), down ? top : bot, 0.f, r.getBottom(), false)); g.fillRoundedRectangle(r, 5.f);
    g.setColour(juce::Colours::white.withAlpha(0.06f)); g.drawLine(r.getX() + 4.f, r.getY() + 1.f, r.getRight() - 4.f, r.getY() + 1.f, 1.f);   // top highlight
    g.setColour(on ? lit.withAlpha(0.8f) : col::edge); g.drawRoundedRectangle(r, 5.f, 1.f);
    if (on && b.getClickingTogglesState()) { const float d = 6.f, cx = r.getX() + 9.f, cy = r.getCentreY(); g.setColour(lit.withAlpha(0.35f)); g.fillEllipse(cx - d, cy - d, 2 * d, 2 * d); g.setColour(lit); g.fillEllipse(cx - 3.f, cy - 3.f, 6.f, 6.f); }   // LED
}

void VVLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool down) {
    if (!b.getProperties().contains("navIcon")) { juce::LookAndFeel_V4::drawButtonText(g, b, over, down); return; }
    const bool on = b.getToggleState();
    const auto c = on ? col::cyan : over ? juce::Colour(0xffc3cad8) : juce::Colour(0xff8f99ab);   // selected cyan, hover brighter silver, idle muted silver
    const float side = juce::jmin(24.f, (float) b.getHeight() - 8.f, (float) b.getWidth() - 8.f);
    navicon::draw(g, (navicon::Id) (int) b.getProperties()["navIcon"], b.getLocalBounds().toFloat().withSizeKeepingCentre(side, side).translated(0.f, down ? 0.5f : 0.f), c);
}

std::vector<std::pair<juce::String, double>> kEnvRepeatChoices() {
    std::vector<std::pair<juce::String, double>> c; c.push_back({ "Off", 0 });
    for (int i = 1; i <= 8; ++i) c.push_back({ juce::String(i) + (i == 1 ? " repeat" : " repeats"), (double) i });
    c.push_back({ "Continuous", -1 }); return c;
}
// ---------------------------------------------------------------- VFader
VFader::VFader(Store& s, const juce::String& label, const juce::String& path, double lo, double hi, double def, KnobOpts o)
    : Control(s, path), def_(def), o_(o) {
    name_.setText(label, juce::dontSendNotification); name_.setJustificationType(juce::Justification::centred);
    name_.setFont(VVLookAndFeel::mono(11.f)); name_.setColour(juce::Label::textColourId, col::dim); name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 16);
    slider.setRange(lo, hi, 0);
    if (o.log && lo > 0) slider.setSkewFactorFromMidPoint(std::sqrt(lo * hi));
    slider.setDoubleClickReturnValue(true, def);
    slider.setNumDecimalPlacesToDisplay(o.decimals);
    slider.textFromValueFunction = [this](double v) { return juce::String::formatted(o_.fmt.toRawUTF8(), v) + o_.unit; };
    slider.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.-eE").getDoubleValue(); };
    slider.onValueChange = [this] { if (!updating_) store.set(path_, slider.getValue()); };
    addAndMakeVisible(slider);
    refresh();
}
void VFader::refresh() {
    const double v = store.num(path_, def_);
    if (std::abs(slider.getValue() - v) > 1e-9 && !slider.isMouseButtonDown()) { updating_ = true; slider.setValue(v, juce::dontSendNotification); updating_ = false; }
    slider.updateText();
}
void VFader::resized() { auto r = getLocalBounds(); name_.setBounds(r.removeFromTop(16)); slider.setBounds(r); }

// ---------------------------------------------------------------- Knob
Knob::Knob(Store& s, const juce::String& label, const juce::String& path, double lo, double hi, double def, KnobOpts o)
    : Control(s, path), def_(def), o_(o) {
    label_ = label; name_.setText(label, juce::dontSendNotification); name_.setJustificationType(juce::Justification::centred);
    name_.setFont(VVLookAndFeel::mono(11.f)); name_.setColour(juce::Label::textColourId, col::dim); name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 16);
    slider.setRange(lo, hi, 0);
    if (o.log && lo > 0) slider.setSkewFactorFromMidPoint(std::sqrt(lo * hi));
    slider.setDoubleClickReturnValue(true, def);
    slider.setRotaryParameters(juce::degreesToRadians(225.f), juce::degreesToRadians(495.f), true);
    slider.setNumDecimalPlacesToDisplay(o.decimals);
    slider.textFromValueFunction = [this](double v) { return juce::String::formatted(o_.fmt.toRawUTF8(), v) + o_.unit; };
    slider.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.-eE").getDoubleValue(); };
    slider.onValueChange = [this] { if (!updating_) store.set(path_, slider.getValue()); };
    addAndMakeVisible(slider); addAndMakeVisible(shield_); slider.addMouseListener(&longPress_, true);
    refresh();
}
void Knob::paintOverChildren(juce::Graphics& g) { if (modOn_) { g.setColour(col::cyan); g.fillEllipse((float) getWidth() - 12.f, 3.f, 7.f, 7.f); } }
void Knob::openModMenu() {
    if (modDest_.isEmpty()) return;
    static const char* const src[] = { "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "note", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow", "macro1", "macro2", "macro3", "macro4", "xy1x", "xy1y", "xy2x", "xy2y", "lorenz", "rossler" };
    juce::PopupMenu m; m.addSectionHeader("Modulate: " + label_);
    for (int i = 0; i < (int) std::size(src); ++i) m.addItem(i + 1, src[i]);
    if (modOn_) { m.addSeparator(); m.addItem(1000, "Remove all routes to this dial"); }
    juce::Component::SafePointer<Knob> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [self](int r) {
        if (!self || r <= 0) return; auto& st = self->store; const auto dest = self->modDest_;
        if (r == 1000) { for (int i = st.count("connections") - 1; i >= 0; --i) if (st.str("connections." + juce::String(i) + ".dest") == dest) st.remove("connections", i); }
        else { auto* o = new juce::DynamicObject(); o->setProperty("source", src[r - 1]); o->setProperty("dest", dest); o->setProperty("amount", 30); st.append("connections", juce::var(o)); }
        self->refresh(); });
}
void Knob::refresh() {
    if (modDest_.isNotEmpty()) { bool has = false; const int n = store.count("connections"); for (int i = 0; i < n && !has; ++i) has = store.str("connections." + juce::String(i) + ".dest") == modDest_; if (has != modOn_) { modOn_ = has; repaint(); } }
    const double v = store.num(path_, def_);
    if (std::abs(slider.getValue() - v) > 1e-9 && !slider.isMouseButtonDown()) { updating_ = true; slider.setValue(v, juce::dontSendNotification); updating_ = false; }
    slider.updateText();
}
bool Knob::labelNeedsTwoLines() const { return label_.containsChar(' ') && juce::GlyphArrangement::getStringWidthInt(VVLookAndFeel::mono(11.f), label_) > preferredSize().x - 6; }
void Knob::setTwoLine(bool b) {
    if (b == twoLine_) return; twoLine_ = b; juce::String t = label_;
    if (b) { const int i = t.indexOfChar(' '); if (i >= 0) t = t.replaceSection(i, 1, "\n"); }
    name_.setText(t, juce::dontSendNotification); name_.setJustificationType(b ? juce::Justification::centredBottom : juce::Justification::centred); resized();
}
void Knob::resized() { auto r = getLocalBounds(); name_.setBounds(r.removeFromTop(twoLine_ ? 28 : 16)); slider.setBounds(r); shield_.setBounds(r); }

// ---------------------------------------------------------------- Toggle
Toggle::Toggle(Store& s, const juce::String& label, const juce::String& path, bool def) : Control(s, path), def_(def) {
    button.setButtonText(label);
    button.onClick = [this] { if (!updating_) { store.set(path_, button.getToggleState()); if (onChanged) onChanged(button.getToggleState()); } };
    addAndMakeVisible(button); refresh();
}
void Toggle::refresh() { updating_ = true; button.setToggleState(store.flag(path_, def_), juce::dontSendNotification); updating_ = false; }

// ---------------------------------------------------------------- Choice
Choice::Choice(Store& s, const juce::String& label, const juce::String& path, const juce::StringArray& options, const juce::String& def)
    : Control(s, path), options_(options), defStr_(def.isEmpty() && options.size() ? options[0] : def) { init(label); }
Choice::Choice(Store& s, const juce::String& label, const juce::String& path, std::vector<std::pair<juce::String, double>> numeric, double def)
    : Control(s, path), numeric_(std::move(numeric)), defNum_(def), isNumeric_(true) { for (auto& n : numeric_) options_.add(n.first); init(label); }
void Choice::init(const juce::String& label) {
    name_.setText(label, juce::dontSendNotification); name_.setFont(VVLookAndFeel::mono(11.f)); name_.setColour(juce::Label::textColourId, col::dim);
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);
    for (int i = 0; i < options_.size(); ++i) box.addItem(options_[i].isEmpty() ? "(none)" : options_[i], i + 1);
    box.onChange = [this] {
        if (updating_) return; const int i = box.getSelectedId() - 1; if (i < 0 || i >= options_.size()) return;
        if (isNumeric_) store.set(path_, numeric_[(size_t) i].second); else { const auto prev = store.str(path_, defStr_); store.set(path_, options_[i]); if (onChosen) onChosen(prev, options_[i]); }
    };
    addAndMakeVisible(box); refresh();
}
void Choice::refresh() {
    updating_ = true; int idx = -1;
    if (isNumeric_) { const double v = store.num(path_, defNum_); double best = 1e18; for (size_t i = 0; i < numeric_.size(); ++i) if (std::abs(numeric_[i].second - v) < best) { best = std::abs(numeric_[i].second - v); idx = (int) i; } }
    else { const auto v = store.str(path_, defStr_); idx = options_.indexOf(v); if (idx < 0 && v.isNotEmpty()) { options_.add(v); box.addItem(v, options_.size()); idx = options_.size() - 1; } }
    box.setSelectedId(idx + 1, juce::dontSendNotification); updating_ = false;
}
void Choice::setLabels(const juce::StringArray& labels) { box.clear(juce::dontSendNotification); for (int i = 0; i < options_.size(); ++i) box.addItem(i < labels.size() ? labels[i] : options_[i], i + 1); refresh(); }
void Choice::resized() { auto r = getLocalBounds(); name_.setBounds(r.removeFromTop(18)); box.setBounds(r.reduced(0, 2)); }

// ---------------------------------------------------------------- Stepper
Stepper::Stepper(Store& s, const juce::String& label, const juce::String& path, int lo, int hi, int def) : Control(s, path), lo_(lo), hi_(hi), def_(def) {
    name_.setText(label, juce::dontSendNotification); name_.setFont(VVLookAndFeel::mono(11.f)); name_.setColour(juce::Label::textColourId, col::dim);
    value_.setJustificationType(juce::Justification::centred); value_.setFont(VVLookAndFeel::mono(14.f));
    auto bump = [this](int d) { store.set(path_, juce::jlimit(lo_, hi_, (int) std::lround(store.num(path_, def_)) + d)); };
    minus_.onClick = [bump] { bump(-1); }; plus_.onClick = [bump] { bump(1); };
    addAndMakeVisible(name_); addAndMakeVisible(value_); addAndMakeVisible(minus_); addAndMakeVisible(plus_); refresh();
}
void Stepper::refresh() { value_.setText(juce::String((int) std::lround(store.num(path_, def_))), juce::dontSendNotification); }
void Stepper::resized() { auto r = getLocalBounds(); name_.setBounds(r.removeFromTop(18)); r = r.reduced(0, 2); minus_.setBounds(r.removeFromLeft(34)); plus_.setBounds(r.removeFromRight(34)); value_.setBounds(r); }

// ---------------------------------------------------------------- Panel
template <class T, class... A> T& Panel::make(A&&... a) {
    auto* c = new T(store, std::forward<A>(a)...); owned_.add(c); addAndMakeVisible(c);
    const auto sz = c->preferredSize(); items_.push_back({ c, sz.x, sz.y }); return *c;
}
Knob& Panel::knob(const juce::String& l, const juce::String& p, double lo, double hi, double def, KnobOpts o) { o.scale *= knobScale_; return make<Knob>(l, p, lo, hi, def, o); }
VFader& Panel::vfader(const juce::String& l, const juce::String& p, double lo, double hi, double def, KnobOpts o) { return make<VFader>(l, p, lo, hi, def, o); }
Toggle& Panel::toggle(const juce::String& l, const juce::String& p, bool def) { return make<Toggle>(l, p, def); }
Choice& Panel::choice(const juce::String& l, const juce::String& p, const juce::StringArray& o, const juce::String& def) { return make<Choice>(l, p, o, def); }
Choice& Panel::choiceNum(const juce::String& l, const juce::String& p, std::vector<std::pair<juce::String, double>> c, double def) { return make<Choice>(l, p, std::move(c), def); }
Stepper& Panel::stepper(const juce::String& l, const juce::String& p, int lo, int hi, int def) { return make<Stepper>(l, p, lo, hi, def); }
void Panel::custom(juce::Component* c, int w, int h) { addAndMakeVisible(c); items_.push_back({ c, w, h }); }

int Panel::layout(int width, bool apply) const {
    const int gap = 6; int x = kPad, y = kHeader + 4, rowH = 0; const int maxX = width - kPad; size_t rowStart = 0; bool rowTwo = false;
    struct P { int x, y, w, h; bool knob; }; std::vector<P> pos; pos.reserve(items_.size());
    // a row that holds a two-line dial label bottom-aligns its dials, so every knob and readout stays on one height whether its label has one line or two
    auto flush = [&](size_t end) { if (rowTwo) for (size_t k = rowStart; k < end; ++k) if (pos[k].knob) pos[k].y = y + (rowH - pos[k].h); };
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i]; const int w = it.w < 0 ? (maxX - kPad) : it.w; int h = it.h; bool isKnob = false, two = false;
        if (auto* kn = dynamic_cast<Knob*>(it.c)) { isKnob = true; two = kn->labelNeedsTwoLines(); if (apply) kn->setTwoLine(two); if (two) h += 12; }
        const bool brk = rowBreaks_.contains((int) i);
        if ((x + w > maxX && x > kPad) || (brk && x > kPad)) { flush(i); x = kPad; y += rowH + gap; rowH = 0; rowStart = i; rowTwo = false; }
        pos.push_back({ x, y, w, h, isKnob }); rowTwo = rowTwo || two;
        x += w + gap; rowH = juce::jmax(rowH, h);
    }
    flush(items_.size());
    if (apply) for (size_t i = 0; i < items_.size(); ++i) items_[i].c->setBounds(pos[i].x, pos[i].y, pos[i].w, pos[i].h);
    return y + rowH + kPad;
}
int Panel::heightForWidth(int w) const { return items_.empty() ? kHeader + kPad : layout(w, false); }
void Panel::resized() { layout(getWidth(), true); }
void Panel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillRoundedRectangle(r.translated(0.f, 2.f), 6.f);
    g.setGradientFill(juce::ColourGradient(col::panelTop, 0.f, r.getY(), col::panel, 0.f, r.getBottom(), false)); g.fillRoundedRectangle(r, 6.f);
    g.setColour(juce::Colours::white.withAlpha(0.06f)); g.drawLine(r.getX() + 6.f, r.getY() + 1.f, r.getRight() - 6.f, r.getY() + 1.f, 1.f);
    g.setColour(col::edge); g.drawRoundedRectangle(r, 6.f, 1.f);
    for (auto c : { juce::Point<float>(r.getX() + 8.f, r.getY() + 8.f), juce::Point<float>(r.getRight() - 8.f, r.getY() + 8.f), juce::Point<float>(r.getX() + 8.f, r.getBottom() - 8.f), juce::Point<float>(r.getRight() - 8.f, r.getBottom() - 8.f) })
        { g.setColour(juce::Colour(0xff111111)); g.fillEllipse(c.x - 3.f, c.y - 3.f, 6.f, 6.f); g.setColour(juce::Colour(0xff4a4f5a)); g.fillEllipse(c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f); }
    g.setColour(col::cyan); g.fillRect((float) kPad + 2.f, 8.f, 3.f, 11.f);                                                   // accent bar before the title
    g.setColour(col::silk); g.setFont(VVLookAndFeel::mono(11.f).boldened());
    g.drawText(title_.toUpperCase(), kPad + 12, 4, getWidth() - 2 * kPad - 10, 20, juce::Justification::centredLeft);
}

void Page::layoutContent() {
    const int w = juce::jmax(200, getWidth() - 14); int y = 6;
    for (auto* p : panels_) { const int h = p->heightForWidth(w - 12); p->setBounds(6, y, w - 12, h); y += h + 8; }
    content.setSize(w, y + 6);
}

// ---------------------------------------------------------------- shared editors
// Amp envelope = four vertical faders + a Repeat menu (Off / 1..8 extra Attack+Decay cycles / Continuous until note-off). Same stored keys as the web app (a, d, s, r, rep).
void addEnv(Panel& p, const juce::String& pre, bool) {
    p.customOwned(new EnvVizView(p.store, pre), -1, 120);
    p.newRow();
    p.vfader("Attack", pre + ".a", 0.001, 5, 0.01, { "%.3f", " s", true, 3 });
    p.vfader("Decay", pre + ".d", 0.001, 5, 0.2, { "%.3f", " s", true, 3 });
    p.vfader("Sustain", pre + ".s", 0, 1, 0.7);
    p.vfader("Release", pre + ".r", 0.001, 8, 0.3, { "%.3f", " s", true, 3 });
    p.choiceNum("Repeat", pre + ".rep", kEnvRepeatChoices(), 0);
}
void addFilter(Panel& p, const juce::String& pre, const juce::String& env, double defCutoff) {
    p.customOwned(new FilterVizView(p.store, pre), -1, 170);
    p.newRow();
    p.toggle("On", pre + ".on", true);   // filter bypass: off routes the signal straight to the volume / pan stage (a missing key still means on)
    p.choice("Filter type", pre + ".type", kFilterTypes, "moog4pole");
    p.choice("Formant table", pre + ".formantTable", kFormantTables, "classic"); p.choice("Vowel", pre + ".vowel", kFormantVowels, "a");
    p.newRow();
    p.knob("Cutoff", pre + ".cutoff", 20, 18000, defCutoff, { "%.0f", " Hz", true, 0 });
    p.knob("Resonance", pre + ".reso", 0, 130, 20, { "%.0f", "", false, 0 });
    p.knob("Key track", pre + ".keytrack", -100, 200, 0, { "%.0f", " %", false, 0 });
    p.knob("Volume", pre + ".volume", 0, 2, 1);
    p.knob("LPG decay", pre + ".lpgDecay", 0.05, 3, 0.8);
    p.newRow();
    p.knob("Env amount", env + ".amount", -18000, 18000, 0, { "%.0f", " Hz", false, 0 });
    p.knob("Env attack", env + ".a", 0.001, 5, 0.01, { "%.3f", " s", true, 3 });
    p.knob("Env decay", env + ".d", 0.001, 5, 0.2, { "%.3f", " s", true, 3 });
    p.knob("Env sustain", env + ".s", 0, 1, 0.3);
    p.knob("Env release", env + ".r", 0.001, 8, 0.3, { "%.3f", " s", true, 3 });
    p.newRow(); p.customOwned(new EnvVizView(p.store, env, env + ".amount"), -1, 140);
    p.customOwned(new CurveButton(p.store, env + ".shape", "DRAW CUSTOM SHAPE"), 190, 34);   // web: hand-drawn filter-envelope contour (stored with the preset)
}
void addOscCommon(Panel& p, const juce::String& pre, bool drift) {
    p.stepper("Octave", pre + ".oct", -4, 4, 0);
    p.stepper("Semitone", pre + ".semitone", -12, 12, 0);
    p.knob("Fine", pre + ".fineTune", -100, 100, 0, { "%.0f", " ct", false, 0 });
    p.knob("Pan", pre + ".pan", -1, 1, 0);
    if (drift) p.knob("Drift", pre + ".drift", 0, 30, 0, { "%.1f", "", false, 1 });
    p.knob("Glide", pre + ".glideTime", 0, 3, 0, { "%.2f", " s", false, 2 });
    p.knob("Key track", pre + ".pitchKeytrack", 0, 200, 100, { "%.0f", " %", false, 0 });
    p.stepper("Zone low", pre + ".rangeLow", 0, 127, 0);
    p.stepper("Zone high", pre + ".rangeHigh", 0, 127, 127);
    p.toggle("Hold", pre + ".hold");
}

} // namespace vv
