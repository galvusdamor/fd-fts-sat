#! /usr/bin/env python3

"""
Formula sizes of every SAT call, read directly from the run logs of a lab experiment -- for
solved and unsolved runs, algorithm A (sat) and algorithm C (rintanen) alike.

    formula_sizes.py OUT.json SOURCE [SOURCE ...]

SOURCE is an experiment directory (data/<name>, with runs-*/<id>/run.log) or its archive
(data/<name>.tar.bz2, as written by the compress-runs-dir step), read without unpacking.
OUT.json maps "<algorithm>:<domain>:<problem>" to

  fs_lengths              number of formulas generated
  fs_max_length           the largest length generated
  fs_total_clauses/_variables   the whole formula at fs_max_length
  fs_step_clauses/_variables    one time step: the difference between the formulas of the two
                                largest generated lengths >= 2, divided by the length
                                difference. The formula grows exactly linearly from length 1 on
                                (checked: total(L) - total(L-1) = ENCSTAT step_*_last), so this
                                is the per-step size, independent of the algorithm.

Lines read:
  rintanen   "Step NN for LLLL timesteps: C clauses V variables"  (every generated call)
  sat        "ENCSTAT length L ... total_clauses C total_variables V"
Runs with formulas of fewer than two lengths >= 1 get no fs_step_* (a single formula does not
separate the per-step part from the initial/goal part), except that an ENCSTAT line provides
step_*_last directly.
"""

import json
import os
import re
import sys
import tarfile

RINTANEN = re.compile(r"Step\s+\d+\s+for\s+(\d+)\s+timesteps: (\d+) clauses (\d+) variables")
ENCSTAT = re.compile(r"ENCSTAT length (\d+) .*?step_clauses_last (\d+) step_variables_last (\d+) "
                     r"total_clauses (\d+) total_variables (\d+)")


def sizes(log):
    formulas = {}  # length -> (clauses, variables)
    encstat_step = None
    for m in RINTANEN.finditer(log):
        formulas[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    for m in ENCSTAT.finditer(log):
        L = int(m.group(1))
        formulas[L] = (int(m.group(4)), int(m.group(5)))
        encstat_step = (int(m.group(2)), int(m.group(3)))
    if not formulas:
        return None
    lengths = sorted(formulas)
    top = lengths[-1]
    out = dict(fs_lengths=len(lengths), fs_max_length=top,
               fs_total_clauses=formulas[top][0], fs_total_variables=formulas[top][1])
    usable = [L for L in lengths if L >= 1]
    if len(usable) >= 2 and usable[-1] >= 2:
        a, b = usable[-2], usable[-1]
        out["fs_step_clauses"] = (formulas[b][0] - formulas[a][0]) // (b - a)
        out["fs_step_variables"] = (formulas[b][1] - formulas[a][1]) // (b - a)
    elif encstat_step:
        out["fs_step_clauses"], out["fs_step_variables"] = encstat_step
    return out


def runs_in_directory(path):
    for runs in sorted(os.listdir(path)):
        if not runs.startswith("runs-"):
            continue
        for run in sorted(os.listdir(os.path.join(path, runs))):
            d = os.path.join(path, runs, run)
            try:
                static = json.load(open(os.path.join(d, "static-properties")))
                log = open(os.path.join(d, "run.log"), errors="replace").read()
            except (FileNotFoundError, NotADirectoryError):
                continue
            yield static, log


def runs_in_archive(path):
    pending = {}  # run directory -> {"static": ..., "log": ...}
    with tarfile.open(path, "r|bz2") as tar:
        for member in tar:
            name = os.path.basename(member.name)
            if name not in ("static-properties", "run.log") or not member.isfile():
                continue
            d = os.path.dirname(member.name)
            data = tar.extractfile(member).read().decode(errors="replace")
            entry = pending.setdefault(d, {})
            entry["static" if name == "static-properties" else "log"] = data
            if len(entry) == 2:
                del pending[d]
                yield json.loads(entry["static"]), entry["log"]


def main():
    out_file, sources = sys.argv[1], sys.argv[2:]
    result = {}
    for source in sources:
        runs = runs_in_archive(source) if source.endswith(".tar.bz2") else runs_in_directory(source)
        n = 0
        for static, log in runs:
            s = sizes(log)
            if s:
                result[f"{static['algorithm']}:{static['domain']}:{static['problem']}"] = s
            n += 1
        print(f"{source}: {n} runs")
    json.dump(result, open(out_file, "w"), indent=0)
    print(f"{out_file}: {len(result)} runs with formula sizes")


if __name__ == "__main__":
    main()
