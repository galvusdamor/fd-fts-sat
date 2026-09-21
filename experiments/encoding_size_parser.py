#! /usr/bin/env python

"""
Parser for the per-time-step size of a SAT encoding.

The `sat` engine emits one line per SAT call:

  ENCSTAT length <L> step_clauses_mean <..> step_variables_mean <..>
          step_clauses_last <..> step_variables_last <..>
          total_clauses <..> total_variables <..>

There is one such line per plan length tried, so we keep the last one, i.e.
the call that solved the task (or the last one attempted before the limit).

`step_*_last` is the steady-state cost of one time step. `step_*_mean`
averages over all steps of that call and is slightly higher, because the first
step also creates the state variables of both of its end points while later
steps only add their successor's.
"""

import re

from lab.parser import Parser

ENCSTAT_FIELDS = [
    "step_clauses_mean",
    "step_variables_mean",
    "step_clauses_last",
    "step_variables_last",
    "total_clauses",
    "total_variables",
]


def add_encoding_size(content, props):
    # last ENCSTAT line: the SAT call that solved the task, or the last one tried
    lines = re.findall(r"ENCSTAT (.+)", content)
    if not lines:
        return
    fields = dict(re.findall(r"(\w+) (-?\d+)", lines[-1]))
    for key in ENCSTAT_FIELDS:
        if key in fields:
            props[f"enc_{key}"] = int(fields[key])
    if "length" in fields:
        props["enc_last_length"] = int(fields["length"])


def add_bdd_construction(content, props):
    """BDDSTAT construction_ok / construction_failed from bdd_sat."""
    ok = re.findall(r"BDDSTAT construction_ok (.+)", content)
    if ok:
        fields = dict(re.findall(r"(\w+) (-?[\d.e+]+)", ok[-1]))
        for key, cast in [
            ("nodes_sum_all_factors", int),
            ("nodes_sum_encoded", int),
            ("nodes_max_pair", int),
            ("tseitin_nodes", int),
            ("peak_nodes", int),
            ("construction_time", float),
        ]:
            if key in fields:
                props[f"bdd_{key}"] = cast(float(fields[key])) if cast is int else cast(fields[key])
        props["bdd_construction_failed"] = 0
        return
    failed = re.findall(r"BDDSTAT construction_failed (.+)", content)
    if failed:
        fields = dict(re.findall(r"(\w+) (-?[\d.e+]+)", failed[-1]))
        reason = re.search(r"reason (\w+)", failed[-1])
        props["bdd_construction_failed"] = 1
        if reason:
            props["bdd_failure_reason"] = reason.group(1)
        for key in ("factor", "live_nodes", "peak_nodes"):
            if key in fields:
                props[f"bdd_failure_{key}"] = int(float(fields[key]))


class EncodingSizeParser(Parser):
    def __init__(self):
        Parser.__init__(self)
        self.add_function(add_encoding_size)
        self.add_function(add_bdd_construction)


if __name__ == "__main__":
    EncodingSizeParser().parse()
