#include "task_transformation_label_union.h"

#include "label_transformation_utils.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"
#include "../options/plugin.h"
#include "../plugin.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <algorithm>
#include <iostream>
#include <map>

using namespace std;
using namespace task_representation;

namespace task_transformation {
LabelUnionTransformation::LabelUnionTransformation(const options::Options &opts)
    : require_no_preconditions(opts.get<bool>("require_no_preconditions")),
      max_group(opts.get<int>("max_group")) {
}

pair<shared_ptr<FTSTask>, shared_ptr<PlanReconstruction>>
LabelUnionTransformation::transform_task(const shared_ptr<FTSTask> &task) {
    utils::Timer timer;
    const int F = task->get_size();
    const int L = task->get_num_labels();
    const Labels &old_labels = task->get_labels();

    vector<vector<vector<Transition>>> transitions(F);
    for (int f = 0; f < F; ++f)
        transitions[f] = transitions_by_label(task->get_ts(f), L);
    auto is_identity = [&](int f, const vector<Transition> &tr) {
        if ((int)tr.size() != task->get_ts(f).get_size()) return false;
        for (const Transition &t : tr)
            if (t.src != t.target) return false;
        return true;   // sorted and duplicate-free: one self-loop per state
    };

    // candidates: current labels moving exactly one factor; key = cost and the transitions in
    // every other factor that are not self-loops everywhere
    map<pair<int, vector<pair<int, vector<Transition>>>>, vector<pair<int, int>>> groups;
    vector<char> grouped(L, 0);
    int single = 0;
    for (int l = 0; l < L; ++l) {
        if (!old_labels.is_current_label(l)) continue;
        int moving = -1, n = 0;
        for (int f = 0; f < F && n < 2; ++f)
            for (const Transition &t : transitions[f][l])
                if (t.src != t.target) { moving = f; ++n; break; }
        if (n != 1) continue;
        ++single;
        vector<pair<int, vector<Transition>>> rest;
        for (int f = 0; f < F; ++f)
            if (f != moving && !is_identity(f, transitions[f][l]))
                rest.emplace_back(f, transitions[f][l]);
        if (require_no_preconditions && !rest.empty()) continue;
        groups[make_pair(old_labels.get_label_cost(l), move(rest))].emplace_back(moving, l);
    }

    auto reconstruction = make_shared<PlanReconstructionLabelUnion>();
    vector<int> representative;     // per new label: an old label (its cost and position)
    // keep every label not unified, in its old order; unions are appended
    vector<vector<pair<int, int>>> unions;
    for (auto &entry : groups) {
        auto &members = entry.second;
        // one member per factor (exact label reduction normally leaves only one anyway)
        sort(members.begin(), members.end());
        vector<pair<int, int>> group;
        for (size_t i = 0; i < members.size(); ++i) {
            if (i > 0 && members[i].first == members[i - 1].first) continue;
            group.push_back(members[i]);
        }
        for (size_t start = 0; start < group.size(); ) {
            size_t end = group.size();
            if (max_group > 0) end = min(end, start + max_group);
            if (end - start >= 2) {
                vector<pair<int, int>> u(group.begin() + start, group.begin() + end);
                for (const auto &m : u) grouped[m.second] = 1;
                unions.push_back(move(u));
            }
            start = end;
        }
    }
    for (int l = 0; l < L; ++l) {
        if (!old_labels.is_current_label(l) || grouped[l]) continue;
        representative.push_back(l);
        reconstruction->members.push_back({make_pair(-1, l)});
    }
    long long unified = 0;
    for (auto &u : unions) {
        representative.push_back(u.front().second);
        unified += u.size();
        reconstruction->members.push_back(move(u));
    }
    const int L2 = representative.size();
    auto labels = select_labels(old_labels, representative);

    vector<unique_ptr<TransitionSystem>> systems;
    long long before = 0, after = 0;
    for (int f = 0; f < F; ++f) {
        vector<vector<Transition>> new_transitions(L2);
        for (int k = 0; k < L2; ++k) {
            vector<Transition> &tr = new_transitions[k];
            for (const auto &m : reconstruction->members[k])
                tr.insert(tr.end(), transitions[f][m.second].begin(), transitions[f][m.second].end());
            sort_unique(tr);
            after += tr.size();
        }
        for (int l = 0; l < L; ++l) before += transitions[f][l].size();
        const TransitionSystem &ts = task->get_ts(f);
        systems.push_back(build_transition_system(
            ts, *labels, move(new_transitions), ts.get_size(),
            vector<bool>(ts.get_is_goal()), ts.get_init_state()));
    }
    cout << "LABELUNION labels " << L << " single_factor " << single << " unions " << unions.size()
         << " unified_labels " << unified << " new_labels " << L2
         << " transitions_before " << before << " transitions_after " << after
         << " time " << timer() << endl;
    return make_pair(make_shared<FTSTask>(move(systems), move(labels)), reconstruction);
}

pair<shared_ptr<FTSTask>, Mapping>
LabelUnionTransformation::transform_task_lossy(const shared_ptr<FTSTask> &) {
    cerr << "label_union() does not support lossy transformations" << endl;
    utils::exit_with(utils::ExitCode::UNSUPPORTED);
}

void PlanReconstructionLabelUnion::reconstruct_plan(Plan &plan) const {
    const vector<int> &labels = plan.get_labels();
    const vector<PlanState> &states = plan.get_traversed_states();
    if (labels.empty())
        return;
    vector<int> new_labels;
    vector<PlanState> new_states;
    new_states.push_back(states[0]);
    int expanded = 0, dropped = 0;
    for (size_t step = 0; step < labels.size(); ++step) {
        const auto &m = members[labels[step]];
        if (m.size() == 1 && m[0].first == -1) {
            new_labels.push_back(m[0].second);
            new_states.push_back(states[step + 1]);
            continue;
        }
        // the members whose factor moved, one after the other
        int fired = 0;
        for (const auto &member : m) {
            const int f = member.first;
            if (states[step][f] == states[step + 1][f]) continue;
            PlanState next(new_states.back());
            next.set(f, states[step + 1][f]);
            new_labels.push_back(member.second);
            new_states.push_back(move(next));
            ++fired;
        }
        if (fired == 0) ++dropped;
        else if (fired > 1) ++expanded;
    }
    cout << "LabelUnion reconstruction: " << labels.size() << " -> " << new_labels.size()
         << " labels, " << expanded << " steps with several members, " << dropped
         << " no-op steps dropped" << endl;
    plan.set_plan(move(new_states), move(new_labels));
}

bool PlanReconstructionLabelUnion::map_label_sets(LabelSets &sets, string &why) const {
    LabelSets after;
    after.operators.resize(members.size());
    for (size_t k = 0; k < members.size(); ++k)
        for (const auto &m : members[k]) {
            if (m.second < 0 || m.second >= (int)sets.operators.size()) { why = "label union label map"; return false; }
            const auto &ops = sets.operators[m.second];
            after.operators[k].insert(after.operators[k].end(), ops.begin(), ops.end());
        }
    sets = move(after);
    return true;
}

static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
    parser.document_synopsis(
        "Union of independent labels",
        "Replaces groups of labels that each move one, different factor and share their "
        "preconditions elsewhere by one label firing any subset of them; see the header.");
    parser.add_option<bool>("require_no_preconditions",
                            "only unite labels that are self-loops everywhere outside their factor", "false");
    parser.add_option<int>("max_group", "unite at most this many labels into one; -1 = no limit", "-1");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<LabelUnionTransformation>(opts);
}

static options::PluginShared<TaskTransformation> _plugin("label_union", _parse);
}
