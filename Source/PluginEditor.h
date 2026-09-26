#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Components.h"

class DaliMixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DaliMixEditor (DaliMixProcessor&);
    ~DaliMixEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void selectTab (ui::ContentView::Mode m);
    void updateTabLabels();
    void handleAction (ui::ContentView::Action a, int index);
    void auditionFinding (int index, bool solo);
    void auditionEvent (int eventIndex, int mode);
    void auditionLoudestSection (bool reference);
    void chooseFile (bool reference);
    void onExport();
    void onCopy();
    void flash (const juce::String& msg);
    juce::String reportText() const;

    DaliMixProcessor& proc;
    AnalysisService& service;
    theme::LookAndFeel lnf;

    juce::TextButton captureBtn, fileBtn, refBtn, stopAuditionBtn, exportBtn, copyBtn;
    juce::ToggleButton playingOnlyToggle;
    juce::ComboBox profileBox;

    ui::LiveMeters     meters;
    ui::HealthGrid     health;
    ui::ReadinessPanel readiness;
    ui::Timeline       timeline;

    juce::TextButton tabs[5];
    juce::Viewport   viewport;
    ui::ContentView  content;

    std::unique_ptr<juce::FileChooser> chooser;

    pa::Report  report;
    pa::Metrics metrics, refMetrics;
    int lastVersion = -1;
    bool lastMixAudio = false, lastRefAudio = false;
    juce::String statusText, flashText;
    juce::uint32 flashUntil = 0;
    float progress = 0.0f;
    juce::Rectangle<int> headerArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaliMixEditor)
};
