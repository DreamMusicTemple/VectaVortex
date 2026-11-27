// Battery Saving Mode (device setting, NOT part of any preset or plugin state): stored in its own small file so loading a preset can never change it and the choice survives restarts.
// apply() hands it to the engine (one atomic word, picked up at the next audio quantum) and sets the UI refresh divider used by VV_UI_THROTTLE.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include "../../Bridge/vv_engine.h"

namespace vv {
inline std::atomic<int>& uiDividerRef() { static std::atomic<int> d{1}; return d; }
inline int uiDivider() { return uiDividerRef().load(std::memory_order_relaxed); }
// Skip a visual timer tick when the refresh divider says it is not due: place at the top of a timerCallback; `hz` is that timer's own rate.
#define VV_UI_THROTTLE(hz) { static thread_local double vvLast_ = 0; const int vvD_ = vv::uiDivider(); if (vvD_ > 1) { const double vvNow_ = juce::Time::getMillisecondCounterHiRes(); if (vvNow_ - vvLast_ < (1000.0 / (hz)) * vvD_ * 0.9) return; vvLast_ = vvNow_; } }

struct PowerSettings {
    bool on = false; int profile = 2;   // 1 Light, 2 Balanced (recommended), 3 Maximum Efficiency
    bool voices = true, dsp = true, fx = true, mod = true, vis = true;
    static juce::File file() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VektorVortex").getChildFile("battery_saving.json"); }
    static PowerSettings load() {
        PowerSettings s; const auto v = juce::JSON::parse(file()); if (auto* o = v.getDynamicObject()) {
            s.on = (bool) o->getProperty("on"); s.profile = juce::jlimit(1, 3, (int) o->getProperty("profile")); if (!o->hasProperty("profile")) s.profile = 2;
            auto b = [o](const char* k, bool d) { return o->hasProperty(k) ? (bool) o->getProperty(k) : d; }; s.voices = b("voices", true); s.dsp = b("dsp", true); s.fx = b("fx", true); s.mod = b("mod", true); s.vis = b("vis", true); }
        return s;
    }
    void save() const { auto* o = new juce::DynamicObject(); o->setProperty("on", on); o->setProperty("profile", profile); o->setProperty("voices", voices); o->setProperty("dsp", dsp); o->setProperty("fx", fx); o->setProperty("mod", mod); o->setProperty("vis", vis);
        auto f = file(); f.getParentDirectory().createDirectory(); f.replaceWithText(juce::JSON::toString(juce::var(o))); }
    int engineFlags() const { return (voices ? 1 : 0) | (dsp ? 2 : 0) | (fx ? 4 : 0) | (mod ? 8 : 0) | (vis ? 16 : 0); }
    void apply(VVEngine* e) const {
        if (e) vv_engine_set_power(e, on ? profile : 0, engineFlags());
        double st[5] = {0, 0, 0, 0, 1}; if (e) vv_engine_get_power_stats(e, st); uiDividerRef().store(on && vis ? juce::jmax(1, (int) st[4]) : 1, std::memory_order_relaxed);
    }
};
}  // namespace vv
