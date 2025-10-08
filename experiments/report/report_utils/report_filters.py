def ignore_unexplained_errors(run):
    def ignore_error(error):
        for x in ['planner failed to log peak memory', 'run.err: warning: could not determine peak memory',
                  'Found multiple occurences of Total time', 'planner finished and wrote', 'BDDError', 'MemoryError', 'planner wall-clock time:','exitcode--15', 'planner exit code:'
                  'cannot allocate memory', 'Fatal glibc error: malloc', 'SystemError: error return without exception set',
                  'rm-tmp-files.py',
                  'planner wrote',
                  'output-to-slurm.err','exitcode',
                  'out-of-memory','driver.log',
                  'exitcode-250']:
            if x in error:
                return True
        return False

    if "unexplained_errors" in run:
        #run['ignored_unexplained_errors'] = [x for x in run['unexplained_errors'] if ignore_error(x)]
        run['unexplained_errors'] = [x for x in run['unexplained_errors'] if not ignore_error(x)]
        if run['unexplained_errors']:
            print(run['unexplained_errors'])
    return run

def invert_min_negative_dominance(run):
    if "min_negative_dominance" in run:
        run ["min_negative_dominance_inverted"] = -run ["min_negative_dominance"]
    return run

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


def unsolvable_wo_mystery(run):
    if "unsolvable" in run:
            if run["domain"] == "mystery":
                run["unsolvable_wo_mystery"] = 0
            else:
                run["unsolvable_wo_mystery"] = run["unsolvable"]
                if run["unsolvable"] == 1:
                    print ("Warning: unsolvable instance {} {} by {} {} ".format(run["domain"], run["problem"], run["algorithm"], run["commandline_config"]))
    return run

def joint_domains(run):
    categories = {
        "1": {
            "nomystery": "nomystery",
            "logistics": "logistics",
            "rovers": "rovers",
            "driverlog": "driverlog"
        },
        "2": {
            "nomystery": "nomystery",
            "logistics": "logistics",
            "rovers": "rovers",
            "driverlog": "driverlog",
            "parcprinter": "parcprinter",
            "visitall": "visitall",
            "floortile": "floortile",
            "zenotravel": "zenotravel",
            "trucks": "trucks",
            "mystery": "mystery"
        },
        "3": {
            "nomystery": "nomystery",
            "rovers": "rovers",
            "tidybot": "tidybot"
        },
        "timesim": {
            "rovers": "rovers",
            "petri-net-alignment": "petri-net-alignment",
            "airport": "airport",
        },

    }

    dm = domain_mapping(run["domain"])
    run["domain_category"] = dm

    for cat in categories:
            category = categories[cat]
            run["domain_category_{}".format(cat)] = (category[dm] if dm in category else "others")
    return run


class FilterAtr:
    def __init__(self, atr):
        self.atr = atr

    def __call__(self, run):
        run.pop(self.atr, None)
        return run
