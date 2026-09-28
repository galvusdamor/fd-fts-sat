"""AbsoluteReport with coverage, IPC agile score and PAR-2, each plain and normalised.

The scores are derived from each run's ``coverage`` and runtime when the
report is generated, so they need no parser: regenerating the report of an
existing eval directory is enough, and it also works for eval directories
that were fetched together from several experiments.

Every score is a per-instance value summed over the instances of a domain
(domain rows) and over all domains (bottom row). The ``_norm`` variants weigh
every domain the same: an instance's value is divided by n, the number of
instances of its domain in the report, so a domain row is the domain average.

* ``coverage``: solved instances, summed (Lab's own attribute).
  ``coverage_norm``: 1/n per solved instance; a domain is worth at most 1 and
  the bottom row is the number of "domain equivalents" solved.
* ``score_agile``: IPC 2018 agile score, 1 for a plan within 1s, 0 for none,
  1 - log(t)/log(T) in between, with T the run's time limit. Summed.
  ``score_agile_norm``: the same divided by n, so a domain is worth at most 1.
* ``par2``: the runtime if solved, 2*T otherwise, divided by N, the number
  of instances in the report: the bottom row is the classic PAR-2 (mean over
  all instances), a domain row that domain's share of it.
  ``par2_norm``: the runtime or 2*T divided by n: a domain row is the
  domain's PAR-2, the bottom row the sum of those.

The runtime is ``planner_time`` (wall clock of the whole planner) when the
report has it for any run, and ``total_time`` otherwise; FTS-benchmark runs
have no translator step and no ``planner_time``. A solved run without a
runtime gets neither time score rather than a wrong one. T comes from the run's
``--overall-time-limit`` driver option, else ``planner_time_limit``, else
``default_time_limit``.

Scripts use it in place of downward's report::

    from score_report import AbsoluteReport
"""

import logging
import math
import re
from collections import defaultdict

from downward.reports.absolute import AbsoluteReport as _DownwardAbsoluteReport
from lab.reports import Attribute


SCORE_ATTRIBUTES = [
    Attribute("coverage", absolute=True, min_wins=False, function=sum),
    Attribute("coverage_norm", absolute=True, min_wins=False, function=sum),
    Attribute("score_agile", absolute=True, min_wins=False, function=sum),
    Attribute("score_agile_norm", absolute=True, min_wins=False, function=sum),
    Attribute("par2", absolute=True, min_wins=True, function=sum),
    Attribute("par2_norm", absolute=True, min_wins=True, function=sum),
]

_UNITS = {"s": 1, "m": 60, "h": 3600}


def _parse_time_limit(value):
    match = re.fullmatch(r"(\d+(?:\.\d+)?)([smh]?)", str(value).strip())
    if not match:
        return None
    return float(match.group(1)) * _UNITS[match.group(2) or "s"]


def run_time_limit(run, default):
    options = run.get("driver_options") or []
    for flag, value in zip(options, options[1:]):
        if flag == "--overall-time-limit":
            limit = _parse_time_limit(value)
            if limit:
                return limit
    return run.get("planner_time_limit") or default


def agile_score(solved, time, limit):
    if not solved or time is None or time > limit:
        return 0.0
    if time <= 1:
        return 1.0
    return 1 - math.log(time) / math.log(limit)


def add_scores(props, default_time_limit=1800):
    """Adds the scores except coverage to every run of *props*."""
    instances = defaultdict(set)
    for run in props.values():
        instances[run["domain"]].add(run["problem"])
    num_instances = sum(len(problems) for problems in instances.values())
    time_attr = ("planner_time"
                 if any(run.get("planner_time") is not None for run in props.values())
                 else "total_time")
    for run in props.values():
        solved = run.get("coverage") == 1
        time = run.get(time_attr)
        limit = run_time_limit(run, default_time_limit)
        n = len(instances[run["domain"]])
        run["coverage_norm"] = (1 if solved else 0) / n
        if solved and time is None:
            # e.g. filters.VirtualSat deletes the times of the runs it merges
            logging.warning(f"solved run without {time_attr}, no time scores: {run.get('id')}")
            continue
        run["score_agile"] = agile_score(solved, time, limit)
        run["score_agile_norm"] = run["score_agile"] / n
        penalised = min(time, limit) if solved else 2 * limit
        run["par2"] = penalised / num_instances
        run["par2_norm"] = penalised / n
    return time_attr


class AbsoluteReport(_DownwardAbsoluteReport):
    """downward's AbsoluteReport plus the scores of :func:`add_scores`.

    They are appended to *attributes* unless already listed, and computed
    after the report's filters, so the domain sizes are those of the report.
    An empty *attributes* keeps its meaning (all numerical attributes), which
    then includes the scores.
    """

    def __init__(self, default_time_limit=1800, **kwargs):
        attributes = list(kwargs.pop("attributes", None) or [])
        if attributes:
            names = {str(a) for a in attributes}
            attributes += [a for a in SCORE_ATTRIBUTES if str(a) not in names]
        super().__init__(attributes=attributes, **kwargs)
        self.default_time_limit = default_time_limit

    def _apply_filter(self):
        super()._apply_filter()
        time_attr = add_scores(self.props, self.default_time_limit)
        logging.info(f"score_agile and par2 computed from {time_attr}")
