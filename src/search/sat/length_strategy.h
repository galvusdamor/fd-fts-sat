#ifndef SAT_LENGTH_STRATEGY_H
#define SAT_LENGTH_STRATEGY_H
#include <optional>

namespace options {
    class Options;
}

namespace sat_search {
    class LengthStrategy {
    public:
        virtual int get_first_length() const = 0;
        virtual std::optional<int> get_next_length(int step_number, int previous_length) const = 0;
    };

    class LengthStrategyConstant : public LengthStrategy {
        const int plan_length;
    public:
        explicit LengthStrategyConstant(const options::Options & opts);

        int get_first_length() const override {
            return plan_length;
        }

        std::optional<int> get_next_length(int , int) const override {
            return std::nullopt;
        }
    };

	// TODO maybe add starting value and step size here? Could be helpful for tests
    class LengthStrategyOneByOne : public LengthStrategy {
    public:
        int get_first_length() const override {
            return 1;
        }

        std::optional<int> get_next_length(int , int previous_length) const override {
            return previous_length + 1;
        }
    };

    class LengthStrategyByIteration : public LengthStrategy {
        const int start_length;
        const double multiplier;
        const int maximum_iteration;
    public:
        LengthStrategyByIteration (const options::Options & opts);

        int get_first_length() const override {
            return start_length;
        }

        std::optional<int> get_next_length(int step_number, int previous_length) const override;
    };
}

#endif
