#include "PluginEditor.h"

using namespace theme;
using Mode   = ui::ContentView::Mode;
using Action = ui::ContentView::Action;

DaliMixEditor::DaliMixEditor (DaliMixProcessor& p)
    : AudioProcessorEditor (&p), proc (p), service (p.getService())
{
    setLookAndFeel (&lnf);

    // ---- transport -------------------------------------------------------------
    captureBtn.onClick = [this]
    {
        switch (service.getState())
        {
            case AnalysisService::State::Idle:      service.startCapture(); break;
            case AnalysisService::State::Capturing: service.stopCapture(); break;
            default:                                service.cancelFileAnalysis(); break;
        }
    };
    fileBtn.setButtonText (HE ("טען מיקס"));
    fileBtn.onClick = [this] { chooseFile (false); };
    refBtn.setButtonText (HE ("טען רפרנס"));
    refBtn.onClick = [this] { chooseFile (true); };
    stopAuditionBtn.setButtonText (HE ("עצור השמעה"));
    stopAuditionBtn.onClick = [this] { service.stopPreview(); };
    exportBtn.setButtonText (HE ("ייצא דוח"));
    exportBtn.onClick = [this] { onExport(); };
    copyBtn.setButtonText (HE ("העתק דוח"));
    copyBtn.onClick = [this] { onCopy(); };

    playingOnlyToggle.setButtonText (HE ("הקלט רק בזמן נגינה"));
    playingOnlyToggle.setToggleState (proc.onlyWhilePlaying.load(), juce::dontSendNotification);
    playingOnlyToggle.setColour (juce::ToggleButton::textColourId, col::textMuted);
    playingOnlyToggle.setColour (juce::ToggleButton::tickColourId, col::accent);
    playingOnlyToggle.setColour (juce::ToggleButton::tickDisabledColourId, col::line);
    playingOnlyToggle.onClick = [this] { proc.onlyWhilePlaying.store (playingOnlyToggle.getToggleState()); };

    for (auto* c : std::initializer_list<juce::Component*> { &captureBtn, &fileBtn, &refBtn, &exportBtn, &copyBtn, &playingOnlyToggle })
        addAndMakeVisible (c);
    addChildComponent (stopAuditionBtn);

    for (int i = 0; i < pa::getNumProfiles(); ++i) profileBox.addItem (str (pa::getProfileName (i)), i + 1);
    profileBox.setSelectedId (service.getProfile() + 1, juce::dontSendNotification);
    profileBox.setJustificationType (juce::Justification::centredRight);
    profileBox.onChange = [this] { service.setProfile (profileBox.getSelectedId() - 1); };
    addAndMakeVisible (profileBox);

    // ---- panels ------------------------------------------------------------------
    addAndMakeVisible (meters);
    addAndMakeVisible (health);
    addAndMakeVisible (readiness);
    addAndMakeVisible (timeline);

    health.onGroupClicked = [this] (int g)
    {
        tabs[0].setToggleState (true, juce::dontSendNotification);
        selectTab (Mode::Findings);
        viewport.setViewPosition (0, content.yForGroup (g));
    };
    timeline.onPlayEvent = [this] (int idx, int mode) { auditionEvent (idx, mode); };
    timeline.onSeek = [this] (double s) { service.startPreview (false, s, s + 12.0, AnalysisService::PreviewMode::Full, 0, 0); };
    timeline.onStop = [this] { service.stopPreview(); };

    // ---- tabs ----------------------------------------------------------------------
    const Mode modes[5] = { Mode::Findings, Mode::Sections, Mode::Reference, Mode::Handoff, Mode::Advanced };
    for (int i = 0; i < 5; ++i)
    {
        tabs[i].setClickingTogglesState (true);
        tabs[i].setRadioGroupId (4301);
        const Mode m = modes[i];
        tabs[i].onClick = [this, m] { selectTab (m); };
        addAndMakeVisible (tabs[i]);
    }
    tabs[0].setToggleState (true, juce::dontSendNotification);
    updateTabLabels();

    content.onAction = [this] (Action a, int idx) { handleAction (a, idx); };
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    setResizable (true, true);
    setResizeLimits (960, 900, 1900, 1800);
    setSize (1100, 1040);

    refresh();
    timerCallback();
    startTimerHz (20);
}

DaliMixEditor::~DaliMixEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ============================================================================
void DaliMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    auto h = headerArea.toFloat();
    auto titleArea = h.removeFromRight (h.getWidth() * 0.55f);
    g.setColour (col::text);
    g.setFont (font (size::title + 6.0f, true));
    g.drawText ("DaliMix", titleArea.removeFromTop (30.0f), juce::Justification::centredRight);
    g.setColour (col::textMuted);
    g.setFont (font (size::body));
    g.drawText (HE ("Dali Audio  ·  בדיקה טכנית של המיקס לפני מאסטרינג"), titleArea, juce::Justification::topRight);

    auto s = statusArea.toFloat();
    const auto st = service.getState();
    if (st == AnalysisService::State::AnalyzingFile || st == AnalysisService::State::AnalyzingReference)
    {
        auto bar = s.removeFromLeft (180.0f).withSizeKeepingCentre (180.0f, 6.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (col::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 3.0f);
        s.removeFromLeft (12.0f);
    }
    auto layout = rtl (statusText, font (size::small), col::textMuted, s.getWidth(), juce::Justification::centredRight);
    layout.draw (g, s);
}

void DaliMixEditor::resized()
{
    auto area = getLocalBounds().reduced (pad);

    headerArea = area.removeFromTop (54);
    {
        auto left = headerArea.withWidth (juce::jmin (340, headerArea.getWidth() / 2));
        profileBox.setBounds (left.withSizeKeepingCentre (left.getWidth(), 34).withX (left.getX()));
    }
    area.removeFromTop (6);

    auto transport = area.removeFromTop (38);
    captureBtn.setBounds (transport.removeFromRight (190));
    transport.removeFromRight (gap);
    fileBtn.setBounds (transport.removeFromRight (120));
    transport.removeFromRight (gap);
    refBtn.setBounds (transport.removeFromRight (120));
    transport.removeFromRight (gap);
    playingOnlyToggle.setBounds (transport.removeFromRight (180));
    exportBtn.setBounds (transport.removeFromLeft (100));
    transport.removeFromLeft (gap);
    copyBtn.setBounds (transport.removeFromLeft (100));
    transport.removeFromLeft (gap);
    stopAuditionBtn.setBounds (transport.removeFromLeft (120));

    area.removeFromTop (6);
    statusArea = area.removeFromTop (20);
    area.removeFromTop (6);
    meters.setBounds (area.removeFromTop (46));
    area.removeFromTop (gap);

    auto top = area.removeFromTop (176);
    readiness.setBounds (top.removeFromRight (340));
    top.removeFromRight (gap);
    health.setBounds (top);
    area.removeFromTop (gap);

    timeline.setBounds (area.removeFromTop (236));
    area.removeFromTop (gap);

    auto tabRow = area.removeFromTop (34);
    const int tw = juce::jmin (170, (tabRow.getWidth() - 4 * 6) / 5);
    for (int i = 0; i < 5; ++i)
    {
        tabs[i].setBounds (tabRow.removeFromRight (tw));
        tabRow.removeFromRight (6);
    }
    area.removeFromTop (gap);

    viewport.setBounds (area);
    content.setLayoutWidth (area.getWidth() - viewport.getScrollBarThickness() - 4);
}

// ============================================================================
void DaliMixEditor::timerCallback()
{
    const auto state = service.getState();
    const bool capturing = state == AnalysisService::State::Capturing;
    const bool busy      = state == AnalysisService::State::AnalyzingFile || state == AnalysisService::State::AnalyzingReference;
    const bool waiting   = capturing && proc.onlyWhilePlaying.load() && ! proc.hostIsPlaying.load();

    captureBtn.setButtonText (capturing ? HE ("עצור והפק דוח") : busy ? HE ("בטל ניתוח") : HE ("התחל ניתוח"));
    captureBtn.setColour (juce::TextButton::buttonColourId, capturing ? col::problem : busy ? col::panelRaised : col::accent);
    captureBtn.setColour (juce::TextButton::textColourOffId, busy ? col::text : col::bg);
    fileBtn.setEnabled (state == AnalysisService::State::Idle);
    refBtn.setEnabled (state == AnalysisService::State::Idle);
    profileBox.setEnabled (! busy);
    exportBtn.setEnabled (report.valid && ! capturing);
    copyBtn.setEnabled (report.valid && ! capturing);

    const bool previewing = service.isPreviewing();
    stopAuditionBtn.setVisible (previewing);
    timeline.setPlayhead (previewing && ! service.isPreviewReference(), service.getPreviewPositionSec());

    meters.setValues (capturing || busy, waiting, service.getCapturedSeconds(),
                      service.getLivePeakDb(), service.getLiveLufs(), service.getLiveCorrelation());

    juce::String s = service.getStatusMessage();
    if (juce::Time::getMillisecondCounter() < flashUntil) s = flashText;
    else if (waiting) s = HE ("ממתין לנגינה. לחצו Play באבלטון (ההקלטה רצה רק בזמן נגינה).");
    else if (previewing)
    {
        const int m = service.getPreviewMode();
        s = (service.isPreviewReference() ? HE ("משמיע רפרנס (מותאם עוצמה)") : HE ("משמיע מהמיקס"))
          + (m == 1 ? HE (" · סולו תחום") : m == 2 ? HE (" · מונו") : juce::String())
          + HE (". לחיצה על Play באבלטון עוצרת את ההשמעה.");
    }
    const float prog = service.getFileProgress();
    if (s != statusText || std::abs (prog - progress) > 0.002f)
    {
        statusText = s;
        progress = prog;
        repaint (statusArea);
    }

    const bool mixA = service.hasAudio (false), refA = service.hasAudio (true);
    if (service.getReportVersion() != lastVersion) refresh();
    else if (mixA != lastMixAudio || refA != lastRefAudio)
    {
        lastMixAudio = mixA; lastRefAudio = refA;
        content.setAudioAvailable (mixA, refA);
        timeline.setAudioAvailable (mixA);
    }
}

void DaliMixEditor::refresh()
{
    lastVersion = service.getReportVersion();
    service.getReport (report, metrics);
    service.getReference (refMetrics);
    const auto tf = service.timeFormatter();
    readiness.setReport (report);
    health.setReport (report);
    timeline.setData (metrics, tf);
    content.setData (report, metrics, refMetrics, service.getReferenceName(), tf);
    lastMixAudio = service.hasAudio (false);
    lastRefAudio = service.hasAudio (true);
    content.setAudioAvailable (lastMixAudio, lastRefAudio);
    timeline.setAudioAvailable (lastMixAudio);
    updateTabLabels();
}

void DaliMixEditor::updateTabLabels()
{
    int flagged = 0, open = 0;
    for (auto& f : report.findings) if (! f.ignored && (int) f.status >= (int) pa::Status::Warning) ++flagged;
    for (auto& t : report.technical) if (t.status != pa::Status::Ok) ++open;

    juce::String f = HE ("ממצאים");
    if (report.valid) f << " (" << flagged << ")";
    tabs[0].setButtonText (f);
    tabs[1].setButtonText (HE ("מבנה הטראק"));
    tabs[2].setButtonText (refMetrics.valid ? HE ("רפרנס (נטען)") : HE ("רפרנס"));
    juce::String h = HE ("מסירה למאסטר");
    if (report.valid && open > 0) h << " (" << open << ")";
    tabs[3].setButtonText (h);
    tabs[4].setButtonText (HE ("מדדים מתקדמים"));
}

void DaliMixEditor::selectTab (Mode m)
{
    content.setMode (m);
    viewport.setViewPosition (0, 0);
}

// ============================================================================
//  Audition
// ============================================================================
void DaliMixEditor::auditionEvent (int idx, int mode)
{
    if (idx < 0 || idx >= (int) metrics.events.size()) return;
    const auto& e = metrics.events[(size_t) idx];
    const double s = juce::jmax (0.0, e.startSec - 0.75);
    const double end = juce::jmin (juce::jmax (e.endSec + 0.75, s + 3.0), s + 10.0);

    auto pm = AnalysisService::PreviewMode::Full;
    double lo = 0, hi = 0;
    if (mode == 1)
    {
        if (e.type == pa::EventType::Phase) pm = AnalysisService::PreviewMode::Mono;
        else if (e.freqHi > 0) { pm = AnalysisService::PreviewMode::Band; lo = e.freqLo; hi = e.freqHi; }
    }
    service.startPreview (false, s, end, pm, lo, hi);
}

void DaliMixEditor::auditionLoudestSection (bool reference)
{
    const pa::Metrics& m = reference ? refMetrics : metrics;
    double s = 0.0, e = juce::jmin (m.durationSec, 16.0);
    double best = -200.0;
    for (auto& sc : m.sections)
        if (sc.lufs > best) { best = sc.lufs; s = sc.startSec; e = juce::jmin (sc.endSec, sc.startSec + 16.0); }
    service.startPreview (reference, s, e, AnalysisService::PreviewMode::Full, 0, 0);
}

void DaliMixEditor::auditionFinding (int idx, bool solo)
{
    if (idx < 0 || idx >= (int) report.findings.size()) return;
    const auto& f = report.findings[(size_t) idx];

    // strongest event of the finding's type, else the loudest section
    int evIdx = -1;
    for (size_t i = 0; i < metrics.events.size(); ++i)
        if ((int) metrics.events[i].type == f.eventType
            && (evIdx < 0 || std::abs (metrics.events[i].value) > std::abs (metrics.events[(size_t) evIdx].value)))
            evIdx = (int) i;

    double s = 0.0, e = juce::jmin (metrics.durationSec, 12.0);
    if (evIdx >= 0)
    {
        const auto& ev = metrics.events[(size_t) evIdx];
        s = juce::jmax (0.0, ev.startSec - 0.75);
        e = juce::jmin (juce::jmax (ev.endSec + 0.75, s + 3.0), s + 10.0);
        timeline.selectFirstOfType (f.eventType);
    }
    else
    {
        double best = -200.0;
        for (auto& sc : metrics.sections)
            if (sc.lufs > best) { best = sc.lufs; s = sc.startSec; e = juce::jmin (sc.endSec, sc.startSec + 12.0); }
    }

    auto pm = AnalysisService::PreviewMode::Full;
    double lo = 0, hi = 0;
    if (solo)
    {
        if (f.soloMode == pa::SoloMode::Mono) pm = AnalysisService::PreviewMode::Mono;
        else if (f.soloMode == pa::SoloMode::Band) { pm = AnalysisService::PreviewMode::Band; lo = f.soloLo; hi = f.soloHi; }
    }
    service.startPreview (false, s, e, pm, lo, hi);
}

void DaliMixEditor::handleAction (Action a, int idx)
{
    switch (a)
    {
        case Action::Play:           auditionFinding (idx, false); break;
        case Action::Solo:           auditionFinding (idx, true); break;
        case Action::Ignore:
            if (idx >= 0 && idx < (int) report.findings.size())
                service.setIgnored (report.findings[(size_t) idx].id, ! report.findings[(size_t) idx].ignored);
            break;
        case Action::LoadReference:  chooseFile (true); break;
        case Action::ClearReference: service.clearReference(); break;
        case Action::PlayMixDrop:    auditionLoudestSection (false); break;
        case Action::PlayRefDrop:    auditionLoudestSection (true); break;
        case Action::PlaySection:
            if (idx >= 0 && idx < (int) metrics.sections.size())
            {
                const auto& sc = metrics.sections[(size_t) idx];
                service.startPreview (false, sc.startSec, juce::jmin (sc.endSec, sc.startSec + 16.0), AnalysisService::PreviewMode::Full, 0, 0);
            }
            break;
        case Action::Toggle: break;
    }
}

// ============================================================================
//  Files / export
// ============================================================================
void DaliMixEditor::chooseFile (bool reference)
{
    chooser = std::make_unique<juce::FileChooser> (reference ? HE ("בחרו טראק רפרנס") : HE ("בחרו את קובץ המיקס"),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, reference] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              if (reference) service.analyzeReference (f);
                              else           service.analyzeFile (f);
                          });
}

juce::String DaliMixEditor::reportText() const
{
    return str (pa::reportToText (report, metrics, service.getProfile(), refMetrics.valid ? &refMetrics : nullptr, service.timeFormatter()));
}

void DaliMixEditor::onExport()
{
    if (! report.valid) return;
    const auto text = reportText();
    auto def = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("DaliMix Report.txt");
    chooser = std::make_unique<juce::FileChooser> (HE ("שמירת הדוח"), def, "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, text] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("txt")) f = f.withFileExtension ("txt");
                              if (f.replaceWithText (text, false, false, "\n")) flash (HE ("הדוח נשמר: ") + f.getFullPathName());
                              else flash (HE ("לא ניתן לשמור בתיקייה הזו. נסו מיקום אחר."));
                          });
}

void DaliMixEditor::onCopy()
{
    if (! report.valid) return;
    juce::SystemClipboard::copyTextToClipboard (reportText());
    flash (HE ("הדוח הועתק. אפשר להדביק אותו במייל למהנדס המאסטרינג."));
}

void DaliMixEditor::flash (const juce::String& msg)
{
    flashText = msg;
    flashUntil = juce::Time::getMillisecondCounter() + 4000;
}
