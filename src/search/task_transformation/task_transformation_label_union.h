#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_LABEL_UNION_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_LABEL_UNION_H

#include "plan_reconstruction.h"
#include "task_transformation.h"

#include <memory>
#include <utility>
#include <vector>

namespace options {
class Options;
}

namespace task_transformation {
/*
  label_union(): replaces a group of labels that could all fire independently of each other
  by one label that fires any subset of them, raising the branching without merging factors.

  Labels l_1..l_k form a group if
    1. each l_i moves exactly one factor f_i (has a non-self-loop transition only there), and
       the f_i are pairwise different;
    2. every l_i has the same transitions -- i.e. the same self-loops, its preconditions -- in
       every factor other than f_1..f_k, and the same cost;
    3. l_i is a self-loop in every state of f_j for j != i (no precondition there).
  (2 and 3 together: the transitions of l_i outside f_i, with self-loops-everywhere left out,
  are the same map for all members.) The union label U has, in every factor, the union of the
  members' transitions: in f_i the moves of l_i plus staying in any state, elsewhere the
  shared self-loops. A firing of U moves any subset of the f_i, each by one transition of its
  l_i; that is the members fired one after the other (in any order: none needs anything in
  another's factor, and none changes a factor another needs). A firing that moves nothing is a
  no-op and is dropped by the plan reconstruction.

  No parallelism is lost: the members could fire in the same step before, and U still can do
  all of them. What changes is that they now share one position in the label order.
  require_no_preconditions: only group labels that are self-loops everywhere outside their
  factor (tau labels).
*/
class LabelUnionTransformation : public TaskTransformation {
    bool require_no_preconditions;
    int max_group;
public:
    explicit LabelUnionTransformation(const options::Options &opts);
    virtual ~LabelUnionTransformation() override = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping>
        transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
};

// Expands a step of a union label into the members whose factor moved.
class PlanReconstructionLabelUnion : public PlanReconstruction {
public:
    // per new label: the old labels it stands for, with the factor each one moves (-1 for a
    // label that is kept as it is)
    std::vector<std::vector<std::pair<int, int>>> members;
    virtual void reconstruct_plan(Plan &plan) const override;
    // states are unchanged
    virtual bool map_state_sets(StateSets &, std::string &) const override {
        return true;
    }
    // a union label stands for the operators of all its members
    virtual bool map_label_sets(LabelSets &sets, std::string &why) const override;
    virtual void print(std::ostream &o) const override {
        o << "LabelUnion";
    }
};
}

#endif
