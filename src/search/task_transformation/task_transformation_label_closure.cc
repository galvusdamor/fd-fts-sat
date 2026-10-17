#include "task_transformation_label_closure.h"

#include "label_transformation_utils.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"
#include "../options/plugin.h"
#include "../plugin.h"
#include "../utils/memory.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>

using namespace std;
using namespace task_representation;

namespace task_transformation {
LabelClosureTransformation::LabelClosureTransformation(const options::Options &opts)
    : max_transitions(opts.get<int>("max_transitions")),
      multi_factor(opts.get<bool>("multi_factor")) {
}

/*
  Pairs (s,t), s != t, connected by an l-path of length >= 1; with elastic_only, only paths
  that visit a state with an l-self-loop (s and t included). BFS over (state, visited a
  self-loop) from every source.
*/
static vector<Transition> closure(const vector<vector<int>> &successors, const vector<char> &selfloop,
                                  bool elastic_only) {
    const int n = successors.size();
    vector<Transition> result;
    vector<int> seen(2 * n, -1);
    for (int s = 0; s < n; ++s) {
        deque<pair<int, int>> queue;
        auto visit = [&](int x, int flag) {
            flag = flag || selfloop[x];
            if (seen[2 * x + flag] == s) return;
            seen[2 * x + flag] = s;
            queue.emplace_back(x, flag);
        };
        for (int t : successors[s])
            visit(t, selfloop[s]);
        while (!queue.empty()) {
            const int x = queue.front().first, flag = queue.front().second;
            queue.pop_front();
            if (x != s && (flag || !elastic_only))
                result.emplace_back(s, x);
            for (int t : successors[x])
                visit(t, flag);
        }
    }
    sort_unique(result);
    return result;
}

pair<shared_ptr<FTSTask>, shared_ptr<PlanReconstruction>>
LabelClosureTransformation::transform_task(const shared_ptr<FTSTask> &task) {
    utils::Timer timer;
    const int F = task->get_size();
    const int L = task->get_num_labels();
    auto reconstruction = make_shared<PlanReconstructionLabelClosure>(L);

    vector<vector<vector<Transition>>> transitions(F);
    for (int f = 0; f < F; ++f)
        transitions[f] = transitions_by_label(task->get_ts(f), L);

    int single = 0, elastic = 0, one_rigid = 0, rigid = 0, closed_labels = 0, skipped = 0;
    long long before = 0, after = 0;
    for (int label = 0; label < L; ++label) {
        vector<int> moving;
        for (int f = 0; f < F; ++f)
            for (const Transition &t : transitions[f][label])
                if (t.src != t.target) { moving.push_back(f); break; }
        if (moving.empty() || (moving.size() > 1 && !multi_factor))
            continue;

        unique_ptr<PlanReconstructionLabelClosure::ClosedLabel> data(
            new PlanReconstructionLabelClosure::ClosedLabel());
        vector<int> rigid_factors;
        for (int f : moving) {
            const int n = task->get_ts(f).get_size();
            PlanReconstructionLabelClosure::LabelFactor lf;
            lf.factor = f;
            lf.successors.resize(n);
            lf.selfloop.assign(n, 0);
            for (const Transition &t : transitions[f][label]) {
                if (t.src == t.target) lf.selfloop[t.src] = 1;
                else lf.successors[t.src].push_back(t.target);
            }
            bool all_elastic = true;
            for (const Transition &t : transitions[f][label])
                if (!lf.selfloop[t.src] && !lf.selfloop[t.target]) { all_elastic = false; break; }
            if (!all_elastic) rigid_factors.push_back(f);
            data->moving.push_back(move(lf));
        }

        // which factors get which closure (see the header): (index in data->moving, elastic only)
        vector<pair<size_t, bool>> todo;
        if (moving.size() == 1) {
            ++single;
            todo.emplace_back(0, false);
        } else if (rigid_factors.empty()) {
            ++elastic;
            for (size_t k = 0; k < moving.size(); ++k) todo.emplace_back(k, true);
        } else if (rigid_factors.size() == 1) {
            ++one_rigid;
            for (size_t k = 0; k < moving.size(); ++k)
                if (moving[k] == rigid_factors[0]) todo.emplace_back(k, false);
        } else {
            ++rigid;
            continue;
        }

        vector<pair<int, vector<Transition>>> results;
        bool too_big = false, changed = false;
        for (const auto &item : todo) {
            const auto &lf = data->moving[item.first];
            vector<Transition> result = closure(lf.successors, lf.selfloop, item.second);
            for (const Transition &t : transitions[lf.factor][label])
                result.push_back(t);
            sort_unique(result);
            if (max_transitions >= 0 && (int)result.size() > max_transitions) too_big = true;
            if (result.size() != transitions[lf.factor][label].size()) changed = true;
            results.emplace_back(lf.factor, move(result));
        }
        if (too_big) { ++skipped; continue; }
        if (!changed) continue;
        ++closed_labels;
        for (auto &r : results) {
            before += transitions[r.first][label].size();
            after += r.second.size();
            transitions[r.first][label] = move(r.second);
        }
        reconstruction->add_closed_label(label, move(data));
    }

    vector<int> all(L);
    for (int l = 0; l < L; ++l) all[l] = l;
    auto labels = select_labels(task->get_labels(), all);
    vector<unique_ptr<TransitionSystem>> systems;
    for (int f = 0; f < F; ++f) {
        const TransitionSystem &ts = task->get_ts(f);
        systems.push_back(build_transition_system(
            ts, *labels, move(transitions[f]), ts.get_size(),
            vector<bool>(ts.get_is_goal()), ts.get_init_state()));
    }
    cout << "LABELCLOSURE labels " << L << " single_factor " << single
         << " multi_elastic " << elastic << " multi_one_rigid " << one_rigid
         << " multi_rigid " << rigid << " closed " << closed_labels << " skipped " << skipped
         << " transitions_before " << before << " transitions_after " << after
         << " time " << timer() << endl;
    return make_pair(make_shared<FTSTask>(move(systems), move(labels)), reconstruction);
}

pair<shared_ptr<FTSTask>, Mapping>
LabelClosureTransformation::transform_task_lossy(const shared_ptr<FTSTask> &) {
    cerr << "label_closure() does not support lossy transformations" << endl;
    utils::exit_with(utils::ExitCode::UNSUPPORTED);
}

/*
  Shortest l-path from s to t (length 0 if s == t and s has a self-loop); with elastic, only
  paths visiting a self-loop state. Returns the states; pad = index of a self-loop state on
  the path (-1 if none). Empty if there is no such path.
*/
static vector<int> find_path(const PlanReconstructionLabelClosure::LabelFactor &lf, int s, int t,
                             bool elastic, int &pad) {
    const int n = lf.successors.size();
    pad = -1;
    if (s == t && lf.selfloop[s]) {
        pad = 0;
        return {s};
    }
    vector<int> parent(2 * n, -2);   // node (2 * state + flag) -> parent node
    deque<int> queue;
    const int start = 2 * s + lf.selfloop[s];
    parent[start] = -1;
    queue.push_back(start);
    int found = -1;
    while (!queue.empty() && found == -1) {
        const int node = queue.front();
        queue.pop_front();
        const int x = node / 2, flag = node % 2;
        for (int y : lf.successors[x]) {
            const int next = 2 * y + ((flag || lf.selfloop[y]) ? 1 : 0);
            if (parent[next] != -2) continue;
            parent[next] = node;
            if (y == t && (!elastic || next % 2)) { found = next; break; }
            queue.push_back(next);
        }
    }
    if (found == -1)
        return {};
    vector<int> path;
    for (int node = found; node != -1; node = parent[node])
        path.push_back(node / 2);
    reverse(path.begin(), path.end());
    for (size_t k = 0; k < path.size(); ++k)
        if (lf.selfloop[path[k]]) { pad = k; break; }
    return path;
}

void PlanReconstructionLabelClosure::reconstruct_plan(Plan &plan) const {
    const vector<int> &labels = plan.get_labels();
    const vector<PlanState> &states = plan.get_traversed_states();
    if (labels.empty())
        return;
    vector<int> new_labels;
    vector<PlanState> new_states;
    new_states.push_back(states[0]);
    int expanded = 0;
    for (size_t step = 0; step < labels.size(); ++step) {
        const int label = labels[step];
        const ClosedLabel *c = closed[label].get();
        if (!c) {
            new_labels.push_back(label);
            new_states.push_back(states[step + 1]);
            continue;
        }
        // one path per moving factor: elastic if possible, else any (the rigid factor)
        vector<vector<int>> paths;
        vector<int> pads;
        size_t K = 1;
        for (const LabelFactor &lf : c->moving) {
            const int s = states[step][lf.factor], t = states[step + 1][lf.factor];
            int pad;
            vector<int> path = find_path(lf, s, t, true, pad);
            if (path.empty())
                path = find_path(lf, s, t, false, pad);
            if (path.empty()) {
                cerr << "label_closure: no path for label " << label << " in factor " << lf.factor
                     << " from " << s << " to " << t << endl;
                utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
            }
            K = max(K, path.size() - 1);
            paths.push_back(move(path));
            pads.push_back(pad);
        }
        // stretch every path to K transitions by repeating its self-loop state
        for (size_t k = 0; k < paths.size(); ++k) {
            const size_t missing = K - (paths[k].size() - 1);
            if (missing == 0) continue;
            if (pads[k] < 0) {
                cerr << "label_closure: cannot stretch the path of label " << label << " in factor "
                     << c->moving[k].factor << " to length " << K << endl;
                utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
            }
            paths[k].insert(paths[k].begin() + pads[k], missing, paths[k][pads[k]]);
        }
        if (K > 1) ++expanded;
        for (size_t i = 1; i <= K; ++i) {
            PlanState next(i == K ? states[step + 1] : new_states.back());
            if (i < K)
                for (size_t k = 0; k < paths.size(); ++k)
                    next.set(c->moving[k].factor, paths[k][i]);
            new_labels.push_back(label);
            new_states.push_back(move(next));
        }
    }
    cout << "LabelClosure reconstruction: " << labels.size() << " -> " << new_labels.size()
         << " labels, " << expanded << " closed transitions expanded" << endl;
    plan.set_plan(move(new_states), move(new_labels));
}

static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
    parser.document_synopsis(
        "Transitive closure of labels",
        "Adds transitions standing for a label applied several times in a row; see the header "
        "for the conditions under which this is sound.");
    parser.add_option<int>("max_transitions",
                           "do not close a label whose closure has more transitions in a factor; -1 = no limit",
                           "-1");
    parser.add_option<bool>("multi_factor",
                            "also close labels that move several factors (elastic closure)", "false");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<LabelClosureTransformation>(opts);
}

static options::PluginShared<TaskTransformation> _plugin("label_closure", _parse);
}
