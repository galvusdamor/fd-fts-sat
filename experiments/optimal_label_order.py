#!/usr/bin/env python3
"""
Given a plan on the transformed FTS task (the file written by
--internal-fts-plan-file: one label id per line), compute the label order
under which bdd_sat(one_step_only=false) needs the fewest time steps to
express *this* plan.

Within one time step the encoding fires a set of labels in label order, each
at most once. A plan (a_1, ..., a_m) therefore fits into consecutive time
steps whose boundaries are exactly the positions i with NOT a_i < a_{i+1} in
the order; the number of steps is 1 + #breaks. Two equal consecutive labels
are always a break (a label fires at most once per step), and contribute a
fixed cost. Minimising the remaining breaks over all linear orders is the
linear ordering / minimum feedback arc set problem on the multigraph of
consecutive pairs, which is NP-hard, hence MaxSAT:

  variables   bef(x,y) for distinct labels x,y in the plan, bef(y,x) = -bef(x,y)
  hard        no 3-cycle: for x<y<z, not (xy & yz & zx) and not (yx & zy & xz)
              (a tournament without 3-cycles is a strict total order)
  soft        bef(a_i, a_{i+1}) for every consecutive pair, weight = multiplicity

The soft part is handled with a totalizer over the break literals and a
descending bound, one kissat call per bound.

Labels that do not occur in the plan are not mentioned in the output; use
label_order_file(filename=..., leftover=...) on the planner side to fill
them in.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time


def read_plan(path):
    plan = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        plan.append(int(line.split()[0]))
    return plan


def read_order(path):
    return read_plan(path)


def breaks_of(order, plan):
    """Number of time-step boundaries the plan needs under this order."""
    pos = {l: i for i, l in enumerate(order)}
    return sum(1 for a, b in zip(plan, plan[1:]) if a == b or pos[a] >= pos[b])


class CNF:
    def __init__(self):
        self.nvars = 0
        self.clauses = []

    def var(self):
        self.nvars += 1
        return self.nvars

    def add(self, *lits):
        self.clauses.append(list(lits))

    def totalizer(self, lits):
        """Unary output o[1..n]: o[i] is true if at least i of `lits` are true
        (Bailleux & Boufkhad, full encoding, so o[i] false forces < i)."""
        if len(lits) == 1:
            return [lits[0]]
        mid = len(lits) // 2
        left, right = self.totalizer(lits[:mid]), self.totalizer(lits[mid:])
        out = [self.var() for _ in range(len(lits))]
        for a in range(len(left) + 1):
            for b in range(len(right) + 1):
                if a + b > 0:
                    # (left>=a & right>=b) -> out>=a+b
                    clause = [out[a + b - 1]]
                    if a > 0:
                        clause.append(-left[a - 1])
                    if b > 0:
                        clause.append(-right[b - 1])
                    self.add(*clause)
                # (left<a+1 & right<b+1) -> out<a+b+1
                if a + b < len(lits):
                    clause = [-out[a + b]]
                    if a < len(left):
                        clause.append(left[a])
                    if b < len(right):
                        clause.append(right[b])
                    self.add(*clause)
        return out

    def write(self, f, extra=()):
        f.write(f"p cnf {self.nvars} {len(self.clauses) + len(extra)}\n")
        for c in self.clauses:
            f.write(" ".join(map(str, c)) + " 0\n")
        for c in extra:
            f.write(" ".join(map(str, c)) + " 0\n")


def solve(kissat, cnf, extra, time_limit, workdir):
    fd, path = tempfile.mkstemp(suffix=".cnf", dir=workdir)
    with os.fdopen(fd, "w") as f:
        cnf.write(f, extra)
    cmd = [kissat, "-q", path]
    if time_limit is not None:
        cmd.append(f"--time={max(1, int(time_limit))}")
    res = subprocess.run(cmd, capture_output=True, text=True)
    os.unlink(path)
    if res.returncode == 20:
        return False, None
    if res.returncode != 10:
        return None, None  # timeout / error
    model = {}
    for line in res.stdout.splitlines():
        if line.startswith("v "):
            for tok in line[2:].split():
                v = int(tok)
                if v:
                    model[abs(v)] = v > 0
    return True, model


def optimal_order(plan, kissat, time_limit, workdir, log=print):
    t0 = time.time()
    labels = sorted(set(plan))
    n = len(labels)
    idx = {l: i for i, l in enumerate(labels)}

    fixed = sum(1 for a, b in zip(plan, plan[1:]) if a == b)
    pairs = {}
    for a, b in zip(plan, plan[1:]):
        if a != b:
            pairs[(a, b)] = pairs.get((a, b), 0) + 1

    # first-occurrence order: a good upper bound and the answer if nothing repeats
    first = []
    seen = set()
    for l in plan:
        if l not in seen:
            seen.add(l)
            first.append(l)
    ub_order, ub = first, breaks_of(first, plan)
    stats = dict(plan_labels=len(plan), distinct=n, fixed_breaks=fixed,
                 first_occurrence_breaks=ub, solver_calls=0)

    if ub == fixed or n <= 2:
        # nothing to optimise: every soft clause is already satisfied, or the
        # order is determined up to the 2-cycle handled by `fixed`
        if n == 2 and ub > fixed:
            alt = list(reversed(first))
            if breaks_of(alt, plan) < ub:
                ub_order, ub = alt, breaks_of(alt, plan)
        stats.update(min_breaks=ub, optimal=True, time=time.time() - t0)
        return ub_order, stats

    cnf = CNF()
    var = {}
    for i in range(n):
        for j in range(i + 1, n):
            var[(i, j)] = cnf.var()

    def bef(x, y):  # literal "x before y" for label ids
        i, j = idx[x], idx[y]
        return var[(i, j)] if i < j else -var[(j, i)]

    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                ij, jk, ik = var[(i, j)], var[(j, k)], var[(i, k)]
                cnf.add(-ij, -jk, ik)   # i<j & j<k -> i<k
                cnf.add(ij, jk, -ik)    # j<i & k<j -> k<i
    n_order_clauses = len(cnf.clauses)

    breaks = []
    for (a, b), w in pairs.items():
        breaks.extend([-bef(a, b)] * w)
    outputs = cnf.totalizer(breaks)
    log(f"optimal_label_order: {n} distinct labels, {len(pairs)} distinct consecutive pairs, "
        f"{len(breaks)} soft literals, {fixed} fixed breaks; "
        f"{n_order_clauses} order clauses, {len(cnf.clauses) - n_order_clauses} totalizer clauses")

    best_order, best = ub_order, ub
    optimal = False
    while True:
        target = best - fixed - 1          # soft breaks allowed
        if target < 0:
            optimal = True
            break
        extra = [[-outputs[target]]]       # fewer than target+1 soft breaks
        remaining = None if time_limit is None else time_limit - (time.time() - t0)
        if remaining is not None and remaining <= 0:
            break
        stats["solver_calls"] += 1
        sat, model = solve(kissat, cnf, extra, remaining, workdir)
        if sat is None:
            log("optimal_label_order: solver timed out, reporting best order so far")
            break
        if not sat:
            optimal = True
            break
        def is_true(lit):
            return model.get(abs(lit), False) == (lit > 0)
        # position of l = number of labels before it; a linear order gives 0..n-1
        order = sorted(labels, key=lambda l: sum(1 for m in labels if m != l and is_true(bef(m, l))))
        assert all(is_true(bef(a, b)) for a, b in zip(order, order[1:])), "model is not a linear order"
        b = breaks_of(order, plan)
        log(f"optimal_label_order: bound {target} soft breaks -> SAT, order has {b} breaks total")
        assert b <= target + fixed, "model does not respect the bound; encoding bug"
        best_order, best = order, b

    stats.update(min_breaks=best, optimal=optimal, time=time.time() - t0)
    return best_order, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plan", help="FTS plan file (label ids, one per line)")
    ap.add_argument("-o", "--output", help="write the order here (default: stdout)")
    ap.add_argument("--kissat", default=os.environ.get("KISSAT", "kissat"))
    ap.add_argument("--time-limit", type=float, default=None, help="seconds for all solver calls together")
    ap.add_argument("--evaluate", metavar="ORDER", help="only report the breaks/horizon of this order for the plan")
    ap.add_argument("--workdir", default=None, help="where to put temporary CNF files")
    args = ap.parse_args()

    plan = read_plan(args.plan)
    if args.evaluate:
        order = read_order(args.evaluate)
        missing = set(plan) - set(order)
        if missing:
            sys.exit(f"order lacks labels {sorted(missing)}")
        b = breaks_of(order, plan)
        print(f"EVAL plan_labels {len(plan)} breaks {b} horizon {b + 1}")
        return

    order, st = optimal_order(plan, args.kissat, args.time_limit, args.workdir,
                              log=lambda m: print(m, file=sys.stderr))
    h = st["min_breaks"] + 1
    print(f"OPTORDER plan_labels {st['plan_labels']} distinct {st['distinct']} "
          f"fixed_breaks {st['fixed_breaks']} min_breaks {st['min_breaks']} predicted_horizon {h} "
          f"first_occurrence_breaks {st['first_occurrence_breaks']} optimal {int(st['optimal'])} "
          f"solver_calls {st['solver_calls']} time {st['time']:.3f}")
    out = open(args.output, "w") if args.output else sys.stdout
    out.write(f"; optimal label order for {args.plan}: {st['plan_labels']} plan labels, "
              f"{st['distinct']} distinct, {st['min_breaks']} breaks "
              f"({st['fixed_breaks']} from repeated consecutive labels), "
              f"predicted horizon {h}, {'optimal' if st['optimal'] else 'NOT proven optimal'}\n")
    for l in order:
        out.write(f"{l}\n")
    if args.output:
        out.close()


if __name__ == "__main__":
    main()
