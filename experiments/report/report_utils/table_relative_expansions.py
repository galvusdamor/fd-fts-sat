from report_utils.personalized_table import PersonalizedTableReport, ColumnCompare
from collections import OrderedDict


def better_by_one(x, y):
    assert (x <= y)
    return x < y

def better_by_factor2(x, y):
    assert (x <= y)
    return x*2 < y

def better_by_factor10(x, y):
    assert (x <= y)
    return x*10 < y

def get_table_relative_expansions(algo_to_print : OrderedDict, attribute='expansions_until_last_jump'):
    algo_list = list(algo_to_print.keys())
    def previous(x):
        return algo_list[algo_list.index(x) - 1]

    first_algo, name_first_algo = algo_to_print.popitem(last=False)

    return PersonalizedTableReport(
        filter_algorithm=algo_list,
        # filter_run=(lambda x :  x["algorithm"] == "blind" and ("expansions_until_last_jump" not in x or x["expansions_until_last_jump"] < 1000)),
        columns=[ColumnCompare(f"$>$ {name_first_algo}", attribute, lambda x: first_algo, better_by_one),
                 ColumnCompare(f"$>$ {name_first_algo} x 2", attribute, lambda x: first_algo, better_by_factor2),
                 ColumnCompare(f"$>$ {name_first_algo} x 10", attribute, lambda x: first_algo, better_by_factor10),
                 ColumnCompare("$>$ -1", attribute, previous, better_by_one),
                 ColumnCompare("$>$ -1 x2", attribute, previous, better_by_factor2),
                 ColumnCompare("$>$ -1 x10", attribute, previous, better_by_factor10),
                 ],
        algo_to_print=algo_to_print,
        format='tex',
        domain_atr='domain_category'
    )
#
#
# def get_table_relative_expansions(algo_list : list, algo_to_print : dict, attribute='expansions_until_last_jump'):
#     def previous(x):
#         return algo_list[algo_list.index(x) - 1]
#
#
#     first_algo = algo_list[0]
#
#     return PersonalizedTableReport(
#         filter_algorithm=algo_list,
#         # filter_run=(lambda x :  x["algorithm"] == "blind" and ("expansions_until_last_jump" not in x or x["expansions_until_last_jump"] < 1000)),
#         columns=[ColumnCompare(f"$>$ {algo_to_print[first_algo]}", attribute, lambda x: first_algo, better_by_one),
#                  ColumnCompare(f"$>$ {algo_to_print[first_algo]} x 2", attribute, lambda x: first_algo, better_by_factor2),
#                  ColumnCompare(f"$>$ {algo_to_print[first_algo]} x 10", attribute, lambda x: first_algo, better_by_factor10),
#                  ColumnCompare("$>$ -1", attribute, previous, better_by_one),
#                  ColumnCompare("$>$ -1 x2", attribute, previous, better_by_factor2),
#                  ColumnCompare("$>$ -1 x10", attribute, previous, better_by_factor10),
#                  ],
#         algo_to_print=algo_to_print,
#         format='tex'
#     )