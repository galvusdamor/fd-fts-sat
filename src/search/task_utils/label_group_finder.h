#ifndef TASK_UTILS_LABEL_GROUP_FINDER_H
#define TASK_UTILS_LABEL_GROUP_FINDER_H

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace task_representation {
class FTSTask;
}

/*
  Label groups: an ordered list of label sets, one per micro-step of a time step (see
  sat/step_encoding.h). Singletons give the intermediate-state encoding (sigma), one set of all
  labels a single parallel step; everything in between interpolates.

  Exists-step groups. Inside a group the labels run with exists-step semantics in the sense of
  Rintanen, Heljanko, Niemela (2006), without a fixed order: every fired label is applicable at
  the group's start state, each factor moves at most once (by its "mover"), and every other fired
  label relevant to the factor self-loops at the start or at the end state of the factor. Such a
  set has a serialisation iff the precedence constraints it induces are acyclic: in factor F with
  mover m (s -> t), a fired label that self-loops at s but not at t runs before m, one that
  self-loops at t but not at s runs after m. A group is *exists-safe* if no state and no fired
  subset induces a cycle; then the encoding needs no order inside the group, and plan extraction
  finds a serialisation by topological sorting.

  ExistsGroupChecker decides exists-safety: statically (the graph of possible precedences is
  acyclic -- sufficient), and exactly with a SAT call that searches for factor states, a fired
  subset and movers realising a cycle (unsatisfiable = safe; the problem is NP-hard).
*/
namespace label_group_finder {

class LabelGroupFinder {
public:
    virtual ~LabelGroupFinder() = default;
    virtual std::vector<std::vector<int>> find_groups(const task_representation::FTSTask &fts) = 0;
};

class ExistsGroupChecker {
    struct Entry {             // a label in a factor where it is relevant
        int ts;
        std::vector<int> app, sl;                  // applicable states, self-loop states (sorted)
        std::vector<std::pair<int, int>> real;     // real transitions (s, t), sorted
    };
    const task_representation::FTSTask &fts;
    std::vector<std::vector<Entry>> entries;       // label -> its relevant factors, by factor
    int exactConflicts;
public:
    long exactCalls = 0, exactSafe = 0, exactUnsafe = 0, exactUnknown = 0;
    ExistsGroupChecker(const task_representation::FTSTask &fts, int exactConflicts);
    // possible precedences between a and b: (a before b, b before a)
    std::pair<bool, bool> static_edges(int a, int b) const;
    // no cycle of possible precedences: every fired subset in every state is serialisable
    bool statically_safe(const std::vector<int> &group) const;
    std::vector<std::tuple<int, int, int, int>> edge_witnesses(int a, int b) const;
    // the exact check (SAT); false also if the conflict limit is hit
    bool exactly_safe(const std::vector<int> &group, bool lastOnCycle = false);
    // some state is a start state of both in every shared factor
    bool co_applicable(int a, int b) const;
    // the last label lies on a cycle of possible precedences (if the group without it is
    // exists-safe, only such cycles can be realised)
    int cycle_through_last(const std::vector<int> &group, long budget) const;
    // keeps the semantics of the label order: a label b after a (in the order) in one group loses
    // no execution of the intermediate-state encoding -- whenever a's real move into t lets b
    // fire at t, b self-loops at t, has no real transition from t and is applicable at a's source
    bool keeps_order_executions(int a, int b) const;
};

}

#endif
