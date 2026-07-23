#include "FLPLookAndFeel.h"

FLPLookAndFeel::FLPLookAndFeel()
{
    setDefaultSansSerifTypefaceName("Segoe UI");

    // Dark theme colors
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xFF181818));
    setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF222222));
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF333333));
    setColour(juce::TextEditor::textColourId, juce::Colour(0xFFE0E0E0));
    setColour(juce::Label::textColourId, juce::Colour(0xFFE0E0E0));
    setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xFF333333));

    // FL Studio orange accent
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF8800));
    setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFAA00));

    setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xFFFF8800));
    setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xFF333333));

    setColour(juce::TableHeaderComponent::backgroundColourId, juce::Colour(0xFF222222));
    setColour(juce::TableHeaderComponent::textColourId, juce::Colour(0xFFCCCCCC));
    setColour(juce::TableHeaderComponent::outlineColourId, juce::Colour(0xFF333333));
}

void FLPLookAndFeel::drawButtonBackground(juce::Graphics& g, const juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
    auto colour = backgroundColour;

    if (shouldDrawButtonAsDown)
        colour = colour.darker(0.3f);
    else if (shouldDrawButtonAsHighlighted)
        colour = colour.brighter(0.2f);

    g.setColour(colour);
    g.fillRoundedRectangle(bounds, 6.0f);
}

void FLPLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto tickWidth = 20.0f;
    auto tickBounds = bounds.removeFromLeft(tickWidth);

    // Tick box
    auto tickBox = tickBounds.reduced(2.0f);
    g.setColour(button.getToggleState() ? juce::Colour(0xFFFF8800) : juce::Colour(0xFF444444));
    g.fillRoundedRectangle(tickBox, 4.0f);

    if (button.getToggleState())
    {
        g.setColour(juce::Colours::black);
        g.drawText("✓", tickBox, juce::Justification::centred, false);
    }

    // Label
    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centredLeft, true);
}

void FLPLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& progressBar,
    int width, int height,
    double progress, const juce::String& textToShow)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    // Background
    g.setColour(progressBar.findColour(juce::ProgressBar::backgroundColourId));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Foreground
    if (progress > 0.0)
    {
        auto progressBounds = bounds;
        progressBounds.setWidth(progress * bounds.getWidth());

        g.setColour(progressBar.findColour(juce::ProgressBar::foregroundColourId));
        g.fillRoundedRectangle(progressBounds, 4.0f);
    }

    // Text
    if (textToShow.isNotEmpty())
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(textToShow, bounds, juce::Justification::centred, false);
    }
}

void FLPLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll(header.findColour(juce::TableHeaderComponent::backgroundColourId));
}