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


}

#endif