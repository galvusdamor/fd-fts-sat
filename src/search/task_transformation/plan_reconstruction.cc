#include "plan_reconstruction.h"

#include <algorithm>
using namespace std;

namespace task_transformation {
PlanReconstructionSequence::PlanReconstructionSequence(
    vector<shared_ptr<PlanReconstruction>> plan_reconstructions_)
    : plan_reconstructions(plan_reconstructions_) {

    std::reverse(plan_reconstructions.begin(), plan_reconstructions.end());
}

    std::ostream& operator<<(std::ostream& o, const PlanReconstruction& b) {
        b.print(o);
        return o;
    }

void normalize(vector<StateSets::Tuple> &tuples) {
    sort(tuples.begin(), tuples.end());
    tuples.erase(unique(tuples.begin(), tuples.end()), tuples.end());
}

vector<StateSets::Tuple> closure_tuples(const vector<vector<StateSets::Tuple>> &state_tuples,
                                        const vector<vector<int>> &successors, int start) {
    vector<StateSets::Tuple> result;
    vector<char> seen(state_tuples.size(), 0);
    vector<int> stack {start};
    seen[start] = 1;
    while (!stack.empty()) {
        const int x = stack.back();
        stack.pop_back();
        result.insert(result.end(), state_tuples[x].begin(), state_tuples[x].end());
        for (int y : successors[x])
            if (!seen[y]) { seen[y] = 1; stack.push_back(y); }
    }
    normalize(result);
    return result;
}

}
