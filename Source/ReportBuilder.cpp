#include "ReportBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace pa
{
namespace
{
// ----------------------------------------------------------------------------
//  Genre profiles - thresholds that depend on style
// ----------------------------------------------------------------------------
struct Profile
{
    const char* name;
    double lowAllowDb;              // sub+bass allowed above the mix trend
    double plrWarn, plrProblem;
    double lraLow, lraHigh;
    double crestWarn, crestProblem;
};

const Profile kProfiles[] = {
    { "טראנס / פסיי / Goa",            5.0, 10.0, 7.5, 3.0, 12.0, 10.0, 7.5 },
    { "אלקטרוני / EDM / האוס / טכנו",   5.0, 10.0, 7.5, 3.0, 12.0, 10.0, 7.5 },
    { "היפ-הופ / טראפ",                  6.0, 10.0, 7.5, 3.0, 12.0, 10.0, 7.5 },
    { "פופ / רוק",                        3.0, 11.0, 8.5, 4.0, 14.0, 11.0, 8.5 },
    { "אקוסטי / ג'אז / אמביינט",         1.0, 13.0, 10.0, 5.0, 20.0, 13.0, 10.0 },
};
constexpr int kNumProfiles = (int) (sizeof (kProfiles) / sizeof (kProfiles[0]));

// ----------------------------------------------------------------------------
std::string fmt (double v, int d = 1)
{
    char b[64];
    std::snprintf (b, sizeof (b), "%.*f", d, v);
    return b;
}
std::string fmtS (double v, int d = 1)
{
    char b[64];
    std::snprintf (b, sizeof (b), "%+.*f", d, v);
    return b;
}
std::string he (double v, int d = 1)        // "מינוס 1.8" inside Hebrew sentences
{
    if (v < 0.0) return "מינוס " + fmt (-v, d);
    return fmt (v, d);
}
std::string hz (double f)
{
    if (f >= 1000.0) return fmt (f / 1000.0, f >= 10000.0 ? 1 : 1) + "kHz";
    return fmt (f, 0) + "Hz";
}
std::string noteOf (double f)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (f <= 0.0) return "-";
    const int n = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
    return std::string (names[((n % 12) + 12) % 12]) + std::to_string (n / 12 - 1);
}
int pitchClassOf (double f)
{
    if (f <= 0.0) return -1;
    const int n = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
    return ((n % 12) + 12) % 12;
}
double clamp01 (double x) { return std::max (0.0, std::min (1.0, x)); }
Status worst (Status a, Status b) { return (int) a >= (int) b ? a : b; }

void add (std::string& s, const std::string& more)
{
    if (more.empty()) return;
    if (! s.empty()) s += " ";
    s += more;
}
void tip (Finding& f, const std::string& t)
{
    for (auto& e : f.tips) if (e == t) return;
    f.tips.push_back (t);
}
void act (Finding& f, const std::string& t) { if (f.action.empty()) f.action = t; }

// confidence: how far past the threshold, scaled down for short analyses
int conf (const Metrics& m, double margin, double scale, int cap = 95)
{
    const double df = clamp01 (m.durationSec / 90.0);
    const double c  = (50.0 + 45.0 * clamp01 (margin / scale)) * (0.75 + 0.25 * df);
    return (int) std::lround (std::min ((double) cap, c));
}

const TimelineEvent* strongestEvent (const Metrics& m, EventType t)
{
    const TimelineEvent* best = nullptr;
    for (auto& e : m.events)
        if (e.type == t && (best == nullptr || std::abs (e.value) > std::abs (best->value))) best = &e;
    return best;
}
int countEvents (const Metrics& m, EventType t)
{
    int n = 0;
    for (auto& e : m.events) if (e.type == t) ++n;
    return n;
}

// ============================================================================
//  Headroom & loudness
// ============================================================================
Finding checkHeadroom (const Metrics& m)
{
    Finding f;
    f.id = "headroom";
    f.title = "Headroom ועוצמה";
    f.group = HealthGroup::Clipping;
    f.proof = { "True Peak " + fmt (m.truePeakDb) + " dBTP  |  Sample peak " + fmt (m.samplePeakDb) + " dBFS",
                "Integrated " + fmt (m.integratedLufs) + " LUFS  |  Max short-term " + fmt (m.maxShortTermLufs) + " LUFS" };

    const double tp = m.truePeakDb;
    if (tp > 0.0)
    {
        f.status = Status::Problem;
        f.confidence = 95;
        f.why = "ה-True Peak עובר 0 dBTP. בקובץ 24 ביט ובהמרה לפורמטים של סטרימינג החלקים האלה ייחתכו.";
        f.action = "הורידו Gain ב-Utility האחרון על ה-Master עד שה-True Peak יהיה מתחת ל-0 dBTP. אין צורך לכוון לערך יעד קבוע.";
    }
    else if (tp > -0.5)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, tp + 0.5, 0.5, 90);
        f.why = "יש headroom, אבל מוגבל מאוד (" + he (tp) + " dBTP). אין קליפינג.";
        f.action = "אם המאסטרינג ידרוש מרווח לעיבוד, הורידו Gain לפני הייצוא. זו לא בעיה במיקס עצמו.";
    }
    else if (tp > -1.0)
    {
        f.status = Status::Info;
        f.confidence = 80;
        f.why = "יש headroom, אבל מוגבל (" + he (tp) + " dBTP). לא זוהה קליפינג. אם המאסטרינג ידרוש יותר מרווח לעיבוד, אפשר להוריד Gain לפני הייצוא.";
    }
    else
    {
        f.status = Status::Ok;
        f.confidence = 95;
        f.why = "יש headroom (" + he (tp) + " dBTP) ואין קליפינג.";
    }

    if (m.integratedLufs > -9.0)
    {
        f.status = worst (f.status, Status::Problem);
        f.confidence = std::max (f.confidence, 85);
        add (f.why, "העוצמה הממוצעת (" + he (m.integratedLufs) + " LUFS) היא עוצמה של מאסטר מוגמר. כמעט בטוח שיש לימיטר או קליפר על ה-Master.");
        f.action = "עקפו (Bypass) את הלימיטר או הקליפר על ה-Master לפני הייצוא. אם הם חלק מהסאונד, שלחו שתי גרסאות ותאמו עם המהנדס.";
    }
    else if (m.integratedLufs > -11.0)
    {
        f.status = worst (f.status, Status::Warning);
        f.confidence = std::max (f.confidence, 65);
        add (f.why, "העוצמה הממוצעת (" + he (m.integratedLufs) + " LUFS) גבוהה למיקס. ייתכן שיש דחיסה או לימיטר על ה-Master.");
        act (f, "בדקו מה יושב על ערוץ ה-Master. אם יש לימיטר להאזנה בלבד, עקפו אותו בייצוא.");
    }
    return f;
}

// ============================================================================
//  Clipping
// ============================================================================
Finding checkClipping (const Metrics& m, const TimeFormatter& tf)
{
    Finding f;
    f.id = "clipping";
    f.title = "קליפינג ובעיות טכניות";
    f.group = HealthGroup::Clipping;
    f.eventType = (int) EventType::Clipping;
    f.proof = { "Clip events " + std::to_string (m.clipEvents) + "  |  Samples over 0dBFS " + std::to_string (m.oversFloat),
                "Inter-sample peaks " + std::to_string (m.interSampleOvers) + "  |  Flat-tops " + std::to_string (m.flatTopRuns)
                    + "  |  DC " + fmt (m.dcOffsetDb, 0) + " dB" };
    f.confidence = 95;

    const double minutes = std::max (0.25, m.durationSec / 60.0);
    if (m.clipEvents > 0 || m.oversFloat > 0)
    {
        f.status = Status::Problem;
        f.why = "זוהו " + std::to_string (m.clipEvents) + " אירועי קליפינג. בתוך אבלטון (32-bit float) זה לא נשמע, אבל בקובץ 24 ביט הדגימות האלה ייחתכו לעיוות שאי אפשר לתקן במאסטר.";
        if (auto* e = strongestEvent (m, EventType::Clipping))
            add (f.why, "הצפיפות הגבוהה ביותר: " + tf (e->startSec) + ".");
        f.action = "מצאו את המקור (ערוץ או Group שמגיע לאדום בקטע הזה) והורידו שם Gain. לא להסתיר את זה עם לימיטר על ה-Master.";
        tip (f, "אם הקליפינג מכוון (קליפר כצבע סאונד), שימו אותו על הערוץ או ה-Group ולא על ה-Master.");
    }
    else if (m.interSampleOvers > 0)
    {
        f.status = Status::Warning;
        f.why = "אין דגימות מעל 0dBFS, אבל יש " + std::to_string (m.interSampleOvers) + " פיקים בין דגימות (Inter-sample). בהמרה ל-MP3/AAC הם עלולים לעוות.";
        f.action = "הורידו Gain קטן (בערך 0.5–1 dB) לפני הייצוא.";
    }

    if ((double) m.flatTopRuns / minutes > 15.0)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "זוהו " + std::to_string (m.flatTopRuns) + " קטעי 'ראש שטוח': גל שנחתך במקום כלשהו בשרשרת ורק אחר כך הונמך.");
        tip (f, "בדקו סמפלים של קיק וסנר (חלק מגיעים חתוכים בכוונה), ופלאגינים עם Drive או Clip.");
    }
    if (m.dcOffsetDb > -50.0)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "יש DC offset (" + he (m.dcOffsetDb, 0) + " dB), שמבזבז headroom.");
        tip (f, "High-pass עדין (10–20Hz) על הערוץ החשוד.");
    }
    if (f.status == Status::Ok) f.why = "לא זוהו קליפינג, פיקים בין דגימות, גלים חתוכים או DC offset.";
    return f;
}

// ============================================================================
//  Kick / Bass overlap (time + frequency + energy)
// ============================================================================
Finding checkKickBass (const Metrics& m, const TimeFormatter& tf, bool& overlapHigh)
{
    Finding f;
    f.id = "kickbass";
    f.title = "חפיפת קיק ובאס";
    f.group = HealthGroup::LowEnd;
    overlapHigh = false;

    if (! m.kickDetected || m.kickHitsMeasured < 8)
    {
        f.status = Status::Info;
        f.confidence = 60;
        f.proof = { "Kick hits detected " + std::to_string (m.kickCount) };
        f.why = "לא זוהה קיק עם מכות ברורות לאורך הטראק. אם יש קיק, ייתכן שהוא רך מאוד או קבור בתוך הבאס.";
        return f;
    }

    const double ov = m.kickBassOverlapPct;
    const double semis = m.bassFreqHz > 0 ? 12.0 * std::log2 (m.bassFreqHz / m.kickFreqHz) : 0.0;

    f.eventType = (int) EventType::KickBass;
    f.soloMode  = SoloMode::Band;
    f.soloLo    = std::max (25.0, std::min (m.kickFreqHz, m.bassFreqHz > 0 ? m.bassFreqHz : m.kickFreqHz) / 1.5);
    f.soloHi    = std::min (250.0, std::max (m.kickFreqHz, m.bassFreqHz) * 1.6);

    f.proof = { "Kick " + hz (m.kickFreqHz) + " (" + noteOf (m.kickFreqHz) + ")  |  Bass " + hz (m.bassFreqHz) + " (" + noteOf (m.bassFreqHz) + ")",
                "Overlap " + fmt (ov, 0) + "%  |  Kick length " + fmt (m.kickLengthMs, 0) + " ms  |  Bass before hit " + fmt (m.kickFloorRelDb) + " dB",
                "Kick over bass " + fmtS (m.kickPunchDb) + " dB  |  Hits " + std::to_string (m.kickHitsMeasured) };
    if (m.rootPitchClass >= 0 && m.rootStrength > 2.0)
    {
        int d = pitchClassOf (m.kickFreqHz) - m.rootPitchClass;
        d = ((d % 12) + 12) % 12;
        if (d > 6) d -= 12;
        f.proof.push_back ("Track root (est.) " + pitchName (m.rootPitchClass) + "  |  Kick to root " + std::to_string (d) + " st");
    }

    // confidence: more hits and consistent hits = more certain
    const double df = clamp01 (m.durationSec / 90.0);
    f.confidence = (int) std::lround (std::min (95.0, (50.0 + 30.0 * clamp01 (m.kickHitsMeasured / 64.0)
                                                            + 15.0 * clamp01 ((8.0 - m.kickOverlapSpreadDb) / 8.0))
                                                           * (0.8 + 0.2 * df)));

    f.why = "החפיפה (" + fmt (ov, 0) + "%) מחושבת משלושה רכיבים: מרחק התדרים (" + fmt (std::abs (semis)) + " חצאי טונים), אורך הקיק ("
          + fmt (m.kickLengthMs, 0) + "ms), ועוצמת הבאס רגע לפני כל מכה ביחס לפיק הקיק (" + he (m.kickFloorRelDb) + " dB). תדרים קרובים לבד לא מעידים על התנגשות. מה שקובע הוא כמה זמן ובאיזו עוצמה הם נשמעים יחד.";

    if (ov > 50.0)      { f.status = Status::Problem; overlapHigh = true; }
    else if (ov > 25.0) { f.status = Status::Warning; }
    if (m.kickPunchDb < 2.0) f.status = worst (f.status, Status::Warning);

    if ((int) f.status >= (int) Status::Warning)
    {
        if (auto* e = strongestEvent (m, EventType::KickBass))
            add (f.why, "החפיפה הגבוהה ביותר: " + tf (e->startSec) + " עד " + tf (e->endSec) + ".");
        f.action = "האזינו לקטע הזה ב'סולו תחום'. אם הקיק מאבד מכה כשהבאס מנגן: Sidechain מהקיק לבאס (Attack 0.1–1ms, Release בערך "
                 + fmt (std::max (60.0, m.kickLengthMs), 0) + "ms), או קיצור זנב הקיק.";
        tip (f, "בדקו אם התדר היסודי של הקיק תומך או מתנגש בהרמוניה של הבאס. תו בס במרחק חצי טון מהקיק נשמע כבוץ, לא כפאנץ'.");
        tip (f, "החליטו מי מחזיק את הסאב: קיק ארוך וסאבי והבאס מעליו, או קיק קצר וטייט והבאס מחזיק את התחתית.");
        tip (f, "EQ משלים: חיתוך צר ועדין בבאס סביב " + hz (m.kickFreqHz) + ", רק אם הבדיקה בסולו מאשרת שיש מיסוך.");
    }
    if (m.kickPunchDb < 2.0)
        add (f.why, "הקיק מעלה את הלואו-אנד רק ב-" + fmt (m.kickPunchDb) + " dB מעל הבאס, כלומר הוא כמעט קבור.");
    if (f.status == Status::Ok)
        add (f.why, "החפיפה בזמן ובתדר נמוכה. הקיק והבאס מתחלקים במקום היטב.");
    return f;
}

// ============================================================================
//  Low end
// ============================================================================
Finding checkLowEnd (const Metrics& m, const Profile& p)
{
    Finding f;
    f.id = "lowend";
    f.title = "איזון הלואו-אנד";
    f.group = HealthGroup::LowEnd;
    const double lowDev = 0.5 * (m.subDevDb + m.bassDevDb);
    const double over = lowDev - p.lowAllowDb;
    f.proof = { "Sub 25–60Hz " + fmtS (m.subDevDb) + " dB  |  Bass 60–125Hz " + fmtS (m.bassDevDb) + " dB",
                "Low-mids 160–400Hz " + fmtS (m.lowMidDevDb) + " dB  (vs. the mix's own tonal trend)" };
    f.soloMode = SoloMode::Band;
    f.soloLo = 25.0; f.soloHi = 125.0;

    if (over > 4.0)
    {
        f.status = Status::Problem;
        f.confidence = conf (m, over - 4.0, 4.0, 85);
        f.why = "הסאב והבאס חזקים בכ-" + fmt (over) + " dB מעבר למקובל בסגנון שנבחר. המאסטר 'ינשום' לפי הבאס, והעוצמה האפשרית תקטן.";
        f.action = "השוו לרפרנס מאותו סגנון (לשונית רפרנס). אם הפער מאושר, הורידו 1–3 dB לקבוצת הבאס/סאב.";
    }
    else if (over > 0.0)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, over, 4.0, 80);
        f.why = "הלואו-אנד מעט כבד ביחס לשאר הספקטרום (כ-" + fmt (over) + " dB מעל הטווח המקובל לסגנון).";
        f.action = "בדיקת A/B מול רפרנס באותה עוצמה נשמעת לפני שמשנים משהו.";
    }
    else if (over < -8.0)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, -8.0 - over, 4.0, 80);
        f.why = "הלואו-אנד חלש ביחס לשאר הספקטרום. המיקס עלול להישמע דק במערכות גדולות.";
        f.action = "בדקו שהבאס לא נחתך ב-High-pass ושאין בעיית פאזה בתחתית (ראו סטריאו ופאזה).";
    }

    if (m.lowMidDevDb > 3.0)
    {
        f.status = worst (f.status, Status::Problem);
        f.confidence = std::max (f.confidence, conf (m, m.lowMidDevDb - 3.0, 3.0, 85));
        add (f.why, "יש הצטברות ב-160–400Hz (" + fmtS (m.lowMidDevDb) + " dB מעל הקו הטבעי של המיקס). זה האזור שגורם למיקס להישמע עמום.");
        f.soloLo = 160.0; f.soloHi = 400.0;
        f.action = "סולו תחום 160–400Hz כדי לשמוע מי ממלא אותו. אחר כך חיתוך רחב ועדין (1–3 dB) בכלים שמצטברים שם, לא על ה-Master.";
    }
    else if (m.lowMidDevDb > 1.5)
    {
        f.status = worst (f.status, Status::Warning);
        f.confidence = std::max (f.confidence, conf (m, m.lowMidDevDb - 1.5, 3.0, 75));
        add (f.why, "מעט עומס ב-160–400Hz.");
        if (over <= 0.0) { f.soloLo = 160.0; f.soloHi = 400.0; }
    }

    if ((int) f.status >= (int) Status::Warning)
    {
        tip (f, "High-pass על כל מה שלא צריך סאב: פאדים, לידים, FX ו-Returns של רוורב.");
        tip (f, "Returns של רוורב: High-pass גבוה יותר (200–400Hz) מנקה הרבה בוץ בלי לגעת בכלים.");
    }
    if (f.status == Status::Ok)
    {
        f.confidence = 85;
        f.why = "הלואו-אנד מאוזן ביחס לשאר הספקטרום, בלי הצטברות בולטת ב-200–400Hz.";
    }
    return f;
}

// ============================================================================
//  Phase
// ============================================================================
Finding checkPhase (const Metrics& m, const TimeFormatter& tf)
{
    Finding f;
    f.id = "phase";
    f.title = "פאזה וקורלציה";
    f.group = HealthGroup::Stereo;
    f.eventType = (int) EventType::Phase;
    f.soloMode = SoloMode::Mono;
    f.proof = { "Correlation " + fmt (m.correlation, 2) + "  |  Low <150Hz " + fmt (m.lowCorrelation, 2),
                "Negative correlation " + fmt (m.negCorrTimePct) + "% of time  |  5th percentile " + fmt (m.corrP5, 2) };

    if (m.correlation < 0.0)
    {
        f.status = Status::Problem;
        f.confidence = 95;
        f.why = "הקורלציה הכללית שלילית. סביר שאחד הערוצים הפוך בפולריות, או שיש אפקט סטריאו קיצוני.";
        f.action = "בדקו Utility עם Phase invert על ערוץ אחד, או Widener קיצוני. השמיעו במונו ושמעו מה נעלם.";
    }
    else if (m.correlation < 0.2)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, 0.2 - m.correlation, 0.2);
        f.why = "הקורלציה נמוכה (" + fmt (m.correlation, 2) + "). המיקס רחב מאוד ועלול להתפרק במונו.";
    }
    if (m.lowCorrelation < 0.1)
    {
        f.status = worst (f.status, Status::Problem);
        f.confidence = std::max (f.confidence, 90);
        add (f.why, "מתחת ל-150Hz הערוצים לא בפאזה. במועדון ובמונו זה יחליש את הלואו-אנד.");
        act (f, "Utility עם Bass Mono מתחת ל-120Hz, ובדיקת Chorus/Unison על הבאס.");
    }
    else if (m.lowCorrelation < 0.6)
    {
        f.status = worst (f.status, Status::Warning);
        f.confidence = std::max (f.confidence, conf (m, 0.6 - m.lowCorrelation, 0.5));
        add (f.why, "בתחתית יש יותר מדי סטריאו או אי-התאמה בפאזה (" + fmt (m.lowCorrelation, 2) + ").");
        act (f, "Bass Mono מתחת ל-120Hz, ובדיקה של שכבות קיק/באס: זום על תחילת הגל של שתי השכבות.");
    }
    if (m.negCorrTimePct > 10.0)
    {
        f.status = worst (f.status, Status::Warning);
        f.confidence = std::max (f.confidence, conf (m, m.negCorrTimePct - 10.0, 20.0));
        add (f.why, "ב-" + fmt (m.negCorrTimePct) + "% מהזמן הקורלציה שלילית.");
    }
    if (auto* e = strongestEvent (m, EventType::Phase))
        if ((int) f.status >= (int) Status::Warning)
            add (f.why, "הקטע הבולט: " + tf (e->startSec) + " (קורלציה " + fmt (e->value, 2) + "). כפתור 'מונו' משמיע אותו כפי שיישמע בטלפון.");

    if (f.status == Status::Ok)
    {
        f.confidence = 95;
        if (m.correlation > 0.95 && m.midSideToMidDb < -25.0)
        {
            f.status = Status::Info;
            f.why = "המיקס כמעט מונו. תקין לגמרי מבחינת פאזה. אם זו לא הכוונה, אפשר להוסיף רוחב לכלים משניים.";
        }
        else f.why = "הקורלציה חיובית ויציבה (" + fmt (m.correlation, 2) + "), והתחתית בפאזה.";
    }
    return f;
}

// ============================================================================
//  Mono compatibility
// ============================================================================
Finding checkMono (const Metrics& m)
{
    Finding f;
    f.id = "mono";
    f.title = "תאימות למונו";
    f.group = HealthGroup::Stereo;
    f.eventType = (int) EventType::Phase;
    f.soloMode = SoloMode::Mono;
    f.proof = { "Mono loss " + fmt (m.monoLossDb) + " dB",
                "Low " + fmt (m.lowMonoLossDb) + "  |  Mid " + fmt (m.midMonoLossDb) + "  |  High " + fmt (m.highMonoLossDb) + " dB" };

    if (m.monoLossDb < -3.0)       f.status = Status::Problem;
    else if (m.monoLossDb < -1.5)  f.status = Status::Warning;
    f.confidence = conf (m, std::max (0.0, -1.5 - m.monoLossDb), 2.0);

    f.why = "בסיכום למונו (טלפונים, בלוטות', חלק מהמועדונים) המיקס מאבד " + fmt (-m.monoLossDb) + " dB.";
    if (m.lowMonoLossDb < -1.0)
    {
        f.status = worst (f.status, m.lowMonoLossDb < -3.0 ? Status::Problem : Status::Warning);
        f.confidence = std::max (f.confidence, conf (m, -1.0 - m.lowMonoLossDb, 2.0));
        add (f.why, "התחתית מאבדת " + fmt (-m.lowMonoLossDb) + " dB.");
        act (f, "Bass Mono מתחת ל-120Hz (Utility).");
    }
    if (m.midMonoLossDb < -2.5)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "האמצע מאבד " + fmt (-m.midMonoLossDb) + " dB. שם יושבים הליד והווקאל.");
        act (f, "השמיעו במונו (כפתור 'מונו') והקשיבו איזה כלי נעלם או נהיה חלול. Widener, Haas או Chorus רחב על כלי מרכזי הם החשודים.");
    }
    if (f.status == Status::Ok) f.why = "המיקס שומר על עצמו במונו (איבוד של " + fmt (-m.monoLossDb) + " dB בלבד).";
    return f;
}

// ============================================================================
//  Stereo balance
// ============================================================================
Finding checkBalance (const Metrics& m, const TimeFormatter& tf)
{
    Finding f;
    f.id = "balance";
    f.title = "איזון שמאל/ימין";
    f.group = HealthGroup::Stereo;
    f.eventType = (int) EventType::Balance;
    f.proof = { "L/R " + fmtS (m.balanceDb) + " dB  |  Low " + fmtS (m.lowBalanceDb) + "  |  Mid " + fmtS (m.midBalanceDb) + "  |  High " + fmtS (m.highBalanceDb),
                "Unbalanced >3dB: " + fmt (m.imbalanceTimePct) + "% of time  |  Low side/mid " + fmt (m.lowSideToMidDb) + " dB" };

    auto side = [] (double db) { return db > 0 ? std::string ("שמאל") : std::string ("ימין"); };
    const double ab = std::abs (m.balanceDb);
    if (ab > 3.0)      f.status = Status::Problem;
    else if (ab > 1.5) f.status = Status::Warning;
    f.confidence = conf (m, std::max (0.0, ab - 1.5), 2.0);
    if (ab > 1.5) f.why = "המיקס נוטה ל" + side (m.balanceDb) + " (" + fmt (ab) + " dB).";

    const std::pair<const char*, double> bands[] = { { "בנמוכים", m.lowBalanceDb }, { "באמצע", m.midBalanceDb }, { "בגבוהים", m.highBalanceDb } };
    for (auto& b : bands)
        if (std::abs (b.second) > 2.5)
        {
            f.status = worst (f.status, Status::Warning);
            add (f.why, std::string (b.first) + " יש נטייה ל" + side (b.second) + " (" + fmt (std::abs (b.second)) + " dB).");
        }
    if (m.imbalanceTimePct > 25.0)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "ב-" + fmt (m.imbalanceTimePct) + "% מהזמן יש הפרש של יותר מ-3 dB בין הצדדים.");
    }
    if (m.lowSideToMidDb > -12.0)
    {
        f.status = worst (f.status, m.lowSideToMidDb > -6.0 ? Status::Problem : Status::Warning);
        f.confidence = std::max (f.confidence, 85);
        add (f.why, "יש סטריאו מתחת ל-120Hz (Side ביחס ל-Mid: " + he (m.lowSideToMidDb) + " dB).");
        act (f, "Bass Mono מתחת ל-120Hz, ובדיקה אילו ערוצים מכניסים סטריאו לתחתית (פאדים, Returns, Unison).");
    }
    if (auto* e = strongestEvent (m, EventType::Balance))
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "הקטע הבולט: " + tf (e->startSec) + " עד " + tf (e->endSec) + ", נטייה ל" + side (e->value) + ".");
        act (f, "השמיעו את הקטע וחפשו כלי בולט שיושב בצד אחד בלי משקל נגד בצד השני.");
    }
    if (f.status == Status::Ok) { f.confidence = 90; f.why = "שמאל וימין מאוזנים, ואין סטריאו מיותר בתחתית."; }
    return f;
}

// ============================================================================
//  Harshness
// ============================================================================
Finding checkHarshness (const Metrics& m, const TimeFormatter& tf)
{
    Finding f;
    f.id = "harsh";
    f.title = "צרימה (Harshness)";
    f.group = HealthGroup::Harshness;
    f.eventType = (int) EventType::Harshness;
    f.soloMode = SoloMode::Band;
    f.soloLo = 2000.0; f.soloHi = 5000.0;

    const int bursts = countEvents (m, EventType::Harshness);
    const auto* top = strongestEvent (m, EventType::Harshness);
    f.proof = { "2–5kHz " + fmtS (m.presenceDevDb) + " dB  |  6–10kHz " + fmtS (m.sibilanceDevDb) + " dB  (vs. trend)" };
    if (top != nullptr)
    {
        f.proof.push_back ("Bursts " + std::to_string (bursts) + "  |  Strongest " + tf (top->startSec) + "–" + tf (top->endSec)
                           + "  |  " + hz (top->freqLo) + "–" + hz (top->freqHi) + "  |  +" + fmt (top->value) + " dB");
        f.soloLo = top->freqLo; f.soloHi = top->freqHi;
    }

    if (m.presenceDevDb > 3.0)
    {
        f.status = Status::Problem;
        f.confidence = conf (m, m.presenceDevDb - 3.0, 3.0, 85);
        f.why = "אזור 2–5kHz חזק בכ-" + fmt (m.presenceDevDb) + " dB מעל הקו הטבעי של המיקס לאורך כל הטראק. זה האזור שהאוזן הכי רגישה אליו, והמאסטר יחמיר אותו.";
        f.action = "מצאו את הכלי שבולט בתחום הזה (סינתים, לידים, מצלתיים) ושימו Dynamic EQ רק עליו.";
    }
    else if (m.presenceDevDb > 1.5)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, m.presenceDevDb - 1.5, 3.0, 75);
        f.why = "אזור 2–5kHz מעט בולט (" + fmtS (m.presenceDevDb) + " dB).";
    }
    else if (m.presenceDevDb < -4.0)
    {
        f.status = Status::Info;
        f.confidence = 65;
        f.why = "אזור 2–5kHz חלש יחסית. המיקס עלול להישמע רחוק או עמום.";
        tip (f, "לפני שמגבירים היי, נסו להוריד בוץ ב-200–500Hz.");
    }

    if (top != nullptr && (m.harshSpikePct > 3.0 || top->severity == 2))
    {
        f.status = worst (f.status, top->severity == 2 ? Status::Warning : Status::Info);
        f.confidence = std::max (f.confidence, conf (m, top->value - 5.0, 5.0, 90));
        add (f.why, "זוהו " + std::to_string (bursts) + " קטעים שבהם 2–5kHz קופץ פתאום. הבולט: " + tf (top->startSec) + " עד " + tf (top->endSec)
                  + ", סביב " + hz (top->peakHz) + " (+" + fmt (top->value) + " dB).");
        act (f, "השמיעו את הקטע ב'סולו תחום' (" + hz (top->freqLo) + "–" + hz (top->freqHi) + ") כדי לשמוע איזה כלי צורם, ושימו עליו Dynamic EQ בתחום הזה.");
    }
    if (m.sibilanceDevDb > 3.0)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "גם 6–10kHz בולט (" + fmtS (m.sibilanceDevDb) + " dB): הי-האטים, מצלתיים או סיבילנטים.");
        tip (f, "De-esser או Dynamic EQ עדין על ה-Group של ההי-האטים והמצלתיים.");
    }
    if ((int) f.status >= (int) Status::Warning)
        tip (f, "A/B: השוו את הקטע לרפרנס באותה עוצמה נשמעת (לשונית רפרנס). אם הרפרנס חד באותה מידה, זה כנראה אופי הסגנון.");
    if (f.status == Status::Ok) { f.confidence = 85; f.why = "אזורי 2–5kHz ו-6–10kHz מאוזנים, בלי קפיצות חדות."; }
    return f;
}

// ============================================================================
//  Resonances
// ============================================================================
Finding checkResonances (const Metrics& m)
{
    Finding f;
    f.id = "resonance";
    f.title = "רזוננסים צרים";
    f.group = HealthGroup::Harshness;

    std::vector<Resonance> rel;
    for (auto& r : m.resonances) if (r.excessDb >= 6.0) rel.push_back (r);
    if (rel.empty())
    {
        f.proof = { "No narrow peaks above 6 dB" };
        f.confidence = 80;
        f.why = "לא נמצאו תדרים צרים שבולטים יותר מ-6 dB מעל הסביבה שלהם.";
        return f;
    }

    std::string line;
    for (size_t i = 0; i < rel.size() && i < 4; ++i)
    {
        if (! line.empty()) line += "  |  ";
        line += hz (rel[i].freqHz) + " (" + noteOf (rel[i].freqHz) + ") +" + fmt (rel[i].excessDb) + "dB";
    }
    f.proof = { line };
    f.status = rel.front().excessDb >= 10.0 ? Status::Problem : Status::Warning;
    f.confidence = conf (m, rel.front().excessDb - 6.0, 6.0, 80);
    f.soloMode = SoloMode::Band;
    f.soloLo = rel.front().freqHz * std::pow (2.0, -0.25);
    f.soloHi = rel.front().freqHz * std::pow (2.0, 0.25);

    f.why = "יש תדרים צרים שבולטים לאורך כל הטראק. הם נשמעים כצלצול, 'בום' או 'אף', והמאסטר מגביר אותם.";
    const int pc = pitchClassOf (rel.front().freqHz);
    if (m.rootPitchClass >= 0 && pc == m.rootPitchClass && m.rootStrength > 2.0)
        add (f.why, "הבולט ביותר (" + noteOf (rel.front().freqHz) + ") תואם לשורש המשוער של הטראק, אז ייתכן שזה פשוט התו המרכזי ולא בעיה.");
    f.action = "סולו תחום סביב " + hz (rel.front().freqHz) + ". אם הצלצול נשמע, מצאו את הכלי ב-EQ Eight במצב Spectrum וחתכו צר (2–5 dB) רק בו.";
    tip (f, "Dynamic EQ או מעבד רזוננסים חותכים רק כשהרזוננס מתפרץ.");
    return f;
}

// ============================================================================
//  Masking (estimate)
// ============================================================================
Finding checkMasking (const Metrics& m, bool kickBassHigh)
{
    Finding f;
    f.id = "masking";
    f.title = "מיסוך (Masking)";
    f.group = HealthGroup::Masking;

    const double mud      = clamp01 ((m.lowMidDevDb + 1.0) / 5.0);
    const double dense    = clamp01 ((m.midFlatness - 0.12) / 0.30);
    const double centered = clamp01 ((-m.midSideToMidDb - 8.0) / 14.0);
    double index = 100.0 * (0.4 * mud + 0.3 * dense + 0.3 * centered);
    if (kickBassHigh) index = std::min (100.0, index + 15.0);

    f.proof = { "Masking index " + fmt (index, 0) + "/100",
                "Low-mid build-up " + fmt (mud * 100.0, 0) + "%  |  Mid density " + fmt (dense * 100.0, 0) + "%  |  Centre crowding " + fmt (centered * 100.0, 0) + "%"
                    + (kickBassHigh ? "  |  Kick/Bass overlap" : "") };
    f.confidence = std::min (70, conf (m, std::abs (index - 40.0), 30.0, 70));

    if (index > 60.0)      f.status = Status::Problem;
    else if (index > 40.0) f.status = Status::Warning;

    f.why = "0 = הפרדה טובה, 100 = הרבה כלים שמסתירים זה את זה. ";
    struct Driver { double v; const char* text; double lo, hi; };
    Driver drivers[] = {
        { mud,      "הצטברות ב-160–400Hz: כלים נבלעים זה בזה באזור החום.",                    160.0, 400.0 },
        { dense,    "האמצע צפוף ואחיד: הרבה שכבות מתחרות על אותו מרחב.",                    500.0, 3000.0 },
        { centered, "רוב האנרגיה ב-250Hz–4kHz יושבת במרכז, וכלים נלחמים על אותה נקודה.", 1000.0, 3000.0 },
    };
    std::sort (std::begin (drivers), std::end (drivers), [] (const Driver& a, const Driver& b) { return a.v > b.v; });
    if ((int) f.status >= (int) Status::Warning)
    {
        f.why += "מה מעלה את המדד:";
        for (auto& d : drivers) if (d.v > 0.5) add (f.why, d.text);
        if (kickBassHigh) add (f.why, "חפיפת קיק ובאס גבוהה.");
        f.soloMode = SoloMode::Band;
        f.soloLo = drivers[0].lo; f.soloHi = drivers[0].hi;
        f.action = "סולו תחום על האזור שמוביל את המדד (" + hz (drivers[0].lo) + "–" + hz (drivers[0].hi) + ") כדי לשמוע אילו כלים חופפים שם.";
        tip (f, "Sidechain EQ: על הכלים המלווים, הורדה של 2–3 dB בתחום של הכלי המוביל רק כשהוא מנגן.");
        tip (f, "פאנינג לכלים משניים. במרכז: קיק, באס, ליד ראשי.");
    }
    else f.why += "המדד נמוך: יש הפרדה סבירה בתדרים ובסטריאו.";
    tip (f, "זו הערכה מתוך מיקס סטריאו. כדי לדעת בדיוק מי מסתיר את מי צריך את הערוצים בנפרד.");
    return f;
}

// ============================================================================
//  Spectral density & tonal balance
// ============================================================================
Finding checkDensity (const Metrics& m)
{
    Finding f;
    f.id = "density";
    f.title = "צפיפות ואיזון טונאלי";
    f.group = HealthGroup::Masking;
    f.proof = { "Tilt " + fmtS (m.tiltDbPerOct) + " dB/oct  |  Centroid " + hz (m.centroidHz),
                "Flatness " + fmt (m.midFlatness, 2) + "  |  Spectral holes " + std::to_string (m.holeFreqs.size()) };
    f.confidence = 70;

    if (m.holeFreqs.size() >= 2)
    {
        f.status = Status::Warning;
        std::string list;
        for (size_t i = 0; i < m.holeFreqs.size() && i < 5; ++i) { if (! list.empty()) list += ", "; list += hz (m.holeFreqs[i]); }
        add (f.why, "יש 'חורים' בספקטרום: " + list + ".");
        act (f, "בדקו אם כלי נחתך באגרסיביות ב-EQ, או שחסר כלי שממלא את התחום.");
    }
    if (m.midFlatness > 0.45)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "האמצע צפוף מאוד ודומה לרעש, ולכן קשה לשמוע הפרדה.");
        act (f, "צמצמו שכבות או הקצו לכל שכבה תחום תדרים.");
    }
    if (m.tiltDbPerOct > -0.5)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "האיזון הטונאלי בהיר מאוד (" + fmtS (m.tiltDbPerOct) + " dB לאוקטבה).");
        act (f, "השוו לרפרנס. לרוב חסר גוף ב-100–300Hz או שיש עודף היי-מיד.");
    }
    else if (m.tiltDbPerOct < -6.0)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "האיזון הטונאלי כהה מאוד (" + fmtS (m.tiltDbPerOct) + " dB לאוקטבה).");
        act (f, "לפני שמגבירים היי, נקו בוץ ב-200–500Hz.");
    }
    if (f.status == Status::Ok) f.why = "הספקטרום מלא ורציף, עם שיפוע טונאלי טבעי.";
    return f;
}

// ============================================================================
//  Dynamics (PLR / LRA) and crest
// ============================================================================
Finding checkDynamics (const Metrics& m, const Profile& p)
{
    Finding f;
    f.id = "dynamics";
    f.title = "טווח דינמי";
    f.group = HealthGroup::Dynamics;
    f.proof = { "PLR " + fmt (m.plr) + " dB  |  LRA " + fmt (m.lra) + " LU" };
    f.confidence = 85;
    f.why = "PLR: הפרש בין ה-True Peak לעוצמה הממוצעת. LRA: כמה העוצמה משתנה בין חלקי הטראק.";

    if (m.plr < p.plrProblem)
    {
        f.status = Status::Problem;
        f.confidence = conf (m, p.plrProblem - m.plr, 3.0);
        add (f.why, "PLR של " + fmt (m.plr) + " dB נמוך מאוד למיקס, ולמאסטר אין מקום לעבוד.");
        f.action = "בטלו לימיטר או קומפרסיה כבדה על ה-Master לפני הייצוא.";
    }
    else if (m.plr < p.plrWarn)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, p.plrWarn - m.plr, 3.0, 80);
        add (f.why, "PLR של " + fmt (m.plr) + " dB: המיקס כבר מעט מוחץ.");
        f.action = "בדקו כמה Gain Reduction יש על קומפרסורי ה-Groups וה-Master.";
    }
    if (m.lra < p.lraLow)
    {
        f.status = worst (f.status, Status::Warning);
        add (f.why, "LRA של " + fmt (m.lra) + " LU: כל חלקי הטראק כמעט באותה עוצמה. ראו 'מבנה הטראק' כדי לבדוק אם הברייק והדרופ באמת שונים.");
        act (f, "אוטומציה או דילול בברייקים. המאסטר לא יכול ליצור ניגוד שלא קיים.");
    }
    else if (m.lra > p.lraHigh)
    {
        f.status = worst (f.status, Status::Info);
        add (f.why, "LRA של " + fmt (m.lra) + " LU: הבדלי עוצמה גדולים בין החלקים.");
    }
    if (f.status == Status::Ok) add (f.why, "הערכים בריאים ומשאירים מקום לעבודה במאסטר.");
    return f;
}

Finding checkCrest (const Metrics& m, const Profile& p)
{
    Finding f;
    f.id = "crest";
    f.title = "קרסט פקטור";
    f.group = HealthGroup::Dynamics;
    f.proof = { "Crest " + fmt (m.crestDb) + " dB  |  Peak " + fmt (m.samplePeakDb) + " dBFS  |  RMS " + fmt (m.rmsDb) + " dBFS" };
    f.why = "ההפרש בין הפיק לרמה הממוצעת. גבוה: טרנזיינטים חיים. נמוך: מיקס דחוס.";
    f.confidence = 85;

    if (m.crestDb < p.crestProblem)
    {
        f.status = Status::Problem;
        f.confidence = conf (m, p.crestProblem - m.crestDb, 3.0);
        f.action = "הורידו בחצי את הקומפרסיה על ה-Groups וה-Master ובדקו שהתופים חוזרים לנשום.";
        tip (f, "Attack מהיר מדי על התופים חונק טרנזיינטים. נסו 10–30ms.");
    }
    else if (m.crestDb < p.crestWarn)
    {
        f.status = Status::Warning;
        f.confidence = conf (m, p.crestWarn - m.crestDb, 3.0, 80);
        f.action = "A/B עם הקומפרסורים על ה-Groups עקופים, באותה עוצמה נשמעת.";
    }
    else if (m.crestDb > 20.0)
    {
        f.status = Status::Info;
        add (f.why, "המיקס דינמי מאוד. ייתכן שפיקים בודדים (סנר, פרקשן) יגבילו את העוצמה במאסטר.");
    }
    return f;
}

HealthGroup groupOf (const std::string& id)
{
    if (id == "lowend" || id == "kickbass")                          return HealthGroup::LowEnd;
    if (id == "dynamics" || id == "crest")                            return HealthGroup::Dynamics;
    if (id == "phase" || id == "mono" || id == "balance")             return HealthGroup::Stereo;
    if (id == "harsh" || id == "resonance")                           return HealthGroup::Harshness;
    if (id == "masking" || id == "density")                           return HealthGroup::Masking;
    return HealthGroup::Clipping;
}

} // namespace

// ============================================================================
int getNumProfiles() { return kNumProfiles; }
std::string getProfileName (int i) { return kProfiles[std::max (0, std::min (kNumProfiles - 1, i))].name; }

const char* statusName (Status s)
{
    switch (s)
    {
        case Status::Ok:      return "תקין";
        case Status::Info:    return "לידיעה";
        case Status::Warning: return "לבדיקה";
        case Status::Problem: return "לתיקון";
    }
    return "";
}
const char* severityName (Status s)
{
    switch (s)
    {
        case Status::Ok:      return "-";
        case Status::Info:    return "נמוכה";
        case Status::Warning: return "בינונית";
        case Status::Problem: return "גבוהה";
    }
    return "";
}
const char* groupName (HealthGroup g)
{
    switch (g)
    {
        case HealthGroup::LowEnd:    return "לואו-אנד";
        case HealthGroup::Dynamics:  return "דינמיקה";
        case HealthGroup::Stereo:    return "סטריאו ופאזה";
        case HealthGroup::Harshness: return "צרימה ורזוננסים";
        case HealthGroup::Masking:   return "מיסוך וצפיפות";
        case HealthGroup::Clipping:  return "קליפינג ו-Headroom";
    }
    return "";
}
const char* groupNameEn (HealthGroup g)
{
    switch (g)
    {
        case HealthGroup::LowEnd:    return "LOW END";
        case HealthGroup::Dynamics:  return "DYNAMICS";
        case HealthGroup::Stereo:    return "STEREO";
        case HealthGroup::Harshness: return "HARSHNESS";
        case HealthGroup::Masking:   return "MASKING";
        case HealthGroup::Clipping:  return "CLIPPING";
    }
    return "";
}
const char* sectionName (SectionType t)
{
    switch (t)
    {
        case SectionType::Intro: return "INTRO";
        case SectionType::Build: return "BUILD";
        case SectionType::Break: return "BREAK";
        case SectionType::Drop:  return "DROP";
        case SectionType::Outro: return "OUTRO";
        case SectionType::Main:  return "MAIN";
    }
    return "";
}
const char* eventName (EventType t)
{
    switch (t)
    {
        case EventType::Harshness: return "צרימה";
        case EventType::KickBass:  return "חפיפת קיק/באס";
        case EventType::Phase:     return "פאזה";
        case EventType::Balance:   return "איזון L/R";
        case EventType::Clipping:  return "קליפינג";
    }
    return "";
}
std::string pitchName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return (pc >= 0 && pc < 12) ? names[pc] : "-";
}
std::string defaultTime (double s)
{
    if (s < 0) s = 0;
    const int t = (int) std::floor (s);
    char b[32];
    std::snprintf (b, sizeof (b), "%d:%02d", t / 60, t % 60);
    return b;
}

std::string describeEvent (const TimelineEvent& e, const TimeFormatter& tfIn)
{
    TimeFormatter tf = tfIn ? tfIn : TimeFormatter (defaultTime);
    std::string s = std::string (eventName (e.type)) + "  ·  " + tf (e.startSec) + " – " + tf (e.endSec);
    switch (e.type)
    {
        case EventType::Harshness: s += "  ·  " + hz (e.freqLo) + "–" + hz (e.freqHi) + "  ·  +" + fmt (e.value) + " dB"; break;
        case EventType::KickBass:  s += "  ·  Overlap " + fmt (e.value, 0) + "%"; break;
        case EventType::Phase:     s += "  ·  Corr " + fmt (e.value, 2); break;
        case EventType::Balance:   s += "  ·  L/R " + fmtS (e.value) + " dB"; break;
        case EventType::Clipping:  s += "  ·  " + fmt (e.value, 0) + (e.severity == 2 ? " clips" : " ISP"); break;
    }
    return s;
}

// ============================================================================
Report buildReport (const Metrics& m, int profileIndex, const std::set<std::string>& ignoredIds, TimeFormatter tfIn)
{
    Report r;
    TimeFormatter tf = tfIn ? tfIn : TimeFormatter (defaultTime);
    if (! m.valid)
    {
        r.readinessTitle = "אין מספיק אודיו לניתוח";
        r.readinessLine  = "נגנו את הטראק (עדיף מההתחלה ועד הסוף) ונסו שוב.";
        return r;
    }
    r.valid = true;
    const Profile& p = kProfiles[std::max (0, std::min (kNumProfiles - 1, profileIndex))];

    bool overlapHigh = false;
    Finding kb = checkKickBass (m, tf, overlapHigh);

    r.findings = { checkClipping (m, tf), checkHeadroom (m), kb, checkLowEnd (m, p), checkPhase (m, tf), checkMono (m),
                   checkBalance (m, tf), checkHarshness (m, tf), checkResonances (m), checkMasking (m, overlapHigh),
                   checkDensity (m), checkDynamics (m, p), checkCrest (m, p) };

    for (auto& f : r.findings)
    {
        f.group = groupOf (f.id);
        f.ignored = ignoredIds.count (f.id) > 0;
    }
    std::stable_sort (r.findings.begin(), r.findings.end(), [] (const Finding& a, const Finding& b)
    {
        if (a.ignored != b.ignored) return ! a.ignored;
        return (int) a.status > (int) b.status;
    });

    // ---- health
    for (int g = 0; g < kNumHealthGroups; ++g)
    {
        r.health[g].name   = groupName ((HealthGroup) g);
        r.health[g].nameEn = groupNameEn ((HealthGroup) g);
        r.health[g].status = Status::Ok;
    }
    int problems = 0, warnings = 0;
    for (auto& f : r.findings)
    {
        if (f.ignored) continue;
        auto& h = r.health[(int) f.group];
        h.status = worst (h.status, f.status == Status::Info ? Status::Ok : f.status);
        if (f.status == Status::Problem) ++problems;
        if (f.status == Status::Warning) ++warnings;
    }

    // ---- readiness
    const int areas = problems + warnings;
    char en[96];
    if (problems > 0)
    {
        r.readiness = Readiness::Fix;
        r.readinessTitle = "לתקן לפני מאסטרינג";
        r.readinessLine  = std::to_string (problems) + (problems == 1 ? " בעיה" : " בעיות") + " לתיקון"
                         + (warnings > 0 ? " ו-" + std::to_string (warnings) + " נקודות לבדיקה" : "");
        std::snprintf (en, sizeof (en), "NEEDS WORK - %d to fix, %d to check", problems, warnings);
    }
    else if (warnings > 0)
    {
        r.readiness = Readiness::Good;
        r.readinessTitle = "טוב · מוכן טכנית";
        r.readinessLine  = std::to_string (warnings) + (warnings == 1 ? " נקודה ששווה לבדוק" : " נקודות ששווה לבדוק");
        std::snprintf (en, sizeof (en), "GOOD - %d area%s worth checking", areas, areas == 1 ? "" : "s");
    }
    else
    {
        r.readiness = Readiness::Ready;
        r.readinessTitle = "מוכן טכנית למאסטרינג";
        r.readinessLine  = "לא נמצאו בעיות טכניות.";
        std::snprintf (en, sizeof (en), "TECHNICALLY READY FOR MASTERING");
    }
    r.readinessEn = en;
    r.note = "בדיקה טכנית בלבד: קליפינג, לואו-אנד, פאזה, דינמיקה, ספקטרום. היא לא יודעת אם הדרופ מרגש או אם הליד יושב נכון.";

    for (auto& f : r.findings)
        if (! f.ignored && f.status == Status::Problem && r.priorities.size() < 3)
            r.priorities.push_back (f.title + ": " + (f.action.empty() ? f.why : f.action));
    for (auto& f : r.findings)
        if (! f.ignored && f.status == Status::Warning && r.priorities.size() < 3)
            r.priorities.push_back (f.title + ": " + (f.action.empty() ? f.why : f.action));

    // ---- mastering handoff
    auto ok = [] (bool b) { return b ? Status::Ok : Status::Problem; };
    r.technical = {
        { "אין קליפינג",                        ok (m.clipEvents == 0 && m.oversFloat == 0) },
        { "True Peak מתחת ל-0 dBTP",            ok (m.truePeakDb <= 0.0) },
        { "אין DC offset",                       ok (m.dcOffsetDb <= -50.0) },
        { "לואו-אנד מונו ובפאזה",              ok (m.lowCorrelation >= 0.6 && m.lowSideToMidDb <= -12.0) },
        { "פאזה יציבה",                          ok (m.correlation >= 0.2 && m.negCorrTimePct <= 10.0) },
        { "סטריאו מאוזן",                        ok (std::abs (m.balanceDb) <= 1.5) },
        { "יש דינמיקה למאסטר (PLR " + fmt (m.plr) + " dB)", ok (m.plr >= p.plrProblem) },
        { "אין לימיטר כבד על ה-Master",        ok (m.integratedLufs <= -9.0) },
    };
    for (auto& f : r.findings)
        if (! f.ignored && (int) f.status >= (int) Status::Warning)
            r.attention.push_back ({ f.title + ": חומרה " + severityName (f.status), f.status });
    r.exportItems = {
        "WAV או AIFF, ב-24 ביט או 32-bit float",
        "ה-Sample Rate של הפרויקט (בלי המרה)",
        "בלי Normalize ובלי Dither (בייצוא 32 float)",
        "מתחילת הטראק ועד סוף הזנב של הרוורב והדיליי",
        "בלי Fade out על הזנב. את הפייד עושים במאסטר",
        "אם יש עיבוד על ה-Master שהוא חלק מהסאונד: לשלוח גם גרסה בלעדיו",
        "רפרנס + הערות למהנדס (BPM, סולם, מה חשוב לכם)",
    };
    return r;
}

// ============================================================================
std::vector<CompareRow> buildComparison (const Metrics& a, const Metrics& b)
{
    std::vector<CompareRow> rows;
    const char* regions[7] = { "Sub 20–60Hz", "Bass 60–150Hz", "Low-mid 150–500Hz", "Mid 500Hz–2kHz",
                               "Presence 2–5kHz", "High 5–10kHz", "Air 10–20kHz" };
    for (int i = 0; i < 7; ++i)
    {
        CompareRow r;
        r.label = regions[i];
        r.mine = a.regionRelDb[i];
        r.ref  = b.regionRelDb[i];
        r.unit = "dB";
        const double d = r.mine - r.ref;
        r.highlight = std::abs (d) > 2.0;
        r.note = r.highlight ? (d > 0 ? "יותר מהרפרנס ב-" + fmt (d) + " dB" : "פחות מהרפרנס ב-" + fmt (-d) + " dB") : "דומה";
        rows.push_back (r);
    }
    auto row = [&] (const std::string& label, double x, double y, const std::string& unit, double thr, const std::string& note)
    {
        CompareRow r { label, x, y, unit, std::abs (x - y) > thr, note };
        rows.push_back (r);
    };
    row ("Stereo correlation", a.correlation, b.correlation, "", 0.15, "");
    row ("Width (Side/Mid 250Hz–4kHz)", a.midSideToMidDb, b.midSideToMidDb, "dB", 3.0, "");
    row ("PLR", a.plr, b.plr, "dB", 3.0, "לפני מאסטר צפוי להיות גבוה מהרפרנס");
    row ("LRA", a.lra, b.lra, "LU", 2.0, "");
    row ("Crest", a.crestDb, b.crestDb, "dB", 3.0, "");
    row ("Integrated", a.integratedLufs, b.integratedLufs, "LUFS", 99.0, "לפני מאסטר צפוי להיות שקט מהרפרנס. ההשמעה מותאמת עוצמה.");
    return rows;
}

std::string reportToText (const Report& r, const Metrics& m, int profileIndex, const Metrics* ref, TimeFormatter tfIn)
{
    TimeFormatter tf = tfIn ? tfIn : TimeFormatter (defaultTime);
    std::string s;
    s += "DaliMix | Dali Audio | דוח מיקס לפני מאסטרינג\n";
    {
        std::time_t t = std::time (nullptr);
        char buf[64];
        std::strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M", std::localtime (&t));
        s += std::string ("תאריך: ") + buf + "  |  סגנון: " + getProfileName (profileIndex) + "  |  משך: " + defaultTime (m.durationSec) + "\n\n";
    }
    s += "MASTERING READINESS: " + r.readinessEn + "\n" + r.readinessTitle + " | " + r.readinessLine + "\n" + r.note + "\n\n";

    s += "MIX HEALTH\n";
    for (auto& h : r.health) s += "  " + h.nameEn + " (" + h.name + "): " + statusName (h.status) + "\n";
    s += "\n";

    if (! m.sections.empty())
    {
        s += "מבנה הטראק (זיהוי אוטומטי)\n";
        for (auto& sc : m.sections)
            s += "  " + std::string (sectionName (sc.type)) + "  " + tf (sc.startSec) + "–" + tf (sc.endSec) + "  |  " + fmt (sc.lufs) + " LUFS  |  Low "
               + fmt (sc.lowPct, 0) + "%  |  Width " + fmt (sc.widthPct, 0) + "%  |  Density " + fmt (sc.densityPct, 0) + "%\n";
        s += "\n";
    }

    for (auto& f : r.findings)
    {
        s += "================================================\n";
        s += "[" + std::string (statusName (f.status)) + "] " + f.title;
        if ((int) f.status >= (int) Status::Warning) s += "  |  חומרה: " + std::string (severityName (f.status)) + "  |  ביטחון: " + std::to_string (f.confidence) + "%";
        if (f.ignored) s += "  (סומן כ'התעלם')";
        s += "\nPROOF\n";
        for (auto& p : f.proof) s += "  " + p + "\n";
        s += "WHY\n  " + f.why + "\n";
        if (! f.action.empty()) s += "ACTION\n  " + f.action + "\n";
        for (auto& t : f.tips) s += "  - " + t + "\n";
        s += "\n";
    }

    if (! m.events.empty())
    {
        s += "================================================\nציר זמן: אירועים\n";
        for (auto& e : m.events) s += "  " + describeEvent (e, tf) + "\n";
        s += "\n";
    }

    if (ref != nullptr && ref->valid)
    {
        s += "================================================\nהשוואה לרפרנס (המיקס שלך / רפרנס)\n";
        for (auto& c : buildComparison (m, *ref))
            s += "  " + c.label + ": " + fmt (c.mine) + " / " + fmt (c.ref) + " " + c.unit + (c.note.empty() ? "" : "  (" + c.note + ")") + "\n";
        s += "\n";
    }

    s += "================================================\nMASTERING HANDOFF\nTechnical\n";
    for (auto& t : r.technical) s += std::string ("  ") + (t.status == Status::Ok ? "[V] " : "[X] ") + t.text + "\n";
    if (! r.attention.empty())
    {
        s += "Needs attention\n";
        for (auto& t : r.attention) s += "  - " + t.text + "\n";
    }
    s += "Export\n";
    for (auto& e : r.exportItems) s += "  - " + e + "\n";
    s += "\nהספים הם נקודת פתיחה מקצועית, לא חוק. השוו תמיד לרפרנס.\n";
    return s;
}

} // namespace pa
