// Copyright(C) 2021-2023 Intel Corporation
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
#include <tuple>

#include "TopiStatistics.hpp"
#include "ToporBitArrayBuffer.hpp"
#include "ToporDynArray.hpp"
#include "ToporBitArray.hpp"
#include "ToporVector.hpp"
#include "TopiParams.hpp"
#include "TopiVarScores.hpp"
#include "ToporWinAverage.hpp"
#include "SetInScope.h"


using namespace std;

namespace Topor
{	
	// To be used by compressed buffer management
	constexpr array<uint32_t, 32> lowestClsSizePerBits = []
	{
		array<uint32_t, 32> lcspb = {};
		
		lcspb[0] = 3;
		for (uint8_t currBits = 1; currBits < 32; ++currBits)
		{
			// 2^n - n + 3
			lcspb[currBits] = ((uint32_t)1 << currBits) - currBits + 3;
		}
		return lcspb;
	}();

	template <typename T>
	inline std::string HexStr(T val, size_t width = sizeof(T) * 2)
	{
		stringstream ss;
		ss << setfill('0') << setw(width) << hex << (val | 0);
		return ss.str();
	}
	
	template <typename T>
	inline size_t memMb(const vector<T>& v)
	{
		return v.size() * sizeof(T) / 1000;
	}

	template <typename TKey, typename TVal>
	inline size_t memMb(const unordered_map<TKey, TVal>& m)
	{
		return m.size() * (sizeof(TKey) + sizeof(TVal)) / 1000;
	}

	using TCompressedClss = unordered_map<uint16_t, CBitArray>;
	inline size_t memMb(const TCompressedClss& bc)
	{
		auto res = bc.size() * sizeof(uint16_t);
		for (auto& i : bc)
		{
			res += i.second.memMb();
		}

		return res;
	}

	// The main solver class
	template <typename TLit, typename TUInd, bool Compress>
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
		TLit GetLitDecLevel(TLit l) const;
		uint64_t GetSolveInvs() const;
		TLit GetMaxUserVar() const;
		TLit GetMaxInternalVar() const;
		std::string GetStatStrShort(bool forcePrintingHead = false);
		uint64_t GetConflictsNumber() const;
		uint64_t GetActiveClss() const;
		uint64_t GetActiveLongLearntClss() const;
		uint64_t GetBacktracks() const;
		uint64_t GetAssumpReuseBacktrackLevelsSaved() const;
		uint64_t GetPropagations() const;
		
		std::vector<TToporLitVal> GetModel() const;
		TToporStatistics<TLit, TUInd> GetStatistics() const;		
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
		void DumpDrat(ofstream& openedDratFile, bool isDratBinary, bool dratSortEveryClause) { m_OpenedDratFile = &openedDratFile; m_IsDratBinary = isDratBinary; m_DratSortEveryClause = dratSortEveryClause; }
		// Set callback: stop-now
		void SetCbStopNow(TCbStopNow CbStopNow) { M_CbStopNow = CbStopNow; }
		// Interrupt now
		void InterruptNow() { m_InterruptNow = true; }
		// Set callback: new-learnt-clause
		void SetCbNewLearntCls(TCbNewLearntCls<TLit> CbNewLearntCls) { M_CbNewLearntCls = CbNewLearntCls; }
		// Boost the score of the variable v by mult
		// DUMPS
		void BoostScore(TLit vExternal, double mult = 1.0);
		// Fix the polarity of the variable |l| to l
		// onlyOnce = false: change until ClearUserPolarityInfo(|l|) is invoked (or, otherwise, forever)
		// onlyOnce = true: change only once right now
		// DUMPS
		void FixPolarity(TLit lExternal, bool onlyOnce = false);
		// Create an internal literal for l *now*
		// DUMPS
		void CreateInternalLit(TLit l);
		// Clear any polarity information of the variable v, provided by the user (so, the default solver's heuristic will be used to determine polarity)
		// DUMPS
		void ClearUserPolarityInfo(TLit vExternal);		
		// Backtrack to the end of decLevel; the 2nd parameter is required for statistics only
		// DUMPS
		void Backtrack(TLit decLevel, bool isBCPBacktrack = false, bool isAPICall = false);		
		// Changing the configuration to # configNum, so that every configNum generates a unique configuration
		// This is used for enabling different configs in parallel solving
		// DUMPS
		string ChangeConfigToGiven(uint16_t configNum);
		// Set the relevant data for a higher-level parallel solver
		void SetParallelData(unsigned threadId, std::function<void(unsigned threadId, int lit)> ReportUnitClause, std::function<int(unsigned threadId, bool reinit)> GetNextUnitClause);		
	protected:	
		/*
		* Internal types
		*/

		static constexpr bool Standard = !Compress;

		// The number of literal-entries in an index-entry
		static constexpr size_t LitsInInd = sizeof(TUInd) / sizeof(TLit);

		// The TLit type, representing the external literals, unsigned
		// Will be used for storing internal literals, whose sign is determined by the LSB, rather than the MSB
		using TULit = make_unsigned<TLit>::type;
		static_assert(numeric_limits<TULit>::digits == numeric_limits<TLit>::digits + 1);

		// Bad literal
		TULit static constexpr BadULit = 0;

		// TUL is used to hold unsigned integers, whose number is not greater than that of the literaks; used only for code clarity
		using TUL = TULit;
		// TUVar is used to hold variables; used only for code clarity
		using TUVar = TULit;
		// TUV is used to hold unsigned integers, whose number is not greater than that of the variables; used only for code clarity
		using TUV = TUVar;
		// Bad variable
		TULit static constexpr BadUVar = 0;

		// counter type for discovering duplicates, tautologies, contradictions etc., e.g., in incoming clauses and assumptions
		// In the context of handling new clauses,
		// we did some tests and found that int32_t for the counter works better than int64_t, int16_t (both are 2x slower) and int8_t (significantly slower)
		// We also tested options using bit-arrays CBitArray<2> and CBitArrayAligned<2> with memset-0 after each clause, 
		// but the former is much slower than the current implementation, while the latter is impossibly slow.
		typedef	int32_t TCounterType;
		static_assert(is_signed<TCounterType>::value);

		// A span of internal literals (TULit)
		using TSpanTULit = span<CTopi<TLit, TUInd, Compress>::TULit>;

		/*
		* Parameters
		*/

		// Contains a map from parameter name to parameter description & function to set it for every parameter
		// Every parameter adds itself to the list, so we don't have to do it manually in this class
		// To add a parameter, it suffices to declare it below, c'est tout!
		CTopiParams m_Params;		
		// The following semantics would have been better, 
		// but double template parameters are not supported even by gcc 10.2
		// CTopiParam template class semantics: <type, initVal, minVal, maxVal>, where:
		//		type must be an arithmetic type, where a floating-point type must fit into a double, and an integer-type must fit into half-a-double
		//		minVal <= initVal <= maxVal

		// To find the mode values using a regular expression: (, *){(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*)}(,| )
		// Replace expression, used last time to create a new mode from mode 2 (when there was one less mode): $1{$2,$3,$4,$5,$6,$7,$8,$9,$4}$10
		// The special mode meta-parameter; Every parameter can be provided either one single default or a separate default for each mode (in an array)
		CTopiParam<TModeType> m_Mode = { m_Params, m_ModeParamName, "The mode (0: Unweighted MaxSAT; 1: FLA; 2: FSN; 3: MF2S; 4: LAR; 5: Weighted MaxSAT; 6: FRU; 7: SN1, 8: EBB)", 0, 0, m_Modes - 1 };
		
		// Parameters: verbosity
		CTopiParam<uint8_t> m_ParamVerbosity = { m_Params, "/verbosity/level", "Verbosity level (0: silent; 1: basic statistics; 2: trail&conflict debugging; 3: extensive debugging)", 0, 0, 3};
		CTopiParam<uint32_t> m_ParamHeavyVerbosityStartConf = { m_Params, "/verbosity/verbosity_start_conf", "Meaningful only if /verbosity/level>1: print-out at level > 1 starting with the provided conflicts number", 0 };
		CTopiParam<uint32_t> m_ParamStatPrintOutConfs = { m_Params, "/verbosity/print_out_confs", "Meaningful only if /verbosity/level>0: the rate of print-outs in #conflicts", 2000, 1 };
		CTopiParam<bool> m_ParamPrintMemoryProfiling = { m_Params, "/verbosity/print_mem_prof", "Print-out memory profiling info", false };

		// Parameters: timeout
		CTopiParam<double> m_ParamOverallTimeout = { m_Params, "/timeout/global", "An overall global timeout for Topor lifespan: Topor will be unusable after reaching this timeout (note that one can also provide a Solve-specific timeout as a parameter to Solve)", numeric_limits<double>::max(), numeric_limits<double>::epsilon() };
		CTopiParam<bool> m_ParamOverallTimeoutIsCpu = { m_Params, "/timeout/global_is_cpu", "Is the overall global timeout (if any) for Topor lifespan CPU (or, otherwise, Wall)", false };
		
		// Parameters: decision
		CTopiParam<uint8_t> m_ParamInitPolarityStrat = { m_Params, "/decision/polarity/init_strategy", "The initial polarity for a new variable: 0: negative; 1: positive; 2: random",  {1, 1, 1, 1, 1, 2, 1, 1, 1}, 0, 2 };
		CTopiParam<uint8_t> m_ParamPolarityStrat = { m_Params, "/decision/polarity/strategy", "How to set the polarity for a non-forced variable: 0: phase saving; 1: random", 0, 0, 1 };
		CTopiParam<uint32_t> m_ParamPolarityFlipFactor = { m_Params, "/decision/polarity/flip_factor", "If non-0, every N's polarity selection will be flipped", 0};
		CTopiParam<bool> m_ParamIfExternalBoostScoreValueOverride = { m_Params, "/decision/polarity/if_external_boost_score_value_override", "Override the boost value by when BoostScore is invoked by the user?", false };
		CTopiParam<double> m_ParamExternalBoostScoreValueOverride = { m_Params, "/decision/polarity/external_boost_score_value_override", "If /decision/polarity/if_external_boost_score_value_override is on, the value by which to override the boost value by when BoostScore is invoked by the user", 1. };
		CTopiParam<double> m_ParamVarActivityInc = { m_Params, "/decision/vsids/var_activity_inc", "Variable activity bumping factor's initial value: m_PosScore[v].m_Score += m_VarActivityInc is carried out for every variable visited during a conflict (hard-coded to 1.0 in Glucose-based solvers)", {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.8, 1.0}, 0.0 };
		CTopiParam<double> m_ParamVarActivityIncDecay = { m_Params, "/decision/vsids/var_activity_inc_decay", "After each conflict, the variable activity bumping factor is increased by multiplication by 1/m_ParamVarActivityIncDecay (the initial value is provided in the parameter):  m_ParamVarActivityInc *= (1 / m_ParamVarActivityIncDecay), where it's 0.8 in Glucose-based solvers and 0.95 in Minisat", {0.95, 0.8, 0.8, 0.8, 0.8, 0.925, 0.8, 0.95, 0.925}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<bool> m_ParamVarActivityIncDecayReinitN = { m_Params, "/decision/vsids/var_activity_inc_decay_reinit_n", "Re-initialize /decision/vsids/var_activity_inc_decay before normal incremental Solve?", {true, false, false, false, false, false, false, true, false} };
		CTopiParam<bool> m_ParamVarActivityIncDecayReinitS = { m_Params, "/decision/vsids/var_activity_inc_decay_reinit_s", "Re-initialize /decision/vsids/var_activity_inc_decay before short incremental Solve?", {true, false, false, false, false, true, false, false, false} };
		CTopiParam<uint32_t> m_ParamVarActivityIncDecayStopReinitSInv = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_s_inv", "The first short incremental invocation to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0 };
		CTopiParam<uint32_t> m_ParamVarActivityIncDecayStopReinitRestart = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_restart", "The first restart to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0 };
		CTopiParam<uint32_t> m_ParamVarActivityIncDecayStopReinitConflict = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_conflict", "The first conflict to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0 };
		CTopiParam<double> m_ParamVarActivityIncDecayStopReinitTime = { m_Params, "/decision/vsids/var_activity_inc_decay_stop_reinit_time", "The time in sec. to stop re-initializing /decision/vsids/var_activity_inc_decay before short incremental Solve (0.0: never stop; relevant only if /decision/vsids/var_activity_inc_decay_reinit_s=1)", 0.0 };
		CTopiParam<double> m_ParamVarActivityIncDecayReinitVal = { m_Params, "/decision/vsids/var_activity_inc_decay_reinit_val", "For /decision/vsids/var_activity_inc_decay_reinit_each_solve=1: the value to re-initialize /decision/vsids/var_activity_inc_decay before each Solve", {0.8, 0.8, 0.8, 0.8, 0.8, 0.925, 0.8, 0.8, 0.8}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<double> m_ParamVarDecayInc = { m_Params, "/decision/vsids/var_decay_inc", "The increment value for var_activity_inc_decay each var_decay_update_conf_rate conflicts:  m_ParamVarDecay += m_ParamVarDecayInc (hard-coded to 0.01 in Glucose-based solvers)", {0.01, 0.01, 0.01, 0.01, 0.01, 0.04, 0.01, 0.01, 0.01}, 0, 1.0 };
		CTopiParam<double> m_ParamVarDecayIncAi = { m_Params, "/decision/vsids/var_decay_inc_ai", "After initial: the increment value for var_activity_inc_decay each var_decay_update_conf_rate conflicts:  m_ParamVarDecay += m_ParamVarDecayInc (hard-coded to 0.01 in Glucose-based solvers)", 0.01, 0, 1.0 };
		CTopiParam<double> m_ParamVarDecayIncS = { m_Params, "/decision/vsids/var_decay_inc_s", "Short incremental: The increment value for var_activity_inc_decay each var_decay_update_conf_rate conflicts:  m_ParamVarDecay += m_ParamVarDecayInc (hard-coded to 0.01 in Glucose-based solvers)", {0.01, 0.01, 0.01, 0.01, 0.01, 0.0025, 0.01, 0.01, 0.01}, 0, 1.0 };
		CTopiParam<double> m_ParamVarDecayMax = { m_Params, "/decision/vsids/var_decay_max", "The maximal value for var_activity_inc_decay", {0.99, 0.95, 0.9, 0.95, 0.95, 0.98, 0.95, 0.99, 0.9}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<double> m_ParamVarDecayMaxAi = { m_Params, "/decision/vsids/var_decay_max_ai", "After initial: The maximal value for var_activity_inc_decay", {0.99, 0.95, 0.9, 0.95, 0.95, 0.99, 0.95, 0.99, 0.94}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<double> m_ParamVarDecayMaxS = { m_Params, "/decision/vsids/var_decay_max_s", "Short incremental: The maximal value for var_activity_inc_decay", {0.99, 0.95, 0.9, 0.95, 0.95, 0.98, 0.95, 0.99, 0.94}, numeric_limits<double>::epsilon(), 1.0 };
		CTopiParam<uint32_t> m_ParamVarDecayUpdateConfRate = { m_Params, "/decision/vsids/var_decay_update_conf_rate", "The rate in conflicts of var_decay's update (5000 in Glucose-based solvers)", 5000, 1 };
		inline bool IsVsidsInitOrderParam(const string& paramName) const { return paramName == "/decision/vsids/init_order"; }
		CTopiParam<bool> m_ParamVsidsInitOrder = { m_Params, "/decision/vsids/init_order", "The order of inserting into the VSIDS heap (0: bigger indices first; 1: smaller indices first)", {false, true, false, true, true, false, true, false, false} };
		CTopiParam<bool> m_ParamVsidsInitOrderAi = { m_Params, "/decision/vsids/init_order_ai", "After initial call: the order of inserting into the VSIDS heap (0: bigger indices first; 1: smaller indices first)", {false, true, false, true, true, false, true, true, false} };
		CTopiParam<bool> m_ParamVarActivityGlueUpdate = { m_Params, "/decision/vsids/var_activity_glue_update", "Do we increase the VSIDS score for every last-level variable, visited during a conflict, whose parent is a learnt clause with LBD score lower than that of the newly learnt clause?", {true, true, true, false, true, true, false, false, true} };
		CTopiParam<bool> m_ParamVarActivityUseMapleLevelBreaker = { m_Params, "/decision/vsids/var_activity_use_maple_level_breaker", "Maple (MapleLCMDistChronoBT-f2trc-s) multiplies the variable activity bumping factor by 1.5 for variables, whose dec. level is higher-than-or-equal than 2nd highest-1 and by 0.5 for other variables; use it?", {false, false, false, true, true, false, true, false, false} };
		CTopiParam<bool> m_ParamVarActivityUseMapleLevelBreakerAi = { m_Params, "/decision/vsids/var_activity_use_maple_level_breaker_ai", "After initial call: Maple (MapleLCMDistChronoBT-f2trc-s) multiplies the variable activity bumping factor by 1.5 for variables, whose dec. level is higher-than-or-equal than 2nd highest-1 and by 0.5 for other variables; use it?", {false, false, false, true, true, false, false, false, false} };
		CTopiParam<uint32_t> m_ParamVarActivityMapleLevelBreakerDecrease = { m_Params, "/decision/vsids/var_activity_maple_level_breaker_decrease", "If var_activity_use_maple_level_breaker is on, this number is the decrease in the 2nd highest level, so that if the variable is higher-than-or-equal, its bump is more significant", {1, 1, 1, 1, 0, 1, 1, 1, 1} };

		CTopiParam<uint8_t> m_ParamInitClssBoostScoreStrat = { m_Params, "/decision/init_clss_boost/strat", "Initial query: variable score boost strategy for user clauses -- 0: none; 1: no clause-size, user-order; 2: clause-size-aware, user-order; 3: no clause-size, reverse-user-order; 4: clause-size-aware, reverse-user-order", {2, 0, 0, 0, 0, 0, 4, 2, 0}, 0, 4 };
		CTopiParam<uint8_t> m_ParamInitClssBoostScoreStratAfterInit = { m_Params, "/decision/init_clss_boost/strat_after_init", "After initial query: variable score boost strategy for user clauses -- 0: none; 1: no clause-size, user-order; 2: clause-size-aware, user-order; 3: no clause-size, reverse-user-order; 4: clause-size-aware, reverse-user-order", {1, 0, 0, 0, 0, 4, 0, 1, 0}, 0, 4 };

		CTopiParam<double> m_ParamInitClssBoostMultHighest = { m_Params, "/decision/init_clss_boost/mult_highest", "Variable score boost for initial clauses: highest mult value", {10., 10., 10., 10., 10., 10., 10., 5., 10.} };
		CTopiParam<double> m_ParamInitClssBoostMultLowest = { m_Params, "/decision/init_clss_boost/mult_lowest", "Variable score boost for initial clauses: lowest mult value", 1. };
		CTopiParam<double> m_ParamInitClssBoostMultDelta = { m_Params, "/decision/init_clss_boost/mult_delta", "Variable score boost for initial clauses: delta mult value",  {0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.01, 0.0001} };

		CTopiParam<bool> m_ParamRandomizePolarityAtEachIncrementalCall = { m_Params, "/decision/polarity/randomize_at_incremental_calls", "Randomize the polarities of all the variables once at the beginning of each incremental call", false };

		// Parameters: backtracking
		CTopiParam<TUV> m_ParamChronoBtIfHigherInit = { m_Params, "/backtracking/chrono_bt_if_higher_init", "Initial query: backtrack chronologically, if the decision level difference is higher than the parameter", {100, numeric_limits<TUV>::max(), 50, 100, 100, 50, 100, 0, 100} };
		CTopiParam<TUV> m_ParamChronoBtIfHigherN = { m_Params, "/backtracking/chrono_bt_if_higher_n", "Normal (non-short) incremental query: Backtrack chronologically, if the decision level difference is higher than the parameter", {100, numeric_limits<TUV>::max(), 50, 100, 100, 50, 100, 0, 50} };
		CTopiParam<TUV> m_ParamChronoBtIfHigherS = { m_Params, "/backtracking/chrono_bt_if_higher_s", "Short incremental query: Backtrack chronologically, if the decision level difference is higher than the parameter", {100, numeric_limits<TUV>::max(), 50, 100, 100, 50, 100, 100, 50} };

		CTopiParam<uint32_t> m_ParamConflictsToPostponeChrono = { m_Params, "/backtracking/conflicts_to_postpone_chrono", "The number of conflicts to postpone considering any backtracking, but NCB", {0, 0, 0, 4000, 8000, 0, 4000, 0, 4000} };
		CTopiParam<uint8_t> m_ParamCustomBtStratInit = { m_Params, "/backtracking/custom_bt_strat_init", "Initial query: 0: no custom backtracking; 1, 2: backtrack to the level containing the variable with the best score *instead of any instances of supposed chronological backtracking*, where ties are broken based on the value -- 1: higher levels are preferred; 2: lower levels are preferred ", {2, 0, 0, 0, 1, 0, 0, 0, 0}, 0, 2 };
		CTopiParam<uint8_t> m_ParamCustomBtStratN = { m_Params, "/backtracking/custom_bt_strat_n", "Normal (non-short) incremental query: 0: no custom backtracking; 1, 2: backtrack to the level containing the variable with the best score *instead of any instances of supposed chronological backtracking*, where ties are broken based on the value -- 1: higher levels are preferred; 2: lower levels are preferred ", {0, 0, 0, 0, 1, 0, 0, 0, 0}, 0, 2 };
		CTopiParam<uint8_t> m_ParamCustomBtStratS = { m_Params, "/backtracking/custom_bt_strat_s", "Short incremental query: 0: no custom backtracking; 1, 2: backtrack to the level containing the variable with the best score *instead of any instances of supposed chronological backtracking*, where ties are broken based on the value -- 1: higher levels are preferred; 2: lower levels are preferred ", {2, 0, 0, 0, 1, 0, 0, 0, 0}, 0, 2 };
		
		// Parameters: BCP
		constexpr uint8_t InitEntriesPerWL() { return 4 >= TWatchInfo::BinsInLongBitCeil ? 4 : TWatchInfo::BinsInLongBitCeil; };
		CTopiParam<uint8_t> m_ParamInitEntriesPerWL = { m_Params, "/bcp/init_entries_per_wl", "BCP: the number of initial entries in a watch list", InitEntriesPerWL(), TWatchInfo::BinsInLongBitCeil };
		CTopiParam<uint8_t> m_ParamBCPWLChoice = { m_Params, "/bcp/wl_choice", "User clause processing: how to choose the watches -- 0: prefer shorter WL; 1: prefer longer WL; 2: disregard WL length", {0, 2, 0, 1, 0, 0, 1, 0, 0}, 0, 2 };
		CTopiParam<uint8_t> m_ParamExistingBinWLStrat = { m_Params, "/bcp/existing_bin_wl_start", "BCP: what to do about duplicate binary clauses -- 0: nothing; 1: boost their VSIDS score; 2: add another copy to the watches; 3: inprocessing (if on) to remove duplicates; 4: inprocessing (if on) to boost their VSIDS score", {1, 1, 1, 1, 2, 1, 1, 1, 1}, 0, 4 };
		CTopiParam<double> m_ParamBinWLScoreBoostFactor = { m_Params, "/bcp/bin_wl_start_score_boost_factor", "BCP: if /bcp/existing_bin_wl_start=1 or 4, what's the factor for boosting the scores", {1., 1., 1., 1., 1., 1., 1., 0.5, 1.}, numeric_limits<double>::epsilon() };
		CTopiParam<uint8_t> m_ParamBestContradictionStrat = { m_Params, "/bcp/best_contradiction_strat", "BCP's best contradiction strategy: 0: size; 1: glue; 2: first; 3: last", {0, 0, 0, 0, 0, 0, 0, 3, 0}, 0, 3 };

		// Parameters: Add-user-clause
		CTopiParam<uint32_t> m_ParamAddClsRemoveClssGloballySatByLitMinSize = { m_Params, "/add_user_clause/remove_clss_globally_sat_by_literal_larger_size", "Assigned literal strategy: check for literals satisfied at decision level 0 and remove globally satisfied clauses for sizes larger than this parameter", {numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), 2, numeric_limits<uint32_t>::max()} };
		CTopiParam<uint32_t> m_ParamAddClsRemoveClssGloballySatByLitMinSizeAi = { m_Params, "/add_user_clause/remove_clss_globally_sat_by_literal_larger_size_ai", "After initial call: assigned literal strategy: check for literals satisfied at decision level 0 and remove globally satisfied clauses for sizes larger than this parameter", {numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), 5, numeric_limits<uint32_t>::max()} };
		CTopiParam<bool> m_ParamAddClsAtLevel0 = { m_Params, "/add_user_clause/guarantee_level_0", "Guarantee that adding a user clause always occurs at level 0 (by backtracking to 0 after every solve)", false };

		// Parameters: Query type
		CTopiParam<uint32_t> m_ParamShortQueryConfThrInv = { m_Params, "/query_type/short_query_conf_thr", "If the conflict threshold is at most this parameter, this invocation is considered to be incremental short", 10000 };

		// Parameters: debugging
		CTopiParam<uint8_t> m_ParamAssertConsistency = { m_Params, "/debugging/assert_consistency", "Debugging only: assert the consistency (0: none; 1: trail; 2: trail and WL's for assigned; 3: trail and WL's for all -- an extremely heavy operation)", 0, 0, 3 };
		CTopiParam<uint32_t> m_ParamAssertConsistencyStartConf = { m_Params, "/debugging/assert_consistency_start_conf", "Debugging only; meaningful only if /debugging/assert_consistency=1: assert the consistency starting with the provided conflicts number", 0 };
		CTopiParam<uint32_t> m_ParamPrintDebugModelInvocation = { m_Params, "/debugging/print_debug_model_invocation", "The number of solver invocation to print the model in debug-model format: intended to be used for internal Topor debugging", 0 };
		CTopiParam<uint32_t> m_ParamVerifyDebugModelInvocation = { m_Params, "/debugging/verify_debug_model_invocation", "The number of solver invocation, when the debug-model is verified: intended to be used for internal Topor debugging", 0 };
		CTopiParam<bool> m_ParamDontDumpClauses = { m_Params, "/debugging/dont_dump_clauses", "If dumping is enabled, clauses are not dumped, only directives", false };

		// Parameters: assumptions handling
		CTopiParam<bool> m_ParamAssumpsSimpAllowReorder = { m_Params, "/assumptions/allow_reorder", "Assumptions handling: allow reordering assumptions when filtering", true };
		CTopiParam<bool> m_ParamAssumpsIgnoreInGlue = { m_Params, "/assumptions/ignore_in_glue", "Assumptions handling: ignore assumptions when calculating the glue score (following the SAT'13 paper \"Improving Glucose for Incremental SAT Solving with Assumptions : Application to MUS Extraction\")", false };
		
		// Parameters: conflict analysis
		CTopiParam<bool> m_ParamMinimizeClausesMinisat = { m_Params, "/conflicts/minimize_clauses", "Conflict analysis: apply deep conflict clause minimization", true };
		CTopiParam<uint32_t> m_ParamMinimizeClausesBinMaxSize = { m_Params, "/conflicts/bin_res_max_size", "Conflict analysis: maximal size to apply binary minimization (both this condition and maximal LBD must hold; 30 in Glucose, Fiver, Maple)", {30, 30, 30, 30, 50, 30, 30, 30, 30} };
		CTopiParam<uint32_t> m_ParamMinimizeClausesBinMaxLbd = { m_Params, "/conflicts/bin_res_max_lbd", "Conflict analysis: maximal LBD to apply binary minimization (both this condition and maximal size must hold; 6 in Glucose, Fiver, Maple)", 6 };
		CTopiParam<uint32_t> m_ParamFlippedRecordingMaxLbdToRecord = { m_Params, "/conflicts/flipped_recording_max_lbd", "Conflict analysis: record a flipped clause with LBD smaller than or equal to the value of the parameter", {numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), 0, 40, numeric_limits<uint32_t>::max(), 0, 90, numeric_limits<uint32_t>::max()} };
		CTopiParam<bool> m_ParamFlippedRecordDropIfSubsumed = { m_Params, "/conflicts/flipped_drop_if_subsumed", "Conflict analysis: test and drop the flipped clause, if subsumed by the main clause", true };
		CTopiParam<uint32_t> m_ParamOnTheFlySubsumptionContradictingMinGlueToDisable = { m_Params, "/conflicts/on_the_fly_subsumption/contradicting_min_glue_to_disable", "Conflict analysis: the minimal glue (LBD) to disable on-the-fly subsumption during conflict analysis over the contradicting clause (1: apply only over initial clauses)", {8, 0, 0, 0, 0, 8, 0, 8, 0} };
		CTopiParam<uint32_t> m_ParamOnTheFlySubsumptionParentMinGlueToDisable = { m_Params, "/conflicts/on_the_fly_subsumption/parent_min_glue_to_disable", "Conflict analysis: the minimal glue (LBD) to disable on-the-fly subsumption during conflict analysis over the parents (1: apply only over initial clauses)", {30, 0, 0, 0, 0, 1, 0, 30, 16} };
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
		CTopiParam<uint8_t> m_ParamRestartStrategyInit = { m_Params, "/restarts/strategy_init", "Restart strategy for the initial query: 0: numeric (arithmetic, luby or in/out); 1: LBD-average-based", {RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_NUMERIC}, 0, 1 };
		// Restart strategy: normal incremental query
		CTopiParam<uint8_t> m_ParamRestartStrategyN = { m_Params, "/restarts/strategy_n", "Restart strategy for the normal (non-short) incremental query: 0: arithmetic; 1: LBD-average-based", {RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_NUMERIC}, 0, 1 };
		// Restart strategy: short incremental query
		CTopiParam<uint8_t> m_ParamRestartStrategyS = { m_Params, "/restarts/strategy_s", "Restart strategy for the short incremental query: 0: arithmetic; 1: LBD-average-based", {RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_LBD, RESTART_STRAT_NUMERIC, RESTART_STRAT_NUMERIC}, 0, 1 };

		CTopiParam<bool> m_ParamRestartNumericLocal = { m_Params, "/restarts/numeric/is_local", "Restarts, numeric strategies: use local restarts?", true };
		CTopiParam<uint32_t> m_ParamRestartNumericInitConfThr = { m_Params, "/restarts/numeric/conflict_thr", "Restarts, numeric strategy: the initial value for the conflict threshold, which triggers a restart", {1000, 1000, 1000, 1000, 100, 1000, 1000, 1000, 1000}, 1 };
		CTopiParam<uint8_t> m_ParamRestartNumericSubStrat = { m_Params, "/restarts/numeric/sub_strat", "Restarts, numeric sub-strategy: 0: arithmetic; 1: luby", 0, 0, 1 };
		CTopiParam<uint32_t> m_ParamRestartArithmeticConfIncr = { m_Params, "/restarts/arithmetic/conflict_increment", "Restarts, arithmetic numeric strategy: increment value for conflicts", {0, 50, 50, 50, 100, 0, 50, 1, 50} };
		CTopiParam<double> m_ParamRestartLubyConfIncr = { m_Params, "/restarts/luby/conflict_increment", "Restarts, luby numeric strategy: increment value for conflicts", {1.5, 1.5, 1.5, 2, 1.5, 1.5, 2, 1.5, 1.5}, 1 + numeric_limits<double>::epsilon() };

		CTopiParam<uint32_t> m_ParamRestartLbdWinSize = { m_Params, "/restarts/lbd/win_size", "Restart, LBD-average-based strategy: window size", {50, 50, 50, 50, 25, 50, 50, 50, 50}, 1 };
		CTopiParam<uint32_t> m_ParamRestartLbdThresholdGlueVal = { m_Params, "/restarts/lbd/thr_glue_val", "Restart, LBD-average-based strategy: the maximal value for LBD queue update", {numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max(), 50, 50, numeric_limits<uint32_t>::max(), 50, numeric_limits<uint32_t>::max(), numeric_limits<uint32_t>::max()} };
		CTopiParam<double> m_ParamRestartLbdAvrgMult = { m_Params, "/restarts/lbd/average_mult", "Restarts, LBD-average-based strategy: the multiplier in the formula for determining whether it's time to restart: recent-LBD-average * multiplier > global-LBD-average", {0.8, 0.8, 0.8, 0.8, 0.9, 0.8, 0.8, 0.8, 0.8} };

		CTopiParam<bool> m_ParamRestartLbdBlockingEnable = { m_Params, "/restarts/lbd/blocking/enable", "Restarts, LBD-average-based blocking enable", {true, true, false, false, true, true, false, true, false} };
		CTopiParam<uint32_t> m_ParamRestartLbdBlockingConfsToConsider = { m_Params, "/restarts/lbd/blocking/conflicts_to_consider_blocking", "Restarts, LBD-average-based blocking strategy: the number of conflicts to consider blocking; relevant, if /restarts/lbd/blocking/enable=1", {10000, 10000, 10000, numeric_limits<uint32_t>::max(), 10000, 10000, numeric_limits<uint32_t>::max(), 10000, 10000} };
		CTopiParam<double> m_ParamRestartLbdBlockingAvrgMult = { m_Params, "/restarts/lbd/blocking/average_mult", "Restarts, LBD-average-based blocking strategy: the multiplier in the formula for determining whether the restart is blocked: #assignments > multiplier * assignments-average", 1.4 };
		CTopiParam<uint32_t> m_ParamRestartLbdBlockingWinSize = { m_Params, "/restarts/lbd/blocking/win_size", "Restarts, LBD-average-based blocking strategy: window size", {5000, 5000, 5000, 5000, 5000, 10000, 5000, 5000, 5000}, 1 };

		// Parameters: buffer multipliers
		inline bool IsMultiplierParam(const string& paramName) const { return paramName == "/multiplier/clauses" || paramName == "/multiplier/variables" || paramName == "/multiplier/watches_if_separate"; }
		CTopiParam<double> m_ParamMultClss = { m_Params, "/multiplier/clauses", "The multiplier for reallocating the clause buffer", CDynArray<TUV>::MultiplierDef, 1. };
		CTopiParam<double> m_ParamMultVars = { m_Params, "/multiplier/variables", "The multiplier for reallocating data structures, indexed by variables (and literals)", 1.33, 1. };
		CTopiParam<double> m_ParamMultWatches = { m_Params, "/multiplier/watches", "The multiplier for reallocating the watches buffer", 1.33, 1. };
		CTopiParam<double> m_ParamMultWasteWatches = { m_Params, "/multiplier/watches_waste", "The multiplier for triggering reallocation of the watches buffer, when there is too much waste", 1.33, 1. };

		// Parameters: clause simplification
		CTopiParam<bool> m_ParamSimplify = { m_Params, "/deletion/simplify", "Simplify?", true };
		CTopiParam<uint8_t> m_ParamSimplifyGlobalLevelScoreStrat = { m_Params, "/deletion/simplify_global_level_strat", "Simplify: how to update the score of the only remaining globally assigned variable (relevant only when /backtracking/custom_bt_strat != 0) -- 0: don't touch; 1: minimal out of all globals; 2: maximal out of all globals", 0, 0, 2 };

		// Parameters: data-based compression
		CTopiParam<double> m_ParamWastedFractionThrToDelete = { m_Params, "/deletion/wasted_fraction_thr", "If wasted > size * this_parameter, we physically compress the memory (0.2 in Fiver, Maple)", 0.2, numeric_limits<double>::epsilon(), 1. };
		CTopiParam<bool> m_ParamCompressAllocatedPerWatch = { m_Params, "/deletion/compress_allocated_per_watch", "Tightly compress the allocated memory per every watch during memory compression (or, otherwise, never compress per watch)", true };
		
		// Parameters: clause deletion
		CTopiParam<uint8_t> m_ParamClsDelStrategy = { m_Params, "/deletion/clause/strategy", "Clause deletion strategy (0: no clause deletion; 1: Topor; 2: Fiver)", 1, 0, 2 };
		CTopiParam<bool> m_ParamClsDelDeleteOnlyAssumpDecLevel = { m_Params, "/deletion/clause/delete_only_assump_dec_level", "Clause deletion: apply clause deletion only when at assumption decision level", false };
		CTopiParam<double> m_ParamClsLowDelActivityDecay = { m_Params, "/deletion/clause/activity_decay", "Clause deletion: activity decay factor", 0.999, numeric_limits<double>::epsilon(), 1. };

		CTopiParam<float> m_ParamClsDelLowFracToDelete = { m_Params, "/deletion/clause/frac_to_delete", "Clause deletion: the fraction of clauses to delete", {0.7f, 0.8f, 0.8f, 0.8f, 0.8f, 0.7f, 0.8f, 0.7f, 0.8f}, numeric_limits<float>::epsilon(), (float)1.f };
		CTopiParam<uint32_t> m_ParamClsDelLowTriggerInit = { m_Params, "/deletion/clause/trigger_init", "Clause deletion: the initial number of learnts to trigger clause deletion", 6000 };
		CTopiParam<uint32_t> m_ParamClsDelLowTriggerInc = { m_Params, "/deletion/clause/trigger_linc", "Clause deletion: the increment in learnts to trigger clause deletion", 75 };
		CTopiParam<double> m_ParamClsDelS1LowTriggerMult = { m_Params, "/deletion/clause/s1/trigger_inc_mult", "Clause deletion: the change in increment in learnts to trigger clause deletion (that is, the value by which /deletion/clause/trigger_linc is multiplied)", 1.2, 1. };
		CTopiParam<uint32_t> m_ParamClsDelS1LowTriggerMax = { m_Params, "/deletion/clause/s1/trigger_max", "Clause deletion: the maximal number of learnts to activate clause deletion", 60000 };
		CTopiParam<uint8_t> m_ParamClsDelS2LowGlue = { m_Params, "/deletion/clause/s2/low_glue", "Clause deletion: the low glue for an additional current-change push", {3, 3, 3, 3, 3, 6, 3, 3, 3} };
		CTopiParam<uint8_t> m_ParamClsDelS2MediumGlue = { m_Params, "/deletion/clause/s2/medium_glue", "Clause deletion: the medium glue for an additional current-change push", {5, 5, 5, 5, 5, 8, 5, 5, 5} };
		CTopiParam<uint32_t> m_ParamClsDelS2LowMediumIncValue = { m_Params, "/deletion/clause/s2/low_medium_inc", "Clause deletion: the increment for the low&medium push to current-change", 1000 };

		CTopiParam<TUV> m_ParamClsDelLowMinGlueFreeze = { m_Params, "/deletion/clause/glue_min_freeze", "Initial query: clause deletion: protect clauses for one round if their glue decreased and is lower than this value", {4, 30, 30, 30, 30, 5, 30, 30, 30}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelLowMinGlueFreezeAi = { m_Params, "/deletion/clause/glue_min_freeze_ai", "Normal incremental query: clause deletion: protect clauses for one round if their glue decreased and is lower than this value", 30, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelLowMinGlueFreezeS = { m_Params, "/deletion/clause/glue_min_freeze_s", "Short incremental query: clause deletion: protect clauses for one round if their glue decreased and is lower than this value", {5, 30, 30, 30, 30, 5, 30, 30, 30}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelGlueNeverDelete = { m_Params, "/deletion/clause/glue_never_delete", "Clause deletion: the highest glue value blocking clause deletion", {2, 2, 2, 2, 2, 3, 2, 2, 2}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelGlueClusters = { m_Params, "/deletion/clause/glue_clusters", "Clause deletion: the number of glue clusters", {11, 0, 0, 0, 0, 11, 0, 8, 0}, 0, (TUV)numeric_limits<uint8_t>::max() };
		CTopiParam<TUV> m_ParamClsDelGlueMaxCluster = { m_Params, "/deletion/clause/glue_max_lex_cluster", "Clause deletion: the highest glue which makes the clause belong to a glue-cluster", {16, 0, 0, 0, 0, 16, 0, 16, 3}, 0, (TUV)numeric_limits<uint8_t>::max() };

		// Parameters: phase saving
		CTopiParam<bool> m_ParamPhaseMngForceSolution = { m_Params, "/phase/force_solution", "Phase management: always force (the polarities of) the latest solution?", {false, false, false, false, false, false, true, false, false} };
		CTopiParam<uint16_t> m_ParamPhaseMngRestartsBlockSize = { m_Params, "/phase/restarts_block_size", "Phase management: the number of restarts in a block for phase management considerations", 10, 1 };
		CTopiParam<double> m_ParamPhaseMngUnforceRestartsFractionInit = { m_Params, "/phase/unforce_restarts_fraction_init", "Phase management for the initial query: don't force the polarities for the provided fraction of restarts", 0.25, 0., 1. };
		CTopiParam<double> m_ParamPhaseMngUnforceRestartsFractionN = { m_Params, "/phase/unforce_restarts_fraction_n", "Phase management for the normal (non-short) incremental query: don't force the polarities for the provided fraction of restarts", 0., 0., 1. };
		CTopiParam<double> m_ParamPhaseMngUnforceRestartsFractionS = { m_Params, "/phase/unforce_restarts_fraction_s", "Phase management for the short incremental query: don't force the polarities for the provided fraction of restarts", 0., 0., 1. };
		CTopiParam<uint8_t> m_ParamPhaseMngStartInvStrat = { m_Params, "/phase/start_inv_strat", "Phase management: startegy to start an invocation with; relevant when 0 < /phase/unforce_restarts_fraction < 1: 0: start with force; 1: start with unforce; 2: start with rand", 0, 0, 2 };
		CTopiParam<bool> m_ParamPhaseBoostFlippedForced = { m_Params, "/phase/boost_flipped_forced", "Phase management: boost the scores of forced variables, flipped by BCP?", false };
		CTopiParam<uint8_t> m_ParamUpdateParamsWhenVarFixed = { m_Params, "/phase/update_params_when_var_fixed", "Phase management: update the parameters to pre-defined values, when fixed-only-once (1) or fixed-forever (2) or either (3)", {0, 0, 0, 0, 0, 0, 0, 0, 3}, 0, 3 };

		// Parameters: inprocessing
		CTopiParam<bool> m_ParamInprocessingOn = { m_Params, "/inprocessing/on", "Inprocessing: on or off", false };
		CTopiParam<bool> m_ParamIngInvokeEveryQueryAfterInitPostpone = { m_Params, "/inprocessing/invoke_every_query_after_init_postpone", "Inprocessing: invoke right after every query after /inprocessing/postpone_first_inv_conflicts conflicts", true };
		CTopiParam<uint32_t> m_ParamIngPostponeFirstInvConflicts = { m_Params, "/inprocessing/postpone_first_inv_conflicts", "Inprocessing: conflicts to postpone the very first inprocessing invocation", 0 };
		CTopiParam<uint32_t> m_ParamIngConflictsBeforeNextInvocation = { m_Params, "/inprocessing/conflicts_before_next", "Inprocessing: conflicts before the next invocation", numeric_limits<uint32_t>::max() };
		

		void ReadAnyParamsFromFile();

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
		inline TLit GetExternalLit(TULit iLit) const { assert(UseI2ELitMap());  return IsNeg(iLit) ? -m_I2ELitMap[GetVar(iLit)] : m_I2ELitMap[GetVar(iLit)]; }
		bool UseI2ELitMap() const { return m_ParamVerifyDebugModelInvocation != 0 || IsCbLearntOrDrat() || M_ReportUnitCls != nullptr; }
		static constexpr TLit ExternalLit2ExternalVar(TLit l) { return l > 0 ? l : -l; }
		inline TULit E2I(TLit l) const
		{
			const TLit externalV = ExternalLit2ExternalVar(l);
			const TULit internalL = m_E2ILitMap[externalV];
			return l < 0 ? Negate(internalL) : internalL;
		}
	
		// Used to make sure tautologies are discovered and duplicates are removed in new clauses
		class CHandleNewCls;
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

		// BIT-COMPRESSED BUFFERS

		// The minimal clause size 
		static constexpr uint8_t BCMinClsSize = 3;

		// The structure of an index into the bit-compressed buffers		
		//		1 bit: is learnt clause
		constexpr static size_t BitsForLearnt = 1;
		//		5 bits: bits for [clause size] 
		//			hence, maximal clause size for M=3: N^max = 2^{S+1}+M-2 = 2^{31+1}+3-2 = 4,294,967,297, since S=2^5-1=31 for 5 bits here
		constexpr static size_t BitsForClsSize = 5;
		//		5 (if literal is 32-bit) bits: bits for each literal
		//			hence, the maximal literal is 2^(2^5=32) = 4,294,967,296
		constexpr static size_t BitsForLit = bit_width((size_t)numeric_limits<TULit>::digits - 1);
		static_assert((size_t)numeric_limits<TULit>::digits != 32 || BitsForLit == 5);
		//		[is-learnt:1][size bits:5][max literal width:5]
		constexpr static size_t BitsForHashId = BitsForLearnt + BitsForClsSize + BitsForLit;
				
		// Buffer structure:
		// Initial clause layout in the buffer with the widths
		// [size: m_BitsForClsSize] [l1: BitsForLit] [l2: BitsForLit] ... [ln: BitsForLit]
		// Deleted clause: [size: m_BitsForClsSize] [0: BitsForLit] [optional-binlit1: BitsForLit] [optional-binlit2: BitsForLit] ... [ln: BitsForLit]		

		// Conflict clause layout in the buffer:
		// [size: m_BitsForClsSize] [glue: min(11, m_BitsForClsSize+2)] [stay-for-another-round: 1] [activity: 31] [l1: BitsForLit] [l2: BitsForLit] ... [ln: BitsForLit]
		// Deleted clause: [size: m_BitsForClsSize] [glue: min(11, m_BitsForClsSize+2)] [stay-for-another-round: 1] [0: BitsForLit] [optional-binlit1: BitsForLit] [optional-binlit2: BitsForLit] ... [ln: BitsForLit]

		// Deleted literal in any buffer: [0: BitsForLit]. Works, since:
		// (1) There can be no deleted literals for clauses of size 3, whose number of bits for clause size is 0 (since binary clauses are stored separately)
		// (2) the number of bits for a literal is never lower than for the clause size

		struct TBCHashId
		{
			TBCHashId() { assert(IsError()); }
			TBCHashId(uint16_t v) : m_Val(v) {}
			TBCHashId(bool isLearnt, uint16_t bitsForClsSize, uint16_t bitsForLit) : m_IsLearnt(isLearnt), m_BitsForClsSize(bitsForClsSize), m_BitsForLit(bitsForLit), m_BitsRest(0)
			{
				assert(m_BitsForClsSize <= m_BitsForLit);
			}
			union
			{
				struct
				{
					uint16_t m_IsLearnt : BitsForLearnt;
					uint16_t m_BitsForClsSize : BitsForClsSize;
					uint16_t m_BitsForLit : BitsForLit;
					uint16_t m_BitsRest : sizeof(uint16_t) * 8 - BitsForLearnt - BitsForClsSize - BitsForLit;
				};

				uint16_t m_Val = 0;
			};
			static constexpr uint8_t BCHashIdBits() { return BitsForHashId; }
			inline operator uint16_t() const { return m_Val; }
			inline bool IsError() const { return m_BitsForLit == 0; }
			inline void SetError() { m_BitsForLit = 0; }
			inline uint8_t GetBitsGlue() const { return (size_t)m_BitsForClsSize + 2 < BitsForHashId ? (uint8_t)m_BitsForClsSize + 2 : (uint8_t)BitsForHashId; }
			inline uint8_t GetFirstLitOffset() const { return m_BitsForClsSize + m_IsLearnt * (GetBitsGlue() + GetBitsActivityAndSkipDel()); }

			inline TUV MaxGlue() const { return ((TUV)1 << GetBitsGlue()) - 1; }
			constexpr static uint8_t GetBitsSkipDel() { return 1; }
			constexpr static uint8_t GetBitsActivity() { return 31; }
			constexpr static uint8_t GetBitsActivityAndSkipDel() { return GetBitsActivity() + GetBitsSkipDel(); }
		};
		static_assert(sizeof(TBCHashId) == sizeof(uint16_t));

		// Clause-sizes: bits-for-clause-size
		// 3: 0 (special value not needed, since literals are not deleted for clauses of size 3; otherwise, special value 0 means that this is not a clause, but a deleted literal)
		// 4: 1 (0 -- special, 1)
		// 5-7: 2 (0 -- special, 1-3)
		// 8-14: 3 (0 -- special, 1-7)
		// 15-29: 4 (0 -- special, 1-15)
		// 30-60: 5 (0 -- special, 1-31)
		// 61-123: 6 (0 -- special, 1-63)
		// 124-250: 7 (0 -- special, 1-127)
		// 251-505: 8 (0 -- special, 1-255)
		// 506-1016: 9 (0 -- special, 1-511)
		// ...	
		// stored in lowestClsSizePerBits outside of the class, since there was a problem to declare a constexpr array inside of a templated class,
		// where the function for low bound is 2^n - n + 3
		void LowestClsSizePerBitsStaticAsserts()
		{
			static_assert(lowestClsSizePerBits[0] == 3);
			static_assert(lowestClsSizePerBits[1] == 4);
			static_assert(lowestClsSizePerBits[2] == 5);
			static_assert(lowestClsSizePerBits[3] == 8);
			static_assert(lowestClsSizePerBits[4] == 15);
			static_assert(lowestClsSizePerBits[5] == 30);
			static_assert(lowestClsSizePerBits[6] == 61);
			static_assert(lowestClsSizePerBits[7] == 124);
			static_assert(lowestClsSizePerBits[8] == 251);
			static_assert(lowestClsSizePerBits[9] == 506);
		}

		template <class T> static constexpr uint16_t BCClsSize2Bits(T clsSize) 
		{ 
			assert(clsSize >= BCMinClsSize);
			assert((uint64_t)clsSize <= (uint64_t)numeric_limits<uint32_t>::max());
			const auto itFirstGreater = upper_bound(lowestClsSizePerBits.begin(), lowestClsSizePerBits.end(), ((uint32_t)clsSize));
			return (uint16_t)(itFirstGreater - lowestClsSizePerBits.begin()) - 1;
		} 
		
		template <class T> static constexpr uint32_t BCClsSize2EncodedClsSize(T clsSize) 
		{
			assert(clsSize >= BCMinClsSize);
			assert((uint64_t)clsSize <= (uint64_t)numeric_limits<uint32_t>::max());
			return clsSize == BCMinClsSize ? 0 : (uint32_t)clsSize - lowestClsSizePerBits[BCClsSize2Bits(clsSize)] + 1;
		}

		// Encoded clause size + bits --> clause size
		static constexpr uint32_t BCEncodedClsSize2ClsSizeConst(uint32_t encodedClsSize, uint16_t bitsForClsSize)
		{
			if (bitsForClsSize == 0)
			{
				return BCMinClsSize;
			}

			assert(encodedClsSize > 0);
			return encodedClsSize + lowestClsSizePerBits[bitsForClsSize] - 1;
		}

		template <class TGetEncodedClsSize>
		static constexpr uint32_t BCEncodedClsSize2ClsSize(TGetEncodedClsSize GetEncodedClsSize, uint16_t bitsForClsSize)
		{
			if (bitsForClsSize == 0)
			{
				return BCMinClsSize;
			}

			const auto encodedClsSize = (uint32_t)GetEncodedClsSize();
			assert(encodedClsSize > 0);
			return encodedClsSize + lowestClsSizePerBits[bitsForClsSize] - 1;
		}

		void BCClsSizeStaticAsserts()
		{
			static_assert(BCClsSize2Bits(3u) == 0);
			static_assert(BCClsSize2Bits(4u) == 1);
			static_assert(BCClsSize2Bits(5u) == 2);
			static_assert(BCClsSize2Bits(6u) == 2);
			static_assert(BCClsSize2Bits(7u) == 2);
			static_assert(BCClsSize2Bits(8u) == 3);
			static_assert(BCClsSize2Bits(9u) == 3);
			static_assert(BCClsSize2Bits(14u) == 3);
			static_assert(BCClsSize2Bits(15u) == 4);
			static_assert(BCClsSize2Bits(16u) == 4);
			static_assert(BCClsSize2Bits(29u) == 4);
			static_assert(BCClsSize2Bits(30u) == 5);
			static_assert(BCClsSize2Bits(31u) == 5);

			static_assert(BCClsSize2EncodedClsSize(3u) == 0);
			static_assert(BCClsSize2EncodedClsSize(4u) == 1);
			static_assert(BCClsSize2EncodedClsSize(5u) == 1);
			static_assert(BCClsSize2EncodedClsSize(6u) == 2);
			static_assert(BCClsSize2EncodedClsSize(7u) == 3);
			static_assert(BCClsSize2EncodedClsSize(8u) == 1);
			static_assert(BCClsSize2EncodedClsSize(9u) == 2);
			static_assert(BCClsSize2EncodedClsSize(14u) == 7);
			static_assert(BCClsSize2EncodedClsSize(15u) == 1);

			static_assert(BCEncodedClsSize2ClsSizeConst(0u, 0) == 3);
			static_assert(BCEncodedClsSize2ClsSizeConst(1u, 1) == 4);
			static_assert(BCEncodedClsSize2ClsSizeConst(1u, 2) == 5);
			static_assert(BCEncodedClsSize2ClsSizeConst(2u, 2) == 6);
			static_assert(BCEncodedClsSize2ClsSizeConst(3u, 2) == 7);
			static_assert(BCEncodedClsSize2ClsSizeConst(1u, 3) == 8);
			static_assert(BCEncodedClsSize2ClsSizeConst(2u, 3) == 9);
			static_assert(BCEncodedClsSize2ClsSizeConst(7u, 3) == 14);
			static_assert(BCEncodedClsSize2ClsSizeConst(1u, 4) == 15);
			static_assert(BCEncodedClsSize2ClsSizeConst(2u, 4) == 16);
			static_assert(BCEncodedClsSize2ClsSizeConst(15u, 4) == 29);
			static_assert(BCEncodedClsSize2ClsSizeConst(1u, 5) == 30);
			static_assert(BCEncodedClsSize2ClsSizeConst(2u, 5) == 31);
		}


		// Bits in the parent, remaining for the index into the clause bit-array, after counting hash-index's BitsForHashId bits 
		constexpr static size_t BitsForBCParentClsInd = sizeof(TUInd) * 8 - BitsForHashId;

		// TUInd for the compressed buffer (used, e.g., for parents). Must have the same size as TUInd.
		struct TBCInd
		{
			TBCInd(TUInd val = BadClsInd) : m_Val(val) {}
			TBCInd(const TBCHashId bcHashId, uint64_t bitStart) :
				m_IsLearnt(bcHashId.m_BitsForLit == 0 || bitStart > m_MaxBitStart ? false : bcHashId.m_IsLearnt),
				m_BitsForClsSize(bcHashId.m_BitsForLit == 0 || bitStart > m_MaxBitStart ? 0 : bcHashId.m_BitsForClsSize),
				m_BitsForLit(bcHashId.m_BitsForLit == 0 || bitStart > m_MaxBitStart ? 0 : bcHashId.m_BitsForLit),
				m_BitStart(bcHashId.m_BitsForLit == 0 || bitStart > m_MaxBitStart ? 0 : (TUInd)bitStart)
			{
				assert(m_BitsForClsSize <= m_BitsForLit);
			}

			inline bool IsError() const { return m_Val == BadClsInd; }
			inline operator TUInd() const { return m_Val; }
			inline TBCHashId GetHashId() const { return TBCHashId(m_IsLearnt, m_BitsForClsSize, m_BitsForLit); }
			inline TUInd BitFirstLit() const { return m_BitStart + TBCHashId(m_IsLearnt, m_BitsForClsSize, m_BitsForLit).GetFirstLitOffset(); }
			inline uint8_t GetBitsGlue() const { return TBCHashId(m_IsLearnt, m_BitsForClsSize, m_BitsForLit).GetBitsGlue(); }
			union
			{
				struct
				{
					TUInd m_IsLearnt : BitsForLearnt;
					TUInd m_BitsForClsSize : BitsForClsSize;
					TUInd m_BitsForLit : BitsForLit;
					TUInd m_BitStart : BitsForBCParentClsInd;
				};

				TUInd m_Val = BadClsInd;
			};
			constexpr static uint64_t m_MaxBitStart = ((uint64_t)1 << (uint64_t)BitsForBCParentClsInd) - 1;

		};
		static_assert(sizeof(TBCInd) == sizeof(TUInd));
		
		// Bit-compressed buffers, indexed by TBCHashId
		unordered_map<uint16_t, CBitArray> m_BC;
		// This is to used in order not to invalidate iterators in SimplifyIfRequired
		unordered_map<uint16_t, CBitArray> m_BCSpare;

		size_t BCNextBitSum() const;
		size_t BCCapacitySum() const;

		void BCRemoveGarbage(function<void(uint64_t oldInd, uint64_t newInd)> NotifyAboutRemainingChunkMove = nullptr)
		{
			assert(NV(2) || P("BCRemoveGarbage\n"));
			TUInd toInd(0);
			for (auto bcIt = m_BC.begin(); bcIt != m_BC.end(); toInd == 0 ? bcIt = m_BC.erase(bcIt) : ++bcIt)
			{
				TBCHashId bcHashInd = bcIt->first;
				CBitArray& ba = bcIt->second;

				TUInd fromInd(0);
				toInd = 0;

				while (fromInd < ba.bit_get_next_bit())
				{
					// Update fromInd to the next remaining clause (or to beyond the buffer capacity)
					for (TBCInd bcInd(bcHashInd, fromInd); fromInd < ba.bit_get_next_bit() && 
							((bcHashInd.m_BitsForClsSize != 0 && ba.bit_get(fromInd, bcHashInd.m_BitsForClsSize) == 0) ||
							ba.bit_get(bcInd.BitFirstLit(), bcHashInd.m_BitsForLit) == 0); 
						bcInd = TBCInd(bcHashInd, fromInd))
					{						
						fromInd = ClsEnd(bcInd);
					}

					if (fromInd >= ba.bit_get_next_bit())
					{
						// Nothing useful to move --> we're done!
						break;
					}
					
					// Copy the current clause (starting at fromInd)
					const TBCInd bcInd(bcHashInd, fromInd);					
					const auto clsEndInd = ClsEnd(bcInd);
					const auto bits2Copy = clsEndInd - fromInd;
					if (toInd != fromInd)
					{
						assert(NV(2) || P("Copying the clause: " + SLits(Cls(bcInd)) + " from " + HexStr((TUInd)bcInd) + "," + HexStr(fromInd) + " to " + HexStr(toInd) + ", where " + to_string(bits2Copy) + " bits are copied\n"));

						if (NotifyAboutRemainingChunkMove != nullptr)
						{
							const TBCInd bcToInd(bcHashInd, toInd);
							NotifyAboutRemainingChunkMove(bcInd, bcToInd);
						}

						ba.copy_block(fromInd, toInd, bits2Copy);						
					}

					toInd += bits2Copy;
					fromInd += bits2Copy;
				}

				ba.bit_resize_and_compress(toInd);		
			}
		}

		uint16_t m_LastBCBitArrayInd = 0;
		CBitArray* m_LastBCBitArrayPtr = nullptr;
		inline CBitArray& BCGetBitArray(uint16_t i)
		{
			CApplyFuncOnExitFromScope<> onExit([&]()
			{
				m_LastBCBitArrayInd = i;
				m_LastBCBitArrayPtr = &m_BC.at(i);
			});

			return i == m_LastBCBitArrayInd ? *m_LastBCBitArrayPtr : m_BC.at(i);
		}

		uint16_t m_LastBCBitArrayConstInd = 0;
		CBitArray* m_LastBCBitArrayConstPtr = nullptr;
		inline const CBitArray& BCGetBitArrayConst(uint16_t i) const
		{
			CApplyFuncOnExitFromScope<> onExit([&]()
			{
				const_cast<uint16_t&>(m_LastBCBitArrayConstInd) = i;
			});

			return i == m_LastBCBitArrayInd ? *m_LastBCBitArrayPtr : m_BC.at(i);
		}

		inline uint16_t BCMaxLitWidth(span<TULit> cls) const
		{
			const auto maxElem = *std::max_element(cls.begin(), cls.end());
			// This assertion should hold as long as 5 bits are allocated for m_BitsForLit
			// For having literals of up to 2^64, it should be sufficient to allocate 6 bits for m_BitsForLit, in which case the assert below should be removed			
			assert((uint64_t)maxElem <= (uint64_t)0x100000000);
			return bit_width(maxElem);
		}

		// A compressed clause, which simulates a standard vector with literals, given a bit-array
		// The main difference from the user perspective is that a seemingly by-value access to CCompressedCls is in fact always by reference. 
		// For example, the following code will change c[0] to 1000 (the same code wouldn't modify a standard clause)
		//	  CCompressedCls c
		//	  auto l = c[0]
		//	  l = 1000
		// whereas, the following code won't compile
		//	  CCompressedCls c
		//	  auto& l = c[0]
		// One implication is that it is impossible to design a range-based by-reference for loop for CCls
		// (we're using CCls = conditional_t<Compress, CCompressedCls, CStandardCls>)
		// , since CCompressedCls requires auto l : c, whereas CStandardCls requires auto& l : c
		// Having said all this, much of std functionality is still supported for CCls transparently for the user, including (by example):
		// c[3] = 5
		// l = c[3]
		// auto it = find(im4.begin(), im4.end(), GetLit(16, false));
		// remove_if(cls1.begin(), cls1.end(), IsEq);
		// transform(cls1.begin(), cls1.end(), cls1.begin(), [&](TULit l) { return l + 1; });		
		// for (auto it = cls.begin() + 2; it != cls.end(); ++it)
		// swap(c[0], c[1]) : implementation required some extra-code; see below
		
		// Need "public" to be able to compile swap overrides with gcc as VS "friend" work-arounds (commented out in this version) do not work there
public:
		class CCompressedCls
		{
		public:
			CCompressedCls(CBitArray& ba, TBCInd bcInd) : m_Ba(&ba), m_BCInd(bcInd),
				m_ClsSize(BCEncodedClsSize2ClsSize([&]() { return m_Ba->bit_get(bcInd.m_BitStart, bcInd.m_BitsForClsSize); }, bcInd.m_BitsForClsSize)) {
				assert(m_ClsSize > 0);
			}

			bool Update()
			{
				m_ClsSize = BCEncodedClsSize2ClsSize([&]() { return m_Ba->bit_get(m_BCInd.m_BitStart, m_BCInd.m_BitsForClsSize); }, m_BCInd.m_BitsForClsSize);
				return true;
			}

			class CIteratorRef
			{
			public:
				CIteratorRef(CBitArray* ba, uint64_t startingBit, uint8_t bits2Read) : m_Ba(ba), m_StartingBit(startingBit), m_BitsForLit(bits2Read) { assert(!IsLocal()); }
				CIteratorRef(CIteratorRef&& ccir) : m_Ba(nullptr), m_LocalLit((TULit)ccir), m_BitsForLit(ccir.m_BitsForLit) { assert(IsLocal()); }
				operator TULit() const
				{
					return !IsLocal() ? (TULit)m_Ba->bit_get(m_StartingBit, m_BitsForLit) : m_LocalLit;
				}

				CIteratorRef& operator=(TULit l)
				{
					assert(!IsLocal());
					m_Ba->bit_set(l, m_BitsForLit, m_StartingBit);
					return *this;
				}

				// Move assignment operator
				CIteratorRef& operator =(CIteratorRef&& ccirOther)
				{
					assert(m_BitsForLit == ccirOther.m_BitsForLit);
					*this = (TULit)ccirOther;
					return *this;
				}
			// Needed to remove "protected" to be able to compile swap overrides with gcc as VS "friend" work-arounds (commented out in this version) do not work there
			//protected:
				CBitArray* m_Ba;
				union
				{
					uint64_t m_StartingBit;
					TULit m_LocalLit;
				};
				uint8_t m_BitsForLit;

				inline bool IsLocal() const { return m_Ba == nullptr; }
				
				//friend void swap(CTopi::CCompressedCls::CIteratorRef&& ccir1RValue, CTopi::CCompressedCls::CIteratorRef&& ccir2RValue);
				//friend void swap(const CTopi::CCompressedCls::CIteratorRef& ccir1LValue, CTopi::CCompressedCls::CIteratorRef&& ccir2RValue);
				//friend void swap(CTopi::CCompressedCls::CIteratorRef&& ccir1RValue, const CTopi::CCompressedCls::CIteratorRef& ccir2LValue);
				//friend void swap(const CTopi::CCompressedCls::CIteratorRef& ccir1LValue, const CTopi::CCompressedCls::CIteratorRef& ccir2LValue);
			};

			class CIteratorRefConst
			{
			public:
				CIteratorRefConst(CBitArray* ba, uint64_t startingBit, uint8_t bits2Read) : m_Ba(ba), m_StartingBit(startingBit), m_BitsForLit(bits2Read) {}

				operator TULit() const
				{
					return (TULit)m_Ba->bit_get(m_StartingBit, m_BitsForLit);
				}
			protected:
				CBitArray* m_Ba;
				uint64_t m_StartingBit;
				uint8_t m_BitsForLit;
			};

			class TIteratorConst
			{
			public:
				using iterator_category = random_access_iterator_tag;
				using difference_type = uint64_t;
				using value_type = TULit;
				using reference = CIteratorRefConst;

				TIteratorConst(CBitArray* ba, uint64_t firstLitBit, uint8_t bitsForLit) :
					m_Ba(ba), m_CurrBit(firstLitBit), m_BitsForLit(bitsForLit) {}

				const reference operator*()
				{
					return CIteratorRefConst(m_Ba, m_CurrBit, m_BitsForLit);
				}

				TIteratorConst& operator++()
				{
					m_CurrBit += m_BitsForLit;
					return *this;
				}

				TIteratorConst operator+(size_t i) const
				{
					return TIteratorConst(m_Ba, m_CurrBit + (uint64_t)i * m_BitsForLit, m_BitsForLit);
				}

				difference_type operator-(const TIteratorConst& it) { return (m_CurrBit - it.m_CurrBit) / m_BitsForLit; }
				friend bool operator== (const TIteratorConst& a, const TIteratorConst& b) { return a.m_Ba->get_const_ptr() == b.m_Ba->get_const_ptr() && a.m_CurrBit == b.m_CurrBit && a.m_BitsForLit == b.m_BitsForLit; };
				friend bool operator!= (const TIteratorConst& a, const TIteratorConst& b) { return !(a == b); };

			protected:
				CBitArray* m_Ba;
				uint64_t m_CurrBit;
				uint8_t m_BitsForLit;
			};

			class TIterator
			{
			public:
				using iterator_category = random_access_iterator_tag;
				using difference_type = uint64_t;
				using value_type = TULit;
				using reference = CIteratorRef;

				TIterator(CBitArray* ba, uint64_t firstLitBit, uint8_t bitsForLit) :
					m_Ba(ba), m_CurrBit(firstLitBit), m_BitsForLit(bitsForLit) {}

				reference operator*()
				{
					return CIteratorRef(m_Ba, m_CurrBit, m_BitsForLit);
				}

				TIterator& operator++()
				{
					m_CurrBit += m_BitsForLit;
					return *this;
				}

				TIterator operator+(size_t i) const
				{
					return TIterator(m_Ba, m_CurrBit + (uint64_t)i * m_BitsForLit, m_BitsForLit);
				}

				difference_type operator-(const TIterator& it) const { return (m_CurrBit - it.m_CurrBit) / m_BitsForLit; }
				friend bool operator== (const TIterator& a, const TIterator& b) { return a.m_Ba->get_const_ptr() == b.m_Ba->get_const_ptr() && a.m_CurrBit == b.m_CurrBit && a.m_BitsForLit == b.m_BitsForLit; };
				friend bool operator!= (const TIterator& a, const TIterator& b) { return !(a == b); };			
			protected:
				CBitArray* m_Ba;
				uint64_t m_CurrBit;
				uint8_t m_BitsForLit;
			};

			TIterator begin()
			{
				return TIterator(m_Ba, m_BCInd.BitFirstLit(), m_BCInd.m_BitsForLit);
			}

			TIterator end()
			{
				return TIterator(m_Ba, m_BCInd.BitFirstLit() + m_BCInd.m_BitsForLit * m_ClsSize, m_BCInd.m_BitsForLit);
			}

			TIterator::reference operator[](size_t i)
			{
				return *TIterator(m_Ba, m_BCInd.BitFirstLit() + m_BCInd.m_BitsForLit * i, m_BCInd.m_BitsForLit);
			}

			TIterator::reference back()
			{
				return (*this)[m_ClsSize - 1];
			}

			TIteratorConst begin() const
			{
				return TIteratorConst(m_Ba, m_BCInd.BitFirstLit(), m_BCInd.m_BitsForLit);
			}

			TIteratorConst end() const
			{
				return TIteratorConst(m_Ba, m_BCInd.BitFirstLit() + m_BCInd.m_BitsForLit * m_ClsSize, m_BCInd.m_BitsForLit);
			}

			TIteratorConst::reference operator[](size_t i) const
			{
				return *TIteratorConst(m_Ba, m_BCInd.BitFirstLit() + m_BCInd.m_BitsForLit * i, m_BCInd.m_BitsForLit);
			}

			TIteratorConst::reference back() const
			{
				return (*this)[m_ClsSize - 1];
			}

			inline bool IsLearnt() const { return (bool)m_BCInd.m_IsLearnt; }

			inline TUV BufferGetGlue() const
			{
				assert(IsLearnt());
				return (TUV)m_Ba->bit_get(m_BCInd.m_BitStart + (uint64_t)m_BCInd.m_BitsForClsSize, m_BCInd.GetBitsGlue());
			}

			inline void BufferSetGlue(TUV glue)
			{
				assert(IsLearnt());
				m_Ba->bit_set(glue, m_BCInd.GetBitsGlue(), m_BCInd.m_BitStart + (uint64_t)m_BCInd.m_BitsForClsSize);
			}

			inline bool BufferGetSkipDel() const
			{
				assert(IsLearnt());
				return (bool)m_Ba->bit_get(m_BCInd.m_BitStart + (uint64_t)m_BCInd.m_BitsForClsSize + m_BCInd.GetBitsGlue(), 1);
			}

			inline void BufferSetSkipDel(bool skipDel)
			{
				assert(IsLearnt());
				m_Ba->bit_set(skipDel, 1, m_BCInd.m_BitStart + (uint64_t)m_BCInd.m_BitsForClsSize + m_BCInd.GetBitsGlue());
			}

			inline float BufferGetActivity() const
			{
				assert(IsLearnt());
				uint32_t tmpUint32 = (uint32_t)m_Ba->bit_get(m_BCInd.m_BitStart + (uint64_t)m_BCInd.m_BitsForClsSize + m_BCInd.GetBitsGlue() + 1, 31);
				return *(float*)&tmpUint32;
			}

			inline void BufferSetActivity(float activity)
			{
				assert(IsLearnt());
				m_Ba->bit_set(*((uint32_t*)&activity), 31, m_BCInd.m_BitStart + (uint64_t)m_BCInd.m_BitsForClsSize + m_BCInd.GetBitsGlue() + 1);
			}

			inline size_t size() const { return (size_t)m_ClsSize; }

		protected:
			CBitArray* m_Ba;
			TBCInd m_BCInd;
			TBCInd m_ClsSize;
		};
protected:
		//friend void swap(typename CTopi::CCompressedCls::CIteratorRef&& ccir1RValue, typename CTopi::CCompressedCls::CIteratorRef&& ccir2RValue);
		//friend void swap(typename const CTopi::CCompressedCls::CIteratorRef& ccir1LValue, typename CTopi::CCompressedCls::CIteratorRef&& ccir2RValue);
		//friend void swap(typename CTopi::CCompressedCls::CIteratorRef&& ccir1RValue, typename const CTopi::CCompressedCls::CIteratorRef& ccir2LValue);
		//friend void swap(typename const CTopi::CCompressedCls::CIteratorRef& ccir1LValue, typename const CTopi::CCompressedCls::CIteratorRef& ccir2LValue);

		// *** ConstDenseSnapshot ***
		// Const: I don't modify
		// Snapshot: others don't modify (otherwise, if they do, I'm expected to ignore)
		// Dense: visiting all (up to maxLitsNum, if specified) literals
		// Properties: always holds a TSpanTULit of the clause's literals
		// Notes: in compressed mode, can be implemented by reading all (up to maxLitsNum, if specified) lits upfront into an array (which would then be used for the span)

		class CCompressedClsConstSnapshotDense
		{
		public:
			CCompressedClsConstSnapshotDense(const CBitArray& ba, TBCInd bcInd, TUV maxLitsNum = numeric_limits<TUV>::max())
			{
				const CCompressedCls cc(*const_cast<CBitArray*>(&ba), bcInd);
				
				if (maxLitsNum > cc.size())
				{
					maxLitsNum = (TUV)cc.size();
				}

				m_ClsLits.reserve(maxLitsNum);

				for (TUV i = 0; i < maxLitsNum; ++i)
				{
					m_ClsLits.push_back(cc[i]);
				}			
				m_Span = span(m_ClsLits.begin(), m_ClsLits.end());
#ifdef _DEBUG
				m_ClsInitially.reserve(m_Span.size());
				copy(m_Span.begin(), m_Span.end(), back_inserter(m_ClsInitially));
#endif // DEBUG
			}

			~CCompressedClsConstSnapshotDense()
			{
#ifdef _DEBUG
				assert(equal(m_Span.begin(), m_Span.end(), m_ClsInitially.begin()));
#endif
			}

			inline span<TULit>::iterator begin() const
			{				
				return m_Span.begin();
			}

			inline span<TULit>::iterator end() const
			{
				return m_Span.end();
			}

			inline span<TULit>::iterator::reference operator[](size_t i) const
			{
				return m_Span[i];
			}

			inline span<TULit>::iterator::reference back() const
			{
				return m_Span.back();
			}

			inline size_t size() const
			{
				return m_Span.size();
			}

			//inline operator const span<TULit>() const { return m_Span; }

		protected:
			vector<TULit> m_ClsLits;			
			span<TULit> m_Span;
#ifdef _DEBUG
			vector<TULit> m_ClsInitially;
#endif // DEBUG
		};


		// Adds a compressed clause
		TBCInd BCCompress(span<TULit> cls, bool isLearnt = false, TUV glue = 0, bool stayForAnotherRound = false, float activity = 0.0f, bool insertToAlternativeIfIteratorsBecomeInvalidated = false, bool* spareUsedinCompressed = nullptr);

		// Decompresses the given compressed clause to m_BNext in m_B and also moves b to the next bit after the clause
		void BCDecompress(const TBCHashId currHashId, uint64_t& b);
		// The function below is intended for debugging and validation
		[[maybe_unused]] bool BCIsIdenticalToDecompressed(TUInd clsInd, const TBCHashId currHashId, uint64_t bStart);

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
		CDynArray<TULit> m_B = Compress ? 1 : InitEntriesInB;
		TUInd m_BNext = Compress ? 1 : LitsInPage;
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

		template <bool TCompress = Compress>
		inline void EClsSetIsLearnt(TUInd clsInd, bool isLearnt)
		{
			if constexpr (TCompress)
			{
				assert(0);
				SetStatus(TToporStatus::STATUS_COMPRESSED_MISMATCH, "EClsSetIsLearnt invoked in compressed mode");
			}
			else
			{
				m_B[clsInd] = (m_B[clsInd] & ~ClsIsLearntMask) | (isLearnt << ClsLShiftToIsLearntOn);
				assert(ClsGetIsLearnt(clsInd) == isLearnt);
			}
		}		

		template <bool TCompress = Compress>
		inline bool ClsGetIsLearnt(TUInd clsInd) const
		{
			if constexpr (!TCompress)
			{
				return m_B[clsInd] & ClsIsLearntMask;
			}
			else
			{
				return ((TBCInd)clsInd).m_IsLearnt;
			}
		}

		template <bool TCompress = Compress>
		const int32_t* EClsGetStandardActivityAsConstIntPtr(TUInd clsInd) const
		{
			if constexpr (!TCompress)
			{
				return (const int32_t*)(m_B.get_const_ptr() + clsInd + 1);
			}
			else
			{
				assert(0);
				const_cast<CTopi*>(this)->SetStatus(TToporStatus::STATUS_COMPRESSED_MISMATCH, "EClsGetStandardActivityAsConstIntPtr invoked in compressed mode");
				return nullptr;
			}
		}

		template <bool TCompress = Compress>
		int32_t* EClsGetStandardActivityAsIntPtr(TUInd clsInd)
		{
			if constexpr (!TCompress)
			{
				return (int32_t*)(m_B.get_ptr() + clsInd + 1);
			}
			else
			{
				assert(0);
				const_cast<CTopi*>(this)->SetStatus(TToporStatus::STATUS_COMPRESSED_MISMATCH, "EClsGetStandardActivityAsIntPtr invoked in compressed mode");
				return nullptr;
			}
		}

		bool m_AnyOversized = false;
		template <bool TCompress = Compress>
		inline bool EClsGetIsOversized(TUInd clsInd) const
		{
			if constexpr (!TCompress)
			{
				return m_AnyOversized ? ClsGetIsLearnt(clsInd) && *EClsGetStandardActivityAsConstIntPtr(clsInd) == ClsOversizeActivity : false;
			}
			else
			{
				assert(0);
				const_cast<CTopi*>(this)->SetStatus(TToporStatus::STATUS_COMPRESSED_MISMATCH, "EClsGetIsOversized invoked in compressed mode");
				return false;
			}
		}

		template <bool TCompress = Compress>
		inline bool EClsGetIsOversizedAssumeLearnt(TUInd clsInd) const
		{
			if constexpr (!TCompress)
			{
				assert(ClsGetIsLearnt(clsInd));
				return m_AnyOversized ? *EClsGetStandardActivityAsConstIntPtr(clsInd) == ClsOversizeActivity : false;
			}
			else
			{
				assert(0);
				const_cast<CTopi*>(this)->SetStatus(TToporStatus::STATUS_COMPRESSED_MISMATCH, "EClsGetIsOversizedAssumeLearnt invoked in compressed mode");
				return false;
			}
		}

		template <bool TCompress = Compress>
		inline void ClsSetSize(TUInd clsInd, TUV sz) 
		{ 
			if constexpr (!TCompress)
			{
				assert((sz & ClsIsLearntMask) == 0);
				const auto cleaned = m_B[clsInd] & ~(!ClsGetIsLearnt(clsInd) || sz > ClsLearntMaxSizeWithGlue ? ClsLearntMaxSizeWithoutGlue : ClsLearntMaxSizeWithGlue);
				m_B[clsInd] = cleaned | sz;

				if (unlikely(ClsGetIsLearnt(clsInd) && sz > ClsLearntMaxSizeWithGlue))
				{
					m_AnyOversized = true;
					// Glue cannot be held with this size
					*EClsGetStandardActivityAsIntPtr(clsInd) = ClsOversizeActivity;
				}				
			}
			else
			{
				const TBCInd bcInd(clsInd);
				if (bcInd.m_BitsForClsSize == 0)
				{
					assert(sz == BCMinClsSize);
					return;
				}
				auto& ba = BCGetBitArray(bcInd.GetHashId());
				ba.bit_set(BCClsSize2EncodedClsSize(sz), bcInd.m_BitsForClsSize, bcInd.m_BitStart);
			}
			assert(ClsGetSize(clsInd) == sz);
		}
		
		inline TUV BCGetEncodedClsSize(TUInd clsInd)
		{
			const TBCInd bcInd(clsInd);
			assert(bcInd.m_BitsForClsSize != 0);
			const TBCHashId bcHashInd(bcInd.GetHashId());
			const auto& ba = BCGetBitArray(bcHashInd);
			return (TUV)ba.bit_get(bcInd.m_BitStart, bcInd.m_BitsForClsSize);
		}

		template <bool TCompress = Compress>
		inline TUV ClsGetSize(TUInd clsInd)
		{
			if constexpr (!TCompress)
			{
				if (!ClsGetIsLearnt(clsInd) || EClsGetIsOversizedAssumeLearnt(clsInd))
				{
					return m_B[clsInd] & (~ClsIsLearntMask);
				}
				else
				{
					return m_B[clsInd] & (~ClsIsLearntAndGlueMask);
				}
			}
			else
			{
				const TBCInd bcInd(clsInd);
				if (bcInd.m_BitsForClsSize == 0)
				{
					return BCMinClsSize;
				}
				const TBCHashId bcHashInd(bcInd.GetHashId());
				const auto& ba = BCGetBitArray(bcHashInd);
				return (TUV)(BCEncodedClsSize2ClsSize([&]() { return ba.bit_get(bcInd.m_BitStart, bcInd.m_BitsForClsSize); }, bcInd.m_BitsForClsSize));
			}
		}
		
		template <bool TCompress = Compress>
		inline void ClsSetGlue(TUInd clsInd, TUV glue)
		{
			assert(ClsGetIsLearnt(clsInd));

			if constexpr (!TCompress)
			{
				if (likely(!EClsGetIsOversizedAssumeLearnt(clsInd)))
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
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());

				if (glue > bcHashInd.MaxGlue())
				{
					glue = bcHashInd.MaxGlue();
				}

				auto& ba = BCGetBitArray(bcHashInd);
				ba.bit_set(glue, bcHashInd.GetBitsGlue(), bcInd.m_BitStart + bcInd.m_BitsForClsSize);
			}
		}

		template <bool TCompress = Compress>
		inline TUV ClsGetGlue(TUInd clsInd)
		{
			assert(ClsGetIsLearnt(clsInd));

			if constexpr (!TCompress)
			{
				if (likely(!EClsGetIsOversizedAssumeLearnt(clsInd)))
				{
					return (m_B[clsInd] & ClsGlueMask) >> ClsSizeBits;
				}
				else
				{
					return ClsGetSize(clsInd);
				}
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				const auto& ba = BCGetBitArray(bcHashInd);
				return (TUV)ba.bit_get(bcInd.m_BitStart + bcInd.m_BitsForClsSize, bcHashInd.GetBitsGlue());
			}
		}

		inline TUInd EClsGetActivityAndSkipdelIndex(TUInd clsInd) const
		{
			return clsInd + 1 + EClsGetIsOversizedAssumeLearnt(clsInd);
		}

		template <bool TCompress = Compress>
		inline TUInd ClsGetActivityAndSkipdel(TUInd clsInd)
		{
			if constexpr (!TCompress)
			{
				return m_B[EClsGetActivityAndSkipdelIndex(clsInd)];
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				const auto& ba = BCGetBitArray(bcHashInd);
				return (TUInd)ba.bit_get(bcInd.m_BitStart + bcInd.m_BitsForClsSize + bcHashInd.GetBitsGlue(), TBCHashId::GetBitsActivityAndSkipDel());
			}
		}

		template <bool TCompress = Compress>
		inline void ClsSetActivityAndSkipdelTo0(TUInd clsInd)
		{
			assert(ClsGetIsLearnt(clsInd));

			if constexpr (!TCompress)
			{
				m_B[EClsGetActivityAndSkipdelIndex(clsInd)] = 0;
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				auto& ba = BCGetBitArray(bcHashInd);
				ba.bit_set(0, TBCHashId::GetBitsActivityAndSkipDel(), bcInd.m_BitStart + bcInd.m_BitsForClsSize + bcHashInd.GetBitsGlue());
			}
		}

		template <bool TCompress = Compress>
		inline bool ClsGetSkipdel(TUInd clsInd)
		{
			if constexpr (!TCompress)
			{
				return *(uint32_t*)(m_B.get_const_ptr() + EClsGetActivityAndSkipdelIndex(clsInd)) & ClsSkipdelMask;
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				const auto& ba = BCGetBitArray(bcHashInd);
				return (bool)ba.bit_get(bcInd.m_BitStart + bcInd.m_BitsForClsSize + bcHashInd.GetBitsGlue(), TBCHashId::GetBitsSkipDel());
			}
		}

		template <bool TCompress = Compress>
		inline void ClsSetActivity(TUInd clsInd, float a)
		{
			assert(a >= 0);
			if constexpr (!TCompress)
			{
				const auto prevSkipDel = ClsGetSkipdel(clsInd);
				const float updatedA = prevSkipDel ? -a : a;
				*(float*)(m_B.get_ptr() + EClsGetActivityAndSkipdelIndex(clsInd)) = updatedA;
				assert(prevSkipDel == ClsGetSkipdel(clsInd));
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				auto& ba = BCGetBitArray(bcHashInd);
				ba.bit_set(*(uint32_t*)&a, TBCHashId::GetBitsActivity(), bcInd.m_BitStart + bcInd.m_BitsForClsSize + bcHashInd.GetBitsGlue() + TBCHashId::GetBitsSkipDel());
			}
			[[maybe_unused]] const auto currAct = ClsGetActivity(clsInd);
			assert(currAct == a);
		}

		template <bool TCompress = Compress>
		inline float ClsGetActivity(TUInd clsInd)
		{
			static_assert(sizeof(float) == sizeof(uint32_t));

			if constexpr (!TCompress)
			{
				return fabs(*(float*)(m_B.get_const_ptr() + EClsGetActivityAndSkipdelIndex(clsInd)));
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				const auto& ba = BCGetBitArray(bcHashInd);
				TUInd asd = (TUInd)ba.bit_get(bcInd.m_BitStart + bcInd.m_BitsForClsSize + bcHashInd.GetBitsGlue() + TBCHashId::GetBitsSkipDel(), TBCHashId::GetBitsActivity());
				return *(float*)(&asd);
			}
		}

		template <bool TCompress = Compress>
		inline void ClsSetSkipdel(TUInd clsInd, bool skipDel)
		{
			if constexpr (!TCompress)
			{
				[[maybe_unused]] const auto prevAct = ClsGetActivity(clsInd);
				*(uint32_t*)(m_B.get_ptr() + EClsGetActivityAndSkipdelIndex(clsInd)) = (*(uint32_t*)(m_B.get_ptr() + EClsGetActivityAndSkipdelIndex(clsInd)) &
					ClsNotSkipdelMask) | ((uint32_t)skipDel << ClsLShiftSkipDel);
				assert(prevAct == ClsGetActivity(clsInd));
			}
			else
			{
				const TBCInd bcInd(clsInd);
				const TBCHashId bcHashInd(bcInd.GetHashId());
				auto& ba = BCGetBitArray(bcHashInd);
				ba.bit_set(skipDel, TBCHashId::GetBitsSkipDel(), bcInd.m_BitStart + bcInd.m_BitsForClsSize + bcHashInd.GetBitsGlue());
				assert(ClsGetSkipdel(clsInd) == skipDel);
			}
		}

		inline TUV EClsLitsStartOffset(bool isLearnt, bool isOversized) const
		{
			if constexpr (Compress)
			{
				assert(0);
				const_cast<CTopi*>(this)->SetStatus(TToporStatus::STATUS_COMPRESSED_MISMATCH, "EClsLitsStartOffset invoked in compressed mode");
				return false;
			}
			else
			{
				return (TUV)1 + ((TUV)isLearnt << ClsActivityFieldsLshift) + ((TUV)isOversized << ClsActivityFieldsLshift);
			}
		}

		// CClsBase: basic functionality of a class
		// TOversized is expected to be either const bool or bool&, depending on the deriving class
		template <class TBuffer, class TOversized>
		class CClsBase
		{
		public:
			CClsBase(TBuffer& b, TOversized anyOversized, TUInd clsInd) : m_B(b), m_ClsInd(clsInd), m_AnyOversized(anyOversized) {}			
			inline bool GetIsLearnt() const
			{
				return m_B[m_ClsInd] & ClsIsLearntMask;
			}
		protected:
			TBuffer& m_B;
			const TUInd m_ClsInd;
			TOversized m_AnyOversized;
			
			const int32_t* GetStandardActivityAsConstIntPtr() const
			{
				return (const int32_t*)(m_B.get_const_ptr() + m_ClsInd + 1);
			}

			inline TUV LitsStartOffset(bool isLearnt, bool isOversized) const
			{
				return (TUV)1 + ((TUV)isLearnt << ClsActivityFieldsLshift) + ((TUV)isOversized << ClsActivityFieldsLshift);
			}

			inline bool GetIsOversized(bool islearnt) const
			{
				return m_AnyOversized && islearnt && *GetStandardActivityAsConstIntPtr() == ClsOversizeActivity;
			}

			inline TUV GetSize(bool isLearnt, bool isOversized) const
			{
				if (!isLearnt || isOversized)
				{
					return m_B[m_ClsInd] & (~ClsIsLearntMask);
				}
				else
				{
					return m_B[m_ClsInd] & (~ClsIsLearntAndGlueMask);
				}
			}
		};

		// Basic functionality of a class: no writing 
		using CConstClsBase = CClsBase<const CDynArray<TULit>, const bool>;
		// Basic functionality of a class: writing permitted
		using CMutableClsBase = CClsBase<CDynArray<TULit>, bool&>;

		// *** ConstDenseSnapshot ***
		// Const: I don't modify
		// Snapshot: others don't modify (otherwise, if they do, I'm expected to ignore)
		// Dense: visiting all (up to maxLitsNum, if specified) literals
		// Properties: always holds a TSpanTULit of the clause's literals
		// Notes: in compressed mode, can be implemented by reading all (up to maxLitsNum, if specified) lits upfront into an array (which would then be used for the span)

		class CStandardClsConstSnapshotDense : public CConstClsBase
		{
		public:
			CStandardClsConstSnapshotDense(const CDynArray<TULit>& b, const bool anyOversized, TUInd clsInd, TUV maxLitsNum = numeric_limits<TUV>::max()) : CConstClsBase(b, anyOversized, clsInd),
				m_IsLearnt(this->GetIsLearnt()), m_IsOversized(this->GetIsOversized(m_IsLearnt)),
				m_LitsStartOffset(this->LitsStartOffset(m_IsLearnt, m_IsOversized)),
				m_Span(this->m_B.get_const_span_cap(clsInd + m_LitsStartOffset, std::min(this->GetSize(m_IsLearnt, m_IsOversized), maxLitsNum)))
			{
#ifdef _DEBUG
				m_ClsInitially.reserve(m_Span.size());
				copy(m_Span.begin(), m_Span.end(), back_inserter(m_ClsInitially));
#endif // DEBUG
			}
			~CStandardClsConstSnapshotDense()
			{
#ifdef _DEBUG
				assert(equal(m_Span.begin(), m_Span.end(), m_ClsInitially.begin()));
#endif
			}

			inline span<TULit>::iterator begin() const
			{
				return m_Span.begin();
			}

			inline span<TULit>::iterator end() const
			{
				return m_Span.end();
			}

			inline span<TULit>::iterator::reference operator[](size_t i) const
			{
				return m_Span[i];
			}

			inline span<TULit>::iterator::reference back() const
			{
				return m_Span.back();
			}

			inline size_t size() const 
			{
				return m_Span.size();
			}

			inline operator span<TULit>() const { return m_Span; }			
		protected:					
			const bool m_IsLearnt;
			const bool m_IsOversized;
			const TUV m_LitsStartOffset;
			span<TULit> m_Span;
#ifdef _DEBUG
			vector<TULit> m_ClsInitially;
#endif // DEBUG
		};

		// Custom standard clause
		class CStandardCls : public CMutableClsBase
		{
		public:
			CStandardCls(CDynArray<TULit>& b, bool& anyOversized, TUInd clsInd) : CMutableClsBase(b, anyOversized, clsInd)
			{
				Update();
			}				

			using TIterator = TSpanTULit::iterator;

			inline TIterator begin()
			{
				return m_Span.begin();
			}

			inline TIterator end()
			{
				return m_Span.end();
			}

			inline TIterator::reference operator[](size_t i)
			{
				return m_Span[i];
			}

			inline TIterator::reference back()
			{
				return m_Span.back();
			}

			inline size_t size()
			{
				return m_Span.size();
			}

			bool Update()
			{
				const auto isLearnt = this->GetIsLearnt();
				const auto isOversized = this->GetIsOversized(isLearnt);
				const auto litsStartOffset = this->LitsStartOffset(isLearnt, isOversized);
				const auto clsSize = this->GetSize(isLearnt, isOversized);
				m_Span = this->m_B.get_span_cap(this->m_ClsInd + litsStartOffset, clsSize);
				return true;
			}
		protected:
			TSpanTULit m_Span;
		};
		
		using CCls = conditional_t<Compress, CCompressedCls, CStandardCls>;
		using CClsConstSnapshotDense = conditional_t<Compress, CCompressedClsConstSnapshotDense, CStandardClsConstSnapshotDense>;		

		// This is the limit on the number of ConstClsSpan's to be used simultaneously in the same context
		// The current limit is 3. This is crucial to remember for the developers.
		static constexpr unsigned m_TmpClssCountMax = 3;
		array<CDynArray<TULit>, m_TmpClssCountMax> m_TmpClss;
		uint8_t m_CurrTmpClssCount = 0;
		array<CDynArray<TULit>, m_TmpClssCountMax> m_TmpClssDebug;
		uint8_t m_CurrTmpClssCountDebug = 0;
		
		inline const span<TULit> ConstClsSpan(TUInd clsInd, TUV maxSpanElems = numeric_limits<TUV>::max())
		{
			return ConstClsSpanInternal(clsInd, maxSpanElems, m_TmpClss, m_CurrTmpClssCount);
		}

		inline const span<TULit> ConstClsSpanDebug(TUInd clsInd, TUV maxSpanElems = numeric_limits<TUV>::max())
		{
			return ConstClsSpanInternal(clsInd, maxSpanElems, m_TmpClssDebug, m_CurrTmpClssCountDebug);
		}

		inline const span<TULit> ConstClsSpanInternal(TUInd clsInd, TUV maxSpanElems, array<CDynArray<TULit>, m_TmpClssCountMax>& tmpClss, uint8_t& tmpClssCount)
		{
			if constexpr (Standard)
			{
				auto sz = ClsGetSize(clsInd);
				if (maxSpanElems > sz)
				{
					maxSpanElems = sz;
				}
				const auto litsStartOffset = EClsLitsStartOffset(ClsGetIsLearnt(clsInd), EClsGetIsOversized(clsInd));
				return span(m_B.get_ptr(clsInd + litsStartOffset), maxSpanElems);
			}
			else
			{
				if (clsInd == 0)
				{
					return span(m_B.get_ptr(), maxSpanElems);
				}
				const TBCInd bcInd = clsInd;
				const TBCHashId bcHashInd = bcInd.GetHashId();

				const CCompressedCls cc(BCGetBitArray(bcHashInd), bcInd);

				if (maxSpanElems > cc.size())
				{
					maxSpanElems = (TUV)cc.size();
				}

				tmpClss[tmpClssCount].reserve_exactly(maxSpanElems);
				for (TUV i = 0; i < maxSpanElems; ++i)
				{
					tmpClss[tmpClssCount][i] = cc[i];
				}

				auto lswoc = span(&tmpClss[tmpClssCount][0], maxSpanElems);
				++tmpClssCount;
				if (tmpClssCount >= m_TmpClssCountMax)
				{
					tmpClssCount = 0;
				}
				return lswoc;
			}
		}

		inline CCls Cls(TUInd clsInd)
		{ 
			if constexpr (Standard)
			{
				return CStandardCls(m_B, m_AnyOversized, clsInd);
			}
			else
			{
				const TBCInd bcInd = clsInd;
				const TBCHashId bcHashInd = bcInd.GetHashId();
				return CCompressedCls(BCGetBitArray(bcHashInd), bcInd);
			}			
		}

		// EXPLICIT (NON-COMPRESSED) CLAUSE BUFFER EXPLANATION:
		// To delete an initial clause [n l1 ... ln] we just put 0 instead of l2
		// To delete a learnt clause [{1<<ClsLShiftToMSB | n} Glue l1 ... ln] we replace the first entry by n+1 and put 0 instead of l2
		// This way, any deleted chunks > 1 is [n l1 0 ... ln], so one can walk clauses and deleted chunks in a similar manner, 
		// e.g., ClsEnd(clsInd) works both when clsInd is a clause index and when it's a deleted chunk index
		// Any entry of size < 4 is considered to be deleted, including [0]
		// We also, optionally place newBinCls[0] at clsStart + 1 and newBinCls[1] at clsStart + 3. 
		//		This trick is used to replace long parents with binary parents for assumption decision levels
		// COMPRESSED CLAUSE BUFFER EXPLANATION:
		// To delete, mark the first literal as 0
		// To store a binary clause, replace literals 1 and 2 with it.

		template <bool TCompress = Compress>
		inline bool ClsChunkDeleted(TUInd clsInd)
		{
			if constexpr (!TCompress)
			{
				if (ClsGetIsLearnt(clsInd))
				{
					return false;
				}
				const auto sz = ClsGetSize(clsInd);
				return sz < 3 || m_B[clsInd + 2] == BadULit;
			}
			else
			{
				const TBCInd bcInd = clsInd;
				const auto& ba = BCGetBitArrayConst(bcInd.GetHashId());
				if (bcInd.m_BitsForClsSize == 0)
				{
					return ba.bit_get(bcInd.BitFirstLit(), bcInd.m_BitsForLit) == 0;
				}
				
				return ba.bit_get(bcInd.m_BitStart, bcInd.m_BitsForClsSize) == 0 || ba.bit_get(bcInd.BitFirstLit(), bcInd.m_BitsForLit) == 0;
			}
		}
		
		template <bool TCompress = Compress>
		void DeleteCls(TUInd clsInd, array<TULit, 2>* newBinCls = nullptr)
		{
			auto cls = Cls(clsInd);

			if constexpr (!TCompress)
			{
				if (unlikely(clsInd == m_FirstLearntClsInd))
				{					
					m_FirstLearntClsInd = ClsEnd(clsInd);									
					while (m_FirstLearntClsInd < m_BNext && ClsChunkDeleted(m_FirstLearntClsInd))
					{
						m_FirstLearntClsInd = ClsEnd(m_FirstLearntClsInd);
					}
					
					// Thought the following code a bug fix, but it only deteriorated the performance, so I brought back the old code
					// The code is not really necessary fopr correctness, since the solver never assumes m_FirstLearntClsInd *must* be learnt
					// Also, there is a loop like that after DeleteCls calls in DeleteClausesIfRequired, but not in the other DeleteCls instances
					/*while (m_FirstLearntClsInd < m_BNext && (ClsChunkDeleted(m_FirstLearntClsInd) || !ClsGetIsLearnt(m_FirstLearntClsInd)))
					{
						m_FirstLearntClsInd = ClsEnd(m_FirstLearntClsInd);
					}
					assert(m_FirstLearntClsInd == m_BNext || ClsGetIsLearnt(m_FirstLearntClsInd));*/
				}

				m_BWasted += (TUInd)cls.size() + EClsLitsStartOffset(ClsGetIsLearnt(clsInd), EClsGetIsOversized(clsInd));
			}
			else
			{
				const TBCInd bcInd = clsInd;
				const TBCHashId bcHashInd = bcInd.GetHashId();
				m_BWasted += bcHashInd.GetFirstLitOffset() + bcHashInd.m_BitsForLit * ClsGetSize(clsInd);
			}

			m_Stat.DeleteClause(cls.size(), ClsGetIsLearnt(clsInd));
			for (uint8_t currWatchI = 0; currWatchI <= 1; ++currWatchI)
			{
				size_t clsWlInd = WLGetLongWatchInd(cls[currWatchI], clsInd);
				assert(clsWlInd != numeric_limits<size_t>::max());
				WLRemoveLongWatch(cls[currWatchI], clsWlInd);
			}

			if constexpr (!TCompress)
			{
				if (ClsGetIsLearnt(clsInd))
				{
					m_B[clsInd] = (TULit)cls.size() + (TULit)EClsLitsStartOffset(ClsGetIsLearnt(clsInd), EClsGetIsOversized(clsInd)) - (TULit)1;
				}
				assert(!ClsGetIsLearnt(clsInd));

				m_B[clsInd + 2] = BadULit;
				if (newBinCls != nullptr)
				{
					m_B[clsInd + 1] = (*newBinCls)[0];
					m_B[clsInd + 3] = (*newBinCls)[1];
				}
			}
			else
			{
				const TBCInd bcInd = clsInd;
				const TBCHashId bcHashInd = bcInd.GetHashId();
				BCGetBitArray(bcHashInd).bit_set(0, bcInd.m_BitsForLit, bcInd.BitFirstLit());
				if (newBinCls != nullptr)
				{
					BCGetBitArray(bcHashInd).bit_set((*newBinCls)[0], bcInd.m_BitsForLit, bcInd.BitFirstLit() + bcInd.m_BitsForLit);
					BCGetBitArray(bcHashInd).bit_set((*newBinCls)[1], bcInd.m_BitsForLit, bcInd.BitFirstLit() + bcInd.m_BitsForLit + bcInd.m_BitsForLit);					
				}
			}
		}

		// Delete a binary clause
		void DeleteBinaryCls(const span<TULit> binCls);
		
		constexpr bool BCDeleteLitsCausesMoveToAnotherBuffer(TUV origSize, TUV lits2Remove)
		{
			assert(origSize > 3);
			assert(lits2Remove < origSize);
			assert(origSize - lits2Remove >= 3);
			return BCClsSize2Bits(origSize) != BCClsSize2Bits(origSize - lits2Remove);
		}

		// <Deletion-handled, spare-buffer-used> 
		pair<bool, bool> BCDeleteLitsByMovingToOtherBufferIfRequiredAssumingLastDeleted(TUInd& clsInd, TUV origSize, TUV lits2Remove, bool insertToSpareIfIteratorsBecomeInvalidated = false);
		
		template <bool TCompress = Compress>
		void DeleteLitFromCls(TUInd& clsInd, TULit l)
		{
			auto cls = Cls(clsInd);
			assert(ClsGetSize(clsInd) > 3);

			auto it = find(cls.begin(), cls.end(), l);
			assert(it != cls.end());

			if (it - cls.begin() < 2)
			{
				const bool myWatchInd = it != cls.begin();
				auto bestWLCandIt = FindBestWLCand(cls, m_DecLevel);
				SwapWatch(clsInd, myWatchInd, bestWLCandIt);
				WLSetCached(cls[!myWatchInd], clsInd, cls[myWatchInd]);
				swap(*bestWLCandIt, cls.back());
			}
			else
			{
				swap(*it, cls.back());
				// Cached update is required, since the cache might point at the deleted literal
				WLSetCached(cls[0], clsInd, cls[1]);
				WLSetCached(cls[1], clsInd, cls[0]);
			}

			if constexpr (!Compress)
			{
				TUInd nextChunkInd = ClsEnd(clsInd);
				if (nextChunkInd < m_BNext && ClsChunkDeleted(nextChunkInd))
				{
					// Extending the deleted chunk
					const auto newSz = ClsGetSize(nextChunkInd) + 1;
					m_B[--nextChunkInd] = newSz;
					assert(cls.back() == newSz);
					if (newSz >= 3)
					{
						m_B[nextChunkInd + 2] = BadULit;
					}
					assert(ClsChunkDeleted(nextChunkInd));
					assert(ClsGetSize(nextChunkInd) == newSz);					
				}
				else
				{
					cls.back() = BadULit;					
				}
				// Resizing our clause	
				ClsSetSize(clsInd, (TUV)cls.size() - 1);
				RecordDeletedLitsFromCls(1);
			}
			else
			{
				auto [deletionHandled, ignore] = BCDeleteLitsByMovingToOtherBufferIfRequiredAssumingLastDeleted(clsInd, (TUV)cls.size(), 1);
				if (!deletionHandled)
				{
					cls.back() = BadULit;
					// Resizing our clause	
					ClsSetSize(clsInd, (TUV)cls.size() - 1);
				}				
				RecordDeletedLitsFromCls(1, ((TBCInd)clsInd).GetHashId().m_BitsForLit);
			}			

			if (IsCbLearntOrDrat())
			{
				auto cls = ConstClsSpan(clsInd);
				NewLearntClsApplyCbLearntDrat(cls);
			}
		}

		void RecordDeletedLitsFromCls(TUV litsNum, uint16_t bitsForLit = 0);
		// Returns clause index for long clauses
		TUInd AddClsToBufferAndWatch(const TSpanTULit cls, bool isLearntNotForDeletion, bool isPartOfProof);
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

			inline bool IsEmpty() const { assert(m_WBInd != 0 || (m_AllocatedEntries == 0 && m_BinaryWatches == 0 && m_LongWatches == 0)); return m_AllocatedEntries == 0; }

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
		// Add a long watch
		void WLAddLongWatch(TULit l, TULit inlinedLit, TUInd clsInd = BadClsInd);
		// Remove a long watch
		void WLRemoveLongWatch(TULit l, size_t longWatchInd);
		// Find the index of the given clause in l's watch
		size_t WLGetLongWatchInd(TULit l, TUInd clsInd);
		// Set the cached literal of l's watch to clsInd to 
		void WLSetCached(TULit l, TUInd clsInd, TULit cachedLit);
		// Replace clsInd by newClsInd (not changing the cached literal, though)
		void WLReplaceInd(TULit l, TUInd clsInd, TUInd newClsInd);
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
		TUInd m_WWasted = 0;

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
		inline TUInd LastWLEntry(TULit l) { return m_Watches[l].m_WBInd + m_Watches[l].GetLongEntries() - LitsInInd; }


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

		inline span<TULit> CiGetSpan(TContradictionInfo& ci, TUV maxSpanElems = numeric_limits<TUV>::max())
		{ 
			if (ci.m_IsContradictionInBinaryCls)
			{
				if (maxSpanElems >= 2)
				{
					return span(ci.m_BinClause);
				}
				else
				{
					return span(ci.m_BinClause).subspan(0, maxSpanElems);
				}
			}
			else
			{
				return ConstClsSpan(ci.m_ParentClsInd, maxSpanElems);
			}			
		}

		inline span<TULit> CiGetSpanDebug(TContradictionInfo& ci, TUV maxSpanElems = numeric_limits<TUV>::max())
		{
			if (ci.m_IsContradictionInBinaryCls)
			{
				return CiGetSpan(ci, maxSpanElems);
			}
			else
			{
				return ConstClsSpanDebug(ci.m_ParentClsInd, maxSpanElems);
			}
		}

		inline size_t CiGetSize(const TContradictionInfo& ci)
		{
			if (ci.m_IsContradictionInBinaryCls)
			{
				return 2;
			}
			else
			{
				return ClsGetSize(ci.m_ParentClsInd);
			}
		}

		inline string CiString(TContradictionInfo& ci)
		{ 
			return !ci.m_IsContradiction ? "" : ci.m_IsContradictionInBinaryCls ? SLits(span(ci.m_BinClause)) : SLits(CiGetSpanDebug(ci));
		}

		inline string CisString(span<TContradictionInfo> cis)
		{ 
			string res; 
			for (auto& ci : cis)
			{
				res = res + CiString(ci) + "; "; 				
			}
			return res;
		}

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
			// Reserved
			uint8_t m_Reserved : 1;
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
		bool m_UpdateParamsWhenVarFixedDone = false;
		uint32_t m_NonForcedPolaritySelectionForFlip = 0;
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

		inline bool IsGloballyAssigned(TULit l) const
		{
			return GetAssignedDecLevel(l) == 0;
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

		inline TUV GetDecLevel0ForUnassigned(TULit l) const
		{
			return IsAssigned(l) ? m_VarInfo[GetVar(l)].m_DecLevel : 0;
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
		inline const span<TULit> GetAssignedNonDecParentSpan(TULit l)
		{
			return GetAssignedNonDecParentSpanVar(GetVar(l));
		}
		inline const span<TULit> GetAssignedNonDecParentSpanVar(TUVar v)
		{
			assert(IsAssignedVar(v) && !IsAssignedDecVar(v));
			return GetAssignedNonDecParentSpanVI(m_AssignmentInfo[v], m_VarInfo[v]);
		}
		inline const span<TULit> GetAssignedNonDecParentSpanVI(TAssignmentInfo& ai, TVarInfo& vi)
		{			
			if (ai.IsAssignedBinary())
			{
				return span(&vi.m_BinOtherLit, 1);
			}
			else
			{
				return ConstClsSpan(vi.m_ParentClsInd);
			}			
		}

		inline bool IsParentLongInitial(TAssignmentInfo& ai, TVarInfo& vi)
		{
			return !ai.IsAssignedBinary() && vi.m_ParentClsInd != BadClsInd && !ClsGetIsLearnt(vi.m_ParentClsInd);
		}
		
		// Returns true iff the assignment is contradictory
		bool Assign(TULit l, TUInd parentClsInd, TULit otherWatch, TUV decLevel, bool toPropagate = true, bool externalAssignment = false);
		void Unassign(TULit l);
		void UnassignVar(TUVar v);
		template <class TULitSpan>
		inline auto AdditionalAssign(TULitSpan acSpan, TUInd acInd)
		{
			if (acSpan.size() > 1 && IsAssigned(acSpan[0]) && !IsAssigned(acSpan[1]))
			{
				swap(acSpan[0], acSpan[1]);
			}

			if (acSpan.size() > 0 && !IsAssigned(acSpan[0]) && all_of(acSpan.begin() + 1, acSpan.end(), [&](TULit l) { return IsFalsified(l); }))
			{
				++m_Stat.m_FlippedClausesUnit;
				Assign(acSpan[0], acSpan.size() >= 2 ? acInd : BadClsInd, acSpan.size() == 1 ? BadULit : acSpan[1], acSpan.size() == 1 ? 0 : GetAssignedDecLevel(acSpan[1]));
			}
		}
		[[maybe_unused]] bool DebugLongImplicationInvariantHolds(TULit l, TUV decLevel, TUInd parentClsIndIfLong);
		
		// TULitSpan can be: (1) const span<TULit>, (2) TSpanTULit
		template <class TULitSpan>
		inline auto GetAssignedLitsHighestDecLevelIt(TULitSpan lits, size_t startInd) const
		{ 
			return max_element(lits.begin() + startInd, lits.end(), [&](TULit l) { return GetAssignedDecLevel(l); });
		}

		// TULitSpan can be: (1) const span<TULit>, (2) TSpanTULit
		template <class TULitSpan>
		inline auto GetLitsHighestDecLevel0ForUnassignedIt(TULitSpan lits, size_t startInd) const
		{
			return max_element(lits.begin() + startInd, lits.end(), [&](TULit l) { return IsAssigned(l) ? GetAssignedDecLevel(l) : 0; });
		}
		
		template <class TULitSpan>
		inline auto GetAssignedLitsLowestDecLevelIt(TULitSpan lits, size_t startInd) const
		{ 
			return min_element(lits.begin() + startInd, lits.end(), [&](TULit l) { return GetAssignedDecLevel(l); }); 
		}	

		template <class TULitSpan>
		inline auto GetSatisfiedLitLowestDecLevelIt(TULitSpan lits, size_t startInd = 0) const
		{ 
			return std::min_element(lits.begin() + startInd, lits.end(), [&](TULit l1, TULit l2) 
			{ 
				return (IsSatisfied(l1) && !IsSatisfied(l2)) || (IsSatisfied(l1) && IsSatisfied(l2) && GetAssignedDecLevel(l1) < GetAssignedDecLevel(l2)); 
			}); 
		}

		// Literal currently propagated by BCP
		TULit m_CurrentlyPropagatedLit = BadULit;
		// Contradictions stashed during BCP
		CVector<TContradictionInfo> m_Cis;
		// Run BCP
		TContradictionInfo BCP();
		// Backtracking during BCP
		void BCPBacktrack(TUV decLevel, bool eraseDecLevel);
		CCls::TIterator FindBestWLCand(CCls& cls, TUV maxDecLevel);

		// Swap watch watchInd in the clause with newWatchIt
		void SwapWatch(const TUInd clsInd, bool watchInd, CCls::TIterator newWatchIt);
		// Swap the current watch while traversing a long WL
		void SwapCurrWatch(TULit l, CCls::TIterator newWatchIt, const TUInd clsInd, CCls& cls, size_t& currLongWatchInd, TULit*& currLongWatchPtr, TWatchInfo& wi);

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
		
		bool TrailAssertConsistency();

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
		// This is an auxiliary function to be called if BCP accidentally backtracked beyond the assumptions
		void HandleAssumptionsIfBacktrackedBeyondThem();
		// Find first unassigned assumption
		size_t FindFirstUnassignedAssumpIndex(size_t indexBeyondHandledAssumps);
		// Assign assumptions starting from firstUnassignedAssumpInd
		void AssignAssumptions(size_t firstUnassignedAssumpInd);

		inline bool IsAssump(TULit l) const { return IsAssumpVar(GetVar(l)); }
		inline bool IsAssumpVar(TUVar v) const { return m_AssignmentInfo[v].m_IsAssump; }
		inline bool IsAssumpFalsifiedGivenVar(TUVar v) const { assert(IsAssumpVar(v)); return IsFalsified(GetLit(v, false)) != m_AssignmentInfo[v].m_IsAssumpNegated; }
		inline TULit GetAssumpLitForVar(TUVar v) const { assert(IsAssumpVar(v)); return GetLit(v, m_AssignmentInfo[v].m_IsAssumpNegated); }
		inline bool IsSatisfiedAssump(TUVar v) const { return IsAssumpVar(v) && IsSatisfied(GetAssumpLitForVar(v)); }		

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
		vector<TLit> m_UserAssumps;
		// The latest invocation of Solve, after which an assumption UNSAT core was requested and extracted
		uint64_t m_LatestAssumpUnsatCoreSolveInvocation = numeric_limits<uint64_t>::max();
		inline void AssumpUnsatCoreCleanUpIfRequired() { if (m_Stat.m_SolveInvs == m_LatestAssumpUnsatCoreSolveInvocation) { CleanVisited(); } }

		/*
		* Statistics
		*/

		TToporStatistics<TLit, TUInd> m_Stat;

		/*
		* Decision Strategy
		*/

		CVarScores<TUVar, TUV> m_VsidsHeap;

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
		uint8_t m_CurrCustomBtStrat = 0;
		uint64_t m_ConfsSinceNewInv = 0;

		/*
		* Conflict Analysis and Learning
		*/
		
		void ConflictAnalysisLoop(TContradictionInfo& contradictionInfo);

		// Return: (1) asserting clause span; (2) Clause index (for long clauses)		
		pair<TSpanTULit, TUInd> LearnAndUpdateHeuristics(TContradictionInfo& contradictionInfo, CVector<TULit>& clsBeforeAllUipOrEmptyIfAllUipFailed);
		// Return a flipped clause if: 
		// (1) Flipped recording is on; (2) The current level is flipped; (3) The flipped clause is not subsumed by the main clause
		// The return span is empty iff the recording is unsuccessful (in which case TUInd is BadParentClsInd too)
		pair<TSpanTULit, TUInd> RecordFlipped(TContradictionInfo& contradictionInfo, TSpanTULit mainClsBeforeAllUip);
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
		void OnBadDratFile();
		void NewLearntClsApplyCbLearntDrat(const span<TULit> learntCls);

		struct TParentSubsumed
		{
			TParentSubsumed(TULit l, bool isBinary, TUInd parentClsInd) : m_L(l), m_IsBinary(isBinary), m_ParentClsInd(parentClsInd) {}
			TULit m_L;
			bool m_IsBinary;
			union
			{
				// Parent clause for longs
				TUInd m_ParentClsInd;
				// The other literal in the clause for binaries
				TULit m_BinOtherLit;
			};
		};
		vector<TParentSubsumed> m_VarsParentSubsumed;
		
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
		template <class TULitSpan>
		TUV GetGlueAndMarkCurrDecLevels(TULitSpan cls)
		{
			++m_MarkedDecLevelsCounter;
			if (unlikely(m_MarkedDecLevelsCounter < 0))
			{
				m_DecLevelsLastAppearenceCounter.memset(0);
				m_MarkedDecLevelsCounter = 1;
			}

			TUV decLevels = 0;
			for (auto l : cls)
			{
				if (m_ParamAssumpsIgnoreInGlue && IsAssump(l))
				{
					continue;
				}
				assert(IsAssigned(l));
				const TUV decLevel = GetAssignedDecLevel(l);
				if (m_DecLevelsLastAppearenceCounter[decLevel] != m_MarkedDecLevelsCounter)
				{
					++decLevels;
					m_DecLevelsLastAppearenceCounter[decLevel] = m_MarkedDecLevelsCounter;
				}
			}

			return decLevels;
		}

		uint64_t m_HugeCounterDecLevels = 0;
		CDynArray<uint64_t> m_HugeCounterPerDecLevel;
		// ret.first: for every decision level m_HugeCounterPerDecLevel[decLevel] - ret.first is the number of literals in cls with that decision level
		// ret.second: all the decision levels in cls in a priority queue (top will return the greatest first)		
		pair<uint64_t, priority_queue<TUV>> GetDecLevelsAndMarkInHugeCounter(TSpanTULit cls);
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
		
		TWinAverage<TUV> m_RstGlueLbdWin;
		double m_RstGlueGlobalLbdSum = 0;
		void RstNewAssertingGluedCls(TUV glue);

		TWinAverage<TUV> m_RstGlueBlckAsgnWin;
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

		void ReserveVarAndLitData(size_t maxAssumps);
		void MoveVarAndLitData(TUVar vFrom, TUVar vTo);
		void RemoveVarAndLitData(TUVar v);

		TUVar m_LastGloballySatisfiedLitAfterSimplify = BadUVar;
		int64_t m_ImplicationsTillNextSimplify = 0;
		
		void SimplifyIfRequired();
		void CompressBuffersIfRequired();
		void CompressWLs();
		bool DebugAssertWaste();
		void DeleteClausesIfRequired();

		// IMPORTANT: in compressed mode, returns the next bit in the current buffer (rather than the whole BC index, including, e.g., the 11 hash bits)
		inline TUInd ClsEnd(TUInd clsInd) 
		{ 
			if constexpr (Compress)
			{			
				const TBCInd bcInd(clsInd);
				if (bcInd.m_BitsForClsSize == 0)
				{
					return bcInd.BitFirstLit() + bcInd.m_BitsForLit * ClsGetSize(clsInd);
				}				
				else
				{
					const TBCHashId bcHashInd(bcInd.GetHashId());
					const auto& ba = BCGetBitArray(bcHashInd);
					const auto bcClsSize = (uint32_t)ba.bit_get(bcInd.m_BitStart, bcInd.m_BitsForClsSize);
					return bcClsSize == 0 ? bcInd.m_BitStart + bcInd.m_BitsForLit : bcInd.BitFirstLit() + bcInd.m_BitsForLit * BCEncodedClsSize2ClsSizeConst(bcClsSize, bcInd.m_BitsForClsSize);
				}
			}
			else
			{
				return clsInd + EClsLitsStartOffset(ClsGetIsLearnt(clsInd), EClsGetIsOversized(clsInd)) + ClsGetSize(clsInd);
			}
		}

		inline bool WLChunkDeleted(TUInd wlInd) const { return wlInd + 1 >= m_W.cap() || m_W[wlInd + 1] == BadULit; }
		inline TUInd WLEnd(TUInd wlInd) const { return wlInd + ((TUInd)1 << m_W[wlInd]); }

		// The index of the first learnt clause: it is guaranteed that no conflict clause appears before m_FirstLearntClsInd in the buffer
		// For non-incremental instances, there may be some initial clauses after m_FirstLearntClsInd
		// The above is correct for non-compressed mode only as m_FirstLearntClsInd is irrelevant for the compressed mode
		TUInd m_FirstLearntClsInd = numeric_limits<TUInd>::max();

		// Clause loop
		bool m_CurrLoopIsLearntOnly = false;
		// Clause loop: current info for standard clauses
		TUInd m_ClsLoopCurrStandardCls = BadClsInd;
		TUInd m_ClsLoopNextStandardCls = BadClsInd;
		// Clause loop: current info for compressed clauses
		unordered_map<uint16_t, CBitArray>::iterator m_ClsLoopCurrCompressedBA;
		uint64_t m_ClsLoopCurrCompressedBACurrBit = numeric_limits<uint64_t>::max();

		inline TBCInd ClsLoopCurrBCInd() const
		{
			TBCHashId bcHashId = m_ClsLoopCurrCompressedBA->first;
			return TBCInd(bcHashId, m_ClsLoopCurrCompressedBACurrBit);
		}

		inline TUInd ClsLoopCompressedNewBC()
		{			
			while (m_ClsLoopCurrCompressedBA != m_BC.end() && 
				((m_ClsLoopCurrCompressedBACurrBit >= m_ClsLoopCurrCompressedBA->second.bit_get_next_bit()) || (m_CurrLoopIsLearntOnly && !((TBCHashId)m_ClsLoopCurrCompressedBA->first).m_IsLearnt)))
			{
				++m_ClsLoopCurrCompressedBA;				
			}

			if (m_ClsLoopCurrCompressedBA == m_BC.end())
			{
				m_ClsLoopCurrCompressedBACurrBit = numeric_limits<uint64_t>::max();
				return numeric_limits<TUInd>::max();
			}

			return ClsLoopCurrBCInd();
		}

		inline TUInd ClsLoopFirst(bool isLearntOnly)
		{
			m_CurrLoopIsLearntOnly = isLearntOnly;
			if constexpr (Compress)
			{
				m_ClsLoopCurrCompressedBA = m_BC.begin();
				m_ClsLoopCurrCompressedBACurrBit = 0;
				return ClsLoopCompressedNewBC();
			}
			else
			{
				m_ClsLoopCurrStandardCls = m_CurrLoopIsLearntOnly ? m_FirstLearntClsInd : LitsInPage;
				m_ClsLoopNextStandardCls = ClsEnd(m_ClsLoopCurrStandardCls);
				return m_ClsLoopCurrStandardCls;
			}
		}

		inline TUInd ClsLoopNext()
		{
			if constexpr (Compress)
			{
				assert(!ClsLoopCompleted());
				m_ClsLoopCurrCompressedBACurrBit = ClsEnd(ClsLoopCurrBCInd());
				CBitArray& ba = m_ClsLoopCurrCompressedBA->second;
				if (m_ClsLoopCurrCompressedBACurrBit >= ba.bit_get_next_bit())
				{
					++m_ClsLoopCurrCompressedBA;
					m_ClsLoopCurrCompressedBACurrBit = 0;
					return ClsLoopCompressedNewBC();
				}
				else
				{
					return ClsLoopCurrBCInd();
				}
			}
			else
			{
				m_ClsLoopCurrStandardCls = m_ClsLoopNextStandardCls;
				m_ClsLoopNextStandardCls = ClsLoopCompleted() ? BadClsInd : ClsEnd(m_ClsLoopCurrStandardCls);
				return m_ClsLoopCurrStandardCls;
			}
		}
		
		inline bool ClsLoopCompleted() const
		{
			if constexpr (Compress)
			{
				return m_ClsLoopCurrCompressedBACurrBit == numeric_limits<uint64_t>::max();
			}
			else
			{
				return m_ClsLoopCurrStandardCls >= m_BNext;
			}
		}
					
		struct TClsDelInfo
		{
			uint64_t m_ConfsPrev = 0;
			uint64_t m_TriggerNext = 0;
			uint64_t m_TriggerInc = 0;
			double m_TriggerMult = 0.;
			union 
			{
				uint64_t m_TriggerMax = 0;
				uint64_t m_CurrChange;
			};
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
		bool m_DratSortEveryClause = false;

		/*
		* Callbacks
		*/
		
		TCbStopNow M_CbStopNow = nullptr;
		bool m_InterruptNow = false;
		TCbNewLearntCls<TLit> M_CbNewLearntCls = nullptr;
		CVector<TLit> m_UserCls;

		/*
		* Parallel support
		*/

		unsigned m_ThreadId = std::numeric_limits<unsigned>::max();
		std::function<void(unsigned id, int lit)> M_ReportUnitCls = nullptr;
		std::function<int(unsigned threadId, bool reinit)> M_GetNextUnitClause = nullptr;

		/*
		* Inprocessing
		*/

		void InprocessIfRequired();
		void IngRemoveBinaryWatchesIfRequired();
		
		// m_Stat.m_SolveInvs last time inprocessing was invoked
		uint64_t m_IngLastSolveInv = 0;
		// m_Stat.m_Conflicts last time inprocessing was invoked
		uint64_t m_IngLastConflicts = 0;
		// m_Stat.m_EverAddedBinaryClss last time inprocessing was invoked
		uint64_t m_IngLastEverAddedBinaryClss = 0;
		
		/*
		* Debugging
		* The printing functions print out some useful info and return true (to be used inside assert statements)
		*/

		string SVar(TUVar v) const;
		string SLit(TULit l) const;
		string STrail();
		template <class TULitSpan>
		string SLits(TULitSpan tulitSpan, bool toExternal = false)
		{
			stringstream ss;
			if (toExternal && m_DratSortEveryClause)
			{
				vector<TLit> externalLits(tulitSpan.size());
				transform(tulitSpan.begin(), tulitSpan.end(), externalLits.begin(), [&](TULit l)
				{
					return GetExternalLit(l);
				});
				sort(externalLits.begin(), externalLits.end(), [&](TLit l1, TLit l2)
				{
					const auto l1Abs = l1 < 0 ? -l1 : l1;
					const auto l2Abs = l2 < 0 ? -l2 : l2;
					return l1Abs < l2Abs;
				});

				for (auto l : externalLits)
				{
					ss << to_string(l) << " ";
				}
			}
			else
			{ 
				for (auto l : tulitSpan)
				{
					ss << (toExternal ? to_string(GetExternalLit(l)) : SLit(l)) << " ";
				}
			}
			


			return ss.str().substr(0, ss.str().size() - 1);
		}
		
		string SUserLits(const span<TLit> litSpan) const;
		string SVars(const TSpanTULit varSpan) const;
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
		void ReserveExactly(T& toReserve, size_t newCap, unsigned char initVal, const string& errSuffix)
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
		
		template<class ForwardIt, class GetValFunc>
		ForwardIt min_element(ForwardIt first, ForwardIt last, GetValFunc GetVal) const
		{
			if (first == last) return last;

			ForwardIt smallest = first;
			auto smallestVal = GetVal(*first);
			++first;
			for (; first != last; ++first) 
			{
				auto currentVal = GetVal(*first);
				if (currentVal < smallestVal)
				{
					smallest = first;
					smallestVal = currentVal;
				}
			}
			return smallest;
		}

		template<class ForwardIt, class GetValFunc>
		ForwardIt max_element(ForwardIt first, ForwardIt last, GetValFunc GetVal) const
		{
			if (first == last) return last;

			ForwardIt largest = first;
			auto largestVal = GetVal(*first);
			++first;
			for (; first != last; ++first)
			{
				auto currentVal = GetVal(*first);
				if (currentVal > largestVal)
				{
					largest = first;
					largestVal = currentVal;
				}
			}
			return largest;
		}

		TUVar static constexpr GetVar(TULit l) { return l >> 1; }
		static_assert(GetVar(0b1001) == 0b100 && GetVar(0b1000) == 0b100);

		TULit static constexpr GetLit(TUVar v, bool isNegative) { return (v << 1) + (TULit)isNegative; }
		static_assert(GetLit(0b100, true) == 0b1001 && GetLit(0b100, false) == 0b1000);

		TULit static constexpr GetFirstLit() { return GetLit(1, false); }
		static_assert(GetFirstLit() == 2);

		bool static constexpr IsPos(TULit l) { return (l & 1) == 0; }
		static_assert(IsPos(0b1000));

		bool static constexpr IsNeg(TULit l) { return (bool)(l & 1); }
		static_assert(IsNeg(0b1001));

		TULit static constexpr Negate(TULit l) { return l ^ 1; }
		static_assert(Negate(0b1001) == 0b1000 && Negate(0b1000) == 0b1001);

		TULit static constexpr GetMaxLit(TULit l) { return IsNeg(l) ? l : Negate(l); }
		static_assert(GetMaxLit(0b1001) == 0b1001 && GetMaxLit(0b1000) == 0b1001);

		// Initialization-related: the initial number of entries in the fused buffer: set the original size to 4Mb
		static constexpr size_t InitEntriesInB = 0x400000 / sizeof(TULit);

		// The assumed number of bytes in one page
		static constexpr size_t BytesInPage = 64;
		static_assert(has_single_bit(BytesInPage));

		// The number of literals in one page; relevant for watch allocation
		static constexpr size_t LitsInPage = BytesInPage / sizeof(TULit);
		static_assert(has_single_bit(LitsInPage));
		// Make sure that, initially, we allocate a sufficient number of entries in the buffer to accommodate LitsInPage literals
		static_assert(LitsInPage <= InitEntriesInB);
		
		// Bad parent clause index
		TUInd static constexpr BadClsInd = 0;

		template<class ForwardIt, class UnaryPredicate>
		ForwardIt move_if(ForwardIt first, ForwardIt last, UnaryPredicate p)
		{
			first = std::find_if(first, last, p);
			if (first != last)
				for (ForwardIt i = first; ++i != last; )
					if (!p(*i))
					{
						//*first++ = std::move(*i);
						swap(*first, *i);
						++first;
					}
			return first;
		}

		// Removing duplicates, discovering tautologies and storing incoming clauses
		class CHandleNewCls
		{
		public:
			CHandleNewCls(size_t initVarNum) : m_LastAppearenceCounter(initVarNum, (size_t)0), m_Counter(0) {}

			inline void NewClause()
			{
				++m_Counter;
				if (unlikely(m_Counter < 0))
				{
					m_LastAppearenceCounter.memset(0);
					m_Counter = 1;
				}
				m_Cls.clear();
			}

			tuple<bool, bool, bool> NewLitIsTauIsDuplicate(TULit newLit)
			{
				bool isTau(false), isDuplicate(false);

				const bool isNegative = IsNeg(newLit);
				const TUVar newVar = GetVar(newLit);
				const bool varExists = newVar < (TUVar)m_LastAppearenceCounter.cap();

				if (!varExists)
				{
					m_LastAppearenceCounter.reserve_atleast((size_t)newVar + 1, (size_t)0);
					if (m_LastAppearenceCounter.uninitialized_or_erroneous())
					{
						return make_tuple(isTau, isDuplicate, true);
					}
					m_LastAppearenceCounter[newVar] = isNegative ? -m_Counter : m_Counter;
				}
				else
				{
					const auto elemVal = m_LastAppearenceCounter[newVar];
					if (unlikely((llabs(elemVal) == llabs(m_Counter))))
					{
						isDuplicate = (elemVal < 0) == isNegative;
						isTau = !isDuplicate;
					}
					else
					{
						m_LastAppearenceCounter[newVar] = isNegative ? -m_Counter : m_Counter;
					}
				}

				if (likely(!isTau && !isDuplicate))
				{
					m_Cls.push_back(newLit);
					if (m_LastAppearenceCounter.uninitialized_or_erroneous())
					{
						return make_tuple(isTau, isDuplicate, true);
					}
				}

				return make_tuple(isTau, isDuplicate, false);
			}

			inline TSpanTULit GetCurrCls() { return m_Cls.get_span(); }
			inline void SetMultiplier(double multiplier) { m_LastAppearenceCounter.SetMultiplier(multiplier); }
			inline size_t memMb() const { return m_Cls.memMb() + m_LastAppearenceCounter.memMb() + sizeof(m_Counter); }
			void reserve_exactly(size_t newCap, unsigned char initVal)
			{
				m_LastAppearenceCounter.reserve_exactly(newCap, initVal);
			}
			void reserve_exactly(size_t newCap)
			{
				m_LastAppearenceCounter.reserve_exactly(newCap);
			}
			inline bool uninitialized_or_erroneous() { return m_LastAppearenceCounter.uninitialized_or_erroneous();	}
		protected:
			CVector<TULit> m_Cls;
			CDynArray<TCounterType> m_LastAppearenceCounter;
			TCounterType m_Counter;			
		};

		string GetMemoryLayout() const;				
	};

	std::ostream& operator << (std::ostream& os, const TToporReturnVal& trv);

	inline void swap(CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef&& ccir1RValue, CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef&& ccir2RValue)
	{
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir1(ccir1RValue.m_Ba, ccir1RValue.m_StartingBit, ccir1RValue.m_BitsForLit);
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir2(ccir2RValue.m_Ba, ccir2RValue.m_StartingBit, ccir2RValue.m_BitsForLit);
		std::swap(ccir1, ccir2);
	}

	inline void swap(const CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef& ccir1LValue, CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef&& ccir2RValue)
	{
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir1(ccir1LValue.m_Ba, ccir1LValue.m_StartingBit, ccir1LValue.m_BitsForLit);
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir2(ccir2RValue.m_Ba, ccir2RValue.m_StartingBit, ccir2RValue.m_BitsForLit);
		std::swap(ccir1, ccir2);
	}

	inline void swap(CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef&& ccir1RValue, const CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef& ccir2LValue)
	{
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir1(ccir1RValue.m_Ba, ccir1RValue.m_StartingBit, ccir1RValue.m_BitsForLit);
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir2(ccir2LValue.m_Ba, ccir2LValue.m_StartingBit, ccir2LValue.m_BitsForLit);
		std::swap(ccir1, ccir2);
	}

	inline void swap(const CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef& ccir1LValue, const CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef& ccir2LValue)
	{
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir1(ccir1LValue.m_Ba, ccir1LValue.m_StartingBit, ccir1LValue.m_BitsForLit);
		CTopi<int32_t, uint64_t, true>::CCompressedCls::CIteratorRef ccir2(ccir2LValue.m_Ba, ccir2LValue.m_StartingBit, ccir2LValue.m_BitsForLit);
		std::swap(ccir1, ccir2);
	}

	template <class T1, class T2>
	inline void swap(T1& cls1, T2& cls2)
	{
		std::swap(cls1, cls2);
	}
}
