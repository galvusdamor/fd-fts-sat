#ifndef TASK_TRANSFORMATION_PLAN_RECONSTRUCTION_H
#define TASK_TRANSFORMATION_PLAN_RECONSTRUCTION_H

#include "../plan.h"
#include <vector>
#include <memory>

namespace task_representation {
    class LabelID;
    class State;
}

namespace task_transformation {
class PlanReconstruction {
public:
    virtual ~PlanReconstruction() = default;
    // Given a sequence of actions that is a plan for the successor task, retrieve a plan
    // for the predecessor task. Directly modifies the contents of plan and traversed_states
    virtual void reconstruct_plan(Plan & plan) const = 0;
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

    virtual void print(std::ostream& o) const override {
        for (const auto & pr : plan_reconstructions) {
            pr->print(o);
            o << " ---> ";
        }
    }
};
}
#endif
