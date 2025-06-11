#include "plan_reconstruction_tau_path.h"

#include <algorithm>    // std::count

#include "tau_graph.h"

#include "../task_representation/transition_system.h"
#include "../task_representation/sas_task.h"
#include "../task_representation/fact.h"

using namespace task_representation;
using namespace std;

namespace task_transformation {

// Does s has a transition with label to some t whose abstraction is abstract_target?
bool TauShrinking::is_target(int s, int label, int abstract_target) const {
    for (const Transition & tr : transition_system->get_transitions_with_label(label) ) {
        if (tr.src == s &&  (abstract_target == -1 || abstraction[tr.target] == abstract_target)) {
            if (haslum_rule_center_state.empty() || std::find (haslum_rule_center_state.begin(), haslum_rule_center_state.end(), tr.target) !=  haslum_rule_center_state.end()) {
            return true;
        }
    }
    }
    return false;
}

// Does s has a transition with label to some t whose abstraction is abstract_target?
int TauShrinking::get_concrete_target(int s, int label, int abstract_target) const {
    assert(is_target(s, label, abstract_target));
    for (const Transition & tr : transition_system->get_transitions_with_label(label) ) {
        if (tr.src == s &&  (abstract_target == -1 || abstraction[tr.target] == abstract_target)) {
            if (haslum_rule_center_state.empty() || std::find (haslum_rule_center_state.begin(), haslum_rule_center_state.end(), tr.target) !=  haslum_rule_center_state.end()) {
                return tr.target;
            }
        }
    }
    return -1;
}

void TauShrinking::apply_label_mapping(const LabelMapping & label_mapping) {
    transition_system->apply_label_mapping(label_mapping);
    tau_graph->apply_label_mapping(label_mapping);
}


void TauShrinking::
reconstruct_step(int source, const std::vector<bool> & targets,
                 std::vector<int> & new_label_path,
                 std::vector<PlanState> & new_traversed_states) const {
    // Use TauGraph to find a path from src to target
    std::vector<std::pair<task_representation::LabelID, int>> tau_path =
        tau_graph->find_shortest_path(source, targets);

    // Insert tau path into the plan
    for (const auto & step : tau_path) {
        new_label_path.push_back(step.first);
        auto new_state = new_traversed_states.back();
        new_state.set(ts_index_predecessor, step.second);
        new_traversed_states.push_back(new_state);
        // if (transition_system->get_incorporated_variables().size() == 1) {
        //     cout << g_sas_task()->get_fact_name(FactPair(transition_system->get_incorporated_variables()[0], new_state)) << endl;
        // }
    }
}


void TauShrinking::
reconstruct_step(int label, const PlanState & abstract_target, PlanState & concrete_target,
                 std::vector<int> & new_label_path,
                 std::vector<PlanState> & new_traversed_states) const {
    //cout << "Reconstruct step " << ts_index_predecessor << endl;
    //cout << new_traversed_states.back() << endl;
    int source = new_traversed_states.back()[ts_index_predecessor];
    //cout << "Projected: " << source << " -> " << abstraction[source] <<endl;
    assert(source >= 0);
    int projected_abstract_target = (ts_index_successor >= 0 ?  abstract_target[ts_index_predecessor] : -1);

    //cout << "Projected abstract target: " << projected_abstract_target <<endl;

    // if (transition_system->get_incorporated_variables().size() == 1) {
    //     cout << g_sas_task()->get_fact_name(FactPair(transition_system->get_incorporated_variables()[0], source)) << endl;
    // }

    if (!is_target(source, label, projected_abstract_target)) {
        int num_states = transition_system->get_size();
        // Find the right target

        std::vector<bool> targets(num_states, false);
        for (int s = 0; s < num_states; ++s) {
            targets[s] = is_target(s, label, projected_abstract_target);
            /*if (targets[s]) {
                cout << s << " is a valid target" << endl;
                }*/
        }

        assert (std::count (targets.begin(), targets.end(), true) > 0);

        reconstruct_step (source, targets, new_label_path, new_traversed_states);
    }

    int concrete_source = new_traversed_states.back()[ts_index_predecessor];
    int projected_concrete_target = get_concrete_target(concrete_source, label, projected_abstract_target );

   // cout << "Concrete target: " << projected_concrete_target << " -> " << abstraction[projected_concrete_target] << endl;
    concrete_target.set(ts_index_predecessor, projected_concrete_target);
}




    bool TauShrinking::has_tau_path(int label, const PlanState & concrete_source,
                                    const PlanState & abstract_target) const {
        int source = concrete_source[ts_index_predecessor];

        assert(source >= 0);
        int projected_abstract_target = (ts_index_successor >= 0 ?  abstract_target[ts_index_predecessor] : -1);


        int num_states = transition_system->get_size();
    
        // Find the right target

        assert(abstraction.size() == (size_t)num_states);
        std::vector<bool> targets(num_states, false);
        for (int s = 0; s < num_states; ++s) {
            targets[s] = is_target(s, label, projected_abstract_target);
        }

        return tau_graph->path_exists(source, targets);
    }


    
bool TauShrinking::has_tau_path(const PlanState & concrete_source,
                                const PlanState & abstract_target) const {
    int source = concrete_source[ts_index_predecessor];

    assert(source >= 0);
    int projected_abstract_target = (ts_index_successor >= 0 ?  abstract_target[ts_index_predecessor] : -1);


    int num_states = transition_system->get_size();
    
    // Find the right target

    assert(abstraction.size() == (size_t)num_states);
    std::vector<bool> targets(num_states, false);
    for (int s = 0; s < num_states; ++s) {
        targets[s] = (abstraction[s] == projected_abstract_target);
    }

    return tau_graph->path_exists(source, targets);
}

void TauShrinking::reconstruct_goal_step(std::vector<int> & new_label_path,
                                         std::vector<PlanState> & new_traversed_states) const {
    // cout << "Reconstructing goal: " << transition_system->get_incorporated_variables()[0] << endl;
    assert(!new_traversed_states.empty());
    assert (ts_index_predecessor >= 0);
    assert(ts_index_predecessor < (int)(new_traversed_states.back().size()));
    int source = new_traversed_states.back()[ts_index_predecessor];
    reconstruct_step (source, transition_system->get_is_goal(), new_label_path, new_traversed_states);

}

void PlanReconstructionTauPath::reconstruct_plan(Plan &plan) const {
    const std::vector<int> & label_path = plan.get_labels ();
    const std::vector<PlanState> & traversed_states = plan.get_traversed_states ();
    cout << "TauPath reconstruction of plan with " << label_path.size() << " steps" << endl;

    //cout << "Input path " << traversed_states[0];
    //for(size_t step = 0; step < label_path.size(); ++step) {
    //    cout << "\n    --" << label_path[step] << "--> " << traversed_states[step+1];
   // }
    //cout << endl;


    //cout << " Transition system mapping" << endl;
    //for (size_t i = 0; i < transition_system_mapping.size(); ++i) {
    //    cout << "    " << i << " -> " << transition_system_mapping[i] << endl;
    //}
    
    assert(label_path.size() + 1 == traversed_states.size());

    std::vector<int> new_label_path;
    std::vector<PlanState> new_traversed_states;

    //cout << "Initial state: " << initial_state << endl;
    //cout << initial_state.size() << endl;
    //cout << PlanState(traversed_states[0], transition_system_mapping) << endl;
    //assert(initial_state.compatible(PlanState(traversed_states[0], transition_system_mapping)));
    new_traversed_states.push_back(initial_state);
    assert(initial_state.is_complete());

    for(size_t step = 0; step < label_path.size(); ++step) {
      //  cout << "Step: " << step << endl;
       
        int label = label_path[step];

        
        assert(step + 1 < traversed_states.size());
        PlanState abstract_target = PlanState(traversed_states[step+1], transition_system_mapping);
        PlanState concrete_target = PlanState(traversed_states[step+1], transition_system_mapping);

        // Skip labels with an effect on a single transition system if there is a tau path
        // to the target
        assert (label_only_relevant_for.empty() || (size_t)label < label_only_relevant_for.size());


        if (!label_only_relevant_for.empty() && label_only_relevant_for[label] &&
            !label_only_relevant_for[label]->has_tau_path(label, new_traversed_states.back(),
                                                          concrete_target) ){
          /*  cout << "Ignoring transition " << label
                 << " because it is only relevant for "
                 << label_only_relevant_for[label]->get_ts_index_predecessor()  << " " << label_only_relevant_for[label]->get_ts_index_successor() << endl;*/
            continue;
        }
 	//       cout << "Not ignoring transition " << label << "(" << (bool)(label_only_relevant_for[label]) << ")"  << endl;
        for (const auto & tau_shrinking : tau_transformations) {
            tau_shrinking->reconstruct_step(label, abstract_target, concrete_target, new_label_path, new_traversed_states);
        }

        //cout << " ----> " << concrete_target << endl;
        assert(concrete_target.is_complete());
        new_label_path.push_back(label);
        new_traversed_states.push_back(concrete_target);
    }

    for (const auto & tau_shrinking : tau_transformations) {
        tau_shrinking->reconstruct_goal_step(new_label_path, new_traversed_states);
    }

    //cout << "Tau path " << new_traversed_states[0];
    //for(size_t step = 0; step < new_label_path.size(); ++step) {
    //    cout << "\n    --" << new_label_path[step] << "--> " << new_traversed_states[step+1];
    //}
    cout << endl;

    plan.set_plan(move(new_traversed_states), move(new_label_path));
}


    void PlanReconstructionTauPath::print(std::ostream& o) const  {
        o << "Tau";
    }

}
