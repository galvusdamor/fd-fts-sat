#ifndef TASK_TRANSFORMATION_TASK_TRANSFORMATION_SEQ_H
#define TASK_TRANSFORMATION_TASK_TRANSFORMATION_SEQ_H

#include "task_transformation.h"

#include <memory>
#include <utility>
#include <vector>

namespace task_transformation {
/*
  seq(transformations=[t1, ..., tn]): applies t1, then t2 to its result, and so on. The
  plan reconstruction undoes them in reverse order; nested sequences are flattened, so
  the result lists the last transformation first, like every other reconstruction
  sequence.
*/
class SequenceTransformation : public TaskTransformation {
    std::vector<std::shared_ptr<TaskTransformation>> transformations;
public:
    explicit SequenceTransformation(const options::Options &opts);
    virtual ~SequenceTransformation() override = default;
    virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
        std::shared_ptr<PlanReconstruction>> transform_task(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;

    virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping>
        transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;
};
}
#endif
