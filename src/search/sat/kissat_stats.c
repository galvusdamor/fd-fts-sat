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


/* Variables after a SAT call. kissat compacts its internal variable numbering during
   solving and drops eliminated (and merges fixed) variables from it, so the internal
   flags undercount; the import table over the external variables keeps an "eliminated"
   mark. vars: external variables, active: kissat's active internal variables,
   eliminated: external variables eliminated, fixed: internal variables currently fixed
   (after compaction all fixed variables share one internal variable, so this is a lower
   bound). */
int fts_kissat_variables (void *solver, uint64_t *vars, uint64_t *active,
                          uint64_t *eliminated, uint64_t *fixed)
{
  kissat *k = (kissat *) solver;
  const size_t n = SIZE_STACK (k->import);
  uint64_t e = 0;
  for (size_t idx = 1; idx < n; idx++)
    e += PEEK_STACK (k->import, idx).eliminated;
  uint64_t f = 0;
  for (unsigned idx = 0; idx < k->vars; idx++)
    f += k->flags[idx].fixed;
  *vars = n ? n - 1 : 0;
  *active = k->active;
  *eliminated = e;
  *fixed = f;
  return 1;
}
