// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include <unordered_set>
#include "Topi.hpp"

using namespace Topor;
using namespace std;


template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::IngRemoveBinaryWatchesIfRequired()
{
	// Remove duplicate binary watches, if any are there and not handled on-the-fly
	if (m_ParamExistingBinWLStrat <= 2 || m_IngLastEverAddedBinaryClss >= m_Stat.m_EverAddedBinaryClss)
	{
		return;
	}

	auto RemoveDuplicateBinWatches = [&](TULit l)
	{
		TWatchInfo& wi = m_Watches[l];
		if (wi.m_BinaryWatches > 0)
		{
			unordered_set<TULit> litsInBinWatches;
			TSpanTULit binWatches = TSpanTULit(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TUInd currbwInd = 0; currbwInd < wi.m_BinaryWatches; ++currbwInd)
			{
				const TULit currSecondLit = binWatches[currbwInd];
				auto pairItNew = litsInBinWatches.emplace(currSecondLit);
				if (!pairItNew.second)
				{
					// Already there!
					const TUVar v = GetVar(l);
					const TUVar currSecondVar = GetVar(currSecondLit);
					// Make sure the code inside runs once per binary clause, rather than twice
					if (v < currSecondVar)
					{
						++m_Stat.m_IngsDuplicateBinsRemoved;
						// Boost the score of both the literals of the clause, if required. 
						if (m_ParamExistingBinWLStrat == 4)
						{
							UpdateScoreVar(v, m_ParamBinWLScoreBoostFactor);
							UpdateScoreVar(currSecondVar, m_ParamBinWLScoreBoostFactor);
						}
					}

					// Replace the current watch by the last one and decrease the number of watched
					binWatches[currbwInd--] = binWatches[--wi.m_BinaryWatches];
				}
			}
		}
	};

	for (TUVar v = 1; v < m_LastExistingVar; ++v)
	{
		RemoveDuplicateBinWatches(GetLit(v, true));
		RemoveDuplicateBinWatches(GetLit(v, false));
	}

	m_IngLastEverAddedBinaryClss = m_Stat.m_EverAddedBinaryClss;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::InprocessIfRequired()
{	
	// If postpone threshold m_ParamIngPostponeFirstInvConflicts not reached --> exit
	// If postpone threshold reached, but no inprocessing invocations so far --> inprocess ASAP
	//		Assertion: if inprocessing invoked at least once --> the postpone threashold must have been reached
	assert(m_Stat.m_Ings == 0 || m_Stat.m_Conflicts >= m_ParamIngPostponeFirstInvConflicts);
	// Once inprocessing invoked at least once (m_Stat.m_Ings > 0), if the # conflicts since the last inprocessing < m_ParamIngConflictsBeforeNextInvocation,
	// wait, unless we must invoke after every query (m_ParamIngInvokeEveryQueryAfterInitPostpone) and we haven't invoked for this query yet (m_IngLastSolveInv < m_Stat.m_SolveInvs)
	if (!m_ParamInprocessingOn || IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT || m_DecLevel != m_DecLevelOfLastAssignedAssumption ||
		(m_Stat.m_Conflicts < m_ParamIngPostponeFirstInvConflicts) ||
		(m_Stat.m_Ings > 0 && ((m_Stat.m_Conflicts - m_IngLastConflicts) < m_ParamIngConflictsBeforeNextInvocation) && 
			!(m_ParamIngInvokeEveryQueryAfterInitPostpone && m_IngLastSolveInv < m_Stat.m_SolveInvs)))
	{
		return;
	}

	++m_Stat.m_Ings;
	m_IngLastSolveInv = m_Stat.m_SolveInvs;
	m_IngLastConflicts = m_Stat.m_Conflicts;
	

	assert(NV(1) || P("Inprocessing started for time " + to_string(m_Stat.m_Ings) + "; m_SolveInvs = " + to_string(m_IngLastSolveInv) + "; m_Conflicts = " + to_string(m_IngLastConflicts)));

	IngRemoveBinaryWatchesIfRequired();
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
