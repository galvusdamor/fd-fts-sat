#ifndef TASK_UTILS_H2_INFORMATION_H
#define TASK_UTILS_H2_INFORMATION_H

#include "../task_representation/fact.h"

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace task_representation {
class FTSTask;
class SASTask;
}

/*
  What the h2 preprocessor found out about the task, without letting it change the task: the
  preprocessor runs on a copy and writes its result to a separate SAS file, and this class
  maps that file back onto the original task (g_sas_task()) by fact and operator names --
  the preprocessor renumbers variables and values and drops facts, operators and variables.

  * mutex(a, b): facts a and b of the original task are h2-mutex;
  * unreachable(a): fact a is h2-unreachable (its variable is in the h2 task, the value is not);
  * removed_operator(o): operator o of the original task is not in the h2 task (h2 proved it
    never applicable, or relevance analysis found it useless; either way no plan needs it).
  Facts whose variable the preprocessor dropped entirely carry no information.
*/
namespace h2_information {
class H2Information {
    std::unique_ptr<task_representation::SASTask> h2;
    // original fact -> h2 fact (var -1: no information, value -1: unreachable)
    std::vector<std::vector<task_representation::FactPair>> to_h2;
    std::vector<bool> operator_removed;
    int num_removed = 0;
public:
    explicit H2Information(const std::string &h2_sas_file);
    ~H2Information();

    bool mutex(const task_representation::FactPair &a, const task_representation::FactPair &b) const;
    bool unreachable(const task_representation::FactPair &a) const;
    bool removed_operator(int op) const {return operator_removed[op];}
    int get_num_removed_operators() const {return num_removed;}
};

/*
  The labels of the transformed task all of whose operators the h2 preprocessor removed,
  found by handing the label origins down through the transformations
  (PlanReconstruction::map_label_sets). Prints a line "H2LABELS ..." with the counts.
  Returns false (and says why) if the label origins cannot be mapped.
*/
bool dead_labels(const task_representation::FTSTask &fts, const H2Information &h2, std::vector<int> &dead);
}

#endif
