// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"

using namespace Topor;
using namespace std;

void CTopi::BacktrackingInit()
{
	m_CurrChronoBtIfHigher = m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamChronoBtIfHigherS : m_QueryCurr == TQueryType::QUERY_INC_NORMAL ? m_ParamChronoBtIfHigherN : m_ParamChronoBtIfHigherInit;
	m_ConfsSinceNewInv = m_Stat.m_Conflicts;
}

void CTopi::Backtrack(TUV decLevel, bool isBCPBacktrack, bool reuseTrail, bool isAPICall)
{	
	//	cout << "\tc b <BacktrackLevel>" << endl;
	if (m_DumpFile && isAPICall) (*m_DumpFile) << "b " << decLevel << endl;

	if (decLevel >= m_DecLevel)
	{
		return;
	}	

	if (m_ParamReuseTrail)
	{
		m_ReuseTrail.clear();
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
		UnassignVar(m_TrailEnd, reuseTrail);
	}
		
	m_DecLevel = decLevel;

	assert(NV(2) || P("***** Backtracked to " + to_string(m_DecLevel) + "\n"));
	assert(NV(2) || !m_ParamReuseTrail || P(SReuseTrail()) + "\n");
}
