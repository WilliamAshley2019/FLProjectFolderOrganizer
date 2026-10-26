#pragma once
#include <JuceHeader.h>
#include "OrganizerTheme.h"

// The Organizer's look and feel. All drawing lives in flo::ui::FLOLookAndFeel
// (brushed graphite, amber LCD, LED keys); this alias keeps existing code compiling.
using FLPLookAndFeel = flo::ui::FLOLookAndFeel;
