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


# TODO: this currently uses coverage and sum as the hard-coded attribute and
# aggregation function.
# NOTE: hacked to consider same domains of different years as one domain.
class PersonalizedTableReport(PlanningReport):
    def __init__(self, algo_to_print = {}, columns = [], filter_run= None, domain_atr ="domain",  **kwargs):
        PlanningReport.__init__(self, **kwargs)
        self.algo_to_print = algo_to_print
        self.filter_run = filter_run

        self.columns = columns
        self.attributes = set ([c.attribute for c in self.columns])
        self.domain_atr = domain_atr

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
        domain_and_algo_to_data = defaultdict(int)
        instances_with_data = defaultdict(int)
        for atr in self.attributes: 
            for (domain, problem), runs in self.problem_runs.items():
                for run in runs:
                    if self.filter_run and self.filter_run(run):
                        continue
                    if not run['algorithm'] in self.algorithms or not atr in run or not run[atr]:
                        continue
                    domains.add(run[self.domain_atr])
                    domain_and_algo_to_data[(run[self.domain_atr],  domain + problem, atr, run['algorithm'])] = run[atr]
                    instances_with_data [(run[self.domain_atr], domain+problem)] += 1

        selected_instances = set([(domain, problem) for (domain, problem) in instances_with_data if instances_with_data [(domain, problem)] == len(self.algorithms)*len(self.attributes)])

        selected_domains = set([domain_mapping(domain) for (domain, problem) in selected_instances])

        print ("Selected instances: {} in {} domains.".format( len(selected_instances), len(selected_domains)))

        print (f"Selected domains: {[d for d in selected_domains]}")

        print (f"Ommited domains: {[d for d in domains if not d in selected_domains]}")
               
        # end for

        lines = ["% Selected instances: {} in {} domains.".format( len(selected_instances), len(selected_domains)),
                 f"% Selected domains: {[d for d in selected_domains]}",
                 f"% Ommited domains: {[d for d in domains if not d in selected_domains]}",
                turn_list_into_table_row(['% '] + [col.header for col in self.columns])]

        for algo1 in self.algorithms:
            line = []
            if not algo1 in self.algo_to_print:
                continue
            line.append(format_algo(algo1))
            for column in self.columns:
                line.append(column(domain_and_algo_to_data, selected_instances, algo1))
            # aggregated value column
            lines.append(turn_list_into_table_row(line))
        return '\n'.join(lines)



class ColumnCompare:
    def __init__(self, _header, _attribute, _algo_map, _comparison, _printList=False):
        self.header = _header
        self.attribute = _attribute
        self.algo_map = _algo_map
        self.comparison = _comparison
        self.printList = _printList

    def __call__(self, domain_and_algo_to_data, selected_instances, algo1):
        algo2 = self.algo_map(algo1)
        domains_algo1_better = set()
        num_algo1_better = 0
        for (domain, problem) in selected_instances:
            mapped_domain = domain_mapping(domain)
            value1 = domain_and_algo_to_data[(domain, problem, self.attribute, algo1)]
            value2 = domain_and_algo_to_data[(domain, problem, self.attribute, algo2)]
            try:
                if self.comparison(value1, value2):
                    domains_algo1_better.add(mapped_domain)
                    num_algo1_better += 1
            except:
                print (f"Warning comparing {algo2} and {algo1} for domain {domain}, {problem}: unexpected that {value2} is better than {value1}")
                #exit()
            num_domains_algo1_better = len(domains_algo1_better)

        if self.printList:
            print (domains_algo1_better)
            

        return "--" if algo1 == algo2 else "{} ({})".format(num_algo1_better, num_domains_algo1_better)
