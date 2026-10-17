#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_DUMP_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_DUMP_H

#include "task_transformation.h"

#include <memory>
#include <string>
#include <utility>

namespace task_transformation {
/*
  dump_fts(filename=F, exit_after=false): writes the FTS it receives to F and passes it on
  unchanged, so it can be placed anywhere in seq(...). Format, one item per line:

    fts <factors> <labels>
    label <l> <cost>                                  for every label
    factor <f> <states> <init> <number of goals> <goal states...>
    t <f> <l> <src> <target>                           for every transition of l in f,
                                                       unless l only self-loops in every state of f
  A label without t-lines for a factor is a self-loop in every state of that factor.
  With exit_after=true the planner stops after writing (exit code 0, no plan).
*/
class DumpTransformation : public TaskTransformation {
    std::string filename;
    bool exit_after;
public:
    explicit DumpTransformation(const options::Options &opts);
    virtual ~DumpTransformation() override = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;

    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping>
        transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
};
}
#endif
