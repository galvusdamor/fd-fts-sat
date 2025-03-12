#include "search_task.h"

#include "fts_operators.h"
#include "fts_task.h"
#include "label_equivalence_relation.h"
#include "labels.h"
#include "sas_task.h"
#include "transition_system.h"
#include "state.h"

#include "../globals.h"

#include "../algorithms/int_packer.h"

#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <map>
#include <unordered_map>
#include "../../search/plan.h"

using namespace std;

namespace task_representation {
// OperatorTree::OperatorTree (const FTSTask & fts_task,
//                             vector<AbstractOperator> & operators) {
//     map<OperatorTreeNode, int> cache;
//     int num_labels = fts_task.get_num_labels();
//     for (int label_no = 0; label_no < num_labels; ++label_no) {
//         vector<AbstractOperator> new_operators;
//         root_per_label[label_no] = construct_tree (label_no, fts_task, operators, operators_by_label, cache, new_operators, 0);
//     }
// }

// void OperatorTree::get_operator_ids(const GlobalState & state, int label, vector<OperatorID> & operators) const {
//     int node_index = root_per_label[label];
//     while(nodes[node_index].variable >= 0) {
//         const int value = state[nodes[node_index].variable];
//         node_index = nodes[node_index].index_operators[value];
//         assert(node_index != 0);
//     }
//     return nodes[node_index].index_operators;
// }

    std::unique_ptr<int_packer::IntPacker> compute_state_packer(const FTSTask &fts_task) {
        std::vector<int> sizes;
        sizes.reserve(fts_task.get_size());
        for (int ts_index = 0; ts_index < fts_task.get_size(); ++ts_index) {
            const TransitionSystem &ts = fts_task.get_ts(ts_index);
            sizes.push_back(ts.get_size());
        }
        return utils::make_unique_ptr<int_packer::IntPacker>(sizes);
    }


    SearchTask::SearchTask(const FTSTask &fts_task, bool print_time) :
            fts_task(fts_task),
            state_packer(move(compute_state_packer(fts_task))),
            //    axiom_evaluator (fts_task),
            min_operator_cost(fts_task.get_min_operator_cost()) {
        size_t num_variables = fts_task.get_size();
        int num_labels = fts_task.get_num_labels();
        utils::Timer timer;
        cout << "Building search task wrapper for FTS task with " << num_variables
             << " variables and " << num_labels << " labels..." << endl;
        activated_labels_by_var_by_state.resize(num_variables);
        for (size_t var = 0; var < num_variables; ++var) {
            const TransitionSystem &ts = fts_task.get_ts(var);
            activated_labels_by_var_by_state[var].resize(ts.get_size(), boost::dynamic_bitset<>(num_labels, false));
            for (const auto &group_and_transitions : ts) {
                boost::dynamic_bitset<> bs(num_labels, false);
                for (int label : group_and_transitions.label_group) {
                    bs.set(label);
                }
                for (const auto &transition :  group_and_transitions.transitions) {
                    activated_labels_by_var_by_state[var][transition.src] |= bs;
                }
            }
        }

        create_fts_operators();

        initial_state.reserve(num_variables);
        for (size_t index = 0; index < num_variables; ++index) {
            const TransitionSystem &ts = fts_task.get_ts(index);
            if (ts.is_goal_relevant()) {
                goal_relevant_transition_systems.push_back(index);
            }
            initial_state.push_back(ts.get_init_state());
        }

        if (print_time) {
            cout << "Done building search task wrapper for FTS task: " << timer << endl;
        } else {
            cout << "Done building search task wrapper for FTS task." << endl;
        }
    }

    bool SearchTask::is_label_group_relevant(
            int num_states, const vector<Transition> &transitions) {
        if (static_cast<int>(transitions.size()) == num_states) {
            /*
              A label group is irrelevant it has exactly a self-loop transition
              for every state.
            */
            for (const Transition &transition : transitions) {
                if (transition.target != transition.src) {
                    return true;
                }
            }
        } else {
            return true;
        }
        return false;
    }

    bool SearchTask::are_transitions_deterministic(const vector<Transition> &transitions) {
        set<int> sources;
        for (const Transition &t : transitions) {
            if (sources.count(t.src)) {
                return false;
            } else {
                sources.insert(t.src);
            }
        }
        return true;
    }

/*
  If there are no relevant non-deterministic transition systems for the label,
  then this method adds a single FTSOperator with empty effects.
*/
    void SearchTask::multiply_out_non_deterministic_labels(
            LabelID label_id,
            const vector<vector<int>> &targets_by_ts_index,
            int ts_index,
            vector<FactPair> &effects) {
        const vector<int> &non_det_ts = label_to_info[label_id].relevant_non_deterministic_transition_systems;
        if (ts_index == static_cast<int>(non_det_ts.size())) {
            OperatorID op_id(operators.size());
//        cout << "For var " << non_det_ts[ts_index] << " and label " << label_id
//             << ", creating FTSOperator " << op_id << " with effects ";
//        for (const auto &fact : effects) {
//            cout << fact.var << ":=" << fact.value << ", ";
//        }
//        cout << endl;
            operators.emplace_back(op_id, label_id, fts_task.get_label_cost(label_id), effects);
            label_to_info[label_id].fts_operators.push_back(op_id);
            return;
        }
        const vector<int> &targets = targets_by_ts_index[ts_index];
        int var = label_to_info[label_id].relevant_non_deterministic_transition_systems[ts_index];
        for (int target : targets) {
            effects.emplace_back(var, target);
            multiply_out_non_deterministic_labels(label_id, targets_by_ts_index, ts_index + 1, effects);
            effects.pop_back();
        }
    }

    void SearchTask::create_fts_operators() {
        size_t num_variables = fts_task.get_size();
        int num_labels = fts_task.get_num_labels();
        label_to_info.resize(num_labels);
        // Set of targets (no duplicates) of transitions indexed by labels and by
        // the indices of the non-deterministic transition systems of that label.
        vector<vector<vector<int>>> targets_by_label_by_ts_index(num_labels);
        // Map from targets to possible sources for that target, indexed as targets_by_label_by_ts_index.
        vector<vector<unordered_map<int, vector<int>>>> target_to_sources_by_label_by_ts_index(num_labels);
        for (size_t var = 0; var < num_variables; ++var) {
            const TransitionSystem &ts = fts_task.get_ts(var);
            for (const GroupAndTransitions &gat : ts) {
                const LabelGroup &label_group = gat.label_group;
                const vector<Transition> &transitions = gat.transitions;
                if (is_label_group_relevant(ts.get_size(), transitions)) {
                    bool deterministic = are_transitions_deterministic(transitions);
                    if (deterministic) {
                        int single_target = -1;
                        bool is_single_target = true;
                        unordered_map<int, int> src_to_target;
                        for (const Transition &t : transitions) {
                            assert(!src_to_target.count(t.src));
                            src_to_target[t.src] = t.target;
                            if (single_target == -1) {
                                single_target = t.target;
                            } else if (single_target != t.target) {
                                is_single_target = false;
                            }
                        }
                        if (is_single_target) {
                            assert(single_target >= 0);
                            for (int label_id : label_group) {
                                label_to_info[label_id].static_effects.push_back(FactPair(var, single_target));
                            }
                        } else {
                            for (int label_id : label_group) {
                                label_to_info[label_id].relevant_deterministic_transition_systems.push_back(var);
                                label_to_info[label_id].src_to_target_by_ts_index.push_back(src_to_target);
                            }
                        }

                    } else {
                        set<int> targets;
                        unordered_map<int, vector<int>> target_to_sources;
                        for (const Transition &t : transitions) {
                            targets.insert(t.target);
                            target_to_sources[t.target].push_back(t.src);
                        }
                        for (int label_id : label_group) {
                            label_to_info[label_id].relevant_non_deterministic_transition_systems.push_back(var);
                            targets_by_label_by_ts_index[label_id].emplace_back(targets.begin(), targets.end());
                            target_to_sources_by_label_by_ts_index[label_id].push_back(target_to_sources);
                        }
                    }
                }
            }
        }

        /*
          Compute the FTSOperators for all labels by multiplying out the
          combinations of possible target states in all relevant non-deterministic
          transition systems. For the relevant deterministic transition systems,
          add one "dummy" FTSOperator to have an ID for the label.

          Furthermore, for all relevant non-deterministic transition systems of
          a label, compute bitsets indexed by their states that have bits sets to
          true if the corresponding FTSOperator of the label with the right effect
          is applicable.
        */
        for (LabelID label_id(0); label_id < num_labels; ++label_id) {
            // Multiply out FTSOperators for labels with relevant non-deterministic
            // transition systems.
            vector<FactPair> effects;
            multiply_out_non_deterministic_labels(label_id, targets_by_label_by_ts_index[label_id], 0, effects);

            const vector<int> &relevant_non_det_ts = label_to_info[label_id].relevant_non_deterministic_transition_systems;
            const vector<OperatorID> &fts_operators = label_to_info[label_id].fts_operators;
            vector<vector<boost::dynamic_bitset<>>> &applicable_fts_ops_by_ts_index_by_state =
                    label_to_info[label_id].applicable_ops_by_ts_index_by_state;
            applicable_fts_ops_by_ts_index_by_state.resize(relevant_non_det_ts.size());
            // Go over all relevant non-deterministc transition systems of the label...
            for (size_t ts_index = 0; ts_index < relevant_non_det_ts.size(); ++ts_index) {
                int var = relevant_non_det_ts[ts_index];
                const TransitionSystem &ts = fts_task.get_ts(var);
                applicable_fts_ops_by_ts_index_by_state[ts_index].resize(
                        ts.get_size(), boost::dynamic_bitset<>(fts_operators.size(), false));

                // ...and over all fts operators induced by the label...
                for (size_t op_index = 0; op_index < fts_operators.size(); ++op_index) {
                    OperatorID fts_op_id = fts_operators[op_index];
                    const FTSOperator &op = operators[fts_op_id.get_index()];
                    assert(label_id == op.get_label());
                    const vector<FactPair> &_effects = op.get_effects();
                    /*
                      ... and find the effect (= target state) relevant to the
                      variable and set the operator to be applicable in the source
                      states relevant to the target state.
                    */
                    for (const auto &var_val : _effects) {
                        if (var_val.var == var) { // triggeres exactly once
                            int target = var_val.value;
                            const vector<int> &sources = target_to_sources_by_label_by_ts_index[label_id][ts_index][target];
                            for (int source : sources) {
                                assert(activated_labels_by_var_by_state[var][source].test(label_id));
                                applicable_fts_ops_by_ts_index_by_state[ts_index][source].set(op_index);
                            }
                        }
                    }
                }
            }
        }
    }

    bool SearchTask::is_goal_state(const GlobalState &state) const {
        // TODO: should use our own method in the long term
        return fts_task.is_goal_state(state);
    }

    bool SearchTask::is_goal_state(const PlanState &state) const {
        // TODO: should use our own method in the long term
        return fts_task.is_goal_state(state);
    }

    bool SearchTask::has_effect(const GlobalState &predecessor, OperatorID op_id, const FactPair &fact) const {
        // Ideally, we would assert that the operator is applicable.
        const FTSOperator &fts_op = operators[op_id.get_index()];
        LabelID label = fts_op.get_label();

        // Static Effects
        for (const auto &eff : label_to_info[label].static_effects) {
            if (eff == fact) {
                return true;
            }
        }

        // Effects on non-deterministic TS
        for (const FactPair &effect : fts_op.get_effects()) {
            if (effect == fact) {
                return true;
            }
        }

        // Effects on deterministic TS
        const vector<int> &det_ts = label_to_info[label].relevant_deterministic_transition_systems;
        const vector<unordered_map<int, int>> &src_to_target_by_ts_index = label_to_info[label].src_to_target_by_ts_index;
        for (size_t ts_index = 0; ts_index < det_ts.size(); ++ts_index) {
            int var = det_ts[ts_index];
            if (var == fact.var &&
                src_to_target_by_ts_index[ts_index].at(predecessor[var]) == fact.value) {
                return true;
            }
        }

        return false;

//    axiom_evaluator.evaluate(buffer, *state_packer);
    }

// We only need predecessor to access certain variables' values.
    void SearchTask::apply_operator(
            const GlobalState &predecessor, OperatorID op_id, PackedStateBin *buffer) const {
        // Ideally, we would assert that the operator is applicable.
        const FTSOperator &fts_op = operators[op_id.get_index()];

        LabelID label = fts_op.get_label();
        // Static Effects
        for (const auto &eff : label_to_info[label].static_effects) {
            state_packer->set(buffer, eff.var, eff.value);
        }

        // Effects on deterministic TS
        const vector<int> &det_ts = label_to_info[label].relevant_deterministic_transition_systems;
        const vector<unordered_map<int, int>> &src_to_target_by_ts_index = label_to_info[label].src_to_target_by_ts_index;
        for (size_t ts_index = 0; ts_index < det_ts.size(); ++ts_index) {
            int var = det_ts[ts_index];
            const unordered_map<int, int> &src_to_target = src_to_target_by_ts_index[ts_index];
            state_packer->set(buffer, var, src_to_target.at(predecessor[var]));
        }

        // Effects on non-deterministic TS
        for (const FactPair &effect : fts_op.get_effects()) {
            state_packer->set(buffer, effect.var, effect.value);
        }
//    axiom_evaluator.evaluate(buffer, *state_packer);
    }

    // We only need predecessor to access certain variables' values.
    void SearchTask::apply_operator(
            const GlobalState &predecessor, OperatorID op_id, vector<int> &buffer) const {
        // Ideally, we would assert that the operator is applicable.
        const FTSOperator &fts_op = operators[op_id.get_index()];

        LabelID label = fts_op.get_label();
        // Static Effects
        for (const auto &eff : label_to_info[label].static_effects) {
            buffer[eff.var] = eff.value;
        }

        // Effects on deterministic TS
        const vector<int> &det_ts = label_to_info[label].relevant_deterministic_transition_systems;
        const vector<unordered_map<int, int>> &src_to_target_by_ts_index =
                label_to_info[label].src_to_target_by_ts_index;
        for (size_t ts_index = 0; ts_index < det_ts.size(); ++ts_index) {
            int var = det_ts[ts_index];
            const unordered_map<int, int> &src_to_target = src_to_target_by_ts_index[ts_index];
            buffer[var] = src_to_target.at(predecessor[var]);
        }

        // Effects on non-deterministic TS
        for (const FactPair &effect : fts_op.get_effects()) {
            buffer[effect.var] = effect.value;
        }
    }

    // We only need predecessor to access certain variables' values.
    void SearchTask::apply_operator(
            const std::vector<int> &predecessor, const OperatorID &op_id, vector<int> &buffer) const {
        // Ideally, we would assert that the operator is applicable.
        const FTSOperator &fts_op = operators[op_id.get_index()];

        LabelID label = fts_op.get_label();
        // Static Effects
        for (const auto &eff : label_to_info[label].static_effects) {
            buffer[eff.var] = eff.value;
        }

        // Effects on deterministic TS
        const vector<int> &det_ts = label_to_info[label].relevant_deterministic_transition_systems;
        const vector<unordered_map<int, int>> &src_to_target_by_ts_index =
                label_to_info[label].src_to_target_by_ts_index;
        for (size_t ts_index = 0; ts_index < det_ts.size(); ++ts_index) {
            int var = det_ts[ts_index];
            const unordered_map<int, int> &src_to_target = src_to_target_by_ts_index[ts_index];
            buffer[var] = src_to_target.at(predecessor[var]);
        }

        // Effects on non-deterministic TS
        for (const FactPair &effect : fts_op.get_effects()) {
            buffer[effect.var] = effect.value;
        }
    }

    vector<int> SearchTask::generate_successor(const State &predecessor, OperatorID op_id) const {
        vector<int> successor(predecessor.get_values());

        const FTSOperator &fts_op = operators[op_id.get_index()];

        LabelID label = fts_op.get_label();
        // Static Effects
        for (const auto &eff : label_to_info[label].static_effects) {
            successor[eff.var] = eff.value;
        }

        // Effects on deterministic TS
        const vector<int> &det_ts = label_to_info[label].relevant_deterministic_transition_systems;
        const vector<unordered_map<int, int>> &src_to_target_by_ts_index =
                label_to_info[label].src_to_target_by_ts_index;
        for (size_t i = 0; i < det_ts.size(); ++i) {
            int var = det_ts[i];
            const unordered_map<int, int> &src_to_target = src_to_target_by_ts_index[i];
            successor[var] = src_to_target.at(predecessor[var]);
        }

        // Effects on non-deterministic TS
        for (const FactPair &effect : fts_op.get_effects()) {
            successor[effect.var] = effect.value;
        }

        return successor;
    }


    void SearchTask::generate_applicable_ops(
            const GlobalState &state, vector<OperatorID> &applicable_ops) const {
        size_t var = 0;

        boost::dynamic_bitset<> activated_labels = activated_labels_by_var_by_state[var][state[var]];
        for (var = 1; var < activated_labels_by_var_by_state.size(); ++var) {
            activated_labels &= activated_labels_by_var_by_state[var][state[var]];
        }

        size_t label_pos = activated_labels.find_first();

        while (label_pos != boost::dynamic_bitset<>::npos) {
            LabelID label(label_pos);
            const vector<OperatorID> &label_operators = label_to_info[label].fts_operators;
            if (label_operators.size() == 1) {
                OperatorID op_id = label_operators[0];
                assert(operators[op_id.get_index()].get_effects().empty());
                applicable_ops.push_back(op_id);
            } else {
                const vector<int> &non_det_ts =
                        label_to_info[label].relevant_non_deterministic_transition_systems;
                assert(!non_det_ts.empty());
                const vector<vector<boost::dynamic_bitset<>>>
                        &applicable_fts_ops_by_ts_index_by_state =
                        label_to_info[label].applicable_ops_by_ts_index_by_state;

                size_t ts_index = 0;
                int _var = non_det_ts[ts_index];
                boost::dynamic_bitset<> applicable_fts_ops = applicable_fts_ops_by_ts_index_by_state[ts_index][state[_var]];
                for (ts_index = 1;
                     ts_index < applicable_fts_ops_by_ts_index_by_state.size(); ++ts_index) {
                    _var = non_det_ts[ts_index];
                    applicable_fts_ops &=
                            applicable_fts_ops_by_ts_index_by_state[ts_index][state[_var]];
                }

                size_t op_index = applicable_fts_ops.find_first();
                while (op_index != boost::dynamic_bitset<>::npos) {
                    applicable_ops.push_back(label_operators[op_index]);
                    op_index = applicable_fts_ops.find_next(op_index);
                }

            }
            label_pos = activated_labels.find_next(label_pos);
        }

    }

    bool SearchTask::is_applicable(const GlobalState &state, OperatorID op) const {
        LabelID label = operators[op.get_index()].get_label();
        for (int var = 0; var < fts_task.get_size(); ++var) {
            int value = state[var];
            if (!activated_labels_by_var_by_state[var][value].test(label)) {
                return false;
            }
        }
        return true;
    }


    int SearchTask::get_operator_cost(OperatorID op) const {
        return fts_task.get_label_cost(operators[op.get_index()].get_label());
    }

    string SearchTask::operator_names(__attribute__ ((unused))LabelID label) const {
/*     const LabelMap &label_map = fts_task.get_label_map();
    vector<int> sas_op_ids;
    for (int sas_op_id = 0; sas_op_id < g_sas_task()->get_num_operators(); ++sas_op_id) {
        if (label_map.get_reduced_label(sas_op_id) == label) {
            sas_op_ids.push_back(sas_op_id);
        }
    }
    string operator_names = "";
    for (size_t i = 0; i < sas_op_ids.size(); ++i) {
        int sas_op_index = sas_op_ids[i];
        operator_names += g_sas_task()->get_operator_name(sas_op_index, false);
        if (i != sas_op_ids.size() - 1) {
            operator_names += " ";
        }
    }
    return operator_names; */
    return "";
    }

    void SearchTask::dump_op(OperatorID op) const {
        const FTSOperator &fts_op = operators[op.get_index()];
        LabelID label = fts_op.get_label();
//    const LabelMap &label_map = fts_task.get_label_map();
//    vector<int> sas_op_ids;
//    for (int sas_op_id = 0; sas_op_id < g_sas_task()->get_num_operators(); ++sas_op_id) {
//        if (label_map.get_reduced_label(sas_op_id) == label) {
//            sas_op_ids.push_back(sas_op_id);
//        }
//    }
//    string operator_names = "";
//    for (size_t i = 0; i < sas_op_ids.size(); ++i) {
//        int sas_op_index = sas_op_ids[i];
//        operator_names += g_sas_task()->get_operator_name(sas_op_index, false);
//        if (i != sas_op_ids.size() - 1) {
//            operator_names += " ";
//        }
//    }
//    operator_names += ":";
//    cout << operator_names;
        cout << "label ID " << label << ":";

        const vector<int> &det_ts = label_to_info[label].relevant_deterministic_transition_systems;
        const vector<unordered_map<int, int>> &src_to_target_by_ts_index = label_to_info[label].src_to_target_by_ts_index;
        for (size_t ts_index = 0; ts_index < det_ts.size(); ++ts_index) {
            int var = det_ts[ts_index];
            const unordered_map<int, int> &src_to_target = src_to_target_by_ts_index[ts_index];
            for (const pair<int, int> var_val : src_to_target) {
                cout << " [var" << var << ": " << var_val.first << "] [var" << var << ":= "
                     << var_val.second << "]";
            }
        }

        // Effects on non-deterministic TS
        for (const FactPair &effect : fts_op.get_effects()) {
            cout << "[var" << effect.var << ":= " << effect.value << "]" << endl;
        }
        cout << endl;
    }

}
