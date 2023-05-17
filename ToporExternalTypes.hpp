// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <cstdint>
#include <type_traits>
#include <bit>
#include <string>
#include <sstream>
#include <array>
#include <tuple>
#include <functional>
#include "ColorPrint.h"
#include "TimeMeasure.h"
#include "BasicMemoryUsage.h"

namespace Topor
{
	// The solver status
	enum class TToporStatus : uint8_t
	{
		// Status unknown
		STATUS_UNDECIDED,
		// The latest invocation had returned SAT and no clauses contradicting the model were introduced since
		STATUS_SAT,
		// The latest invocation returned UNSAT, but this status might be temporary (under assumptions)
		STATUS_UNSAT,
		// The latest invocation returned USER_INTERRUPT
		STATUS_USER_INTERRUPT,
		/*
		* Only unrecoverable status values below
		*/
		STATUS_FIRST_UNRECOVERABLE,
		// The instance is forever contradictory. 
		STATUS_CONTRADICTORY = STATUS_FIRST_UNRECOVERABLE,
		/*
		* Only permanently erroneous values below
		*/
		STATUS_FIRST_PERMANENT_ERROR,
		// The instance is in memory-out state, meaning that one of the allocations fails.
		STATUS_ALLOC_FAILED = STATUS_FIRST_PERMANENT_ERROR,
		// Data doesn't fit into the buffer
		STATUS_INDEX_TOO_NARROW,
		// Parameter-related error
		STATUS_PARAM_ERROR,
		// Error while processing assumption-required queries
		STATUS_ASSUMPTION_REQUIRED_ERROR,
		// Global timeout reached
		STATUS_GLOBAL_TIMEOUT,
		// Problem when trying to access/write the DRAT file
		STATUS_DRAT_FILE_PROBLEM,
		// An explicit-clauses-only function invoked in compressed mode
		STATUS_COMPRESSED_MISMATCH,
		// So-far: cannot accommodate the last possible variable if sizeof(TLit) == sizeof(size_t), since the allocation will fail
		STATUS_EXOTIC_ERROR
	};

	// Return value of Topor solving functions
	enum class TToporReturnVal : uint8_t
	{
		// Satisfiable
		RET_SAT,
		// Unsatisfiable
		RET_UNSAT,
		// Time-out for the last Solve invocation (the run-time is beyond user-provided threshold value in Solve's parameter)
		RET_TIMEOUT_LOCAL,
		// Conflict-out (the number of conflicts is beyond user's threshold value)
		RET_CONFLICT_OUT,
		// Memory-out
		RET_MEM_OUT,
		// Interrupted by the user
		RET_USER_INTERRUPT,
		// Data doesn't fit into the buffer anymore; Try to compile and run Topor with a wider TUInd, if you get this error
		RET_INDEX_TOO_NARROW,
		// Parameter-related error
		RET_PARAM_ERROR,
		// Error while processing assumption-required queries
		RET_ASSUMPTION_REQUIRED_ERROR,
		// Global time-out (the run-time is beyond user-provided threshold value in /global/overall_timeout parameter: the solver is unusable, if this value is returned)
		RET_TIMEOUT_GLOBAL,
		// Problem with DRAT file generation
		RET_DRAT_FILE_PROBLEM,
		// Exotic error: see the error string for more information
		RET_EXOTIC_ERROR	
	};

	[[maybe_unused]] std::ostream& operator << (std::ostream& os, const TToporReturnVal& trv);

	// Literal value in Topor                     
	enum class TToporLitVal : uint8_t
	{
		VAL_SATISFIED,
		VAL_UNSATISFIED,
		VAL_UNASSIGNED,
		VAL_DONT_CARE,
	};

	// Callbacks

	// Callbacks return TStopTopor, which indicates, whether the solver should be stopped
	enum class TStopTopor : bool 
	{
		VAL_STOP,
		VAL_CONTINUE
	};
	// New learnt clause report callback. 
	template <typename TLit>
	using TCbNewLearntCls = std::function<TStopTopor(const std::span<TLit>)>;
	using TCbStopNow = std::function<TStopTopor()>;
	using TGetNum = std::function<size_t()>;
	using TGetString = std::function<std::string()>;

	// Statistics, available to the user
	template <typename TLit, typename TUInd>
	struct TToporStatistics
	{
		TToporStatistics(TGetNum BGetNum, TGetNum BGetCap, TGetNum BGetSize, TGetString GetExtraString, double varActivityInc) : M_BGetNum(BGetNum), M_BGetCap(BGetCap), M_BGetSize(BGetSize), M_GetExtraString(GetExtraString), m_VarActivityInc(varActivityInc), m_OverallTime(false, 1000), m_TimeSinceLastSolveStart(false, 1000) {}
		
		template <bool IsColor = true> 
		std::string StatStrShort(bool forcePrintingHead = false)
		{
			std::stringstream ssHead, ssStat;			
			const bool printHead = forcePrintingHead || m_ShortStartInv % 50 == 0;
			if (printHead) ssHead << "c ToporStt ";
			ssStat << "c ToporStt ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>("CpuT0 WallT0 CPUTSolve WallTSolve CurrMemMb PeakMemMb");
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>(to_string(m_OverallTime.CpuTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>(to_string(m_OverallTime.WallTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>(to_string(m_TimeSinceLastSolveStart.CpuTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>(to_string(m_TimeSinceLastSolveStart.WallTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>(to_string(BasicMemUsage::GetCurrentRSSMb())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan: ansi_color_code::none>(to_string(BasicMemUsage::GetPeakRSSMb())) << " ";			

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(" Confs Decs D/C BCPs Asngs ImplPr ImplPerCPUT");
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string(m_Conflicts)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string(m_Decisions)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string((double)m_Decisions / (double)m_Conflicts)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string(m_BCPs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string(m_Assignments)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string(Perc(m_Implications, m_Assignments))) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta: ansi_color_code::none>(to_string((double)m_Implications / m_OverallTime.CpuTimePassedSinceStartOrResetConst())) << " ";
			
			//if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::bright_magenta: ansi_color_code::none>(" DyiTrg DyiProp DyiDlCo");
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::bright_magenta: ansi_color_code::none>(" DyiTrg");
			ssStat << print_as_color<IsColor ? ansi_color_code::bright_magenta: ansi_color_code::none>(to_string(m_DelayedImplicationsTrigerring)) << " ";
			//ssStat << print_as_color<IsColor ? ansi_color_code::bright_magenta: ansi_color_code::none>(to_string(m_DelayedImplicationsPropagated)) << " ";
			//ssStat << print_as_color<IsColor ? ansi_color_code::bright_magenta: ansi_color_code::none>(to_string(m_DelayedImplicationDecLevelsCollapsed)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::red: ansi_color_code::none>(" Bufs BufSzMb BufCapMb");
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string(M_BGetNum())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string((double)M_BGetSize() / 1000000.)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::red: ansi_color_code::none>(to_string((double)M_BGetCap() / 1000000.)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(" SolveInvs ShortIncrInvs UserVars IntrVars AddClss ActClss BinAClss LongAClss AvrgAClsLen AvrgALongClsLen LongALrnts Simplfs ClssDels FlpRecs FlpSw FlpUnit SubsLRem AllUipAtmp AllUipSucc AllUipLRem");
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_SolveInvs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_ShortIncSolveInvs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_MaxUserVar)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_MaxInternalVar)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_AddClauseInvs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(GetActiveClss())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_ActiveBinaryClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_ActiveLongClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(GetActiveClss() == 0 ? 0.0 : (double)m_ActiveOverallClsLen / (double)GetActiveClss())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_ActiveLongClss == 0 ? 0.0 : (double)(m_ActiveOverallClsLen - (m_ActiveBinaryClss << 1)) / (double)m_ActiveLongClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_ActiveLongLearntClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_Simplifies)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_ClssDel)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_FlippedClauses)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_FlippedClausesSwapped)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_FlippedClausesUnit )) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_LitsRemovedByConfSubsumption)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_AllUipAttempted)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_AllUipSucceeded)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue: ansi_color_code::none>(to_string(m_LitsRemovedByAllUip)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" Bts");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(m_Backtracks)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" ChBtsPr");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(ChronoBtsPerc())) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" BCPBtPr");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(BCPBtsPerc())) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" AvrgDecLev");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(m_Decisions == 0 ? 0.0 : (double)m_SumOfAllDecLevels / (double)m_Decisions)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" LvlsSvdAsmp");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(m_AssumpReuseBacktrackLevelsSaved)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" RTAsg");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(m_ReuseTrailAsssignments)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(" RTContr");
			ssStat << print_as_color<IsColor ? ansi_color_code::green: ansi_color_code::none>(to_string(m_ReuseTrailContradictions)) << " ";


			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::black: ansi_color_code::none>(" VSIDSInc VSIDSDecay");
			ssStat << print_as_color<IsColor ? ansi_color_code::black: ansi_color_code::none>(to_string_scientific(m_VarActivityInc)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::black: ansi_color_code::none>(to_string(m_VarDecay, 3)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::bright_cyan: ansi_color_code::none>(" Rsts RstBlocked");
			ssStat << print_as_color<IsColor ? ansi_color_code::bright_cyan: ansi_color_code::none>(to_string(m_Restarts)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::bright_cyan: ansi_color_code::none>(to_string(m_RestartsBlocked)) << " ";

			++m_ShortStartInv;

			std::string extraStr = M_GetExtraString == nullptr ? "" : M_GetExtraString();

			return (printHead ? ssHead.str() + "\n" : "") + ssStat.str() + (extraStr == "" ? "\n" : "\n" + extraStr + "\n");
		}

		// The maximal variable number, provided by the user
		TLit m_MaxUserVar = 0;
		// The last existing internal variable
		uint64_t m_MaxInternalVar = 0;
		void UpdateMaxInternalVar(uint64_t miv) { m_MaxInternalVar = miv; }

		// The number of invocations of Solve function
		uint64_t m_SolveInvs = 0;
		// The number of short incremental invocations of Solve function
		uint64_t m_ShortIncSolveInvs = 0;
		// The number of invocations of AddClause function
		uint64_t m_AddClauseInvs = 0;

		// The number of active binary clauses
		uint64_t m_ActiveBinaryClss = 0;
		// The number of active long (non-binary) clauses
		uint64_t m_ActiveLongClss = 0;
		// The number of active long learnt clauses (of any quality: low, medium, high)
		uint64_t m_ActiveLongLearntClss = 0;
		// The number of active clauses
		uint64_t GetActiveClss() const { return m_ActiveLongClss + m_ActiveBinaryClss; }

		// The number of backtracks
		uint64_t m_Backtracks = 0;
		// The number of chronological backtracks
		uint64_t m_ChronoBacktracks = 0;
		inline double ChronoBtsPerc() const { return Perc(m_ChronoBacktracks, m_Backtracks); }
		// The number of BCP-backtracks (required to handle multiple conflicts and delayed implications)
		uint64_t m_BCPBacktracks = 0;
		inline double BCPBtsPerc() const { return Perc(m_BCPBacktracks, m_Backtracks); }

		// The number of backtrack levels, saved by reusing assumptions
		uint64_t m_AssumpReuseBacktrackLevelsSaved = 0;
		// The sum of all the levels at which a decision was taken
		uint64_t m_SumOfAllDecLevels = 0;
		// Assignments carried out as a result of reuse-trail
		uint64_t m_ReuseTrailAsssignments = 0;
		// Contradictions discovered during reuse-trail
		uint64_t m_ReuseTrailContradictions = 0;

		// The number, capacity and sizes of the main buffer(s), containing the clauses, in bytes
		TGetNum M_BGetNum = nullptr;
		TGetNum M_BGetCap = nullptr;
		TGetNum M_BGetSize = nullptr;
		// An extra-string
		TGetString M_GetExtraString = nullptr;
		
		// The number of conflicts
		uint64_t m_Conflicts = 0;

		// The number of assignments
		uint64_t m_Assignments = 0;

		// The number of decisions
		uint64_t m_Decisions = 0;

		// The number of BCP invocations
		uint64_t m_BCPs = 0;

		// The number of implications (in all BCPs)
		uint64_t m_Implications = 0;

		// The number of triggering delayed implications (that is, not propagated)
		uint64_t m_DelayedImplicationsTrigerring = 0;
		// The number of propagated delayed implications
		uint64_t m_DelayedImplicationsPropagated = 0;		
		// The number of decision levels, which collapsed (disappeared) as a result of delayed implications
		uint64_t m_DelayedImplicationDecLevelsCollapsed = 0;

		// The current variable decay (initialized to m_ParamVarDecay in Solve)
		double m_VarDecay = 0.0;
		double m_VarActivityInc;

		// The number of flipped clauses
		uint64_t m_FlippedClauses = 0;
		// The number of flipped clauses, swapped with 1UIP clauses (since they're better asserting clauses after minimization)
		uint64_t m_FlippedClausesSwapped = 0;
		// The number of flipped clauses, which became unit: note that every swapped clause is unit, but not every unit is swapped
		uint64_t m_FlippedClausesUnit = 0;

		// The number of literals removed by subsumption during conflict analysis
		uint64_t m_LitsRemovedByConfSubsumption = 0;
		
		// The number of restarts
		uint64_t m_Restarts = 0;
		// The number of blocked restarts
		uint64_t m_RestartsBlocked = 0;

		// The number of Simplify invocations
		uint64_t m_Simplifies = 0;
		// The number of clause-deletion invocations
		uint64_t m_ClssDel = 0;

		// The number of literals deleted from clauses by ALL-UIP scheme
		uint64_t m_LitsRemovedByAllUip = 0;
		uint32_t m_AllUipAttempted = 0;
		uint32_t m_AllUipSucceeded = 0;
	protected:
		template <class T>
		inline double Perc(T fraction, T total) const { return total == 0 ? 0. : (double)100. * (double)fraction / (double)total; }
		inline void NewClause(size_t clsLen, bool isLearnt) 
		{ 
			if (clsLen >= 2)
			{
				m_ActiveOverallClsLen += clsLen;
				++(clsLen == 2 ? m_ActiveBinaryClss : m_ActiveLongClss);				
				if (isLearnt && clsLen > 2)
				{
					++m_ActiveLongLearntClss;
				}
			}			
		}
		
		inline void DeleteClause(size_t clsLen, bool isLearnt)
		{
			if (clsLen >= 2)
			{
				m_ActiveOverallClsLen -= clsLen;
				--(clsLen == 2 ? m_ActiveBinaryClss : m_ActiveLongClss);
				if (isLearnt && clsLen > 2)
				{
					--m_ActiveLongLearntClss;
				}
			}
		}

		inline void DeleteBinClauses(size_t numOfClauses)
		{
			m_ActiveOverallClsLen -= ((uint64_t)numOfClauses << 1);
			m_ActiveBinaryClss -= numOfClauses;
		}
		
		inline void RecordDeletedLitsFromCls(uint64_t litsToDelete)
		{
			m_ActiveOverallClsLen -= litsToDelete;				
		}

		inline uint64_t GetActiveLongClsLen() const 
		{
			return m_ActiveOverallClsLen - (m_ActiveBinaryClss << 1);
		}

		inline void NewSolveInvocation(bool isShortIncQuery)
		{
			++m_SolveInvs; 			
			if (isShortIncQuery)
			{
				++m_ShortIncSolveInvs;
			}
			m_TimeSinceLastSolveStart.Reset();
		}
		CTimeMeasure m_OverallTime;
		CTimeMeasure m_TimeSinceLastSolveStart;
		uint8_t m_ShortStartInv = 0;
		uint64_t m_ActiveOverallClsLen = 0;		
		
		template <class T>
		std::string to_string(T v, size_t precision = 1) 
		{
			if (std::is_floating_point<T>::value)
			{
				std::ostringstream stm;
				
				stm << std::fixed << std::setprecision(precision) << v;
				return stm.str();
			}
			else
			{
				return std::to_string(v);
			}			
		}

		std::string to_string_scientific(double v)
		{
			std::ostringstream stm;

			stm << std::scientific << std::setprecision(1) << v;
			return stm.str();
		}

		template <typename T1, typename T2, bool T3>
		friend class CTopi; // every CTopi is our friend
	};
}

