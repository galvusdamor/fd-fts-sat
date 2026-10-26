/* No kissat sources available: no search statistics (see kissat_stats.c). */
#include <stdint.h>

int fts_kissat_statistics (void *solver, uint64_t *conflicts, uint64_t *decisions,
                           uint64_t *propagations)
{
  (void) solver;
  *conflicts = *decisions = *propagations = 0;
  return 0;
}

uint64_t fts_kissat_conflicts (void *solver)
{
  (void) solver;
  return 0;
}

int fts_kissat_variables (void *solver, uint64_t *vars, uint64_t *active,
                          uint64_t *eliminated, uint64_t *fixed)
{
  (void) solver;
  *vars = *active = *eliminated = *fixed = 0;
  return 0;
}
