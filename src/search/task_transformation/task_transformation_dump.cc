#include "task_transformation_dump.h"

#include "label_transformation_utils.h"
#include "plan_reconstruction.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"
#include "../options/plugin.h"
#include "../plugin.h"
#include "../utils/system.h"

#include <fstream>
#include <iostream>

using namespace std;
using namespace task_representation;

namespace task_transformation {
DumpTransformation::DumpTransformation(const options::Options &opts)
    : filename(opts.get<string>("filename")), exit_after(opts.get<bool>("exit_after")) {
}

pair<shared_ptr<FTSTask>, shared_ptr<PlanReconstruction>>
DumpTransformation::transform_task(const shared_ptr<FTSTask> &task) {
    const int F = task->get_size();
    const int L = task->get_num_labels();
    ofstream out(filename);
    out << "fts " << F << " " << L << "\n";
    for (int l = 0; l < L; ++l)
        out << "label " << l << " " << task->get_label_cost(l) << "\n";
    long long transitions = 0;
    for (int f = 0; f < F; ++f) {
        const TransitionSystem &ts = task->get_ts(f);
        const vector<int> &goals = ts.get_goal_states();
        out << "factor " << f << " " << ts.get_size() << " " << ts.get_init_state() << " " << goals.size();
        for (int g : goals)
            out << " " << g;
        out << "\n";
    }
    for (int f = 0; f < F; ++f) {
        const TransitionSystem &ts = task->get_ts(f);
        for (int l = 0; l < L; ++l) {
            if (is_noop_label(ts, l))
                continue;
            for (const Transition &t : ts.get_transitions_with_label(l)) {
                out << "t " << f << " " << l << " " << t.src << " " << t.target << "\n";
                ++transitions;
            }
        }
    }
    out.close();
    cout << "DUMPFTS " << filename << " factors " << F << " labels " << L
         << " transitions " << transitions << endl;
    if (exit_after)
        utils::exit_with(utils::ExitCode::PLAN_FOUND);
    return make_pair(task, make_shared<PlanReconstructionSequence>(
                         vector<shared_ptr<PlanReconstruction>>()));
}

pair<shared_ptr<FTSTask>, Mapping>
DumpTransformation::transform_task_lossy(const shared_ptr<FTSTask> &task) {
    transform_task(task);
    return make_pair(task, Mapping());
}

static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Dump the FTS", "Writes the FTS to a file and passes it on unchanged.");
    parser.add_option<string>("filename", "output file (the option parser lower-cases it)");
    parser.add_option<bool>("exit_after", "stop the planner after writing", "false");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<DumpTransformation>(opts);
}

static options::PluginShared<TaskTransformation> _plugin("dump_fts", _parse);
}
