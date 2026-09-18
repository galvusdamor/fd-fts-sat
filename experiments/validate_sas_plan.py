#!/usr/bin/env python3
"""Validate a plan against a SAS+ (version 3) task. Exit 0 = valid."""
import sys

def parse_sas(path):
    toks = open(path).read().split("\n")
    i = 0
    def line():
        nonlocal i
        v = toks[i]; i += 1; return v
    def expect(s):
        v = line()
        assert v == s, f"expected {s!r} got {v!r} at line {i}"
    expect("begin_version"); ver = int(line()); expect("end_version")
    assert ver == 3, f"unsupported SAS version {ver}"
    expect("begin_metric"); line(); expect("end_metric")
    nvars = int(line())
    ranges = []
    for _ in range(nvars):
        expect("begin_variable")
        line()                      # name
        line()                      # axiom layer
        r = int(line()); ranges.append(r)
        for _ in range(r): line()   # value names
        expect("end_variable")
    nmutex = int(line())
    for _ in range(nmutex):
        expect("begin_mutex_group")
        n = int(line())
        for _ in range(n): line()
        expect("end_mutex_group")
    expect("begin_state")
    init = [int(line()) for _ in range(nvars)]
    expect("end_state")
    expect("begin_goal")
    ng = int(line())
    goal = []
    for _ in range(ng):
        a, b = line().split(); goal.append((int(a), int(b)))
    expect("end_goal")
    nops = int(line())
    ops = {}
    for _ in range(nops):
        expect("begin_operator")
        name = line()
        prevail = []
        for _ in range(int(line())):
            a, b = line().split(); prevail.append((int(a), int(b)))
        effects = []
        for _ in range(int(line())):
            parts = [int(x) for x in line().split()]
            ncond = parts[0]
            conds = [(parts[1 + 2*k], parts[2 + 2*k]) for k in range(ncond)]
            var, pre, post = parts[1 + 2*ncond], parts[2 + 2*ncond], parts[3 + 2*ncond]
            effects.append((conds, var, pre, post))
        cost = int(line())
        expect("end_operator")
        ops.setdefault(name, []).append((prevail, effects, cost))
    naxioms = int(line())
    if naxioms: raise SystemExit("VALIDATOR-UNSUPPORTED: task has axioms")
    return ranges, init, goal, ops

def parse_plan(path):
    out = []
    for l in open(path):
        l = l.strip()
        if not l or l.startswith(";"): continue
        assert l.startswith("(") and l.endswith(")"), f"bad plan line {l!r}"
        out.append(l[1:-1])
    return out

def main():
    task, plan_file = sys.argv[1], sys.argv[2]
    ranges, state, goal, ops = parse_sas(task)
    plan = parse_plan(plan_file)
    state = list(state)
    for step, name in enumerate(plan):
        if name not in ops:
            print(f"INVALID: step {step}: no operator named {name!r}"); return 1
        # a name may be shared; accept the first applicable variant
        chosen = None
        for prevail, effects, cost in ops[name]:
            if all(state[v] == d for v, d in prevail) and \
               all(pre == -1 or state[v] == pre for _, v, pre, _ in effects):
                chosen = (prevail, effects); break
        if chosen is None:
            print(f"INVALID: step {step}: {name!r} not applicable"); return 1
        _, effects = chosen
        new = dict()
        for conds, var, pre, post in effects:
            if all(state[v] == d for v, d in conds):
                if var in new and new[var] != post:
                    print(f"INVALID: step {step}: conflicting effects on var{var}"); return 1
                new[var] = post
        for var, post in new.items(): state[var] = post
    unmet = [(v, d) for v, d in goal if state[v] != d]
    if unmet:
        print(f"INVALID: goal not reached, unmet {unmet}"); return 1
    print(f"Plan valid ({len(plan)} steps)"); return 0

sys.exit(main())
