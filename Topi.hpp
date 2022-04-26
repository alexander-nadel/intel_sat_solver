// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <span>
#include <limits>
#include <bit>
#include <string>
#include <any>
#include <fstream>  
#include <queue>  
#include <memory>

#include "ToporExternalTypes.hpp"
#include "BitArray.hpp"
#include "TopiHandleNewUserCls.hpp"
#include "ToporDynArray.hpp"
#include "ToporVector.hpp"
#include "TopiGlobal.hpp"
#include "TopiParams.hpp"
#include "TopiVarScores.hpp"
#include "ToporWinAverage.hpp"

using namespace std;

namespace Topor
{
	// The main solver class
	class CTopi
	{
	public:
		// varNumHint is the expected number of variables -- a non-mandatory hint, which, if provided correctly, helps the solver initialize faster 		
		CTopi(TLit varNumHint = 0);
		// DUMPS
		void AddUserClause(const span<TLit> c);
		// DUMPS
		TToporReturnVal Solve(const span<TLit> userAssumps = {}, pair<double, bool> toInSecIsCpuTime = make_pair(numeric_limits<double>::max(), true), uint64_t confThr = numeric_limits<uint64_t>::max());
		bool IsAssumptionRequired(size_t assumpInd);
		TToporLitVal GetValue(TLit l) const;
		std::vector<TToporLitVal> GetModel() const;
		TToporStatistics GetStatistics() const;		
		string GetParamsDescr() const;
		// Set a parameter value; using double for avalue, since it encompasses all the arithmetic types, which can be used for parameters, that is:
		// Signed and unsigned integers of at most 32 bits and floating-points of at most the size of a C++ double 
		// DUMPS
		void SetParam(const string& paramName, double newVal);
		// Is there an error?
		bool IsError() const;
		// Get the explanation for the current status (returns the empty string, if there is no error and verbosity is off)
		string GetStatusExplanation() const { return IsError() || m_ParamVerbosity > 0 ? m_StatusExplanation : ""; }
		// Dump DRAT, if invoked
		void DumpDrat(ofstream& openedDratFile, bool isDratBinary) { m_OpenedDratFile = &openedDratFile; m_IsDratBinary = isDratBinary; }
		// Set callback: stop-now
		void SetCbStopNow(TCbStopNow CbStopNow) { M_CbStopNow = CbStopNow; }
		// Interrupt now
		void InterruptNow() { m_InterruptNow = true; }
		// Set callback: new-learnt-clause
		void SetCbNewLearntCls(TCbNewLearntCls CbNewLearntCls) { M_CbNewLearntCls = CbNewLearntCls; }
		// Boost the score of the variable v by mult
		// DUMPS
		void BoostScore(TLit vExternal, double mult = 1.0);
		// Fix the polarity of the variable |l| to l
		// onlyOnce = false: change until ClearUserPolarityInfo(|l|) is invoked (or, otherwise, forever)
		// onlyOnce = true: change only once right now
		// DUMPS
		void FixPolarity(TLit lExternal, bool onlyOnce = false);
		// Clear any polarity information of the variable v, provided by the user (so, the default solver's heuristic will be used to determine polarity)
		// DUMPS
		void ClearUserPolarityInfo(TLit vExternal);		
		// Backtrack to the end of decLevel; the 2nd parameter is required for statistics only
		// DUMPS
		void Backtrack(TUV decLevel, bool isBCPBacktrack = false, bool reuseTrail = false, bool isAPICall = false);
	protected:	
		/*
		* Parameters
		*/

		// Contains a map from parameter name to parameter description & function to set it for every parameter
		// Every parameter adds itself to the list, so we don't have to do it manually in this class
		// To add a parameter, it suffices to declare it below, c'est tout!
		CTopiParams m_Params;		
		// The following semantics would have been better, 
		// but double template parameters are not supported even by gcc 10.2 (although the support is there in gcc trunc as of Jan., 2021)
		// CTopiParam template class semantics: <type, initVal, minVal, maxVal>, where:
		//		type must be an arithmetic type, where a floating-point type must fit into a double, and an integer-type must fit into half-a-double
		//		minVal <= initVal <= maxVal

		// To find the mode values using a regular expression: ,( *){.*,.*,.*,.*,.*,.*}(,| )
		// The special mode meta-parameter; Every parameter can be provided either one single default or a separate default for each mode (in an array)
		CTopiParam<TModeType> m_Mode = { m_Params, m_ModeParamName, "The mode (0: Unweighted MaxSAT; 1: FLA; 2: FSN; 3: MF2S; 4: LAR; 5: Weighted MaxSAT; 6: FRU; 7: SN1)", 0, 0, m_Modes - 1 };
		
		// Parameters: verbosity
		CTopiParam<uint8_t> m_ParamVerbosity = { m_Params, "/verbosity/level", "Verbosity level (0: silent; 1: basic statistics; 2: trail&conflict debugging; 3: extensive debugging)", 0, 0, 3};
		CTopiParam<uint32_t> m_ParamHeavyVerbosityStartConf = { m_Params, "/verbosity/verbosity_start_conf", "Meaningful only if /verbosity/level>1: print-out at level > 1 starting with the provided conflicts number", 0 };
		CTopiParam<uint32_t> m_ParamStatPrintOutConfs = { m_Params, "/verbosity/print_out_confs", "Meaningful only if /verbosity/level>0: the rate of print-outs in #conflicts", 2000, 1 };

		// Parameters: timeout
		CTopiParam<double> m_ParamOverallTimeout = { m_Params, "/timeout/global", "An overall global timeout for Topor lifespan: Topor will be unusable after reaching this timeout (note that one can also provide a Solve-specific timeout as a parameter to Solve)", numeric_limits<double>::max(), numeric_limits<double>::epsilon() };
		CTopiParam<bool> m_ParamOverallTimeoutIsCpu = { m_Params, "/timeout/global_is_cpu", "Is the overall global timeout (if any) for Topor lifespan CPU (or, otherwise, Wall)", false };
		
		// Parameters: decision
		CTopiParam<uint8_t> m_ParamInitPolarityStrat = { m_Params, "/decision/polarity/init_strategy", "The initial polarity for a new variable: 0: negative; 1: positive; 2: random",  {1, 1, 1, 1, 1, 2, 1, 1}, 0, 2 };
		CTopiParam<uint8_t> m_ParamPolarityStrat = { m_Params, "/decision/polarity/strategy", "How to set the polarity for a non-forced variable: 0: phase saving; 1: random", 0, 0, 1 };
		CTopiParam<double> m_ParamVarActivityInc = { m_Params, "/decision/vsids/var_activity_inc", "Variable activity bumping factor's initial value: m_PosScore[v].m_Score += m_VarActivityInc is carried out for every variable visited during a conflict (hard-coded to 1.0 in Glucose-based solvers)", {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.8}, 0.0 };
		CTopiParam<double> m_ParamVarActivityIncDecay = { m_Params, "/decision/vsids/var_activity_inc_decay", "After each conflict, the variable activity bumping factor is increased by multiplication by 1/m_ParamVarActivityIncDecay (the initial value is provided in the parameter):  m_ParamVarActivityInc *= (1 / m_ParamVarActivityIncDecay), where it's 0.8 in Glucose-based solvers and 0.95 in Minisat", {0.95, 0.8, 0.8, 0.8, 0.8, 0.95, 0.8, 0.95}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<bool> m_ParamVarActivityIncDecayReinitN = { m_Params, "/decision/vsids/var_activity_inc_decay_reinit_n", "Re-initialize /decision/vsids/var_activity_inc_decay before normal incremental Solve?", {true, false, false, false, false, true, false, true} };
		CTopiParam<bool> m_ParamVarActivityIncDecayReinitS = { m_Params, "/decision/vsids/var_activity_inc_decay_reinit_s", "Re-initialize /decision/vsids/var_activity_inc_decay before short incremental Solve?", false };
		CTopiParam<uint32_t> m_ParamVarActivityIncDecayStopReinitSInv = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_s_inv", "The first short incremental invocation to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0 };
		CTopiParam<uint32_t> m_ParamVarActivityIncDecayStopReinitRestart = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_restart", "The first restart to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0 };
		CTopiParam<uint32_t> m_ParamVarActivityIncDecayStopReinitConflict = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_conflict", "The first conflict to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0 };
		CTopiParam<double> m_ParamVarActivityIncDecayStopReinitTime = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_time", "The time in sec. to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0.0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0.0 };
		CTopiParam<double> m_ParamVarActivityIncDecayReinitVal = { m_Params, "/decision/vsids/var_activity_inc_decay_reinit_val", "For /decision/vsids/var_activity_inc_decay_reinit_each_solve=1: the value to re-initialize /decision/vsids/var_activity_inc_decay before each Solve", 0.8, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<double> m_ParamVarDecayInc = { m_Params, "/decision/vsids/var_decay_inc", "The increment value for var_activity_inc_decay each var_decay_update_conf_rate conflicts:  m_ParamVarDecay += m_ParamVarDecayInc (hard-coded to 0.01 in Glucose-based solvers)", 0.01, 0, 1.0 };
		CTopiParam<double> m_ParamVarDecayMax = { m_Params, "/decision/vsids/var_decay_max", "The maximal value for var_activity_inc_decay", {0.99, 0.95, 0.9, 0.95, 0.95, 0.99, 0.95, 0.99}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<uint32_t> m_ParamVarDecayUpdateConfRate = { m_Params, "/decision/vsids/var_decay_update_conf_rate", "The rate in conflicts of var_decay's update (5000 in Glucose-based solvers)", 5000, 1 };
		inline bool IsVsidsInitOrderParam(const string& paramName) const { return paramName == "/decision/vsids/init_order"; }
		CTopiParam<bool> m_ParamVsidsInitOrder = { m_Params, "/decision/vsids/init_order", "The order of inserting into the VSIDS heap (0: bigger indices first; 1: smaller indices first)", {false, true, false, true, true, false, true, false} };
		CTopiParam<bool> m_ParamVarActivityGlueUpdate = { m_Params, "/decision/vsids/var_activity_glue_update", "Do we increase the VSIDS score for every last-level variable, visited during a conflict, whose parent is a learnt clause with LBD score lower than that of the newly learnt clause?", {true, true, true, false, true, true, false, false} };
		CTopiParam<bool> m_ParamVarActivityUseMapleLevelBreaker = { m_Params, "/decision/vsids/var_activity_use_maple_level_breaker", "Maple (MapleLCMDistChronoBT-f2trc-s) multiplies the variable activity bumping factor by 1.5 for variables, whose dec. level is higher-than-or-equal than 2nd highest-1 and by 0.5 for other variables; use it?", {false, false, false, true, true, false, true, false} };
		CTopiParam<bool> m_ParamVarActivityUseMapleLevelBreakerAi = { m_Params, "/decision/vsids/var_activity_use_maple_level_breaker_ai", "After initial call: Maple (MapleLCMDistChronoBT-f2trc-s) multiplies the variable activity bumping factor by 1.5 for variables, whose dec. level is higher-than-or-equal than 2nd highest-1 and by 0.5 for other variables; use it?", {false, false, false, true, true, false, false, false} };
		CTopiParam<uint32_t> m_ParamVarActivityMapleLevelBreakerDecrease = { m_Params, "/decision/vsids/var_activity_maple_level_breaker_decrease", "If var_activity_use_maple_level_breaker is on, this number is the decrease in the 2nd highest level, so that if the variable is higher-than-or-equal, its bump is more significant", {1, 1, 1, 1, 0, 1, 1, 1} };
		
		CTopiParam<uint8_t> m_ParamInitClssBoostScoreStrat = { m_Params, "/decision/init_clss_boost/strat", "Initial query: variable score boost strategy for user clauses -- 0: none; 1: no clause-size, user-order; 2: clause-size-aware, user-order; 3: no clause-size, reverse-user-order; 4: clause-size-aware, reverse-user-order", {2, 0, 0, 0, 0, 0, 4, 2}, 0, 4 };
		CTopiParam<uint8_t> m_ParamInitClssBoostScoreStratAfterInit = { m_Params, "/decision/init_clss_boost/strat_after_init", "After initial query: variable score boost strategy for user clauses -- 0: none; 1: no clause-size, user-order; 2: clause-size-aware, user-order; 3: no clause-size, reverse-user-order; 4: clause-size-aware, reverse-user-order", {1, 0, 0, 0, 0, 0, 0, 1}, 0, 4 };
		
		CTopiParam<double> m_ParamInitClssBoostMultHighest = { m_Params, "/decision/init_clss_boost/mult_highest", "Variable score boost for initial clauses: highest mult value", {10., 10., 10., 10., 10., 10., 10., 5.} };
		CTopiParam<double> m_ParamInitClssBoostMultLowest = { m_Params, "/decision/init_clss_boost/mult_lowest", "Variable score boost for initial clauses: lowest mult value", 1. };
		CTopiParam<double> m_ParamInitClssBoostMultDelta = { m_Params, "/decision/init_clss_boost/mult_delta", "Variable score boost for initial clauses: delta mult value",  {0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.01} };

		// Parameters: backtracking
		CTopiParam<TUV> m_ParamChronoBtIfHigherInit = { m_Params, "/backtracking/chrono_bt_if_higher_init", "Initial query: backtrack chronologically, if the decision level difference is higher than the parameter", {100, numeric_limits<TUV>::max(), 50, 100, 100, 0, 100, 0} };
		CTopiParam<TUV> m_ParamChronoBtIfHigherN = { m_Params, "/backtracking/chrono_bt_if_higher_n", "Normal (non-short) incremental query: Backtrack chronologically, if the decision level difference is higher than the parameter", {0, numeric_limits<TUV>::max(), 50, 100, 100, 0, 100, 0} };
		CTopiParam<TUV> m_ParamChronoBtIfHigherS = { m_Params, "/backtracking/chrono_bt_if_higher_s", "Short incremental query: Backtrack chronologically, if the decision level difference is higher than the parameter", {100, numeric_limits<TUV>::max(), 50, 100, 100, 0, 100, 100} };
		
		CTopiParam<uint32_t> m_ParamConflictsToPostponeChrono = { m_Params, "/backtracking/conflicts_to_postpone_chrono", "The number of conflicts to postpone considering any backtracking, but NCB", {0, 0, 0, 4000, 8000, 0, 4000, 0} };
		CTopiParam<uint8_t> m_ParamCustomBtStrat = { m_Params, "/backtracking/custom_bt_strat", "0: no custom backtracking; 1, 2: backtrack to the level containing the variable with the best score *instead of any instances of supposed chronological backtracking*, where ties are broken based on the value -- 1: higher levels are preferred; 2: lower levels are preferred ", {2, 0, 0, 0, 1, 2, 0, 0}, 0, 2 };
		CTopiParam<bool> m_ParamReuseTrail = { m_Params, "/backtracking/reuse_trail", "0: no trail re-usage; 1: re-use trail (the idea is from SAT'20 Hickey&Bacchus' paper, but our implementation is fully compatible with CB)", 0 };

		// Parameters: BCP
		constexpr uint8_t InitEntriesPerWL() { return 4 >= TWatchInfo::BinsInLongBitCeil ? 4 : TWatchInfo::BinsInLongBitCeil; };
		CTopiParam<uint8_t> m_ParamInitEntriesPerWL = { m_Params, "/bcp/init_entries_per_wl", "BCP: the number of initial entries in a watch list", InitEntriesPerWL(), TWatchInfo::BinsInLongBitCeil };
		CTopiParam<uint8_t> m_ParamBCPWLChoice = { m_Params, "/bcp/wl_choice", "User clause processing: how to choose the watches -- 0: prefer shorter WL; 1: prefer longer WL; 2: disregard WL length", {0, 2, 0, 1, 0, 0, 1, 0}, 0, 2 };
		CTopiParam<uint8_t> m_ParamExistingBinWLStrat = { m_Params, "/bcp/existing_bin_wl_start", "BCP: what to do about duplicate binary clauses -- 0: nothing; 1: boost their VSIDS score; 2: add another copy to the watches", 1, 0, 2 };
		CTopiParam<double> m_ParamBinWLScoreBoostFactor = { m_Params, "/bcp/bin_wl_start_score_boost_factor", "BCP: if /bcp/existing_bin_wl_start=1, what's the factor for boosting the scores", {1., 1., 1., 1., 1., 1., 1., 0.5}, numeric_limits<double>::epsilon() };
		CTopiParam<uint8_t> m_ParamBestContradictionStrat = { m_Params, "/bcp/best_contradiction_strat", "BCP's best contradiction strategy: 0: size; 1: glue; 2: first; 3: last", {0, 0, 0, 0, 0, 0, 0, 3}, 0, 3 };

		// Parameters: Add-user-clause
		CTopiParam<uint32_t> m_ParamAddClsRemoveClssGloballySatByLitMinSize = { m_Params, "/add_user_clause/remove_clss_globally_sat_by_literal_larger_size", "Assigned literal strategy: check for literals satisfied at decision level 0 and remove globally satisfied clauses for sizes larger than this parameter", numeric_limits<uint32_t>::max() };
		CTopiParam<bool> m_ParamAddClsAtLevel0 = { m_Params, "/add_user_clause/guarantee_level_0", "Guarantee that adding a user clause always occurs at level 0 (by backtracking to 0 after every solve)", false };

		// Parameters: Query type
		CTopiParam<uint32_t> m_ParamShortQueryConfThrInv = { m_Params, "/query_type/short_query_conf_thr", "If the conflict threshold is at most this parameter, this invocation is considered to be incremental short", 10000 };

		// Parameters: debugging
		CTopiParam<uint8_t> m_ParamAssertConsistency = { m_Params, "/debugging/assert_consistency", "Debugging only: assert the consistency (0: none; 1: trail; 2: trail and WL's for assigned; 3: trail and WL's for all -- an extremely heavy operation)", 0, 0, 3};
		CTopiParam<uint32_t> m_ParamAssertConsistencyStartConf = { m_Params, "/debugging/assert_consistency_start_conf", "Debugging only; meaningful only if /debugging/assert_consistency=1: assert the consistency starting with the provided conflicts number", 0 };
		CTopiParam<uint32_t> m_ParamPrintDebugModelInvocation = { m_Params, "/debugging/print_debug_model_invocation", "The number of solver invocation to print the model in debug-model format: intended to be used for internal Topor debugging", 0 };
		CTopiParam<uint32_t> m_ParamVerifyDebugModelInvocation = { m_Params, "/debugging/verify_debug_model_invocation", "The number of solver invocation, when the debug-model is verified: intended to be used for internal Topor debugging", 0 };
		
		// Parameters: assumptions handling
		CTopiParam<bool> m_ParamAssumpsSimpAllowReorder = { m_Params, "/assumptions/allow_reorder", "Assumptions handling: allow reordering assumptions when filtering", true };
		CTopiParam<bool> m_ParamAssumpsIgnoreInGlue = { m_Params, "/assumptions/ignore_in_glue", "Assumptions handling: ignore assumptions when calculating the glue score (following the SAT'13 paper \"Improving Glucose for Incremental SAT Solving with Assumptions : Application to MUS Extraction\")", false };
		CTopiParam<uint8_t> m_ParamAssumpsConflictStrat = { m_Params, "/assumptions/conflict_strat", "Assumptions conflict handling strategy: 0 -- backtrack on conflict and post-process in UNSAT core extraction; 1 -- handle conflicts eagerly", {0, 0, 0, 0, 1, 0, 0, 0}, 0, 1 };

		// Parameters: conflict analysis
		CTopiParam<bool> m_ParamMinimizeClausesMinisat = { m_Params, "/conflicts/minimize_clauses", "Conflict analysis: apply deep conflict clause minimization", true };
		CTopiParam<uint32_t> m_ParamMinimizeClausesBinMaxSize = { m_Params, "/conflicts/bin_res_max_size", "Conflict analysis: maximal size to apply binary minimization (both this condition and maximal LBD must hold; 30 in Glucose, Fiver, Maple)", {30, 30, 30, 30, 50, 30, 30, 30} };
		CTopiParam<uint32_t> m_ParamMinimizeClausesBinMaxLbd = { m_Params, "/conflicts/bin_res_max_lbd", "Conflict analysis: maximal LBD to apply binary minimization (both this condition and maximal size must hold; 6 in Glucose, Fiver, Maple)", 6 };
		CTopiParam<uint32_t> m_ParamFlippedRecordingMaxLbdToRecord = { m_Params, "/conflicts/flipped_recording_max_lbd", "Conflict analysis: record a flipped clause with LBD smaller than or equal to the value of the parameter", {numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), 0, 40, numeric_limits<uint32_t>::max(), 0, 90} };
		CTopiParam<bool> m_ParamFlippedRecordDropIfSubsumed = { m_Params, "/conflicts/flipped_drop_if_subsumed", "Conflict analysis: test and drop the flipped clause, if subsumed by the main clause", true };
		CTopiParam<uint32_t> m_ParamOnTheFlySubsumptionContradictingMinGlueToDisable = { m_Params, "/conflicts/on_the_fly_subsumption/contradicting_min_glue_to_disable", "Conflict analysis: the minimal glue (LBD) to disable on-the-fly subsumption during conflict analysis over the contradicting clause (1: apply only over initial clauses)", {8, 0, 0, 0, 0, 8, 0, 8} };
		CTopiParam<uint32_t> m_ParamOnTheFlySubsumptionParentMinGlueToDisable = { m_Params, "/conflicts/on_the_fly_subsumption/parent_min_glue_to_disable", "Conflict analysis: the minimal glue (LBD) to disable on-the-fly subsumption during conflict analysis over the parents (1: apply only over initial clauses)", {30, 0, 0, 0, 0, 30, 0, 30} };
		CTopiParam<uint32_t> m_ParamOnTheFlySubsumptionContradictingFirstRestart = { m_Params, "/conflicts/on_the_fly_subsumption/contradicting_first_restart", "Conflict analysis: the first restart after which on-the-fly subsumption over the contradicting clause is enabled", 1 };
		CTopiParam<uint32_t> m_ParamOnTheFlySubsumptionParentFirstRestart = { m_Params, "/conflicts/on_the_fly_subsumption/parent_first_restart", "Conflict analysis: the first restart after which on-the-fly subsumption over the parent clause is enabled", 1 };
		CTopiParam<uint8_t> m_ParamAllUipMode = { m_Params, "/conflicts/all_uip/mode", "Conflict analysis: ALL-UIP scheme mode -- 0: disabled; 1: only main clause; 2: only flipped clause; 3: both main and flipped", 0, 0, 3 };
		CTopiParam<uint32_t> m_ParamAllUipFirstRestart = { m_Params, "/conflicts/all_uip/first_restart", "Conflict analysis: the first restart after which the ALL-UIP scheme is enabled", 0 };
		CTopiParam<uint32_t> m_ParamAllUipLastRestart = { m_Params, "/conflicts/all_uip/last_restart", "Conflict analysis: the last restart after which the ALL-UIP scheme is enabled", numeric_limits<uint32_t>::max() };
		CTopiParam<double> m_ParamAllUipFailureThr = { m_Params, "/conflicts/all_uip/success_rate_failure_thr", "Conflict analysis: the threshold on ALL-UIP scheme failure (0.8 in the SAT'20 paper)", 0.8 };
		
		// Parameters: restart strategy
		static constexpr uint8_t RESTART_STRAT_NUMERIC = 0;
		static constexpr uint8_t RESTART_STRAT_LBD = 1;
		static constexpr uint8_t RESTART_STRAT_NONE = 2;
		// Restart strategy: initial query
		CTopiParam<uint8_t> m_ParamRestartStrategyInit = { m_Params, "/restarts/strategy_init", "Restart strategy for the initial query: 0: numeric (arithmetic, luby or in/out); 1: LBD-average-based", {RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC}, 0, 1 };
		// Restart strategy: normal incremental query
		CTopiParam<uint8_t> m_ParamRestartStrategyN = { m_Params, "/restarts/strategy_n", "Restart strategy for the normal (non-short) incremental query: 0: arithmetic; 1: LBD-average-based", {RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC}, 0, 1 };
		// Restart strategy: short incremental query
		CTopiParam<uint8_t> m_ParamRestartStrategyS = { m_Params, "/restarts/strategy_s", "Restart strategy for the short incremental query: 0: arithmetic; 1: LBD-average-based", {RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC}, 0, 1 };
				
		CTopiParam<bool> m_ParamRestartNumericLocal = { m_Params, "/restarts/numeric/is_local", "Restarts, numeric strategies: use local restarts?", true };
		CTopiParam<uint32_t> m_ParamRestartNumericInitConfThr = { m_Params, "/restarts/numeric/conflict_thr", "Restarts, numeric strategy: the initial value for the conflict threshold, which triggers a restart", {1000, 1000, 1000, 1000, 100, 1000, 1000, 1000}, 1 };
		CTopiParam<uint8_t> m_ParamRestartNumericSubStrat = { m_Params, "/restarts/numeric/sub_strat", "Restarts, numeric sub-strategy: 0: arithmetic; 1: luby", 0, 0, 1 };
		CTopiParam<uint32_t> m_ParamRestartArithmeticConfIncr = { m_Params, "/restarts/arithmetic/conflict_increment", "Restarts, arithmetic numeric strategy: increment value for conflicts", {0, 50, 50, 50, 100, 0, 50, 1} };
		CTopiParam<double> m_ParamRestartLubyConfIncr = { m_Params, "/restarts/luby/conflict_increment", "Restarts, luby numeric strategy: increment value for conflicts", {1.5, 1.5, 1.5, 2, 1.5, 1.5, 2, 1.5}, 1 + numeric_limits<double>::epsilon() };

		CTopiParam<uint32_t> m_ParamRestartLbdWinSize = { m_Params, "/restarts/lbd/win_size", "Restart, LBD-average-based strategy: window size", {50, 50, 50, 50, 25, 50, 50, 50}, 1 };
		CTopiParam<uint32_t> m_ParamRestartLbdThresholdGlueVal = { m_Params, "/restarts/lbd/thr_glue_val", "Restart, LBD-average-based strategy: the maximal value for LBD queue update", {numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), 50, 50, numeric_limits<uint32_t>::max(), 50, numeric_limits<uint32_t>::max()} };
		CTopiParam<double> m_ParamRestartLbdAvrgMult = { m_Params, "/restarts/lbd/average_mult", "Restarts, LBD-average-based strategy: the multiplier in the formula for determining whether it's time to restart: recent-LBD-average * multiplier > global-LBD-average", {0.8, 0.8, 0.8, 0.8, 0.9, 0.8, 0.8, 0.8} };

		CTopiParam<bool> m_ParamRestartLbdBlockingEnable = { m_Params, "/restarts/lbd/blocking/enable", "Restarts, LBD-average-based blocking enable", {true, true, false, false, true, true, false, true} };
		CTopiParam<uint32_t> m_ParamRestartLbdBlockingConfsToConsider = { m_Params, "/restarts/lbd/blocking/conflicts_to_consider_blocking", "Restarts, LBD-average-based blocking strategy: the number of conflicts to consider blocking; relevant, if /restarts/lbd/blocking/enable=1", {10000, 10000, 10000, numeric_limits<uint32_t>::max(), 10000, 10000, numeric_limits<uint32_t>::max(), 10000} };
		CTopiParam<double> m_ParamRestartLbdBlockingAvrgMult = { m_Params, "/restarts/lbd/blocking/average_mult", "Restarts, LBD-average-based blocking strategy: the multiplier in the formula for determining whether the restart is blocked: #assignments > multiplier * assignments-average", 1.4 };
		CTopiParam<uint32_t> m_ParamRestartLbdBlockingWinSize = { m_Params, "/restarts/lbd/blocking/win_size", "Restarts, LBD-average-based blocking strategy: window size", 5000, 1 };

		// Parameters: buffer multipliers
		inline bool IsMultiplierParam(const string& paramName) const { return paramName == "/multiplier/clauses" || paramName == "/multiplier/variables" || paramName == "/multiplier/watches_if_separate"; }
		CTopiParam<double> m_ParamMultClss = { m_Params, "/multiplier/clauses", "The multiplier for reallocating the clause buffer", CDynArray<TUV>::MultiplierDef, 1. };
		CTopiParam<double> m_ParamMultVars = { m_Params, "/multiplier/variables", "The multiplier for reallocating data structures, indexed by variables (and literals)", 1., 1. };
		CTopiParam<double> m_ParamMultWatches = { m_Params, "/multiplier/watches", "The multiplier for reallocating the watches buffer", 1.33, 1. };

		// Parameters: clause simplification
		CTopiParam<bool> m_ParamSimplify = { m_Params, "/deletion/simplify", "Simplify?", true };
		CTopiParam<uint8_t> m_ParamSimplifyGlobalLevelScoreStrat = { m_Params, "/deletion/simplify_global_level_strat", "Simplify: how to update the score of the only remaining globally assigned variable (relevant only when /backtracking/custom_bt_strat != 0) -- 0: don't touch; 1: minimal out of all globals; 2: maximal out of all globals", 0, 0, 2 };

		// Parameters: data-based compression
		CTopiParam<double> m_ParamWastedFractionThrToDelete = { m_Params, "/deletion/wasted_fraction_thr", "If wasted > size * this_parameter, we physically compress the memory (0.2 in Fiver, Maple)", 0.2, numeric_limits<double>::epsilon(), 1. };
		CTopiParam<bool> m_ParamCompressAllocatedPerWatch = { m_Params, "/deletion/compress_allocated_per_watch", "Tightly compress the allocated memory per every watch during memory compression (or, otherwise, never compress per watch)", true };
		CTopiParam<bool> m_ParamReduceBuffersSizeAfterCompression = { m_Params, "/deletion/reduce_buffers_size_after_compression", "Reduce the physical size of the clause and watch buffers after compression, if required (to the current size * Multiplier)", true };
		
		// Parameters: clause deletion
		CTopiParam<bool> m_ParamClsDelStrategy = { m_Params, "/deletion/clause/strategy", "Clause deletion strategy (0: no clause deletion; 1: clause deletion)", true };
		CTopiParam<bool> m_ParamClsDelDeleteOnlyAssumpDecLevel = { m_Params, "/deletion/clause/delete_only_assump_dec_level", "Clause deletion: apply clause deletion only when at assumption decision level", false };
		CTopiParam<double> m_ParamClsLowDelActivityDecay = { m_Params, "/deletion/clause/activity_decay", "Clause deletion: activity decay factor", 0.999, numeric_limits<double>::epsilon(), 1. };

		CTopiParam<float> m_ParamClsDelLowFracToDelete = { m_Params, "/deletion/clause/frac_to_delete", "Clause deletion: the fraction of clauses to delete", {0.7f, 0.8f, 0.8f, 0.8f, 0.8f, 0.7f, 0.8f, 0.7f}, numeric_limits<float>::epsilon(), (float)1.f };
		CTopiParam<uint32_t> m_ParamClsDelLowTriggerInit = { m_Params, "/deletion/clause/trigger_init", "Clause deletion: the initial number of learnts to trigger clause deletion", 6000 };
		CTopiParam<uint32_t> m_ParamClsDelLowTriggerInc = { m_Params, "/deletion/clause/trigger_linc", "Clause deletion: the increment in learnts to trigger clause deletion", 75 };
		CTopiParam<double> m_ParamClsDelLowTriggerMult = { m_Params, "/deletion/clause/trigger_inc_mult", "Clause deletion: the change in increment in learnts to trigger clause deletion (that is, the value by which /deletion/clause/trigger_linc is multiplied)", 1.2, 1. };
		CTopiParam<uint32_t> m_ParamClsDelLowTriggerMax = { m_Params, "/deletion/clause/trigger_max", "Clause deletion: the maximal number of learnts to activate clause deletion", 60000 };		
		CTopiParam<TUV> m_ParamClsDelLowMinGlueFreeze = { m_Params, "/deletion/clause/glue_min_freeze", "Initial query: clause deletion: protect clauses for one round if their glue decreased and is lower than this value", {4, 30, 30, 30, 30, 4, 30, 30}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelLowMinGlueFreezeAi = { m_Params, "/deletion/clause/glue_min_freeze_ai", "Normal incremental query: clause deletion: protect clauses for one round if their glue decreased and is lower than this value", 30, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelLowMinGlueFreezeS = { m_Params, "/deletion/clause/glue_min_freeze_s", "Short incremental query: clause deletion: protect clauses for one round if their glue decreased and is lower than this value", {5, 30, 30, 30, 30, 5, 30, 30}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelGlueNeverDelete = { m_Params, "/deletion/clause/glue_never_delete", "Clause deletion: the highest glue value blocking clause deletion", 2, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelGlueClusters = { m_Params, "/deletion/clause/glue_clusters", "Clause deletion: the number of glue clusters", {11, 0, 0, 0, 0, 11, 0, 8}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelGlueMaxCluster = { m_Params, "/deletion/clause/glue_max_lex_cluster", "Clause deletion: the highest glue which makes the clause belong to a glue-cluster", {16, 0, 0, 0, 0, 16, 0, 16}, 0, (TUV)numeric_limits<uint8_t>::max() };

		// Parameters: phase saving
		CTopiParam<bool> m_ParamPhaseMngForceSolution = { m_Params, "/phase/force_solution", "Phase management: always force (the polarities of) the latest solution?", {false, false, false, false, false, false, true, false} };
		CTopiParam<uint16_t> m_ParamPhaseMngRestartsBlockSize = { m_Params, "/phase/restarts_block_size", "Phase management: the number of restarts in a block for phase management considerations", 10, 1 };
		CTopiParam<double> m_ParamPhaseMngUnforceRestartsFractionInit = { m_Params, "/phase/unforce_restarts_fraction_init", "Phase management for the initial query: don't force the polarities for the provided fraction of restarts", 0.25, 0., 1. };
		CTopiParam<double> m_ParamPhaseMngUnforceRestartsFractionN = { m_Params, "/phase/unforce_restarts_fraction_n", "Phase management for the normal (non-short) incremental query: don't force the polarities for the provided fraction of restarts", 0., 0., 1. };
		CTopiParam<double> m_ParamPhaseMngUnforceRestartsFractionS = { m_Params, "/phase/unforce_restarts_fraction_s", "Phase management for the short incremental query: don't force the polarities for the provided fraction of restarts", 0., 0., 1. };
		CTopiParam<uint8_t> m_ParamPhaseMngStartInvStrat = { m_Params, "/phase/start_inv_strat", "Phase management: startegy to start an invocation with; relevant when 0 < /phase/unforce_restarts_fraction < 1: 0: start with force; 1: start with unforce; 2: start with rand", 0, 0, 2 };
		CTopiParam<bool> m_ParamPhaseBoostFlippedForced = { m_Params, "/phase/boost_flipped_forced", "Phase management: boost the scores of forced variables, flipped by BCP?", false };

		/*
		* INITIALIZATION-RELATED
		*/

		// The number of initially allocated entries in variable-indexed arrays
		const size_t m_InitVarNumAlloc;
		// The number of initially allocated entries in literal-indexed arrays
		inline size_t GetInitLitNumAlloc() const;
		// Handle a potentially new user variable
		void HandleIncomingUserVar(TLit v, bool isUndoable = false);		
		
		/*
		* STATUS
		*/
		
		bool m_IsSolveOngoing = false;
		TToporStatus m_Status = TToporStatus::STATUS_UNDECIDED;
		string m_StatusExplanation;
		inline bool IsUnrecoverable() const { return m_Status >= TToporStatus::STATUS_FIRST_UNRECOVERABLE; }
		inline bool IsErroneous() const { return m_Status >= TToporStatus::STATUS_FIRST_PERMANENT_ERROR; }
		inline void SetStatus(TToporStatus s, string&& expl = "") { m_Status = s; m_StatusExplanation = move(expl); }
		inline TToporReturnVal UnrecStatusToRetVal() const;
		inline TToporReturnVal StatusToRetVal() const;		
		inline void SetOverallTimeoutIfAny();

		/*
		* INITIALIZATION AND EXTERNAL->INTERNAL VARIABLE MAP
		*/
		
		// external variable-->internal literal map
		CDynArray<TULit> m_E2ILitMap;
		CVector<TLit> m_NewExternalVarsAddUserCls;
		// internal variable-->external literal map (initialized only, if required, e.g., for callbacks)
		CDynArray<TLit> m_I2ELitMap;
		bool UseI2ELitMap() const { return m_ParamVerifyDebugModelInvocation != 0 || IsCbLearntOrDrat(); }
		
		static constexpr TLit ExternalLit2ExternalVar(TLit l) { return l > 0 ? l : -l; }
		inline TULit E2I(TLit l) const
		{
			const TLit externalV = ExternalLit2ExternalVar(l);
			const TULit internalL = m_E2ILitMap[externalV];
			return l < 0 ? Negate(internalL) : internalL;
		}
	
		// Used to make sure tautologies are discovered and duplicates are removed in new clauses
		CHandleNewCls m_HandleNewUserCls;

		// The last internal variable so-far
		TUVar m_LastExistingVar = 0;
		inline TUVar GetNextVar() const { return m_LastExistingVar + 1; }
		inline TULit GetLastExistingLit() const { return GetLit((TULit)m_LastExistingVar, true); }		
		inline TULit GetNextLit() const { return GetLastExistingLit() + 1; }
		inline TULit GetAssignedLitForVar(TUVar v) const { assert(IsAssignedVar(v)); return GetLit(v, m_AssignmentInfo[v].m_IsNegated); }
		
		/*
		* CLAUSE-BUFFER
		*/	

		// Fixed layout for clause with n literals
		
		// Not over-sized clause header:
		// [is-learnt:1][glue:11][size:20] (for 32-bit-literals, otherwise see below)
		// [1:stay-for-another-round][activity:31] (must not be -1 for not over-sized; 32 for <=32-bit-literals, n for n>32-bit-literals)
		
		// Over-sized clause header:
		// [is-learnt:1][size:31] (for 32 bits, otherwise see below)
		// 11111111111111111111111111111111:32 (32 for <=32-bit-literals, n for n>32-bit-literals)
		// [1:stay-for-another-round][activity:31] (32 for <=32-bit-literals, n for n>32-bit-literals)
		
		// LITERALS:
		// literal-1
		// literal-2
		// .........
		// literal-n		
		
		// Fixed mode buffer
		CDynArray<TULit> m_B = InitEntriesInB;
		TUInd m_BNext = LitsInPage;
		TUInd m_BWasted = 0;

		constexpr static TUV ClsIsLearntBits = 1;
		constexpr static TUV ClsLShiftToIsLearntOn = numeric_limits<TUV>::digits - 1;
		constexpr static TUV ClsIsLearntMask = (TUV)1 << ClsLShiftToIsLearntOn;

		constexpr static TUV ClsGlueBits = numeric_limits<TUV>::digits == 8 ? 3 : numeric_limits<TUV>::digits == 16 ? 5 : numeric_limits<TUV>::digits == 32 ? 11 : 20;
		constexpr static TUV ClsMaxGlue = ((TUV)1 << ClsGlueBits) - (TUV)1;
		constexpr static TUV ClsGlueMask = ClsMaxGlue << (numeric_limits<TUV>::digits - ClsIsLearntBits - ClsGlueBits);

		constexpr static TUV ClsIsLearntAndGlueMask = ClsIsLearntMask | ClsGlueMask;

		constexpr static TUV ClsSizeBits = numeric_limits<TUV>::digits - ClsIsLearntBits - ClsGlueBits;
		constexpr static TUV ClsLearntMaxSizeWithGlue = ((TUV)1 << ClsSizeBits) - (TUV)1;
		constexpr static TUV ClsLearntMaxSizeWithoutGlue = ((TUV)1 << (ClsSizeBits + ClsGlueBits)) - (TUV)1;

		constexpr static int32_t ClsOversizeActivity = -1;

		constexpr static TUV ClsActivityFields = numeric_limits<TUV>::digits >= 32 ? 1 : 32 / numeric_limits<TUV>::digits;
		constexpr static TUV ClsActivityFieldsLshift = countr_zero(ClsActivityFields);

		constexpr static uint32_t ClsLShiftSkipDel = numeric_limits<uint32_t>::digits - 1;
		constexpr static uint32_t ClsSkipdelMask = (uint32_t)1 << ClsLShiftSkipDel;
		constexpr static uint32_t ClsNotSkipdelMask = ~ClsSkipdelMask;

		inline void ClsSetIsLearnt(TUInd clsInd, bool isLearnt)
		{
			m_B[clsInd] = (m_B[clsInd] & ~ClsIsLearntMask) | (isLearnt << ClsLShiftToIsLearntOn);
			assert(ClsGetIsLearnt(clsInd) == isLearnt);
		}

		inline bool ClsGetIsLearnt(TUInd clsInd) const
		{
			return m_B[clsInd] & ClsIsLearntMask;
		}

		const int32_t* ClsGetStandardActivityAsConstIntPtr(TUInd clsInd) const
		{
			return (const int32_t*)(m_B.get_const_ptr() + clsInd + 1);
		}

		int32_t* ClsGetStandardActivityAsIntPtr(TUInd clsInd)
		{
			return (int32_t*)(m_B.get_ptr() + clsInd + 1);
		}

		bool m_AnyOversized = false;
		inline bool ClsGetIsOversized(TUInd clsInd) const
		{
			return m_AnyOversized ? ClsGetIsLearnt(clsInd) && *ClsGetStandardActivityAsConstIntPtr(clsInd) == ClsOversizeActivity : false;
		}
		inline bool ClsGetIsOversizedAssumeLearnt(TUInd clsInd) const
		{
			assert(ClsGetIsLearnt(clsInd));
			return m_AnyOversized ? *ClsGetStandardActivityAsConstIntPtr(clsInd) == ClsOversizeActivity : false;
		}

		inline void ClsSetSize(TUInd clsInd, TUV sz) 
		{ 
			assert((sz & ClsIsLearntMask) == 0);
			const auto cleaned = m_B[clsInd] & ~(!ClsGetIsLearnt(clsInd) || sz > ClsLearntMaxSizeWithGlue ? ClsLearntMaxSizeWithoutGlue : ClsLearntMaxSizeWithGlue);
			m_B[clsInd] = cleaned | sz;
		
			if (unlikely(ClsGetIsLearnt(clsInd) && sz > ClsLearntMaxSizeWithGlue))
			{
				m_AnyOversized = true;
				// Glue cannot be held with this size
				*ClsGetStandardActivityAsIntPtr(clsInd) = ClsOversizeActivity;
			}	
			assert(ClsGetSize(clsInd) == sz);
		}
		
		inline TUV ClsGetSize(TUInd clsInd) const 
		{	
			if (!ClsGetIsLearnt(clsInd) || ClsGetIsOversizedAssumeLearnt(clsInd))
			{
				return m_B[clsInd] & (~ClsIsLearntMask);
			}
			else
			{
				return m_B[clsInd] & (~ClsIsLearntAndGlueMask);
			}
		}		
		
		inline void ClsSetGlue(TUInd clsInd, TUV glue) 
		{ 
			assert(ClsGetIsLearnt(clsInd)); 
			if (likely(!ClsGetIsOversizedAssumeLearnt(clsInd)))
			{
				if (unlikely(glue > ClsMaxGlue))
				{
					glue = ClsMaxGlue;
				}
				m_B[clsInd] = (m_B[clsInd] & ~ClsGlueMask) | (glue << ClsSizeBits);
				assert(ClsGetGlue(clsInd) == glue);
			}	
			else
			{
				assert(ClsGetGlue(clsInd) == ClsGetSize(clsInd));
			}
		}
		
		inline TUV ClsGetGlue(TUInd clsInd) const 
		{
			assert(ClsGetIsLearnt(clsInd));
			if (likely(!ClsGetIsOversizedAssumeLearnt(clsInd)))
			{
				return (m_B[clsInd] & ClsGlueMask) >> ClsSizeBits;
			}
			else
			{
				return ClsGetSize(clsInd);
			}
		}

		inline TUInd ClsGetActivityAndSkipdelIndex(TUInd clsInd) const
		{
			return clsInd + 1 + ClsGetIsOversizedAssumeLearnt(clsInd);
		}

		inline void ClsSetActivityAndSkipdelTo0(TUInd clsInd)
		{
			assert(ClsGetIsLearnt(clsInd));
			m_B[ClsGetActivityAndSkipdelIndex(clsInd)] = 0;
		}

		inline bool ClsGetSkipdel(TUInd clsInd)
		{
			return *(uint32_t*)(m_B.get_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)) & ClsSkipdelMask;
		}

		inline uint32_t ClsGetSkipdelNoShift(TUInd clsInd) const
		{
			return *(uint32_t*)(m_B.get_const_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)) & ClsSkipdelMask;
		}
		
		inline void ClsSetActivity(TUInd clsInd, float a)
		{			
			const auto prevSkipDel = ClsGetSkipdel(clsInd);
			*(float*)(m_B.get_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)) = prevSkipDel ? -a : a;
			assert(prevSkipDel == ClsGetSkipdel(clsInd));
			[[maybe_unused]] const auto currAct = ClsGetActivity(clsInd);
			assert(currAct == a);
		}	

		inline float ClsGetActivity(TUInd clsInd) const
		{
			static_assert(sizeof(float) == sizeof(uint32_t));
			return fabs(*(float*)(m_B.get_const_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)));
		}

		inline void ClsSetSkipdel(TUInd clsInd, bool skipDel)
		{
			[[maybe_unused]] const auto prevAct = ClsGetActivity(clsInd);
			*(uint32_t*)(m_B.get_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)) = (*(uint32_t*)(m_B.get_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)) &
				ClsNotSkipdelMask) | ((uint32_t)skipDel << ClsLShiftSkipDel);
			assert(prevAct == ClsGetActivity(clsInd));
			assert(ClsGetSkipdel(clsInd) == skipDel);
		}

		inline bool ClsSetSkipdel(TUInd clsInd) const
		{
			return *(uint32_t*)(m_B.get_const_ptr() + ClsGetActivityAndSkipdelIndex(clsInd)) & ClsSkipdelMask;
		}

		inline TUV ClsLitsStartOffset(bool isLearnt, bool isOversized) const
		{ 
			return (TUV)1 + ((TUV)isLearnt << ClsActivityFieldsLshift) + ((TUV)isOversized << ClsActivityFieldsLshift);
		}

		inline span<TULit> Cls(TUInd clsInd) { return m_B.get_span_cap(clsInd + ClsLitsStartOffset(ClsGetIsLearnt(clsInd), ClsGetIsOversized(clsInd)), ClsGetSize(clsInd)); }
		inline span<TULit> ClsConst(TUInd clsInd) const { return m_B.get_const_span_cap(clsInd + ClsLitsStartOffset(ClsGetIsLearnt(clsInd), ClsGetIsOversized(clsInd)), ClsGetSize(clsInd)); }
		
		// To delete an initial clause [n l1 ... ln] we just put 0 instead of l2
		// To delete a learnt clause [{1<<ClsLShiftToMSB | n} Glue l1 ... ln] we replace the first entry by n+1 and put 0 instead of l2
		// This way, any deleted chunks > 1 is [n l1 0 ... ln], so one can walk clauses and deleted chunks in a similar manner, 
		// e.g., ClsEnd(clsInd) works both when clsInd is a clause index and when it's a deleted chunk index
		// Any entry of size < 4 is considered to be deleted, including [0]
		// We also, optionally place newBinCls[0] at clsStart + 1 and newBinCls[1] at clsStart + 3. 
		//		This trick is used to replace long parents with binary parents for assumption decision levels
		void DeleteCls(TUInd clsInd, array<TULit, 2>* newBinCls = nullptr);
		// Delete a binary clause
		void DeleteBinaryCls(const span<TULit> binCls);
		void DeleteLitFromCls(TUInd clsInd, TULit l);
		void RecordDeletedLitsFromCls(TUV litsNum);
		// Returns clause index for long clauses
		TUInd AddClsToBufferAndWatch(span<TULit> cls, bool isLearnt);
		size_t SizeWithoutDecLevel0(const span<TULit> cls) const;
		
		/*
		* WATCHES
		*/		

		// The struct represents the Watch information per one literal
		struct TWatchInfo
		{
			TUInd m_WBInd;
			TUInd m_AllocatedEntries;
			TUInd m_LongWatches;
			TUInd m_BinaryWatches;
			
			inline void PointToNewArena(TUInd bInd, TUInd allocatedEntries) { m_WBInd = bInd; m_AllocatedEntries = allocatedEntries; }

			inline TUInd GetLongEntries() const { return m_LongWatches * BinsInLong; }
			inline TUInd GetUsedEntries() const { return GetLongEntries() + m_BinaryWatches; }
			inline size_t GetLongEntry(size_t longWatchInd) const { return longWatchInd * BinsInLong; }
			inline size_t GetBinEntry(size_t binWatchInd) const { return GetLongEntries() + binWatchInd; }

			inline bool IsEmpty() const { assert(m_WBInd != 0 || (m_AllocatedEntries == 0 && m_BinaryWatches == 0 && m_LongWatches == 0)); return m_AllocatedEntries == 0;	}

			static constexpr TUInd BinsInLong = 1 + (TUInd)LitsInInd;
			static constexpr TUInd BinsInLongBitCeil = bit_ceil(BinsInLong);
			static constexpr size_t BinsInLongBytes = sizeof(TULit) * TWatchInfo::BinsInLong;
			static constexpr TUInd MaxWatchInfoAlloc = rotr((TUInd)1, 1);
		};

		// Literal-indexed watch information
		CDynArray<TWatchInfo> m_Watches;

		//************************************************

		// Fixed layout watches structure in the fused buffer 
		// The sizes are in literal's TWatchInfo; the buffer only contains row data: 
		// long watches (literal, clause-buffer-index), followed by binary watches (1 literal each)
		// Long watched come first, since, otherwise, there could have been a hole, when adding a binary watch
		
		// 1: long-watch {1 + LitsInInd entries} [literal, clause-buffer-index]
		// ................
		// m_Watches[l].m_LongWatches: long-watch {1 + LitsInInd entries} [literal, clause-buffer-index]
		// 1: binary-watch {1 entry} [literal] 
		// ................
		// m_Watches[l].m_BinaryWatches: binary-watch {1 entry} [literal]
		
		// Add a binary watch
		void WLAddBinaryWatch(TULit l, TULit otherWatch);
		// Remove a binary watch
		void WLRemoveBinaryWatch(TULit l, TULit otherWatch);
		// Binary watch exists?
		bool WLBinaryWatchExists(TULit l, TULit otherWatch);		
		// Returns an index to the place in m_B, where clause index in inserted
		// This is to handle cases when clsInd is but a placeholder; we also allow not to specify clsInd to handle this case
		TUInd WLAddLongWatch(TULit l, TULit inlinedLit, TUInd clsInd = BadClsInd);
		// Remove a long watch
		void WLRemoveLongWatch(TULit l, size_t longWatchInd);
		// Find the index of the given clause in l's watch
		size_t WLGetLongWatchInd(TULit l, TUInd clsInd);
		// Set the cached literal of l's watch to clsInd to 
		void WLSetCached(TULit l, TUInd clsInd, TULit cachedLit);
		// Prepare the arena for a watch list; take into account a potential binary/long watch (or both, but this isn't expected to happen)
		TULit* WLPrepareArena(TULit l, bool allowNewBinaryWatch, bool allowNewLongWatch);		

		void MarkWatchBufferChunkDeleted(TWatchInfo& wi);
		void MarkWatchBufferChunkDeletedOrByLiteral(TUInd wlbInd, TUInd allocatedEntries, TULit l = BadULit);

		// Assert the consistency of WL: a heavy test, use for debugging only; returns true iff consistent
		// if testMissedImplications=true, then all the clauses must either be satisfied or have two unassigned literals
		bool WLAssertConsistency(bool testMissedImplications);		
		// A lighter version of WLAssertConsistency
		bool WLAssertNoMissedImplications();

		// Separate buffer for the watches
		// The beginning of moved regions are marked as deleted in the following format:
		// [long_2(allocated-entries) 0]
		CDynArray<TULit> m_W = InitEntriesInB;
		TUInd m_WNext = LitsInPage;
		
		// This function determines which literal is better in terms of making it to the WL's
		// We ensure the following order between the literals for WL construction (< means better):
		// (1) Satisfied literal < unassigned literal < falsfied literal
		// (2) Ties between satisfied literal are broken by:
		//		(a) Preferring a lower decision level; in case of a tie
		//		(b) Preferring a shorter watch lists 
		// (3) Ties between unassigned literals are broken by preferring shorter watch lists 
		// (4) Ties between falsified literal are broken by:
		//		(a) Preferring a higher decision level; in case of a tie
		//		(b) Preferring a shorter watch lists 	
		// Shorter WL's should be good for:
		//			(a) Increasing the odds that we won't need to copy over the watches during the instance creation process
		//			(b) Being cache-friendly
		bool WLIsLitBetter(TULit lCand, TULit lOther) const;

		/*
		* DECISION LEVELS, ASSIGNMENTS, TRAIL
		*/
		
		// The current decision level
		TUV m_DecLevel = 0;
		// The last variable on the trail per decision level
		CDynArray<TUVar> m_TrailLastVarPerDecLevel;
		// Get the decision variable of the given decision level
		inline TUVar GetDecVar(TUV decLevel) const
		{ 
			assert(decLevel > 0 && decLevel <= m_DecLevel); 
			// Skip any collapsed decision levels
			auto prevDlLastVar = m_TrailLastVarPerDecLevel[decLevel - 1];
			while (decLevel > 0 && (prevDlLastVar = m_TrailLastVarPerDecLevel[decLevel - 1]) == BadUVar)
			{
				--decLevel;				
			}
			return prevDlLastVar == BadUVar ? m_TrailStart : m_VarInfo[prevDlLastVar].m_TrailNext;
		}

		inline bool DecLevelIsCollapsed(TUV decLevel) const
		{
			if (decLevel == 0)
			{
				return false;
			}
			assert(decLevel <= m_DecLevel);

			return m_TrailLastVarPerDecLevel[decLevel] == BadUVar;
		}
		
		CDynArray<double> m_BestScorePerDecLevel;
		TUV GetDecLevelWithBestScore(TUV dlLowestIncl, TUV dlHighestExcl);
		double CalcMaxDecLevelScore(TUV dl) const;
		double CalcMinDecLevelScore(TUV dl) const;
		// The pointer to trail start
		TUVar m_TrailStart = BadUVar;
		// The pointer to trail end
		TUVar m_TrailEnd = BadUVar;
		// Literals to propagate
		CVector<TULit> m_ToPropagate;
		inline void ToPropagatePushBack(TULit l) { m_ToPropagate.push_back(l);  }
		inline TULit ToPropagateBackAndPop() { return m_ToPropagate.pop_back(); }
		inline void ToPropagateClear() { m_ToPropagate.clear(); }
		
		// Contradiction information (meant to be returned by BCP for conflict analysis) 
		struct TContradictionInfo
		{
			TContradictionInfo() : m_ParentClsInd(BadClsInd), m_IsContradiction(false), m_IsContradictionInBinaryCls(false) {}
			TContradictionInfo(TUInd parentClsInd) : m_ParentClsInd(parentClsInd), m_IsContradiction(true), m_IsContradictionInBinaryCls(false) { assert(parentClsInd != BadClsInd);  }
			TContradictionInfo(const array<TULit, 2>& binClause) : m_BinClause(binClause), m_IsContradiction(true), m_IsContradictionInBinaryCls(true) { assert(binClause[0] != BadULit && binClause[1] != BadULit); }
			inline bool IsContradiction() const { return m_IsContradiction; }
			union
			{
				// Parent clause index (for a contradiction in a long clause)
				TUInd m_ParentClsInd;
				// The binary parent (for a contradiction in a binary clause)
				array<TULit, 2> m_BinClause;
			};
			uint8_t m_IsContradiction : 1;
			uint8_t m_IsContradictionInBinaryCls : 1;		
		};

		inline span<TULit> CiGetSpan(TContradictionInfo& ci) { return ci.m_IsContradictionInBinaryCls ? span(ci.m_BinClause) : Cls(ci.m_ParentClsInd); }
		inline string CiString(TContradictionInfo& ci) { return !ci.m_IsContradiction ? "" : SLits(ci.m_IsContradictionInBinaryCls ? span(ci.m_BinClause) : Cls(ci.m_ParentClsInd)); }
		inline string CisString(span<TContradictionInfo> cis) { string res; for (auto& ci : cis) res = res + CiString(ci) + "; "; return res; }
		[[maybe_unused]] bool CiIsLegal(TContradictionInfo& ci, bool assertTwoLitsSameDecLevel = true);		
			
		struct TAssignmentInfo
		{
			inline void Assign(bool isNegated, TUInd parentClsInd, TULit otherWatch)
			{
				m_IsAssigned = true;
				m_IsAssignedInBinary = parentClsInd == BadClsInd && otherWatch != BadULit;
				m_IsNegated = isNegated;					
			}
			inline void Unassign() { m_IsAssigned = false; }
			inline bool IsAssignedBinary() const { return m_IsAssignedInBinary; }

			// Assigned?
			uint8_t m_IsAssigned : 1;
			// Assigned with a binary clause parent?
			uint8_t m_IsAssignedInBinary : 1;
			// Assigned negated?
			uint8_t m_IsNegated : 1;
			// Visited for various purposes
			uint8_t m_Visit : 1;
			// Rooted for various purposes (there is no real difference between visited and rooted; we simply sometimes need several lists; the name is different (rather than, e.g., m_Visit2) for readability and error-proneness)
			uint8_t m_Root : 1;
			// Assumption variable?
			uint8_t m_IsAssump : 1;
			// relevant only if m_IsAssump=true: is the negation of this variable is the assumption?
			uint8_t m_IsAssumpNegated : 1;
			// Was the last parent (at unassignment time) a binary clause; maintained in re-use trail mode only?
			// Also used as a flag for removing the pivot by on-the-fly subsumption when the variable is assigned
			uint8_t m_IsLastParentBin : 1;
		};
		static_assert(sizeof(TAssignmentInfo) == 1);

		struct TVarInfo
		{
			inline void Assign(TUInd parentClsInd, TULit otherWatch, TUV decLevel, TUVar trailPrev, TUVar trailNext)
			{
				parentClsInd == BadClsInd && otherWatch != BadULit ? m_BinOtherLit = otherWatch : m_ParentClsInd = parentClsInd;
				m_DecLevel = decLevel;
				m_TrailPrev = trailPrev;
				m_TrailNext = trailNext;				
			}
			
			inline bool IsDecVar() const { return m_DecLevel != 0 && m_ParentClsInd == BadUVar; }						
			
			union 
			{
				// Parent clause for longs
				TUInd m_ParentClsInd;
				// The other literal in the clause for binaries
				TULit m_BinOtherLit;
			};
			
			union
			{
				// Decision level
				TUV m_DecLevel;				
			};

			// The previous variable on the trail
			TUVar m_TrailPrev;
			// The next variable on the trail
			TUVar m_TrailNext;
		};

		struct TPolarityInfo
		{
			TPolarityInfo(bool isPolarityFixed, bool isNextPolarityNegated) :
				m_IsNextPolarityDetermined(true), m_IsNextPolarityNegated(isNextPolarityNegated), m_IsPolarityFixed(isPolarityFixed) {}
			uint8_t m_IsNextPolarityDetermined : 1;
			uint8_t m_IsNextPolarityNegated : 1;
			uint8_t m_IsPolarityFixed : 1;
			inline bool IsNextPolarityDetermined() const { return m_IsNextPolarityDetermined; }
			inline bool GetNextPolarityIsNegated() 
			{
				assert(m_IsNextPolarityDetermined);
				if (!m_IsPolarityFixed)
				{
					m_IsNextPolarityDetermined = false;
				}
				return m_IsNextPolarityNegated;
			}
			inline void Clear() { m_IsNextPolarityDetermined = m_IsNextPolarityNegated = m_IsPolarityFixed = 0; }
		};
		static_assert(sizeof(TPolarityInfo) == 1);
		bool m_PolarityInfoActivated = false;

		inline TUVar GetTrailPrevVar(TUVar v) const { return m_VarInfo[v].m_TrailPrev; }
		inline TUVar GetTrailNextVar(TUVar v) const { return m_VarInfo[v].m_TrailNext; }

		TUV m_AssignedVarsNum = 0;
		CDynArray<TAssignmentInfo> m_AssignmentInfo;
		size_t m_PrevAiCap = 0;
		CDynArray<TVarInfo> m_VarInfo;
		CDynArray<TPolarityInfo> m_PolarityInfo;

		inline bool IsAssignedVar(TUVar v) const
		{
			return m_AssignmentInfo[v].m_IsAssigned;
		}

		inline bool IsAssigned(TULit l) const
		{
			return IsAssignedVar(GetVar(l));
		}

		inline bool IsAssignedNegated(TULit l) const
		{
			return m_AssignmentInfo[GetVar(l)].m_IsNegated ^ IsNeg(l);
		}

		inline bool IsFalsified(TULit l) const
		{
			const TUVar v = GetVar(l);
			return m_AssignmentInfo[v].m_IsAssigned && m_AssignmentInfo[v].m_IsNegated ^ IsNeg(l);
		}

		inline bool IsGloballyFalsified(TULit l) const
		{
			return IsFalsified(l) && GetAssignedDecLevel(l) == 0;
		}

		inline bool IsGloballySatisfied(TULit l) const
		{
			return IsSatisfied(l) && GetAssignedDecLevel(l) == 0;
		}

		inline bool IsGloballyAssignedVar(TUVar v) const
		{
			return IsAssignedVar(v) && GetAssignedDecLevelVar(v) == 0;
		}

		inline bool IsSatisfied(TULit l) const
		{
			const TUVar v = GetVar(l);
			return m_AssignmentInfo[v].m_IsAssigned && m_AssignmentInfo[v].m_IsNegated == IsNeg(l);
		}
		
		inline bool UnassignedOrSatisfied(TULit l) const
		{
			const TUVar v = GetVar(l);
			return !m_AssignmentInfo[v].m_IsAssigned || m_AssignmentInfo[v].m_IsNegated == IsNeg(l);
		}

		inline TUV GetAssignedDecLevel(TULit l) const
		{
			assert(IsAssigned(l));
			return m_VarInfo[GetVar(l)].m_DecLevel;
		}
		inline TUV GetAssignedDecLevelVar(TUVar v) const
		{
			assert(IsAssignedVar(v));
			return m_VarInfo[v].m_DecLevel;
		}
		inline TUInd GetAssignedParentClsInd(TULit l) const
		{
			assert(IsAssigned(l));
			return m_VarInfo[GetVar(l)].m_ParentClsInd;
		}

		inline bool IsAssignedDec(TULit l) const
		{
			return IsAssignedDecVar(GetVar(l));
		}
		inline bool IsAssignedDecVar(TUVar v) const
		{
			assert(IsAssignedVar(v));
			return m_VarInfo[v].IsDecVar();
		}
		inline bool IsAssignedAndDecVar(TUVar v) const
		{
			return IsAssignedVar(v) && m_VarInfo[v].IsDecVar();
		}
		inline std::span<TULit> GetAssignedNonDecParentSpan(TULit l)
		{
			return GetAssignedNonDecParentSpanVar(GetVar(l));
		}
		inline std::span<TULit> GetAssignedNonDecParentSpanVar(TUVar v)
		{
			assert(IsAssignedVar(v) && !IsAssignedDecVar(v));
			return GetAssignedNonDecParentSpanVI(m_AssignmentInfo[v], m_VarInfo[v]);
		}
		inline std::span<TULit> GetAssignedNonDecParentSpanVI(TAssignmentInfo& ai, TVarInfo& vi)
		{			
			return ai.IsAssignedBinary() ? span(&vi.m_BinOtherLit, 1) : Cls(vi.m_ParentClsInd);
		}

		inline bool IsParentLongInitial(TAssignmentInfo& ai, TVarInfo& vi)
		{
			return !ai.IsAssignedBinary() && vi.m_ParentClsInd != BadClsInd && !ClsGetIsLearnt(vi.m_ParentClsInd);
		}
		
		// Returns true iff the assignment is contradictory
		bool Assign(TULit l, TUInd parentClsInd, TULit otherWatch, TUV decLevel, bool toPropagate = true);
		void Unassign(TULit l);
		void UnassignVar(TUVar v, bool reuseTrail = false);
		[[maybe_unused]] bool DebugLongImplicationInvariantHolds(TULit l, TUV decLevel, TUInd parentClsIndIfLong);
		inline auto GetAssignedLitsHighestDecLevelIt(span<TULit> lits, size_t startInd) const { return max_element(lits.begin() + startInd, lits.end(), [&](TULit l1, TULit l2) { return GetAssignedDecLevel(l1) < GetAssignedDecLevel(l2); }); }
		inline auto GetAssignedLitsLowestDecLevelIt(span<TULit> lits, size_t startInd) const { return min_element(lits.begin() + startInd, lits.end(), [&](TULit l1, TULit l2) { return GetAssignedDecLevel(l1) < GetAssignedDecLevel(l2); }); }		
		inline auto GetSatisfiedLitLowestDecLevelIt(span<TULit> lits, size_t startInd = 0) const { return min_element(lits.begin() + startInd, lits.end(), [&](TULit l1, TULit l2) { return (IsSatisfied(l1) && !IsSatisfied(l2)) || (IsSatisfied(l1) && IsSatisfied(l2) && GetAssignedDecLevel(l1) < GetAssignedDecLevel(l2)); }); }

		// Literal currently propagated by BCP
		TULit m_CurrentlyPropagatedLit = BadULit;
		// Contradictions stashed during BCP
		CVector<TContradictionInfo> m_Cis;
		// Run BCP
		TContradictionInfo BCP();
		// Backtracking during BCP
		void BCPBacktrack(TUV decLevel, bool eraseDecLevel);
		span<TULit>::iterator FindBestWLCand(span<TULit> cls, TUV maxDecLevel);

		// Swap watch watchInd in the clause with newWatchIt
		void SwapWatch(const TUInd clsInd, bool watchInd, span<TULit>::iterator newWatchIt);
		// Swap the current watch while traversing a long WL
		void SwapCurrWatch(TULit l, span<TULit>::iterator newWatchIt, const TUInd clsInd, span<TULit>& cls, size_t& currLongWatchInd, TULit*& currLongWatchPtr, TWatchInfo& wi);

		// Holding delayed implications
		struct TDelImpl
		{
			TDelImpl(TULit l, TULit otherWatch, TUInd parentClsInd) : m_L(l), m_OtherWatch(otherWatch), m_ParentClsInd(parentClsInd) {}
			TULit m_L;
			TULit m_OtherWatch;
			TUInd m_ParentClsInd;
		};
		CVector<TDelImpl> m_Dis;
		// Process a new delayed implication in-place
		// Returns true iff BCP should stop propagating the current literal
		bool ProcessDelayedImplication(TULit diL, TULit otherWatch, TUInd parentClsInd, CVector<TContradictionInfo>& cis);
		bool m_CurrPropWatchModifiedDuringProcessDelayedImplication = false;
		
		bool TraiAssertConsistency();

		// Data structure for trail-reuse		
		struct TReuseTrail
		{
			TReuseTrail(TULit l, TUInd parentClsIndOrBinOtherLit) : m_ParentClsInd(parentClsIndOrBinOtherLit), m_L(l) {}
			// Note that the flag distinguishing between long and binary parents is in TVarInfo (per variable)
			union
			{
				// Parent clause for longs
				TUInd m_ParentClsInd;
				// The other literal in the clause for binaries
				TULit m_BinOtherLit;
			};
			TULit m_L;
		};
		// The latest trail to re-use (active only if trail reuse is on)
		CVector<TReuseTrail> m_ReuseTrail;

		void FixPolarityInternal(TULit l, bool onlyOnce = false);
		void ClearUserPolarityInfoInternal(TUVar v);


		/*
		* Assumptions
		*/

		// The vector of assumptions
		// During Solve invocation, m_Assumps will hold all the assumptions, 
		// where m_Assumps's capacity is precisely the number of assumptions. It will always be cleared at the end of Solve.		
		CDynArray<TULit> m_Assumps;		
		// A falsified assumption of the earliest decision level (if any)
		TULit m_EarliestFalsifiedAssump = BadULit;
		// The decision level of the last assigned assumption
		TUV m_DecLevelOfLastAssignedAssumption = 0;
		// This function is called at an early stage of every Solve to set up assumption handling
		void HandleAssumptions(const span<TLit> userAssumps);

		inline bool IsAssump(TULit l) const { return IsAssumpVar(GetVar(l)); }
		inline bool IsAssumpVar(TUVar v) const { return m_AssignmentInfo[v].m_IsAssump; }
		inline bool IsAssumpFalsifiedGivenVar(TUVar v) const { assert(IsAssumpVar(v)); return IsFalsified(GetLit(v, false)) != m_AssignmentInfo[v].m_IsAssumpNegated; }
		inline TULit GetAssumpLitForVar(TUVar v) const { assert(IsAssumpVar(v)); return GetLit(v, m_AssignmentInfo[v].m_IsAssumpNegated); }

		// Assumption-UNSAT-core-related
		// 
		// If there is an internal contradiction in the assumption list, this is a literal of the problematic variable
		TULit m_SelfContrOrGloballyUnsatAssump = BadULit;
		// Latest m_EarliestFalsifiedAssump for assumption-UNSAT-core
		TULit m_LatestEarliestFalsifiedAssump = BadULit;
		// The Solve invocation corresponding to the value in m_SelfContradictingAssump 
		uint64_t m_SelfContrOrGloballyUnsatAssumpSolveInv = 0;
		// The Solve invocation corresponding to the latest m_EarliestFalsifiedAssump for assumption-UNSAT-core
		uint64_t m_LatestEarliestFalsifiedAssumpSolveInv = 0;
		// The latest user assumptions
		span<TLit> m_UserAssumps;
		// The latest invocation of Solve, after which an assumption UNSAT core was requested and extracted
		uint64_t m_LatestAssumpUnsatCoreSolveInvocation = numeric_limits<uint64_t>::max();
		inline void AssumpUnsatCoreCleanUpIfRequired() { if (m_Stat.m_SolveInvs == m_LatestAssumpUnsatCoreSolveInvocation) { CleanVisited(); } }

		/*
		* Statistics
		*/

		TToporStatistics m_Stat;

		/*
		* Decision Strategy
		*/

		CVarScores m_VsidsHeap;

		// Get the decision literal
		TULit Decide();
		// Choose the next assigned polarity for the given variable
		inline bool GetNextPolarityIsNegated(TUVar v);
		inline bool IsForced(TUVar v) const { return m_PolarityInfoActivated && v < m_PolarityInfo.cap() && m_PolarityInfo[v].IsNextPolarityDetermined() && m_PhaseStage != TPhaseStage::PHASE_STAGE_DONT_FORCE; }
		inline bool IsNotForced(TUVar v) const { return !m_PolarityInfoActivated || v >= m_PolarityInfo.cap() || !m_PolarityInfo[v].IsNextPolarityDetermined() || m_PhaseStage == TPhaseStage::PHASE_STAGE_DONT_FORCE; }
		void UpdateScoreVar(TUVar v, double mult = 1.0);
		// This function is invoked after every conflict
		void UpdateDecisionStrategyOnNewConflict(TUV glueScoreOfLearnt, TUVar lowestGlueUpdateVar, TUVar fakeTrailEnd);
		void DecisionInit();
		double m_CurrInitClssBoostScoreMult = 0;
		//CTopiParam<uint8_t> m_ParamInitClssBoostScoreStrat = { m_Params, "/decision/init_clss_boost/strat", "Initial query: variable score boost strategy for user clauses -- 0: none; 1: no clause-size, user-order; 2: clause-size-aware, user-order; 3: no clause-size, reverse-user-order; 4: clause-size-aware, reverse-user-order", {2, 0, 0, 0, 0, 0}, 0, 4 };
		//CTopiParam<uint8_t> m_ParamInitClssBoostScoreStratAfterInit = { m_Params, "/decision/init_clss_boost/strat_after_init", "After initial query: variable score boost strategy for user clauses -- 0: none; 1: no clause-size, user-order; 2: clause-size-aware, user-order; 3: no clause-size, reverse-user-order; 4: clause-size-aware, reverse-user-order", {1, 0, 0, 0, 0, 0}, 0, 4 };
		uint8_t InitClssBoostScoreStrat() const { return m_QueryPrev == TQueryType::QUERY_NONE ? m_ParamInitClssBoostScoreStrat : m_ParamInitClssBoostScoreStratAfterInit; }
		bool InitClssBoostScoreStratOn() const { return InitClssBoostScoreStrat() > 0; }
		bool InitClssBoostScoreStratIsReversedOrder() const { return InitClssBoostScoreStrat() >= 3; }
		bool InitClssBoostScoreStratIsClauseSizeAware() const { return InitClssBoostScoreStrat() == 2 || InitClssBoostScoreStrat() == 4; }
		
		/*
		* Backtracking
		*/
		void BacktrackingInit();
		TUV m_CurrChronoBtIfHigher = 0;
		uint64_t m_ConfsSinceNewInv = 0;

		/*
		* Conflict Analysis and Learning
		*/
		
		void ConflictAnalysisLoop(TContradictionInfo& contradictionInfo, bool reuseTrail = false, bool assumpMode = false);

		// Return: (1) asserting clause span; (2) Clause index (for long clauses)
		pair<span<TULit>, TUInd> LearnAndUpdateHeuristics(TContradictionInfo& contradictionInfo, CVector<TULit>& clsBeforeAllUipOrEmptyIfAllUipFailed, bool reachDecisionMaxLevel = false);
		// Return a flipped clause if: 
		// (1) Flipped recording is on; (2) The current level is flipped; (3) The flipped clause is not subsumed by the main clause
		// The return span is empty iff the recording is unsuccessful (in which case TUInd is BadParentClsInd too)
		pair<span<TULit>, TUInd> RecordFlipped(TContradictionInfo& contradictionInfo, span<TULit> mainClsBeforeAllUip);
		// Carry out the deep minimization (applied already in Minisat; appears also in Maple and Fiver)
		// Uses m_RootedVars, which must be empty before the MinimizeClause call
		void MinimizeClauseMinisat(CVector<TULit>& cls);
		// Binary minimization (there in Glucose, Fiver, Maple)
		// Uses m_RootedVars, which must be empty before the MinimizeClause call
		void MinimizeClauseBin(CVector<TULit>& cls);
		// AllUIP clause generation, based on the ALL-UIP SAT'20 paper; returns an empty clause, if fails
		// Returns true iff succeeded
		bool GenerateAllUipClause(CVector<TULit>& cls);
		void UpdateAllUipInfoAfterRestart();
		
		inline bool IsCbLearntOrDrat() const { return M_CbNewLearntCls != nullptr || m_OpenedDratFile != nullptr; }
		void NewLearntClsApplyCbLearntDrat(span<TULit> learntCls);

		void RemoveLitsFromSubsumed();
		CVector<TUVar> m_VarsParentSubsumed;
		
		// An array of vectors of literals for various algorithms (which must cleaned up *before* every usage) 
		// The capacity, assigned at the beginning of Solve, is the number of variables
		array<CVector<TULit>, 2> m_HandyLitsClearBefore;
		// All the variables, whose m_Visit flag is on
		CVector<TUVar> m_VisitedVars;

		[[maybe_unused]] bool IsVisitedConsistent() const;
		inline void CleanVisited() { m_VisitedVars.clear([&](TUVar& v) { m_AssignmentInfo[v].m_Visit = false; }); }
		inline void MarkVisitedVar(TUVar v) { if (!IsVisitedVar(v)) { m_VisitedVars.push_back(v); m_AssignmentInfo[v].m_Visit = true; } }
		inline void MarkVisited(TULit l) { MarkVisitedVar(GetVar(l)); }
		inline bool IsVisitedVar(TUVar v) const { return m_AssignmentInfo[v].m_Visit; }
		inline bool IsVisited(TULit l) const { return IsVisitedVar(GetVar(l)); }			
		inline TUVar VisitedPopBack() { m_AssignmentInfo[m_VisitedVars.back()].m_Visit = false; return m_VisitedVars.pop_back(); }
		// All the variables, whose m_Root flag is on
		CVector<TUVar> m_RootedVars;
		inline void CleanRooted() { m_RootedVars.clear([&](TUVar& v) { m_AssignmentInfo[v].m_Root = false; }); }
		inline void MarkRootedVar(TUVar v) { if (!IsRootedVar(v)) { m_RootedVars.push_back(v); m_AssignmentInfo[v].m_Root = true; } }
		inline void MarkRooted(TULit l) { MarkRootedVar(GetVar(l)); }
		inline bool IsRootedVar(TUVar v) const { return m_AssignmentInfo[v].m_Root; }
		inline bool IsRooted(TULit l) const { return IsRootedVar(GetVar(l)); }
		inline TUVar RootedPopBack() { m_AssignmentInfo[m_RootedVars.back()].m_Root = false; return m_RootedVars.pop_back(); }
		// Get the # of decision levels (glue) in the clause
		TUV GetGlueAndMarkCurrDecLevels(span<TULit> cls);		
		uint64_t m_HugeCounterDecLevels = 0;
		CDynArray<uint64_t> m_HugeCounterPerDecLevel;
		// ret.first: for every decision level m_HugeCounterPerDecLevel[decLevel] - ret.first is the number of literals in cls with that decision level
		// ret.second: all the decision levels in cls in a priority queue (top will return the greatest first)		
		pair<uint64_t, priority_queue<TUV>> GetDecLevelsAndMarkInHugeCounter(span<TULit> cls);
		TCounterType m_MarkedDecLevelsCounter = 0;
		CDynArray<TCounterType> m_DecLevelsLastAppearenceCounter;
		inline bool IsAssignedMarkedDecLevel(TULit l) const { return IsAssignedMarkedDecLevelVar(GetVar(l)); }
		inline bool IsAssignedMarkedDecLevelVar(TUVar v) const { assert(IsAssignedVar(v));  return m_DecLevelsLastAppearenceCounter[GetAssignedDecLevelVar(v)] == m_MarkedDecLevelsCounter; }
		// For flipped recording
		TULit m_FlippedLit = BadULit;
		// For marking used assumptions, but can also be used for any other purpose
		void MarkDecisionsInConeAsVisited(TULit triggeringLit);

		// Counters required for on-the-fly subsuming all the parents 
		TCounterType m_CurrClsCounter = 0;
		CDynArray<TCounterType> m_CurrClsCounters;

		// All-UIP-related

		// The gap (see the SAT'20 All-UIP paper)
		TUV m_AllUipGap = 0;
		uint32_t m_AllUipAttemptedCurrRestart = 0;
		uint32_t m_AllUipSucceededCurrRestart = 0;

		// On-th-fly subsumption related

		inline bool IsOnTheFlySubsumptionContradictingOn() const { return m_ParamOnTheFlySubsumptionContradictingMinGlueToDisable > 0 && m_Stat.m_Restarts >= m_ParamOnTheFlySubsumptionContradictingFirstRestart; }
		inline bool IsOnTheFlySubsumptionParentOn() const { return m_ParamOnTheFlySubsumptionParentMinGlueToDisable > 0 && m_Stat.m_Restarts >= m_ParamOnTheFlySubsumptionParentFirstRestart; }
		/*
		* RESTARTS
		*/

		uint64_t m_RstNumericCurrConfThr = 0;
		uint64_t m_ConfsSinceRestart = 0;
		CDynArray<uint64_t> m_RstNumericLocalConfsSinceRestartAtDecLevelCreation;
		
		TWinAverage m_RstGlueLbdWin;
		double m_RstGlueGlobalLbdSum = 0;
		void RstNewAssertingGluedCls(TUV glue);

		TWinAverage m_RstGlueBlckAsgnWin;
		uint64_t m_RstGlueBlckGlobalAsgnSum = 0;	
		uint64_t m_RstGlueAssertingGluedClss = 0;

		bool Restart();
		inline double GetCurrUnforceRestartsFraction() const 
		{
			return m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamPhaseMngUnforceRestartsFractionN : 
				m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamPhaseMngUnforceRestartsFractionS : m_ParamPhaseMngUnforceRestartsFractionInit;
		}
		void NewDecLevel();
		uint8_t m_CurrRestartStrat = RESTART_STRAT_NONE;
		void RestartInit();
		uint64_t m_RestartsSinceInvStart = 0;
		static double RestartLubySequence(double y, uint64_t x);

		/*
		* Compression, simplification, deletion,  (of the clause buffer, the watch buffer, the literals)
		*/

		void ReserveVarAndLitData();
		void MoveVarAndLitData(TUVar vFrom, TUVar vTo);
		void RemoveVarAndLitData(TUVar v);

		TUVar m_LastGloballySatisfiedLitAfterSimplify = BadUVar;
		int64_t m_ImplicationsTillNextSimplify = 0;
		
		void SimplifyIfRequired();
		void CompressBuffersIfRequired();
		bool DebugAssertWaste();
		void DeleteClausesIfRequired();

		inline bool ClsChunkDeleted(TUInd clsInd) const { if (ClsGetIsLearnt(clsInd)) return false; const auto sz = ClsGetSize(clsInd); return sz < 3 || m_B[clsInd + 2] == BadULit; }
		inline TUInd ClsEnd(TUInd clsInd) const { return clsInd + ClsLitsStartOffset(ClsGetIsLearnt(clsInd), ClsGetIsOversized(clsInd)) + ClsGetSize(clsInd); }

		inline bool WLChunkDeleted(TUInd wlInd) const { return wlInd + 1 >= m_W.cap() || m_W[wlInd + 1] == BadULit; }
		inline TUInd WLEnd(TUInd wlInd) const { return wlInd + ((TUInd)1 << m_W[wlInd]); }

		// The index of the first learnt clause: it is guaranteed that no conflict clause appears before m_FirstLearntClsInd in the buffer
		// For non-incremental instances, there may be some initial clauses after m_FirstLearntClsInd
		TUInd m_FirstLearntClsInd = numeric_limits<TUInd>::max();
					
		struct TClsDelInfo
		{
			uint64_t m_ConfsPrev = 0;
			uint64_t m_TriggerNext = 0;
			uint64_t m_TriggerInc = 0;
			double m_TriggerMult = 0.;
			uint64_t m_TriggerMax = 0;
			float m_FracToDelete = 0.;		
			uint8_t m_GlueNeverDelete = 0;
			uint8_t m_Clusters = 0;
			uint8_t m_MaxClusteredGlue = 0;
			bool m_Initialized = false;
			inline uint8_t GetCluster(TUV glue) const
			{
				assert(m_Clusters != 0);
				assert(glue >= (TUV)m_GlueNeverDelete);
				
				if (glue > (TUV)m_MaxClusteredGlue)
				{
					return numeric_limits<uint8_t>::max();
				}
				
				uint8_t oneClusterSize = (m_MaxClusteredGlue - m_GlueNeverDelete) / m_Clusters;
				if (oneClusterSize == 0)
				{
					oneClusterSize = 1;
				}
				return ((uint8_t)glue - m_GlueNeverDelete - 1) / oneClusterSize;
			}
		};
		TClsDelInfo m_ClsDelInfo;

		double m_ClsDelOneTierActivityIncrease = 1.0;
		void ClsDeletionInit();
		void ClsDelNewLearntOrGlueUpdate(TUInd clsInd, TUV prevGlue);
		void ClsDeletionDecayActivity();
		inline uint64_t ClsDeletionTrigger() const { return m_Stat.m_ActiveLongLearntClss; }
		
		inline TUV GetGlueMinFreeze() const
		{
			return m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamClsDelLowMinGlueFreezeAi :
				m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamClsDelLowMinGlueFreezeS : m_ParamClsDelLowMinGlueFreeze;
		}

		/*
		* Query-type, including parameters
		*/
		
		// Change the parameter to this value immediately after the initial invocation; don't undo the change
		const string m_ContextParamAfterInitInvPrefix = "/__ai";
		// Change the parameter to this value immediately before any invocation with conflict threshold <= /meta_strategy/short_inv_conf_thr; undo the change immediately after Solve
		const string m_ContextParamShortInvLifetimePrefix = "/__s";
		vector<pair<string, double>> m_AfterInitInvParamVals;
		vector<pair<string, double>> m_ShortInvLifetimeParamVals;

		enum class TQueryType : uint8_t
		{
			// Initial SAT solver query 
			QUERY_INIT,
			// Incremental short query (conflict threshold <= m_ParamShortQueryConfThrInv)
			QUERY_INC_SHORT,
			// Incremental normal query (conflict threshold > m_ParamShortQueryConfThrInv)
			QUERY_INC_NORMAL,
			// Initial value
			QUERY_NONE
		};

		TQueryType m_QueryCurr = TQueryType::QUERY_NONE;
		TQueryType m_QueryPrev = TQueryType::QUERY_NONE;

		/*
		* Phase management
		*/

		enum class TPhaseStage : uint8_t
		{
			PHASE_STAGE_STANDARD,
			PHASE_STAGE_DONT_FORCE,
		};		

		TPhaseStage m_PhaseStage = TPhaseStage::PHASE_STAGE_STANDARD;
		TPhaseStage m_PhaseInitStage = TPhaseStage::PHASE_STAGE_STANDARD;
		inline string GetPhaseStageStr() const { return m_PhaseStage == TPhaseStage::PHASE_STAGE_STANDARD ? "Standard" : "Don't-force"; }

		/*
		* DRAT
		*/

		ofstream* m_OpenedDratFile = nullptr;
		bool m_IsDratBinary = true;

		/*
		* Callbacks
		*/
		
		TCbStopNow M_CbStopNow = nullptr;
		bool m_InterruptNow = false;
		TCbNewLearntCls M_CbNewLearntCls = nullptr;
		CVector<TLit> m_UserCls;

		/*
		* Debugging
		* The printing functions print out some useful info and return true (to be used inside assert statements)
		*/

		string SVar(TUVar v);
		string SLit(TULit l);
		string STrail();
		string SLits(span<TULit> litSpan);
		string SUserLits(span<TLit> litSpan);
		string SVars(span<TULit> varSpan);
		string SReuseTrailEntry(const TReuseTrail& rt);
		string SReuseTrail();
		string SE2I();

		bool P(const string& s);

		vector<bool> m_DebugModel;
		void PrintDebugModel(TToporReturnVal trv);
		// Assumed to be invoked when the trail and the clauses are expected to be consistent with debug-model, that is:
		// The assigned trail must be consistent, while there must be no clauses falsified by the debug-model
		void VerifyDebugModel();
		
		/*
		* Dump file
		*/

		std::unique_ptr<std::ofstream> m_DumpFile = nullptr;
		void DumpSetUp(const char* filePrefix);		
		void DumpSpan(const span<TLit> c, const string& prefix = "", const string& suffix = "", bool addNewLine = true);
		
		/*
		* Misc
		*/

		template <class T>
		void ReserveExactly(T& toReserve, size_t newCap, size_t initVal, const string& errSuffix)
		{
			if (unlikely(IsUnrecoverable())) return;
			toReserve.reserve_exactly(newCap, initVal);
			if (unlikely(toReserve.uninitialized_or_erroneous())) SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "Couldn't ReserveExactly with initial value " + to_string(initVal) + " : " + errSuffix);
		}

		template <class T>
		void ReserveExactly(T& toReserve, size_t newCap, const string& errSuffix)
		{
			if (unlikely(IsUnrecoverable())) return;
			toReserve.reserve_exactly(newCap);
			if (unlikely(toReserve.uninitialized_or_erroneous())) SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "Couldn't ReserveExactly : " + errSuffix);
		}

		void SetMultipliers();

		bool m_AxePrinted = false;
		void PrintAxe();
		// Not-verbose: use inside asserts
		[[maybe_unused]] inline bool NV(const uint8_t vl) const { return m_ParamVerbosity <= vl || m_Stat.m_Conflicts < m_ParamHeavyVerbosityStartConf; }
	};

}
