// Four assignable XY pads (port of the web app's XY PADS): each axis drives any of ~50 destinations, with hold, snap-back and
// recordable/loopable automation. Destinations write preset paths (or macros / globals), exactly like the web app's applyXYAxis().
#pragma once
#include "Widgets.h"

namespace vv {

class XYPadsView : public juce::Component {
public:
    explicit XYPadsView(Store&);
    ~XYPadsView() override;
    void resized() override;
    int heightForWidth(int w) const;
    struct Impl; 
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace vv
