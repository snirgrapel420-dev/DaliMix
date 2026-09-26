#pragma once
// ============================================================================
//  DaliMix (Dali Audio) - AnalysisEngine
//  Pure C++17 (no JUCE) so it can be tested outside the plugin.
//  Feed it stereo audio with process(), then call finalize() to get Metrics.
// ============================================================================
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

namespace pa
{

struct Resonance
{
    double freqHz   = 0.0;
    double excessDb = 0.0;   // how much it sticks out above its 1-octave neighbourhood
};

enum class EventType { Harshness = 0, KickBass, Phase, Balance, Clipping };
constexpr int kNumEventTypes = 5;

struct TimelineEvent
{
    EventType type = EventType::Harshness;
    double startSec = 0.0, endSec = 0.0;
    int    severity = 1;            // 1 = worth checking, 2 = problem
    double value    = 0.0;          // type specific (dB, %, correlation, count)
    double freqLo   = 0.0, freqHi = 0.0;   // band to audition (0 = full range)
    double peakHz   = 0.0;
};

enum class SectionType { Intro = 0, Build, Break, Drop, Outro, Main };

struct Section
{
    SectionType type = SectionType::Main;
    double startSec = 0.0, endSec = 0.0;
    double lufs = -120.0;
    double lowPct = 0.0, widthPct = 0.0, densityPct = 0.0;   // 0..100, relative to the track
};

struct Metrics
{
    bool   valid       = false;
    double sampleRate  = 0.0;
    double durationSec = 0.0;

    // ---- Levels -----------------------------------------------------------
    double samplePeakDb     = -120.0;
    double truePeakDb       = -120.0;
    double rmsDb            = -120.0;   // gated (ignores silence)
    double crestDb          = 0.0;
    double integratedLufs   = -120.0;
    double maxShortTermLufs = -120.0;
    double lra              = 0.0;      // loudness range, LU
    double plr              = 0.0;      // true peak - integrated loudness

    // ---- Clipping / technical --------------------------------------------
    int64_t clipEvents       = 0;       // >=3 consecutive samples at full scale
    int64_t clippedSamples   = 0;
    int64_t oversFloat       = 0;       // samples above 1.0 (float headroom)
    int64_t interSampleOvers = 0;       // true peak > 0 dBTP while samples are not
    int64_t flatTopRuns      = 0;       // identical-sample plateaus (clipped then turned down)
    double  dcOffsetDb       = -120.0;

    // ---- Phase / stereo ---------------------------------------------------
    double correlation     = 1.0;
    double lowCorrelation  = 1.0;       // < 150 Hz
    double midCorrelation  = 1.0;       // 150 Hz - 4 kHz
    double highCorrelation = 1.0;       // > 4 kHz
    double negCorrTimePct  = 0.0;
    double corrP5          = 1.0;       // 5th percentile of per-frame correlation

    double monoLossDb     = 0.0;        // level change when summed to mono
    double lowMonoLossDb  = 0.0;
    double midMonoLossDb  = 0.0;
    double highMonoLossDb = 0.0;

    double balanceDb        = 0.0;      // + = left louder
    double lowBalanceDb     = 0.0;
    double midBalanceDb     = 0.0;
    double highBalanceDb    = 0.0;
    double imbalanceTimePct = 0.0;      // % of frames with |L-R| > 3 dB
    double lowSideToMidDb   = -120.0;   // side/mid energy below 120 Hz
    double midSideToMidDb   = -120.0;   // side/mid energy 250 Hz - 4 kHz (width)

    // ---- Spectrum ---------------------------------------------------------
    std::vector<double> bandFreqs;      // 1/3-octave centres
    std::vector<double> bandDb;         // band energy (relative dB)
    std::vector<double> bandDevDb;      // deviation from the mix's own tonal trend
    double tiltDbPerOct   = 0.0;        // slope of 1/3-oct energies (pink noise = 0)
    double subDevDb       = 0.0;        // 25-60 Hz
    double bassDevDb      = 0.0;        // 60-125 Hz
    double lowMidDevDb    = 0.0;        // 160-400 Hz
    double presenceDevDb  = 0.0;        // 2-5 kHz
    double sibilanceDevDb = 0.0;        // 5-10 kHz
    double airDevDb       = 0.0;        // 10-16 kHz
    double harshSpikePct  = 0.0;        // % of frames where 2-5k jumps >5 dB above its median
    std::vector<Resonance> resonances;
    std::vector<double>    holeFreqs;
    double midFlatness = 0.0;           // spectral flatness 250 Hz - 4 kHz (0..1)
    double centroidHz  = 0.0;

    // ---- Kick / bass ------------------------------------------------------
    bool   kickDetected = false;
    int    kickCount    = 0;
    double kickFreqHz   = 0.0;
    double bassFreqHz   = 0.0;
    double kickPunchDb  = 0.0;          // low-end level at kick hits vs. between hits
    double kickLengthMs = 0.0;          // median decay time of the kick body
    double kickFloorRelDb = 0.0;        // bass level just before a hit vs. the hit peak
    double kickBassOverlapPct = 0.0;    // 0..100 time+frequency+energy overlap
    double kickOverlapSpreadDb = 0.0;   // consistency across hits (IQR), lower = more certain
    int    kickHitsMeasured = 0;

    // ---- Tonal centre (estimate) -------------------------------------------
    int    rootPitchClass = -1;         // 0 = C ... 11 = B
    double rootStrength   = 0.0;        // peak / mean of chroma

    // ---- Level-independent region balance (for reference comparison) -------
    // Sub 20-60, Bass 60-150, Low-mid 150-500, Mid 500-2k, Presence 2-5k, High 5-10k, Air 10-20k
    double regionRelDb[7] {};

    // ---- Timeline -----------------------------------------------------------
    std::vector<TimelineEvent> events;
    std::vector<Section>       sections;
    std::vector<float>         loudPerSec;   // short-term LUFS, one value per second
};

class AnalysisEngine
{
public:
    AnalysisEngine();

    void    prepare (double sampleRate);                                   // also resets
    void    process (const float* left, const float* right, int numSamples); // right may == left
    Metrics finalize() const;

    double  getProcessedSeconds() const noexcept { return processedSeconds.load(); }
    int64_t getTotalSamples() const noexcept     { return totalSamples; }   // worker thread only
    float  getLivePeakDb()       const noexcept { return livePeakDb.load(); }
    float  getLiveMomentaryLufs() const noexcept { return liveMomentary.load(); }
    float  getLiveCorrelation()  const noexcept { return liveCorrelation.load(); }

    static constexpr int kFftOrder = 13;
    static constexpr int kFftSize  = 1 << kFftOrder;   // 8192
    static constexpr int kHop      = kFftSize / 4;     // 2048

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0.0; }
    };

    class Fft
    {
    public:
        void init (int order);
        void forward (std::vector<std::complex<double>>& data) const;
    private:
        int n = 0;
        std::vector<int> rev;
        std::vector<std::complex<double>> twiddle;
    };

    class TruePeak
    {
    public:
        TruePeak();
        void  reset();
        float push (int channel, float x) noexcept;   // max |x| of 4 interpolated points
    private:
        static constexpr int kPhases = 4, kTaps = 12;
        double coef[kPhases][kTaps] {};
        float  hist[2][kTaps] {};
    };

    void processFrame();
    void finaliseExtras (Metrics& m, const std::vector<double>& P) const;
    void handleOnsetBlock();
    void checkClip (int ch, float x) noexcept;

    static Biquad makeLowpass (double fs, double f, double q);

    double fs = 48000.0, binHz = 1.0;
    int numBins = kFftSize / 2 + 1;

    // FFT / frames
    Fft fft;
    std::vector<double> window;
    std::vector<std::complex<double>> fftBuf;
    std::vector<float> ringL, ringR;
    int ringPos = 0, ringFilled = 0, hopCounter = 0;
    std::vector<double> accPL, accPR, accC;
    int64_t nonSilentFrames = 0;

    int lowBins = 64;
    std::vector<float>  frameLow;            // mid power, bins [0, lowBins) per stored frame
    std::vector<double> frameCenters;        // seconds
    std::vector<float>  frameCorr, frameBal, framePres, frameFlat;
    std::vector<float>  framePresPeak, frameLowPow, frameTotPow, frameSidePow;
    static constexpr size_t kMaxFrames = 90000;   // ~1 hour at 48k

    // time domain
    int64_t totalSamples = 0;
    double  sumSq[2] {}, sum[2] {};
    float   peakLin = 0.0f, truePeakLin = 0.0f;
    TruePeak truePeak;
    int64_t clipEvents = 0, clippedSamples = 0, oversFloat = 0, interSampleOvers = 0, flatTopRuns = 0;
    int     clipRun[2] {}, flatRun[2] {};
    float   lastSample[2] {};

    // loudness (BS.1770)
    Biquad kPre[2], kRlb[2];
    int    subLen = 4800, subCount = 0;
    double subAcc = 0.0, rawSubAcc = 0.0;
    std::vector<double> subBlocks, rawSubBlocks;   // 100 ms mean squares

    // kick onset detection
    Biquad onsetLp1, onsetLp2;
    int    onsetBlock = 256, onsetCount = 0;
    double onsetAcc = 0.0, slowMean = 0.0, peakEnv = 0.0, peakDecay = 0.98;
    static constexpr int kOnsetHist = 16;
    double onsetHist[kOnsetHist] {};
    int    onsetHistPos = 0;
    int64_t lastOnsetSample = -((int64_t) 1 << 40);
    std::vector<double> onsets;   // seconds
    std::vector<float>  lowEnv;   // low-band energy per onset block (for kick envelope)
    std::vector<double> clipTimes, ispTimes;
    int64_t lastIspSample = -((int64_t) 1 << 40);

    // live values (read from other threads)
    std::atomic<double> processedSeconds { 0.0 };
    std::atomic<float>  livePeakDb { -120.0f }, liveMomentary { -120.0f }, liveCorrelation { 1.0f };
};

} // namespace pa
