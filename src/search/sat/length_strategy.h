#ifndef SAT_LENGTH_STRATEGY_H
#define SAT_LENGTH_STRATEGY_H

namespace sat_search {
    class LengthStrategy {
        virtual int get_first_length() const = 0;
        virtual int get_next_length(int previous_length) const = 0;
    };

    class LengthStrategyOneByOne : public LengthStrategy {

        int get_first_length() override {
            return 1;
        }

        int get_next_length(int previous_length) override {
            return previous_length + 1;
        }
    };

    class LengthStrategyByIteration : public LengthStrategy {
        const int start_length;
        const double multiplier;

        LengthStrategyByIteration (const Options & opts);

        int get_next_length(int previous_length) override {
            int(0.5 + start_length * pow(multiplier, stepNumber);
        }
    };
}

#endif