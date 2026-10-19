#ifndef TASK_TRANSFORMATION_PLAN_RECONSTRUCTION_H
#define TASK_TRANSFORMATION_PLAN_RECONSTRUCTION_H

#include "../plan.h"
#include <memory>
#include <string>
#include <vector>

namespace task_representation {
    class LabelID;
    class State;
}

namespace task_transformation {
/*
  What the states of a task stand for in the original (SAS) task, handed down through the
  transformations step by step. For every factor: the SAS variables it covers, and per state
  the value tuples over them the concrete task can be in while a plan of this task is in that
  state -- including every state it passes through during plan reconstruction (e.g. along tau
  paths). Used to map mutexes of the SAS task onto the transformed task: two states of
  different factors are mutex only if every pair of their tuples contains a mutex pair of
  facts, so a set must never miss a tuple (over-approximating is sound, under-approximating
  is not).
*/
struct StateSets {
    using Tuple = std::vector<int>;
    std::vector<std::vector<int>> vars;                   // factor -> SAS variables
    std::vector<std::vector<std::vector<Tuple>>> tuples;  // factor -> state -> tuples
};

// sort and remove duplicates
void normalize(std::vector<StateSets::Tuple> &tuples);
// the tuples of all states reachable from start along successors (start included)
std::vector<StateSets::Tuple> closure_tuples(const std::vector<std::vector<StateSets::Tuple>> &state_tuples,
                                             const std::vector<std::vector<int>> &successors, int start);

class PlanReconstruction {
public:
    virtual ~PlanReconstruction() = default;
    // Given a sequence of actions that is a plan for the successor task, retrieve a plan
    // for the predecessor task. Directly modifies the contents of plan and traversed_states
    virtual void reconstruct_plan(Plan & plan) const = 0;
    /*
      Maps the state sets of the task before this transformation step onto the task after it
      (see StateSets). Every step must implement this; there is deliberately no default, so
      that each transformation decides how mutex information passes through it. A step that
      cannot do it returns false and explains why; the caller then uses no mutexes at all.
    */
    virtual bool map_state_sets(StateSets &sets, std::string &why) const = 0;
    virtual void print(std::ostream& ) const { /* do your stuff */ }

    friend std::ostream& operator<<(std::ostream& o, const PlanReconstruction& b);

};


class PlanReconstructionSequence : public PlanReconstruction {
    std::vector<std::shared_ptr<PlanReconstruction>> plan_reconstructions;
public:
    explicit PlanReconstructionSequence(
        std::vector<std::shared_ptr<PlanReconstruction>> plan_reconstructions);
    virtual ~PlanReconstructionSequence() override = default;

    const std::vector<std::shared_ptr<PlanReconstruction>> &get_reconstructions() const {
        return plan_reconstructions;
    }

    virtual void reconstruct_plan(Plan & plan) const override {
        for (const auto & pr : plan_reconstructions) {
            pr->reconstruct_plan(plan);
        }
    }

    // the steps in transformation order (the stored order is the reverse)
    virtual bool map_state_sets(StateSets &sets, std::string &why) const override {
        for (auto it = plan_reconstructions.rbegin(); it != plan_reconstructions.rend(); ++it)
            if (!(*it)->map_state_sets(sets, why))
                return false;
        return true;
    }

    virtual void print(std::ostream& o) const override {
        for (const auto & pr : plan_reconstructions) {
            pr->print(o);
            o << " ---> ";
        }
    }
};
}
#endif
