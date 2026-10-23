#ifndef TASK_TRANSFORMATION_MERGE_STRATEGY_LABEL_UNIFY_H
#define TASK_TRANSFORMATION_MERGE_STRATEGY_LABEL_UNIFY_H

#include "merge_strategy.h"
#include "merge_strategy_factory.h"

#include <memory>
#include <string>

namespace options {
class Options;
}

namespace task_transformation {
/*
  merge_label_unify(max_states, min_saved): merges the factor pair whose merge lets exact label
  reduction unify the most labels.

  Two labels that differ (in their transitions) in exactly two factors a and b differ in only
  one factor once a and b are merged, and exact label reduction (run after the merge, e.g.
  label_reduction=exact(before_shrinking=true)) then makes them one label whose transitions in
  the product are the union of theirs: the branching of the product grows, the number of labels
  shrinks.

  get_next() looks at the pairs of labels that share a moving factor (a non-self-loop
  transition) and differ in exactly two factors; for each such factor pair it computes how many
  labels the merge would save: the number of labels minus the number of distinct label
  signatures outside {a, b} (a signature is the label's group in every factor). It returns the
  pair with the most saved labels whose product has at most max_states states (ties: smaller
  product), and (-1, -1) once no pair saves at least min_saved labels, which ends the main loop.
  A factor moved by more than max_bucket labels is not used to find pairs.

  require_exclusive: only count label pairs that can never fire in the same time step anyway,
  so that unifying them costs no parallelism: they share a moving factor in which neither label
  has a transition from a state the other one's moving transitions end in (after either one,
  the other is not applicable there). E.g. picking one ball with either gripper.
*/
class MergeStrategyLabelUnify : public MergeStrategy {
    const int max_states;
    const int min_saved;
    const int max_bucket;
    const bool require_exclusive;
    int merges = 0;
    long long saved_total = 0;
public:
    MergeStrategyLabelUnify(const FactoredTransitionSystem &fts, int max_states, int min_saved,
                            int max_bucket, bool require_exclusive);
    virtual ~MergeStrategyLabelUnify() override = default;
    virtual std::pair<int, int> get_next(
        const std::vector<int> &allowed_indices = std::vector<int>()) override;
};

class MergeStrategyFactoryLabelUnify : public MergeStrategyFactory {
    const int max_states;
    const int min_saved;
    const int max_bucket;
    const bool require_exclusive;
protected:
    virtual std::string name() const override;
    virtual void dump_strategy_specific_options() const override;
public:
    explicit MergeStrategyFactoryLabelUnify(const options::Options &opts);
    virtual ~MergeStrategyFactoryLabelUnify() override = default;
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
