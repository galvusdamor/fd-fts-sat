#include <fstream>
#include <algorithm>
#include <array>
#include <climits>
#include <functional>
#include <map>
#include <unordered_set>
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
	pathEncoding(opts.get<bool>("path_encoding")),
	pathSharing(opts.get<bool>("path_sharing")),
	impliedBicliques(opts.get<bool>("label_implications_bicliques")),
	combineITE(opts.get<bool>("combine_ite")),
	omitForcedVariables(opts.get<bool>("omitforcedvariables")),
	forcedVariablesThreshold(opts.get<int>("forcedvariablesthreshold")),
	bddCutting(opts.get<bool>("cutbdds")),
	cutMode(opts.get_enum("cut_mode")),
	cutLimitFactor(opts.get<double>("cut_limit_factor")),
	reportLabelConjunction(opts.get<bool>("report_label_conjunction")),
	cubeEncodingMax(opts.get<int>("cube_encoding_max")),
	labelImplications(opts.get_enum("label_implications")),
	labelImplicationsMax(opts.get<int>("label_implications_max")),
	cutMinOverlap(opts.get<double>("cut_min_overlap")),
	labelConjunctionLimit(opts.get<int>("label_conjunction_limit")),
	bddCovering(opts.get<bool>("coverbdds")),
	alternateLabelOrders(opts.get<bool>("alternate_label_orders")),
	reportLabelImplications(opts.get<bool>("report_label_implications")),
	reportFactorStatistics(opts.get<bool>("report_factor_statistics")),
	bddInitTimeLimit(opts.get<int>("bdd_init_time_limit")),
	bddNodeLimit(long(opts.get<int>("bdd_node_limit"))),
	label_order_finder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
	dumpLabelOrderFile(opts.get<string>("dump_label_order")),
	bdd_variable_order_finder(opts.contains("bdd_variable_order")
		? opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("bdd_variable_order")
		: nullptr),
	bddReorderMethod(opts.get_enum("bdd_reorder")),
	bddReorderDynamic(opts.get<bool>("bdd_reorder_dynamic")),
	dumpBDDVariableOrderFile(opts.get<string>("dump_bdd_variable_order")),
	cudd_init_nodes(long(opts.get<int>("cudd_init_nodes"))),
	cudd_init_cache_size(long(opts.get<int>("cudd_cache_size"))),
	cudd_init_available_memory(long(opts.get<int>("cudd_max_memory_mb")) * 1024L * 1024L)
{
	if (oneStepOnly && bddEncodingSizeLimit != -1){
		cerr << "bdd_size_limit only has an effect if one_step_only=false: it interpolates "
			 << "between the full reachability BDDs and the one-step BDDs." << endl;
		utils::exit_with(utils::ExitCode::INPUT_ERROR);
	}
	if (bddReorderDynamic && bddReorderMethod == 0){
		cerr << "bdd_reorder_dynamic needs a bdd_reorder method." << endl;
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
		"path_encoding",
		"translate each BDD with a path-based encoding (NNFPath, Abio et al. CPAIOR 2016) instead "
		"of Tseitin: variables for the nodes and edges on the root-to-1 path, plus clauses that "
		"make unit propagation domain consistent on the BDD's variables. About three variables per "
		"node and no sharing between BDDs; best with combinebdds",
		"false");

	parser.add_option<bool>(
		"path_sharing",
		"with path_encoding and split BDDs: one path encoding per factor over all its "
		"(source, target) BDDs, which share nodes; the BDD of the active pair is selected by "
		"an auxiliary variable for previous_state(s) & next_state(s'). Without it every pair's "
		"BDD gets its own path encoding",
		"false");
	parser.add_option<bool>(
		"label_implications_bicliques",
		"encode the binary label_implications clauses by a greedy biclique cover "
		"(Rintanen, ECAI 2006): a biclique A x B of conflicting literals becomes a -> z, "
		"b -> -z with one new variable per step instead of |A|*|B| binary clauses",
		"false");
	parser.add_option<bool>(
		"combine_ite",
		"with combinebdds: build the factor BDD as if-then-else chains over the state "
		"variables instead of conjoining full one-hot minterms. Equal on one-hot assignments, "
		"which the exactly-one constraints guarantee; |S|^2 instead of |S|^3 BDD operations",
		"false");

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

	vector<string> cut_modes {"AND", "AND_LIMITED", "RESTRICT", "GLOBAL_AND", "GLOBAL_AND_LIMITED", "GLOBAL_RESTRICT", "AND_CAPPED"};
	vector<string> cut_modes_doc {
		"conjoin the constraint into the target's BDDs (cutting proper)",
		"conjoin with a node limit of cut_limit_factor * size of the target BDD; keep the result "
		"only if it is smaller than the original",
		"Cudd_bddRestrict with the constraint as care set: never larger. Care sets flow along the "
		"factor order only (target index > source index), so each one is implied by an "
		"unrestricted BDD; restricting in both directions would be unsound",
		"one global constraint instead of pairwise ones: the AND of all factors' any-transition "
		"BDDs (under label_conjunction_limit), projected onto each factor's labels, conjoined",
		"as GLOBAL_AND with the node limit of AND_LIMITED; keep only smaller results",
		"as GLOBAL_AND but with Restrict, using the conjunction of the factors before the target",
		"conjoin with a node limit of cut_limit_factor * size of the target BDD and keep the result "
		"even if it is larger (bounded growth); meant together with cut_min_overlap"};
	parser.add_enum_option("cut_mode", cut_modes, "with cutbdds: how a constraint is applied",
		"AND", cut_modes_doc);
	parser.add_option<double>("cut_limit_factor", "with cut_mode=AND_LIMITED", "1");
	parser.add_option<bool>(
		"report_label_conjunction",
		"diagnostics: conjoin the any-transition label BDDs of all factors (every label set "
		"permitted by every factor in one step) under label_conjunction_limit new nodes per "
		"step, and report size, time and how many factors fit",
		"false");
	parser.add_option<int>("label_conjunction_limit", "node limit for report_label_conjunction", "10000000");
	vector<string> impl_modes {"NONE", "UNCONDITIONAL", "PER_SOURCE", "BOTH"};
	vector<string> impl_modes_doc {
		"no extra clauses",
		"per factor, the units and binary implications (mutexes included) that hold in every "
		"label set the factor permits in one step, as clauses over each step's label variables",
		"the same per source state of a factor, guarded by that state variable (split BDDs only)",
		"both"};
	parser.add_enum_option("label_implications", impl_modes,
		"add clauses implied by the factor BDDs, so unit propagation sees dependencies the Tseitin "
		"translation does not propagate", "NONE", impl_modes_doc);
	parser.add_option<int>("label_implications_max",
		"at most this many implied clauses of each kind (unconditional, per source); -1 = all", "-1");
	parser.add_option<double>("cut_min_overlap",
		"cutbdds (pairwise modes): only propagate from S to T if the labels they share make up at "
		"least this fraction of T's labels", "0");
	parser.add_option<int>(
		"cube_encoding_max",
		"encode a BDD with at most this many paths to 0 directly, one clause per path (cube of "
		"its negation) together with the conditions, without Tseitin variables; larger BDDs keep "
		"the Tseitin encoding. -1 = off (Tseitin only)",
		"-1");

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
		"alternate_label_orders",
		"use two label orderings instead of one: the order from label_order for "
		"the odd-numbered time steps and its reverse for the even ones. The label "
		"order is the BDD variable order, so this means building and encoding two "
		"sets of BDDs, and a step's label variables are then constrained in the "
		"order belonging to that step",
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
		"order in which labels may be applied within one time step. Unless "
		"bdd_variable_order is given, this is also the BDD variable order",
		"label_order_linear()");

	parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
		"bdd_variable_order",
		"order of the label variables in the BDDs, independently of label_order. "
		"It changes only the size of the BDDs, not which label sets they allow. "
		"Omitted: the label order",
		options::OptionParser::NONE);

	vector<string> reorder_methods {"NONE", "SIFT", "SIFT_CONVERGE", "SYMM_SIFT", "GROUP_SIFT"};
	vector<string> reorder_methods_doc {
		"keep the initial variable order",
		"Cudd's sifting (Rudell), one pass",
		"sifting repeated until no improvement",
		"symmetric sifting",
		"group sifting"};
	parser.add_enum_option("bdd_reorder", reorder_methods,
		"Cudd variable reordering, applied once after the BDDs are built (and "
		"after cutbdds), before they are Tseitin-translated. The labels keep "
		"their meaning; only the variable levels change",
		"NONE", reorder_methods_doc);

	parser.add_option<bool>(
		"bdd_reorder_dynamic",
		"also let Cudd reorder automatically during construction, with the "
		"bdd_reorder method",
		"false");

	parser.add_option<string>(
		"dump_bdd_variable_order",
		"write the final BDD variable order of the labels (after reordering, "
		"top level first) to this file, one label id per line, in the format "
		"label_order_file reads; 'none' = do not. The option parser lower-cases "
		"the path.",
		"none");

	parser.add_option<string>(
		"dump_label_order",
		"write the (primary) label order to this file, one label id per line; "
		"'none' = do not. The option parser lower-cases the path.",
		"none");

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
		var_names[data->num_factor_vars + i] = "label_" + to_string(data->orderings[0].bddVarToLabel[i]);

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
BDD BDDSATEncodingFactory::any_transition(const BDDEncodingData::Ordering & ord, int fac) const {
	if (combineAllBDDsIntoOne){
		BDD stateCube = _manager->bddOne();
		for (int i = 0; i < data->num_factor_vars; i++) stateCube *= _manager->bddVar(i);
		return ord.transition_BDDs_per_factor[fac].ExistAbstract(stateCube);
	}
	const TransitionSystem & factor = fts->get_ts(fac);
	BDD any = _manager->bddZero();
	for (int s = 0; s < factor.get_size(); s++)
		for (int ss = 0; ss < factor.get_size(); ss++)
			any += ord.transition_BDDs_per_factor_per_state_pair[fac][s][ss];
	return any;
}


/**
 * Propagate constraints between factors until a fixpoint is reached.
 *
 * For a pair of factors (source, target) the labels that are irrelevant for the
 * target are projected away from "any transition of source". What remains is a
 * constraint over the labels both factors share, and it is applied to every
 * transition BDD of the target (cut_mode). With AND the fixpoint is unique, so a
 * source is only projected again once its own BDDs changed.
 */
void BDDSATEncodingFactory::cut_bdds_to_fixpoint(BDDEncodingData::Ordering & ord){
	const int F = fts->get_size();
	utils::Timer total;
	double t_any = 0, t_project = 0, t_apply = 0;
	long projections = 0, applications = 0, changed_bdds = 0, aborted = 0, kept_larger = 0;
	const long nodes_before = encoded_nodes_sum(ord);

	// per factor: the labels its BDDs mention, and the labels to keep for it as target
	vector<vector<int>> mentioned(F);
	vector<vector<char>> keep(F, vector<char>(fts->get_num_labels(), 0));
	for (int f = 0; f < F; f++){
		const TransitionSystem & ts = fts->get_ts(f);
		for (int label = 0; label < fts->get_num_labels(); label++){
			LabelID id(label);
			if (ts.is_relevant_label(id) || !ts.is_selfloop_everywhere(id)) { mentioned[f].push_back(label); keep[f][label] = 1; }
		}
	}

	// apply constraint c to one BDD: 0 AND, 1 AND_LIMITED, 2 RESTRICT; true if it changed
	auto apply = [&](BDD & target, const BDD & c, int mode){
		BDD old = target;
		if (mode == 0) {
			target *= c;
		} else if (mode == 1) {
			const unsigned int limit = max(1u, (unsigned int)(cutLimitFactor * max(1, target.nodeCount())));
			try {
				BDD r = target.And(c, limit);
				if (r.nodeCount() < target.nodeCount()) target = r; else kept_larger++;
			} catch (const BDDError &) {
				Cudd_ClearErrorCode(_manager->getManager());
				aborted++;
			}
		} else if (mode == 3) {   // AND_CAPPED: bounded growth, kept even if larger
			const unsigned int limit = max(1u, (unsigned int)(cutLimitFactor * max(1, target.nodeCount())));
			try {
				target = target.And(c, limit);
			} catch (const BDDError &) {
				Cudd_ClearErrorCode(_manager->getManager());
				aborted++;
			}
		} else {
			target = target.Restrict(c);
		}
		applications++;
		if (old != target) { changed_bdds++; return true; }
		return false;
	};

	/*
	  GLOBAL_*: G = AND of all factors' any_transition BDDs (every label set all
	  factors permit at once), computed once from the original BDDs under
	  label_conjunction_limit; if that is exceeded, the conjunction of the factors
	  that fit (weaker, still implied). Each T gets G projected onto its own labels.
	  GLOBAL_RESTRICT uses only the factors before T (prefix conjunction), so every
	  care set is implied by unrestricted BDDs.
	*/
	if (cutMode >= 3 && cutMode <= 5) {
		const int mode = cutMode - 3;
		utils::Timer tg;
		BDD G = _manager->bddOne();
		vector<BDD> prefix(F + 1, _manager->bddOne());   // prefix[k] = AND over factors < k
		int fitted = 0;
		for (int f = 0; f < F; f++){
			prefix[f] = G;
			const BDD any = any_transition(ord, f);
			try {
				G = G.And(any, (unsigned int)labelConjunctionLimit);
				fitted++;
			} catch (const BDDError &) {
				Cudd_ClearErrorCode(_manager->getManager());
				for (int g = f; g <= F; g++) prefix[g] = G;
				break;
			}
		}
		if (fitted == F) prefix[F] = G;
		t_any = tg();
		const long g_nodes = G.nodeCount();
		for (int facT = 0; facT < F; facT++){
			utils::Timer tp;
			const BDD & source = (mode == 2) ? prefix[facT] : G;
			BDD cube = _manager->bddOne();
			for (int label = 0; label < fts->get_num_labels(); label++)
				if (!keep[facT][label]) cube *= _manager->bddVar(ord.labelToBDDVar[label]);
			const BDD constraint = source.ExistAbstract(cube);
			projections++;
			t_project += tp();
			if (constraint == _manager->bddOne()) continue;
			utils::Timer ta;
			if (!combineAllBDDsIntoOne){
				const TransitionSystem & factorTarget = fts->get_ts(facT);
				for (int s = 0; s < factorTarget.get_size(); s++)
					for (int ss = 0; ss < factorTarget.get_size(); ss++)
						apply(ord.transition_BDDs_per_factor_per_state_pair[facT][s][ss], constraint, mode);
			} else {
				apply(ord.transition_BDDs_per_factor[facT], constraint, mode);
			}
			t_apply += ta();
		}
		cout << "BDDSTAT cut mode " << cutMode << " rounds 1 global_factors " << fitted << " of " << F
			 << " global_nodes " << g_nodes
			 << " projections " << projections << " applications " << applications
			 << " changed_bdds " << changed_bdds << " aborted " << aborted << " kept_larger " << kept_larger
			 << " nodes_before " << nodes_before << " nodes_after " << encoded_nodes_sum(ord)
			 << " time_any " << t_any << " time_project " << t_project << " time_apply " << t_apply
			 << " time_total " << total() << endl;
		return;
	}

	vector<char> dirty(F, 1);   // source whose BDDs changed since it was last projected
	int round = 0;
	bool anyUpdate = true;
	while (anyUpdate) {
		anyUpdate = false;
		round++;
		utils::Timer t; t.resume();
		vector<BDD> any(F);
		for (int fac = 0; fac < F; fac++) if (dirty[fac]) any[fac] = any_transition(ord, fac);
		t_any += t(); t.reset();
		vector<char> next_dirty(F, 0);
		for (int facS = 0; facS < F; facS++){
			if (!dirty[facS]) continue;
			for (int facT = 0; facT < F; facT++){
				if (facS == facT) continue;
				// RESTRICT uses the constraint as a care set: T may then be arbitrary
				// outside it. That is only sound if the constraint is implied by an
				// *unrestricted* BDD, so care sets flow along the factor order only (by
				// induction from factor 0, which is never restricted). Restricting in both
				// directions lets two factors justify each other and accept label sets
				// neither originally allows.
				if (cutMode == 2 && facS > facT) continue;
				utils::Timer tp;
				BDD cube = _manager->bddOne();
				int shared = 0;
				for (int label : mentioned[facS]) {
					if (keep[facT][label]) { shared++; continue; }
					cube *= _manager->bddVar(ord.labelToBDDVar[label]);
				}
				if (!shared) continue;
				if (cutMinOverlap > 0 && shared < cutMinOverlap * max<size_t>(1, mentioned[facT].size())) continue;
				const BDD constraint = any[facS].ExistAbstract(cube);
				projections++;
				t_project += tp();
				if (constraint == _manager->bddOne()) continue;
				utils::Timer ta;
				bool changed = false;
				if (!combineAllBDDsIntoOne){
					const TransitionSystem & factorTarget = fts->get_ts(facT);
					for (int s = 0; s < factorTarget.get_size(); s++)
						for (int ss = 0; ss < factorTarget.get_size(); ss++)
							changed |= apply(ord.transition_BDDs_per_factor_per_state_pair[facT][s][ss], constraint, cutMode == 6 ? 3 : cutMode);
				} else {
					changed |= apply(ord.transition_BDDs_per_factor[facT], constraint, cutMode == 6 ? 3 : cutMode);
				}
				t_apply += ta();
				if (changed) { next_dirty[facT] = 1; anyUpdate = true; }
			}
		}
		dirty.swap(next_dirty);
		// RESTRICT is not a propagation and need not converge: one pass only
		if (cutMode == 2) break;
	}
	cout << "BDDSTAT cut mode " << cutMode << " rounds " << round
		 << " projections " << projections << " applications " << applications
		 << " changed_bdds " << changed_bdds << " aborted " << aborted << " kept_larger " << kept_larger
		 << " nodes_before " << nodes_before << " nodes_after " << encoded_nodes_sum(ord)
		 << " time_any " << t_any << " time_project " << t_project << " time_apply " << t_apply
		 << " time_total " << total() << endl;
}


void BDDSATEncodingFactory::compute_zero_cubes() {
	utils::Timer t;
	long bdds = 0, cube_bdds = 0, tseitin_bdds = 0, cubes = 0, literals = 0, constant_true = 0;
	int max_len = 0;
	double smallest_tseitin_paths = -1;
	DdManager * dd = _manager->getManager();
	auto handle = [&](const BDD & f) {
		DdNode * node = f.getNode();
		bdds++;
		if (data->zero_cubes.count(node)) { cube_bdds++; return; }   // shared root, already enumerated
		if (f == _manager->bddOne()) { constant_true++; data->zero_cubes[node] = {}; cube_bdds++; return; }
		DdNode * neg = Cudd_Not(node);
		const double paths = Cudd_CountPathsToNonZero(neg);
		if (paths > double(cubeEncodingMax)) {
			tseitin_bdds++;
			if (smallest_tseitin_paths < 0 || paths < smallest_tseitin_paths) smallest_tseitin_paths = paths;
			return;
		}
		vector<vector<int>> list;
		DdGen * gen; int * cube; CUDD_VALUE_TYPE value;
		Cudd_ForeachCube(dd, neg, gen, cube, value) {
			vector<int> c;
			for (int v = 0; v < Cudd_ReadSize(dd); v++) {
				if (cube[v] == 1) c.push_back(v + 1);
				else if (cube[v] == 0) c.push_back(-(v + 1));
			}
			max_len = max<int>(max_len, c.size());
			literals += c.size();
			list.push_back(move(c));
		}
		cubes += list.size();
		data->zero_cubes[node] = move(list);
		cube_bdds++;
	};
	for (const auto & ord : data->orderings) {
		if (combineAllBDDsIntoOne) {
			for (const BDD & f : ord.transition_BDDs_per_factor) handle(f);
		} else {
			for (const auto & per_source : ord.transition_BDDs_per_factor_per_state_pair)
				for (const auto & per_target : per_source)
					for (const BDD & f : per_target) handle(f);
		}
	}
	cout << "BDDSTAT cubes max " << cubeEncodingMax << " bdds " << bdds
		 << " cube_bdds " << cube_bdds << " tseitin_bdds " << tseitin_bdds
		 << " constant_true " << constant_true << " cubes " << cubes << " literals " << literals
		 << " max_cube_len " << max_len
		 << " mean_cube_len " << (cubes ? double(literals) / cubes : 0.0)
		 << " smallest_tseitin_paths " << smallest_tseitin_paths
		 << " time " << t() << endl;
}


void BDDSATEncodingFactory::compute_implied_clauses() {
	utils::Timer t;
	const int nfv = data->num_factor_vars;
	BDD stateCube = _manager->bddOne();
	for (int i = 0; i < nfv; i++) stateCube *= _manager->bddVar(i);
	const bool uncond = labelImplications == 1 || labelImplications == 3;
	const bool per_source = (labelImplications == 2 || labelImplications == 3) && !combineAllBDDsIntoOne;
	long cap_hits = 0;
	data->implied_clauses.assign(data->orderings.size(), {});
	data->implied_cond_clauses.assign(data->orderings.size(), {});
	for (size_t o = 0; o < data->orderings.size(); o++) {
		const auto & ord = data->orderings[o];
		auto lit = [&](unsigned int v, bool positive) {
			const int label = ord.bddVarToLabel[v - nfv];
			return positive ? label + 1 : -(label + 1);
		};
		// units and binary implications of f, as clauses
		auto extract = [&](const BDD & f, vector<vector<int>> & out) {
			if (f == _manager->bddOne() || f == _manager->bddZero()) return;
			for (unsigned int v : f.FindEssential().SupportIndices()) {
				if (int(v) < nfv) continue;
				out.push_back({lit(v, f.IsVarEssential(v, 1))});
			}
			for (unsigned int v : f.SupportIndices()) {
				if (int(v) < nfv) continue;
				for (int phase = 0; phase < 2; phase++) {
					const BDD cof = f * (phase ? _manager->bddVar(v) : ~_manager->bddVar(v));
					if (cof == _manager->bddZero()) continue;
					for (unsigned int w : cof.FindEssential().SupportIndices()) {
						if (w == v || int(w) < nfv) continue;
						// premise (v = phase) implies w = essential value
						vector<int> c = {lit(v, !phase), lit(w, cof.IsVarEssential(w, 1))};
						sort(c.begin(), c.end());
						out.push_back(c);
					}
				}
			}
		};
		set<vector<int>> seen;
		for (int fac = 0; fac < fts->get_size(); fac++) {
			if (uncond) {
				vector<vector<int>> cl;
				extract(any_transition(ord, fac), cl);
				for (auto & c : cl) {
					if (!seen.insert(c).second) continue;
					if (labelImplicationsMax >= 0 && (long)data->implied_clauses[o].size() >= labelImplicationsMax) { cap_hits++; continue; }
					data->implied_clauses[o].push_back(c);
				}
			}
			if (per_source) {
				const TransitionSystem & factor = fts->get_ts(fac);
				for (int src = 0; src < factor.get_size(); src++) {
					BDD fromSrc = _manager->bddZero();
					for (int ss = 0; ss < factor.get_size(); ss++)
						fromSrc += ord.transition_BDDs_per_factor_per_state_pair[fac][src][ss];
					vector<vector<int>> cl;
					extract(fromSrc, cl);
					set<vector<int>> local;
					for (auto & c : cl) {
						if (seen.count(c) || !local.insert(c).second) continue;   // already unconditional
						if (labelImplicationsMax >= 0 && (long)data->implied_cond_clauses[o].size() >= labelImplicationsMax) { cap_hits++; continue; }
						data->implied_cond_clauses[o].push_back({fac, src, c});
					}
				}
			}
		}
	}
	long units = 0;
	for (const auto & c : data->implied_clauses[0]) if (c.size() == 1) units++;
	cout << "BDDSTAT implied_clauses mode " << labelImplications
		 << " unconditional " << data->implied_clauses[0].size() << " (units " << units << ")"
		 << " per_source " << data->implied_cond_clauses[0].size()
		 << " cap_hits " << cap_hits << " time " << t() << endl;
}


/*
  Greedy biclique cover of the binary label_implications clauses (Rintanen, ECAI 2006).
  A clause (l1 | l2) is a conflict between the literals -l1 and -l2. Starting from the
  literal with most remaining conflicts, A = {v}, B = its conflict partners; A grows by the
  literal that keeps the most of B as long as |A||B| - |A| - |B| (the clauses saved) grows.
  Covered conflicts are removed; whatever is left stays a binary clause.
*/
void BDDSATEncodingFactory::cover_bicliques() {
	utils::Timer t;
	const int L = fts->get_num_labels();
	auto id = [](int lit) { return lit > 0 ? 2 * (lit - 1) : 2 * (-lit - 1) + 1; };
	auto lit_of = [](int i) { return i % 2 == 0 ? i / 2 + 1 : -(i / 2 + 1); };
	data->implied_bicliques.assign(data->orderings.size(), {});
	long before = 0, after = 0, covered = 0;
	for (size_t o = 0; o < data->orderings.size(); o++) {
		vector<unordered_set<int>> adj(2 * L);
		vector<vector<int>> kept;
		for (const auto & c : data->implied_clauses[o]) {
			if (c.size() != 2) { kept.push_back(c); continue; }
			const int a = id(-c[0]), b = id(-c[1]);
			if (a == b) { kept.push_back(c); continue; }
			adj[a].insert(b); adj[b].insert(a);
		}
		before += data->implied_clauses[o].size();
		vector<char> exhausted(2 * L, 0);
		auto value = [](long a, long b) { return a * b - a - b; };
		vector<int> count(2 * L, 0);
		while (true) {
			int v = -1;
			for (int i = 0; i < 2 * L; i++)
				if (!exhausted[i] && adj[i].size() >= 3 && (v < 0 || adj[i].size() > adj[v].size())) v = i;
			if (v < 0) break;
			vector<int> A = {v};
			vector<int> B(adj[v].begin(), adj[v].end());
			while (true) {
				// count[u] = |N(u) & B|
				vector<int> touched;
				for (int b : B) for (int u : adj[b]) { if (!count[u]) touched.push_back(u); count[u]++; }
				int best = -1;
				for (int u : touched)
					if (find(A.begin(), A.end(), u) == A.end() && (best < 0 || count[u] > count[best])) best = u;
				const int nb = best < 0 ? 0 : count[best];
				for (int u : touched) count[u] = 0;
				if (best < 0 || value(A.size() + 1, nb) <= value(A.size(), B.size())) break;
				A.push_back(best);
				vector<int> nB;
				for (int b : B) if (adj[best].count(b)) nB.push_back(b);
				B.swap(nB);
			}
			if (value(A.size(), B.size()) <= 0) { exhausted[v] = 1; continue; }
			BDDEncodingData::Biclique bc;
			for (int a : A) bc.A.push_back(lit_of(a));
			for (int b : B) bc.B.push_back(lit_of(b));
			for (int a : A) for (int b : B) { adj[a].erase(b); adj[b].erase(a); covered++; }
			data->implied_bicliques[o].push_back(std::move(bc));
		}
		for (int a = 0; a < 2 * L; a++)
			for (int b : adj[a]) if (a < b) {
				vector<int> c = {-lit_of(a), -lit_of(b)};
				sort(c.begin(), c.end());
				kept.push_back(c);
			}
		data->implied_clauses[o].swap(kept);
		after += data->implied_clauses[o].size();
		for (const auto & bc : data->implied_bicliques[o]) after += bc.A.size() + bc.B.size();
	}
	cout << "BDDSTAT implied_bicliques bicliques " << data->implied_bicliques[0].size()
		 << " covered_conflicts " << covered << " clauses_before " << before
		 << " clauses_after " << after << " time " << t() << endl;
}


void BDDSATEncodingFactory::report_label_conjunction(const BDDEncodingData::Ordering & ord) const {
	utils::Timer t;
	BDD all = _manager->bddOne();
	int done = 0;
	bool aborted = false;
	long max_nodes = 0;
	for (int fac = 0; fac < fts->get_size(); fac++){
		const BDD any = any_transition(ord, fac);
		try {
			all = all.And(any, (unsigned int)labelConjunctionLimit);
		} catch (const BDDError &) {
			Cudd_ClearErrorCode(_manager->getManager());
			aborted = true;
			break;
		}
		done++;
		max_nodes = max<long>(max_nodes, all.nodeCount());
		if (all == _manager->bddZero()) break;
	}
	cout << "BDDSTAT label_conjunction factors " << done << " of " << fts->get_size()
		 << " aborted " << (aborted ? 1 : 0) << " nodes " << all.nodeCount() << " max_nodes " << max_nodes
		 << " empty " << (all == _manager->bddZero() ? 1 : 0)
		 << " time " << t() << endl;
}


/**
 * Report label/state implications that hold in every transition of a factor.
 *
 * This is a diagnostic only: it prints what it finds and does not change the
 * BDDs or the generated formula. It was written to judge whether such
 * implications are frequent enough to be worth encoding separately.
 */
void BDDSATEncodingFactory::report_covering_implications(const BDDEncodingData::Ordering & ord) const {
	long claims_verified = 0, claims_refuted = 0;
	for (int fac = 0; fac < fts->get_size(); fac++){
		const TransitionSystem & factor = fts->get_ts(fac);
		const vector<vector<BDD>> & allPossiblePaths = ord.transition_BDDs_per_factor_per_state_pair[fac];

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
				BDD testBDD = _manager->bddVar(ord.labelToBDDVar[label]);
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
			const BDD labelIsTaken = _manager->bddVar(ord.labelToBDDVar[label]);
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
		int fac, const TransitionSystem & factor,
		const BDDEncodingData::Ordering & ord, int & num_relevant_labels){
	const int numStates = factor.get_size();

	vector<vector<BDD>> paths(numStates);
	for (int s = 0; s < numStates; s++){
		paths[s].resize(numStates);
		for (int ss = 0; ss < numStates; ss++)
			paths[s][ss] = (s == ss) ? _manager->bddOne() : _manager->bddZero();
	}

	for(int l = fts->get_num_labels() - 1; l >= 0; l--){
		int label = ord.labelOrder[l];
		LabelID labelID (label);
		if (!factor.is_relevant_label(labelID) && factor.is_selfloop_everywhere(labelID))
			continue;
		num_relevant_labels++;
		check_budget(fac, "full_reachability_dp");

		const BDD labelVar = _manager->bddVar(ord.labelToBDDVar[label]);
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
		int fac, const TransitionSystem & factor, const FTSMatrix & matrix,
		const BDDEncodingData::Ordering & ord){
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
	// Built in *label order*, which is the BDD variable order unless
	// bdd_variable_order says otherwise. That matters: the BDDs below are
	// products over these labels, and conjoining them in variable order keeps
	// the intermediate results a chain. Walking them in label-id order instead
	// builds the same BDD but roughly twice as slowly on the label-rich
	// instances.
	vector<vector<int>> mustBeFalseAt(numStates);
	for (int s = 0; s < numStates; s++){
		const set<int> & loops = matrix.get_self_loop_labels_for_state(s);
		for (int pos = 0; pos < numLabels; pos++){
			const int label = ord.labelOrder[pos];
			assert(groupOfLabel[label] >= 0);
			if (!loops.count(groupOfLabel[label]))
				mustBeFalseAt[s].push_back(label);
		}
	}

	auto orderOf = [&](int label){ return ord.labelPosition[label]; };

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
			const int label = ord.labelOrder[pos];
			prefixFalse[s][pos+1] = cannotSelfLoop[label]
				? prefixFalse[s][pos] * ~_manager->bddVar(ord.labelToBDDVar[label])
				: prefixFalse[s][pos];
		}

		suffixFalse[s].resize(numLabels + 1);
		suffixFalse[s][numLabels] = _manager->bddOne();
		for (int pos = numLabels - 1; pos >= 0; pos--){
			const int label = ord.labelOrder[pos];
			suffixFalse[s][pos] = cannotSelfLoop[label]
				? suffixFalse[s][pos+1] * ~_manager->bddVar(ord.labelToBDDVar[label])
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
			const BDD thisTrans = _manager->bddVar(ord.labelToBDDVar[l])
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
void BDDSATEncodingFactory::report_label_implications(const BDDEncodingData::Ordering & ord) const {
	const int nfv = data->num_factor_vars;
	BDD stateCube = _manager->bddOne();
	for (int i = 0; i < nfv; i++) stateCube *= _manager->bddVar(i);

	auto labelOfVar = [&](int v){ return ord.bddVarToLabel[v - nfv]; };

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
			any = ord.transition_BDDs_per_factor[fac].ExistAbstract(stateCube);
		} else {
			const TransitionSystem & factor = fts->get_ts(fac);
			for (int s = 0; s < factor.get_size(); s++)
				for (int ss = 0; ss < factor.get_size(); ss++)
					any += ord.transition_BDDs_per_factor_per_state_pair[fac][s][ss];
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
					fromSrc += ord.transition_BDDs_per_factor_per_state_pair[fac][src][ss];
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


void BDDSATEncodingFactory::build_bdds_for_ordering(BDDEncodingData::Ordering & ord,
		long & total_nodes_sum, long & total_nodes_after_limit,
		int & overall_max_pair_nodes){
	if (combineAllBDDsIntoOne) ord.transition_BDDs_per_factor.resize(fts->get_size());
	else ord.transition_BDDs_per_factor_per_state_pair.resize(fts->get_size());

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
				allPossiblePaths = build_full_reachability_bdds(fac, factor, ord, num_relevant_labels);

			vector<vector<BDD>> oneStepPaths;
			if (oneStepOnly || bddEncodingSizeLimit != -1)
				oneStepPaths = build_one_step_bdds(fac, factor, matrix_for(fac), ord);

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

			if (combineAllBDDsIntoOne && combineITE){
				// if x_0 then (if y_0 then B(0,0) else if y_1 ...) else if x_1 ...: on one-hot
				// assignments of x (state) and y (next state) this is B(s, s'). The state
				// variables lie above the labels, so every ITE only creates a node.
				const int halfFactorVars = data->num_factor_vars / 2;
				BDD chain = _manager->bddZero();
				for (int s = numStates - 1; s >= 0; s--){
					check_budget(fac, "combine_bdds");
					BDD inner = _manager->bddZero();
					for (int ss = numStates - 1; ss >= 0; ss--)
						inner = _manager->bddVar(halfFactorVars + ss).Ite(chosen[s][ss], inner);
					chain = _manager->bddVar(s).Ite(inner, chain);
				}
				ord.transition_BDDs_per_factor[fac] = chain;
			} else if (combineAllBDDsIntoOne){
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
				ord.transition_BDDs_per_factor[fac] = allTransitionsBDD;
			} else {
				ord.transition_BDDs_per_factor_per_state_pair[fac] = std::move(chosen);
			}
		}

		// Cutting changes the BDDs, so it is part of building them and is charged
		// to construction_time. The two reporting passes below are not.
}


void BDDSATEncodingFactory::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	data = make_shared<BDDEncodingData>();
	data->combineAllBDDsIntoOne = combineAllBDDsIntoOne;
	data->implicationalTseitsin = implicationalTseitsin;
	data->pathEncoding = pathEncoding;
	data->pathSharing = pathSharing;
	data->omitForcedVariables = omitForcedVariables;
	data->forcedVariablesThreshold = forcedVariablesThreshold;

	fts_matrices.assign(fts->get_size(), nullptr);

	{
		vector<int> primary = label_order_finder->find_order(*fts);
		assert(int(primary.size()) == fts->get_num_labels());
		data->orderings.resize(alternateLabelOrders ? 2 : 1);
		data->orderings[0].labelOrder = primary;
		// FD's option parser rejects an empty string as a default, hence 'none'
		if (dumpLabelOrderFile != "none") {
			// same format label_order_file reads and optimal_label_order.py --evaluate takes
			ofstream out(dumpLabelOrderFile);
			out << "; label order used by bdd_sat, " << primary.size() << " labels" << endl;
			for (int l : primary) out << l << endl;
		}
		if (alternateLabelOrders){
			// the reverse of the primary order, for the even-numbered steps
			data->orderings[1].labelOrder.assign(primary.rbegin(), primary.rend());
		}
	}

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

	// Label variables in BDD variable order: bdd_variable_order if given,
	// shared by all orderings, and otherwise each ordering's label order.
	vector<int> variableOrder;
	if (bdd_variable_order_finder){
		variableOrder = bdd_variable_order_finder->find_order(*fts);
		assert(int(variableOrder.size()) == fts->get_num_labels());
	}
	for (auto & ord : data->orderings){
		const vector<int> & byVariable = bdd_variable_order_finder ? variableOrder : ord.labelOrder;
		ord.labelPosition.resize(fts->get_num_labels());
		ord.labelToBDDVar.resize(fts->get_num_labels());
		ord.bddVarToLabel = byVariable;
		for (int i = 0; i < fts->get_num_labels(); i++){
			ord.labelPosition[ord.labelOrder[i]] = i;
			ord.labelToBDDVar[byVariable[i]] = data->num_factor_vars + i;
		}
	}

	cout << "Number BDD vars: " << data->bdd_num_vars << " of that "
		 << data->num_factor_vars << " factor state variables"
		 << (data->orderings.size() > 1 ? ", two label orderings." : ".") << endl;

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

	static const Cudd_ReorderingType reorderTypes[] = {CUDD_REORDER_NONE,
		CUDD_REORDER_SIFT, CUDD_REORDER_SIFT_CONVERGE, CUDD_REORDER_SYMM_SIFT,
		CUDD_REORDER_GROUP_SIFT};
	const Cudd_ReorderingType reorderType = reorderTypes[bddReorderMethod];
	if (bddReorderDynamic) _manager->AutodynEnable(reorderType);

	long total_nodes_sum = 0;
	long total_nodes_after_limit = 0;
	int overall_max_pair_nodes = 0;
	double construction_seconds = 0.0;

	try {
		for (size_t o = 0; o < data->orderings.size(); o++){
			if (data->orderings.size() > 1)
				cout << "Building BDDs for label ordering " << o
					 << (o == 0 ? " (primary, odd time steps)" : " (reversed, even time steps)") << endl;
			build_bdds_for_ordering(data->orderings[o], total_nodes_sum,
					total_nodes_after_limit, overall_max_pair_nodes);
		}

		if (bddCutting)
			for (auto & o : data->orderings) cut_bdds_to_fixpoint(o);

		// Reordering is part of building the BDDs that get encoded, so it is
		// charged to construction_time, and reported on its own as well.
		if (bddReorderMethod != 0){
			if (bddReorderDynamic) _manager->AutodynDisable();
			long nodesBefore = 0, nodesAfter = 0;
			for (const auto & o : data->orderings) nodesBefore += encoded_nodes_sum(o);
			const long liveBefore = _manager->ReadNodeCount();
			const double reorderStart = bdd_construction_timer();
			_manager->ReduceHeap(reorderType, 0);
			const double reorderSeconds = bdd_construction_timer() - reorderStart;
			for (const auto & o : data->orderings) nodesAfter += encoded_nodes_sum(o);
			cout << "BDDSTAT reorder method " << bddReorderMethod
				 << " dynamic " << (bddReorderDynamic ? 1 : 0)
				 << " reorderings " << _manager->ReadReorderings()
				 << " time " << reorderSeconds
				 << " nodes_sum_before " << nodesBefore
				 << " nodes_sum_after " << nodesAfter
				 << " live_nodes_before " << liveBefore
				 << " live_nodes_after " << _manager->ReadNodeCount()
				 << endl;
		}

		construction_seconds = bdd_construction_timer();

		if (dumpBDDVariableOrderFile != "none") {
			// label variables by level, top first; same format as dump_label_order
			ofstream out(dumpBDDVariableOrderFile);
			out << "; BDD variable order used by bdd_sat, " << fts->get_num_labels() << " labels" << endl;
			for (int level = 0; level < data->bdd_num_vars; level++){
				const int index = Cudd_ReadInvPerm(_manager->getManager(), level);
				if (index >= data->num_factor_vars)
					out << data->orderings[0].bddVarToLabel[index - data->num_factor_vars] << endl;
			}
		}

		if (bddCovering)
			for (const auto & o : data->orderings) report_covering_implications(o);
		if (reportLabelConjunction)
			report_label_conjunction(data->orderings[0]);
		if (reportLabelImplications)
			for (const auto & o : data->orderings) report_label_implications(o);

		// -----------------------------------------------------------------
		// (d) node in-degrees, used by the omitForcedVariables optimisation.
		//     This is a property of the BDDs, so it is computed once here
		//     rather than per SAT call.
		// -----------------------------------------------------------------
		for (auto & ord : data->orderings)
			for(int fac = 0 ; fac < fts->get_size() ; fac++){
				if (combineAllBDDsIntoOne){
					bdd_in_degree(ord.transition_BDDs_per_factor[fac].getNode());
				} else {
					const TransitionSystem & factor = fts->get_ts(fac);
					for (int s = 0; s < factor.get_size(); s++)
						for (int ss = 0; ss < factor.get_size(); ss++)
							bdd_in_degree(ord.transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode());
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

	if (cubeEncodingMax >= 0) compute_zero_cubes();
	if (labelImplications != 0) compute_implied_clauses();
	if (labelImplications != 0 && impliedBicliques) cover_bicliques();
	data->varLevel.resize(data->bdd_num_vars);
	for (int i = 0; i < data->bdd_num_vars; i++) data->varLevel[i] = _manager->ReadPerm(i);

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


long BDDSATEncodingFactory::encoded_nodes_sum(const BDDEncodingData::Ordering & ord) const {
	long sum = 0;
	if (combineAllBDDsIntoOne){
		for (const BDD & bdd : ord.transition_BDDs_per_factor) sum += bdd.nodeCount();
	} else {
		for (const auto & per_source : ord.transition_BDDs_per_factor_per_state_pair)
			for (const auto & per_target : per_source)
				for (const BDD & bdd : per_target)
					if (bdd != _manager->bddZero()) sum += bdd.nodeCount();
	}
	return sum;
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
		const BDDEncodingData::Ordering & ord,
		const vector<int> & factorVars,
		const vector<int> & labelVars,
		const vector<int> & nextFactorVars) const {
	const int nfv = data->num_factor_vars;
	if (bddvar >= nfv){
		return labelVars[ord.bddVarToLabel[bddvar - nfv]];
	}
	if (bddvar < nfv / 2) {
		assert(factorVars.size() > size_t(bddvar));
		return factorVars[bddvar];
	}
	assert(int(nextFactorVars.size()) > bddvar - nfv / 2);
	return nextFactorVars[bddvar - nfv / 2];
}


void BDDSATEncoding::bdd_to_cnf_paths(const vector<pair<DdNode *, vector<int>>> & roots,
		const BDDEncodingData::Ordering & ord,
		const vector<int> & factorVars,
		const vector<int> & labelVars,
		const vector<int> & nextFactorVars){
	// Variables mean "on the path from the active root to 1". Nodes are (regular node,
	// complement parity): the terminal an edge reaches depends on the parity of the
	// complemented edges on the way. child: index of the child, -1 = 1, -2 = 0.
	struct PNode { int var; int x; int level; int edge[2]; int child[2]; };
	vector<PNode> nodes;
	map<pair<DdNode *, bool>, int> index;
	function<int(DdNode *, bool)> visit = [&](DdNode * n, bool c) -> int {
		DdNode * r = Cudd_Regular(n);
		const bool parity = c ^ (Cudd_IsComplement(n) != 0);
		if (Cudd_IsConstant(r)) return parity ? -2 : -1;
		auto it = index.find({r, parity});
		if (it != index.end()) return it->second;
		const int id = nodes.size();
		index[{r, parity}] = id;
		const int x = Cudd_NodeReadIndex(r);
		nodes.push_back({sat->new_variable(), x, data->varLevel[x], {0, 0}, {0, 0}});
		const int t = visit(Cudd_T(r), parity);
		const int e = visit(Cudd_E(r), parity);
		nodes[id].child[1] = t; nodes[id].child[0] = e;
		return id;
	};
	const int INF = INT_MAX;

	// roots: activation literal (0 = always active), root node and its level
	struct Root { int act; int node; int level; };
	vector<Root> active_roots;
	for (const auto & [root, conditions] : roots) {
		const int rid = visit(root, false);
		if (rid == -2) { sat->notAll(conditions); continue; }
		int act = 0;
		if (conditions.size() == 1) act = conditions[0];
		else if (conditions.size() > 1) {
			act = sat->new_variable();
			for (int c : conditions) sat->implies(act, c);
			vector<int> c = conditions; c.push_back(-act);
			sat->notAll(c);
		}
		active_roots.push_back({act, rid, rid >= 0 ? nodes[rid].level : INF});
	}
	if (active_roots.empty()) return;

	vector<vector<int>> incoming(nodes.size());
	vector<char> always(nodes.size(), 0);
	for (const Root & r : active_roots) {
		if (r.node < 0) continue;
		if (r.act == 0) { sat->assertYes(nodes[r.node].var); always[r.node] = 1; }
		else { sat->implies(r.act, nodes[r.node].var); incoming[r.node].push_back(r.act); }
	}
	for (PNode & nd : nodes) {
		const int v = givevar(nd.x, ord, factorVars, labelVars, nextFactorVars);
		vector<int> out;
		for (int k = 0; k < 2; k++) {
			const int xl = k ? v : -v;   // the literal this edge stands for
			if (nd.child[k] == -2) {     // edge into 0: node -> -literal
				sat->notAll(vector<int>{nd.var, xl});
				continue;
			}
			const int ev = nd.edge[k] = sat->new_variable();
			out.push_back(ev);
			sat->implies(ev, nd.var);                       // T2
			sat->implies(ev, xl);                           // T4
			sat->notAll(vector<int>{nd.var, xl, -ev});      // P1: node & literal -> edge
			if (nd.child[k] >= 0) {
				sat->implies(ev, nodes[nd.child[k]].var);   // T3
				incoming[nd.child[k]].push_back(ev);
			}
		}
		sat->impliesOr(nd.var, out);                        // T1
	}
	for (size_t i = 0; i < nodes.size(); i++)              // P2
		if (!always[i]) sat->impliesOr(nodes[i].var, incoming[i]);

	// "some root is active": the P3 clauses only hold then
	int on = 0;   // 0 = always
	bool always_on = false;
	for (const Root & r : active_roots) if (r.act == 0) always_on = true;
	if (!always_on) {
		if (active_roots.size() == 1) on = active_roots[0].act;
		else {
			on = sat->new_variable();
			vector<int> acts;
			for (const Root & r : active_roots) { acts.push_back(r.act); sat->implies(r.act, on); }
			sat->impliesOr(on, acts);
		}
	}

	// P3: literal of a support variable -> an edge with that value at its level, an edge
	// jumping over its level, or an active root below it
	map<int, int> level_var;   // level -> BDD variable index, support only
	for (const auto & nd : nodes) level_var[nd.level] = nd.x;
	vector<int> levels;
	for (const auto & [lev, x] : level_var) levels.push_back(lev);
	auto level_index = [&](int lev) { return lower_bound(levels.begin(), levels.end(), lev) - levels.begin(); };
	vector<vector<int>> skipping(levels.size());
	vector<array<vector<int>, 2>> at(levels.size());
	vector<char> no_clause(levels.size(), 0);   // an always active root lies below
	for (const auto & nd : nodes) {
		const size_t i = level_index(nd.level);
		for (int k = 0; k < 2; k++) {
			if (nd.edge[k] == 0) continue;
			at[i][k].push_back(nd.edge[k]);
			const int cl = nd.child[k] < 0 ? INF : nodes[nd.child[k]].level;
			for (size_t j = i + 1; j < levels.size() && levels[j] < cl; j++) skipping[j].push_back(nd.edge[k]);
		}
	}
	for (const Root & r : active_roots)
		for (size_t j = 0; j < levels.size() && levels[j] < r.level; j++) {
			if (r.act == 0) no_clause[j] = 1;
			else skipping[j].push_back(r.act);
		}
	for (size_t i = 0; i < levels.size(); i++) {
		if (no_clause[i]) continue;
		const int v = givevar(level_var[levels[i]], ord, factorVars, labelVars, nextFactorVars);
		for (int k = 0; k < 2; k++) {
			vector<int> c;   // notAll: on & literal & no edge -> false
			if (on != 0) c.push_back(on);
			c.push_back(k ? v : -v);
			for (int e : at[i][k]) c.push_back(-e);
			for (int e : skipping[i]) c.push_back(-e);
			sat->notAll(c);
		}
	}
}


void BDDSATEncoding::bdd_to_cnf(DdNode * node,
		const BDDEncodingData::Ordering & ord,
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
	int var_to_branch = givevar(Cudd_NodeReadIndex(node), ord, factorVars, labelVars, nextFactorVars);
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
				bdd_to_cnf(otherbranch, ord, currentConditions, factorVars, labelVars, nextFactorVars);
				// this can happen for only one branch (otherwise the BDD is not reduced)
				return;
			}
			if (Cudd_IsConstant(branch) && !Cudd_IsComplement(branch)){
				// this branch immediately leads to true, so the other branch is only
				// relevant if the condition is false: the negation of the branch
				// variable essentially becomes a new condition.
				vector<int> newConditions = currentConditions;
				newConditions.push_back(-branch_var);
				bdd_to_cnf(otherbranch, ord, newConditions, factorVars, labelVars, nextFactorVars);
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

	bdd_to_cnf(true_branch, ord, trueVarVector, factorVars, labelVars, nextFactorVars);
	bdd_to_cnf(false_branch, ord, falseVarVector, factorVars, labelVars, nextFactorVars);
	if (!data->implicationalTseitsin){
		// if the variable for this one is false, and we take a branch, then that
		// variable also must be false.
		trueVarVector[0] *= -1;
		falseVarVector[0] *= -1;
		bdd_to_cnf(Cudd_Not(true_branch), ord, trueVarVector, factorVars, labelVars, nextFactorVars);
		bdd_to_cnf(Cudd_Not(false_branch), ord, falseVarVector, factorVars, labelVars, nextFactorVars);
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
	// Which ordering this time step is encoded in. Its BDDs constrain only this
	// step's label variables, so neighbouring steps may differ.
	const BDDEncodingData::Ordering & ord = data->ordering_at(fromTime);

	const vector<int> noFactorVars;
	for(int fac = 0 ; fac < fts->get_size() ; fac++){
		// the Tseitin variables are only shared within one factor
		tseitsinVars.clear();

		// a BDD with enumerated 0-cubes: one clause per cube, no Tseitin variables
		auto encode_cubes = [&](DdNode * node, const vector<int> & conditions,
				const vector<int> & factorVars, const vector<int> & nextFactorVars) {
			auto it = data->zero_cubes.find(node);
			if (it == data->zero_cubes.end()) return false;
			vector<int> lits;
			for (const vector<int> & cube : it->second) {
				lits = conditions;
				for (int v : cube) {
					const int var = givevar(abs(v) - 1, ord, factorVars, labelVars, nextFactorVars);
					lits.push_back(v > 0 ? var : -var);
				}
				sat->notAll(lits);
			}
			return true;
		};
		if (data->combineAllBDDsIntoOne){
			const vector<int> noConditions;
			DdNode * node = ord.transition_BDDs_per_factor[fac].getNode();
			if (data->pathEncoding)
				bdd_to_cnf_paths({{node, noConditions}}, ord, previousStateVars[fac], labelVars, nextStateVars[fac]);
			else if (!encode_cubes(node, noConditions, previousStateVars[fac], nextStateVars[fac]))
				bdd_to_cnf(node, ord, noConditions, previousStateVars[fac], labelVars, nextStateVars[fac]);
		} else {
			const TransitionSystem & factor = fts->get_ts(fac);
			vector<pair<DdNode *, vector<int>>> shared_roots;
			for (int s = 0; s < factor.get_size(); s++){
				for (int ss = 0; ss < factor.get_size(); ss++){
					// The factor state variables are not part of these BDDs, they are
					// the conditions instead.
					const vector<int> conditions = {previousStateVars[fac][s], nextStateVars[fac][ss]};
					DdNode * node = ord.transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode();
					if (data->pathEncoding && data->pathSharing)
						shared_roots.push_back({node, conditions});
					else if (data->pathEncoding)
						bdd_to_cnf_paths({{node, conditions}}, ord, noFactorVars, labelVars, noFactorVars);
					else if (!encode_cubes(node, conditions, noFactorVars, noFactorVars))
						bdd_to_cnf(node, ord, conditions, noFactorVars, labelVars, noFactorVars);
				}
			}
			if (!shared_roots.empty())
				bdd_to_cnf_paths(shared_roots, ord, noFactorVars, labelVars, noFactorVars);
		}
	}
	tseitsinVars.clear();

	// label_implications: clauses implied by the BDDs, over this step's label variables
	if (!data->implied_clauses.empty()) {
		const size_t o = (fromTime - 1) % data->orderings.size();
		vector<int> negs;
		for (const auto & c : data->implied_clauses[o]) {
			negs.clear();
			for (int l : c) negs.push_back(l > 0 ? -labelVars[l - 1] : labelVars[-l - 1]);
			sat->notAll(negs);
		}
		if (!data->implied_bicliques.empty())
			for (const auto & bc : data->implied_bicliques[o]) {
				const int z = sat->new_variable();
				DEBUG(sat->registerVariable(z, "biclique@" + to_string(fromTime)));
				for (int l : bc.A) sat->implies(l > 0 ? labelVars[l - 1] : -labelVars[-l - 1], z);
				for (int l : bc.B) sat->implies(l > 0 ? labelVars[l - 1] : -labelVars[-l - 1], -z);
			}
		for (const auto & cc : data->implied_cond_clauses[o]) {
			negs = {previousStateVars[cc.factor][cc.source]};
			for (int l : cc.lits) negs.push_back(l > 0 ? -labelVars[l - 1] : labelVars[-l - 1]);
			sat->notAll(negs);
		}
	}
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
		for (const int & l : data->ordering_at(labelTimestep).labelOrder)
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
