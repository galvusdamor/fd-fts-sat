#include "sigma_encoding.h"

#include "ipasir.h"
#include "sat_encoder.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../task_utils/label_order_finder.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <functional>
#include <set>

using namespace std;
using namespace task_representation;

namespace sat_search {

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

SigmaEncodingFactory::SigmaEncodingFactory(const options::Options &opts)
    : SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
      labelOrderFinder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
      oneMove(opts.get<bool>("one_move")),
      strict(opts.get<bool>("strict")),
      groupSize(opts.get<int>("group_size")),
      staticOptions(opts) {
    if (strict && !oneMove) {
        cerr << "sigma_sat: strict needs one_move" << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
}

void SigmaEncodingFactory::initialize() {
    utils::Timer timer;
    auto d = make_shared<SigmaData>();
    d->oneMove = oneMove;
    d->strict = strict;
    d->order = labelOrderFinder->find_order(*fts);
    d->groupOf.assign(fts->get_num_labels(), 0);
    for (size_t k = 0; k < d->order.size(); k++)
        d->groupOf[d->order[k]] = k / max(groupSize, 1);
    d->factors.resize(fts->get_size());
    size_t changedVars = 0, labelsInFactors = 0, nondet = 0;
    for (int ts = 0; ts < fts->get_size(); ts++) {
        const TransitionSystem &tss = fts->get_ts(ts);
        for (int label : d->order) {
            if (tss.isIrrelevantLabel(label))
                continue;
            SigmaLabelData ld;
            ld.label = label;
            map<int, set<int>> targets, preds;
            set<int> changed, applicable;
            for (const Transition &t : tss.get_transitions_with_label(label)) {
                applicable.insert(t.src);
                targets[t.src].insert(t.target);
                if (t.src != t.target) {
                    changed.insert(t.src);
                    changed.insert(t.target);
                }
            }
            for (const Transition &t : tss.get_transitions_with_label(label))
                if (changed.count(t.target))
                    preds[t.target].insert(t.src);
            ld.changed.assign(changed.begin(), changed.end());
            ld.applicable.assign(applicable.begin(), applicable.end());
            for (auto &[x, ys] : targets) {
                ld.targets.emplace_back(x, vector<int>(ys.begin(), ys.end()));
                nondet += ys.size() > 1;
            }
            for (int s : ld.changed)
                ld.predecessors.emplace_back(s, vector<int>(preds[s].begin(), preds[s].end()));
            changedVars += ld.changed.size();
            labelsInFactors++;
            d->factors[ts].push_back(move(ld));
        }
    }
    data = d;
    staticConstraints = static_constraints::compute(*fts, staticOptions);
    cout << "SIGMASTAT sigma_vars_per_step " << changedVars << " label_factor_pairs " << labelsInFactors
         << " nondeterministic_sources " << nondet << " time " << timer() << endl;
    cout << "SAT init time: " << timer << endl;
}

unique_ptr<SATEncoding> SigmaEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule) {
    auto encoding = make_unique<SigmaEncoding>(capsule, fts, forceAtLeastOneAction, data);
    encoding->setStaticConstraints(staticConstraints);
    return encoding;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

SigmaEncoding::SigmaEncoding(shared_ptr<sat_capsule> capsule, const shared_ptr<FTSTask> &fts,
                             bool forceAtLeastOneAction, shared_ptr<const SigmaData> _data)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction, false, true), data(_data) {
    // plan extraction executes the labels of a step in this order
    labelOrder = data->order;
}

void SigmaEncoding::generateAdditionalVariables(int /*fromTime*/) {
}

void SigmaEncoding::encode_frame_axioms(const vector<vector<int>> &, const vector<vector<int>> &, int) {
    // a label that does not fire copies the factor's state (encode_transition)
}

void SigmaEncoding::encode_transition(const vector<vector<int>> &previousStateVars,
                                      const vector<vector<int>> &nextStateVars, int fromTime) {
    const vector<int> &labelVars = allTimesLabelVars[fromTime];
    auto &updates = allTimesUpdates[fromTime];
    updates.assign(fts->get_size(), {});
    for (int ts = 0; ts < fts->get_size(); ts++) {
        vector<int> cur = previousStateVars[ts];   // sigma variables of the current position
        int moved = 0;                             // one_move: M of the current position (0 = false)
        const vector<SigmaLabelData> &labels = data->factors[ts];
        // one_move: entering a state sets M; after a move nothing can be entered
        auto oneMoveClauses = [&](const vector<int> &next, const vector<int> &changed, int label) {
            (void) label;   // debug variable names only
            const int m = sat->new_variable();
            DEBUG(sat->registerVariable(m, "moved_" + to_string(ts) + "_" + to_string(label) +
                                        "@" + to_string(fromTime)));
            if (moved) sat->implies(moved, m);
            for (int s : changed) {
                sat->andImpliesOr(vector<int>{-cur[s], next[s]}, vector<int>{m});
                if (moved)
                    sat->andImpliesOr(vector<int>{moved, -cur[s], next[s]}, vector<int>{});
            }
            moved = m;
        };
        for (size_t i = 0; i < labels.size();) {
            // the labels of this factor in the same group
            size_t j = i + 1;
            while (j < labels.size() && data->groupOf[labels[j].label] == data->groupOf[labels[i].label])
                j++;
            // preconditions (all labels: applicable at the group's start state)
            for (size_t k = i; k < j; k++) {
                const SigmaLabelData &ld = labels[k];
                const int l = labelVars[ld.label];
                vector<int> pre;
                for (int x : ld.applicable)
                    pre.push_back(cur[x]);
                sat->impliesOr(l, pre);
                if (data->strict) {
                    vector<int> start;
                    for (int x : ld.applicable)
                        start.push_back(previousStateVars[ts][x]);
                    sat->impliesOr(l, start);
                }
            }
            if (j == i + 1) {
                encode_single(ts, labels[i], labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else {
                encode_group(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            }
            i = j;
        }
        // the last sigma is the state at the end of the step
        for (size_t s = 0; s < cur.size(); s++) {
            if (cur[s] == nextStateVars[ts][s])
                continue;
            sat->implies(cur[s], nextStateVars[ts][s]);
            sat->implies(nextStateVars[ts][s], cur[s]);
        }
    }
}

/*
  One label in its own micro-step: new variables for the states it changes, not-fired copy,
  effect, at most one target per source, justification.
*/
void SigmaEncoding::encode_single(int ts, const SigmaLabelData &ld, const vector<int> &labelVars,
                                  vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates,
                                  const function<void(const vector<int> &, const vector<int> &, int)> &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    if (ld.changed.empty())
        return;
    const int l = labelVars[ld.label];
    vector<int> next = cur;
    SigmaUpdate upd{ld.label, {}, 0};
    for (int s : ld.changed) {
        next[s] = sat->new_variable();
        DEBUG(sat->registerVariable(next[s], "sigma_" + to_string(ts) + "_" + to_string(ld.label) +
                                    "_" + to_string(s) + "@" + to_string(fromTime)));
        upd.vars.emplace_back(s, next[s]);
        sat->andImpliesOr(vector<int>{-l, cur[s]}, vector<int>{next[s]});
        sat->andImpliesOr(vector<int>{-l, next[s]}, vector<int>{cur[s]});
    }
    for (const auto &[x, ys] : ld.targets) {
        if (ys.size() == 1 && ys[0] == x && next[x] == cur[x])
            continue;   // pure self-loop at an unchanged state
        vector<int> eff;
        for (int y : ys)
            eff.push_back(next[y]);
        sat->andImpliesOr(vector<int>{l, cur[x]}, eff);
        for (size_t a = 0; a < ys.size(); a++)
            for (size_t b = a + 1; b < ys.size(); b++)
                sat->andImpliesOr(vector<int>{l, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
    }
    for (const auto &[s, xs] : ld.predecessors) {
        vector<int> from;
        for (int x : xs)
            from.push_back(cur[x]);
        sat->andImpliesOr(vector<int>{l, next[s]}, from);
    }
    if (data->oneMove)
        oneMove(next, ld.changed, ld.label);
    updates.push_back(move(upd));
    cur = move(next);
}

/*
  Several labels in one micro-step, forall-step semantics inside: per factor at most one of them
  moves (from the group's start state p to its end state n), every other firing label self-loops
  at both p and n. mover_l ("l moves the factor in this group") is forced by a firing label that
  lacks a self-loop at p or n, at most one mover_l is true, and the move follows the mover's
  transitions; with no mover, n = p. Labels without real transitions in the factor only need
  self-loops at p and n.
*/
void SigmaEncoding::encode_group(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end,
                                 const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                 vector<SigmaUpdate> &updates,
                                 const function<void(const vector<int> &, const vector<int> &, int)> &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = cur;
    vector<pair<int, int>> vars;
    for (int s : changed) {
        next[s] = sat->new_variable();
        DEBUG(sat->registerVariable(next[s], "sigmaG_" + to_string(ts) + "_" + to_string(labels[begin].label) +
                                    "_" + to_string(s) + "@" + to_string(fromTime)));
        vars.emplace_back(s, next[s]);
    }
    vector<int> movers;
    for (size_t k = begin; k < end; k++) {
        const SigmaLabelData &ld = labels[k];
        const int l = labelVars[ld.label];
        set<int> selfLoops;
        for (const auto &[x, ys] : ld.targets)
            if (binary_search(ys.begin(), ys.end(), x))
                selfLoops.insert(x);
        if (ld.changed.empty()) {
            // only self-loops: also at the end state
            if (!changed.empty()) {
                vector<int> post;
                for (int x : selfLoops)
                    post.push_back(next[x]);
                sat->impliesOr(l, post);
            }
            continue;
        }
        const int mv = sat->new_variable();
        DEBUG(sat->registerVariable(mv, "mover_" + to_string(ts) + "_" + to_string(ld.label) + "@" +
                                    to_string(fromTime)));
        movers.push_back(mv);
        sat->implies(mv, l);
        // a firing label without a self-loop at the start or the end state is the mover
        for (int x : ld.applicable)
            if (!selfLoops.count(x))
                sat->andImpliesOr(vector<int>{l, cur[x]}, vector<int>{mv});
        for (int y : changed)
            if (!selfLoops.count(y))
                sat->andImpliesOr(vector<int>{l, next[y]}, vector<int>{mv});
        // the mover follows its transitions
        for (const auto &[x, ys] : ld.targets) {
            vector<int> eff;
            for (int y : ys)
                eff.push_back(next[y]);
            sat->andImpliesOr(vector<int>{mv, cur[x]}, eff);
            for (size_t a = 0; a < ys.size(); a++)
                for (size_t b = a + 1; b < ys.size(); b++)
                    sat->andImpliesOr(vector<int>{mv, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
        }
        for (int y : changed) {
            vector<int> from;
            for (const auto &[x, ys] : ld.targets)
                if (binary_search(ys.begin(), ys.end(), y))
                    from.push_back(cur[x]);
            sat->andImpliesOr(vector<int>{mv, next[y]}, from);
        }
        updates.push_back(SigmaUpdate{ld.label, vars, mv});
    }
    sat->atMostOne(movers);
    // without a mover the state does not change
    for (int s : changed) {
        sat->andImpliesOr(vector<int>{cur[s], -next[s]}, movers);
        sat->andImpliesOr(vector<int>{-cur[s], next[s]}, movers);
    }
    if (data->oneMove)
        oneMove(next, changed, labels[begin].label);
    cur = move(next);
}

vector<vector<int>> SigmaEncoding::extractIntermediateStates(vector<int> &selectedLabels,
                                                             vector<int> &currentLastState,
                                                             vector<int> & /*nextState*/,
                                                             int labelTimestep) {
    // selectedLabels are in label order; per factor, a label that changes the state (its own
    // micro-step, or the mover of its group) sets it to the changed state whose variable is
    // true, or keeps it if none is (a self-loop at an unchanged state)
    const auto &updates = allTimesUpdates.at(labelTimestep);
    vector<map<int, const SigmaUpdate *>> byLabel(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++)
        for (const SigmaUpdate &u : updates[ts])
            byLabel[ts][u.label] = &u;
    vector<vector<int>> states;
    vector<int> state = currentLastState;
    for (size_t i = 0; i + 1 < selectedLabels.size(); i++) {
        for (int ts = 0; ts < fts->get_size(); ts++) {
            auto it = byLabel[ts].find(selectedLabels[i]);
            if (it == byLabel[ts].end())
                continue;
            const SigmaUpdate &u = *it->second;
            if (u.mover && ipasir_val(sat->solver, u.mover) <= 0)
                continue;
            for (const auto &[s, var] : u.vars)
                if (ipasir_val(sat->solver, var) > 0) {
                    state[ts] = s;
                    break;
                }
        }
        states.push_back(state);
    }
    return states;
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

static shared_ptr<SATEncodingFactory> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Intermediate-state encoding over an FTS",
                             "R2E step semantics (labels in label order, several moves per factor "
                             "and step) with one variable per factor state changed by a label "
                             "(Symbolic Pattern Planning's sigma); see sat/sigma_encoding.h.");
    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
        "label_order", "label order of the step semantics", "label_order_linear()");
    parser.add_option<bool>("one_move",
                            "exists-step (lax): at most one real move per factor and step", "false");
    parser.add_option<bool>("strict",
                            "with one_move: every firing label is applicable at the start of the step",
                            "false");
    parser.add_option<int>("group_size",
                           "consecutive labels of the label order per micro-step (1: every label "
                           "its own micro-step; >1: forall-step inside a micro-step)", "1");
    parser.add_option<bool>("force_at_least_one_action",
                            "force that every time step contains at least one action", "false");
    static_constraints::add_options(parser);
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<SigmaEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin("sigma_sat", _parse);
}
