#pragma once
// ============================================================================
//  DaliMix - ReportBuilder
//  Metrics -> Hebrew report built as  Problem -> Proof -> Why -> Action.
//  No overall "quality score": a technical readiness status + severity and
//  confidence per finding. All thresholds live in the .cpp (kProfiles + checks).
// ============================================================================
#include "AnalysisEngine.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace pa
{

enum class Status      { Ok = 0, Info = 1, Warning = 2, Problem = 3 };
enum class Readiness   { Ready = 0, Good, Fix };
enum class HealthGroup { LowEnd = 0, Dynamics, Stereo, Harshness, Masking, Clipping };
constexpr int kNumHealthGroups = 6;
enum class SoloMode    { None = 0, Band, Mono };

using TimeFormatter = std::function<std::string (double)>;   // seconds -> "2:14 · Bar 65"

struct Finding
{
    std::string id;
    std::string title;                  // Hebrew
    Status      status = Status::Ok;
    int         confidence = 0;         // 0..100
    HealthGroup group = HealthGroup::LowEnd;

    std::vector<std::string> proof;     // numbers (LTR lines)
    std::string why;                    // why it matters (Hebrew)
    std::string action;                 // the first thing to do (Hebrew)
    std::vector<std::string> tips;      // more practical steps (Hebrew)

    int      eventType = -1;            // EventType to audition, -1 = loudest section
    SoloMode soloMode  = SoloMode::None;
    double   soloLo = 0.0, soloHi = 0.0;
    bool     ignored = false;
};

struct HealthItem  { std::string name, nameEn; Status status = Status::Ok; };
struct HandoffItem { std::string text; Status status = Status::Info; };

struct Report
{
    bool valid = false;
    Readiness readiness = Readiness::Good;
    std::string readinessTitle;         // Hebrew, big
    std::string readinessLine;          // Hebrew, one line
    std::string readinessEn;            // "GOOD - 2 areas worth checking"
    std::string note;                   // what this analysis can / cannot judge

    std::vector<Finding> findings;      // Problem, Warning, Info, Ok; ignored last
    HealthItem health[kNumHealthGroups];
    std::vector<std::string> priorities;

    std::vector<HandoffItem> technical;
    std::vector<HandoffItem> attention;
    std::vector<std::string> exportItems;
};

struct CompareRow
{
    std::string label;                  // Hebrew + range
    double mine = 0.0, ref = 0.0;
    std::string unit;
    bool highlight = false;
    std::string note;                   // Hebrew
};

int         getNumProfiles();
std::string getProfileName (int index);

Report buildReport (const Metrics& m, int profileIndex,
                    const std::set<std::string>& ignoredIds = {}, TimeFormatter fmt = {});
std::vector<CompareRow> buildComparison (const Metrics& mix, const Metrics& ref);
std::string reportToText (const Report& r, const Metrics& m, int profileIndex,
                          const Metrics* reference = nullptr, TimeFormatter fmt = {});

const char* statusName   (Status s);    // תקין / לידיעה / לבדיקה / לתיקון
const char* severityName (Status s);    // נמוכה / בינונית / גבוהה
const char* groupName    (HealthGroup g);
const char* groupNameEn  (HealthGroup g);
const char* sectionName  (SectionType t);   // INTRO / BUILD / ...
const char* eventName    (EventType t);     // Hebrew
std::string pitchName    (int pitchClass);
std::string defaultTime  (double seconds);
std::string describeEvent (const TimelineEvent& e, const TimeFormatter& fmt);

} // namespace pa
