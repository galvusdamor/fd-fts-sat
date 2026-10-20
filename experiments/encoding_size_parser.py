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
        # bdd_reorder: nodes_sum_all_factors is from before reordering, and
        # what gets Tseitin-translated is nodes_sum_after
        reorder = re.findall(r"BDDSTAT reorder (.+)", content)
        if reorder:
            fields = dict(re.findall(rf"(\w+) ({NUMBER})", reorder[-1]))
            for key, prop, cast in [
                ("time", "bdd_reorder_time", float),
                ("nodes_sum_after", "bdd_nodes_sum_reordered", int),
                ("reorderings", "bdd_reorderings", int),
            ]:
                if key in fields:
                    props[prop] = cast(float(fields[key]))
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


def _last(pattern, content, cast=int):
    m = re.findall(pattern, content)
    return cast(m[-1]) if m else None


def add_transformation_info(content, props):
    """The transformed task, tau_baking / label_closure, h2 information
    (--h2-information), mutexes and dead labels, kissat effort. Attributes are
    only set when their line is present."""
    def put(name, value):
        if value is not None:
            props[name] = value
    m = re.findall(r"Main task: FTSTask with (\d+) variables, (\d+) labels, (\d+) facts, (\d+) transitions", content)
    if m:
        for name, v in zip(("fts_factors", "fts_labels", "fts_facts", "fts_transitions"), m[-1]):
            props[name] = int(v)
    props["solved_by_transformation"] = int("Task solved without search" in content)
    if "TAUBAKING" in content:
        props["tau_labels"] = sum(int(x) for x in re.findall(r"TAUBAKING round \d+ labels \d+ tau_labels (\d+)", content))
        put("tau_rounds", _last(r"TAUBAKING rounds (\d+)", content))
    put("closure_closed", _last(r"LABELCLOSURE .* closed (\d+)", content))
    # h2 on a copy of the task (driver --h2-information)
    if "h2 information:" in content:
        props["h2_available"] = int("h2 information: written to" in content)
        props["h2_timeout"] = int("h^mutexes could not be computed" in content)
    put("h2_removed_operators", _last(r"H2INFO .* removed_operators (\d+)", content))
    put("mutex_pairs", _last(r"BDDSTAT state_mutexes source \S+ .*?mutex_pairs (\d+)", content))
    put("mutex_unreachable_facts", _last(r"BDDSTAT state_mutexes source \S+ .*?unreachable_facts (\d+)", content))
    props["mutexes_skipped"] = int("STATEMUTEXES skipped" in content)
    put("dead_labels", _last(r"DEADLABELS source \S+ labels \d+ dead (\d+)", content))
    props["dead_labels_skipped"] = int("DEADLABELS skipped" in content)
    steps = re.findall(r"^STEP .*?conflicts (\d+) decisions (\d+)", content, re.M)
    if steps:
        props["sat_conflicts"] = sum(int(c) for c, _ in steps)
        props["sat_decisions"] = sum(int(d) for _, d in steps)


TRANSFORMATION_ATTRIBUTES = [
    "fts_factors", "fts_labels", "fts_facts", "fts_transitions", "solved_by_transformation",
    "tau_labels", "tau_rounds", "closure_closed", "h2_available", "h2_timeout",
    "h2_removed_operators", "mutex_pairs", "mutex_unreachable_facts", "mutexes_skipped",
    "dead_labels", "dead_labels_skipped", "sat_conflicts", "sat_decisions",
]


class EncodingSizeParser(Parser):
    def __init__(self):
        Parser.__init__(self)
        self.add_function(add_encoding_size)
        self.add_function(add_bdd_construction)
        self.add_function(add_label_order)
        self.add_function(add_transformation_info)


if __name__ == "__main__":
    EncodingSizeParser().parse()
