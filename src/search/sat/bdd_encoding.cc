#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "bdd_encoding.h"

#include "fts_matrix.h"

#include "ipasir.h"
#include "sat_encoder.h"
#include "../task_representation/label_equivalence_relation.h"
#include "../task_utils/label_order_finder.h"
#include "../utils/logging.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"

using namespace std;
using namespace task_representation;


namespace sat_search {

namespace {
struct BDDError {};

/// raised when a construction budget (time or nodes) runs out
struct BDDBudgetExceeded {
	const char * reason;   // "time_limit" or "node_limit"
	int factor;
	const char * where;
	// captured when the budget runs out: unwinding destroys the local BDD
	// vectors, so reading the manager in the handler would under-report.
	double elapsed;
	long live_nodes;
	long peak_nodes;
};

/// wall clock for the whole BDD construction, started by initialize()
utils::Timer bdd_construction_timer;

// promise from symbolic
void exceptionError(string /*message*/) {
	throw BDDError();
}

void exitOutOfMemory(size_t) {
	cerr << "Memory exceeded within BDD operation" << endl;
	utils::exit_with(utils::ExitCode::OUT_OF_MEMORY);
}
}


// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

BDDSATEncodingFactory::BDDSATEncodingFactory(const options::Options &opts):
	SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
	oneStepOnly(opts.get<bool>("one_step_only")),
	combineAllBDDsIntoOne(opts.get<bool>("combinebdds")),
	bddEncodingSizeLimit(opts.get<int>("bdd_size_limit")),
	implicationalTseitsin(opts.get<bool>("impltseitsin")),
	omitForcedVariables(opts.get<bool>("omitforcedvariables")),
	forcedVariablesThreshold(opts.get<int>("forcedvariablesthreshold")),
	bddCutting(opts.get<bool>("cutbdds")),
	bddCovering(opts.get<bool>("coverbdds")),
	reportLabelImplications(opts.get<bool>("report_label_implications")),
	reportFactorStatistics(opts.get<bool>("report_factor_statistics")),
	bddInitTimeLimit(opts.get<int>("bdd_init_time_limit")),
	bddNodeLimit(long(opts.get<int>("bdd_node_limit"))),
	label_order_finder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
	cudd_init_nodes(long(opts.get<int>("cudd_init_nodes"))),
	cudd_init_cache_size(long(opts.get<int>("cudd_cache_size"))),
	cudd_init_available_memory(long(opts.get<int>("cudd_max_memory_mb")) * 1024L * 1024L)
{
	if (oneStepOnly && bddEncodingSizeLimit != -1){
		cerr << "bdd_size_limit only has an effect if one_step_only=false: it interpolates "
			 << "between the full reachability BDDs and the one-step BDDs." << endl;
		utils::exit_with(utils::ExitCode::INPUT_ERROR);
	}
	if (bddCovering && (oneStepOnly || combineAllBDDsIntoOne)){
		cerr << "coverbdds requires one_step_only=false and combinebdds=false." << endl;
		utils::exit_with(utils::ExitCode::INPUT_ERROR);
	}
}


static shared_ptr<SATEncodingFactory> _parse_bdd_sat_factory(options::OptionParser &parser) {
	parser.document_synopsis("BDD-based SAT encoding",
		"For every factor, BDDs over the label variables (in the order given by "
		"label_order) describe which sets of labels move the factor from one state "
		"to another within a single plan step. The BDDs are Tseitin-translated into "
		"CNF once and reused for every time step.");

	parser.add_option<bool>(
		"one_step_only",
		"only allow one real transition per factor and time step (plus self loops "
		"before and after it) instead of arbitrary label sequences",
		"true");

	parser.add_option<bool>(
		"combinebdds",
		"use one BDD per factor over (state, next state, labels) instead of one BDD "
		"per (factor, source, target) triple conditioned on the state variables",
		"false");

	parser.add_option<int>(
		"bdd_size_limit",
		"per-factor budget of BDD nodes. Oversized (source,target) BDDs fall back to "
		"the one-step BDD. -1 means no limit. Requires one_step_only=false",
		"-1");

	parser.add_option<bool>(
		"impltseitsin",
		"use implicational (one-directional) Tseitin encoding instead of a "
		"bi-implicational one",
		"true");

	parser.add_option<bool>(
		"omitforcedvariables",
		"do not create Tseitin variables for BDD nodes whose in-degree is at most "
		"forcedvariablesthreshold",
		"true");

	parser.add_option<int>(
		"forcedvariablesthreshold",
		"in-degree up to which omitforcedvariables applies",
		"100");

	parser.add_option<bool>(
		"cutbdds",
		"fixpoint 'cutting' of the BDDs across factors for stronger constraints",
		"false");

	parser.add_option<bool>(
		"coverbdds",
		"search for always-true/false state -> +-label implications and report them. "
		"Diagnostic only: this does not change the generated formula",
		"false");

	parser.add_option<bool>(
		"force_at_least_one_action",
		"force that every time step contains at least one action",
		"false");

	parser.add_option<bool>(
		"report_label_implications",
		"mine each factor's label BDD for dependencies that hold in every legal "
		"label set (l, -l, l -> l', l -> -l') and report how many there are. "
		"Diagnostic only: it does not change the formula",
		"false");

	parser.add_option<bool>(
		"report_factor_statistics",
		"emit one BDDSTAT line per factor with its sizes and construction time. "
		"Off by default: it is one line per factor and there can be hundreds. The "
		"single construction_ok/construction_failed summary is always emitted",
		"false");

	parser.add_option<int>(
		"cudd_init_nodes",
		"initial size of the Cudd unique table, divided by the number of labels",
		"16000000");

	parser.add_option<int>(
		"cudd_cache_size",
		"initial number of Cudd cache slots. Allocated up front and independent "
		"of task size, so it dominates memory on small tasks",
		"16000000");

	parser.add_option<int>(
		"cudd_max_memory_mb",
		"hard cap on Cudd's memory in MB. 0 lets Cudd decide from the machine's "
		"RAM, which ignores any per-run budget",
		"0");

	parser.add_option<int>(
		"bdd_init_time_limit",
		"seconds allowed for building the BDDs. When it is used up the run stops "
		"with 'BDDSTAT construction_failed ... reason time_limit' instead of "
		"grinding on until the driver kills it. -1 means no limit",
		"-1");

	parser.add_option<int>(
		"bdd_node_limit",
		"how many live Cudd nodes the construction may use, reported the same way. "
		"-1 means no limit",
		"-1");

	parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
		"label_order",
		"order in which labels may be applied within one time step. This is also the "
		"BDD variable order",
		"label_order_linear()");

	options::Options opts = parser.parse();
	if (parser.dry_run())
		return nullptr;
	else
		return make_shared<BDDSATEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin_bdd_sat_factory("bdd_sat", _parse_bdd_sat_factory);


void BDDSATEncodingFactory::bdd_to_dot(const BDD &bdd, const std::string &file_name) const {
	std::vector<string> var_names(data->bdd_num_vars);
	for(int f = 0; f < data->num_factor_vars; f++){
		if (f < data->num_factor_vars / 2)
			var_names[f] = "factor_state_" + to_string(f);
		else
			var_names[f] = "factor_next_state_" + to_string(f - (data->num_factor_vars/2));
	}
	for(int i = 0; i < fts->get_num_labels(); i++)
		var_names[data->num_factor_vars + i] = "label_" + to_string(data->labelOrder[i]);

	std::vector<char *> names(var_names.size());
	for (size_t i = 0; i < var_names.size(); ++i)
		names[i] = &var_names[i].front();

	FILE *outfile = fopen(file_name.c_str(), "w");
	DdNode **ddnodearray = (DdNode **)malloc(sizeof(bdd.Add().getNode()));
	ddnodearray[0] = bdd.Add().getNode();
	Cudd_DumpDot(_manager->getManager(), 1, ddnodearray, names.data(), NULL, outfile);
	free(ddnodearray);
	fclose(outfile);
}


const FTSMatrix & BDDSATEncodingFactory::matrix_for(int fac) const {
	if (!fts_matrices[fac])
		fts_matrices[fac] = make_shared<FTSMatrix>(fts->get_ts(fac));
	return *fts_matrices[fac];
}


void BDDSATEncodingFactory::check_budget(int fac, const char * where) const {
	if (bddInitTimeLimit >= 0 &&
			bdd_construction_timer() > double(bddInitTimeLimit))
		throw BDDBudgetExceeded{"time_limit", fac, where, bdd_construction_timer(),
			_manager->ReadNodeCount(), _manager->ReadPeakNodeCount()};
	if (bddNodeLimit >= 0 && _manager->ReadNodeCount() > bddNodeLimit)
		throw BDDBudgetExceeded{"node_limit", fac, where, bdd_construction_timer(),
			_manager->ReadNodeCount(), _manager->ReadPeakNodeCount()};
}


void BDDSATEncodingFactory::bdd_in_degree(DdNode * node){
	// first handle edge cases
	if (Cudd_IsConstant(node)) return;

	// this node is branching
	DdNode* true_branch = Cudd_T(node);
	DdNode* false_branch = Cudd_E(node);
	if (implicationalTseitsin && Cudd_IsComplement(node)) {
		true_branch = Cudd_Not(true_branch);
		false_branch = Cudd_Not(false_branch);
	}

	// compute lookup node. For the biimplicational encoding we only keep one copy,
	// for tseitsin we need both positive and negative versions
	DdNode * lookup;
	if (implicationalTseitsin) lookup = node; else lookup = Cudd_Regular(node);

	if (data->node_indegree.count(lookup)){
		data->node_indegree[lookup]++;
		return;
	}

	data->node_indegree[lookup]++;
	bdd_in_degree(true_branch);
	bdd_in_degree(false_branch);
}


/**
 * Propagate constraints between factors until a fixpoint is reached.
 *
 * For a pair of factors (source, target) the labels that are irrelevant for the
 * target are projected away from "any transition of source". What remains is a
 * constraint over the labels both factors share, and it can be conjoined onto
 * every transition BDD of the target.
 */
void BDDSATEncodingFactory::cut_bdds_to_fixpoint(){
	BDD stateCube = _manager->bddOne();
	for (int i = 0; i < data->num_factor_vars; i++) stateCube *= _manager->bddVar(i);

	int round = 0;
	bool anyUpdate = true;
	while (anyUpdate) {
		cout << "Propagation Round " << round << flush;
		anyUpdate = false;
		round++;

		// 1. a BDD per factor describing any legal transition in that factor
		vector<BDD> any_transition_per_factor(fts->get_size());
		for (int fac = 0; fac < fts->get_size(); fac++){
			if (!combineAllBDDsIntoOne){
				const TransitionSystem & factor = fts->get_ts(fac);
				any_transition_per_factor[fac] = _manager->bddZero();
				for (int s = 0; s < factor.get_size(); s++)
					for (int ss = 0; ss < factor.get_size(); ss++)
						any_transition_per_factor[fac] +=
							data->transition_BDDs_per_factor_per_state_pair[fac][s][ss];
			} else {
				// project away the state variables.
				any_transition_per_factor[fac] =
					data->transition_BDDs_per_factor[fac].ExistAbstract(stateCube);
			}
		}

		// 2. Go over all pairs of factors
		for (int facS = 0; facS < fts->get_size(); facS++){
			const TransitionSystem & factorSource = fts->get_ts(facS);
			for (int facT = 0; facT < fts->get_size(); facT++){
				if (facS == facT) continue;
				const TransitionSystem & factorTarget = fts->get_ts(facT);

				// cube for the variables that need to be abstracted away
				BDD cube = _manager->bddOne();
				bool foundRemainingVariable = false;
				for(int label = 0; label < fts->get_num_labels(); label++){
					LabelID labelID (label);
					// will not be mentioned in this BDD anyway
					if (!factorSource.is_relevant_label(labelID) && factorSource.is_selfloop_everywhere(labelID)) continue;
					// don't project away labels that *are* relevant
					if (factorTarget.is_relevant_label(labelID) || !factorTarget.is_selfloop_everywhere(labelID)) {
						foundRemainingVariable = true;
						continue;
					}
					cube *= _manager->bddVar(data->labelToBDDVar[label]);
				}

				// no shared variables
				if (!foundRemainingVariable) continue;

				BDD constraintsOverLabelsRelevantForTarget = any_transition_per_factor[facS].ExistAbstract(cube);

				// if the relevant BDD is 1, then there is nothing to propagate.
				if (constraintsOverLabelsRelevantForTarget == _manager->bddOne()) continue;

				// actually propagate
				if (!combineAllBDDsIntoOne){
					for (int s = 0; s < factorTarget.get_size(); s++){
						for (int ss = 0; ss < factorTarget.get_size(); ss++){
							BDD & currentMemory = data->transition_BDDs_per_factor_per_state_pair[facT][s][ss];
							BDD old = currentMemory;
							currentMemory *= constraintsOverLabelsRelevantForTarget;
							if (old != currentMemory) anyUpdate = true;
						}
					}
				} else {
					BDD old = data->transition_BDDs_per_factor[facT];
					data->transition_BDDs_per_factor[facT] *= constraintsOverLabelsRelevantForTarget;
					if (old != data->transition_BDDs_per_factor[facT]) anyUpdate = true;
				}
			}
		}
		cout << " completed with " << (anyUpdate?"some reduction. Continuing": "no reduction. Reached fixpoint.") << endl;
	}
}


/**
 * Report label/state implications that hold in every transition of a factor.
 *
 * This is a diagnostic only: it prints what it finds and does not change the
 * BDDs or the generated formula. It was written to judge whether such
 * implications are frequent enough to be worth encoding separately.
 */
void BDDSATEncodingFactory::report_covering_implications() const {
	long claims_verified = 0, claims_refuted = 0;
	for (int fac = 0; fac < fts->get_size(); fac++){
		const TransitionSystem & factor = fts->get_ts(fac);
		const vector<vector<BDD>> & allPossiblePaths = data->transition_BDDs_per_factor_per_state_pair[fac];

		map<int, vector<int>> prev_state_implies_pos_label, prev_state_implies_neg_label;
		map<int, vector<int>> next_state_implies_pos_label, next_state_implies_neg_label;
		map<int, vector<int>> pos_label_implies_prev_state, pos_label_implies_next_state;

		for(int label = 0; label < fts->get_num_labels(); label++){
			LabelID labelID (label);
			if (!factor.is_relevant_label(labelID) && factor.is_selfloop_everywhere(labelID)) continue;

			vector<int> prev_states_implying_this_neg;
			vector<int> next_states_implying_this_neg;

			for (int mode = 0; mode < 2; mode++){
				bool m = mode == 0;
				BDD testBDD = _manager->bddVar(data->labelToBDDVar[label]);
				if (!m) testBDD = !testBDD;

				// source
				for (int s = 0; s < factor.get_size(); s++){
					bool isFalse = true;
					for (int ss = 0; ss < factor.get_size(); ss++)
						if (testBDD * allPossiblePaths[s][ss] != _manager->bddZero()){ isFalse = false; break; }

					if (isFalse){
						cout << "Relevant label " << label << " is constantly " << (m?"false":"true")
							 << " for source state " << s << " in factor " << fac << endl;
						if (m) { prev_state_implies_neg_label[s].push_back(label); prev_states_implying_this_neg.push_back(s); }
						else     prev_state_implies_pos_label[s].push_back(label);
					}
				}

				// target
				for (int ss = 0; ss < factor.get_size(); ss++){
					bool isFalse = true;
					for (int s = 0; s < factor.get_size(); s++)
						if (testBDD * allPossiblePaths[s][ss] != _manager->bddZero()){ isFalse = false; break; }

					if (isFalse){
						cout << "Relevant label " << label << " is constantly " << (m?"false":"true")
							 << " for target state " << ss << " in factor " << fac << endl;
						if (m) { next_state_implies_neg_label[ss].push_back(label); next_states_implying_this_neg.push_back(ss); }
						else     next_state_implies_pos_label[ss].push_back(label);
					}
				}
			}

			// If all but one state imply that the label is not taken, then taking
			// the label implies being in the remaining state. If *every* state
			// implied it, the label could never be executed at all.
			assert(int(prev_states_implying_this_neg.size()) != factor.get_size());

			auto only_remaining_state = [&](const vector<int> & implying) {
				// the states were inserted in increasing order
				for (int i = 0; i < int(implying.size()); i++)
					if (i != implying[i]) return i;
				return factor.get_size() - 1;
			};

			// The claimed state is the one the others do not rule out, so the
			// label must actually be possible there. Checking that catches a
			// wrong pick, which the derivation itself cannot.
			const BDD labelIsTaken = _manager->bddVar(data->labelToBDDVar[label]);
			if (int(prev_states_implying_this_neg.size()) + 1 == factor.get_size()){
				int s = only_remaining_state(prev_states_implying_this_neg);
				bool possible = false;
				for (int ss = 0; ss < factor.get_size() && !possible; ss++)
					possible = (labelIsTaken * allPossiblePaths[s][ss]) != _manager->bddZero();
				if (possible) claims_verified++; else claims_refuted++;
				pos_label_implies_prev_state[label].push_back(s);
				cout << "Label " << label << " in factor " << fac << " implies source state " << s
					 << (possible ? "" : "   *** REFUTED: label impossible there ***") << endl;
			}
			if (int(next_states_implying_this_neg.size()) + 1 == factor.get_size()){
				int ss = only_remaining_state(next_states_implying_this_neg);
				bool possible = false;
				for (int s = 0; s < factor.get_size() && !possible; s++)
					possible = (labelIsTaken * allPossiblePaths[s][ss]) != _manager->bddZero();
				if (possible) claims_verified++; else claims_refuted++;
				pos_label_implies_next_state[label].push_back(ss);
				cout << "Label " << label << " in factor " << fac << " implies target state " << ss
					 << (possible ? "" : "   *** REFUTED: label impossible there ***") << endl;
			}
		}
	}
	cout << "BDDSTAT covering_claims verified " << claims_verified
		 << " refuted " << claims_refuted << endl;
}


/**
 * All label sequences, in label order, that take the factor from s to ss
 * within one time step.
 *
 * A DP backwards over the label order: having handled the labels after
 * "label", allPossiblePaths[s][ss] holds the sequences over those; the step
 * for "label" either skips it or uses it once and continues from its target.
 * Labels that are irrelevant for this factor are skipped, so the cost is one
 * pass per *relevant* label, each pass touching |S|^2 entries.
 */
vector<vector<BDD>> BDDSATEncodingFactory::build_full_reachability_bdds(
		int fac, const TransitionSystem & factor, int & num_relevant_labels){
	const int numStates = factor.get_size();

	vector<vector<BDD>> paths(numStates);
	for (int s = 0; s < numStates; s++){
		paths[s].resize(numStates);
		for (int ss = 0; ss < numStates; ss++)
			paths[s][ss] = (s == ss) ? _manager->bddOne() : _manager->bddZero();
	}

	for(int l = fts->get_num_labels() - 1; l >= 0; l--){
		int label = data->labelOrder[l];
		LabelID labelID (label);
		if (!factor.is_relevant_label(labelID) && factor.is_selfloop_everywhere(labelID))
			continue;
		num_relevant_labels++;
		check_budget(fac, "full_reachability_dp");

		const BDD labelVar = _manager->bddVar(data->labelToBDDVar[label]);
		vector<vector<BDD>> next (numStates);
		for (int s = 0; s < numStates; s++){
			next[s].resize(numStates);
			for (int ss = 0; ss < numStates; ss++){
				// either we skip this label ...
				next[s][ss] = ~labelVar * paths[s][ss];
				// ... or we use it once and carry on from its target
				for (const auto & transition : factor.get_transitions_with_label(label)){
					if (transition.src != s) continue;
					next[s][ss] += labelVar * paths[transition.target][ss];
				}
			}
		}
		swap(paths, next);
	}
	return paths;
}


/**
 * The label sets that take the factor from s to ss using at most one real
 * transition, framed by self loops: labels earlier in the label order must
 * self-loop on the source, later ones on the target, and anything that cannot
 * self-loop there has to be false.
 *
 * "Which labels self-loop in state s" is exactly FTSMatrix's
 * self_loops_for_state, so we take it from there instead of rescanning every
 * label's transition list for every transition. Its sets are keyed by label
 * group, which is what we want anyway: labels in one group have identical
 * transitions in this factor, so they self-loop in the same states.
 */
vector<vector<BDD>> BDDSATEncodingFactory::build_one_step_bdds(
		int fac, const TransitionSystem & factor, const FTSMatrix & matrix){
	const int numStates = factor.get_size();
	const int numLabels = fts->get_num_labels();

	// label -> its label group, to query the per-group sets for a single label
	vector<int> groupOfLabel(numLabels, -1);
	for (int lg = 0; lg < matrix.get_num_label_groups(); lg++)
		for (int label : matrix.get_labels_in_label_group(lg))
			groupOfLabel[label] = lg;

	// Per state, the labels that must be false while the factor sits there,
	// i.e. everything that cannot self-loop in it. Usually a small set, and
	// it is the only thing the inner loop below needs.
	// Built in *label order*, i.e. BDD variable order. That matters: the BDDs
	// below are products over these labels, and conjoining them in variable
	// order keeps the intermediate results a chain. Walking them in label-id
	// order instead builds the same BDD but roughly twice as slowly on the
	// label-rich instances.
	vector<vector<int>> mustBeFalseAt(numStates);
	for (int s = 0; s < numStates; s++){
		const set<int> & loops = matrix.get_self_loop_labels_for_state(s);
		for (int pos = 0; pos < numLabels; pos++){
			const int label = data->labelOrder[pos];
			assert(groupOfLabel[label] >= 0);
			if (!loops.count(groupOfLabel[label]))
				mustBeFalseAt[s].push_back(label);
		}
	}

	// position in the label order, which is also the BDD variable order
	auto orderOf = [&](int label){ return data->labelToBDDVar[label] - data->num_factor_vars; };

	/*
	  Per state, the running conjunctions of "this label is false" over a
	  prefix resp. suffix of the label order:

	    prefixFalse[s][p] = AND over i < p  of ~label_i, for labels that cannot self-loop in s
	    suffixFalse[s][p] = AND over i > p  of ~label_i,        likewise

	  A transition on label l at order position p is then exactly
	      label_l AND prefixFalse[src][p] AND suffixFalse[target][p]
	  which is two BDD operations instead of one per label that has to be
	  forced false. Building these costs O(|S| * |L|) operations once, against
	  O(transitions * labels-forced-false) for doing it per transition -- and
	  the transition count is itself around |L|*|S|, which is what made the
	  label-rich instances hopeless.
	*/
	vector<char> cannotSelfLoop(numLabels);
	vector<vector<BDD>> prefixFalse(numStates), suffixFalse(numStates);
	for (int s = 0; s < numStates; s++){
		check_budget(fac, "one_step_prefix_cubes");
		for (int label = 0; label < numLabels; label++) cannotSelfLoop[label] = 0;
		for (int label : mustBeFalseAt[s]) cannotSelfLoop[label] = 1;

		prefixFalse[s].resize(numLabels + 1);
		prefixFalse[s][0] = _manager->bddOne();
		for (int pos = 0; pos < numLabels; pos++){
			const int label = data->labelOrder[pos];
			prefixFalse[s][pos+1] = cannotSelfLoop[label]
				? prefixFalse[s][pos] * ~_manager->bddVar(data->labelToBDDVar[label])
				: prefixFalse[s][pos];
		}

		suffixFalse[s].resize(numLabels + 1);
		suffixFalse[s][numLabels] = _manager->bddOne();
		for (int pos = numLabels - 1; pos >= 0; pos--){
			const int label = data->labelOrder[pos];
			suffixFalse[s][pos] = cannotSelfLoop[label]
				? suffixFalse[s][pos+1] * ~_manager->bddVar(data->labelToBDDVar[label])
				: suffixFalse[s][pos+1];
		}
	}

	vector<vector<BDD>> paths(numStates);
	for (int s = 0; s < numStates; s++){
		paths[s].resize(numStates);
		for (int ss = 0; ss < numStates; ss++)
			paths[s][ss] = _manager->bddZero();
	}

	// No real transition at all: the factor stays in s and only labels that
	// self-loop there may fire, possibly none. Without this a time step could
	// never be empty, which makes the formula unsatisfiable for every horizon
	// longer than the shortest one.
	for (int s = 0; s < numStates; s++){
		check_budget(fac, "one_step_stay_disjunct");
		paths[s][s] += prefixFalse[s][numLabels];
	}

	// Exactly one real transition.
	for(int l = 0; l < numLabels; l++){
		check_budget(fac, "one_step_construction");
		const int orderL = orderOf(l);
		for (const auto & transition : factor.get_transitions_with_label(l)){
			// self loops on the source before it, on the target after it
			const BDD thisTrans = _manager->bddVar(data->labelToBDDVar[l])
				* prefixFalse[transition.src][orderL]
				* suffixFalse[transition.target][orderL + 1];
			paths[transition.src][transition.target] += thisTrans;
		}
	}
	return paths;
}


/**
 * Mine the factors' label BDDs for dependencies between labels.
 *
 * For a factor, the union over all (source,target) pairs of its transition
 * BDDs describes every label set that factor permits in a single time step.
 * Whatever holds in all of those minterms holds in every plan, so it is a
 * sound clause over the label variables of any time step -- and a binary one
 * is exactly what a SAT solver propagates best.
 *
 * Cudd_FindEssential returns, in one call, the cube of variables that are
 * forced in every satisfying assignment. Applying it to the function itself
 * gives the unit dependencies; applying it to the cofactor by a literal gives
 * every binary dependency with that literal as premise at once. So this costs
 * O(|support|) cofactor+essential calls per factor rather than testing pairs.
 */
void BDDSATEncodingFactory::report_label_implications() const {
	const int nfv = data->num_factor_vars;
	BDD stateCube = _manager->bddOne();
	for (int i = 0; i < nfv; i++) stateCube *= _manager->bddVar(i);

	// BDD variable index -> label (the variable order is the label order)
	auto labelOfVar = [&](int v){ return data->labelOrder[v - nfv]; };

	long units = 0, implications = 0, mutexes = 0;
	int trivial_one = 0, trivial_zero = 0, nontrivial = 0;
	long mutex_verified = 0, mutex_refuted = 0;
	// conditioned on the factor's source state, which is strictly more
	// informative than the union over all state pairs
	long cond_units = 0, cond_mutexes = 0, cond_implications = 0;
	int cond_nontrivial = 0, cond_trivial_one = 0;
	set<pair<int,int>> distinct_mutex;          // unordered label pairs
	set<pair<int,int>> distinct_implication;    // ordered (premise, conclusion)
	set<int> distinct_units;
	int factors_with_info = 0;

	for (int fac = 0; fac < fts->get_size(); fac++){
		// every label set this factor permits in one step
		BDD any = _manager->bddZero();
		if (combineAllBDDsIntoOne){
			any = data->transition_BDDs_per_factor[fac].ExistAbstract(stateCube);
		} else {
			const TransitionSystem & factor = fts->get_ts(fac);
			for (int s = 0; s < factor.get_size(); s++)
				for (int ss = 0; ss < factor.get_size(); ss++)
					any += data->transition_BDDs_per_factor_per_state_pair[fac][s][ss];
		}
		if (any == _manager->bddOne()) { trivial_one++; continue; }
		if (any == _manager->bddZero()) { trivial_zero++; continue; }
		nontrivial++;

		vector<unsigned int> supp = any.SupportIndices();
		int facUnits = 0, facImpl = 0, facMutex = 0;

		// unit dependencies: labels fixed in every legal set
		BDD ess = any.FindEssential();
		for (unsigned int v : ess.SupportIndices()){
			if (int(v) < nfv) continue;
			bool positive = any.IsVarEssential(v, 1);
			facUnits++; distinct_units.insert(positive ? labelOfVar(v) : -labelOfVar(v) - 1);
		}

		// binary dependencies, premise by premise
		for (unsigned int v : supp){
			if (int(v) < nfv) continue;
			const int a = labelOfVar(v);
			for (int phase = 0; phase < 2; phase++){
				BDD lit = phase ? _manager->bddVar(v) : ~_manager->bddVar(v);
				BDD cof = any * lit;
				if (cof == _manager->bddZero()) continue;   // premise impossible: a unit, already counted
				BDD cessential = cof.FindEssential();
				for (unsigned int w : cessential.SupportIndices()){
					if (w == v || int(w) < nfv) continue;
					const int b = labelOfVar(w);
					const bool bPositive = cof.IsVarEssential(w, 1);
					if (phase && !bPositive){
						facMutex++; mutexes++;
						// independent check of what FindEssential told us: the pair
						// really must not be able to occur together
						if ((any * _manager->bddVar(v) * _manager->bddVar(w)) == _manager->bddZero())
							mutex_verified++;
						else
							mutex_refuted++;
						distinct_mutex.insert({min(a,b), max(a,b)});
					} else {
						facImpl++; implications++;
						distinct_implication.insert({phase ? a : -a-1, bPositive ? b : -b-1});
					}
				}
			}
		}
		units += facUnits;
		if (facUnits + facImpl + facMutex) factors_with_info++;

		// Now the same question conditioned on the factor's source state:
		// "given that this factor sits in s, which labels exclude each other?"
		// Those are still sound clauses, just guarded by the state variable.
		if (!combineAllBDDsIntoOne){
			const TransitionSystem & factor = fts->get_ts(fac);
			for (int src = 0; src < factor.get_size(); src++){
				BDD fromSrc = _manager->bddZero();
				for (int ss = 0; ss < factor.get_size(); ss++)
					fromSrc += data->transition_BDDs_per_factor_per_state_pair[fac][src][ss];
				if (fromSrc == _manager->bddOne()) { cond_trivial_one++; continue; }
				if (fromSrc == _manager->bddZero()) continue;
				cond_nontrivial++;

				for (unsigned int v : fromSrc.SupportIndices()){
					if (int(v) < nfv) continue;
					if (fromSrc.IsVarEssential(v, 1) || fromSrc.IsVarEssential(v, 0)) cond_units++;
					BDD cof = fromSrc * _manager->bddVar(v);
					if (cof == _manager->bddZero()) continue;
					for (unsigned int w : cof.FindEssential().SupportIndices()){
						if (w == v || int(w) < nfv) continue;
						if (cof.IsVarEssential(w, 1)) cond_implications++; else cond_mutexes++;
					}
				}
			}
		}

		if (facUnits + facImpl + facMutex)
			cout << "BDDSTAT implications factor " << fac
				 << " support " << supp.size()
				 << " units " << facUnits
				 << " mutexes " << facMutex
				 << " other_binary " << facImpl << endl;
	}

	cout << "BDDSTAT implications_shape unconditional_trivially_true " << trivial_one
		 << " unconditional_empty " << trivial_zero
		 << " unconditional_informative " << nontrivial
		 << " per_source_trivially_true " << cond_trivial_one
		 << " per_source_informative " << cond_nontrivial
		 << endl;
	cout << "BDDSTAT implications_per_source units " << cond_units
		 << " mutexes " << cond_mutexes
		 << " other_binary " << cond_implications
		 << endl;
	cout << "BDDSTAT implications_total factors " << fts->get_size()
		 << " factors_with_info " << factors_with_info
		 << " units " << units
		 << " distinct_units " << distinct_units.size()
		 << " mutexes " << mutexes
		 << " distinct_mutexes " << distinct_mutex.size()
		 << " other_binary " << implications
		 << " distinct_other_binary " << distinct_implication.size()
		 << " labels " << fts->get_num_labels()
		 << " mutex_verified " << mutex_verified
		 << " mutex_refuted " << mutex_refuted
		 << " extraction_time " << bdd_construction_timer()
		 << endl;
}


void BDDSATEncodingFactory::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	data = make_shared<BDDEncodingData>();
	data->combineAllBDDsIntoOne = combineAllBDDsIntoOne;
	data->implicationalTseitsin = implicationalTseitsin;
	data->omitForcedVariables = omitForcedVariables;
	data->forcedVariablesThreshold = forcedVariablesThreshold;

	fts_matrices.assign(fts->get_size(), nullptr);

	data->labelOrder = label_order_finder->find_order(*fts);
	assert(int(data->labelOrder.size()) == fts->get_num_labels());

	data->num_factor_vars = 0;
	if (combineAllBDDsIntoOne) {
		for (int fac = 0; fac < fts->get_size(); fac++){
			const TransitionSystem & factor = fts->get_ts(fac);
			if (data->num_factor_vars < factor.get_size()) data->num_factor_vars = factor.get_size();
		}
		// state variables and next-state variables, they come first
		data->num_factor_vars *= 2;
	}
	data->bdd_num_vars = data->num_factor_vars + fts->get_num_labels();

	// The BDD variable order *is* the label order.
	data->labelToBDDVar.resize(fts->get_num_labels());
	for (int i = 0; i < fts->get_num_labels(); i++)
		data->labelToBDDVar[data->labelOrder[i]] = data->num_factor_vars + i;

	cout << "Number BDD vars: " << data->bdd_num_vars << " of that "
		 << data->num_factor_vars << " factor state variables." << endl;

	_manager = std::make_unique<Cudd> (data->bdd_num_vars, 0,
			cudd_init_nodes / max(1, fts->get_num_labels()),
			cudd_init_cache_size,
			cudd_init_available_memory);

	bdd_construction_timer.reset();
	bdd_construction_timer.resume();

	_manager->setHandler(exceptionError);
	_manager->setTimeoutHandler(exceptionError);
	_manager->setNodesExceededHandler(exceptionError);
	_manager->RegisterOutOfMemoryCallback(exitOutOfMemory);

	long total_nodes_sum = 0;
	long total_nodes_after_limit = 0;
	int overall_max_pair_nodes = 0;
	double construction_seconds = 0.0;

	try {
		if (combineAllBDDsIntoOne) data->transition_BDDs_per_factor.resize(fts->get_size());
		else data->transition_BDDs_per_factor_per_state_pair.resize(fts->get_size());

		for (int fac = 0; fac < fts->get_size(); fac++){
			const TransitionSystem & factor = fts->get_ts(fac);
			const int numStates = factor.get_size();
			int num_relevant_labels = 0;
			check_budget(fac, "factor_start");
			const double factor_t_start = bdd_construction_timer();
			const long nodes_before_factor = _manager->ReadNodeCount();

			// -----------------------------------------------------------------
			// the two transition relations (see the header for what they mean)
			// -----------------------------------------------------------------
			vector<vector<BDD>> allPossiblePaths;
			if (!oneStepOnly)
				allPossiblePaths = build_full_reachability_bdds(fac, factor, num_relevant_labels);

			vector<vector<BDD>> oneStepPaths;
			if (oneStepOnly || bddEncodingSizeLimit != -1)
				oneStepPaths = build_one_step_bdds(fac, factor, matrix_for(fac));

			// -----------------------------------------------------------------
			// pick the representation that is actually encoded
			// -----------------------------------------------------------------
			vector<vector<BDD>> & chosen = oneStepOnly ? oneStepPaths : allPossiblePaths;

			int summedSizeBefore = 0, summedSizeAfter = 0, possibleSingleTrans = 0, allTrans = 0;
			int maxPairNodes = 0;
			map<int,vector<pair<int,int>>> bdd_sizes;
			for (int s = 0; s < numStates; s++){
				for (int ss = 0; ss < numStates; ss++){
					if (chosen[s][ss] != _manager->bddZero()){
						allTrans++;
						int thisBDDsize = chosen[s][ss].nodeCount();
						if (thisBDDsize > maxPairNodes) maxPairNodes = thisBDDsize;
						summedSizeBefore += thisBDDsize;
						bdd_sizes[thisBDDsize].push_back({s,ss});
					}
					if (!oneStepPaths.empty() && oneStepPaths[s][ss] != _manager->bddZero())
						possibleSingleTrans++;
				}
			}

			// replace the largest BDDs by their one-step counterpart until the
			// per-factor node budget is met
			if (bddEncodingSizeLimit != -1){
				int size_up_to_now = 0;
				for (const auto & thisSizeBDDs : bdd_sizes){
					for (const pair<int,int> & s_ss : thisSizeBDDs.second){
						const int & s = s_ss.first;
						const int & ss = s_ss.second;
						if (size_up_to_now + thisSizeBDDs.first > bddEncodingSizeLimit)
							chosen[s][ss] = oneStepPaths[s][ss];
						else
							size_up_to_now += thisSizeBDDs.first;
						summedSizeAfter += chosen[s][ss].nodeCount();
					}
				}
			}

			cout << "Factor Overall: before limiting " << summedSizeBefore
				 << " after limiting " << summedSizeAfter
				 << " all transitions: " << allTrans
				 << " possible 1-step transitions: " << possibleSingleTrans << endl;

			// one machine-readable line per factor
			if (reportFactorStatistics)
			cout << "BDDSTAT factor " << fac
				 << " states " << numStates
				 << " relevant_labels " << num_relevant_labels
				 << " nonzero_pairs " << allTrans
				 << " onestep_pairs " << possibleSingleTrans
				 << " nodes_sum " << summedSizeBefore
				 << " nodes_max_pair " << maxPairNodes
				 << " nodes_sum_after_limit " << summedSizeAfter
				 << " live_nodes_delta " << (_manager->ReadNodeCount() - nodes_before_factor)
				 << " time " << (bdd_construction_timer() - factor_t_start)
				 << endl;
			total_nodes_sum += summedSizeBefore;
			total_nodes_after_limit += summedSizeAfter;
			if (maxPairNodes > overall_max_pair_nodes) overall_max_pair_nodes = maxPairNodes;

			if (combineAllBDDsIntoOne){
				// one BDD describing all transitions of this factor at once
				BDD allTransitionsBDD = _manager->bddZero();
				const int halfFactorVars = data->num_factor_vars / 2;
				for (int s = 0; s < numStates; s++){
					check_budget(fac, "combine_bdds");
					for (int ss = 0; ss < numStates; ss++){
						BDD thisFactorTransitionBDD = _manager->bddVar(s) * _manager->bddVar(halfFactorVars + ss);
						for (int nots = 0; nots < numStates; nots++)
							if (s != nots) thisFactorTransitionBDD *= ~_manager->bddVar(nots);
						for (int notss = 0; notss < numStates; notss++)
							if (ss != notss) thisFactorTransitionBDD *= ~_manager->bddVar(halfFactorVars + notss);

						thisFactorTransitionBDD *= chosen[s][ss];
						allTransitionsBDD += thisFactorTransitionBDD;
					}
				}
				data->transition_BDDs_per_factor[fac] = allTransitionsBDD;
			} else {
				data->transition_BDDs_per_factor_per_state_pair[fac] = std::move(chosen);
			}
		}

		// Cutting changes the BDDs, so it is part of building them and is charged
		// to construction_time. The two reporting passes below are not.
		if (bddCutting) cut_bdds_to_fixpoint();

		construction_seconds = bdd_construction_timer();

		if (bddCovering) report_covering_implications();
		if (reportLabelImplications) report_label_implications();

		// -----------------------------------------------------------------
		// (d) node in-degrees, used by the omitForcedVariables optimisation.
		//     This is a property of the BDDs, so it is computed once here
		//     rather than per SAT call.
		// -----------------------------------------------------------------
		for(int fac = 0 ; fac < fts->get_size() ; fac++){
			if (combineAllBDDsIntoOne){
				bdd_in_degree(data->transition_BDDs_per_factor[fac].getNode());
			} else {
				const TransitionSystem & factor = fts->get_ts(fac);
				for (int s = 0; s < factor.get_size(); s++)
					for (int ss = 0; ss < factor.get_size(); ss++)
						bdd_in_degree(data->transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode());
			}
		}
	} catch (const BDDBudgetExceeded & e) {
		cout << "BDDSTAT construction_failed reason " << e.reason
			 << " factor " << e.factor << " of " << fts->get_size()
			 << " where " << e.where
			 << " elapsed " << e.elapsed
			 << " live_nodes " << e.live_nodes
			 << " peak_nodes " << e.peak_nodes
			 << " memory_bytes " << _manager->ReadMemoryInUse()
			 << endl;
		cerr << "BDD construction ran out of its " << e.reason << "." << endl;
		utils::exit_with(string(e.reason) == "time_limit"
			? utils::ExitCode::OUT_OF_TIME : utils::ExitCode::OUT_OF_MEMORY);
	} catch (const BDDError &) {
		cout << "BDDSTAT construction_failed reason cudd_limit"
			 << " elapsed " << bdd_construction_timer()
			 << " live_nodes " << _manager->ReadNodeCount()
			 << " peak_nodes " << _manager->ReadPeakNodeCount()
			 << " memory_bytes " << _manager->ReadMemoryInUse()
			 << endl;
		cerr << "BDD construction exceeded the limits of the Cudd manager." << endl;
		utils::exit_with(utils::ExitCode::OUT_OF_MEMORY);
	}

	bdd_construction_timer.stop();
	cout << "BDDSTAT construction_ok factors " << fts->get_size()
		 << " labels " << fts->get_num_labels()
		 << " bdd_vars " << data->bdd_num_vars
		 << " one_step_only " << (oneStepOnly ? 1 : 0)
		 << " combined " << (combineAllBDDsIntoOne ? 1 : 0)
		 << " nodes_sum_all_factors " << total_nodes_sum
		 // What actually gets encoded once bdd_size_limit has substituted the
		 // one-step BDD for oversized pairs. Without this, nodes_sum_all_factors
		 // reads the same with and without a limit and looks like the limit does
		 // nothing. -1 when no limit is set, where the two are equal by definition.
		 << " nodes_sum_encoded " << (bddEncodingSizeLimit == -1 ? -1 : total_nodes_after_limit)
		 << " nodes_max_pair " << overall_max_pair_nodes
		 << " tseitin_nodes " << data->node_indegree.size()
		 << " live_nodes " << _manager->ReadNodeCount()
		 << " peak_nodes " << _manager->ReadPeakNodeCount()
		 << " memory_bytes " << _manager->ReadMemoryInUse()
		 << " construction_time " << construction_seconds
		 << endl;

	cout << "BDD nodes with an in-degree entry: " << data->node_indegree.size() << endl;
	cout << "SAT init time: " << sat_init_timer << endl;
}


unique_ptr<SATEncoding> BDDSATEncodingFactory::createEncodingInstance(std::shared_ptr<sat_capsule> capsule){
	return make_unique<BDDSATEncoding>(capsule, fts, forceAtLeastOneAction, data);
}


// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

BDDSATEncoding::BDDSATEncoding(
	std::shared_ptr<sat_capsule> capsule,
	const std::shared_ptr<FTSTask> & _fts,
	bool _forceAtLeastOneAction,
	const std::shared_ptr<const BDDEncodingData> & _data):
	LabelEncoding(capsule, _fts, _forceAtLeastOneAction, false, false),
	data(_data)
{
}


int BDDSATEncoding::givevar(int bddvar,
		const vector<int> & factorVars,
		const vector<int> & labelVars,
		const vector<int> & nextFactorVars) const {
	const int nfv = data->num_factor_vars;
	if (bddvar >= nfv){
		// the BDD variable order is the label order
		return labelVars[data->labelOrder[bddvar - nfv]];
	}
	if (bddvar < nfv / 2) {
		assert(factorVars.size() > size_t(bddvar));
		return factorVars[bddvar];
	}
	assert(int(nextFactorVars.size()) > bddvar - nfv / 2);
	return nextFactorVars[bddvar - nfv / 2];
}


void BDDSATEncoding::bdd_to_cnf(DdNode * node,
		const vector<int> & currentConditions,
		const vector<int> & factorVars,
		const vector<int> & labelVars,
		const vector<int> & nextFactorVars){

	// first handle edge cases
	if (Cudd_IsConstant(node)){
		bool isTrue = !Cudd_IsComplement(node);
		if (!isTrue){
			// if the conditions were true we would end up at the false node,
			// so the conditions must not all be true
			sat->notAll(currentConditions);
		}
		// in the true case the BDD is satisfied anyway and there is nothing to do
		return;
	}

	// this node is branching
	DdNode* true_branch = Cudd_T(node);
	DdNode* false_branch = Cudd_E(node);
	if (data->implicationalTseitsin && Cudd_IsComplement(node)) {
		true_branch = Cudd_Not(true_branch);
		false_branch = Cudd_Not(false_branch);
	}
	int var_to_branch = givevar(Cudd_NodeReadIndex(node), factorVars, labelVars, nextFactorVars);
	vector<tuple<int,DdNode*,DdNode*>> successors {
		{var_to_branch, true_branch, false_branch},
		{-var_to_branch, false_branch, true_branch}};

	// compute lookup node. For the biimplicational encoding we only keep one copy,
	// for tseitsin we need both positive and negative versions
	DdNode * lookup;
	if (data->implicationalTseitsin) lookup = node; else lookup = Cudd_Regular(node);

	// forcing takes precedence over lookup.
	// A node that the factory did not see cannot be judged, so we conservatively
	// skip the optimisation for it. Either choice is sound; this one only ever
	// costs clauses, never correctness.
	auto indegree = data->node_indegree.find(lookup);
	if (data->implicationalTseitsin && data->omitForcedVariables &&
			indegree != data->node_indegree.end() &&
			indegree->second <= data->forcedVariablesThreshold){
		// check if one of the branches leads to the false node
		for (const auto & [branch_var, branch, otherbranch] : successors){
			if (Cudd_IsConstant(branch) && Cudd_IsComplement(branch)){
				// this branch leads immediately to false, so we *must* take the other
				// one: if the conditions hold, the branch variable must point away.
				sat->andImplies(currentConditions,-branch_var);
				// and the conditions are then propagated further down the tree
				bdd_to_cnf(otherbranch, currentConditions, factorVars, labelVars, nextFactorVars);
				// this can happen for only one branch (otherwise the BDD is not reduced)
				return;
			}
			if (Cudd_IsConstant(branch) && !Cudd_IsComplement(branch)){
				// this branch immediately leads to true, so the other branch is only
				// relevant if the condition is false: the negation of the branch
				// variable essentially becomes a new condition.
				vector<int> newConditions = currentConditions;
				newConditions.push_back(-branch_var);
				bdd_to_cnf(otherbranch, newConditions, factorVars, labelVars, nextFactorVars);
				return;
			}
		}
	}

	// we now know that we (may) need to create a new decision variable here.
	// TODO: in theory, we could extend the condition with the branch vars,
	// but only up to a point as otherwise this will be an exponential encoding

	auto known = tseitsinVars.find(lookup);
	if (known != tseitsinVars.end()){
		int myVar = known->second;
		if (!data->implicationalTseitsin && Cudd_IsComplement(node)) myVar *= -1;
		sat->andImplies(currentConditions, myVar);
		return;
	}

	// create the formula for this BDD for the first time, so we need a variable
	// representing its truth
	int thisVar = sat->new_variable();
	DEBUG(sat->registerVariable(thisVar, "BDD_eval_var_" + to_string(tseitsinVars.size())));
	tseitsinVars[lookup] = thisVar;

	// the variable for this node becomes the new condition
	vector<int> trueVarVector = {thisVar, var_to_branch};
	vector<int> falseVarVector = {thisVar, -var_to_branch};

	bdd_to_cnf(true_branch, trueVarVector, factorVars, labelVars, nextFactorVars);
	bdd_to_cnf(false_branch, falseVarVector, factorVars, labelVars, nextFactorVars);
	if (!data->implicationalTseitsin){
		// if the variable for this one is false, and we take a branch, then that
		// variable also must be false.
		trueVarVector[0] *= -1;
		falseVarVector[0] *= -1;
		bdd_to_cnf(Cudd_Not(true_branch), trueVarVector, factorVars, labelVars, nextFactorVars);
		bdd_to_cnf(Cudd_Not(false_branch), falseVarVector, factorVars, labelVars, nextFactorVars);
		if (Cudd_IsComplement(node)) thisVar *= -1;
	}

	sat->andImplies(currentConditions,thisVar);
}


void BDDSATEncoding::encode(int fromTime, int toTime){
	// generate state vars if necessary for from time
	auto preStateVarFind = allTimesStateVars.find(fromTime);
	const vector<vector<int>> & previousStateVars = (preStateVarFind == allTimesStateVars.end()) ?
		(allTimesStateVars[fromTime] = generateStateVars()):
		preStateVarFind->second;

	// generate state vars if necessary for next time
	auto nextStateVarFind = allTimesStateVars.find(toTime);
	const vector<vector<int>> & nextStateVars = (nextStateVarFind == allTimesStateVars.end()) ?
		(allTimesStateVars[toTime] = generateStateVars()):
		nextStateVarFind->second;

	// generate label vars (must not exist yet)
	assert(allTimesLabelVars.find(fromTime) == allTimesLabelVars.end());
	const vector<int> & labelVars = (allTimesLabelVars[fromTime] = generateLabelVars());

	if (forceAtLeastOneAction) sat->atLeastOne(labelVars);

	// no frame axioms are needed: the BDDs describe, for every factor, exactly
	// which label sets lead from one state of that factor to another, and that
	// includes staying put.
	const vector<int> noFactorVars;
	for(int fac = 0 ; fac < fts->get_size() ; fac++){
		// the Tseitin variables are only shared within one factor
		tseitsinVars.clear();

		if (data->combineAllBDDsIntoOne){
			const vector<int> noConditions;
			bdd_to_cnf(data->transition_BDDs_per_factor[fac].getNode(), noConditions,
					previousStateVars[fac], labelVars, nextStateVars[fac]);
		} else {
			const TransitionSystem & factor = fts->get_ts(fac);
			for (int s = 0; s < factor.get_size(); s++){
				for (int ss = 0; ss < factor.get_size(); ss++){
					// The factor state variables are not part of these BDDs, they are
					// the conditions instead.
					const vector<int> conditions = {previousStateVars[fac][s], nextStateVars[fac][ss]};
					bdd_to_cnf(data->transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode(),
							conditions, noFactorVars, labelVars, noFactorVars);
				}
			}
		}
	}
	tseitsinVars.clear();
}


bool BDDSATEncoding::bdd_state_reconstruction_dfs(int fac,
		std::vector<std::vector<int>> & reconstructedStates,
		int depth,
		const std::vector<int> & plan,
		std::set<std::pair<int,int>> & visited) const {
	const TransitionSystem & factor = fts->get_ts(fac);

	int currentState = reconstructedStates[depth][fac];

	// if already visited it will be unsuccessful.
	if (visited.count({currentState, depth})) return false;
	visited.insert({currentState, depth});

	// try to walk one step further
	for (const auto & transition : factor.get_transitions_with_label(plan[depth])){
		if (transition.src != currentState) continue;
		if (depth == int(plan.size()) - 1){
			// this is the last label, so we need to have reached the target state
			if (transition.target == reconstructedStates[depth+1][fac]) return true;
			continue; // cannot use this transition
		}

		reconstructedStates[depth + 1][fac] = transition.target;
		if (bdd_state_reconstruction_dfs(fac,reconstructedStates,depth+1,plan,visited))
			return true;
	}
	return false;
}


std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>>
BDDSATEncoding::extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order){

	auto readState = [&](int time){
		vector<int> state;
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			for(size_t s = 0 ; s < allTimesStateVars[time][ts].size() ; s++){
				if(ipasir_val(sat->solver, allTimesStateVars[time][ts][s]) > 0){
					state.push_back(s);
					break;
				}
			}
		}
		return state;
	};

	vector<vector<int>> statesPerTimestep;
	statesPerTimestep.push_back(readState(initTime));

	set<int> timesteps_with_labels;
	vector<int> labels;

	for(auto [labelTimestep,stateAfterTimestep] : time_step_order){
		// which labels are used in this time step?
		set<int> selectedLabelSet;
		for(size_t label = 0 ; label < allTimesLabelVars[labelTimestep].size() ; label++)
			if(ipasir_val(sat->solver, allTimesLabelVars[labelTimestep][label]) > 0)
				selectedLabelSet.insert(label);

		// if we don't execute any label, we don't have to extract a new state
		if (selectedLabelSet.empty()) continue;
		timesteps_with_labels.insert(labelTimestep);

		// Several labels may fire in one time step and they have to be applied in
		// the order the BDDs were built with, not in the order of their IDs.
		vector<int> selectedLabels;
		for (const int & l : data->labelOrder)
			if (selectedLabelSet.count(l))
				selectedLabels.push_back(l);

		// The formula does not contain the states between the labels of one time
		// step, so we have to reconstruct them. We know the labels and their order,
		// and the state before and after the step. Since the factors are
		// independent, each of them can be reconstructed on its own.
		vector<vector<int>> reconstructedStates(selectedLabels.size() + 1);
		reconstructedStates[0] = statesPerTimestep.back();
		for (size_t i = 1; i < selectedLabels.size(); i++)
			reconstructedStates[i].resize(fts->get_size());
		reconstructedStates[selectedLabels.size()] = readState(stateAfterTimestep);

		for (int fac = 0; fac < fts->get_size(); fac++){
			std::set<std::pair<int,int>> visited;
			bool reconstruction_successful =
				bdd_state_reconstruction_dfs(fac,reconstructedStates,0,selectedLabels,visited);
			if (!reconstruction_successful){
				cout << "Reconstruction failed on factor " << fac << " at time " << labelTimestep
					 << " going from " << reconstructedStates[0][fac]
					 << " to " << reconstructedStates.back()[fac] << "." << endl;
				assert(false);
			}
		}

		for (size_t i = 1; i <= selectedLabels.size(); i++)
			statesPerTimestep.push_back(reconstructedStates[i]);
		for (const int & l : selectedLabels)
			labels.push_back(l);
	}

	vector<int> GS = statesPerTimestep.back();
	PlanState goalState = PlanState(std::move(GS));
	vector<PlanState> states;
	for(size_t s = 0 ; s < statesPerTimestep.size() ; s++)
		states.push_back(PlanState(std::move(statesPerTimestep[s])));

	cout << "Total states: " << states.size() << endl;
	cout << "Total labels: " << labels.size() << endl;
	cout << "Total timesteps with label: " << timesteps_with_labels.size() << endl;

	// manual checking of the FTS plan
	for (size_t i = 0; i < labels.size(); i++){
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			int from = states[i][ts];
			int to = states[i+1][ts];

			const TransitionSystem & tss = fts->get_ts(ts);
			auto transitions = tss.get_transitions_with_label(labels[i]);
			bool good = false;
			for (const Transition &t: transitions)
				if (t.src == from && t.target == to) good = true;

			if (!good){
				cout << "Execution of FTS plan failed at label nr. " << i << " being " << labels[i] << endl;
				cout << "Formula wanted to transition in ts " << ts << " from " << from << " to " << to << " but this is impossible" << endl;
				cout << "Possible transitions are: " << endl;
				for (const Transition &t: transitions)
					cout << "\t" << t.src << " -> " << t.target << endl;
				assert(false);
			}
		}
	}

	return make_tuple(goalState,states,labels,timesteps_with_labels);
}

}
