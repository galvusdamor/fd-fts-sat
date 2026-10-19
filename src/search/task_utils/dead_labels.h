#ifndef TASK_UTILS_DEAD_LABELS_H
#define TASK_UTILS_DEAD_LABELS_H

#include <string>
#include <vector>

namespace task_representation {
class FTSTask;
}

/*
  Labels of the FTS that no plan needs: an encoding may force them false at every step (and
  simplify whatever it built over them accordingly). Like task_utils/state_mutexes, the
  sources do not know the encodings, and the transformations hand the label origins down
  themselves (PlanReconstruction::map_label_sets).
*/
namespace dead_labels {
enum class Source {
    NONE,
    SAS_FILE   // labels all of whose operators the h2 preprocessor removed; file = its output
               // (task_utils/h2_information), the task itself not preprocessed
};

struct Options {
    Source source = Source::NONE;
    std::string file;
};

/*
  dead[l] is true for dead labels. Returns false (dead all false) if the source gives nothing
  or the labels cannot be mapped; a line "DEADLABELS skipped: <reason>" then says why.
  FTS_DEAD_LABELS_DUMP=file writes the dead label ids, one per line.
*/
bool compute(const task_representation::FTSTask &fts, const Options &opts, std::vector<bool> &dead);
}

#endif
