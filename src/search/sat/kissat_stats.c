/*
  Search statistics of a kissat solver, read from its internal state: the public
  API has no getter, and quiet mode suppresses kissat's own statistics.

  Compiled as C against the kissat sources next to SAT_DIR (see CMakeLists.txt)
  with the defines kissat itself was built with (-DNDEBUG), so that struct kissat
  has the same layout as in the library. The counters used here are COUNTERs,
  which exist irrespective of the STATISTICS / METRICS build options.
*/
#include <limits.h>
#include "internal.h"
#include <stdint.h>
#include <stdlib.h>

int fts_kissat_statistics (void *solver, uint64_t *conflicts, uint64_t *decisions,
                           uint64_t *propagations)
{
  kissat *k = (kissat *) solver;
  *conflicts = k->statistics.conflicts;
  *decisions = k->statistics.decisions;
  *propagations = k->statistics.propagations;
  // layout check: with FTS_KISSAT_PRINT_STATISTICS set (and solver_quiet=false),
  // kissat prints its own statistics, whose conflicts must equal ours
  if (getenv ("FTS_KISSAT_PRINT_STATISTICS"))
    kissat_print_statistics (k);
  return 1;
}


/* Variables after a SAT call: kissat's internal count, the active ones (not
   eliminated, not fixed), and the eliminated and fixed ones (per-variable flags;
   the "eliminated" statistic itself is not compiled into this kissat). */
int fts_kissat_variables (void *solver, uint64_t *vars, uint64_t *active,
                          uint64_t *eliminated, uint64_t *fixed)
{
  kissat *k = (kissat *) solver;
  *vars = k->vars;
  *active = k->active;
  uint64_t e = 0, f = 0;
  for (unsigned idx = 0; idx < k->vars; idx++)
    {
      e += k->flags[idx].eliminated;
      f += k->flags[idx].fixed;
    }
  *eliminated = e;
  *fixed = f;
  return 1;
}
