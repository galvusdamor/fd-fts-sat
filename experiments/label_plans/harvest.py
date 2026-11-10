#!/usr/bin/env python3
"""
Label-plan dataset: collects the plans that local runs printed as label ids of the transformed
FTS ("Time t" / "Label : l" lines of plan extraction), validates them on the task's FTS dump and
writes, per task,

  DATASET/<domain>__<task>/fts.txt.gz -> symlink to the FTS dump (dump_fts format)
  DATASET/<domain>__<task>/plans.jsonl   one distinct plan per line:
      {"steps": [[labels of step 1], ...],      as printed (labels of a step in id order)
       "sequence": [labels] | null,             a valid serialisation (null: none found)
       "valid": true | false | null,            null: no serialisation within the search budget
       "length": n, "horizon": h,
       "sources": [{"config": ..., "log": ...}, ...]}
  DATASET/<domain>__<task>/meta.json
  DATASET/index.csv

Validation replays the plan on the dump: per factor the set of states it can be in (exact for
nondeterministic transitions, as the factors only interact through the label sequence); a label
is applicable if every factor it is relevant to has a source state in its set. The labels of a
step are logged without their execution order, so a valid order is searched (DFS, budget
--budget nodes per step).

Only plans whose label numbering is the dump's survive validation, so logs of other
transformations (or older label numberings) drop out by themselves.

Usage: harvest.py DUMPDIR DATASET LOGROOT... [--budget 20000] [--ff FFDIR]
Run it throttled, e.g. systemd-run --user --scope -p CPUQuota=20% nice -n 19 harvest.py ...
"""
import argparse, csv, gzip, json, os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fts_dumps"))
from fts_lib import read_fts


def parse_plans(text):
    """the extracted plans of a log: list of steps (lists of labels)"""
    if "Solution found" not in text:
        return []
    plans, cur, last_t = [], None, None
    for line in text.splitlines():
        if line.startswith("Time "):
            t = int(line.split()[1])
            if cur is None or (last_t is not None and t <= last_t):
                cur = []
                plans.append(cur)
            cur.append([])
            last_t = t
        elif line.startswith("Label : ") and cur is not None:
            cur[-1].append(int(line.split()[-1]))
    return [[s for s in p if s] for p in plans if any(p)]


class Replay:
    def __init__(self, fts):
        self.fts = fts
        self.succ = []   # per factor: label -> source -> set of targets
        for f in range(fts.F):
            d = defaultdict(lambda: defaultdict(set))
            for l, tr in fts.trans[f].items():
                for s, t in tr:
                    d[l][s].add(t)
            self.succ.append(d)
        self.relevant = defaultdict(list)   # label -> factors
        for f in range(fts.F):
            for l in fts.trans[f]:
                self.relevant[l].append(f)

    def changes(self, state, l):
        """the factors l changes in state (a list of sets per factor): [(f, new set)], or None"""
        out = []
        for f in self.relevant.get(l, ()):
            nxt = set()
            for st in state[f]:
                nxt |= self.succ[f][l].get(st, set())
            if not nxt:
                return None
            out.append((f, nxt))
        return out

    def goal(self, state):
        return all(not self.fts.goals[f] or state[f] & self.fts.goals[f] for f in range(self.fts.F))

    def serialise(self, steps, budget):
        state = [{s} for s in self.fts.init]
        sequence = []
        for step in steps:
            nodes = [0]
            order = []
            def dfs(rest):
                if not rest:
                    return True
                for i, l in enumerate(rest):
                    nodes[0] += 1
                    if nodes[0] > budget:
                        return False
                    ch = self.changes(state, l)
                    if ch is None:
                        continue
                    old = [(f, state[f]) for f, _ in ch]
                    for f, n in ch:
                        state[f] = n
                    order.append(l)
                    if dfs(rest[:i] + rest[i + 1:]):
                        return True
                    order.pop()
                    for f, o in old:
                        state[f] = o
                return False
            if not dfs(list(step)):
                return None, nodes[0] > budget
            sequence += order
        if not self.goal(state):
            return None, False
        return sequence, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("dataset")
    ap.add_argument("logroots", nargs="+")
    ap.add_argument("--budget", type=int, default=20000)
    ap.add_argument("--ff", help="directory of FF plans (dump_and_ff.py: <task>.txt, one label per line)")
    a = ap.parse_args()
    tasks = {f[:-len(".fts.gz")] for f in os.listdir(a.dumpdir) if f.endswith(".fts.gz")}
    # logs per task (the task is the path component that names a dumped task; the other
    # components below the root name the configuration)
    logs = defaultdict(list)
    for root in a.logroots:
        for d, _, files in os.walk(root):
            if "log" not in files:
                continue
            parts = os.path.relpath(d, root).split(os.sep)
            hit = [p for p in parts if p in tasks]
            if not hit:
                continue
            task = hit[-1]
            config = "/".join([os.path.basename(root.rstrip("/"))] + [p for p in parts if p != task])
            logs[task].append((config, os.path.join(d, "log")))
    os.makedirs(a.dataset, exist_ok=True)
    index = []
    if a.ff:
        for f in os.listdir(a.ff):
            if f.endswith(".txt") and f[:-4] in tasks:
                logs.setdefault(f[:-4], [])
    for task in sorted(logs):
        out = os.path.join(a.dataset, task)
        if os.path.exists(os.path.join(out, "meta.json")):
            index.append(json.load(open(os.path.join(out, "meta.json"))))
            continue
        plans = {}   # steps (tuple) -> record
        for config, path in logs[task]:
            try:
                text = open(path, errors="replace").read()
            except OSError:
                continue
            for steps in parse_plans(text):
                key = tuple(tuple(s) for s in steps)
                rec = plans.setdefault(key, {"steps": steps, "sources": []})
                rec["sources"].append({"config": config, "log": path})
        if a.ff and os.path.exists(os.path.join(a.ff, task + ".txt")):
            seq = [int(x) for x in open(os.path.join(a.ff, task + ".txt")) if x.strip() and not x.startswith(";")]
            key = tuple((l,) for l in seq)
            rec = plans.setdefault(key, {"steps": [[l] for l in seq], "sources": []})
            rec["sources"].append({"config": "ff_pref", "log": os.path.join(a.ff, task + ".txt")})
        fts = read_fts(os.path.join(a.dumpdir, task + ".fts.gz"))
        rp = Replay(fts)
        valid = invalid = unknown = 0
        for rec in plans.values():
            seq, budget_hit = rp.serialise(rec["steps"], a.budget)
            rec["sequence"] = seq
            rec["valid"] = True if seq is not None else (None if budget_hit else False)
            rec["length"] = sum(len(s) for s in rec["steps"])
            rec["horizon"] = len(rec["steps"])
            valid += seq is not None
            invalid += rec["valid"] is False
            unknown += rec["valid"] is None
        os.makedirs(out, exist_ok=True)
        link = os.path.join(out, "fts.txt.gz")
        if not os.path.lexists(link):
            os.symlink(os.path.abspath(os.path.join(a.dumpdir, task + ".fts.gz")), link)
        with open(os.path.join(out, "plans.jsonl"), "w") as f:
            for rec in plans.values():
                f.write(json.dumps(rec) + "\n")
        ok = [r for r in plans.values() if r["valid"]]
        meta = {"task": task, "factors": fts.F, "labels": fts.L, "logs": len(logs[task]),
                "plans": len(plans), "valid": valid, "invalid": invalid, "unknown": unknown,
                "min_horizon": min((r["horizon"] for r in ok), default=None),
                "min_length": min((r["length"] for r in ok), default=None)}
        json.dump(meta, open(os.path.join(out, "meta.json"), "w"), indent=1)
        index.append(meta)
        print(task, meta["plans"], "plans:", valid, "valid", invalid, "invalid", unknown, "unknown", flush=True)
    with open(os.path.join(a.dataset, "index.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(index[0]))
        w.writeheader()
        w.writerows(index)


if __name__ == "__main__":
    main()
