/* No kissat sources available: no search statistics (see kissat_stats.c). */
#include <stdint.h>

int fts_kissat_statistics (void *solver, uint64_t *conflicts, uint64_t *decisions,
                           uint64_t *propagations)
{
  (void) solver;
  *conflicts = *decisions = *propagations = 0;
  return 0;
}
