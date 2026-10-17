"""Reader for the FTS dumps of the dump_fts transformation (see task_transformation_dump.h).

    fts = read_fts(path)
    fts.F, fts.L, fts.cost[l], fts.size[f], fts.init[f], fts.goals[f] (set)
    fts.trans[f][l] = list of (src, target); a label missing from fts.trans[f] is a
                      self-loop in every state of f (a no-op there)
"""
import gzip
from collections import defaultdict


class FTS:
    def __init__(self):
        self.F = self.L = 0
        self.cost, self.size, self.init, self.goals = [], [], [], []
        self.trans = []

    def moves(self, f, l):
        """l has a transition in f that is not a self-loop"""
        return any(s != t for s, t in self.trans[f].get(l, ()))

    def only_selfloops(self, f, l):
        """l is a self-loop wherever it is applicable in f (possibly not in every state)"""
        return all(s == t for s, t in self.trans[f].get(l, ()))

    def has_precondition(self, f, l):
        """l is not applicable in every state of f"""
        tr = self.trans[f].get(l)
        return tr is not None and len({s for s, _ in tr}) < self.size[f]


def read_fts(path):
    fts = FTS()
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "t":
                fts.trans[int(p[1])][int(p[2])].append((int(p[3]), int(p[4])))
            elif p[0] == "label":
                fts.cost[int(p[1])] = int(p[2])
            elif p[0] == "factor":
                f_ = int(p[1])
                fts.size[f_], fts.init[f_] = int(p[2]), int(p[3])
                fts.goals[f_] = {int(x) for x in p[5:5 + int(p[4])]}
            elif p[0] == "fts":
                fts.F, fts.L = int(p[1]), int(p[2])
                fts.cost = [0] * fts.L
                fts.size, fts.init = [0] * fts.F, [0] * fts.F
                fts.goals = [set() for _ in range(fts.F)]
                fts.trans = [defaultdict(list) for _ in range(fts.F)]
    return fts
