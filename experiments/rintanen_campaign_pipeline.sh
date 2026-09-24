#!/bin/bash
# Rounds 2.. of the rintanen profiling campaign, unattended.
# Waits for round 1, rebuilds, smoke-tests step_time_limits, then alternates
# demand analysis and demand-driven profiling until nothing is missing, the
# demand stops shrinking, or MAX_ROUNDS is reached. Aborts on the first error.
set -u
REPO=/home/gregor/data/claude/fd-fts-sat
C=/home/gregor/data/rintanen-campaign
MAX_ROUNDS=${MAX_ROUNDS:-5}
T=${SIM_TIME_LIMIT:-300}
LOG=$C/pipeline.log
export PATH=/usr/bin:$PATH
say() { echo "$(date '+%F %T') $*" | tee -a $LOG; }
cd $REPO

say "waiting for round 1"
while ps -eo args | grep -q '[r]intanen_profile_campaign.py .*--lengths 1-30'; do sleep 60; done
say "round 1 done: $(grep -c '^\[' $C/round1.log) instances"

say "building"
./build.py release64 -s/home/gregor/data/lisat/kissat-p/build --custom-kissat -j8 > $C/build.log 2>&1 || { say "BUILD FAILED"; exit 1; }

say "smoke test"
S=$C/smoke; rm -rf $S; mkdir -p $S; cd $S
P=/home/gregor/data/lisat/classical-domains/classical/gripper/prob01.pddl
python3 $REPO/fast-downward.py $P --transform "cost(cost_type=one)" --search \
  "sat(encoder=bdd_sat(one_step_only=false),solver_quiet=true,continue_after_first_plan=true,step_time_limit=60,step_time_limits=[1,2,3],length_strategy=length_list(lengths=[1,2,3,4]))" > smoke.log 2>&1
n=$(grep -c '^STEP' smoke.log)
[ "$n" = "4" ] || { say "SMOKE FAILED: $n STEP lines"; exit 1; }
python3 $REPO/fast-downward.py $P --transform "cost(cost_type=one)" --search \
  "sat(encoder=bdd_sat(one_step_only=false),solver_quiet=true)" > smoke2.log 2>&1
grep -q 'Solution found' smoke2.log || { say "SMOKE FAILED: default sat()"; exit 1; }
cd $REPO
say "smoke ok"

prev=999999
for r in $(seq 2 $MAX_ROUNDS); do
  say "demand analysis before round $r"
  python3 experiments/rintanen_demand.py $C/profiles --full-grid --processes 8 --time-limit $T --out $C/demand_r$r.json > $C/demand_r$r.log 2>&1 || { say "DEMAND FAILED"; exit 1; }
  python3 experiments/rintanen_demand_cost.py $C/demand_r$r.json $C/profiles 4 >> $C/demand_r$r.log 2>&1
  cat $C/demand_r$r.log | tee -a $LOG
  u=$(python3 -c "import json; print(json.load(open('$C/demand_r$r.json'))['summary']['undetermined'])")
  if [ "$u" = "0" ]; then say "nothing missing"; break; fi
  if [ "$u" -ge "$prev" ]; then say "demand not shrinking ($u >= $prev), stopping"; break; fi
  prev=$u
  say "round $r: $u instances"
  python3 experiments/rintanen_profile_campaign.py experiments/ipc-suite-2026-local-paths.json \
    --outdir $C/profiles --demand $C/demand_r$r.json --step-limit 300 --memory-limit 3000 --jobs 4 > $C/round$r.log 2>&1 \
    || { say "ROUND $r FAILED"; exit 1; }
  say "round $r done"
done
say "pipeline finished"
