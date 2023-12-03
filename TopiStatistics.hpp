// Copyright(C) 2021-2023 Intel Corporation
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
#include "TimeMeasure.h"
#include "BasicMemoryUsage.h"
#include "ToporExternalTypes.hpp"

namespace Topor
{
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

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>("CpuT0 WallT0 CPUTSolve WallTSolve CurrMemMb PeakMemMb");
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(m_OverallTime.CpuTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(m_OverallTime.WallTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(m_TimeSinceLastSolveStart.CpuTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(m_TimeSinceLastSolveStart.WallTimePassedSinceStartOrResetConst())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(BasicMemUsage::GetCurrentRSSMb())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(BasicMemUsage::GetPeakRSSMb())) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(" Confs Decs D/C BCPs Asngs ImplPr ImplPerCPUT");
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_Conflicts)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_Decisions)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string((double)m_Decisions / (double)m_Conflicts)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_BCPs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_Assignments)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(Perc(m_Implications, m_Assignments))) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string((double)m_Implications / m_OverallTime.CpuTimePassedSinceStartOrResetConst())) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(" Inprocs DupBins");
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string(m_Ings)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string(m_IngsDuplicateBinsRemoved)) << " ";
			
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(" Bufs BufSzMb BufCapMb");
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string(M_BGetNum())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string((double)M_BGetSize() / 1000000.)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string((double)M_BGetCap() / 1000000.)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::blue : ansi_color_code::none>(" SolveInvs ShortIncrInvs");
			ssStat << print_as_color<IsColor ? ansi_color_code::blue : ansi_color_code::none>(to_string(m_SolveInvs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::blue : ansi_color_code::none>(to_string(m_ShortIncSolveInvs)) << " ";
			
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(" UserVars IntrVars");
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(m_MaxUserVar)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::cyan : ansi_color_code::none>(to_string(m_MaxInternalVar)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(" AddClss ActClss BinAClss LongAClss AvrgAClsLen AvrgALongClsLen LongALrnts");
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_AddClauseInvs)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(GetActiveClss())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_ActiveBinaryClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_ActiveLongClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(GetActiveClss() == 0 ? 0.0 : (double)m_ActiveOverallClsLen / (double)GetActiveClss())) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_ActiveLongClss == 0 ? 0.0 : (double)(m_ActiveOverallClsLen - (m_ActiveBinaryClss << 1)) / (double)m_ActiveLongClss)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::magenta : ansi_color_code::none>(to_string(m_ActiveLongLearntClss)) << " ";
			
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(" Simplfs ClssDels SubsLRem");
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string(m_Simplifies)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string(m_ClssDel)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string(m_LitsRemovedByConfSubsumption)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(" FlpRecs FlpSw FlpUnit");
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string(m_FlippedClauses)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string(m_FlippedClausesSwapped)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::red : ansi_color_code::none>(to_string(m_FlippedClausesUnit)) << " ";
		

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(" Bts");
			ssStat << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(to_string(m_Backtracks)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(" ChBtsPr");
			ssStat << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(to_string(ChronoBtsPerc())) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(" BCPBtPr");
			ssStat << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(to_string(BCPBtsPerc())) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(" AvrgDecLev");
			ssStat << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(to_string(m_Decisions == 0 ? 0.0 : (double)m_SumOfAllDecLevels / (double)m_Decisions)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(" LvlsSvdAsmp");
			ssStat << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(to_string(m_AssumpReuseBacktrackLevelsSaved)) << " ";
			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::green : ansi_color_code::none>(" RTAsg");

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(" VSIDSInc VSIDSDecay");
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string_scientific(m_VarActivityInc)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::black : ansi_color_code::none>(to_string(m_VarDecay, 3)) << " ";

			if (printHead) ssHead << print_as_color<IsColor ? ansi_color_code::bright_cyan : ansi_color_code::none>(" Rsts RstBlocked");
			ssStat << print_as_color<IsColor ? ansi_color_code::bright_cyan : ansi_color_code::none>(to_string(m_Restarts)) << " ";
			ssStat << print_as_color<IsColor ? ansi_color_code::bright_cyan : ansi_color_code::none>(to_string(m_RestartsBlocked)) << " ";

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
		// The number of binary clauses ever added
		uint64_t m_EverAddedBinaryClss = 0;


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

		// The number of inprocessings so far
		uint32_t m_Ings = 0;
		// The number of duplicate binary clauses, removed by inprocessing
		uint32_t m_IngsDuplicateBinsRemoved = 0;
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
				if (clsLen == 2)
				{
					++m_EverAddedBinaryClss;
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

