#pragma once
// ============================================================================
//  DaliMix (Dali Audio) - Theme
//  ONE place for colours, fonts, spacing and the LookAndFeel.
//  To match the other plugins in the series, change only this file.
// ============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include "ReportBuilder.h"

#define HE(literal) juce::String::fromUTF8 (literal)

namespace theme
{
// ---- Palette -----------------------------------------------------------------
namespace col
{
    const juce::Colour bg          { 0xff1c222c };   // window background (graphite blue)
    const juce::Colour panel       { 0xff252d3a };   // panels, cards
    const juce::Colour panelRaised { 0xff2e3847 };   // hover / raised / dial face
    const juce::Colour line        { 0xff3a4555 };   // hairlines, grid
    const juce::Colour text        { 0xffe8ecf2 };
    const juce::Colour textMuted   { 0xff8f9bae };
    const juce::Colour accent      { 0xfff0b34a };   // VU amber: needle, highlights, active tab

    const juce::Colour ok          { 0xff4fc39a };
    const juce::Colour info        { 0xff6ea2e6 };
    const juce::Colour warning     { 0xffe98a3c };
    const juce::Colour problem     { 0xffe5574f };
}

inline juce::Colour statusColour (pa::Status s)
{
    switch (s)
    {
        case pa::Status::Ok:      return col::ok;
        case pa::Status::Info:    return col::info;
        case pa::Status::Warning: return col::warning;
        case pa::Status::Problem: return col::problem;
    }
    return col::textMuted;
}

// timeline event colours (by type) and section colours
inline juce::Colour eventColour (pa::EventType t)
{
    switch (t)
    {
        case pa::EventType::Harshness: return juce::Colour (0xffe98a3c);
        case pa::EventType::KickBass:  return juce::Colour (0xffa78bfa);
        case pa::EventType::Phase:     return juce::Colour (0xffe5574f);
        case pa::EventType::Balance:   return juce::Colour (0xff6ea2e6);
        case pa::EventType::Clipping:  return juce::Colour (0xfff472b6);
    }
    return col::textMuted;
}
inline juce::Colour sectionColour (pa::SectionType t)
{
    switch (t)
    {
        case pa::SectionType::Intro: return juce::Colour (0xff64748b);
        case pa::SectionType::Build: return juce::Colour (0xfff0b34a);
        case pa::SectionType::Break: return juce::Colour (0xff6ea2e6);
        case pa::SectionType::Drop:  return juce::Colour (0xffe5574f);
        case pa::SectionType::Outro: return juce::Colour (0xff64748b);
        case pa::SectionType::Main:  return juce::Colour (0xff4fc39a);
    }
    return col::textMuted;
}

// ---- Metrics -----------------------------------------------------------------
constexpr float radius      = 8.0f;    // cards / panels
constexpr float radiusSmall = 5.0f;    // buttons, pills
constexpr int   pad         = 16;
constexpr int   gap         = 10;

// ---- Type ----------------------------------------------------------------------
// Default system sans (JUCE 8 falls back automatically to a font with Hebrew glyphs).
// To use your brand font, embed it with juce_add_binary_data and return it here.
inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
namespace size
{
    constexpr float display = 44.0f;   // score number
    constexpr float title   = 20.0f;
    constexpr float heading = 16.5f;
    constexpr float body    = 14.5f;
    constexpr float small   = 12.5f;
}

// ---- Text helpers --------------------------------------------------------------
inline juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

// Hebrew paragraph: right-to-left, right aligned, word wrapped
inline juce::TextLayout rtl (const juce::String& text, const juce::Font& f, juce::Colour c, float width,
                             juce::Justification j = juce::Justification::topRight)
{
    juce::AttributedString a;
    a.setJustification (j);
    a.setReadingDirection (juce::AttributedString::rightToLeft);
    a.setWordWrap (juce::AttributedString::byWord);
    a.setLineSpacing (3.0f);
    a.append (text, f, c);
    juce::TextLayout l;
    l.createLayout (a, juce::jmax (10.0f, width));
    return l;
}

// Numbers / English line: left-to-right reading, but may be right aligned
inline juce::TextLayout ltr (const juce::String& text, const juce::Font& f, juce::Colour c, float width,
                             juce::Justification j = juce::Justification::topRight)
{
    juce::AttributedString a;
    a.setJustification (j);
    a.setReadingDirection (juce::AttributedString::leftToRight);
    a.setWordWrap (juce::AttributedString::byWord);
    a.append (text, f, c);
    juce::TextLayout l;
    l.createLayout (a, juce::jmax (10.0f, width));
    return l;
}

// ---- LookAndFeel ---------------------------------------------------------------
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, col::bg);
        setColour (juce::TextButton::buttonColourId,   col::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, col::accent);
        setColour (juce::TextButton::textColourOffId,  col::text);
        setColour (juce::TextButton::textColourOnId,   col::bg);
        setColour (juce::ComboBox::backgroundColourId, col::panelRaised);
        setColour (juce::ComboBox::outlineColourId,    col::line);
        setColour (juce::ComboBox::textColourId,       col::text);
        setColour (juce::ComboBox::arrowColourId,      col::textMuted);
        setColour (juce::PopupMenu::backgroundColourId,            col::panel);
        setColour (juce::PopupMenu::textColourId,                  col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::panelRaised);
        setColour (juce::PopupMenu::highlightedTextColourId,       col::accent);
        setColour (juce::ScrollBar::thumbColourId, col::line);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bgColour;
        if (! b.isEnabled()) c = c.withAlpha (0.4f);
        else if (down)       c = c.darker (0.15f);
        else if (highlighted) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (r, radiusSmall);
        if (! b.getToggleState())
        {
            g.setColour (col::line);
            g.drawRoundedRectangle (r, radiusSmall, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (size::body, true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override           { return font (size::body); }
    juce::Font getPopupMenuFont() override                           { return font (size::body); }
};

} // namespace theme
