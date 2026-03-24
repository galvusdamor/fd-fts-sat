#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_COST_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_COST_H

#include <utility>
#include <memory>
#include "task_transformation.h"

#include "../option_parser.h"
#include "../operator_cost.h"

namespace task_representation {
    class FTSTask;
    class SASTask;
}

namespace task_transformation {
class PlanReconstruction;
class StateMapping;

class CostTransformation : public TaskTransformation {

    OperatorCost cost_type;
    
public:
    CostTransformation(const Options & opts) ;
    virtual ~CostTransformation() = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task)  override;

    //We do not care about plan reconstruction 
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, 
        Mapping> transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task)  override;

};
}
#endif
