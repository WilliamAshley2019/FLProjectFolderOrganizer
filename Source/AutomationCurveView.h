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
    // clipEndPosition: where this clip's Playlist placement actually ends,
    // in the SAME units as Record::position. Testing against two real
    // files found Record::position is NOT the "PPQ ticks" its own comment
    // claims - a ramp ending at position 2.0038 sits inside playlist clips
    // whose FL::PlaylistItem::length was 384/420 raw PPQ ticks (PPQ=96,
    // i.e. 4.0/4.375 beats) - so position looks like it's actually in
    // BEATS, matching the legacy AutomationPoint::beatIncrement naming.
    // Caller must convert: clipEndPosition = playlistItem.length /
    // project.getPPQ(). A clip can be drawn longer than its automation
    // data - FL just holds the last value flat for the remainder,
    // rather than storing extra points for it. Pass 0 if no matching
    // playlist placement was found (falls back to the last record's
    // position, same as before).
    void setClipLength(double clipEndPosition);
    void clear();

    void paint(juce::Graphics& g) override;

private:
    std::vector<FL::AutomationEvent::Record> points;
    double clipEndPosition = 0.0;

    // t in [0,1] within a segment; a/b are the values at the segment's
    // start/end. Formulas per the curve-type table reverse-engineered
    // from FL 2026 (curveType is stored on the *arriving* point).
    static double interpolate(int curveType, double a, double b, double t, float tension);

    juce::Rectangle<float> getPlotArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationCurveView)
};
