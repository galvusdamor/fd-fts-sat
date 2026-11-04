#ifndef MICRO_STEPS_ENCODING_H
#define MICRO_STEPS_ENCODING_H

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "common_encoding.h"

namespace micro_steps_encoding {

/**
 * Micro-step (MS) encoding with exists-step parallelism (R2E paper, section
 * "Micro-Steps").
 *
 * Every time step t is divided into one micro-step per label, following a fixed
 * label order l_1, ..., l_m. The state of every factor after every micro-step
 * is represented by a fresh copy of its state variables (a "layer"): layer 0
 * are the state variables of time step t and layer m those of time step t+1.
 * Micro-step k relates layer k-1 to layer k through the transitions of l_k if
 * l_k is selected, and through frame axioms otherwise. No transition, source,
 * target or F variables are needed.
 *
 * Hierarchy:
 *   SATEncoding -> StateEncoding -> LabelEncoding -> CommonEncoding
 *                                                 -> MicroStepsEncoding
 *
 * Options:
 *  - useSelfloopOptimisation: variable sharing. Layer k only gets new
 *    variables for the states M_k moved by l_k (source or target of a
 *    non-self-loop transition); all other states reuse the variable of layer
 *    k-1. Labels irrelevant for a factor produce no micro-step at all in it.
 *  - dualThreshold (tau): a row of a micro-step is encoded by its dual
 *    constraint (28-33) iff the dual clause has arity <= tau, and by its primal
 *    instances (24-27) otherwise. tau < 0 means infinity (fully dual).
 */

/// Precomputed, time-independent data of a single micro-step of one factor.
struct MicroStep {
    int label;
    int position;                              // position of label in the label order
    std::vector<std::pair<int, int>> transitions; // (src, target), sorted and unique
    std::vector<int> sources;                  // src(l_k), sorted
    std::vector<int> targets;                  // tgt(l_k), sorted
    std::vector<int> moved;                    // M_k, sorted
};

/// Time-independent data of the whole task, shared by all encoding instances.
struct MicroStepsTaskInfo {
    std::vector<int> labelOrder;               // position -> label
    std::vector<int> labelPosition;            // label -> position
    std::vector<std::vector<MicroStep>> steps; // [ts] -> micro-steps in label order
};

/// Counters of generated clauses per constraint type (for the statistics output).
struct MicroStepsStatistics {
    long long layerVars = 0;
    long long amoAuxVars = 0;
    long long c22_alo = 0, c22_amo = 0, c23 = 0, c24 = 0, c25 = 0, c26 = 0, c27 = 0;
    long long d28 = 0, d29 = 0, d30 = 0, d31 = 0, d32 = 0, d33 = 0;
    long long links = 0;
    void print() const;
};

class MicroStepsEncoding : public sat_search::CommonEncoding {
    const int dualThreshold;
    bool statisticsPrinted;
    std::shared_ptr<const MicroStepsTaskInfo> info;

    /// A layer of a micro-step in which new variables were introduced.
    /// Only the new variables are stored: if none of them is true, the factor
    /// is in the same state as before the micro-step.
    struct RecordedLayer {
        int position;
        std::vector<std::pair<int, int>> newVars; // (state, SAT variable)
    };
    // [fromTime][ts] -> layers in label order
    std::map<int, std::vector<std::vector<RecordedLayer>>> allTimesLayers;

    bool useDual(size_t arity) const;

    /// Constraint 22 for a layer whose variables for newStates were just created.
    void encodeLayerExactlyOne(const std::vector<int> &layer, const std::vector<int> &newStates,
                               const std::vector<char> &isNew, MicroStepsStatistics &stats);

    /// Encodes all micro-steps of factor ts at time step fromTime, starting at
    /// layer0. Returns the variables of the last layer.
    std::vector<int> encodeFactor(int ts, const std::vector<int> &layer0, int fromTime,
                                  MicroStepsStatistics &stats);

protected:
    // CommonEncoding hooks. They are unused since encode() is overridden:
    // with variable sharing the state variables of time step t+1 are not fresh
    // variables but (partially) those of earlier layers, so they cannot be
    // generated upfront as CommonEncoding::encode does.
    void generateAdditionalVariables(int fromTime) override;
    void encode_transition(const std::vector<std::vector<int>> &previousStateVars,
                           const std::vector<std::vector<int>> &nextStateVars, int fromTime) override;
    void encode_frame_axioms(const std::vector<std::vector<int>> &previousStateVars,
                             const std::vector<std::vector<int>> &nextStateVars, int fromTime) override;

    std::vector<std::vector<int>> extractIntermediateStates(std::vector<int> &selectedLabels,
                                                            std::vector<int> &currentLastState,
                                                            std::vector<int> &nextState,
                                                            int labelTimestep) override;

public:
    explicit MicroStepsEncoding(
        std::shared_ptr<sat_capsule> capsule,
        const std::shared_ptr<task_representation::FTSTask> &fts,
        bool forceAtLeastOneAction,
        bool useSelfloopOptimisation,
        int dualThreshold,
        bool statisticsPrinted,
        std::shared_ptr<const MicroStepsTaskInfo> info);
    ~MicroStepsEncoding() override = default;

    void encode(int fromTime, int toTime) override;
};

class MicroStepsEncodingFactory : public sat_search::SATEncodingFactory {
    const bool useSelfloopOptimisation;
    const int dualThreshold;
    bool statisticsPrinted;
    std::shared_ptr<const MicroStepsTaskInfo> info;

    void computeTaskInfo();

public:
    explicit MicroStepsEncodingFactory(const options::Options &opts);

    void initialize() override;
    std::unique_ptr<sat_search::SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
    ~MicroStepsEncodingFactory() override = default;
};

} // namespace micro_steps_encoding

#endif // MICRO_STEPS_ENCODING_H
