#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_LABEL_CLOSURE_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_LABEL_CLOSURE_H

#include "plan_reconstruction.h"
#include "task_transformation.h"

#include <memory>
#include <utility>
#include <vector>

namespace task_transformation {
/*
  label_closure(): adds transitions that stand for a label applied several times in a row.

  M(l) = factors in which l has a transition that is not a self-loop. An l-path in factor f
  is elastic if it visits a state with an l-self-loop: it can be stretched to any longer
  length by staying on that self-loop. A transition (s,t) is elastic if s or t has an
  l-self-loop. Sound closures (every new product transition is realised by l^K):

  1. |M(l)| = 1: full transitive closure in that factor; every other factor stays where it
     is, and l only self-loops there.
  2. (multi_factor) every transition of l in every f in M(l) is elastic: add the pairs
     connected by an elastic l-path in every f in M(l). K = the longest of the chosen
     paths; every factor stretches its path to K.
  3. (multi_factor) exactly one g in M(l) has a non-elastic transition: full closure in g,
     the other factors unchanged (their transitions are elastic and stretch to g's length).

  With two or more non-elastic moving factors the path lengths would have to agree exactly,
  which a product of independent factors cannot express; such labels are left alone.
  Self-loops created by cycles are not added. States and labels are unchanged.
*/
class LabelClosureTransformation : public TaskTransformation {
    int max_transitions;
    bool multi_factor;
public:
    explicit LabelClosureTransformation(const options::Options &opts);
    virtual ~LabelClosureTransformation() override = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;

    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping>
        transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
};

// Expands a step with a closed label into l^K with the factors' original paths.
class PlanReconstructionLabelClosure : public PlanReconstruction {
public:
    struct LabelFactor {
        int factor;
        std::vector<std::vector<int>> successors;   // original l-successors per state
        std::vector<char> selfloop;                 // state has an l-self-loop
    };
    struct ClosedLabel {
        std::vector<LabelFactor> moving;            // every factor in M(l)
    };
private:
    std::vector<std::unique_ptr<ClosedLabel>> closed;   // per label, null if not closed
public:
    explicit PlanReconstructionLabelClosure(int num_labels) : closed(num_labels) {}
    void add_closed_label(int label, std::unique_ptr<ClosedLabel> data) {
        closed[label] = std::move(data);
    }
    virtual void reconstruct_plan(Plan &plan) const override;
    virtual void print(std::ostream &o) const override {
        o << "LabelClosure";
    }
};
}
#endif
