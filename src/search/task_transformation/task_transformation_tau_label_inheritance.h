#ifndef TASK_TRASNFORMATION_TASK_TRANSFORMATION_LABEL_INHERITANCE_H
#define TASK_TRASNFORMATION_TASK_TRANSFORMATION_LABEL_INHERITANCE_H


namespace task_transformation {
    class TaskTransformationTauLabelInheritance : public TaskTransformation {
        options::Options options;
    public:
        explicit TaskTransformationMergeAndShrink(const options::Options &options);
        virtual ~TaskTransformationMergeAndShrink() = default;
        virtual std::pair<std::shared_ptr<task_representation::FTSTask>,
            std::shared_ptr<PlanReconstruction>> transform_task(
                const std::shared_ptr<task_representation::FTSTask> &fts_task) override;

        virtual std::pair<std::shared_ptr<task_representation::FTSTask>, Mapping >
            transform_task_lossy(
            const std::shared_ptr<task_representation::FTSTask> &fts_task) override;

    };
}

#endif //TASK_TRASNFORMATION_TASK_TRANSFORMATION_LABEL_INHERITANCE_H