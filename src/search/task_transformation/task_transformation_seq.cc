#include "task_transformation_seq.h"

#include "plan_reconstruction.h"

#include "../task_representation/fts_task.h"
#include "../options/plugin.h"
#include "../plugin.h"
#include "../utils/system.h"

#include <iostream>

using namespace std;

namespace task_transformation {
SequenceTransformation::SequenceTransformation(const options::Options &opts)
    : transformations(opts.get_list<shared_ptr<TaskTransformation>>("transformations")) {
}

static void flatten(const shared_ptr<PlanReconstruction> &pr,
                    vector<shared_ptr<PlanReconstruction>> &out) {
    if (!pr)
        return;
    if (auto seq = dynamic_pointer_cast<PlanReconstructionSequence>(pr)) {
        for (const auto &sub : seq->get_reconstructions())
            flatten(sub, out);
    } else {
        out.push_back(pr);
    }
}

pair<shared_ptr<task_representation::FTSTask>, shared_ptr<PlanReconstruction>>
SequenceTransformation::transform_task(const shared_ptr<task_representation::FTSTask> &fts_task) {
    shared_ptr<task_representation::FTSTask> task = fts_task;
    // reconstructions of every step, each in its own order (last transformation first)
    vector<vector<shared_ptr<PlanReconstruction>>> steps;
    for (const auto &transformation : transformations) {
        auto result = transformation->transform_task(task);
        task = result.first;
        steps.emplace_back();
        flatten(result.second, steps.back());
        cout << "Transformation step " << steps.size() << " of " << transformations.size()
             << ": " << *task << endl;
        if (task->trivially_solved())
            break;
    }
    // flattened sequences list the last transformation first; the constructor of
    // PlanReconstructionSequence takes them in the order they were applied
    vector<shared_ptr<PlanReconstruction>> reconstructions;
    for (const auto &step : steps)
        reconstructions.insert(reconstructions.end(), step.rbegin(), step.rend());
    return make_pair(task, make_shared<PlanReconstructionSequence>(move(reconstructions)));
}

pair<shared_ptr<task_representation::FTSTask>, Mapping>
SequenceTransformation::transform_task_lossy(const shared_ptr<task_representation::FTSTask> &) {
    cerr << "seq() does not support lossy transformations" << endl;
    utils::exit_with(utils::ExitCode::UNSUPPORTED);
}

static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Sequence of transformations",
                             "Applies the transformations in the given order.");
    parser.add_list_option<shared_ptr<TaskTransformation>>(
        "transformations", "the transformations, applied first to last");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<SequenceTransformation>(opts);
}

static options::PluginShared<TaskTransformation> _plugin("seq", _parse);
}
