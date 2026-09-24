#ifndef LABEL_ORDER_FINDER_H
#define LABEL_ORDER_FINDER_H

#include <vector>
#include <memory>
#include <string>


namespace task_representation {
    class FTSTask;
}

namespace options {
    class Options;
}

namespace utils {
    class RandomNumberGenerator;
}

namespace label_order_finder {
    /*
      Relaxed-reachability layer of every label (see LabelOrderFinderRelaxed),
      -1 for labels never applicable under the delete relaxation. Writes the
      number of non-empty layers to *num_layers if given.
    */
    std::vector<int> relaxed_layers(const task_representation::FTSTask &fts_task, int *num_layers = nullptr);

    class LabelOrderFinder {
    public:
        virtual std::vector<int> find_order(const task_representation::FTSTask &fts_task) = 0;
    };

    class LabelOrderFinderRandom : public LabelOrderFinder {
        std::shared_ptr<utils::RandomNumberGenerator> rng;
    public:
        LabelOrderFinderRandom(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    class LabelOrderFinderLinear : public LabelOrderFinder{
        public:
        LabelOrderFinderLinear(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    class LabelOrderFinderReverse : public LabelOrderFinder{
        public:
        LabelOrderFinderReverse(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    /*
      Orders labels causally: if a label can be applied in state s of some
      factor, labels that can move *into* s there are preferred earlier.

      Self-loops are excluded -- they do not move into anything, and since a
      label that is irrelevant for a factor self-loops on every one of its
      states, including them would make almost every label its own
      predecessor.

      The relation is cyclic in general, so this is not a topological sort. It
      is Kahn's algorithm with forced placement: whenever nothing is free of
      unplaced predecessors, the label with the fewest unmet ones is placed
      anyway and those constraints are counted as violated. The violation
      count is reported, since it says how causal the resulting order really
      is.

      The label-to-label relation is never built. It is kept as producer and
      consumer sets per (factor, state), which is linear in the number of
      transitions; the explicit relation would be quadratic in the label count
      and hopeless for the label-rich tasks (cavediving has 14160 labels,
      matrix-multiplication 59535).
    */
    /*
      Orders labels by the layer of the relaxed planning graph in which they
      first become applicable.

      Start from the initial state of every factor. A label is applicable when
      each factor has some transition for it out of a state already reached.
      All applicable labels form the next layer; applying them adds their
      targets to the reached sets, which only ever grow -- this is the delete
      relaxation, so a state once reached stays reached and the layering is
      acyclic by construction. Labels never applicable go last, in label order.

      Labels within a layer are unordered with respect to each other and are
      emitted by index. Unlike the causal order this cannot contradict itself,
      because a label's layer is strictly greater than that of whatever first
      made it applicable.
    */
    class LabelOrderFinderRelaxed : public LabelOrderFinder{
        public:
        LabelOrderFinderRelaxed(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    class LabelOrderFinderCausal : public LabelOrderFinder{
        public:
        LabelOrderFinderCausal(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    /*
      Balyo's "Topological Ranking" (PhD thesis, 2013, sec. 3.4.2): depth-first
      post-order over the enabling graph, so that every label comes after the
      labels supporting it, and back edges (cycles) are ignored. A label l'
      supports l if in some factor l has a precondition on, l' has a
      non-self-loop transition into one of l's source states there. Roots are
      taken in label order; with goal_first, the labels that move a goal
      factor into a goal state are taken first, which addresses Balyo's own
      caveat that the ranking ignores the goal. Being depth-first, it keeps a
      label's supporter chain contiguous, which layering does not.
    */
    class LabelOrderFinderTSort : public LabelOrderFinder{
        bool goal_first;
        public:
        LabelOrderFinderTSort(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    /*
      Approximates the plan-optimal order (experiments/optimal_label_order.py)
      without a plan.

      For every goal-relevant factor g, take g together with its direct
      causal ancestors -- factors on which a label that moves g has a
      precondition -- build their explicit product and find a shortest path
      to g's goal (and the goals of the other included factors) by BFS. Every
      such abstract plan is a chain of labels. Ancestors are added strongest
      first (most shared labels) and dropped again from the weakest end
      whenever the product exceeds max_states reachable states.

      The chains are then merged into one order by minimising the number of
      consecutive chain pairs (a,b) with b before a -- the same linear
      ordering problem the plan-optimal inferrer solves, on the multigraph of
      all chains together. The precedence graph is split into strongly
      connected components, which are ordered topologically at no cost; each
      component is ordered by exact MaxSAT (kissat, bef(x,y) variables,
      no-3-cycle clauses, bounded totalizer, descending bound) when it has at
      most exact_max_size labels, otherwise and as the MaxSAT's starting
      bound by the Eades-Lin-Smyth heuristic plus insertion local search.
      Labels in no chain follow `leftover`.

      The chains from different goals ignore each other, so unlike a
      plan-derived order this gives no horizon guarantee.
    */
    class LabelOrderFinderGoalChains : public LabelOrderFinder{
        int max_states;
        int exact_max_size;
        int exact_conflicts;
        double exact_time_limit;
        std::shared_ptr<LabelOrderFinder> leftover;
        bool verbose;
        int ancestor_depth;
        int subgoal_depth;
        int plans_per_goal;
        int plan_slack;
        int selection_rounds;
        bool leftover_support;
        bool leftover_layer;
        int goal_pairs;
        int state_budget;
        bool all_plans;
        int deep_giveup;
        int work_budget;
        public:
        LabelOrderFinderGoalChains(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };

    /*
      Reads a (partial) label order from a file: one label id per line, lines
      starting with ';' are comments. Labels not mentioned in the file are
      appended in the order given by `leftover` (default: label_order_relaxed).

      Meant for experiments/optimal_label_order.py, which computes the order
      that packs a given plan into the fewest time steps; the labels that do
      not occur in that plan are exactly the ones the file leaves out.
    */
    class LabelOrderFinderFile : public LabelOrderFinder{
        std::string filename;
        std::shared_ptr<LabelOrderFinder> leftover;
        public:
        LabelOrderFinderFile(const options::Options &opts);
        std::vector<int> find_order(const task_representation::FTSTask &fts_task) override;
    };


}

#endif