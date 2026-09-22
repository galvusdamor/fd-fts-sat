#ifndef LABEL_ORDER_FINDER_H
#define LABEL_ORDER_FINDER_H

#include <vector>
#include <memory>


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


}

#endif