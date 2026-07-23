#pragma once
#include <JuceHeader.h>
#include "flp.h"

// Draws the automation curve FL Studio will actually play back, straight
// from FL::AutomationEvent::Record (position/value/curveType) - the
// modern, serialized data - rather than the legacy AutomationPoint list.
//
// Segment i (between records[i] and records[i+1]) uses
// records[i+1].curveType, matching AutomationEvent::getCurveTypeForSegment's
// existing convention: a point's curveType describes the shape of the
// segment arriving at it, not leaving it.
//
// Read-only view for now. Point handles are drawn large enough that
// drag-to-edit can be added later without changing the paint logic.
class AutomationCurveView : public juce::Component
{
public:
    AutomationCurveView();

    void setPoints(const std::vector<FL::AutomationEvent::Record>& newPoints);
    void clear();

    void paint(juce::Graphics& g) override;

private:
    std::vector<FL::AutomationEvent::Record> points;

    // t in [0,1] within a segment; a/b are the values at the segment's
    // start/end. Formulas per the curve-type table reverse-engineered
    // from FL 2026 (curveType is stored on the *arriving* point).
    static double interpolate(int curveType, double a, double b, double t);

    juce::Rectangle<float> getPlotArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationCurveView)
};
