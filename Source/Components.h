#pragma once
// ============================================================================
//  DaliMix - UI components
//  ReadinessPanel | HealthGrid | Timeline | ToneChart | LiveMeters | ContentView
// ============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "Theme.h"
#include "ReportBuilder.h"

namespace ui
{
using namespace theme;

inline void drawChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c, bool filled)
{
    g.setColour (filled ? c : c.withAlpha (0.14f));
    g.fillRoundedRectangle (r, radiusSmall);
    if (! filled)
    {
        g.setColour (c.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), radiusSmall, 1.0f);
    }
    g.setColour (filled ? col::bg : c);
    g.setFont (font (size::small, true));
    g.drawText (text, r, juce::Justification::centred);
}

// ============================================================================
//  ReadinessPanel - technical readiness status (no quality score)
// ============================================================================
class ReadinessPanel : public juce::Component
{
public:
    void setReport (const pa::Report& r) { report = r; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);
        auto a = b.reduced ((float) pad);

        g.setColour (col::textMuted);
        g.setFont (font (size::small, true));
        g.drawText ("MASTERING READINESS", a.removeFromTop (16.0f), juce::Justification::centredRight);
        a.removeFromTop (6.0f);

        if (! report.valid)
        {
            auto t = rtl (HE ("עדיין אין ניתוח. התחילו הקלטה או טענו קובץ."), font (size::heading, true), col::textMuted, a.getWidth());
            t.draw (g, a);
            return;
        }
        const auto c = report.readiness == pa::Readiness::Fix ? col::problem
                     : report.readiness == pa::Readiness::Good ? col::warning : col::ok;
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (a.getRight() - 5.0f, a.getY() + 2.0f, 5.0f, 30.0f), 2.0f);
        auto title = rtl (str (report.readinessTitle), font (size::title + 2.0f, true), c, a.getWidth() - 14.0f);
        title.draw (g, a.withTrimmedRight (14.0f).removeFromTop (title.getHeight()));
        a.removeFromTop (title.getHeight() + 4.0f);

        auto line = rtl (str (report.readinessLine), font (size::body, true), col::text, a.getWidth());
        line.draw (g, a.removeFromTop (line.getHeight()));
        a.removeFromTop (2.0f);
        auto en = ltr (str (report.readinessEn), font (size::small), col::textMuted, a.getWidth());
        en.draw (g, a.removeFromTop (en.getHeight()));
        a.removeFromTop (8.0f);
        auto note = rtl (str (report.note), font (size::small), col::textMuted.withAlpha (0.8f), a.getWidth());
        note.draw (g, a);
    }

private:
    pa::Report report;
};

// ============================================================================
//  HealthGrid - six areas at a glance, click to jump to the details
// ============================================================================
class HealthGrid : public juce::Component
{
public:
    std::function<void (int group)> onGroupClicked;

    void setReport (const pa::Report& r) { report = r; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);
        auto a = b.reduced ((float) pad);
        g.setColour (col::textMuted);
        g.setFont (font (size::small, true));
        g.drawText ("MIX HEALTH", a.removeFromTop (16.0f), juce::Justification::centredRight);
        a.removeFromTop (8.0f);

        const float cw = (a.getWidth() - 2.0f * gap) / 3.0f, ch = (a.getHeight() - gap) / 2.0f;
        for (int i = 0; i < pa::kNumHealthGroups; ++i)
        {
            const int col3 = i % 3, row = i / 3;
            // RTL order: first item on the right
            auto r = juce::Rectangle<float> (a.getRight() - (col3 + 1) * cw - col3 * gap, a.getY() + row * (ch + gap), cw, ch);
            cells[(size_t) i] = r;
            const auto& h = report.health[i];
            const auto st = report.valid ? h.status : pa::Status::Info;
            const auto c = report.valid ? statusColour (st) : col::line;

            g.setColour (i == hover ? col::panelRaised.brighter (0.05f) : col::panelRaised);
            g.fillRoundedRectangle (r, radiusSmall);
            auto in = r.reduced (10.0f, 6.0f);
            auto dot = in.removeFromRight (12.0f).withSizeKeepingCentre (10.0f, 10.0f);
            g.setColour (c);
            g.fillEllipse (dot);
            in.removeFromRight (8.0f);
            g.setColour (col::text);
            g.setFont (font (size::body, true));
            g.drawText (str (h.name.empty() ? pa::groupName ((pa::HealthGroup) i) : h.name), in.removeFromTop (in.getHeight() * 0.55f), juce::Justification::bottomRight);
            g.setColour (col::textMuted);
            g.setFont (font (size::small));
            g.drawText (report.valid ? juce::String::fromUTF8 (pa::statusName (st)) : juce::String ("--"), in, juce::Justification::topRight);
            g.drawText (pa::groupNameEn ((pa::HealthGroup) i), in, juce::Justification::topLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < pa::kNumHealthGroups; ++i) if (cells[(size_t) i].contains (e.position)) h = i;
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h >= 0 && report.valid ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < pa::kNumHealthGroups; ++i)
            if (cells[(size_t) i].contains (e.position) && report.valid && onGroupClicked) onGroupClicked (i);
    }

private:
    pa::Report report;
    std::array<juce::Rectangle<float>, pa::kNumHealthGroups> cells;
    int hover = -1;
};

// ============================================================================
//  Timeline - sections, loudness, problem markers, audition controls
// ============================================================================
class Timeline : public juce::Component
{
public:
    std::function<void (int eventIndex, int mode)> onPlayEvent;   // mode 0 = full, 1 = solo
    std::function<void (double seconds)> onSeek;
    std::function<void()> onStop;

    Timeline()
    {
        for (auto* b : { &playBtn, &soloBtn, &stopBtn }) addChildComponent (b);
        playBtn.setButtonText (HE ("השמע"));
        stopBtn.setButtonText (HE ("עצור"));
        playBtn.onClick = [this] { if (selected >= 0 && onPlayEvent) onPlayEvent (selected, 0); };
        soloBtn.onClick = [this] { if (selected >= 0 && onPlayEvent) onPlayEvent (selected, 1); };
        stopBtn.onClick = [this] { if (onStop) onStop(); };
    }

    void setData (const pa::Metrics& m, pa::TimeFormatter f)
    {
        metrics = m;
        tf = std::move (f);
        selected = -1;
        updateButtons();
        repaint();
    }
    void setAudioAvailable (bool a) { audio = a; updateButtons(); }
    void setPlayhead (bool active, double sec)
    {
        if (active == headActive && std::abs (sec - headSec) < 0.02) return;
        headActive = active; headSec = sec;
        stopBtn.setVisible (active);
        repaint();
    }
    void selectFirstOfType (int type)
    {
        for (size_t i = 0; i < metrics.events.size(); ++i)
            if ((int) metrics.events[i].type == type) { selected = (int) i; break; }
        updateButtons();
        repaint();
    }

    void resized() override
    {
        auto d = detailArea().toNearestInt();
        stopBtn.setBounds (d.removeFromLeft (70).reduced (0, 2));
        d.removeFromLeft (6);
        soloBtn.setBounds (d.removeFromLeft (96).reduced (0, 2));
        d.removeFromLeft (6);
        playBtn.setBounds (d.removeFromLeft (70).reduced (0, 2));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);

        auto header = headerArea();
        g.setColour (col::text);
        g.setFont (font (size::heading, true));
        g.drawText (HE ("ציר זמן"), header, juce::Justification::centredRight);

        // legend
        {
            auto lg = header.withTrimmedRight (90.0f);
            g.setFont (font (size::small));
            for (int t = 0; t < pa::kNumEventTypes; ++t)
            {
                const auto name = juce::String::fromUTF8 (pa::eventName ((pa::EventType) t));
                auto cell = lg.removeFromLeft (juce::jmin (110.0f, lg.getWidth() / (pa::kNumEventTypes - t)));
                g.setColour (eventColour ((pa::EventType) t));
                g.fillRoundedRectangle (cell.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), 2.0f);
                g.setColour (col::textMuted);
                g.drawText (name, cell.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
            }
        }

        const auto plot = plotArea();
        const double dur = juce::jmax (1.0, metrics.durationSec);
        auto xOf = [&] (double s) { return plot.getX() + plot.getWidth() * (float) juce::jlimit (0.0, 1.0, s / dur); };

        if (! metrics.valid)
        {
            g.setColour (col::textMuted);
            g.setFont (font (size::body));
            g.drawText (HE ("ציר הזמן יופיע אחרי הניתוח: מבנה הטראק, ובדיוק איפה כל בעיה קורית."), plot, juce::Justification::centred);
            return;
        }

        // sections band
        auto secRow = sectionRow();
        for (auto& s : metrics.sections)
        {
            auto r = juce::Rectangle<float>::leftTopRightBottom (xOf (s.startSec), secRow.getY(), xOf (s.endSec), secRow.getBottom()).reduced (1.0f, 0.0f);
            const auto c = sectionColour (s.type);
            g.setColour (c.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (c.brighter (0.3f));
            g.setFont (font (size::small, true));
            if (r.getWidth() > 34.0f) g.drawText (pa::sectionName (s.type), r, juce::Justification::centred);
        }

        // loudness envelope
        auto env = envelopeRow();
        if (! metrics.loudPerSec.empty())
        {
            float mx = -70.0f;
            for (float v : metrics.loudPerSec) mx = juce::jmax (mx, v);
            const float floorDb = mx - 30.0f;
            juce::Path p;
            p.startNewSubPath (plot.getX(), env.getBottom());
            for (size_t i = 0; i < metrics.loudPerSec.size(); ++i)
            {
                const float norm = juce::jlimit (0.0f, 1.0f, (metrics.loudPerSec[i] - floorDb) / 30.0f);
                p.lineTo (xOf ((double) i + 0.5), env.getBottom() - norm * env.getHeight());
            }
            p.lineTo (xOf (dur), env.getBottom());
            p.closeSubPath();
            g.setColour (col::textMuted.withAlpha (0.28f));
            g.fillPath (p);
        }

        // event lanes
        for (int t = 0; t < pa::kNumEventTypes; ++t)
        {
            auto lane = laneRect (t);
            g.setColour (col::bg.withAlpha (0.6f));
            g.fillRect (lane.withX (plot.getX()).withWidth (plot.getWidth()));
            g.setColour (col::textMuted);
            g.setFont (font (11.0f));
            g.drawText (juce::String::fromUTF8 (pa::eventName ((pa::EventType) t)),
                        juce::Rectangle<float> (plot.getRight() + 6.0f, lane.getY() - 2.0f, b.getRight() - plot.getRight() - 12.0f, lane.getHeight() + 4.0f),
                        juce::Justification::centredRight);
        }
        for (size_t i = 0; i < metrics.events.size(); ++i)
        {
            const auto& e = metrics.events[i];
            auto lane = laneRect ((int) e.type);
            const float x0 = xOf (e.startSec), x1 = juce::jmax (x0 + 3.0f, xOf (e.endSec));
            auto r = juce::Rectangle<float>::leftTopRightBottom (x0, lane.getY(), x1, lane.getBottom());
            g.setColour (eventColour (e.type).withAlpha (e.severity == 2 ? 1.0f : 0.55f));
            g.fillRect (r);
            if ((int) i == selected)
            {
                g.setColour (col::text);
                g.drawRect (r.expanded (2.0f, 2.0f), 1.5f);
            }
        }

        // time axis
        auto axis = axisRow();
        g.setColour (col::textMuted);
        g.setFont (font (11.0f));
        const double stepChoices[] = { 10, 15, 30, 60, 120 };
        double step = 30;
        for (double s : stepChoices) if (dur / s <= 10.0) { step = s; break; }
        for (double s = 0; s <= dur; s += step)
            g.drawText (juce::String (pa::defaultTime (s)), juce::Rectangle<float> (50.0f, axis.getHeight()).withCentre ({ xOf (s), axis.getCentreY() }), juce::Justification::centred);

        // audition playhead
        if (headActive)
        {
            g.setColour (col::accent);
            const float x = xOf (headSec);
            g.drawLine (x, secRow.getY(), x, laneRect (pa::kNumEventTypes - 1).getBottom(), 2.0f);
        }

        // detail row
        auto d = detailArea();
        juce::String text;
        if (selected >= 0 && selected < (int) metrics.events.size())
            text = str (pa::describeEvent (metrics.events[(size_t) selected], tf));
        else
            text = HE ("לחצו על סימון כדי לראות פרטים ולהשמיע. לחיצה על הגרף משמיעה מאותה נקודה (כשאבלטון עצור).");
        auto layout = rtl (text, font (size::small, selected >= 0), selected >= 0 ? col::text : col::textMuted, d.getWidth() - 270.0f,
                           juce::Justification::centredRight);
        layout.draw (g, d.withTrimmedLeft (270.0f));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! metrics.valid) return;
        const auto plot = plotArea();
        const double dur = juce::jmax (1.0, metrics.durationSec);
        auto xOf = [&] (double s) { return plot.getX() + plot.getWidth() * (float) juce::jlimit (0.0, 1.0, s / dur); };

        for (int t = 0; t < pa::kNumEventTypes; ++t)
            if (laneRect (t).expanded (0.0f, 2.0f).withX (plot.getX()).withWidth (plot.getWidth()).contains (e.position))
            {
                int best = -1;
                float bestDist = 8.0f;
                for (size_t i = 0; i < metrics.events.size(); ++i)
                {
                    const auto& ev = metrics.events[i];
                    if ((int) ev.type != t) continue;
                    const float x0 = xOf (ev.startSec), x1 = juce::jmax (x0 + 3.0f, xOf (ev.endSec));
                    const float dist = e.position.x < x0 ? x0 - e.position.x : e.position.x > x1 ? e.position.x - x1 : 0.0f;
                    if (dist < bestDist) { bestDist = dist; best = (int) i; }
                }
                selected = best;
                updateButtons();
                repaint();
                return;
            }

        if (plot.contains (e.position) && onSeek)
            onSeek (dur * (e.position.x - plot.getX()) / plot.getWidth());
    }

private:
    juce::Rectangle<float> inner() const       { return getLocalBounds().toFloat().reduced ((float) pad, 12.0f); }
    juce::Rectangle<float> headerArea() const  { return inner().removeFromTop (20.0f); }
    juce::Rectangle<float> plotArea() const
    {
        auto a = inner();
        a.removeFromTop (26.0f);
        a.removeFromBottom (38.0f);
        return a.withTrimmedRight (86.0f);
    }
    juce::Rectangle<float> sectionRow() const  { return plotArea().removeFromTop (18.0f); }
    juce::Rectangle<float> envelopeRow() const { auto p = plotArea(); p.removeFromTop (22.0f); return p.removeFromTop (juce::jmax (30.0f, p.getHeight() - 5 * 10.0f - 22.0f)); }
    juce::Rectangle<float> laneRect (int t) const
    {
        const auto env = envelopeRow();
        return { env.getX(), env.getBottom() + 4.0f + t * 10.0f, env.getWidth(), 7.0f };
    }
    juce::Rectangle<float> axisRow() const     { return plotArea().removeFromBottom (14.0f); }
    juce::Rectangle<float> detailArea() const  { return inner().removeFromBottom (30.0f); }

    void updateButtons()
    {
        const bool has = selected >= 0 && selected < (int) metrics.events.size() && audio;
        playBtn.setVisible (has);
        bool solo = false;
        if (has)
        {
            const auto t = metrics.events[(size_t) selected].type;
            solo = t == pa::EventType::Harshness || t == pa::EventType::KickBass || t == pa::EventType::Phase;
            soloBtn.setButtonText (t == pa::EventType::Phase ? HE ("השמע במונו") : HE ("סולו תחום"));
        }
        soloBtn.setVisible (solo);
    }

    pa::Metrics metrics;
    pa::TimeFormatter tf;
    int selected = -1;
    bool audio = false, headActive = false;
    double headSec = 0.0;
    juce::TextButton playBtn, soloBtn, stopBtn;
};

// ============================================================================
//  ToneChart - 1/3-octave deviation from the mix's own tonal trend
// ============================================================================
class ToneChart : public juce::Component
{
public:
    void setData (const pa::Metrics& m) { freqs = m.bandFreqs; devs = m.bandDevDb; tilt = m.tiltDbPerOct; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);
        auto area = b.reduced ((float) pad);
        auto header = area.removeFromTop (22.0f);
        g.setColour (col::text);
        g.setFont (font (size::heading, true));
        g.drawText (HE ("איזון טונאלי"), header, juce::Justification::centredRight);
        g.setColour (col::textMuted);
        g.setFont (font (size::small));
        if (! freqs.empty()) g.drawText ("Tilt " + juce::String (tilt, 1) + " dB/oct", header, juce::Justification::centredLeft);
        auto sub = rtl (HE ("כל עמודה: כמה התחום חורג מהקו הטבעי של המיקס. מעל האפס הצטברות, מתחת חוסר."), font (size::small), col::textMuted, area.getWidth());
        sub.draw (g, area.removeFromTop (sub.getHeight()));
        area.removeFromTop (8.0f);

        auto labels = area.removeFromBottom (16.0f);
        auto plot = area;
        const float range = 9.0f;
        auto xOf = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        auto yOf = [&] (double d) { return plot.getCentreY() - (float) juce::jlimit (-1.0, 1.0, d / range) * plot.getHeight() * 0.5f; };

        g.setColour (col::ok.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yOf (3.0), plot.getRight(), yOf (-3.0)));
        g.setColour (col::line);
        for (double d : { -6.0, 6.0 }) g.drawHorizontalLine ((int) yOf (d), plot.getX(), plot.getRight());
        g.setColour (col::textMuted.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yOf (0.0), plot.getX(), plot.getRight());

        g.setColour (col::textMuted);
        for (double f : { 50.0, 100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0 })
        {
            const juce::String t = f >= 1000.0 ? juce::String (f / 1000.0, f == 2500.0 ? 1 : 0) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ xOf (f), labels.getCentreY() }), juce::Justification::centred);
        }
        for (size_t i = 0; i < freqs.size() && i < devs.size(); ++i)
        {
            if (freqs[i] < 20.0 || freqs[i] > 20000.0) continue;
            const float x0 = xOf (freqs[i] * std::pow (2.0, -1.0 / 6.0)) + 1.5f, x1 = xOf (freqs[i] * std::pow (2.0, 1.0 / 6.0)) - 1.5f;
            const double d = devs[i];
            const float y0 = yOf (0.0), y1 = yOf (d);
            g.setColour (std::abs (d) > 6.0 ? col::problem : std::abs (d) > 3.0 ? col::warning : col::textMuted.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, juce::jmin (y0, y1), x1, juce::jmax (y0, y1) + 0.5f));
        }
    }

private:
    std::vector<double> freqs, devs;
    double tilt = 0.0;
};

// ============================================================================
//  LiveMeters
// ============================================================================
class LiveMeters : public juce::Component
{
public:
    void setValues (bool isActive, bool isWaiting, double seconds, float peakDb, float lufs, float corr)
    {
        active = isActive; waiting = isWaiting; secs = seconds; peak = peakDb; lu = lufs; cor = corr;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);
        auto area = b.reduced (12.0f, 6.0f);
        const float w = area.getWidth() / 4.0f;
        const float dim = active ? 1.0f : 0.35f;
        auto label = [&] (juce::Rectangle<float> r, const juce::String& t)
        {
            g.setColour (col::textMuted);
            g.setFont (font (size::small));
            g.drawText (t, r.removeFromTop (14.0f), juce::Justification::centredRight);
        };

        auto c1 = area.removeFromRight (w).reduced (8.0f, 0.0f);
        label (c1, HE ("זמן"));
        c1.removeFromTop (14.0f);
        {
            const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
            auto dot = c1.removeFromRight (14.0f).withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (active && ! waiting && blinkOn ? col::problem : col::line);
            g.fillEllipse (dot);
            const int s = (int) secs;
            g.setColour (col::text.withAlpha (dim));
            g.setFont (font (size::heading, true));
            g.drawText (juce::String::formatted ("%02d:%02d", s / 60, s % 60), c1.withTrimmedRight (6.0f), juce::Justification::centredRight);
        }

        auto c2 = area.removeFromRight (w).reduced (8.0f, 0.0f);
        label (c2, HE ("פיק (dBFS)"));
        c2.removeFromTop (16.0f);
        {
            auto bar = c2.removeFromTop (8.0f);
            g.setColour (col::bg);
            g.fillRoundedRectangle (bar, 3.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, (peak + 60.0f) / 63.0f);
            g.setColour ((peak > 0.0f ? col::problem : peak > -1.0f ? col::warning : col::ok).withAlpha (dim));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * norm), 3.0f);
            g.setColour (col::text.withAlpha (dim));
            g.setFont (font (size::small));
            g.drawText (juce::String (peak, 1), c2, juce::Justification::centredLeft);
        }

        auto c3 = area.removeFromRight (w).reduced (8.0f, 0.0f);
        label (c3, HE ("עוצמה (LUFS-M)"));
        c3.removeFromTop (14.0f);
        g.setColour (col::text.withAlpha (dim));
        g.setFont (font (size::heading, true));
        g.drawText (lu > -99.0f ? juce::String (lu, 1) : juce::String ("--"), c3, juce::Justification::centredRight);

        auto c4 = area.reduced (8.0f, 0.0f);
        label (c4, HE ("קורלציה"));
        c4.removeFromTop (16.0f);
        {
            auto bar = c4.removeFromTop (8.0f);
            g.setColour (col::bg);
            g.fillRoundedRectangle (bar, 3.0f);
            const float mid = bar.getCentreX();
            const float x = mid + bar.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, cor);
            g.setColour ((cor < 0.0f ? col::problem : col::ok).withAlpha (dim));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), bar.getY(), juce::jmax (mid, x), bar.getBottom()));
            g.setColour (col::textMuted);
            g.drawVerticalLine ((int) mid, bar.getY() - 2.0f, bar.getBottom() + 2.0f);
            g.setColour (col::text.withAlpha (dim));
            g.setFont (font (size::small));
            g.drawText (juce::String (cor, 2), c4, juce::Justification::centredLeft);
        }
    }

private:
    bool active = false, waiting = false;
    double secs = 0.0;
    float peak = -120.0f, lu = -120.0f, cor = 1.0f;
};

// ============================================================================
//  ContentView - Findings | Sections | Reference | Handoff | Advanced
// ============================================================================
class ContentView : public juce::Component
{
public:
    enum class Mode   { Findings, Sections, Reference, Handoff, Advanced };
    enum class Action { Toggle, Play, Solo, Ignore, LoadReference, ClearReference, PlayMixDrop, PlayRefDrop, PlaySection };

    std::function<void (Action, int index)> onAction;

    ContentView() { addChildComponent (chart); }

    void setData (const pa::Report& r, const pa::Metrics& m, const pa::Metrics& ref, const juce::String& refNameIn, pa::TimeFormatter f)
    {
        report = r; metrics = m; refMetrics = ref; refName = refNameIn; tf = std::move (f);
        expanded.assign (r.findings.size(), false);
        for (size_t i = 0; i < r.findings.size(); ++i)
            expanded[i] = ! r.findings[i].ignored && (int) r.findings[i].status >= (int) pa::Status::Warning;
        chart.setData (m);
        relayout();
    }
    void setAudioAvailable (bool mix, bool ref) { mixAudio = mix; refAudio = ref; relayout(); }
    void setMode (Mode m)       { mode = m; relayout(); }
    void setLayoutWidth (int w) { width = juce::jmax (300, w); relayout(); }

    int yForGroup (int group) const
    {
        for (auto& it : items) if (it.group == group) return (int) it.r.getY();
        return 0;
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& it : items) if (it.paint) it.paint (g, it.r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (auto& it : items)
        {
            if (! it.r.contains (e.position)) continue;
            for (auto& h : it.hits)
                if (h.r.contains (e.position))
                {
                    if (h.action == Action::Toggle && h.index >= 0 && h.index < (int) expanded.size())
                    {
                        expanded[(size_t) h.index] = ! expanded[(size_t) h.index];
                        relayout();
                    }
                    else if (onAction) onAction (h.action, h.index);
                    return;
                }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool over = false;
        for (auto& it : items)
            for (auto& h : it.hits) if (h.r.contains (e.position)) over = true;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    struct Hit  { juce::Rectangle<float> r; Action action; int index; };
    struct Item
    {
        juce::Rectangle<float> r;
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> paint;
        std::vector<Hit> hits;
        int group = -1;
    };

    // ------------------------------------------------------------------ layout
    void relayout()
    {
        items.clear();
        chart.setVisible (mode == Mode::Advanced && report.valid);
        float y = 0.0f;
        if (! report.valid && mode != Mode::Reference) y = layoutHowTo();
        else switch (mode)
        {
            case Mode::Findings:  y = layoutFindings(); break;
            case Mode::Sections:  y = layoutSections(); break;
            case Mode::Reference: y = layoutReference(); break;
            case Mode::Handoff:   y = layoutHandoff(); break;
            case Mode::Advanced:  y = layoutAdvanced(); break;
        }
        setSize (width, (int) std::ceil (y + 8.0f));
        repaint();
    }

    float W() const { return (float) width; }

    float addHeading (float y, const juce::String& text, const juce::String& sub = {})
    {
        auto t = rtl (text, font (size::heading, true), col::text, W() - 2.0f * pad);
        auto s = rtl (sub, font (size::small), col::textMuted, W() - 2.0f * pad);
        const float h = t.getHeight() + (sub.isNotEmpty() ? s.getHeight() + 4.0f : 0.0f) + 10.0f;
        Item it;
        it.r = { 0.0f, y, W(), h };
        const bool hasSub = sub.isNotEmpty();
        it.paint = [t, s, hasSub] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            auto a = r.reduced ((float) pad, 4.0f);
            t.draw (g, a.removeFromTop (t.getHeight()));
            if (hasSub) { a.removeFromTop (4.0f); s.draw (g, a); }
        };
        items.push_back (std::move (it));
        return y + h + 4.0f;
    }

    // mark: 0 = none, 1 = ok tick, 2 = cross, 3 = hollow, 4 = number, 5 = coloured dot
    float addRow (float y, const juce::String& text, int mark, juce::Colour markColour, int number = 0,
                  Action a = Action::Toggle, int idx = -1, bool clickable = false)
    {
        const float indent = mark != 0 ? 40.0f : 0.0f;
        auto t = rtl (text, font (size::body), col::text.withAlpha (0.92f), W() - 2.0f * pad - indent);
        const float h = juce::jmax (26.0f, t.getHeight()) + 16.0f;
        Item it;
        it.r = { 0.0f, y, W(), h };
        if (clickable) it.hits.push_back ({ it.r, a, idx });
        it.paint = [t, mark, markColour, number, indent, clickable] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (r, radius);
            const float right = r.getRight() - pad, ty = r.getY() + 8.0f;
            auto circ = juce::Rectangle<float> (right - 24.0f, ty + 1.0f, 24.0f, 24.0f);
            if (mark == 1)
            {
                g.setColour (markColour); g.fillEllipse (circ);
                juce::Path p;
                p.startNewSubPath (circ.getX() + 6.5f, circ.getCentreY() + 0.5f);
                p.lineTo (circ.getX() + 10.5f, circ.getCentreY() + 4.5f);
                p.lineTo (circ.getRight() - 6.0f, circ.getY() + 7.5f);
                g.setColour (col::bg);
                g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else if (mark == 2)
            {
                g.setColour (markColour); g.fillEllipse (circ);
                g.setColour (col::bg);
                auto x = circ.reduced (7.5f);
                g.drawLine (juce::Line<float> (x.getTopLeft(), x.getBottomRight()), 2.2f);
                g.drawLine (juce::Line<float> (x.getTopRight(), x.getBottomLeft()), 2.2f);
            }
            else if (mark == 3) { g.setColour (col::textMuted); g.drawEllipse (circ.reduced (1.0f), 1.6f); }
            else if (mark == 4)
            {
                g.setColour (markColour.withAlpha (0.16f)); g.fillEllipse (circ);
                g.setColour (markColour); g.setFont (font (size::body, true));
                g.drawText (juce::String (number), circ, juce::Justification::centred);
            }
            else if (mark == 5) { g.setColour (markColour); g.fillEllipse (circ.withSizeKeepingCentre (11.0f, 11.0f)); }
            t.draw (g, { r.getX() + pad + (clickable ? 70.0f : 0.0f), ty + 2.0f, r.getWidth() - 2.0f * pad - indent - (clickable ? 70.0f : 0.0f), t.getHeight() });
            if (clickable) drawChip (g, { r.getX() + pad, ty, 60.0f, 24.0f }, HE ("השמע"), col::accent, false);
        };
        items.push_back (std::move (it));
        return y + h + 4.0f;
    }

    // ---------------------------------------------------------------- findings
    float layoutFindings()
    {
        float y = 0.0f;
        const float innerW = W() - 2.0f * pad - 6.0f;
        for (size_t i = 0; i < report.findings.size(); ++i)
        {
            const auto& f = report.findings[i];
            const bool open = expanded[i];
            const bool flagged = (int) f.status >= (int) pa::Status::Warning && ! f.ignored;
            const auto sc = f.ignored ? col::line : statusColour (f.status);

            auto title  = rtl (str (f.title), font (size::heading, true), f.ignored ? col::textMuted : col::text, innerW - 230.0f);
            juce::String proofText;
            for (auto& p : f.proof) proofText << (proofText.isEmpty() ? "" : "\n") << str (p);
            auto proof  = ltr (proofText, font (size::small), col::accent.withAlpha (f.ignored ? 0.5f : 0.95f), innerW - 70.0f);
            auto why    = rtl (str (f.why), font (size::body), col::text.withAlpha (f.ignored ? 0.5f : 0.92f), innerW);
            const bool showAction = open && ! f.action.empty();
            auto action = rtl (str (f.action), font (size::body), col::text, innerW - 24.0f);
            std::vector<juce::TextLayout> tips;
            if (open) for (auto& t : f.tips) tips.push_back (rtl (juce::String::fromUTF8 ("•  ") + str (t), font (size::body), col::textMuted.brighter (0.25f), innerW));

            // vertical plan
            float cy = (float) pad;
            const float yTitle = cy;
            const float titleH = juce::jmax (24.0f, title.getHeight());
            cy += titleH + 8.0f;
            const float yProof = cy;
            cy += juce::jmax (16.0f, proof.getHeight()) + 10.0f;
            float yWhy = 0.0f, yAction = 0.0f, actionH = 0.0f, yTips = 0.0f;
            if (open || flagged)
            {
                yWhy = cy;
                cy += 16.0f + why.getHeight() + 10.0f;
            }
            if (showAction)
            {
                yAction = cy;
                actionH = 10.0f + 16.0f + action.getHeight() + 10.0f;
                cy += actionH + 10.0f;
            }
            yTips = cy;
            for (auto& t : tips) cy += t.getHeight() + 6.0f;
            const float yButtons = cy;
            cy += 30.0f + (float) pad - 4.0f;

            Item it;
            it.r = { 0.0f, y, W(), cy };
            it.group = (int) f.group;
            const float left = pad;

            // hit targets (absolute coordinates)
            float bx = it.r.getX() + left;
            const float by = y + yButtons;
            const bool canPlay = mixAudio;
            const bool hasSolo = f.soloMode != pa::SoloMode::None;
            std::vector<std::pair<juce::Rectangle<float>, std::pair<Action, juce::String>>> chips;
            if (canPlay)
            {
                chips.push_back ({ { bx, by, 64.0f, 26.0f }, { Action::Play, HE ("השמע") } }); bx += 72.0f;
                if (hasSolo)
                {
                    chips.push_back ({ { bx, by, 96.0f, 26.0f }, { Action::Solo, f.soloMode == pa::SoloMode::Mono ? HE ("השמע במונו") : HE ("סולו תחום") } });
                    bx += 104.0f;
                }
            }
            if (flagged || f.ignored)
            {
                chips.push_back ({ { bx, by, 96.0f, 26.0f }, { Action::Ignore, f.ignored ? HE ("בטל התעלמות") : HE ("התעלם") } });
                bx += 104.0f;
            }
            auto toggleRect = juce::Rectangle<float> (it.r.getRight() - pad - 150.0f, by, 150.0f, 26.0f);
            for (auto& c : chips) it.hits.push_back ({ c.first, c.second.first, (int) i });
            it.hits.push_back ({ toggleRect, Action::Toggle, (int) i });
            it.hits.push_back ({ juce::Rectangle<float> (0.0f, y, W(), yProof), Action::Toggle, (int) i });

            const juce::String sevText = flagged ? HE ("חומרה: ") + juce::String::fromUTF8 (pa::severityName (f.status))
                                                 : juce::String::fromUTF8 (f.ignored ? "התעלמת" : pa::statusName (f.status));
            const juce::String confText = flagged ? HE ("ביטחון ") + juce::String (f.confidence) + "%" : juce::String();
            const juce::String toggleText = open ? HE ("הסתר פרטים") : HE ("הצג פרטים וטיפים");
            const bool ignoredF = f.ignored;

            it.paint = [=] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setColour (col::panel);
                g.fillRoundedRectangle (r, radius);
                if (f.status == pa::Status::Problem && ! ignoredF)
                {
                    g.setColour (sc.withAlpha (0.35f));
                    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
                }
                g.setColour (sc);
                g.fillRoundedRectangle (juce::Rectangle<float> (r.getRight() - 6.0f, r.getY(), 6.0f, r.getHeight()), 3.0f);

                const float x = r.getX() + left;
                title.draw (g, { x + 230.0f, r.getY() + yTitle, innerW - 230.0f, titleH });
                drawChip (g, { x, r.getY() + yTitle + 1.0f, 118.0f, 22.0f }, sevText, sc, false);
                if (confText.isNotEmpty())
                {
                    g.setColour (col::textMuted);
                    g.setFont (font (size::small));
                    g.drawText (confText, juce::Rectangle<float> (x + 124.0f, r.getY() + yTitle + 1.0f, 100.0f, 22.0f), juce::Justification::centredLeft);
                }

                auto label = [&] (float yy, const juce::String& t)
                {
                    g.setColour (col::textMuted);
                    g.setFont (font (size::small, true));
                    g.drawText (t, juce::Rectangle<float> (x, r.getY() + yy, innerW, 14.0f), juce::Justification::centredRight);
                };
                label (yProof, HE ("הוכחה"));
                proof.draw (g, { x, r.getY() + yProof, innerW - 70.0f, proof.getHeight() });

                if (yWhy > 0.0f)
                {
                    label (yWhy, HE ("למה זה חשוב"));
                    why.draw (g, { x, r.getY() + yWhy + 16.0f, innerW, why.getHeight() });
                }
                if (showAction)
                {
                    auto box = juce::Rectangle<float> (x, r.getY() + yAction, innerW, actionH);
                    g.setColour (col::accent.withAlpha (0.09f));
                    g.fillRoundedRectangle (box, radiusSmall);
                    g.setColour (col::accent);
                    g.fillRect (juce::Rectangle<float> (box.getRight() - 3.0f, box.getY() + 6.0f, 3.0f, box.getHeight() - 12.0f));
                    auto in = box.reduced (12.0f, 10.0f);
                    g.setFont (font (size::small, true));
                    g.drawText (HE ("מה לעשות"), in.removeFromTop (16.0f), juce::Justification::centredRight);
                    action.draw (g, in);
                }
                float ty = r.getY() + yTips;
                for (auto& t : tips) { t.draw (g, { x, ty, innerW, t.getHeight() }); ty += t.getHeight() + 6.0f; }

                for (auto& c : chips) drawChip (g, c.first, c.second.second, c.second.first == Action::Ignore ? col::textMuted : col::accent, false);
                g.setColour (col::textMuted);
                g.setFont (font (size::small));
                g.drawText (toggleText, toggleRect, juce::Justification::centredRight);
            };
            items.push_back (std::move (it));
            y += cy + (float) gap;
        }
        return y;
    }

    // ---------------------------------------------------------------- sections
    float layoutSections()
    {
        float y = addHeading (0.0f, HE ("מבנה הטראק"), HE ("זיהוי אוטומטי לפי עוצמה ותחתית. באחוזים: אנרגיית לואו, רוחב סטריאו וצפיפות, ביחס לטראק עצמו. לחיצה משמיעה את החלק."));
        if (metrics.sections.empty()) return addRow (y, HE ("לא זוהו חלקים (טראק קצר מדי)."), 0, col::textMuted);

        for (size_t i = 0; i < metrics.sections.size(); ++i)
        {
            const auto s = metrics.sections[i];
            const juce::String range = str (tf ? tf (s.startSec) : pa::defaultTime (s.startSec)) + "  -  " + str (tf ? tf (s.endSec) : pa::defaultTime (s.endSec));
            Item it;
            it.r = { 0.0f, y, W(), 64.0f };
            if (mixAudio) it.hits.push_back ({ it.r, Action::PlaySection, (int) i });
            const bool playable = mixAudio;
            it.paint = [s, range, playable] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setColour (col::panel);
                g.fillRoundedRectangle (r, radius);
                auto a = r.reduced ((float) pad, 10.0f);
                const auto c = sectionColour (s.type);
                auto tag = a.removeFromRight (86.0f).withSizeKeepingCentre (86.0f, 26.0f);
                drawChip (g, tag, pa::sectionName (s.type), c, false);
                a.removeFromRight (12.0f);
                auto info = a.removeFromRight (190.0f);
                g.setColour (col::text);
                g.setFont (font (size::body, true));
                g.drawText (range, info.removeFromTop (info.getHeight() * 0.5f), juce::Justification::centredRight);
                g.setColour (col::textMuted);
                g.setFont (font (size::small));
                g.drawText (juce::String (s.lufs, 1) + " LUFS", info, juce::Justification::centredRight);
                if (playable) drawChip (g, a.removeFromLeft (60.0f).withSizeKeepingCentre (60.0f, 24.0f), HE ("השמע"), col::accent, false);
                a.removeFromLeft (16.0f);

                const std::pair<const char*, double> bars[] = { { "Low", s.lowPct }, { "Width", s.widthPct }, { "Density", s.densityPct } };
                const float bw = a.getWidth() / 3.0f;
                for (int k = 2; k >= 0; --k)
                {
                    auto cell = a.removeFromRight (bw).reduced (8.0f, 4.0f);
                    g.setColour (col::textMuted);
                    g.setFont (font (size::small));
                    g.drawText (juce::String (bars[k].first) + "  " + juce::String ((int) std::lround (bars[k].second)) + "%", cell.removeFromTop (16.0f), juce::Justification::centredRight);
                    auto bar = cell.removeFromTop (7.0f);
                    g.setColour (col::bg);
                    g.fillRoundedRectangle (bar, 3.0f);
                    g.setColour (c.withAlpha (0.85f));
                    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, bars[k].second / 100.0)), 3.0f);
                }
            };
            items.push_back (std::move (it));
            y += 64.0f + 4.0f;
        }
        return y;
    }

    // --------------------------------------------------------------- reference
    float layoutReference()
    {
        float y = addHeading (0.0f, HE ("השוואה לרפרנס"),
                              HE ("טענו טראק מסחרי מאותו סגנון. ההשוואה לא תלויה בעוצמה, וההשמעה מותאמת עוצמה (Loudness matched) כדי שהאוזן לא תועדף לטובת הרפרנס הרועש."));

        // action bar
        {
            Item it;
            it.r = { 0.0f, y, W(), 44.0f };
            const bool hasRef = refMetrics.valid;
            std::vector<std::pair<juce::Rectangle<float>, std::pair<Action, juce::String>>> chips;
            float x = W() - pad;
            auto push = [&] (float w, Action a, const juce::String& t)
            {
                chips.push_back ({ { x - w, y + 8.0f, w, 28.0f }, { a, t } });
                x -= w + 8.0f;
            };
            push (120.0f, Action::LoadReference, hasRef ? HE ("החלף רפרנס") : HE ("טען רפרנס"));
            if (hasRef)
            {
                if (mixAudio && report.valid) push (150.0f, Action::PlayMixDrop, HE ("השמע מיקס (דרופ)"));
                if (refAudio)                 push (180.0f, Action::PlayRefDrop, HE ("השמע רפרנס (מותאם)"));
                push (100.0f, Action::ClearReference, HE ("הסר רפרנס"));
            }
            for (auto& c : chips) it.hits.push_back ({ c.first, c.second.first, -1 });
            const juce::String name = hasRef ? refName : juce::String();
            it.paint = [chips, name] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                for (auto& c : chips) drawChip (g, c.first, c.second.second, c.second.first == Action::ClearReference ? col::textMuted : col::accent, false);
                if (name.isNotEmpty())
                {
                    g.setColour (col::textMuted);
                    g.setFont (font (size::small));
                    g.drawText (name, r.reduced ((float) pad, 0.0f), juce::Justification::centredLeft);
                }
            };
            items.push_back (std::move (it));
            y += 48.0f;
        }

        if (! refMetrics.valid) return addRow (y, HE ("עדיין לא נטען רפרנס."), 3, col::textMuted);
        if (! report.valid)     return addRow (y, HE ("הרפרנס נותח. נתחו את המיקס שלכם כדי לראות השוואה."), 3, col::textMuted);

        // header row
        {
            Item it;
            it.r = { 0.0f, y, W(), 24.0f };
            it.paint = [] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                auto a = r.reduced ((float) pad, 0.0f);
                g.setColour (col::textMuted);
                g.setFont (font (size::small, true));
                g.drawText (HE ("מדד"), a.removeFromRight (220.0f), juce::Justification::centredRight);
                g.drawText (HE ("שלך"), a.removeFromRight (90.0f), juce::Justification::centred);
                g.drawText (HE ("רפרנס"), a.removeFromRight (90.0f), juce::Justification::centred);
                g.drawText (HE ("הערה"), a, juce::Justification::centredRight);
            };
            items.push_back (std::move (it));
            y += 26.0f;
        }
        for (auto& c : pa::buildComparison (metrics, refMetrics))
        {
            Item it;
            it.r = { 0.0f, y, W(), 34.0f };
            it.paint = [c] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setColour (c.highlight ? col::warning.withAlpha (0.08f) : col::panel);
                g.fillRoundedRectangle (r, radiusSmall);
                auto a = r.reduced ((float) pad, 0.0f);
                g.setColour (col::text);
                g.setFont (font (size::body));
                g.drawText (str (c.label), a.removeFromRight (220.0f), juce::Justification::centredRight);
                const int dec = c.unit.empty() ? 2 : 1;
                g.setFont (font (size::body, true));
                g.setColour (c.highlight ? col::warning : col::text);
                g.drawText (juce::String (c.mine, dec) + " " + str (c.unit), a.removeFromRight (90.0f), juce::Justification::centred);
                g.setColour (col::textMuted);
                g.drawText (juce::String (c.ref, dec) + " " + str (c.unit), a.removeFromRight (90.0f), juce::Justification::centred);
                g.setFont (font (size::small));
                g.drawText (str (c.note), a, juce::Justification::centredRight);
            };
            items.push_back (std::move (it));
            y += 36.0f;
        }
        return y;
    }

    // ----------------------------------------------------------------- handoff
    float layoutHandoff()
    {
        float y = addHeading (0.0f, "MASTERING HANDOFF", str (report.readinessEn));
        y = addHeading (y, HE ("טכני"));
        for (auto& t : report.technical)
            y = addRow (y, str (t.text), t.status == pa::Status::Ok ? 1 : 2, statusColour (t.status));
        if (! report.attention.empty())
        {
            y = addHeading (y, HE ("דורש תשומת לב"));
            for (auto& t : report.attention) y = addRow (y, str (t.text), 5, statusColour (t.status));
        }
        y = addHeading (y, HE ("ייצוא למאסטר"), HE ("לבדוק ידנית לפני השליחה"));
        for (auto& e : report.exportItems) y = addRow (y, str (e), 3, col::textMuted);
        return y;
    }

    // ---------------------------------------------------------------- advanced
    float layoutAdvanced()
    {
        chart.setBounds (0, 0, width, 260);
        float y = 270.0f;
        y = addHeading (y, "ADVANCED METRICS", HE ("כל הנתונים הגולמיים של הניתוח."));
        const auto& m = metrics;
        auto f1 = [] (double v, int d = 1) { return juce::String (v, d); };
        const std::vector<std::pair<juce::String, juce::String>> kv = {
            { "Integrated / Max short-term", f1 (m.integratedLufs) + " / " + f1 (m.maxShortTermLufs) + " LUFS" },
            { "True peak / Sample peak", f1 (m.truePeakDb) + " dBTP / " + f1 (m.samplePeakDb) + " dBFS" },
            { "RMS (gated) / Crest", f1 (m.rmsDb) + " dBFS / " + f1 (m.crestDb) + " dB" },
            { "PLR / LRA", f1 (m.plr) + " dB / " + f1 (m.lra) + " LU" },
            { "Correlation all / low / mid / high", f1 (m.correlation, 2) + " / " + f1 (m.lowCorrelation, 2) + " / " + f1 (m.midCorrelation, 2) + " / " + f1 (m.highCorrelation, 2) },
            { "Mono loss all / low / mid / high", f1 (m.monoLossDb) + " / " + f1 (m.lowMonoLossDb) + " / " + f1 (m.midMonoLossDb) + " / " + f1 (m.highMonoLossDb) + " dB" },
            { "L/R balance all / low / mid / high", f1 (m.balanceDb) + " / " + f1 (m.lowBalanceDb) + " / " + f1 (m.midBalanceDb) + " / " + f1 (m.highBalanceDb) + " dB" },
            { "Side/Mid <120Hz / 250Hz-4kHz", f1 (m.lowSideToMidDb) + " / " + f1 (m.midSideToMidDb) + " dB" },
            { "Tilt / Centroid / Flatness", f1 (m.tiltDbPerOct) + " dB/oct / " + f1 (m.centroidHz, 0) + " Hz / " + f1 (m.midFlatness, 2) },
            { "Dev sub / bass / low-mid", f1 (m.subDevDb) + " / " + f1 (m.bassDevDb) + " / " + f1 (m.lowMidDevDb) + " dB" },
            { "Dev presence / sibilance / air", f1 (m.presenceDevDb) + " / " + f1 (m.sibilanceDevDb) + " / " + f1 (m.airDevDb) + " dB" },
            { "Kick / Bass fundamental", f1 (m.kickFreqHz) + " / " + f1 (m.bassFreqHz) + " Hz" },
            { "Kick length / Overlap / Hits", f1 (m.kickLengthMs, 0) + " ms / " + f1 (m.kickBassOverlapPct, 0) + "% / " + juce::String (m.kickHitsMeasured) },
            { "Estimated root / strength", str (pa::pitchName (m.rootPitchClass)) + " / " + f1 (m.rootStrength, 2) },
            { "Clips / Overs / ISP / Flat-tops", juce::String ((juce::int64) m.clipEvents) + " / " + juce::String ((juce::int64) m.oversFloat) + " / " + juce::String ((juce::int64) m.interSampleOvers) + " / " + juce::String ((juce::int64) m.flatTopRuns) },
            { "DC offset / Duration / Sample rate", f1 (m.dcOffsetDb, 0) + " dB / " + str (pa::defaultTime (m.durationSec)) + " / " + f1 (m.sampleRate, 0) + " Hz" },
        };
        for (auto& p : kv)
        {
            Item it;
            it.r = { 0.0f, y, W(), 30.0f };
            it.paint = [p] (juce::Graphics& g, juce::Rectangle<float> r)
            {
                g.setColour (col::panel);
                g.fillRoundedRectangle (r, radiusSmall);
                auto a = r.reduced ((float) pad, 0.0f);
                g.setColour (col::textMuted);
                g.setFont (font (size::small));
                g.drawText (p.first, a.removeFromLeft (a.getWidth() * 0.45f), juce::Justification::centredLeft);
                g.setColour (col::text);
                g.setFont (font (size::body, true));
                g.drawText (p.second, a, juce::Justification::centredLeft);
            };
            items.push_back (std::move (it));
            y += 32.0f;
        }
        return y;
    }

    float layoutHowTo()
    {
        float y = addHeading (0.0f, HE ("איך משתמשים ב-DaliMix"));
        const char* steps[] = {
            "שימו את DaliMix אחרון על ערוץ ה-Master. אם יש לימיטר להאזנה בלבד, עקפו אותו.",
            "בחרו סגנון למעלה. הוא משנה את הספים של הלואו-אנד והדינמיקה.",
            "לחצו 'התחל ניתוח' ונגנו את הטראק מההתחלה ועד הסוף. בסיום לחצו 'עצור והפק דוח'.",
            "בציר הזמן תראו איפה כל בעיה קורית, עם מספר ה-Bar באבלטון. כשאבלטון עצור אפשר להשמיע כל קטע, גם בסולו תחום או במונו.",
            "לשונית 'רפרנס': טענו טראק מסחרי והשוו, עם השמעה מותאמת עוצמה.",
        };
        int n = 1;
        for (auto* s : steps) y = addRow (y, HE (s), 4, col::accent, n++);
        return y;
    }

    pa::Report report;
    pa::Metrics metrics, refMetrics;
    juce::String refName;
    pa::TimeFormatter tf;
    Mode mode = Mode::Findings;
    int width = 600;
    bool mixAudio = false, refAudio = false;
    std::vector<bool> expanded;
    std::vector<Item> items;
    ToneChart chart;
};

} // namespace ui
