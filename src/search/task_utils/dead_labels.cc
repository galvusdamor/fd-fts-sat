#include "dead_labels.h"

#include "h2_information.h"

#include "../task_representation/fts_task.h"
#include "../utils/timer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace std;
using namespace task_representation;

namespace dead_labels {
bool compute(const FTSTask &fts, const Options &opts, vector<bool> &dead) {
    dead.assign(fts.get_num_labels(), false);
    if (opts.source == Source::NONE)
        return false;
    utils::Timer t;
    vector<int> ids;
    if (!h2_information::available(opts.file)) {
        cout << "DEADLABELS skipped: no h2 information in " << opts.file << " (h2 failed or was not run)" << endl;
        return false;
    }
    {
        h2_information::H2Information info(opts.file);
        if (!h2_information::dead_labels(fts, info, ids)) {
            cout << "DEADLABELS skipped: label origins could not be mapped" << endl;
            return false;
        }
    }
    for (int l : ids)
        dead[l] = true;
    cout << "DEADLABELS source sas_file labels " << fts.get_num_labels() << " dead " << ids.size()
         << " time " << t() << endl;
    if (const char *dump = getenv("FTS_DEAD_LABELS_DUMP")) {
        ofstream out(dump);
        for (int l : ids)
            out << l << "\n";
    }
    return true;
}
}
