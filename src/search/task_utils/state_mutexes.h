#ifndef TASK_UTILS_STATE_MUTEXES_H
#define TASK_UTILS_STATE_MUTEXES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace task_representation {
class FTSTask;
}

/*
  Mutexes between the states of different factors of an FTS. A fact is a pair (factor,
  state). compatible(i, j) == false means that facts i and j never hold together in any
  state a plan of the task -- including its plan reconstruction -- can pass through, so a
  SAT encoding may exclude the combination; compatible(i, i) == false means fact i is
  unreachable. The relation over-approximates compatibility: wrongly claiming compatibility
  is harmless, wrongly claiming a mutex is not.

  The sources do not know the encodings, and the SAS source does not know the
  transformations: every plan-reconstruction step maps the state information itself
  (PlanReconstruction::map_state_sets).
*/
namespace state_mutexes {
class StateMutexes {
    std::vector<int> fact_offset;                 // factor -> first fact
    std::vector<std::vector<uint64_t>> compat;    // fact x fact bit matrix, symmetric
    std::string source;
    double seconds = 0;
public:
    // all pairs incompatible (sources then add compatible pairs) or all compatible
    StateMutexes(const task_representation::FTSTask &fts, bool all_compatible);

    int num_factors() const {return int(fact_offset.size()) - 1;}
    int num_facts() const {return fact_offset.back();}
    int fact(int factor, int state) const {return fact_offset[factor] + state;}
    int factor_size(int factor) const {return fact_offset[factor + 1] - fact_offset[factor];}
    bool compatible(int i, int j) const {return (compat[i][j >> 6] >> (j & 63)) & 1;}
    const std::vector<uint64_t> &compatible_row(int i) const {return compat[i];}

    void set_compatible(int i, int j);
    void set_incompatible(int i, int j);

    void set_source(const std::string &name, double time) {source = name; seconds = time;}
    const std::string &get_source() const {return source;}
    double get_seconds() const {return seconds;}
};

enum class Source {
    SAS,           // mutexes of the finite-domain task (translator, or h2), mapped onto the FTS
    FILE,          // read from a file: lines "f s g t" (mutex) or "f s" (unreachable)
    FTS_FIXPOINT   // h^2-style pairwise reachability fixpoint on the FTS itself
};

struct Options {
    Source source = Source::SAS;
    std::string file;
    int max_facts = 20000;         // skip larger FTSs (the matrix is quadratic)
    double time_limit = 30;        // FTS_FIXPOINT: give up (and use nothing) after this
};

/*
  Computes the mutexes of the FTS from the given source. Returns null if they cannot be
  computed; a line "STATEMUTEXES skipped: <reason>" says why, so a configuration asking for
  mutexes never silently runs without them unnoticed.
*/
std::unique_ptr<StateMutexes> compute(const task_representation::FTSTask &fts, const Options &opts);
}

#endif
