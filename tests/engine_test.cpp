// Build: g++ -std=c++17 -O2 -I../Source engine_test.cpp ../Source/AnalysisEngine.cpp ../Source/ReportBuilder.cpp -o engine_test
// Synthetic trance-like track with planted problems at known times.
#include "AnalysisEngine.h"
#include "ReportBuilder.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
using namespace pa;
static const double PI = 3.14159265358979323846;

int main()
{
    const double fs = 48000, bpm = 140, beat = 60.0 / bpm;
    // intro 0-24 | build 24-40 | drop 40-80 | break 80-104 | drop 104-144 | outro 144-160
    const int N = (int) (fs * 160);
    std::vector<float> L (N), R (N);
    std::mt19937 rng (7); std::normal_distribution<double> nd;
    double ph = 0, lp1 = 0, lp2 = 0, hsL = 0;
    for (int i = 0; i < N; ++i)
    {
        const double t = i / fs;
        const bool drop  = (t >= 40 && t < 80) || (t >= 104 && t < 144);
        const bool brk   = t >= 80 && t < 104;
        const bool build = t >= 24 && t < 40;
        const bool intro = t < 24, outro = t >= 144;

        // kick (4/4) everywhere except break
        double kick = 0;
        const double tk = std::fmod (t, beat);
        if (! brk)
        {
            if (tk < 1.0 / fs) ph = 0;
            ph += 2 * PI * (50 + 120 * std::exp (-tk * 40)) / fs;
            kick = (intro || outro ? 0.35 : 0.5) * std::exp (-tk * 16) * std::sin (ph);
        }
        // rolling off-beat bass (A1 = 55 Hz) with natural ducking, only in drops
        double bass = 0;
        if (drop) { const double env = std::min (1.0, tk / 0.08); bass = 0.18 * env * std::sin (2 * PI * 55 * t); }
        // pad/lead bed: filtered noise, wider in break
        const double w1 = nd (rng), w2 = nd (rng);
        lp1 = 0.9 * lp1 + 0.1 * w1; lp2 = 0.9 * lp2 + 0.1 * w2;
        const double bedAmt = brk ? 0.10 : build ? 0.05 + 0.05 * (t - 24) / 16 : drop ? 0.08 : 0.03;
        double bl = bedAmt * (0.8 * lp1 + 0.2 * lp2), br = bedAmt * (0.2 * lp1 + 0.8 * lp2);
        // planted harsh lead 3.8 kHz burst: 60-63 s
        if (t >= 60 && t < 63) { hsL = 0.12 * std::sin (2 * PI * 3800 * t); bl += hsL; br += hsL; }
        double l = kick + bass + bl, r = kick + bass + br;
        // planted phase problem: 120-123 s, right channel of the bed inverted strongly
        if (t >= 120 && t < 123) r = -(0.1 * lp1 + 0.1 * lp2) * 2.0 + kick * 0.0;
        L[i] = (float) l; R[i] = (float) r;
    }

    AnalysisEngine e;
    e.prepare (fs);
    for (int i = 0; i < N; i += 512) e.process (&L[i], &R[i], std::min (512, N - i));
    Metrics m = e.finalize();

    std::printf ("LUFS %.1f TP %.1f PLR %.1f LRA %.1f corr %.2f\n", m.integratedLufs, m.truePeakDb, m.plr, m.lra, m.correlation);
    std::printf ("Kick %.1fHz bass %.1fHz overlap %.0f%% len %.0fms floor %.1fdB hits %d root %s(%.2f)\n",
                 m.kickFreqHz, m.bassFreqHz, m.kickBassOverlapPct, m.kickLengthMs, m.kickFloorRelDb, m.kickHitsMeasured,
                 pitchName (m.rootPitchClass).c_str(), m.rootStrength);
    std::printf ("SECTIONS:\n");
    for (auto& s : m.sections)
        std::printf ("  %-6s %5.0f-%5.0f  %.1f LUFS low %.0f%% width %.0f%% dens %.0f%%\n", sectionName (s.type), s.startSec, s.endSec, s.lufs, s.lowPct, s.widthPct, s.densityPct);
    std::printf ("EVENTS:\n");
    for (auto& ev : m.events) std::printf ("  type %d  %.2f-%.2f sev %d val %.2f band %.0f-%.0f\n", (int) ev.type, ev.startSec, ev.endSec, ev.severity, ev.value, ev.freqLo, ev.freqHi);

    Report r = buildReport (m, 0);
    std::printf ("READINESS: %s\n", r.readinessEn.c_str());
    std::FILE* f = std::fopen ("report.txt", "w");
    std::fputs (reportToText (r, m, 0, &m).c_str(), f);
    std::fclose (f);
    return 0;
}
