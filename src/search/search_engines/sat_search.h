#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_engine.h"
#include "../option_parser.h"
#include "../plugin.h"
#include "sat_encoder.h"
#include "../task_representation/transition_system.h"
#include "../task_representation/label_equivalence_relation.h"

#include "../task_utils/label_order_finder.h"

struct BlockInfo {
    std::vector<int> empty_rows;
    std::vector<int> empty_columns;
    std::map<int, std::vector<int>> extra_ones_per_row;    // rows with 1's outside block
    std::map<int, std::vector<int>> extra_ones_per_column; // columns with 1's outside block
};


namespace plugins {
	class Feature;
}

namespace label_order_finder {
	class LabelOrderFinder;
}

namespace fts_base_manager{
	class FTSBaseManager {
		protected:
			std::shared_ptr<task_representation::FTSTask> fts;
		public:
			FTSBaseManager(std::shared_ptr<task_representation::FTSTask> __fts);
	};
}

namespace label_manager{

	class LabelManager{
		private:
			std::shared_ptr<task_representation::FTSTask> fts;
			std::vector<std::vector<int>> allTimesLabelVars;

			std::vector<int> labelOrder;
			std::vector<std::vector<int>> relevantLabels;
			std::vector<std::vector<int>> labelsWithoutOnlySelfLoops;
			std::map<int, std::map<int, std::vector<int>>> labelsWithEffectOnValue;
			std::map<int,std::vector<std::vector<int>>> labelGroups;

		public:
			bool containsSelfLoops(int ts, int label);
			bool isAlwaysSelfLoop(int ts, int label);
			bool isIrrelevantLabel(int ts, int label) const;
			int getLabelSATVar(int label);
			int getOrderedLabel(int index);
			int getRelevantLabel(int ts, int labelIndex);
			int getNumRelevantLabels(int ts);
			std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
			std::map<int, std::vector<int>> generateLabelGroupVars(void* solver, sat_capsule & capsule, std::vector<int> &labelVars/* , int timestep */);
	};
}

namespace states_manager{

		class StatesManager{
			private:
				std::shared_ptr<task_representation::FTSTask> fts;
				std::vector<std::vector<std::vector<int>>> allTimesStateVars;
			public:
				std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
				int getPreviousStateSATVar(int ts, int state);
				int getNextStateSATVar(int ts, int state);
		};
}

namespace transitions_encoding{

	class TransitionsEncoding : public fts_base_manager::FTSBaseManager {
		protected:
			std::shared_ptr<label_manager::LabelManager> lm;
			std::shared_ptr<states_manager::StatesManager> sm;
			std::map<std::tuple<int,int>, std::vector<std::vector<int>>> states_label_can_move_from_to_and_non_encoded_state;
			std::map<std::tuple<int,int,int>, std::vector<int>> moving_to_different_state_vars;
		public:
			TransitionsEncoding(std::shared_ptr<task_representation::FTSTask> __fts);
			//virtual ~TransitionEncoding() = default;
			
			bool isSelfLoop(task_representation::Transition t) const;//TODO: MOVE TO TRANSITIONS FILE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			virtual bool has_to_encode_transition(task_representation::Transition transition) = 0;
			std::vector<int> get_all_states_label_can_move_to(int ts, int label);
			std::vector<int> get_all_states_label_can_move_from(int ts, int label);
			std::vector<std::vector<int>> compute_possible_label_movements(int ts, int label);
			std::vector<int> get_all_non_encoded_self_loop_states(int ts, int label);

			virtual void generateTransitionVars(void* solver, sat_capsule &capsule) = 0;

			std::vector<int> getPreconditions(int src, int ts, int relevantLabel);
			virtual void appendPreconditions(std::vector<int> &impliesOrPrec, int ts, int relevantLabelPrec, int src) = 0;

			std::vector<int> get_all_later_vars_moving_to_different_state(int target, int ts, int relevantLabel);
			virtual void appendEffects(std::vector<int> &impliesOrEff, int ts, int relevantLabelEff, int target) = 0;

			virtual std::vector<int> get_all_sat_vars_moving_to_state(int ts, int label, int state) = 0;
			virtual void encode_label_consistency(void* solver, int ts, int relevantLabelIndex) = 0;

			void encodeRegularPreconditionsAndEffects(int ts, int relevantLabel);

	};


	
	class FullTransitionsEncoding : public TransitionsEncoding {
		private:
			bool no_selfloop_SATvars;
			std::vector<std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>>> allTimesTransitionVars;//[transition_systems[labels[<transition, SATVar>, <transition, SATVar>...]]] per timestep
			std::map<std::tuple<int,int,int>, std::vector<int>> moving_to_state_vars;
			std::map<std::tuple<int,int>, std::vector<int>> states_with_non_encoded_transitions;
		public:
			FullTransitionsEncoding(bool __no_selfloop_SATvars, std::shared_ptr<task_representation::FTSTask> __fts);
			bool has_to_encode_transition(task_representation::Transition transition) override;
			std::vector<int> get_all_non_encoded_self_loop_states(int ts, int label);
			void encode_label_consistency(void* solver, int ts, int relevantLabelIndex);

			void generateTransitionVars(void* solver, sat_capsule &capsule) override;
			void appendNewTransitionVars(std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> &newVars);
			void appendPreconditions(std::vector<int> &impliesOrPrec, int ts, int relevantLabelPrec, int src);
			void appendEffects(std::vector<int> &impliesOrEff, int ts, int relevantLabelEff, int target);
			//void encodePreconditions() const override;
			//void encodeEffects() const override;
			std::vector<int> get_all_sat_vars_moving_to_state(int ts, int label, int state);
	};

	class SplitTransitionsEncoding : public TransitionsEncoding {
		public:
			bool has_to_encode_transition(task_representation::Transition transition) override;
	};

}


namespace sat_search{

class SATSearch : public SearchEngine {
private:
	std::shared_ptr<label_order_finder::LabelOrderFinder> label_order_finder;

	int stepTimeLimit;

	// global limit
	int planLength;

	// for iteration	
	int stepNumber;
	int currentLength;
	int start_length;
	double multiplier;
	
	bool length_by_iteration; 
	int maximum_iteration;

	bool do_R2_encoding;
	bool no_selfloop_SATvars;
	bool computing_block;


	bool sequential = false;
	bool selfloopParallelism = false;
	bool chainsParallelism = false;

	bool useLabelGroups = false;
	bool useSelfloopOptimisation = false;

	bool basic_per_row = false;
	bool eliminating_rnc_and_pairs = false;

	bool forceAtLeastOneAction;
	
	std::shared_ptr<task_representation::FTSTask> fts;

    std::vector<int> labelOrder;
    std::vector<std::vector<int>> relevantLabels;
	std::map<int, std::map<int, BlockInfo>> labelBasedEncodingInfo;
	std::vector<std::vector<int>> labelsWithoutOnlySelfLoops;
	std::map<int, std::map<int, std::vector<int>>> labelsWithEffectOnValue;
	std::map<int, std::map<int, std::set<int>>> empty_rows, empty_cols;
	std::map<int, std::map<int, std::map<int, std::set<int>>>> ones_per_row/* , ones_per_column */;
	std::map<int, std::vector<std::vector<int>>> labelProjection;
	std::map<int, std::map<int, std::vector<int>>> empty_projected_cells_per_row;//ts->row->cols

	std::map<int,std::vector<std::vector<int>>> labelGroups;

protected:
    virtual void initialize() override;
	bool containsSelfLoops(int ts, int label);
	virtual bool isAlwaysSelfLoop(int ts, int label);
	bool hasMixedTransitions(int ts, int label);
	int findPreviousValidAuxVar(std::vector<int> &auxVars, int label);
	BlockInfo find_largest_block(const std::vector<std::vector<int>>& filled_columns_per_row);
	bool hasSelfLoopOnValue(int ts, int value, int label);
	virtual void checkSolution(std::vector<std::vector<std::vector<int>>> &allTimesStateVars, std::vector<std::vector<int>> &allTimesLabelVars, 
		std::vector<std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>>> &allTimesTransitionVars, int length, void* solver);
    virtual SearchStatus step() override;
    virtual std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> generateTransitionVars(void* solver, sat_capsule &capsule);
    virtual std::map<int, std::map<int, std::vector<int>>> generateAuxVars(sat_capsule &capsule);
    virtual std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
	std::map<int, std::vector<int>> generateLabelGroupVars(void* solver, sat_capsule & capsule, std::vector<int> &labelVars/* , int timestep */);
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars(sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<int>>> getApplicableLabels();
    virtual std::map<int, std::map<int, std::map<int, std::vector<int>>>> getSuccessorStates(std::map<int, std::map<int, std::vector<int>>> applicableLabels);

public:
    explicit SATSearch(const Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
