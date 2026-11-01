#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_LOAD_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_LOAD_H

#include "task_transformation.h"

#include <memory>
#include <string>
#include <utility>

namespace task_transformation {
/*
  load_fts(filename=F): ignores the task it receives and returns the FTS in F, written by
  dump_fts (see task_transformation_dump.h). For experiments on encodings: the expensive
  transformation is done once. The plan of the loaded task cannot be mapped back to the input
  task, so when a plan is found the planner stops (exit code 0) before reconstructing or saving it;
  the search's own output (encoding sizes, SAT calls, horizon) is complete by then.
*/
class LoadTransformation : public TaskTransformation {
    std::string filename;
public:
    explicit LoadTransformation(const options::Options &opts);
    virtual ~LoadTransformation() override = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping>
        transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
};
}

#endif
