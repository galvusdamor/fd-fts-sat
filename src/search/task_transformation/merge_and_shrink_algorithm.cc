#include "merge_and_shrink_algorithm.h"

#include "distances.h"
#include "factored_transition_system.h"
#include "fts_factory.h"
#include "label_map.h"
#include "label_reduction.h"
#include "merge_and_shrink_representation.h"
#include "merge_strategy.h"
#include "merge_strategy_factory.h"
#include "shrink_strategy.h"
#include "types.h"
#include "utils.h"
#include "plan_reconstruction.h"

#include "../options/option_parser.h"
#include "../options/options.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"

#include "../dominance/numeric_dominance_fts_pruning.h"

#include "../utils/markup.h"
#include "../utils/math.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using options::Bounds;
using options::OptionParser;
using options::Options;
using utils::ExitCode;

namespace task_transformation {
static void print_time(const utils::Timer &timer, string text) {
    cout << "t=" << timer << " (" << text << ")" << endl;
}

MergeAndShrinkAlgorithm::MergeAndShrinkAlgorithm(const Options &opts) :
        merge_strategy_factory(
        opts.get<shared_ptr<MergeStrategyFactory>>("merge_strategy", nullptr)),
        shrink_strategy(opts.get<shared_ptr<ShrinkStrategy>>("shrink_strategy", nullptr)),
        shrink_atomic_fts(opts.get<bool>("shrink_atomic_fts")),
        run_atomic_loop(opts.get<bool>("run_atomic_loop")),
        run_final_lr_shrink(opts.get<bool>("run_final_lr_shrink")),
        reduce_labels_final_fts(opts.get<bool>("reduce_labels_final_fts")),
        shrink_final_fts(opts.get<bool>("shrink_final_fts")),
        run_final_loop(opts.get<bool>("run_final_loop")),
        num_states_to_trigger_shrinking(opts.get<int>("num_states_to_trigger_shrinking")),
        max_states(opts.get<int>("max_states")),
        label_reduction(opts.get<shared_ptr<LabelReduction>>("label_reduction", nullptr)),
        prune_unreachable_states(opts.get<bool>("prune_unreachable_states")),
        prune_irrelevant_states(opts.get<bool>("prune_irrelevant_states")),
        prune_transitions_from_goal(opts.get<bool>("prune_transitions_from_goal")),
        fts_transition_pruning(opts.get<shared_ptr<dominance::FTSTransitionPruning>>("fts_transition_pruning", nullptr)),
        verbosity(static_cast<Verbosity>(opts.get_enum("verbosity"))),
        run_main_loop(opts.get<bool>("run_main_loop")),
        max_time(opts.get<double>("max_time")),
        num_transitions_to_abort(opts.get<int>("num_transitions_to_abort")),
        num_transitions_to_exclude(opts.get<int>("num_transitions_to_exclude")),
        cost_type(static_cast<OperatorCost>(opts.get_enum("cost_type"))),
        starting_peak_memory(0) {
    assert(num_states_to_trigger_shrinking > 0);
    assert(max_states > 0);
    if (run_main_loop && (!shrink_strategy || !merge_strategy_factory)) {
        cerr << "Running the main loop requires a shrink and a merge strategy" << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
}

void MergeAndShrinkAlgorithm::report_peak_memory_delta(bool final) const {
    if (final)
        cout << "Final";
    else
        cout << "Current";
    cout << " peak memory increase of merge-and-shrink algorithm: "
         << utils::get_peak_memory_in_kb() - starting_peak_memory << " KB"
         << endl;
}

void MergeAndShrinkAlgorithm::dump_options() const {
    if (merge_strategy_factory) { // deleted after merge strategy extraction
        merge_strategy_factory->dump_options();
        cout << endl;
    } else {
        cout << "Merging disabled" << endl;
    }

    cout << "Options related to size limits and shrinking: " << endl;
    cout << "Number of states to trigger shrinking: "
         << num_states_to_trigger_shrinking << endl;
    cout << "Maximum number of states, otherwise merge is forbidden: "
         << max_states << endl;
    cout << endl;

    if (shrink_strategy) {
        shrink_strategy->dump_options();
    } else {
        cout << "Shrinking disabled" << endl;
    }
    cout << endl;

    if (label_reduction) {
        label_reduction->dump_options();
    } else {
        cout << "Label reduction disabled" << endl;
    }
    cout << endl;

    cout << "Verbosity: ";
    switch (verbosity) {
    case Verbosity::SILENT:
        cout << "silent";
        break;
    case Verbosity::NORMAL:
        cout << "normal";
        break;
    case Verbosity::VERBOSE:
        cout << "verbose";
        break;
    }
    cout << endl;
}

void MergeAndShrinkAlgorithm::warn_on_unusual_options() const {
    string dashes(79, '=');
    if (!label_reduction) {
        cout << dashes << endl
             << "WARNING! You did not enable label reduction.\nThis may "
            "drastically reduce the performance of merge-and-shrink!"
             << endl << dashes << endl;
    } else if (label_reduction->reduce_before_merging() && label_reduction->reduce_before_shrinking()) {
        cout << dashes << endl
             << "WARNING! You set label reduction to be applied twice in each merge-and-shrink\n"
            "iteration, both before shrinking and merging. This double computation effort\n"
            "does not pay off for most configurations!"
             << endl << dashes << endl;
    } else {
        if (!shrink_strategy) {
            return;
        }
        if (label_reduction->reduce_before_shrinking() &&
            (shrink_strategy->get_name() == "f-preserving"
             || shrink_strategy->get_name() == "random")) {
            cout << dashes << endl
                 << "WARNING! Bucket-based shrink strategies such as f-preserving random perform\n"
                "best if used with label reduction before merging, not before shrinking!"
                 << endl << dashes << endl;
        }
        if (label_reduction->reduce_before_merging() &&
            shrink_strategy->get_name() == "bisimulation") {
            cout << dashes << endl
                 << "WARNING! Shrinking based on bisimulation performs best if used with label\n"
                "reduction before shrinking, not before merging!"
                 << endl << dashes << endl;
        }
    }

   if (!prune_unreachable_states || !prune_irrelevant_states) {
       cout << dashes << endl
            << "WARNING! Pruning is (partially) turned off!\nThis may "
           "drastically reduce the performance of merge-and-shrink!"
            << endl << dashes << endl;
   }

   if (prune_transitions_from_goal) {
       cout << "Prune transitions from goal activated.\n";
   }
}

bool MergeAndShrinkAlgorithm::ran_out_of_time(
    const utils::Timer &timer) const {
    if (timer() > max_time) {
        if (verbosity >= Verbosity::NORMAL) {
            cout << "Ran out of time, stopping computation." << endl;
            cout << endl;
        }
        return true;
    }
    return false;
}

bool MergeAndShrinkAlgorithm::too_many_transitions(const FactoredTransitionSystem &fts, int index) const {
    int num_transitions = fts.get_ts(index).compute_total_transitions();
    if (num_transitions > num_transitions_to_abort) {
        if (verbosity >= Verbosity::NORMAL) {
            cout << "Factor has too many transitions, stopping computation."
                 << endl;
            cout << endl;
        }
        return true;
    }
    return false;
}

bool MergeAndShrinkAlgorithm::too_many_transitions(const FactoredTransitionSystem &fts) const {
    for (int index = 0; index < fts.get_size(); ++index) {
        if (fts.is_active(index)) {
            if (too_many_transitions(fts, index)) {
                return true;
            }
        }
    }
    return false;
}

bool MergeAndShrinkAlgorithm::exclude_if_too_many_transitions() const {
    return num_transitions_to_exclude != INF;
}


bool MergeAndShrinkAlgorithm::check_dead_labels(FactoredTransitionSystem &fts,
                                                const vector<int> & ts_to_check_initially) const {
    set<LabelID> dead_labels;

    for (int index : ts_to_check_initially) {
        if (fts.is_active(index)) {
            fts.get_ts(index).check_dead_labels(dead_labels);
        }
    }

    return remove_dead_labels(fts, dead_labels);
}


bool MergeAndShrinkAlgorithm::check_dead_labels(FactoredTransitionSystem &fts,
                                                int index) const {
    set<LabelID> dead_labels;

    assert (fts.is_active(index));
    fts.get_ts(index).check_dead_labels(dead_labels);

    return remove_dead_labels(fts, dead_labels);
}


bool MergeAndShrinkAlgorithm::remove_dead_labels(FactoredTransitionSystem &fts,
                                                set<LabelID> & dead_labels) const {

    while(!dead_labels.empty()) {
        cout << "Eliminating " << dead_labels.size() << " dead labels.\n";
        vector<int> ts_to_check = fts.remove_labels(vector<LabelID>(dead_labels.begin(), dead_labels.end()));

        dead_labels.clear();
        for (int index : ts_to_check) {
            fts.recompute_distances(index);
            if (prune_unreachable_states || prune_irrelevant_states) {
                bool pruned_factor = prune_step(fts, index,
                                                prune_unreachable_states,
                                                prune_irrelevant_states,
                                                verbosity);

                if (pruned_factor) {
                    fts.get_ts(index).check_dead_labels(dead_labels);
                }
            }
            if (!fts.is_factor_solvable(index)) {
                return true;
            }
        }
    }
    return false;
}

bool MergeAndShrinkAlgorithm::prune_fts(FactoredTransitionSystem &fts, const utils::Timer &timer, bool prune_transitions = true) const {
   /*
     Prune all factors according to the chosen options. Stop early if one factor is unsolvable. Return true iff unsolvable.
   */
   bool pruned = false;

   // Pruning
    vector<int> check_dead_labels_in;

   if (prune_transitions_from_goal) {
       optional<int> pruned_factor = fts.remove_transitions_from_goal();
       if (pruned_factor.has_value()) {
           if (verbosity >= Verbosity::NORMAL) {
               cout << "Removed transitions from goal in factor "
                    << pruned_factor.value() << endl;
           }
            pruned = true;
           check_dead_labels_in.push_back(pruned_factor.value());
       }
   }

    for (int index = 0; index < fts.get_size(); ++index) {
        if (!fts.is_active(index)) {
            continue;
        }
        if (prune_unreachable_states || prune_irrelevant_states) {
            bool pruned_factor = prune_step(
               fts,
               index,
               prune_unreachable_states,
               prune_irrelevant_states,
               verbosity);

            if (pruned_factor) {
                check_dead_labels_in.push_back(index);
                pruned = true;
            }
        }

        if (!fts.is_factor_solvable(index)) {
            return true;
        }
    }


    if (pruned) {
        if(check_dead_labels(fts, check_dead_labels_in)) {
            return true;
        }

        fts.remove_irrelevant_transition_systems(verbosity);
    }

    if (prune_transitions && fts_transition_pruning) {
        // TODO: Here we should only prune affected transition systems, and only need to recompute the distances of them aswell.
        // however, the returned value is currently not correct due to fts remove_labels not returning the correct tss
        vector<int> affected_ts = fts_transition_pruning->prune_transitions(fts);
        for (int ts_id = 0; ts_id < fts.get_size(); ts_id++) {
            fts.recompute_distances(ts_id);
        }

        prune_fts(fts, timer, false);
    }

    if (verbosity >= Verbosity::NORMAL && pruned) {
       print_time(timer, "after pruning atomic factors");
   }
   return false;
}

void MergeAndShrinkAlgorithm::main_loop(
    FactoredTransitionSystem &fts,
    const FTSTask &fts_task,
    const utils::Timer &timer) {
    cout << endl << "Running main loop..." << endl;
    int maximum_intermediate_size = 0;
    for (int i = 0; i < fts.get_size(); ++i) {
        if (fts.is_active(i)) {
        int size = fts.get_ts(i).get_size();
        if (size > maximum_intermediate_size) {
            maximum_intermediate_size = size;
        }
    }
    }

    unique_ptr<MergeStrategy> merge_strategy =
        merge_strategy_factory->compute_merge_strategy(fts_task, fts);
    merge_strategy_factory = nullptr;

    int iteration_counter = 0;
    set<int> allowed_indices;
    while (fts.get_num_active_entries() > 1) {
        // Choose next transition systems to merge
        vector<int> vec_allowed_indices;;
        if (exclude_if_too_many_transitions()) {
            vec_allowed_indices = vector<int>(
                allowed_indices.begin(), allowed_indices.end());
        }
        pair<int, int> merge_indices = merge_strategy->get_next(vec_allowed_indices);
        if (ran_out_of_time(timer)) {
            break;
        }
        int merge_index1 = merge_indices.first;
        int merge_index2 = merge_indices.second;
        if (merge_index1 == -1 && merge_index2 == -1) {
            cout << "Stopping main loop" << endl;
            break;
        }
        assert(merge_index1 != merge_index2);
        if (verbosity >= Verbosity::NORMAL) {
            cout << "Next pair of indices: ("
                 << merge_index1 << ", " << merge_index2 << ")" << endl;
            if (verbosity >= Verbosity::VERBOSE) {
                fts.statistics(merge_index1);
                fts.statistics(merge_index2);
            }
            print_time(timer, "after computation of next merge");
        }

        if (ran_out_of_time(timer)) {
            break;
        }

        // TODO: forbid too large merges!
//        int merged_size = fts.get_ts(merge_index1).get_size() * fts.get_ts(merge_index2).get_size();
//        if (merged_size > max_states) {
//            if (verbosity >= Verbosity::NORMAL) {
//                cout << "Product would be too large, skipping merge" << endl;
//                continue;
//            }
//        }

        // Label reduction (before merging)
        if (label_reduction && label_reduction->reduce_before_merging()) {
            bool reduced = label_reduction->reduce(merge_indices, fts, verbosity);
            if (verbosity >= Verbosity::NORMAL && reduced) {
                print_time(timer, "after label reduction");
            }
        }

        if (ran_out_of_time(timer)) {
            break;
        }

        // Merging
        int merged_index = fts.merge(merge_index1, merge_index2, verbosity);
        int abs_size = fts.get_ts(merged_index).get_size();
        if (abs_size > maximum_intermediate_size) {
            maximum_intermediate_size = abs_size;
        }

        if (verbosity >= Verbosity::NORMAL) {
            if (verbosity >= Verbosity::VERBOSE) {
                fts.statistics(merged_index);
            }
            print_time(timer, "after merging");
        }

        // We do not check for num transitions here but only after shrinking
        // to allow recovering a too large product.
        if (ran_out_of_time(timer)) {
            break;
        }

        // Pruning
        if (prune_transitions_from_goal) {
            auto pruned_factor = fts.remove_transitions_from_goal();
            if (pruned_factor.has_value()) {
                if (verbosity >= Verbosity::NORMAL) {
                    cout << "Removed transitions from goal in factor "
                         << pruned_factor.value() << endl;
                }
                if (check_dead_labels(fts, pruned_factor.value())) {
                    if (verbosity >= Verbosity::NORMAL) {
                        cout << "Abstract problem is unsolvable, exiting" << endl;
                        utils::exit_with(ExitCode::UNSOLVED_INCOMPLETE);
                    }
                }
            }
        }

       if (prune_unreachable_states || prune_irrelevant_states) {
           bool pruned = prune_step(
               fts,
               merged_index,
               prune_unreachable_states,
               prune_irrelevant_states,
               verbosity);

           if (pruned) {
               if (check_dead_labels(fts, merged_index)) {
                   if (verbosity >= Verbosity::NORMAL) {
                       cout << "Abstract problem is unsolvable, exiting" << endl;
                       utils::exit_with(ExitCode::UNSOLVED_INCOMPLETE);
                   }
               }
           }
           if (verbosity >= Verbosity::NORMAL && pruned) {
               if (verbosity >= Verbosity::VERBOSE) {
                   fts.statistics(merged_index);
               }
               print_time(timer, "after pruning");
           }
       }

        /*
          NOTE: both the shrink strategy classes and the construction
          of the composite transition system require the input
          transition systems to be non-empty, i.e. the initial state
          not to be pruned/not to be evaluated as infinity.
        */
        if (!fts.is_factor_solvable(merged_index)) {
            if (verbosity >= Verbosity::NORMAL) {
                cout << "Abstract problem is unsolvable, exiting" << endl;
                utils::exit_with(ExitCode::UNSOLVED_INCOMPLETE);
            }
            break;
        }

        fts.remove_irrelevant_transition_systems(verbosity);

        if (!fts.is_active(merged_index)) {
            // The just merged transition system was irrelevant and removed.
            continue;
        }

        fts.remove_irrelevant_labels();

        if (ran_out_of_time(timer)) {
            break;
        }

        // Label reduction (before shrinking)
        if (label_reduction && label_reduction->reduce_before_shrinking()) {
            bool reduced = label_reduction->reduce(merge_indices, fts, verbosity);
            if (verbosity >= Verbosity::NORMAL && reduced) {
                print_time(timer, "after label reduction");
            }
        }

        if (ran_out_of_time(timer)) {
            break;
        }


        // Shrinking
        bool shrunk = shrink_strategy->
            apply_shrinking_transformation(fts, verbosity, merged_index);

        // bool shrunk = shrink_factor( fts, merged_index, *shrink_strategy, verbosity,
        //     num_states_to_trigger_shrinking);
        if (verbosity >= Verbosity::NORMAL && shrunk) {
            print_time(timer, "after shrinking");
        }

        // if (exclude_if_too_many_transitions()) {
        //     allowed_indices.erase(merge_index1);
        //     allowed_indices.erase(merge_index2);
        //     int num_trans = fts.get_ts(merged_index).compute_total_transitions();
        //     if (num_trans <= num_transitions_to_exclude) {
        //         allowed_indices.insert(merged_index);
        //     } else {
        //         if (verbosity >= Verbosity::NORMAL) {
        //             cout << fts.get_ts(merged_index).tag()
        //                  << "too many number of transitions, excluding "
        //                     "from further consideration." << endl;
        //         }
        //     }
        //     if (allowed_indices.size() <= 1) {
        //         if (verbosity >= Verbosity::NORMAL) {
        //             cout << "Not enough factors remaining with a low enough "
        //                     "number of transitions, stopping computation."
        //                  << endl;
        //             cout << endl;
        //         }
        //         break;
        //     }
        // }

        // assert (merged_index < fts.get_size());
        // if (ran_out_of_time(timer) || too_many_transitions(fts, merged_index)) {
        //     break;
        // }

        // End-of-iteration output.
        if (verbosity >= Verbosity::VERBOSE) {
            report_peak_memory_delta();
        }
        if (verbosity >= Verbosity::NORMAL) {
            cout << endl;
        }

        // if (fts.get_ts(merged_index).get_size() > max_states) {
        //     cout << "Merged factor is too large even after shrinking, "
        //             "stopping the merge-and-shrink algorithm." << endl;
        //     break;
        // }

        ++iteration_counter;
    }

    cout << "End of merge-and-shrink algorithm main loop, statistics:" << endl;
    cout << "Maximum intermediate abstraction size: "
         << maximum_intermediate_size << endl;
}


void MergeAndShrinkAlgorithm::
apply_full_label_reduction_and_shrinking(FactoredTransitionSystem &fts,
                                         bool apply_label_reduction, bool apply_shrink,
                                         bool run_loop, const utils::Timer &timer,
                                         Verbosity verbosity) {
    cout << "Run label reduction and shrinking loop" << endl;
    bool has_simplified;
    do {
        if (fts.get_size() == 0) {
            return;
        }

        has_simplified = false;
        // Label reduction of atomic FTS.
        if (label_reduction && apply_label_reduction) {
            bool reduced = label_reduction->reduce(pair<int, int>(-1, -1), fts, verbosity);
            if (verbosity >= Verbosity::NORMAL && reduced) {
                print_time(timer, "after label reduction of atomic FTS");
            }
        }

        if (ran_out_of_time(timer)) {
            return;
        }


        if (shrink_strategy && apply_shrink) {
            shrink_strategy->apply_shrinking_transformation(fts, verbosity);

            if (verbosity >= Verbosity::NORMAL) {
                print_time(timer, "after shrinking of atomic FTS");
            }
        }


        has_simplified |= fts.remove_irrelevant_transition_systems(verbosity);

        has_simplified |= fts.remove_irrelevant_labels();

        if (ran_out_of_time(timer)) {
            return;
        }
    } while (run_loop && has_simplified);
}

FactoredTransitionSystem MergeAndShrinkAlgorithm::build_factored_transition_system(
    const std::shared_ptr<task_representation::FTSTask> &fts_task, bool lossy_mapping) {
    if (starting_peak_memory) {
        cerr << "Calling build_factored_transition_system twice is not "
             << "supported!" << endl;
        utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
    }

    // fts_task->dump();
    starting_peak_memory = utils::get_peak_memory_in_kb();

    if (label_reduction) {
        label_reduction->initialize(*fts_task);
    }

    utils::Timer timer;
    cout << "Running merge-and-shrink algorithm..." << endl;
//    task_properties::verify_no_axioms(task_proxy);
    dump_options();
    warn_on_unusual_options();
    cout << endl;

    std::unique_ptr<Labels> labels = utils::make_unique_ptr<Labels>(fts_task->get_labels(),
                                                                    cost_type);
    int num_vars = fts_task->get_size();
    assert(num_vars);
    std::vector<std::unique_ptr<TransitionSystem>> transition_systems;
    transition_systems.reserve(num_vars * 2 - 1);
    for (int index = 0; index < num_vars; ++index) {
        transition_systems.push_back(
            utils::make_unique_ptr<TransitionSystem>(fts_task->get_ts(index), *labels));
    }

    std::vector<std::unique_ptr<MergeAndShrinkRepresentation>> mas_representations =
        create_mas_representations(transition_systems);
    std::vector<std::unique_ptr<Distances>> distances =
        create_distances(transition_systems);

    bool compute_init_distances = prune_unreachable_states;
    if ((shrink_strategy && shrink_strategy->requires_init_distances()) ||
            (merge_strategy_factory && merge_strategy_factory->requires_init_distances())) {
        compute_init_distances = true;
    }
    bool compute_goal_distances = prune_irrelevant_states;
    if ((shrink_strategy && shrink_strategy->requires_goal_distances()) ||
            (merge_strategy_factory && merge_strategy_factory->requires_goal_distances())) {
        compute_goal_distances = true;
    }

    FactoredTransitionSystem fts(fts_task,
        move(labels),
        move(transition_systems),
        move(mas_representations),
        move(distances),
        compute_init_distances,
        compute_goal_distances,
        verbosity,
        lossy_mapping);

    bool unsolvable = prune_fts(fts, timer);
    if (unsolvable) {
            cout << "Atomic FTS is unsolvable, exiting" << endl;
            utils::exit_with(ExitCode::UNSOLVED_INCOMPLETE);
    }


    apply_full_label_reduction_and_shrinking(fts,
                                             label_reduction->reduce_atomic_fts(),
                                             shrink_atomic_fts, run_atomic_loop, timer,
                                             verbosity) ;

    cout << "Merge-and-shrink atomic construction runtime: " << timer << endl;

    if (run_main_loop) {
        assert(shrink_strategy && merge_strategy_factory);
        main_loop(fts, *fts_task, timer);

        if (run_final_lr_shrink) {
            apply_full_label_reduction_and_shrinking(fts,
                                                     reduce_labels_final_fts,
                                                     shrink_final_fts, run_final_loop, timer,
                                                     verbosity) ;
        }
    }


    const bool final = true;
    report_peak_memory_delta(final);
    shrink_strategy = nullptr;
    label_reduction = nullptr;

    cout << "Merge-and-shrink algorithm runtime: " << timer << endl;
    cout << endl;
    return fts;
}

void add_merge_and_shrink_algorithm_options_to_parser(OptionParser &parser) {
    add_cost_type_option_to_parser(parser);

    // Merge strategy option.
    parser.add_option<shared_ptr<MergeStrategyFactory>>(
        "merge_strategy",
        "See detailed documentation for merge strategies. "
        "We currently recommend SCC-DFP, which can be achieved using "
        "{{{merge_strategy=merge_sccs(order_of_sccs=topological,merge_selector="
        "score_based_filtering(scoring_functions=[goal_relevance,dfp,total_order"
        "]))}}}",
        OptionParser::NONE);

    // Shrink strategy option.
    parser.add_option<shared_ptr<ShrinkStrategy>>(
        "shrink_strategy",
        "See detailed documentation for shrink strategies. "
        "We currently recommend non-greedy shrink_bisimulation, which can be "
        "achieved using {{{shrink_strategy=shrink_bisimulation(greedy=false)}}}",
        OptionParser::NONE);
    parser.add_option<bool>(
        "shrink_atomic_fts",
        "Shrink the atomic factored transition system.",
        "false");
    parser.add_option<int>(
        "num_states_to_trigger_shrinking",
        "Number of states to trigger shrinking.",
        "1");
    parser.add_option<int>(
        "max_states",
        "Number of states that is not allowed to be surpassed by merging. "
        "If a merge would surpuass it, the merge is forbidden from that "
        "point on.",
        "infinity");

    // Label reduction option.
    parser.add_option<shared_ptr<LabelReduction>>(
        "label_reduction",
        "See detailed documentation for labels. There is currently only "
        "one 'option' to use label_reduction, which is {{{label_reduction=exact}}} "
        "Also note the interaction with shrink strategies.",
        OptionParser::NONE);

    // Pruning options.
   parser.add_option<bool>(
       "prune_unreachable_states",
       "If true, prune abstract states unreachable from the initial state.",
       "true");
   parser.add_option<bool>(
       "prune_irrelevant_states",
       "If true, prune abstract states from which no goal state can be "
       "reached.",
       "true");

   parser.add_option<bool>(
       "prune_transitions_from_goal",
       "If true, prune transitions that can only be applied in goal states",
       "true");

   parser.add_option<shared_ptr<dominance::FTSTransitionPruning>>(
           "fts_transition_pruning",
            "Pruning transitions of FTS.",
            OptionParser::NONE
           );

    vector<string> verbosity_levels;
    vector<string> verbosity_level_docs;
    verbosity_levels.push_back("silent");
    verbosity_level_docs.push_back(
        "silent: no output during construction, only starting and final "
        "statistics");
    verbosity_levels.push_back("normal");
    verbosity_level_docs.push_back(
        "normal: basic output during construction, starting and final "
        "statistics");
    verbosity_levels.push_back("verbose");
    verbosity_level_docs.push_back(
        "verbose: full output during construction, starting and final "
        "statistics");
    parser.add_enum_option(
        "verbosity",
        verbosity_levels,
        "Option to specify the level of verbosity.",
        "normal",
        verbosity_level_docs);

    parser.add_option<bool>(
        "run_main_loop",
        "Run the main loop of the algorithm.",
        "true");

    parser.add_option<bool>(
        "run_atomic_loop",
        "The atomic label reduction + shrinking loop on the atomic FTS runs up to a fixpoint.",
        "true");

    parser.add_option<bool>(
        "shrink_atomic_fts",
        "Use shrinking on the atomic FTS.",
        "true");

    parser.add_option<bool>(
        "reduce_labels_final_fts",
        "Use label reduction after the main loop (only if main loop is used).",
        "true");

    parser.add_option<bool>(
        "run_final_loop",
        "The final label reduction + shrinking loop is computed up to a fixpoint.",
        "true");

    parser.add_option<bool>(
        "run_final_lr_shrink",
        "Runs a label reduction + shrinking loop after the main loop (only if main loop is used).",
        "false");

    parser.add_option<bool>(
        "shrink_final_fts",
        "Use shrinking on the final FTS after the main loop (only if main loop is used).",
        "true");

    parser.add_option<double>(
        "max_time",
        "A limit in seconds on the computation time of the algorithm.",
        "infinity",
        Bounds("0.0", "infinity"));
    parser.add_option<int>(
        "num_transitions_to_abort",
        "A limit on the number of transitions of any factor during the "
        "computation. Once this limit is reached, the algorithm terminates, "
        "leaving the chosen partial_mas_method to compute a heuristic from the "
        "set of remaining factors.",
        "infinity",
        Bounds("0", "infinity"));
    parser.add_option<int>(
        "num_transitions_to_exclude",
        "A limit on the number of transitions of any factor during the "
        "computation. Once a factor reaches this limit, it is excluded from "
        "further considerations of the algorithm.",
        "infinity",
        Bounds("0", "infinity"));

}
void add_transition_system_size_limit_options_to_parser(OptionParser &parser) {
    parser.add_option<int>(
        "max_states",
        "maximum transition system size allowed at any time point.",
        "-1",
        Bounds("-1", "infinity"));
    parser.add_option<int>(
        "max_states_before_merge",
        "maximum transition system size allowed for two transition systems "
        "before being merged to form the synchronized product.",
        "-1",
        Bounds("-1", "infinity"));
    parser.add_option<int>(
        "threshold_before_merge",
        "If a transition system, before being merged, surpasses this soft "
        "transition system size limit, the shrink strategy is called to "
        "possibly shrink the transition system.",
        "-1",
        Bounds("-1", "infinity"));
}

void handle_shrink_limit_options_defaults(Options &opts) {
    int max_states = opts.get<int>("max_states");
    int max_states_before_merge = opts.get<int>("max_states_before_merge");
    int threshold = opts.get<int>("threshold_before_merge");

    // If none of the two state limits has been set: set default limit.
    if (max_states == -1 && max_states_before_merge == -1) {
        max_states = 50000;
    }

    // If exactly one of the max_states options has been set, set the other
    // so that it imposes no further limits.
    if (max_states_before_merge == -1) {
        max_states_before_merge = max_states;
    } else if (max_states == -1) {
        int n = max_states_before_merge;
        if (utils::is_product_within_limit(n, n, INF)) {
            max_states = n * n;
        } else {
            max_states = INF;
        }
    }

    if (max_states_before_merge > max_states) {
        cout << "warning: max_states_before_merge exceeds max_states, "
             << "correcting." << endl;
        max_states_before_merge = max_states;
    }

    if (max_states < 1) {
        cerr << "error: transition system size must be at least 1" << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }

    if (max_states_before_merge < 1) {
        cerr << "error: transition system size before merge must be at least 1"
             << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }

    if (threshold == -1) {
        threshold = max_states;
    }
    if (threshold < 1) {
        cerr << "error: threshold must be at least 1" << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }
    if (threshold > max_states) {
        cout << "warning: threshold exceeds max_states, correcting" << endl;
        threshold = max_states;
    }

    opts.set<int>("max_states", max_states);
    opts.set<int>("max_states_before_merge", max_states_before_merge);
    opts.set<int>("threshold_before_merge", threshold);
}
}
