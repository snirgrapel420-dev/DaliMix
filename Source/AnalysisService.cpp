#include "AnalysisService.h"

#define HE(literal) juce::String::fromUTF8 (literal)

namespace
{
constexpr double kStoreMinutes = 12.0;
constexpr double kPi = 3.14159265358979323846;
}

// ============================================================================
void AnalysisService::AudioStore::reset (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    cap = (size_t) (kStoreMinutes * 60.0 * sr);
    L.clear(); R.clear();
    L.shrink_to_fit(); R.shrink_to_fit();
}

void AnalysisService::AudioStore::append (const float* l, const float* r, int n)
{
    if (L.size() >= cap || n <= 0) return;
    const size_t m = std::min ((size_t) n, cap - L.size());
    auto q = [] (float x) { return (int16_t) juce::jlimit (-32767.0f, 32767.0f, x * 32767.0f); };
    for (size_t i = 0; i < m; ++i) { L.push_back (q (l[i])); R.push_back (q (r[i])); }
}

void AnalysisService::Bq::design (bool highpass, double fs, double f)
{
    f = juce::jlimit (10.0, fs * 0.45, f);
    const double w0 = 2.0 * kPi * f / fs, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.7071);
    const double a0 = 1.0 + alpha;
    if (highpass) { b0 = (1.0 + cw) * 0.5 / a0; b1 = -(1.0 + cw) / a0; }
    else          { b0 = (1.0 - cw) * 0.5 / a0; b1 =  (1.0 - cw) / a0; }
    b2 = b0;
    a1 = -2.0 * cw / a0;
    a2 = (1.0 - alpha) / a0;
    z1 = z2 = 0.0;
}

// ============================================================================
AnalysisService::AnalysisService() : juce::Thread ("DaliMix worker")
{
    fifoL.assign ((size_t) kFifoSize, 0.0f);
    fifoR.assign ((size_t) kFifoSize, 0.0f);
    fifoT.assign ((size_t) kFifoSize, -1.0f);
    fifoP.assign ((size_t) kFifoSize, 0.0f);
    statusMessage = HE ("מוכן. לחצו 'התחל ניתוח' ונגנו את הטראק, או טענו קובץ.");
    startThread();
}

AnalysisService::~AnalysisService()
{
    capturing.store (false);
    cancelFlag.store (true);
    stopPreviewAndWait();
    stopThread (4000);
}

// ---------------------------------------------------------------- audio thread
void AnalysisService::pushAudio (const float* left, const float* right, int n, const HostTime& ht) noexcept
{
    if (! capturing.load() || n <= 0) return;
    if (right == nullptr) right = left;

    hostBpm.store (ht.bpm);
    hostNum.store (ht.num);
    hostDen.store (ht.den);
    const double sr = hostSampleRate.load();

    const auto scope = fifo.write (n);
    auto copy = [&] (int start, int size, int srcOffset)
    {
        for (int i = 0; i < size; ++i)
        {
            const int s = srcOffset + i;
            fifoL[(size_t) (start + i)] = left[s];
            fifoR[(size_t) (start + i)] = right[s];
            fifoT[(size_t) (start + i)] = ht.valid ? (float) (ht.seconds + s / sr) : -1.0f;
            fifoP[(size_t) (start + i)] = (float) (ht.ppq + (s / sr) * ht.bpm / 60.0);
        }
    };
    if (scope.blockSize1 > 0) copy (scope.startIndex1, scope.blockSize1, 0);
    if (scope.blockSize2 > 0) copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
}

bool AnalysisService::renderPreview (float* left, float* right, int n, double hostSR) noexcept
{
    if (! previewActive.load()) return false;
    previewInUse.store (true);
    if (! previewActive.load()) { previewInUse.store (false); return false; }

    const AudioStore& st = previewRef.load() ? refStore : mixStore;
    const int64_t size = (int64_t) st.L.size();
    if (size < 4) { previewInUse.store (false); return false; }

    const int mode = previewMode.load();
    const int ver = previewVersion.load();
    if (ver != renderVersion)
    {
        renderVersion = ver;
        renderPos = (double) previewStart.load();
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 2; ++k)
            {
                hp[ch][k].design (true,  hostSR, previewLo.load());
                lp[ch][k].design (false, hostSR, previewHi.load());
            }
    }

    const double s0 = (double) juce::jlimit ((int64_t) 0, size - 2, previewStart.load());
    const double s1 = (double) juce::jlimit ((int64_t) 1, size - 2, previewEnd.load());
    const double step = st.sr / hostSR;
    const double fade = 0.01 * st.sr;
    const float  gain = previewGain.load();

    for (int i = 0; i < n; ++i)
    {
        if (renderPos >= s1 || renderPos < s0) renderPos = s0;
        const size_t idx = (size_t) renderPos;
        const double fr = renderPos - (double) idx;
        double l = (st.L[idx] * (1.0 - fr) + st.L[idx + 1] * fr) / 32767.0;
        double r = (st.R[idx] * (1.0 - fr) + st.R[idx + 1] * fr) / 32767.0;

        if (mode == (int) PreviewMode::Mono)
        {
            l = r = 0.5 * (l + r);
        }
        else if (mode == (int) PreviewMode::Band)
        {
            l = lp[0][1].process (lp[0][0].process (hp[0][1].process (hp[0][0].process (l))));
            r = lp[1][1].process (lp[1][0].process (hp[1][1].process (hp[1][0].process (r))));
        }
        const double g = gain * std::min (1.0, std::min ((renderPos - s0) / fade, (s1 - renderPos) / fade));
        left[i] = (float) (l * g);
        if (right != nullptr) right[i] = (float) (r * g);
        else left[i] = (float) (0.5 * (l + r) * g);
        renderPos += step;
    }
    previewPosSec.store (renderPos / st.sr);
    previewInUse.store (false);
    return true;
}

// ------------------------------------------------------------- message thread
void AnalysisService::startCapture()
{
    stopPreview();
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::Start, {} });
    notify();
}

void AnalysisService::stopCapture()
{
    capturing.store (false);
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::Stop, {} });
    notify();
}

void AnalysisService::analyzeFile (const juce::File& file)
{
    stopPreview();
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::File, file });
    notify();
}

void AnalysisService::analyzeReference (const juce::File& file)
{
    stopPreview();
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::Reference, file });
    notify();
}

void AnalysisService::clearReference()
{
    if (state.load() != State::Idle) return;
    stopPreviewAndWait();
    {
        const juce::ScopedLock sl (reportLock);
        refMetrics = pa::Metrics();
        refName.clear();
    }
    refStore.reset (48000.0);
    reportVersion.fetch_add (1);
}

void AnalysisService::setProfile (int index)
{
    profile.store (juce::jlimit (0, pa::getNumProfiles() - 1, index));
    rebuildReport();
}

void AnalysisService::setIgnored (const std::string& id, bool ign)
{
    {
        const juce::ScopedLock sl (reportLock);
        if (ign) ignored.insert (id); else ignored.erase (id);
    }
    rebuildReport();
}

void AnalysisService::rebuildReport()
{
    pa::Metrics m;
    std::set<std::string> ign;
    {
        const juce::ScopedLock sl (reportLock);
        if (! metrics.valid) return;
        m = metrics;
        ign = ignored;
    }
    pa::Report r = pa::buildReport (m, profile.load(), ign, timeFormatter());
    {
        const juce::ScopedLock sl (reportLock);
        report = r;
    }
    reportVersion.fetch_add (1);
}

bool AnalysisService::hasAudio (bool reference) const
{
    return state.load() == State::Idle && ! (reference ? refStore.L.empty() : mixStore.L.empty());
}

void AnalysisService::startPreview (bool reference, double startSec, double endSec, PreviewMode mode, double lo, double hi)
{
    if (state.load() != State::Idle) return;
    const AudioStore& st = reference ? refStore : mixStore;
    if (st.L.size() < 4) return;

    stopPreviewAndWait();
    if (endSec - startSec < 1.0) endSec = startSec + 1.0;
    previewRef.store (reference);
    previewMode.store ((int) mode);
    previewStart.store ((int64_t) (juce::jmax (0.0, startSec) * st.sr));
    previewEnd.store ((int64_t) (endSec * st.sr));
    previewLo.store (lo > 0 ? lo : 20.0);
    previewHi.store (hi > 0 ? hi : 20000.0);

    float g = 1.0f;
    {
        const juce::ScopedLock sl (reportLock);
        if (reference && metrics.valid && refMetrics.valid)   // loudness-matched reference
            g = (float) std::pow (10.0, juce::jlimit (-24.0, 6.0, metrics.integratedLufs - refMetrics.integratedLufs) / 20.0);
    }
    if (mode == PreviewMode::Band) g *= 1.41f;   // +3 dB make-up for band solo
    previewGain.store (g);
    previewPosSec.store (startSec);
    previewVersion.fetch_add (1);
    previewActive.store (true);
}

void AnalysisService::stopPreviewAndWait()
{
    previewActive.store (false);
    for (int i = 0; i < 200 && previewInUse.load(); ++i) juce::Thread::sleep (1);
}

bool AnalysisService::getReport (pa::Report& r, pa::Metrics& m) const
{
    const juce::ScopedLock sl (reportLock);
    r = report;
    m = metrics;
    return metrics.valid;
}

bool AnalysisService::getReference (pa::Metrics& m) const
{
    const juce::ScopedLock sl (reportLock);
    m = refMetrics;
    return refMetrics.valid;
}

juce::String AnalysisService::getReferenceName() const { const juce::ScopedLock sl (reportLock); return refName; }
juce::String AnalysisService::getStatusMessage() const { const juce::ScopedLock sl (reportLock); return statusMessage; }
juce::String AnalysisService::getSourceName() const    { const juce::ScopedLock sl (reportLock); return sourceName; }
void AnalysisService::setStatus (const juce::String& s) { const juce::ScopedLock sl (reportLock); statusMessage = s; }

std::string AnalysisService::formatTime (double sec) const
{
    const juce::ScopedLock sl (reportLock);
    if (! mixFromCapture || segments.empty()) return pa::defaultTime (sec);

    const int64_t s = (int64_t) (sec * mixSampleRate);
    const Segment* seg = &segments.front();
    for (auto& g : segments)
    {
        if (g.engineSample <= s) seg = &g;
        else break;
    }
    const double dt   = (double) (s - seg->engineSample) / mixSampleRate;
    const double host = seg->hostSec + dt;
    const double ppq  = seg->ppq + dt * seg->bpm / 60.0;
    const double barLen = seg->num * 4.0 / juce::jmax (1, seg->den);
    const int bar = (int) std::floor (ppq / barLen) + 1;
    return pa::defaultTime (host) + " · Bar " + std::to_string (bar);
}

// -------------------------------------------------------------- worker thread
void AnalysisService::run()
{
    while (! threadShouldExit())
    {
        bool has = false;
        Command cmd { Cmd::Stop, {} };
        {
            const juce::ScopedLock sl (queueLock);
            if (! queue.empty()) { cmd = queue.front(); queue.pop_front(); has = true; }
        }
        if (has) handle (cmd);
        if (state.load() == State::Capturing) drainFifo (true);
        wait (15);
    }
}

void AnalysisService::drainFifo (bool feed)
{
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) break;
        const auto scope = fifo.read (ready);
        if (! feed) continue;

        auto chunk = [&] (int start, int size)
        {
            if (size <= 0) return;
            const float t0 = fifoT[(size_t) start];
            if (t0 >= 0.0f)
            {
                const double expected = lastHostEnd + 1.0 / hostSampleRate.load();
                if (! haveHost || std::abs ((double) t0 - expected) > 0.02)
                {
                    const juce::ScopedLock sl (reportLock);
                    segments.push_back ({ engine.getTotalSamples(), (double) t0, (double) fifoP[(size_t) start],
                                          hostBpm.load(), hostNum.load(), hostDen.load() });
                }
                lastHostEnd = fifoT[(size_t) (start + size - 1)];
                haveHost = true;
            }
            engine.process (fifoL.data() + start, fifoR.data() + start, size);
            mixStore.append (fifoL.data() + start, fifoR.data() + start, size);
        };
        chunk (scope.startIndex1, scope.blockSize1);
        chunk (scope.startIndex2, scope.blockSize2);
    }
}

void AnalysisService::handle (const Command& c)
{
    switch (c.type)
    {
        case Cmd::Start:
        {
            if (state.load() != State::Idle) return;
            stopPreviewAndWait();
            drainFifo (false);
            const double sr = hostSampleRate.load();
            engine.prepare (sr);
            mixStore.reset (sr);
            {
                const juce::ScopedLock sl (reportLock);
                segments.clear();
                mixFromCapture = true;
                mixSampleRate = sr;
            }
            haveHost = false;
            lastHostEnd = -1.0;
            state.store (State::Capturing);
            capturing.store (true);
            setStatus (HE ("מקליט לניתוח... נגנו את הטראק מההתחלה ועד הסוף, ואז לחצו 'עצור והפק דוח'."));
            break;
        }
        case Cmd::Stop:
        {
            if (state.load() != State::Capturing) return;
            capturing.store (false);
            drainFifo (true);
            finish (HE ("הקלטה מאבלטון"));
            state.store (State::Idle);
            break;
        }
        case Cmd::File:
        case Cmd::Reference:
        {
            if (state.load() != State::Idle) return;
            stopPreviewAndWait();
            runFileAnalysis (c.file, c.type == Cmd::Reference);
            state.store (State::Idle);
            break;
        }
    }
}

void AnalysisService::runFileAnalysis (const juce::File& file, bool reference)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        setStatus (HE ("לא ניתן לפתוח את הקובץ. נתמכים: WAV, AIFF, FLAC, MP3, OGG."));
        return;
    }

    state.store (reference ? State::AnalyzingReference : State::AnalyzingFile);
    cancelFlag.store (false);
    fileProgress.store (0.0f);

    auto& eng   = reference ? refEngine : engine;
    auto& store = reference ? refStore : mixStore;
    eng.prepare (reader->sampleRate);
    store.reset (reader->sampleRate);
    setStatus ((reference ? HE ("מנתח רפרנס: ") : HE ("מנתח קובץ: ")) + file.getFileName());

    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buf (2, chunk);
    const juce::int64 len = reader->lengthInSamples;
    const bool mono = reader->numChannels < 2;

    for (juce::int64 pos = 0; pos < len; pos += chunk)
    {
        if (threadShouldExit() || cancelFlag.load())
        {
            setStatus (HE ("הניתוח בוטל."));
            fileProgress.store (0.0f);
            return;
        }
        const int n = (int) juce::jmin ((juce::int64) chunk, len - pos);
        reader->read (&buf, 0, n, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = mono ? l : buf.getReadPointer (1);
        eng.process (l, r, n);
        store.append (l, r, n);
        fileProgress.store ((float) ((double) (pos + n) / (double) len));
    }

    if (reference)
    {
        pa::Metrics rm = refEngine.finalize();
        {
            const juce::ScopedLock sl (reportLock);
            refMetrics = rm;
            refName = file.getFileName();
            statusMessage = rm.valid ? HE ("הרפרנס נותח: ") + file.getFileName() : HE ("לא נמצא מספיק אודיו ברפרנס.");
        }
        reportVersion.fetch_add (1);
        return;
    }

    {
        const juce::ScopedLock sl (reportLock);
        segments.clear();
        mixFromCapture = false;
        mixSampleRate = reader->sampleRate;
    }
    finish (file.getFileName());
}

void AnalysisService::finish (const juce::String& source)
{
    const double secs = engine.getProcessedSeconds();
    if (secs < 3.0)
    {
        setStatus (HE ("ההקלטה קצרה מדי. צריך לפחות 3 שניות של אודיו (מומלץ את כל הטראק)."));
        return;
    }

    pa::Metrics m = engine.finalize();
    std::set<std::string> ign;
    {
        const juce::ScopedLock sl (reportLock);
        ign = ignored;
    }
    pa::Report r = pa::buildReport (m, profile.load(), ign, timeFormatter());
    {
        const juce::ScopedLock sl (reportLock);
        metrics = m;
        report = r;
        sourceName = source;
        if (! m.valid) statusMessage = HE ("לא נמצא מספיק אודיו שאינו שקט. בדקו שהטראק באמת התנגן.");
        else           statusMessage = HE ("הדוח מוכן: ") + juce::String (secs, 0) + HE (" שניות נותחו.");
    }
    reportVersion.fetch_add (1);
}
