#pragma once
#include <JuceHeader.h>

class FLPLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FLPLookAndFeel();

    // Remove 'override' - this method doesn't exist in JUCE 8.0.12
    void drawButtonBackground(juce::Graphics& g, const juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown);

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& progressBar,
        int width, int height,
        double progress, const juce::String& textToShow) override;

    void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override;
};