#include "task_transformation_load.h"

#include "plan_reconstruction.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/label_equivalence_relation.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"
#include "../options/plugin.h"
#include "../plugin.h"
#include "../utils/system.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;
using namespace task_representation;

namespace task_transformation {
namespace {
// the loaded task's plan cannot be mapped back to the input task: stop when a plan is found
class PlanReconstructionLoaded : public PlanReconstruction {
public:
    virtual void reconstruct_plan(Plan &) const override {
        cout << "LOADFTS plan found; not mapped back to the input task" << endl;
        utils::exit_with(utils::ExitCode::PLAN_FOUND);
    }
    virtual bool map_state_sets(StateSets &, string &why) const override {
        why = "load_fts";
        return false;
    }
    virtual bool map_label_sets(LabelSets &, string &why) const override {
        why = "load_fts";
        return false;
    }
};
}

LoadTransformation::LoadTransformation(const options::Options &opts)
    : filename(opts.get<string>("filename")) {
}

pair<shared_ptr<FTSTask>, shared_ptr<PlanReconstruction>>
LoadTransformation::transform_task(const shared_ptr<FTSTask> &) {
    ifstream in(filename);
    if (!in) {
        cerr << "load_fts: cannot read " << filename << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
    int F = 0, L = 0;
    vector<int> cost, size, init;
    vector<vector<int>> goals;
    vector<vector<vector<Transition>>> trans;   // [factor][label]
    vector<vector<bool>> listed;                // [factor][label]: has t-lines
    string line, kind;
    while (getline(in, line)) {
        istringstream ls(line);
        if (!(ls >> kind)) continue;
        if (kind == "fts") {
            ls >> F >> L;
            cost.assign(L, 0); size.assign(F, 0); init.assign(F, 0); goals.assign(F, {});
            trans.assign(F, vector<vector<Transition>>(L));
            listed.assign(F, vector<bool>(L, false));
        } else if (kind == "label") {
            int l, c; ls >> l >> c; cost[l] = c;
        } else if (kind == "factor") {
            int f, n, i, g; ls >> f >> n >> i >> g;
            size[f] = n; init[f] = i;
            for (int k = 0; k < g; ++k) { int s; ls >> s; goals[f].push_back(s); }
        } else if (kind == "t") {
            int f, l, s, t; ls >> f >> l >> s >> t;
            trans[f][l].emplace_back(s, t);
            listed[f][l] = true;
        }
    }
    vector<unique_ptr<Label>> labelVec;
    for (int l = 0; l < L; ++l)
        labelVec.push_back(make_unique<Label>(cost[l]));
    auto labels = make_unique<Labels>(move(labelVec), max(2 * L - 1, 1));
    vector<unique_ptr<TransitionSystem>> systems;
    long long transitions = 0;
    for (int f = 0; f < F; ++f) {
        vector<vector<Transition>> byLabel(L);
        for (int l = 0; l < L; ++l) {
            if (listed[f][l]) {
                byLabel[l] = trans[f][l];
                sort(byLabel[l].begin(), byLabel[l].end(), [](const Transition &a, const Transition &b) {
                    return a.src < b.src || (a.src == b.src && a.target < b.target); });
            } else {
                for (int s = 0; s < size[f]; ++s)
                    byLabel[l].emplace_back(s, s);
            }
            transitions += byLabel[l].size();
        }
        vector<bool> goal(size[f], false);
        for (int g : goals[f]) goal[g] = true;
        // one group per label (group i = label i); the constructor merges equal ones
        vector<vector<int>> groups;
        for (int l = 0; l < L; ++l) groups.push_back({l});
        systems.push_back(make_unique<TransitionSystem>(
            F, vector<int>{f}, make_unique<LabelEquivalenceRelation>(*labels, groups), move(byLabel), size[f],
            move(goal), init[f], true));
    }
    cout << "LOADFTS " << filename << " factors " << F << " labels " << L << " transitions "
         << transitions << endl;
    auto task = make_shared<FTSTask>(move(systems), move(labels));
    return make_pair(task, make_shared<PlanReconstructionLoaded>());
}

pair<shared_ptr<FTSTask>, Mapping>
LoadTransformation::transform_task_lossy(const shared_ptr<FTSTask> &task) {
    auto loaded = transform_task(task);
    return make_pair(loaded.first, Mapping());
}

static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Load an FTS", "Replaces the task by the FTS written by dump_fts to a file.");
    parser.add_option<string>("filename", "dump_fts file (the option parser lower-cases it)");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<LoadTransformation>(opts);
}

static options::PluginShared<TaskTransformation> _plugin("load_fts", _parse);
}
