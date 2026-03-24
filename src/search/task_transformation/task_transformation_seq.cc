#include "task_transformation_cost.h"

#include "plan_reconstruction.h"

#include "../task_representation/fts_task.h"
#include "../options/plugin.h"
#include "../plugin.h"

using namespace std;

namespace task_transformation {


    CostTransformation::CostTransformation(const Options & opts) :
        cost_type(static_cast<OperatorCost>(opts.get_enum("cost_type"))) {
    }
std::pair<std::shared_ptr<task_representation::FTSTask>,
          std::shared_ptr<PlanReconstruction>>
CostTransformation::transform_task(const std::shared_ptr<task_representation::FTSTask> &fts_task) {
    return make_pair(std::make_shared<task_representation::FTSTask>(*fts_task, cost_type),
                     std::make_shared<PlanReconstructionSequence>(
                         vector<shared_ptr<PlanReconstruction>>()));

}

    //We do not care about plan reconstruction 
std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping >
    CostTransformation::transform_task_lossy(
    const std::shared_ptr<task_representation::FTSTask> &fts_task)   {
    return make_pair(std::make_shared<task_representation::FTSTask>(*fts_task, cost_type),
                     Mapping());
}

    static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
        add_cost_type_option_to_parser(parser);
        options::Options opts = parser.parse();
        if (parser.dry_run()) {
            return nullptr;
        } else {
            return make_shared<CostTransformation>(opts);
        }
    }

static options::PluginShared<TaskTransformation> _plugin("cost", _parse);

}
