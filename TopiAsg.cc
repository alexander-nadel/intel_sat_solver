// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#include <functional>
#include <string>
#include <bit>
#include <iterator> 
#include "Topi.hpp"
#include "SetInScope.h"

using namespace Topor;
using namespace std;

bool CTopi::DebugLongImplicationInvariantHolds(TULit l, TUV decLevel, TUInd parentClsIndIfLong)
{
	// Parent clause is well-defined
	assert(parentClsIndIfLong != BadClsInd);
	// Implied literal is well-defined
	assert(l != BadULit);
	// Implied literal is unassigned
	assert(!IsAssigned(l));
		
	// Get the clause span
	const span<TULit> cls = Cls(parentClsIndIfLong);
	
	// The implied literal l must be one of the first literals in the clause
	const bool lOneOfFirstLits = l == cls[0] || l == cls[1];
	assert(lOneOfFirstLits);

	// The second WL in the clause must be assigned false
	const TULit secondLit = l == cls[0] ? cls[1] : cls[0];
	assert(IsFalsified(secondLit));

	// Our literal must be implied at the same level as the second WL
	assert(decLevel == GetAssignedDecLevel(secondLit));

	// The decision level of the implication is the highest in the clause
	const auto highestNonWLDecLevel = GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt(cls, 2));
	assert(decLevel >= highestNonWLDecLevel);

	return parentClsIndIfLong != BadClsInd && l != BadULit && !IsAssigned(l) && lOneOfFirstLits && IsFalsified(secondLit) && decLevel == GetAssignedDecLevel(secondLit) && decLevel >= highestNonWLDecLevel;
}

bool CTopi::Assign(TULit l, TUInd parentClsInd, TULit otherWatch, TUV decLevel, bool toPropagate)
{
	assert(parentClsInd == BadClsInd || DebugLongImplicationInvariantHolds(l, decLevel, parentClsInd));
	++m_Stat.m_Assignments;

	const TUVar v = GetVar(l);
	assert(v < m_VarInfo.cap());

	const auto isAssigned = IsAssigned(l);
	const auto isNegated = IsAssignedNegated(l);
	if (unlikely(isAssigned))
	{
		// Ignore if already assigned the same value; return false (contradiction), if assigned the opposite value
		return isNegated;
	}

	// It is either that: (1) The trail is empty, or (2) The level is 0, or (3) A new level is started, or (4) The level is non-empty
	// So, there cannot be an empty intermediate level
	assert(m_TrailEnd == BadUVar || decLevel == 0 || decLevel == m_DecLevel || m_TrailLastVarPerDecLevel[decLevel] != BadUVar);

	TUVar trailPrev = m_TrailLastVarPerDecLevel[decLevel] == BadUVar && decLevel != 0 ? m_TrailEnd : m_TrailLastVarPerDecLevel[decLevel];	
	TUVar trailNext = BadUVar;

	if (unlikely(trailPrev == BadUVar))
	{
		trailNext = m_TrailStart;
		m_TrailStart = v;
	}
	else
	{
		trailNext = m_VarInfo[trailPrev].m_TrailNext;
		m_VarInfo[trailPrev].m_TrailNext = v;
	}

	if (trailNext != BadUVar)
	{
		m_VarInfo[trailNext].m_TrailPrev = v;
	}
	else
	{
		m_TrailEnd = v;
	}

	if (m_ParamPhaseBoostFlippedForced && IsForced(v) && m_PolarityInfo[v].GetNextPolarityIsNegated() != IsNeg(l))
	{
		UpdateScoreVar(v);
	}

	m_AssignmentInfo[v].Assign(IsNeg(l), parentClsInd, otherWatch);
	m_VarInfo[v].Assign(parentClsInd, otherWatch, decLevel, trailPrev, trailNext);

	m_TrailLastVarPerDecLevel[decLevel] = v;

	if (toPropagate)
	{
		ToPropagatePushBack(l);		
	}	

	++m_AssignedVarsNum;

	if (m_AssignmentInfo[v].m_IsAssump && IsAssumpFalsifiedGivenVar(v) && 
		(m_EarliestFalsifiedAssump == BadULit || !IsAssigned(m_EarliestFalsifiedAssump) || GetAssignedDecLevel(l) < GetAssignedDecLevel(m_EarliestFalsifiedAssump)))
	{
		m_LatestEarliestFalsifiedAssump = m_EarliestFalsifiedAssump = GetAssumpLitForVar(v);
		m_LatestEarliestFalsifiedAssumpSolveInv = m_Stat.m_SolveInvs;
	}	

	if (m_ParamCustomBtStrat > 0 && m_VsidsHeap.var_score_exists(v))
	{
		const auto currScore = m_VsidsHeap.get_var_score(v);

		if (decLevel >= m_BestScorePerDecLevel.cap())
		{
			assert(decLevel <= GetNextVar());
			ReserveExactly(m_BestScorePerDecLevel, GetNextVar(), 0, "m_ParamCustomBtStrat in Assign");
			if (unlikely(IsUnrecoverable())) return false;
		}

		if (IsAssignedDecVar(v) || currScore > m_BestScorePerDecLevel[decLevel])
		{
			m_BestScorePerDecLevel[decLevel] = currScore;
			assert(NV(2) || P("m_BestScorePerDecLevel[" + to_string(decLevel) + "] updated to " + to_string(currScore) + " in Assign\n"));
		}		
		assert(m_ParamAssertConsistency < 1 || CalcMaxDecLevelScore(decLevel) == m_BestScorePerDecLevel[decLevel]);

	}

	return false;
}

void CTopi::UnassignVar(TUVar v, bool reuseTrail)
{
	assert(v < m_VarInfo.cap());
	assert(m_AssignmentInfo[v].m_IsAssigned);

	if (m_VarInfo[v].m_TrailNext != BadUVar)
	{
		m_VarInfo[m_VarInfo[v].m_TrailNext].m_TrailPrev = m_VarInfo[v].m_TrailPrev;
	}
	else
	{
		assert(m_TrailEnd == v);
		m_TrailEnd = m_VarInfo[v].m_TrailPrev;
	}
	if (m_VarInfo[v].m_TrailPrev != BadUVar)
	{
		m_VarInfo[m_VarInfo[v].m_TrailPrev].m_TrailNext = m_VarInfo[v].m_TrailNext;
	}
	else
	{
		assert(m_TrailStart == v);
		m_TrailStart = m_VarInfo[v].m_TrailNext;
	}

	const TUV lDecLevel = GetAssignedDecLevelVar(v);

	if (m_TrailLastVarPerDecLevel[lDecLevel] == v)
	{
		if (m_VarInfo[v].m_TrailPrev == BadUVar || GetAssignedDecLevelVar(m_VarInfo[v].m_TrailPrev) != lDecLevel)
		{
			m_TrailLastVarPerDecLevel[lDecLevel] = BadUVar;
		}
		else
		{
			m_TrailLastVarPerDecLevel[lDecLevel] = m_VarInfo[v].m_TrailPrev;
		}
	}

	if (reuseTrail && lDecLevel != m_DecLevel)
	{
		m_AssignmentInfo[v].m_IsLastParentBin = m_AssignmentInfo[v].IsAssignedBinary();
		m_ReuseTrail.emplace_back(TReuseTrail(GetAssignedLitForVar(v), m_VarInfo[v].m_ParentClsInd));
	}

	m_AssignmentInfo[v].Unassign();

	m_VsidsHeap.reinsert_if_not_in_heap(v);

	if (unlikely(GetVar(m_FlippedLit) == v))
	{
		m_FlippedLit = BadULit;
	}

	--m_AssignedVarsNum;
}

void CTopi::Unassign(TULit l)
{
	const TUVar v = GetVar(l);	
	UnassignVar(v);
}

void CTopi::BoostScore(TLit vExternal, double value)
{	
	// cout << "\tc lb <BoostScoreLit> <Mult>" << endl;
	if (m_DumpFile) (*m_DumpFile) << "lb " << vExternal << " " << value << endl;
	
	HandleIncomingUserVar(vExternal);
	
	if (unlikely(IsUnrecoverable())) return;
	
	const TULit l = E2I(vExternal);
	const TUVar v = GetVar(l);
	UpdateScoreVar(v, value);
}

void CTopi::FixPolarityInternal(TULit l, bool onlyOnce)
{
	const TUVar v = GetVar(l);
	if (!m_PolarityInfoActivated || v >= m_PolarityInfo.cap())
	{
		m_PolarityInfoActivated = true;
		ReserveExactly(m_PolarityInfo, GetNextVar(), 0, "m_PolarityInfo in FixPolarityInternal");
	}
	if (unlikely(IsUnrecoverable())) return;

	m_PolarityInfo[v] = TPolarityInfo(!onlyOnce, IsNeg(l));
}

void CTopi::FixPolarity(TLit lExternal, bool onlyOnce)
{
	// cout << "\tc lf <FixPolarityLit> <OnlyOnce>" << endl;
	if (m_DumpFile) (*m_DumpFile) << "lf " << lExternal << " " << (int)onlyOnce << endl;
	const TLit vExternal = ExternalLit2ExternalVar(lExternal);
	
	HandleIncomingUserVar(vExternal);

	if (unlikely(IsUnrecoverable())) return;
	
	const TULit l = E2I(lExternal);

	FixPolarityInternal(l, onlyOnce);
}

void CTopi::ClearUserPolarityInfoInternal(TUVar v)
{
	if (m_PolarityInfoActivated)
	{
		if (v >= m_PolarityInfo.cap())
		{
			ReserveExactly(m_PolarityInfo, GetNextVar(), 0, "m_PolarityInfo in ClearUserPolarityInfoInternal");
		}
		m_PolarityInfo[v].Clear();
	}
}

void CTopi::ClearUserPolarityInfo(TLit vExternal)
{
	//	cout << "\tc lc <ClearUserPolarityInfoLit>" << endl;
	if (m_DumpFile) (*m_DumpFile) << "lc " << vExternal << endl;

	HandleIncomingUserVar(vExternal);

	if (unlikely(IsUnrecoverable())) return;

	if (m_PolarityInfoActivated)
	{
		const TULit l = E2I(vExternal);
		const TUVar v = GetVar(l);
		ClearUserPolarityInfoInternal(v);
	}
}

bool CTopi::TraiAssertConsistency()
{
	for (auto [nextIndStartDecLevel, v] = make_tuple(false, m_TrailStart); v != BadUVar; v = m_VarInfo[v].m_TrailNext)
	{
		[[maybe_unused]] const TULit l = GetAssignedLitForVar(v);
		[[maybe_unused]] const TUVar vTrailNext = m_VarInfo[v].m_TrailNext;
		
		[[maybe_unused]] const bool c1 = (vTrailNext == BadUVar && m_TrailEnd == v) || (vTrailNext != BadUVar && m_VarInfo[vTrailNext].m_TrailPrev == v);
		assert(c1 || P("***** TrailAssertConsistency failure: " + STrail() + "\n"));
		assert(c1);
		
		[[maybe_unused]] const TUV vDecLevel = GetAssignedDecLevelVar(v);
		[[maybe_unused]] const TUV nextDecLevel = vTrailNext == BadUVar ? numeric_limits<TUV>::max() : GetAssignedDecLevelVar(vTrailNext);

		[[maybe_unused]] const bool c2 = vDecLevel == nextDecLevel || m_TrailLastVarPerDecLevel[vDecLevel] == v;
		assert(c2 || P("***** TrailAssertConsistency failure 2: " + STrail() + "\n"));
		assert(c2);
	}

	/*[[maybe_unused]] auto vvSpan = m_VisitedVars.get_span();
	[[maybe_unused]] auto rvSpan = m_RootedVars.get_span();

	for (TUVar v = 1; v < GetNextVar(); ++v)
	{
		[[maybe_unused]]  auto& ai = m_AssignmentInfo[v];
		assert(!ai.m_Visit || find(vvSpan.begin(), vvSpan.end(), v) != vvSpan.end());
		assert(!ai.m_Root || find(rvSpan.begin(), rvSpan.end(), v) != rvSpan.end());
	}

	for (TUVar v : vvSpan)
	{
		[[maybe_unused]] auto& ai = m_AssignmentInfo[v];
		assert(ai.m_Visit);
	}

	for (TUVar v : rvSpan)
	{
		[[maybe_unused]] auto& ai = m_AssignmentInfo[v];
		assert(ai.m_Root);
	}*/

	return true;
}
