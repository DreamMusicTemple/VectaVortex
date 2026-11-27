// Locating the bundled assets (wavetables, built-in samples, factory presets) at runtime.
#pragma once
#include <juce_core/juce_core.h>

namespace vv::assets {

// Search order: $VV_ASSETS, the bundle (Resources/Assets, or next to the binary on iOS), then the source tree (development builds).
inline juce::File root() {
    static juce::File cached;
    if (cached.isDirectory()) return cached;
    auto ok = [](const juce::File& f) { return f.getChildFile("Wavetables").isDirectory(); };
    if (auto e = juce::SystemStats::getEnvironmentVariable("VV_ASSETS", {}); e.isNotEmpty() && ok(juce::File(e))) return cached = juce::File(e);
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File dirs[] = { exe.getParentDirectory().getChildFile("Assets"), exe.getParentDirectory().getParentDirectory().getChildFile("Resources/Assets"),
                          exe.getParentDirectory().getChildFile("Resources/Assets"), exe.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Resources/Assets"),
                          juce::File::getSpecialLocation(juce::File::currentApplicationFile).getChildFile("Assets"),
                          juce::File::getSpecialLocation(juce::File::currentApplicationFile).getChildFile("Contents/Resources/Assets") };
    for (auto& d : dirs) if (ok(d)) return cached = d;
#ifdef VV_ASSETS_DIR
    if (ok(juce::File(VV_ASSETS_DIR))) return cached = juce::File(VV_ASSETS_DIR);
#endif
    return {};
}
inline juce::StringArray wavetableNames() {
    juce::StringArray n; for (auto& f : root().getChildFile("Wavetables").findChildFiles(juce::File::findFiles, false, "*.vvwt")) n.add(f.getFileNameWithoutExtension());
    n.sort(true); if (n.isEmpty()) n.add("analogMorph"); return n;
}
inline juce::StringArray sampleNames() {
    juce::StringArray n; auto v = juce::JSON::parse(root().getChildFile("builtin_samples.json"));
    if (auto* a = v.getArray()) for (auto& e : *a) n.add(e.getProperty("name", {}).toString());
    n.sort(true); return n;
}

} // namespace vv::assets
