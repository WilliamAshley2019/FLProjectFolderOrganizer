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

void AutomationCurveView::clear()
{
    points.clear();
    repaint();
}

double AutomationCurveView::interpolate(int curveType, double a, double b, double t)
{
    t = juce::jlimit(0.0, 1.0, t);

    switch (curveType)
    {
        case 0x00: // Linear
            return a + (b - a) * t;

        case 0x01: // Double Curve (ease-in-out)
        case 0x0B: // Double Curve 2
        case 0x0C: // Double Curve 3
        {
            double e = (t < 0.5) ? (4.0 * t * t * t)
                                  : (1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0);
            return a + (b - a) * e;
        }

        case 0x02: // Single Curve (smoothstep / bezier-ish)
        case 0x09: // Single Curve 2
        case 0x0A: // Single Curve 3
            return a + (b - a) * (t * t * (3.0 - 2.0 * t));

        case 0x03: // Stairs
            return (t < 0.5) ? a : b;

        case 0x04: // Smooth Stairs
        {
            double warped = t * t * (3.0 - 2.0 * t);
            return a + (b - a) * (warped * warped * (3.0 - 2.0 * warped));
        }

        case 0x05: // Half Sine
            return a + (b - a) * std::sin(t * juce::MathConstants<double>::pi / 2.0);

        case 0x06: // Hold / Pulse
            return (t < 0.99) ? a : b;

        case 0x07: // Wave (full sine)
            return a + (b - a) * (std::sin(t * 2.0 * juce::MathConstants<double>::pi
                                            - juce::MathConstants<double>::pi / 2.0) * 0.5 + 0.5);

        case 0x08: // Flat Anchor - endpoint marker, no interpolation
            return a;

        default:
            return a + (b - a) * t; // unknown curveType: fall back to linear
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

    // Ranges. Value nominally 0..1 but pad slightly and also expand to fit
    // out-of-range values (e.g. after Scale) rather than clip them off-view.
    double minPos = points.front().position, maxPos = points.back().position;
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

        for (int s = 0; s <= kSamplesPerSegment; ++s)
        {
            double t = (double)s / (double)kSamplesPerSegment;
            double pos = p0.position + (p1.position - p0.position) * t;
            double val = interpolate(curveType, p0.value, p1.value, t);
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

    // Point handles
    for (const auto& p : points)
    {
        float x = xForPos(p.position);
        float y = yForVal(p.value);
        bool isEndpoint = (p.controlCode == 0x03);
        g.setColour(isEndpoint ? juce::Colours::white : juce::Colour(0xFFFFC896));
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
}
