#include "AutomationCurveView.h"

AutomationCurveView::AutomationCurveView()
{
    setOpaque(false);
}

void AutomationCurveView::setPoints(const std::vector<FL::AutomationEvent::Record>& newPoints)
{
    points = newPoints;
    repaint();
}

void AutomationCurveView::setClipLength(double newClipEndPosition)
{
    clipEndPosition = newClipEndPosition;
    repaint();
}

void AutomationCurveView::clear()
{
    points.clear();
    clipEndPosition = 0.0;
    repaint();
}

double AutomationCurveView::interpolate(int curveType, double a, double b, double t, float tension)
{
    t = juce::jlimit(0.0, 1.0, t);

    switch (curveType)
    {
        case 0: // Single Curve - CONFIRMED curveType value. Tension's exact
                // effect on FL's own rendering math is NOT confirmed (only
                // its storage - a signed float, -1..1 - is). Modeled here
                // as a quadratic Bezier whose control point bulges toward
                // one endpoint by `tension`; matches the "drag the dot up
                // or down" description but hasn't been visually verified
                // against FL's actual display yet.
        {
            double mid = (a + b) * 0.5 + tension * (b - a) * 0.5;
            double u = 1.0 - t;
            return u * u * a + 2.0 * u * t * mid + t * t * b;
        }

        case 1: // Double Curve - CONFIRMED curveType value. Same caveat on
                 // tension's exact math as Single Curve above; modeled as
                 // an asymmetric ease-in-out whose inflection point shifts
                 // with tension.
        {
            double p = std::pow(2.0, -tension * 2.0); // tension>0 -> p<1 (eases earlier), tension<0 -> p>1
            double tw = std::pow(t, p);
            double e = (tw < 0.5) ? (4.0 * tw * tw * tw)
                                  : (1.0 - std::pow(-2.0 * tw + 2.0, 3.0) / 2.0);
            return a + (b - a) * e;
        }

        case 5: // Pulse - CONFIRMED curveType value, but its "step count"
                // parameter (confirmed to exist - e.g. 4 vs 14 steps on two
                // real segments) has NOT been located in the record bytes
                // yet, so this renders as a plain hold rather than guessing
                // a step count that could be visually misleading.
            return (t < 0.999) ? a : b;

        // Everything below is UNCONFIRMED - carried over from the original
        // reverse-engineering doc's guesses, which were already shown
        // wrong for Single Curve (guessed 0x02) and Linear (guessed 0x00,
        // doesn't appear to exist as a distinct type at all). Kept only as
        // a better-than-nothing fallback for curve type bytes we haven't
        // seen in a real file yet.
        case 0x03: return (t < 0.5) ? a : b;
        case 0x04: { double w = t * t * (3.0 - 2.0 * t); return a + (b - a) * (w * w * (3.0 - 2.0 * w)); }
        case 0x06: return (t < 0.99) ? a : b;
        case 0x07: return a + (b - a) * (std::sin(t * 2.0 * juce::MathConstants<double>::pi
                                            - juce::MathConstants<double>::pi / 2.0) * 0.5 + 0.5);
        case 0x08: return a;

        default:
            return a + (b - a) * t; // unrecognized curveType byte: fall back to linear
    }
}

juce::Rectangle<float> AutomationCurveView::getPlotArea() const
{
    return getLocalBounds().toFloat().reduced(8.0f, 6.0f);
}

void AutomationCurveView::paint(juce::Graphics& g)
{
    auto area = getPlotArea();

    g.setColour(juce::Colour(0xFF111111));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

    if (points.size() < 2)
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(points.empty() ? "No automation data for this channel."
                                   : "Only one point - nothing to draw a curve between.",
            getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Record::position is a DELTA in beats from the previous point (matches
    // the legacy AutomationPoint::beatIncrement naming), NOT an absolute
    // position - confirmed against a file with known beat-spaced points.
    // Accumulate before using it for layout, or every point past the
    // second one plots in the wrong place.
    std::vector<double> absPos(points.size());
    double running = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        running += points[i].position;
        absPos[i] = running;
    }

    // Ranges. Value nominally 0..1 but pad slightly and also expand to fit
    // out-of-range values (e.g. after Scale) rather than clip them off-view.
    double minPos = absPos.front();
    double lastRecordPos = absPos.back();
    double maxPos = std::max(lastRecordPos, clipEndPosition);
    double minVal = 0.0, maxVal = 1.0;
    for (const auto& p : points)
    {
        minVal = std::min(minVal, p.value);
        maxVal = std::max(maxVal, p.value);
    }
    if (maxPos <= minPos) maxPos = minPos + 1.0;
    if (maxVal <= minVal) maxVal = minVal + 1.0;
    double valPad = (maxVal - minVal) * 0.08;
    minVal -= valPad; maxVal += valPad;

    auto xForPos = [&](double pos) -> float {
        return area.getX() + (float)((pos - minPos) / (maxPos - minPos)) * area.getWidth();
    };
    auto yForVal = [&](double val) -> float {
        return area.getBottom() - (float)((val - minVal) / (maxVal - minVal)) * area.getHeight();
    };

    // Gridlines at 0.0 / 0.5 / 1.0 value if within range
    g.setColour(juce::Colour(0xFF2A2A2A));
    for (double gridVal : { 0.0, 0.5, 1.0 })
    {
        if (gridVal < minVal || gridVal > maxVal) continue;
        float y = yForVal(gridVal);
        g.drawHorizontalLine((int)y, area.getX(), area.getRight());
    }

    // Build the curve path, sampling each segment per its curveType
    juce::Path curve;
    constexpr int kSamplesPerSegment = 32;
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];
        int curveType = p1.curveType; // segment's shape lives on the arriving point
        float tension = p1.tension;

        for (int s = 0; s <= kSamplesPerSegment; ++s)
        {
            double t = (double)s / (double)kSamplesPerSegment;
            double pos = absPos[i] + (absPos[i + 1] - absPos[i]) * t;
            double val = interpolate(curveType, p0.value, p1.value, t, tension);
            float x = xForPos(pos);
            float y = yForVal(val);
            if (i == 0 && s == 0) curve.startNewSubPath(x, y);
            else curve.lineTo(x, y);
        }
    }

    // Filled area under the curve for readability
    juce::Path fill = curve;
    fill.lineTo(xForPos(maxPos), area.getBottom());
    fill.lineTo(xForPos(minPos), area.getBottom());
    fill.closeSubPath();
    g.setColour(juce::Colour(0xFFFF5C00).withAlpha(0.12f));
    g.fillPath(fill);

    g.setColour(juce::Colour(0xFFFF5C00));
    g.strokePath(curve, juce::PathStrokeType(2.0f));

    // Flat hold past the last real point, if the clip's Playlist placement
    // runs longer than the automation data itself (see setClipLength doc).
    if (maxPos > lastRecordPos)
    {
        float xStart = xForPos(lastRecordPos);
        float xEnd = xForPos(maxPos);
        float y = yForVal(points.back().value);
        juce::Path holdPath;
        holdPath.startNewSubPath(xStart, y);
        holdPath.lineTo(xEnd, y);
        float dashLengths[] = { 4.0f, 3.0f };
        juce::PathStrokeType(2.0f).createDashedStroke(holdPath, holdPath, dashLengths, 2);
        g.setColour(juce::Colour(0xFFFF5C00).withAlpha(0.55f));
        g.fillPath(holdPath);

        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("clip end", (int)xEnd - 40, getHeight() - 14, 40, 12,
            juce::Justification::centredRight);
    }

    // Point handles
    for (size_t i = 0; i < points.size(); ++i)
    {
        float x = xForPos(absPos[i]);
        float y = yForVal(points[i].value);
        bool isEndpoint = (i == 0 || i == points.size() - 1);
        g.setColour(isEndpoint ? juce::Colours::white : juce::Colour(0xFFFFC896));
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
}
