#ifndef SAT_LENGTH_STRATEGY_H
#define SAT_LENGTH_STRATEGY_H
#include <optional>
#include <vector>

namespace options {
    class Options;
}

namespace sat_search {
    class LengthStrategy {
    public:
        virtual ~LengthStrategy() = default;
        virtual int get_first_length() const = 0;
        virtual std::optional<int> get_next_length(int step_number, int previous_length) const = 0;
    };

    class LengthStrategyConstant : public LengthStrategy {
        const int plan_length;
    public:
        explicit LengthStrategyConstant(const options::Options & opts);
        virtual ~LengthStrategyConstant() override = default;

        int get_first_length() const override {
            return plan_length;
        }

        std::optional<int> get_next_length(int , int) const override {
            return std::nullopt;
        }
    };

	// TODO maybe add starting value and step size here? Could be helpful for tests
    class LengthStrategyOneByOne : public LengthStrategy {
        const int maximum_length;   // -1: unbounded
    public:
        explicit LengthStrategyOneByOne(int maximum_length_ = -1) : maximum_length(maximum_length_) {}
        ~LengthStrategyOneByOne() override = default;
        int get_first_length() const override {
            return 1;
        }

        std::optional<int> get_next_length(int , int previous_length) const override {
            if (maximum_length >= 0 && previous_length + 1 > maximum_length) return std::nullopt;
            return previous_length + 1;
        }
    };

    // exactly the given lengths, in the given order (for profiling runs)
    class LengthStrategyList : public LengthStrategy {
        const std::vector<int> lengths;
    public:
        explicit LengthStrategyList(std::vector<int> lengths_) : lengths(std::move(lengths_)) {}
        ~LengthStrategyList() override = default;
        int get_first_length() const override {
            return lengths.front();
        }
        std::optional<int> get_next_length(int step_number, int) const override {
            if (step_number >= (int)lengths.size()) return std::nullopt;
            return lengths[step_number];
        }
    };

    // start_length, start_length + increment, start_length + 2*increment, ...
    class LengthStrategyByIncrement : public LengthStrategy {
        const int start_length;
        const int increment;
        const int maximum_iteration;
    public:
        LengthStrategyByIncrement(const options::Options & opts);
        ~LengthStrategyByIncrement() override = default;
        int get_first_length() const override {
            return start_length;
        }
        std::optional<int> get_next_length(int step_number, int previous_length) const override;
    };

    class LengthStrategyByIteration : public LengthStrategy {
        const int start_length;
        const double multiplier;
        const int maximum_iteration;
        const bool strictly_increasing;
    public:
        LengthStrategyByIteration (const options::Options & opts);
        ~LengthStrategyByIteration() override = default;
        int get_first_length() const override {
            return start_length;
        }

        std::optional<int> get_next_length(int step_number, int previous_length) const override;
    };
}

#endif
