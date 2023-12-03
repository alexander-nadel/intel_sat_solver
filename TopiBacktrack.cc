// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::BacktrackingInit()
{
	m_CurrChronoBtIfHigher = m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamChronoBtIfHigherS : m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamChronoBtIfHigherN : m_ParamChronoBtIfHigherInit;
	m_CurrCustomBtStrat = m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamCustomBtStratS : m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamCustomBtStratN : m_ParamCustomBtStratInit;
	m_ConfsSinceNewInv = m_Stat.m_Conflicts;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::Backtrack(TLit decLevelL, bool isBCPBacktrack, bool isAPICall)
{	
	TUV decLevel = (TUV)decLevelL;
	//	cout << "\tc b <BacktrackLevel>" << endl;
	if (m_DumpFile && isAPICall) (*m_DumpFile) << "b " << decLevel << endl;

	if (decLevel >= m_DecLevel)
	{
		return;
	}	

	assert(NV(2) || P("***** Backtracking to " + to_string(decLevel) + "\n"));

	++m_Stat.m_Backtracks;
	if (isBCPBacktrack) ++m_Stat.m_BCPBacktracks;
	m_Stat.m_ChronoBacktracks += (decLevel == m_DecLevel - 1);	

	// Cleaning up collapsed decision levels
	while (decLevel != 0 && m_TrailLastVarPerDecLevel[decLevel] == BadClsInd)
	{
		if (decLevel == m_DecLevelOfLastAssignedAssumption)
		{
			--m_DecLevelOfLastAssignedAssumption;
		}
		--decLevel;
	}

	while (m_TrailEnd != m_TrailLastVarPerDecLevel[decLevel])
	{
		UnassignVar(m_TrailEnd);
	}
		
	m_DecLevel = decLevel;

	assert(NV(2) || P("***** Backtracked to " + to_string(m_DecLevel) + "\n"));	
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
