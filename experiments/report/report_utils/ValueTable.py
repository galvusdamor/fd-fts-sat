from downward.reports import PlanningReport

from collections import defaultdict


class ValueTable(PlanningReport):
    def __init__(self, base, rows, base_prop, columns, algo_to_print = {}, **kwargs):
        PlanningReport.__init__(self, **kwargs)
        self.algo_to_print = algo_to_print
        self.rows = [base] + rows
        self.base = base
        self.base_prop = base_prop
        self.columns = [base_prop] + columns

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

        ##algorithm_to_coverage = defaultdict(int)
        ##for (domain, problem), runs in self.problem_runs.items():
        ##    for run in runs:
        ##        if run['coverage'] == 1: 
        ##            algorithm_to_coverage[run['algorithm']] += 1
    


        def format_number(n):
            return '{:0.2f}%'.format(100*n)
    
        ##            
        def format_result(prop, conf):
            values = []

            for (domain, problem), runs in self.problem_runs.items():
                base_val = -1
                conf_val = -1
                for run in runs:
                    if run['algorithm'] == conf and prop in run:
                        conf_val = run[prop]
                    if run['algorithm'] == self.base and self.base_prop in run:
                        base_val = run[self.base_prop]
                if base_val == -1: continue
                if conf_val == -1: continue

                my_val = 0

                if base_val == 0:
                    my_val = 1
                else:
                    my_val = float(conf_val) / base_val
            
                values = [my_val] + values

                ##print(f"{conf} {self.base} on {prop} : {base_val} {conf_val} -> {my_val}")
            
            if len(values) == 0:
                print(f"{conf} {self.base} on {prop}: 0/0 = 0")
                return 0
            else:
                print(f"{conf} {self.base} on {prop}: {sum(values)}/{len(values)} = {sum(values)/len(values)}")
                return sum(values)/len(values)
            ###print(f"{conf}  {algorithm_to_coverage}")
            ##if conf in algorithm_to_coverage:
            ##    return algorithm_to_coverage[conf]
            ### print (conf, algorithm_to_coverage)
            ##if (("chains" in conf) or ("slflpp" in conf)) and not ("slf_" in conf):
            ##    return "X"
            ##else:
            ##    return "--"


        
        row_content = [turn_list_into_table_row([format_algo(row)] + [format_number(format_result(col, row)) for col in self.columns] + [format_number(sum([format_result(col, row)  for col in self.columns]))]) for row in self.rows]
        return '\n'.join([turn_list_into_table_row([""] + [prop for prop in self.columns] + ["\Sigma"])] + row_content)
        

