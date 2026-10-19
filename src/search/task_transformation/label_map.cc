#include "label_map.h"

#include <numeric>
#include <iostream>
#include <set>

using namespace std;

namespace task_transformation {
LabelMap::LabelMap(int num_labels) {
    reduced_labels.resize(num_labels);
    iota(reduced_labels.begin(), reduced_labels.end(), 0);
}

// renumbering by Labels::cleanup(): -1 means the label was removed, so the original labels
// reduced onto it are gone too (-1). (Keeping the old id left entries pointing into the old
// numbering, which then coincided with unrelated labels of the new one.)
void LabelMap::update(const LabelMapping &old_to_new_labels) {
    for (int &entry : reduced_labels) {
        if (entry != -1) {
            entry = old_to_new_labels[entry];
        }
    }
}

// label reduction: -1 means the label is not reduced and keeps its number
void LabelMap::update(const std::vector<int> &old_to_new_labels) {
    for (int &entry : reduced_labels) {
        if (entry != -1 && old_to_new_labels[entry] != -1) {
            entry = old_to_new_labels[entry];
        }
    }
}

void LabelMap::dump() const {
    for (int entry : reduced_labels) {
        cout << entry << " ";
    }
    cout << endl;
}

std::ostream &operator<<(std::ostream &os, const LabelMap & label_map) {
for (int entry : label_map.reduced_labels) {
    os << entry << " ";
}
return os;

}
}
