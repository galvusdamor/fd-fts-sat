#include "plan_reconstruction_merge_and_shrink.h"

#include "label_map.h"
#include "merge_and_shrink_representation.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/sas_task.h"
#include "../task_representation/search_task.h"
#include "../operator_id.h"
#include "../utils/system.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include "../globals.h"
using namespace std;

namespace task_transformation {
PlanReconstructionMergeAndShrink::PlanReconstructionMergeAndShrink(
    const shared_ptr<task_representation::FTSTask> &predecessor_task,
    vector<unique_ptr<MergeAndShrinkRepresentation>> &&merge_and_shrink_representations,
    shared_ptr<LabelMap> label_map,
    vector<int> factor_of_representation)
    : predecessor_task(predecessor_task),
      state_registry(predecessor_task->get_search_task()),
      search_space(state_registry,OperatorCost::NORMAL), 
      merge_and_shrink_representations(move(merge_and_shrink_representations)),
      label_map(move(label_map)),
      factor_of_representation(move(factor_of_representation)) {

    //predecessor_task->dump();
    // Diagnostics: FTS_DUMP_LABEL_MAP=<file> appends, per reconstruction step, the map from
    // the predecessor task's labels to this step's labels, and (for the atomic task, whose
    // labels are the SAS operators) the operator names, so that transformed labels can be named.
    if (const char *dump = getenv("FTS_DUMP_LABEL_MAP")) {
        ofstream out(dump, ios::app);
        const int n = predecessor_task->get_num_labels();
        out << "; step predecessor_labels " << n << endl;
        for (int l = 0; l < n; l++) out << l << " " << this->label_map->get_reduced_label(l) << endl;
        const auto sas = g_sas_task();
        if (sas && sas->get_num_operators() == n) {
            out << "; names" << endl;
            for (int l = 0; l < n; l++) out << l << " " << sas->get_operators()[l].get_name() << endl;
        }
    }
}

    bool PlanReconstructionMergeAndShrink::match_states(const GlobalState & original_state,
                                                 const PlanState & abstract_state) const {
        for(size_t i = 0; i < merge_and_shrink_representations.size(); ++i) {
            int value = merge_and_shrink_representations[i]->get_value(original_state);
            if (value != abstract_state[i])  {
                return false;
            }
        }
        return true;
    }

    
    bool PlanReconstructionMergeAndShrink::match_labels(int original_label,
                                                        int abstract_label) const {
        return label_map->get_reduced_label(original_label) == abstract_label;
    }

    void PlanReconstructionMergeAndShrink::reconstruct_step(int label, const PlanState & target,
                                                            std::vector<int> & new_label_path,
                                                            std::vector<GlobalState> & new_traversed_states) const {

        const GlobalState & initial_state = new_traversed_states.back();
        auto search_task = predecessor_task->get_search_task();

        // cout << "Reconstructing step from "  << " to " << target << " with " << label << endl;
        // cout << g_sas_task()->get_operator_name(label) << endl;
        std::vector<OperatorID> applicable_ops; 
        search_task->generate_applicable_ops(initial_state, applicable_ops);
        bool found_match = false;
        for (OperatorID op : applicable_ops)  {
            LabelID original_label = search_task->get_label (op);
            if (match_labels(original_label, label)) {
                auto result_state = state_registry.get_successor_state(initial_state, op);
                if (match_states(result_state, target)) {
                    found_match = true;
                    //cout << g_sas_task()->get_operator_name(original_label) << endl;
                    new_label_path.push_back(original_label);
                    new_traversed_states.push_back(result_state);
                    break;
                }
            }
        }
        if (!found_match) {
            cerr << "Error: no match found in plan reconstruction" << endl;
            cerr<< "Trying to find a match with abstract label: " << label << endl;
            for(int i = 0; i < g_sas_task()->get_num_operators(); ++i) {
                if (label_map->get_reduced_label(i) == label) {
                    cerr <<  g_sas_task()->get_operator_name(i) << endl;
                }
            }
            cerr << "Available original labels:" << endl;
            for (OperatorID op : applicable_ops)  {
                LabelID original_label = search_task->get_label (op);
                cerr<< "  " << g_sas_task()->get_operator_name(original_label)
                     << " (" << original_label << ") reduced to "
                    << label_map->get_reduced_label(original_label) << endl;

                
                if (label == label_map->get_reduced_label(original_label)) {
                    auto result_state = state_registry.get_successor_state(initial_state, op);
                
                    for(size_t i = 0; i < merge_and_shrink_representations.size(); ++i) {
                        int value = merge_and_shrink_representations[i]->get_value(result_state);
                        if (value != target[i])  {
                            cerr << "      state does not match for TS " << i << ": " << value << " " << target[i] << endl;
                        }
                    }
                }
            }
            utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
        }

    }

void PlanReconstructionMergeAndShrink::reconstruct_plan(Plan & plan) const {


    const std::vector<int> & label_path = plan.get_labels ();
    const std::vector<PlanState> & traversed_states = plan.get_traversed_states ();
    cout << "M&S reconstruction of plan with " << label_path.size() << " steps for task with " << predecessor_task->get_size() << " variables"   << endl;
    

    // cout << traversed_states[0];
    //  for(size_t step = 0; step < label_path.size(); ++step) {
    //     cout << " --" << label_path[step] << "--> " << traversed_states[step+1];
    // }
    // cout << endl;

// cout << *label_map << endl;
    assert(label_path.size() + 1 == traversed_states.size());
    
    std::vector<int> new_label_path;
    std::vector<GlobalState> new_traversed_states;

    new_traversed_states.push_back(state_registry.get_initial_state());

    for(size_t step = 0; step < label_path.size(); ++step) {
        // cout << "Step: " << step << endl;
        int label = label_path[step];
        assert(step + 1 < traversed_states.size());
        const PlanState & target = traversed_states[step+1];
        reconstruct_step(label, target, new_label_path, new_traversed_states);
    }

    
    std::vector<PlanState> plan_states;
    plan_states.reserve(new_traversed_states.size());
    for (const auto & s : new_traversed_states) {
        plan_states.push_back(s);
    }

    //cout << "MS reconstructed path " << plan_states[0];
  //  for(size_t step = 0; step < new_label_path.size(); ++step) {
   //     cout << " --" << new_label_path[step] << "--> " << plan_states[step+1];
   // }
   // cout << endl;

    plan.set_plan(move(plan_states), move(new_label_path));
}


    void PlanReconstructionMergeAndShrink::print(std::ostream& o) const  {
        o << "MS";
    }


}
