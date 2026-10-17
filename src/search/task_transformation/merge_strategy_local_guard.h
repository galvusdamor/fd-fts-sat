#ifndef TASK_TRANSFORMATION_MERGE_STRATEGY_LOCAL_GUARD_H
#define TASK_TRANSFORMATION_MERGE_STRATEGY_LOCAL_GUARD_H

#include "merge_strategy.h"
#include "merge_strategy_factory.h"

#include <memory>
#include <string>

namespace options {
class Options;
}

namespace task_representation {
class TransitionSystem;
}

namespace task_transformation {
/*
  merge_local_guard(max_states): merges a factor into its guard so that its labels become
  tau labels of the product.

  A factor i is locally driven if every label that moves i (has a transition in i that is
  not a self-loop) moves no other factor. Its guards are the other factors in which those
  labels are not a self-loop in every state, i.e. where they have preconditions. If i has
  exactly one guard g, then in the product g x i the labels of i move only the product and
  are self-loops everywhere else: they are tau labels there, which weak bisimulation and
  tau_baking can remove.

  get_next() returns such a pair (g, i) whose product has at most max_states states, and
  (-1, -1) when there is none, which ends the main loop. One scan finds all candidates and
  keeps a set of disjoint pairs, smallest products first; they are merged one after the
  other (merging g and i changes neither the labels nor the guard of another local factor
  with a different guard), and the next scan starts when they are used up. Local factors
  sharing a guard are therefore merged in successive scans: after the first merge the next
  one is guarded by the product, and the products multiply. max_dependents >= 1 only merges
  into a guard on which at most that many local factors depend (counting all of them, not
  only those within max_states); with 1 nothing stacks up.
*/
class MergeStrategyLocalGuard : public MergeStrategy {
    const int max_states;
    const int max_dependents;
    // Disjoint pairs found by the last scan, merged one after the other. Stored as the
    // transition systems themselves: shrinking may renumber the factors between merges,
    // so indices would go stale.
    std::vector<std::pair<const task_representation::TransitionSystem *,
                          const task_representation::TransitionSystem *>> queue;
    std::vector<std::pair<int, int>> scan(const std::vector<int> &allowed_indices) const;
public:
    MergeStrategyLocalGuard(const FactoredTransitionSystem &fts, int max_states, int max_dependents);
    virtual ~MergeStrategyLocalGuard() override = default;
    virtual std::pair<int, int> get_next(
        const std::vector<int> &allowed_indices = std::vector<int>()) override;
};

class MergeStrategyFactoryLocalGuard : public MergeStrategyFactory {
    const int max_states;
    const int max_dependents;
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    explicit MergeStrategyFactoryLocalGuard(const options::Options &opts);
    virtual ~MergeStrategyFactoryLocalGuard() override = default;
    virtual std::unique_ptr<MergeStrategy> compute_merge_strategy(
        const FTSTask &fts_task,
        const FactoredTransitionSystem &fts) override;
    virtual bool requires_init_distances() const override {
        return false;
    }
    virtual bool requires_goal_distances() const override {
        return false;
    }
};
}

#endif
