#include "label_order_finder.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../utils/rng.h"
#include "../utils/system.h"
#include "../utils/rng_options.h"
#include "../option_parser.h"
#include "../plugin.h"

using namespace std;
namespace  label_order_finder {

    LabelOrderFinderRandom::LabelOrderFinderRandom(const options::Options &opts)
: rng(utils::parse_rng_from_options(opts)) {
    }

    LabelOrderFinderLinear::LabelOrderFinderLinear(const options::Options &) {
    }

    LabelOrderFinderReverse::LabelOrderFinderReverse(const options::Options &) {
    }

    LabelOrderFinderCausal::LabelOrderFinderCausal(const options::Options &) {
    }

    LabelOrderFinderRelaxed::LabelOrderFinderRelaxed(const options::Options &) {
    }
    std::vector<int> LabelOrderFinderLinear::find_order(const task_representation::FTSTask &fts_task) {
        vector<int> order;
        order.reserve(fts_task.get_num_labels());
        // for now just the natural ordering
        for(int l = 0; l < fts_task.get_num_labels(); l++) {
            order.push_back(l);
        }
        return order;
    }
    std::vector<int> LabelOrderFinderReverse::find_order(const task_representation::FTSTask &fts_task) {
        vector<int> order;
        order.reserve(fts_task.get_num_labels());
        // for now just the natural ordering
        for(int l = fts_task.get_num_labels() - 1; l >=0; --l) {
            order.push_back(l);
        }
        return order;
    }


    std::vector<int> LabelOrderFinderRelaxed::find_order(const task_representation::FTSTask &fts_task) {
        const int num_labels = fts_task.get_num_labels();
        const int num_factors = fts_task.get_size();

        // states reached so far, per factor; monotonically growing
        std::vector<std::vector<char>> reached(num_factors);
        for (int f = 0; f < num_factors; f++) {
            const task_representation::TransitionSystem &factor = fts_task.get_ts(f);
            reached[f].assign(factor.get_size(), 0);
            reached[f][factor.get_init_state()] = 1;
        }

        std::vector<char> placed(num_labels, 0);
        std::vector<char> applicable(num_labels, 0);   // once applicable, always applicable
        std::vector<int> order;
        order.reserve(num_labels);
        int layer = 0;

        while (true) {
            // which labels have become applicable with what is reached now
            std::vector<int> newly;
            for (int l = 0; l < num_labels; l++) {
                if (applicable[l]) continue;
                bool ok = true;
                for (int f = 0; f < num_factors && ok; f++) {
                    ok = false;
                    for (const task_representation::Transition &t :
                            fts_task.get_ts(f).get_transitions_with_label(l))
                        if (reached[f][t.src]) { ok = true; break; }
                }
                if (ok) { applicable[l] = 1; newly.push_back(l); }
            }
            for (int l : newly) { placed[l] = 1; order.push_back(l); }

            // apply every applicable label; a label applied earlier can reach
            // new targets once more of its sources have been reached
            bool grew = false;
            for (int l = 0; l < num_labels; l++) {
                if (!applicable[l]) continue;
                for (int f = 0; f < num_factors; f++)
                    for (const task_representation::Transition &t :
                            fts_task.get_ts(f).get_transitions_with_label(l))
                        if (reached[f][t.src] && !reached[f][t.target]) {
                            reached[f][t.target] = 1;
                            grew = true;
                        }
            }
            if (!newly.empty()) layer++;
            if (newly.empty() && !grew) break;
        }

        int unreachable = 0;
        for (int l = 0; l < num_labels; l++)
            if (!placed[l]) { order.push_back(l); unreachable++; }

        std::cout << "Relaxed-reachability label order: " << num_labels << " labels in "
                  << layer << " layers, " << unreachable
                  << " never applicable under the delete relaxation." << std::endl;
        return order;
    }

    std::vector<int> LabelOrderFinderCausal::find_order(const task_representation::FTSTask &fts_task) {
        const int num_labels = fts_task.get_num_labels();

        /*
          One group per (factor, state): the labels that move into that state
          and the labels that can be applied there. Self-loops count as
          neither. A label that does both is left out of the group's consumers,
          because "it must come before itself" is not a constraint we can or
          should honour.
        */
        std::vector<std::vector<int>> producers;   // per group
        std::vector<std::vector<int>> consumers;   // per group, minus producers
        std::vector<std::vector<int>> groups_produced_by(num_labels);

        for (int fac = 0; fac < fts_task.get_size(); fac++) {
            const task_representation::TransitionSystem &factor = fts_task.get_ts(fac);
            const int num_states = factor.get_size();
            std::vector<std::set<int>> into(num_states), outof(num_states);
            for (int label = 0; label < num_labels; label++) {
                for (const task_representation::Transition &t : factor.get_transitions_with_label(label)) {
                    if (t.src == t.target) continue;          // a self loop moves nowhere
                    into[t.target].insert(label);
                    outof[t.src].insert(label);
                }
            }
            for (int s = 0; s < num_states; s++) {
                if (into[s].empty() || outof[s].empty()) continue;   // nothing to order
                const int g = producers.size();
                producers.emplace_back(into[s].begin(), into[s].end());
                std::vector<int> cons;
                for (int l : outof[s])
                    if (!into[s].count(l)) cons.push_back(l);        // drop producer-consumers
                if (cons.empty()) { producers.pop_back(); continue; }
                consumers.push_back(std::move(cons));
                for (int p : producers[g]) groups_produced_by[p].push_back(g);
            }
        }

        // Kahn's algorithm: block_count[l] is how many groups still hold an
        // unplaced producer of a state l wants to consume.
        std::vector<int> unplaced_producers(producers.size());
        for (size_t g = 0; g < producers.size(); g++)
            unplaced_producers[g] = int(producers[g].size());
        std::vector<int> block_count(num_labels, 0);
        for (size_t g = 0; g < consumers.size(); g++)
            for (int c : consumers[g]) block_count[c]++;

        // ordered by (remaining unmet predecessors, label) so that both the
        // ready labels and the cheapest cycle break are the front element
        std::set<std::pair<int,int>> pending;
        for (int l = 0; l < num_labels; l++) pending.insert({block_count[l], l});

        std::vector<int> order;
        order.reserve(num_labels);
        std::vector<char> placed(num_labels, 0);
        long violations = 0, forced = 0;

        while (!pending.empty()) {
            const auto it = pending.begin();
            const int cost = it->first, label = it->second;
            pending.erase(it);
            if (cost > 0) { forced++; violations += cost; }   // no label was free: break the cycle
            placed[label] = 1;
            order.push_back(label);

            for (int g : groups_produced_by[label]) {
                if (--unplaced_producers[g] != 0) continue;
                for (int c : consumers[g]) {
                    if (placed[c]) continue;
                    auto old = pending.find({block_count[c], c});
                    if (old != pending.end()) pending.erase(old);
                    block_count[c]--;
                    pending.insert({block_count[c], c});
                }
            }
        }

        std::cout << "Causal label order: " << num_labels << " labels, "
                  << producers.size() << " (factor,state) constraint groups, "
                  << forced << " labels placed despite unmet predecessors, "
                  << violations << " constraints violated." << std::endl;
        return order;
    }

    std::vector<int> LabelOrderFinderRandom::find_order(const task_representation::FTSTask &fts_task) {
        vector<int> order;
        order.reserve(fts_task.get_num_labels());
        // for now just the natural ordering
        for(int l = 0; l < fts_task.get_num_labels(); l++) {
            order.push_back(l);
        }
        rng->shuffle(order);
        return order;
    }


    LabelOrderFinderTSort::LabelOrderFinderTSort(const options::Options &opts)
        : goal_first(opts.get<bool>("goal_first")) {
    }

    std::vector<int> LabelOrderFinderTSort::find_order(const task_representation::FTSTask &fts_task) {
        const int num_labels = fts_task.get_num_labels();
        const int num_factors = fts_task.get_size();
        // producers[f][s]: labels with a non-self-loop transition into s in f
        std::vector<std::vector<std::vector<int>>> producers(num_factors);
        std::vector<std::vector<int>> pre_factors(num_labels);
        for (int f = 0; f < num_factors; f++) {
            const task_representation::TransitionSystem &ts = fts_task.get_ts(f);
            producers[f].assign(ts.get_size(), {});
            for (int l = 0; l < num_labels; l++) {
                if (ts.has_precondition_on(task_representation::LabelID(l))) pre_factors[l].push_back(f);
                int last = -1;
                for (const auto &t : ts.get_transitions_with_label(l))
                    if (t.src != t.target && t.target != last) { producers[f][t.target].push_back(l); last = t.target; }
            }
            for (auto &v : producers[f]) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }
        }
        std::vector<int> roots;
        std::vector<char> is_root(num_labels, 0);
        if (goal_first) {
            for (int f = 0; f < num_factors; f++) {
                const task_representation::TransitionSystem &ts = fts_task.get_ts(f);
                if (!ts.is_goal_relevant()) continue;
                for (int l = 0; l < num_labels; l++)
                    for (const auto &t : ts.get_transitions_with_label(l))
                        if (!ts.is_goal_state(t.src) && ts.is_goal_state(t.target)) { is_root[l] = 1; break; }
            }
            for (int l = 0; l < num_labels; l++) if (is_root[l]) roots.push_back(l);
        }
        for (int l = 0; l < num_labels; l++) if (!is_root[l]) roots.push_back(l);

        // iterative DFS; a label's supporters are generated when it is entered
        std::vector<char> state(num_labels, 0);   // 0 new, 1 on stack, 2 done
        std::vector<int> order;
        order.reserve(num_labels);
        std::vector<int> stamp(num_labels, -1);
        for (int r : roots) {
            if (state[r]) continue;
            std::vector<std::pair<int, std::vector<int>>> stack;
            auto enter = [&](int l) {
                state[l] = 1;
                std::vector<int> sup;
                for (int f : pre_factors[l])
                    for (int s : fts_task.get_ts(f).get_label_precondition(task_representation::LabelID(l)))
                        for (int p : producers[f][s])
                            if (p != l && stamp[p] != l) { stamp[p] = l; sup.push_back(p); }
                std::reverse(sup.begin(), sup.end());   // pop from the back = ascending label order
                stack.push_back({l, std::move(sup)});
            };
            enter(r);
            while (!stack.empty()) {
                auto &top = stack.back();
                if (!top.second.empty()) {
                    const int p = top.second.back(); top.second.pop_back();
                    if (!state[p]) enter(p);        // back edges (state 1) are ignored
                } else {
                    state[top.first] = 2;
                    order.push_back(top.first);
                    stack.pop_back();
                }
            }
        }
        std::cout << "TSort label order: " << num_labels << " labels" << (goal_first ? ", goal-achieving labels as first roots" : "") << std::endl;
        return order;
    }

    LabelOrderFinderFile::LabelOrderFinderFile(const options::Options &opts)
        : filename(opts.get<std::string>("filename")),
          leftover(opts.get<std::shared_ptr<LabelOrderFinder>>("leftover")) {
    }

    std::vector<int> LabelOrderFinderFile::find_order(const task_representation::FTSTask &fts_task) {
        const int num_labels = fts_task.get_num_labels();

        std::ifstream in(filename);
        if (!in) {
            std::cerr << "label_order_file: cannot open " << filename << std::endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }

        std::vector<int> order;
        std::vector<char> placed(num_labels, 0);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == ';') continue;
            std::istringstream iss(line);
            int l;
            if (!(iss >> l)) {
                std::cerr << "label_order_file: cannot parse line '" << line << "'" << std::endl;
                utils::exit_with(utils::ExitCode::INPUT_ERROR);
            }
            if (l < 0 || l >= num_labels) {
                std::cerr << "label_order_file: label " << l << " out of range, task has "
                          << num_labels << " labels -- was the file made for this task and transform?"
                          << std::endl;
                utils::exit_with(utils::ExitCode::INPUT_ERROR);
            }
            if (placed[l]) {
                std::cerr << "label_order_file: label " << l << " listed twice" << std::endl;
                utils::exit_with(utils::ExitCode::INPUT_ERROR);
            }
            placed[l] = 1;
            order.push_back(l);
        }
        const int from_file = order.size();

        for (int l : leftover->find_order(fts_task))
            if (!placed[l]) order.push_back(l);

        std::cout << "File label order: " << from_file << " labels from " << filename
                  << ", " << (num_labels - from_file) << " appended from the leftover order." << std::endl;
        return order;
    }


    static options::PluginTypePlugin<LabelOrderFinder> _type_plugin(
        "LabeLOrderFinder",
        "This page describes the various label ordering strategies.");

    static shared_ptr<LabelOrderFinder>_parse_random(OptionParser &parser) {
        parser.document_synopsis("random", "");
        utils::add_rng_options(parser);

        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderRandom>(opts);
    }
    static shared_ptr<LabelOrderFinder>_parse_linear(OptionParser &parser) {
        parser.document_synopsis("linear", "");
        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderLinear>(opts);
    }

    static shared_ptr<LabelOrderFinder>_parse_relaxed(OptionParser &parser) {
        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderRelaxed>(opts);
    }

    static shared_ptr<LabelOrderFinder>_parse_causal(OptionParser &parser) {
        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderCausal>(opts);
    }

    static shared_ptr<LabelOrderFinder>_parse_tsort(OptionParser &parser) {
        parser.document_synopsis("Balyo's topological ranking", "");
        parser.add_option<bool>("goal_first",
            "start the depth-first search from the labels that move a goal factor into a goal state", "false");
        Options opts = parser.parse();
        if (parser.help_mode() || parser.dry_run())
            return nullptr;
        return make_shared<LabelOrderFinderTSort>(opts);
    }

    static shared_ptr<LabelOrderFinder>_parse_file(OptionParser &parser) {
        parser.document_synopsis("from file", "");
        parser.add_option<std::string>("filename",
            "label order, one label id of the transformed task per line; ';' starts a comment. "
            "NOTE: the option parser lower-cases the whole --search string, so the path must be lower-case");
        parser.add_option<shared_ptr<LabelOrderFinder>>("leftover",
            "order for the labels the file does not mention", "label_order_relaxed()");
        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderFile>(opts);
    }

    static shared_ptr<LabelOrderFinder>_parse_reverse(OptionParser &parser) {
        parser.document_synopsis("reverse", "");
        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderReverse>(opts);
    }


    static PluginShared<LabelOrderFinder> _plugin_random("label_order_random", _parse_random);
    static PluginShared<LabelOrderFinder> _plugin_linear("label_order_linear", _parse_linear);
    static PluginShared<LabelOrderFinder> _plugin_reverse("label_order_reverse", _parse_reverse);
    static PluginShared<LabelOrderFinder> _plugin_causal("label_order_causal", _parse_causal);
    static PluginShared<LabelOrderFinder> _plugin_relaxed("label_order_relaxed", _parse_relaxed);
    static PluginShared<LabelOrderFinder> _plugin_file("label_order_file", _parse_file);
    static PluginShared<LabelOrderFinder> _plugin_tsort("label_order_tsort", _parse_tsort);

}
