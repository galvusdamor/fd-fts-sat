#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_TAU_BAKING_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_TAU_BAKING_H

#include "plan_reconstruction.h"
#include "task_transformation.h"

#include "../task_representation/transition_system.h"

#include <memory>
#include <utility>
#include <vector>

namespace task_transformation {
/*
  tau_baking(): removes tau labels. A label is a tau label of factor i if it is a self-loop
  in every state of every other factor. Such moves commute with every label that is a
  self-loop everywhere in i, so they can be postponed until the next label that is not,
  or until the end of the plan. They are therefore baked into the labels that follow:

  * every transition (s,l,s') of a label l that is not a self-loop everywhere in i gets a
    copy (s'',l,s') for every s'' from which s is reachable with tau labels of i;
  * every state from which a goal state is reachable with tau labels becomes a goal state;
  * tau labels and labels that are self-loops everywhere in every factor are removed;
  * states that are no longer reachable from the initial state, or from which no goal is
    reachable, are pruned, and labels left without transitions in some factor are removed
    (repeated until nothing changes).

  Removing labels can turn further labels into tau labels, so the whole step is repeated
  up to max_rounds times. The plan reconstruction inserts the tau paths again.
*/
class TauBakingTransformation : public TaskTransformation {
    int max_rounds;
public:
    explicit TauBakingTransformation(const options::Options &opts);
    virtual ~TauBakingTransformation() override = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;

    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping>
        transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
};

// One round of tau baking.
class PlanReconstructionTauBaking : public PlanReconstruction {
public:
    struct BakedFactor {
        // in the predecessor task (old ids):
        std::vector<std::vector<std::pair<int, int>>> tau_successors;   // state -> (label, target)
        std::vector<char> moves;     // label -> not a self-loop everywhere in this factor
        std::vector<std::vector<task_representation::Transition>> transitions;   // label -> its transitions
        std::vector<bool> goal;
    };
    std::vector<int> old_label;                     // new label -> old label
    std::vector<std::vector<int>> old_state;        // factor -> new state -> old state
    std::vector<std::unique_ptr<BakedFactor>> baked;   // factor -> null if nothing was baked
    std::vector<int> old_initial_state;

    virtual void reconstruct_plan(Plan &plan) const override;
    /*
      Factors keep their numbering; states are renumbered (pruning). Before the next label a
      baked factor walks a tau path forward from its state, so a new state stands for every
      old state tau-reachable from its old state.
    */
    virtual bool map_state_sets(StateSets &sets, std::string &why) const override;
    virtual void print(std::ostream &o) const override {
        o << "TauBaking";
    }
private:
    void tau_path(int factor, std::vector<int> &state, const std::vector<bool> &target,
                  std::vector<int> &labels, std::vector<PlanState> &states) const;
};
}
#endif
