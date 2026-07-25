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
        case 0:  
        {
            double mid = (a + b) * 0.5 + tension * (b - a) * 0.5;
            double u = 1.0 - t;
            return u * u * a + 2.0 * u * t * mid + t * t * b;
        }

        case 1:  
        {
            double p = std::pow(2.0, -tension * 2.0); // tension>0 -> p<1 (eases earlier), tension<0 -> p>1
            double tw = std::pow(t, p);
            double e = (tw < 0.5) ? (4.0 * tw * tw * tw)
                                  : (1.0 - std::pow(-2.0 * tw + 2.0, 3.0) / 2.0);
            return a + (b - a) * e;
        }

        case 5: 
            return (t < 0.999) ? a : b;

         
        case 0x03: return (t < 0.5) ? a : b;
        case 0x04: { double w = t * t * (3.0 - 2.0 * t); return a + (b - a) * (w * w * (3.0 - 2.0 * w)); }
        case 0x06: return (t < 0.99) ? a : b;
        case 0x07: return a + (b - a) * (std::sin(t * 2.0 * juce::MathConstants<double>::pi
                                            - juce::MathConstants<double>::pi / 2.0) * 0.5 + 0.5);
        case 0x08: return a;

        default:
            return a + (b - a) * t;  
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

     
    std::vector<double> absPos(points.size());
    double running = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        running += points[i].position;
        absPos[i] = running;
    }

     
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

    
    g.setColour(juce::Colour(0xFF2A2A2A));
    for (double gridVal : { 0.0, 0.5, 1.0 })
    {
        if (gridVal < minVal || gridVal > maxVal) continue;
        float y = yForVal(gridVal);
        g.drawHorizontalLine((int)y, area.getX(), area.getRight());
    }

     
    juce::Path curve;
    constexpr int kSamplesPerSegment = 32;
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];
        int curveType = p1.curveType;  
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

     
    juce::Path fill = curve;
    fill.lineTo(xForPos(maxPos), area.getBottom());
    fill.lineTo(xForPos(minPos), area.getBottom());
    fill.closeSubPath();
    g.setColour(juce::Colour(0xFFFF5C00).withAlpha(0.12f));
    g.fillPath(fill);

    g.setColour(juce::Colour(0xFFFF5C00));
    g.strokePath(curve, juce::PathStrokeType(2.0f));

     
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

  
    for (size_t i = 0; i < points.size(); ++i)
    {
        float x = xForPos(absPos[i]);
        float y = yForVal(points[i].value);
        bool isEndpoint = (i == 0 || i == points.size() - 1);
        g.setColour(isEndpoint ? juce::Colours::white : juce::Colour(0xFFFFC896));
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
}
