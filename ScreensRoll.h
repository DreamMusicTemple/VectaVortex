// PIANO ROLL page (web: #rollPanel) for the SEQ screen. See ScreensRoll.cpp.
#pragma once
#include "Widgets.h"
struct VVEngine;
namespace vv { juce::Component* makeRollPage(Store&, VVEngine*); }
