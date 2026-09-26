#pragma once
// ============================================================================
//  DaliMix - AnalysisService
//  - Audio thread -> lock-free FIFO -> worker thread -> AnalysisEngine
//  - Tracks Ableton arrangement time + bar numbers for every captured sample
//  - Keeps a 16-bit copy of the analysed audio (mix + reference) so problems
//    can be auditioned inside the plugin: full, band-solo or mono
//  - Offline analysis of a mix file or a reference file
// ============================================================================
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <deque>
#include <set>

#include "AnalysisEngine.h"
#include "ReportBuilder.h"

struct HostTime
{
    bool   valid   = false;
    double seconds = 0.0, ppq = 0.0, bpm = 120.0;
    int    num = 4, den = 4;
};

class AnalysisService : private juce::Thread
{
public:
    enum class State       { Idle = 0, Capturing, AnalyzingFile, AnalyzingReference };
    enum class PreviewMode { Full = 0, Band, Mono };

    AnalysisService();
    ~AnalysisService() override;

    // ---- audio thread ---------------------------------------------------------
    void setHostSampleRate (double sr) noexcept { hostSampleRate.store (sr); }
    bool isCapturing() const noexcept           { return capturing.load(); }
    void pushAudio (const float* left, const float* right, int numSamples, const HostTime& ht) noexcept;
    bool renderPreview (float* left, float* right, int numSamples, double hostSR) noexcept;
    void stopPreviewFromAudio() noexcept        { previewActive.store (false); }

    // ---- message thread -------------------------------------------------------
    void startCapture();
    void stopCapture();
    void analyzeFile (const juce::File& file);
    void analyzeReference (const juce::File& file);
    void cancelFileAnalysis() noexcept { cancelFlag.store (true); }
    void clearReference();

    void setProfile (int index);
    int  getProfile() const noexcept { return profile.load(); }
    void setIgnored (const std::string& findingId, bool ignored);

    void startPreview (bool reference, double startSec, double endSec, PreviewMode mode, double lo, double hi);
    void stopPreview() noexcept               { previewActive.store (false); }
    bool isPreviewing() const noexcept        { return previewActive.load(); }
    bool isPreviewReference() const noexcept  { return previewRef.load(); }
    int  getPreviewMode() const noexcept      { return previewMode.load(); }
    double getPreviewPositionSec() const noexcept { return previewPosSec.load(); }
    bool hasAudio (bool reference) const;

    State  getState() const noexcept            { return state.load(); }
    double getCapturedSeconds() const noexcept  { return engine.getProcessedSeconds(); }
    float  getFileProgress() const noexcept     { return fileProgress.load(); }
    float  getLivePeakDb() const noexcept       { return engine.getLivePeakDb(); }
    float  getLiveLufs() const noexcept         { return engine.getLiveMomentaryLufs(); }
    float  getLiveCorrelation() const noexcept  { return engine.getLiveCorrelation(); }

    int  getReportVersion() const noexcept { return reportVersion.load(); }
    bool getReport (pa::Report& r, pa::Metrics& m) const;
    bool getReference (pa::Metrics& m) const;
    juce::String getReferenceName() const;
    juce::String getStatusMessage() const;
    juce::String getSourceName() const;

    std::string       formatTime (double seconds) const;     // "2:14 · Bar 65" when captured from Ableton
    pa::TimeFormatter timeFormatter() const { return [this] (double s) { return formatTime (s); }; }

private:
    enum class Cmd { Start, Stop, File, Reference };
    struct Command { Cmd type; juce::File file; };

    struct Segment { int64_t engineSample; double hostSec, ppq, bpm; int num, den; };

    struct AudioStore
    {
        std::vector<int16_t> L, R;
        double sr = 48000.0;
        size_t cap = 0;
        void reset (double sampleRate);
        void append (const float* l, const float* r, int n);
    };

    struct Bq
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void design (bool highpass, double fs, double f);
    };

    void run() override;
    void handle (const Command& c);
    void drainFifo (bool feed);
    void runFileAnalysis (const juce::File& f, bool reference);
    void finish (const juce::String& sourceName);
    void rebuildReport();
    void setStatus (const juce::String& s);
    void stopPreviewAndWait();

    pa::AnalysisEngine engine, refEngine;

    static constexpr int kFifoSize = 1 << 19;
    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> fifoL, fifoR, fifoT, fifoP;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int>    hostNum { 4 }, hostDen { 4 };
    double lastHostEnd = -1.0;
    bool   haveHost = false;

    std::atomic<bool>   capturing { false }, cancelFlag { false };
    std::atomic<State>  state { State::Idle };
    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<float>  fileProgress { 0.0f };
    std::atomic<int>    profile { 0 }, reportVersion { 0 };

    juce::CriticalSection queueLock;
    std::deque<Command> queue;

    mutable juce::CriticalSection reportLock;
    pa::Metrics  metrics, refMetrics;
    pa::Report   report;
    juce::String statusMessage, sourceName, refName;
    std::set<std::string> ignored;
    std::vector<Segment> segments;
    bool   mixFromCapture = false;
    double mixSampleRate = 48000.0;

    // ---- preview (audition) ------------------------------------------------------
    AudioStore mixStore, refStore;
    std::atomic<bool>    previewActive { false }, previewInUse { false }, previewRef { false };
    std::atomic<int>     previewMode { 0 }, previewVersion { 0 };
    std::atomic<int64_t> previewStart { 0 }, previewEnd { 0 };
    std::atomic<double>  previewLo { 20.0 }, previewHi { 20000.0 }, previewPosSec { 0.0 };
    std::atomic<float>   previewGain { 1.0f };
    // audio-thread only
    int    renderVersion = -1;
    double renderPos = 0.0;
    Bq     hp[2][2], lp[2][2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisService)
};
