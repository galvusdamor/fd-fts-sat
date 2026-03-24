from downward.reports import PlanningReport

from collections import defaultdict


def domain_mapping(domain):
    if domain == 'openstacks':
        return 'openstacks-06'
    elif 'openstacks' in domain:
        return 'openstacks-08-11-14'
    elif 'logistics' in domain:
        return 'logistics'
    else:
        result = domain.replace('-adl', '')
        result = domain.replace('-strips', '')
        result = result.replace('-08', '')
        result = result.replace('-opt08', '')
        result = result.replace('-opt11', '')
        result = result.replace('-opt14', '')
        result = result.replace('-opt18', '')
        result = result.replace('-sat08', '')
        result = result.replace('-sat11', '')
        result = result.replace('-sat14', '')
        result = result.replace('-sat18', '')
        return result



class DomainCoverageTable(PlanningReport):
    def __init__(self, algs, get_name_f, algo_to_print = {}, **kwargs):
        PlanningReport.__init__(self, **kwargs)
        self.algo_to_print = algo_to_print
        self.columns = algs
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



        domains = set()
        domain_and_instances = defaultdict(set)
        domain_algo_to_solved = defaultdict(int)
        for (domain, problem), runs in self.problem_runs.items():
            d = domain_mapping(domain)
            domains.add(d)
            domain_and_instances[d].add(domain+problem)
            print(f"\nD {domain} {d} {problem}")
            for run in runs:
                if not run['algorithm'] in self.algorithms or not "coverage" in run:
                    continue
                if run['coverage'] == 1:
                    print(f"{d + run['algorithm']} set to {domain_algo_to_solved[d + run['algorithm']]}")
                    domain_algo_to_solved[d + run['algorithm']] += 1

                    
        def format_result(domain_algo_to_solved, row, col):
            print(f"R: {row} C: {col} Sol: {domain_algo_to_solved[row+col]}")
            return domain_algo_to_solved[row+col]

        
        row_content = [turn_list_into_table_row([row, len(domain_and_instances[row])] + [format_result(domain_algo_to_solved, row, self.get_name_f(col)) for col in self.columns]) for row in domains] + [turn_list_into_table_row(["\Sigma", sum([len(domain_and_instances[row]) for row in domains])] + [sum([format_result(domain_algo_to_solved, row, self.get_name_f(col)) for row in domains])  for col in self.columns])]

        return '\n'.join([turn_list_into_table_row(["","\Sigma"] + [format_algo(col) for col in self.columns])] +  row_content)
        

