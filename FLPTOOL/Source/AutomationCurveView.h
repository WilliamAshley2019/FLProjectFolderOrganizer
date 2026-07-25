#pragma once
#include <JuceHeader.h>
#include "flp.h"

 
class AutomationCurveView : public juce::Component
{
public:
    AutomationCurveView();

    void setPoints(const std::vector<FL::AutomationEvent::Record>& newPoints);
     
    void setClipLength(double clipEndPosition);
    void clear();

    void paint(juce::Graphics& g) override;

private:
    std::vector<FL::AutomationEvent::Record> points;
    double clipEndPosition = 0.0;
 
    static double interpolate(int curveType, double a, double b, double t, float tension);

    juce::Rectangle<float> getPlotArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationCurveView)
};
