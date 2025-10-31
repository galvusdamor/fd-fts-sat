#include "fts_factory.h"

#include "distances.h"
#include "factored_transition_system.h"
#include "merge_and_shrink_representation.h"
#include "types.h"

#include "../task_representation/labels.h"
#include "../task_representation/label_equivalence_relation.h"
#include "../task_representation/sas_task.h"
#include "../task_representation/transition_system.h"

#include "../utils/memory.h"
#include "../utils/system.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>

#include <vector>

using namespace std;
using namespace task_representation;

namespace task_transformation {
class FTSFactory {
    const SASTask &sas_task;

    struct TransitionSystemData {
        // The following two attributes are only used for statistics
        int num_variables;
        vector<int> incorporated_variables;

        unique_ptr<LabelEquivalenceRelation> label_equivalence_relation;
        std::unordered_map<std::pair<int, int>, int> pre_eff_pair_to_label_group;
        std::unordered_map<std::vector<int>, int> factored_transition_to_label_group;


        vector<vector<Transition>> transitions_by_label_group;
        vector<vector<int>> label_groups;

        vector<bool> relevant_labels;
        int num_states;
        vector<bool> goal_states;
        int init_state;

        TransitionSystemData(TransitionSystemData &&other)
            : num_variables(other.num_variables),
              incorporated_variables(move(other.incorporated_variables)),
              label_equivalence_relation(move(other.label_equivalence_relation)),
              pre_eff_pair_to_label_group(move(other.pre_eff_pair_to_label_group)),
              transitions_by_label_group(move(other.transitions_by_label_group)),
              label_groups(move(other.label_groups)),
              relevant_labels(move(other.relevant_labels)),
              num_states(other.num_states),
              goal_states(move(other.goal_states)),
              init_state(other.init_state) {
        }
        TransitionSystemData() = default;
        TransitionSystemData(TransitionSystemData &other) = delete;
        TransitionSystemData &operator=(TransitionSystemData &other) = delete;

        void set_label_equivalence_relation(const Labels & labels) {
            label_equivalence_relation =
                utils::make_unique_ptr<LabelEquivalenceRelation>(labels,
                                                                 label_groups);
        }

        void add_factored_transition(int label_no, const std::vector<int> & target_values) {
            relevant_labels[label_no] = true ;

            assert((size_t)num_states == target_values.size());
            auto pos = factored_transition_to_label_group.find(target_values);
            if (pos == factored_transition_to_label_group.end()) {
                int new_label_group = label_groups.size();
                label_groups.push_back(vector<int>());
                label_groups.back().push_back(label_no);
                factored_transition_to_label_group [target_values] = new_label_group;

                vector<Transition> transitions;
                for (int src = 0; src < num_states; ++src) {
                    int dest_value = target_values[src];
                    assert(dest_value < num_states);
                    if (dest_value >= 0) {
                        transitions.push_back(Transition(src, dest_value));
                    }
                }
                transitions_by_label_group.push_back(transitions);

            } else {
                label_groups[pos->second].push_back(label_no);
            }
        }

        void add_transition(int label_no, int src_value, int dest_value) {

            relevant_labels[label_no] = (dest_value != -1) ;

            assert(dest_value < num_states);
            assert(src_value < num_states);
            auto src_dest = make_pair(src_value, dest_value);
            auto pos = pre_eff_pair_to_label_group.find(src_dest);
            if (pos == pre_eff_pair_to_label_group.end()) {
                int new_label_group = label_groups.size();
                label_groups.push_back(vector<int>());
                label_groups.back().push_back(label_no);
                pre_eff_pair_to_label_group [src_dest] = new_label_group;

                vector<Transition> transitions;
                if (dest_value == -1 && src_value == -1) {
                    transitions.reserve(num_states);
                    for(int s = 0; s < num_states; ++s) {
                        transitions.push_back(Transition(s, s));
                    }
                } else if (src_value == -1) {
                    transitions.reserve(num_states);
                    for(int s = 0; s < num_states; ++s) {
                        transitions.push_back(Transition(s, dest_value));
                    }
                }else {
                    transitions.push_back(Transition(src_value, dest_value));
                }
                transitions_by_label_group.push_back(transitions);

            } else {
                label_groups[pos->second].push_back(label_no);
            }

        }

    };
    vector<TransitionSystemData> transition_system_data_by_var;

    unique_ptr<Labels> create_labels();
    // void build_label_equivalence_relation(LabelEquivalenceRelation &label_equivalence_relation);
    void build_state_data(int var_no);
    void initialize_transition_system_data();
    void add_transition(int var_no, int label_no,
                        int src_value, int dest_value);
    void build_transitions();
    vector<unique_ptr<TransitionSystem>> create_transition_systems();
//    vector<unique_ptr<MergeAndShrinkRepresentation>> create_mas_representations();
//    vector<unique_ptr<Distances>> create_distances(
//        const vector<unique_ptr<TransitionSystem>> &transition_systems);
public:
    explicit FTSFactory(const SASTask &sas_task);

    /*
      Note: create() may only be called once. We don't worry about
      misuse because the class is only used internally in this file.
    */
    pair<unique_ptr<Labels>, vector<unique_ptr<TransitionSystem>>> create();
};

FTSFactory::FTSFactory(const SASTask &sas_task) : sas_task(sas_task) {
}

unique_ptr<Labels> FTSFactory::create_labels() {
    vector<unique_ptr<Label>> result;
//    vector<vector<int>> sas_op_indices_by_label;
    int num_ops = sas_task.get_num_operators();
    int max_num_labels = (num_ops ? num_ops * 2 - 1 : 0);
    result.reserve(max_num_labels);
//    sas_op_indices_by_label.reserve(max_num_labels);
    for (int index = 0; index < num_ops; ++index) {
        result.push_back(utils::make_unique_ptr<Label>(sas_task.get_operator_cost(index, false)));
//        sas_op_indices_by_label.push_back({index});
    }
    return utils::make_unique_ptr<Labels>(move(result), max_num_labels);
}

// void FTSFactory::build_label_equivalence_relation(
//     LabelEquivalenceRelation &label_equivalence_relation) {
//     /*
//       Prepare label_equivalence_relation data structure: add one single-element
//       group for every operator.
//     */
//     int num_labels = sas_task.get_num_operators();
//     for (int label_no = 0; label_no < num_labels; ++label_no) {
//         // We use the label number as index for transitions of groups.
//         label_equivalence_relation.add_label_group({LabelID(label_no)});
//     }
// }

void FTSFactory::build_state_data(int var_no) {
    TransitionSystemData &ts_data = transition_system_data_by_var[var_no];
    ts_data.init_state = sas_task.get_initial_state_data()[var_no];

    int range = sas_task.get_variable_domain_size(var_no);
    ts_data.num_states = range;

    int goal_val = sas_task.get_goal_value(var_no);
    if (goal_val == -1){
        ts_data.goal_states.resize(range, true);
    } else {
        ts_data.goal_states.resize(range, false);
        ts_data.goal_states[goal_val] = true;
    }
}

void FTSFactory::initialize_transition_system_data() {
    int num_variables = sas_task.get_num_variables();
    int num_labels = sas_task.get_num_operators();
    transition_system_data_by_var.resize(num_variables);
    for (int var_no = 0; var_no < num_variables; ++var_no) {
        TransitionSystemData &ts_data = transition_system_data_by_var[var_no];
        ts_data.num_variables = num_variables;
        ts_data.incorporated_variables.push_back(var_no);
        // ts_data.label_equivalence_relation = utils::make_unique_ptr<LabelEquivalenceRelation>(labels);
        // build_label_equivalence_relation(*ts_data.label_equivalence_relation);
        ts_data.relevant_labels.resize(num_labels, false);
        build_state_data(var_no);
    }
}

void FTSFactory::build_transitions() {
    int num_variables = sas_task.get_num_variables();
    int num_labels = sas_task.get_num_operators();
    /*
      - Add all transitions.
      - Computes relevant operator information as a side effect.
    */
    for (int label_no = 0; label_no < sas_task.get_num_operators(); ++label_no) {
        const auto & op = sas_task.get_operator(label_no);
        /*
          - Mark op as relevant in the transition systems corresponding
          to variables on which it has a precondition or effect.
          - Add transitions induced by op in these transition systems.
        */
        unordered_map<int, int> pre_val;
        for (const auto & precondition : op.get_preconditions()) {
            pre_val[precondition.var] = precondition.val;
        }
        vector <bool> has_effect_on_var(sas_task.get_num_variables(), false);
        std::unordered_map<int, vector<int>> factored_variables;

        for (const SASEffect & effect : op.get_effects()) {
            int var_no = effect.var;
            has_effect_on_var[var_no] = true;
            int post_value = effect.val;

            int pre_value = -1;
            auto pre_val_it = pre_val.find(var_no);
            if (pre_val_it != pre_val.end()) {
                pre_value = pre_val_it->second;
            }

            if (effect.conditions.empty()) {
                transition_system_data_by_var[var_no].add_transition(label_no, pre_value , post_value);
            } else {
               //Alvaro: modified support of conditional effects to assume factored tasks
                if (effect.conditions.size() == 1 && effect.var == effect.conditions[0].var) {
                    if (pre_value != -1 && pre_value != effect.conditions[0].val) {
                        //Alvaro: we can just ignore this effect, but we raise an error just to check this does not happen
                        cerr << "Error: inconsistent precondition and conditional effect." << endl;
                        cerr << "Precondition: " << op.get_name() << " var: " << effect.var
                             << " value: " << pre_value << endl;
                        cerr << "Conditional effect: " << op.get_name() << " var: "
                             << effect.var << " value: " << effect.conditions[0].val << endl;
                        utils::exit_with(utils::ExitCode::UNSUPPORTED);
                    }
                    //cout << "Encoding factored conditional effect" << op.get_name() << endl;
                    auto & values = factored_variables[effect.var];
                    if (values.empty()) {
                        for (int val = 0; val < sas_task.get_variable_domain_size(effect.var); ++val) {
                            values.push_back(val);
                        }
                    }
                    values[effect.conditions[0].val] = post_value;
                } else {
                    cerr << "Error: general conditional effects are not supported." << endl;
                    cerr << "Conditional effect on " << op.get_name() << endl;
                    utils::exit_with(utils::ExitCode::UNSUPPORTED);
                }
            }
        }

        for (const auto & [var, values] : factored_variables) {
            transition_system_data_by_var[var].add_factored_transition(label_no, values);
        }

        /*
          We must handle preconditions *after* effects because handling
          the effects sets has_effect_on_var.
        */
        for (const auto & precondition : op.get_preconditions()) {
            int var_no = precondition.var;
            if (!has_effect_on_var[var_no]) {
                int value = precondition.val;
                transition_system_data_by_var[var_no].add_transition(label_no, value, value);
            }
        }
    }

    for (int var_no = 0; var_no < num_variables; ++var_no) {
        // Make all irrelevant labels explicit.
        for (int label_no = 0; label_no < num_labels; ++label_no) {
            if (!transition_system_data_by_var[var_no].relevant_labels[label_no]) {
                transition_system_data_by_var[var_no].add_transition(label_no, -1, -1);
            }
        }
    }
}

vector<unique_ptr<TransitionSystem>> FTSFactory::create_transition_systems() {
    // Create the actual TransitionSystem objects.
    int num_variables = sas_task.get_num_variables();

    // We reserve space for the transition systems added later by merging.
    vector<unique_ptr<TransitionSystem>> result;
    assert(num_variables >= 1);
    result.reserve(num_variables * 2 - 1);

    const bool compute_label_equivalence_relation = true;
    for (int var_no = 0; var_no < num_variables; ++var_no) {
        TransitionSystemData &ts_data = transition_system_data_by_var[var_no];
        result.push_back(utils::make_unique_ptr<TransitionSystem>(
                             ts_data.num_variables,
                             move(ts_data.incorporated_variables),
                             move(ts_data.label_equivalence_relation),
                             move(ts_data.transitions_by_label_group),
                             ts_data.num_states,
                             move(ts_data.goal_states),
                             ts_data.init_state,
                             compute_label_equivalence_relation
                             ));
    }
    return result;
}

pair<unique_ptr<Labels>, vector<unique_ptr<TransitionSystem>>> FTSFactory::create() {
    unique_ptr<Labels> labels = create_labels();
    initialize_transition_system_data();
    build_transitions();

    for (size_t var_no = 0; var_no < transition_system_data_by_var.size(); ++var_no) {
        transition_system_data_by_var[var_no].set_label_equivalence_relation(*labels);
    }
    return make_pair(move(labels), create_transition_systems());
}

pair<unique_ptr<Labels>, vector<unique_ptr<TransitionSystem>>>
    create_labels_and_transition_systems(const SASTask &sas_task) {
    return FTSFactory(sas_task).create();
}

vector<unique_ptr<MergeAndShrinkRepresentation>> create_mas_representations(
    const vector<unique_ptr<TransitionSystem>> &transition_systems) {
    // Create the actual MergeAndShrinkRepresentation objects.
    int num_variables = transition_systems.size();

    // We reserve space for the transition systems added later by merging.
    vector<unique_ptr<MergeAndShrinkRepresentation>> result;
    assert(num_variables >= 1);
    result.reserve(num_variables * 2 - 1);

    for (int var_no = 0; var_no < num_variables; ++var_no) {
        int range = transition_systems[var_no]->get_size();
        result.push_back(
            utils::make_unique_ptr<MergeAndShrinkRepresentationLeaf>(var_no, range));
    }
    return result;
}

vector<unique_ptr<Distances>> create_distances(
    const vector<unique_ptr<TransitionSystem>> &transition_systems) {
    // Create the actual Distances objects.
    int num_variables = transition_systems.size();

    // We reserve space for the transition systems added later by merging.
    vector<unique_ptr<Distances>> result;
    assert(num_variables >= 1);
    result.reserve(num_variables * 2 - 1);

    for (int var_no = 0; var_no < num_variables; ++var_no) {
        result.push_back(
            utils::make_unique_ptr<Distances>(*transition_systems[var_no]));
    }
    return result;
}
}
