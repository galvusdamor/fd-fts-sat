from downward.reports import PlanningReport

from collections import defaultdict


class TotalCoverageTable(PlanningReport):
    def __init__(self, algo_to_print = {},  **kwargs):
        PlanningReport.__init__(self, **kwargs)
        self.algo_to_print = algo_to_print

    def get_text(self):
        def turn_list_into_table_row(line):
            result = ''
            for index, value in enumerate(line):
                result += '{}'.format(value)
                if index == len(line) - 1:
                    result += ' \\\\'
                else:
                    result += ' & '
            return result

        def format_algo(algo):
            if algo in self.algo_to_print:
                return self.algo_to_print[algo]
            return algo

        algorithm_to_coverage = defaultdict(int)
        for (domain, problem), runs in self.problem_runs.items():
            for run in runs:
                if run['coverage'] == 1: 
                    algorithm_to_coverage[run['algorithm']] += 1

        return '\n'.join([f"{a}: {v}" for (a, v) in algorithm_to_coverage.items()])
        
