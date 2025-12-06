from downward.reports import PlanningReport

from collections import defaultdict


class MyTable(PlanningReport):
    def __init__(self, rows, columns, get_name_f, algo_to_print = {}, **kwargs):
        PlanningReport.__init__(self, **kwargs)
        self.algo_to_print = algo_to_print
        self.columns = columns
        self.rows = rows
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

        algorithm_to_coverage = defaultdict(int)
        for (domain, problem), runs in self.problem_runs.items():
            for run in runs:
                if run['coverage'] == 1: 
                    algorithm_to_coverage[run['algorithm']] += 1

                    
        def format_result(algorithm_to_coverage, conf):
            #print(f"{conf}  {algorithm_to_coverage}")
            if conf in algorithm_to_coverage:
                return algorithm_to_coverage[conf]
            # print (conf, algorithm_to_coverage)
            if "chains" in conf and not ("slf" in conf):
                return "X"
            else:
                return "--"


        
        row_content = [turn_list_into_table_row([format_algo(row)] + [format_result(algorithm_to_coverage, self.get_name_f(row, col))  for col in self.columns]) for row in self.rows]

        return '\n'.join([turn_list_into_table_row([""] + [format_algo(col) for col in self.columns])] +  row_content)
        

