// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::TULit CTopi<TLit,TUInd,Compress>::Decide()
{
	// #topor : we should try other decision heuristics
	// Maple uses DISTANCE for the first 50000 conflicts, then switches between VSIDS and LRB
	// Cadical switches between VSIDS and VMTF
	// Fiver uses CBH
	while (!m_VsidsHeap.empty())
	{
		TUVar v = m_VsidsHeap.remove_min();
		if (!IsAssignedVar(v))
		{
			++m_Stat.m_Decisions;
			m_Stat.m_SumOfAllDecLevels += m_DecLevel;
			return GetLit(v, GetNextPolarityIsNegated(v));
		}
	}
	return BadULit;
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit,TUInd,Compress>::GetNextPolarityIsNegated(TUVar v)
{
	assert(!IsAssignedVar(v));
	if (IsNotForced(v))
	{
		return m_ParamPolarityStrat == 1 ? (bool)(rand() % 2) : m_AssignmentInfo[v].m_IsNegated;
	}
	else
	{
		return m_PolarityInfo[v].GetNextPolarityIsNegated();
	}	
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::UpdateDecisionStrategyOnNewConflict(TUV glueLearnt, TUVar lowestGlueUpdateVar, TUVar fakeTrailEnd)
{
	auto D1LowerOrEqD2UpToPrecision = [](double d1, double d2)
	{
		static constexpr double precision = 0.000001;
		return d1 <= d2 || d1 - d2 <= precision;
	};

	assert(m_Stat.m_VarDecay != 0.0);

	const double varDecayInc = m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamVarDecayIncS : m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamVarDecayIncAi : m_ParamVarDecayInc;
	const double varDecayMax = m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamVarDecayMaxS : m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamVarDecayMaxAi : m_ParamVarDecayMax;

	if (m_ParamVarDecayUpdateConfRate > 0 && (m_Stat.m_Conflicts % m_ParamVarDecayUpdateConfRate == 0) 
		&& D1LowerOrEqD2UpToPrecision(m_Stat.m_VarDecay + varDecayInc, varDecayMax))
	{ 
		m_Stat.m_VarDecay += varDecayInc;
	}

	if (m_ParamVarActivityGlueUpdate && glueLearnt != 0)
	{
		assert(fakeTrailEnd != lowestGlueUpdateVar);
		for (TUVar v = fakeTrailEnd; v != BadUVar && m_VarInfo[v].m_TrailNext != lowestGlueUpdateVar; v = m_VarInfo[v].m_TrailPrev)
		{
			auto& ai = m_AssignmentInfo[v];
			assert(ai.m_IsAssigned || fakeTrailEnd != m_TrailEnd);
			auto& vi = m_VarInfo[v];

			if (ai.m_Visit && (ai.IsAssignedBinary() || 
				(vi.m_ParentClsInd != BadUVar && ClsGetIsLearnt(vi.m_ParentClsInd) && ClsGetGlue(vi.m_ParentClsInd) < glueLearnt)))
			{				
				UpdateScoreVar(v);				
			}
		}
	}

	// Decay activity
	m_VsidsHeap.var_inc_update(m_Stat.m_VarDecay);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::DecisionInit()
{
	assert(m_QueryCurr != TQueryType::QUERY_NONE);
	if ((m_QueryCurr == TQueryType::QUERY_INIT) || (m_QueryCurr == TQueryType::QUERY_INC_NORMAL && m_ParamVarActivityIncDecayReinitN) || 
		(m_QueryCurr == TQueryType::QUERY_INC_SHORT && m_ParamVarActivityIncDecayReinitS && 
			(m_ParamVarActivityIncDecayStopReinitSInv == 0 || m_Stat.m_ShortIncSolveInvs < (uint64_t)m_ParamVarActivityIncDecayStopReinitSInv) &&
			(m_ParamVarActivityIncDecayStopReinitRestart == 0 || m_Stat.m_Restarts < (uint64_t)m_ParamVarActivityIncDecayStopReinitRestart) &&
		(m_ParamVarActivityIncDecayStopReinitConflict == 0 || m_Stat.m_Conflicts < (uint64_t)m_ParamVarActivityIncDecayStopReinitConflict) && 
		(m_ParamVarActivityIncDecayStopReinitTime == 0.0 || m_Stat.m_OverallTime.WallTimePassedSinceStartOrResetConst() < m_ParamVarActivityIncDecayStopReinitTime)))
	{
		m_Stat.m_VarDecay = m_QueryCurr == TQueryType::QUERY_INIT ? m_ParamVarActivityIncDecay : m_ParamVarActivityIncDecayReinitVal;		
	}	

	if (InitClssBoostScoreStratOn())
	{
		m_CurrInitClssBoostScoreMult = InitClssBoostScoreStratIsReversedOrder() ? m_ParamInitClssBoostMultLowest : m_ParamInitClssBoostMultHighest;
	}

	if (m_ParamRandomizePolarityAtEachIncrementalCall && m_Stat.m_SolveInvs > 1)
	{
		for (TUVar v = 1; v < GetNextVar(); ++v)
		{
			FixPolarityInternal(GetLit(v, (bool)(rand() % 2)), true);
		}
	}
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::UpdateScoreVar(TUVar v, double mult)
{
	const bool isRescaled = m_VsidsHeap.increase_score(v, mult);
	if (m_CurrCustomBtStrat > 0)
	{
		if (isRescaled)
		{
			for (auto& s : m_BestScorePerDecLevel.get_span_cap())
			{
				s *= 1e-100;
			}
		}

		if (IsAssignedVar(v))
		{
			const auto decLevel = GetAssignedDecLevelVar(v);
			const auto currScore = m_VsidsHeap.get_var_score(v);

			if (decLevel >= m_BestScorePerDecLevel.cap())
			{
				assert(decLevel <= GetNextVar());
				ReserveExactly(m_BestScorePerDecLevel, GetNextVar(), 0, "m_BestScorePerDecLevel in UpdateScoreVar");
				if (unlikely(IsUnrecoverable())) return;
			}

			if (currScore > m_BestScorePerDecLevel[decLevel])
			{
				m_BestScorePerDecLevel[decLevel] = currScore;
				assert(NV(2) || P("m_BestScorePerDecLevel[" + to_string(decLevel) + "] updated to " + to_string(currScore) + " in UpdateScoreVar\n"));
			}

			assert(m_ParamAssertConsistency < 1 || CalcMaxDecLevelScore(decLevel) == m_BestScorePerDecLevel[decLevel]);
		}
	}
}

template class CTopi<int32_t, uint32_t, false>;
template class CTopi<int32_t, uint64_t, false>;
template class CTopi<int32_t, uint64_t, true>;
