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

# A full float, exponent included. Note the "-" inside the exponent: a pattern
# like [-?\d.e+]+ stops at the minus of "8.934e-05" and yields "8.934e", which
# float() then rejects.
NUMBER = r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?"

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
    fields = dict(re.findall(rf"(\w+) ({NUMBER})", lines[-1]))
    for key in ENCSTAT_FIELDS:
        if key in fields:
            props[f"enc_{key}"] = int(float(fields[key]))
    if "length" in fields:
        props["enc_last_length"] = int(float(fields["length"]))


def add_bdd_construction(content, props):
    """BDDSTAT construction_ok / construction_failed from bdd_sat."""
    ok = re.findall(r"BDDSTAT construction_ok (.+)", content)
    if ok:
        fields = dict(re.findall(rf"(\w+) ({NUMBER})", ok[-1]))
        for key, cast in [
            ("nodes_sum_all_factors", int),
            ("nodes_sum_encoded", int),
            ("nodes_max_pair", int),
            ("tseitin_nodes", int),
            ("peak_nodes", int),
            ("construction_time", float),
        ]:
            if key in fields:
                props[f"bdd_{key}"] = cast(float(fields[key]))
        props["bdd_construction_failed"] = 0
        return
    failed = re.findall(r"BDDSTAT construction_failed (.+)", content)
    if failed:
        fields = dict(re.findall(rf"(\w+) ({NUMBER})", failed[-1]))
        reason = re.search(r"reason (\w+)", failed[-1])
        props["bdd_construction_failed"] = 1
        if reason:
            props["bdd_failure_reason"] = reason.group(1)
        for key in ("factor", "live_nodes", "peak_nodes"):
            if key in fields:
                props[f"bdd_failure_{key}"] = int(float(fields[key]))


GOALCHAINS_FIELDS = {
    # GOALCHAINS key -> property
    "total_time": "lo_time",
    "chains": "lo_chains",
    "pair_chains": "lo_pair_chains",
    "subgoal_chains": "lo_subgoal_chains",
    "leftover": "lo_leftover",
    "violated": "lo_violated",
    "explored_states": "lo_explored_states",
    "skipped_budget": "lo_skipped_budget",
    "deep_fallbacks": "lo_deep_fallbacks",
}


def add_label_order(content, props):
    """GOALCHAINS statistics of label_order_goal_chains (one line per run)."""
    lines = re.findall(r"GOALCHAINS goal_factors (.+)", content)
    if not lines:
        return
    fields = dict(re.findall(rf"(\w+) ({NUMBER})", lines[-1]))
    for key, prop in GOALCHAINS_FIELDS.items():
        if key in fields:
            v = float(fields[key])
            props[prop] = v if key == "total_time" else int(v)


class EncodingSizeParser(Parser):
    def __init__(self):
        Parser.__init__(self)
        self.add_function(add_encoding_size)
        self.add_function(add_bdd_construction)
        self.add_function(add_label_order)


if __name__ == "__main__":
    EncodingSizeParser().parse()
