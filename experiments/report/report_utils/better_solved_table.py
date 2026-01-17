from downward.reports import PlanningReport

from collections import defaultdict


class BetterSolvedTable(PlanningReport):
    def __init__(self, algs, get_name_f, algo_to_print = {}, **kwargs):
        PlanningReport.__init__(self, **kwargs)
        self.algo_to_print = algo_to_print
        self.columns = algs
        self.rows = algs
        self.get_name_f = get_name_f

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

        algorithm_to_solved = defaultdict(list)
        for (domain, problem), runs in self.problem_runs.items():
            for run in runs:
                if run['coverage'] == 1: 
                    algorithm_to_solved[run['algorithm']].append((domain + "#" + problem))

                    
        def format_result(algorithm_to_solved, row, col):
            if row == col:
                return "--"
            better = 0
            for s in algorithm_to_solved[row]:
                if s not in algorithm_to_solved[col]:
                    better += 1
            
            print(f"R: {row} C: {col} Better: {better}")
            return better
            

        
        row_content = [turn_list_into_table_row([format_algo(row)] + [format_result(algorithm_to_solved, self.get_name_f(row), self.get_name_f(col))  for col in self.columns]) for row in self.rows]

        return '\n'.join([turn_list_into_table_row([""] + [format_algo(col) for col in self.columns])] +  row_content)
        

