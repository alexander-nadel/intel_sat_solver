// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"
#include "SetInScope.h"

using namespace Topor;
using namespace std;

void CTopi::ReserveVarAndLitData()
{
	ReserveExactly(m_Watches, GetNextLit(), 0, "m_Watches in ReserveVarAndLitData");
	ReserveExactly(m_AssignmentInfo, GetNextVar(), 0, "m_AssignmentInfo in ReserveVarAndLitData");
	if (m_PolarityInfoActivated) ReserveExactly(m_PolarityInfo, GetNextVar(), 0, "m_PolarityInfo in ReserveVarAndLitData");
	ReserveExactly(m_VarInfo, GetNextVar(), 0, "m_VarInfo in ReserveVarAndLitData");
	ReserveExactly(m_ToPropagate, GetNextVar(), "m_ToPropagate in ReserveVarAndLitData");
	ReserveExactly(m_TrailLastVarPerDecLevel, GetNextVar(), BadUVar, "m_TrailLastVarPerDecLevel in ReserveVarAndLitData");
	ReserveExactly(m_VsidsHeap, GetNextVar(), "m_VsidsHeap in ReserveVarAndLitData");
	ReserveExactly(m_HandyLitsClearBefore[0], GetNextVar(), "m_HandyLitsCleanBefore[0] in ReserveVarAndLitData");
	if (m_ParamFlippedRecordingMaxLbdToRecord != 0) ReserveExactly(m_HandyLitsClearBefore[1], GetNextVar(), "m_HandyLitsCleanBefore[1] in ReserveVarAndLitData");
	ReserveExactly(m_VisitedVars, GetNextVar(), "m_VisitedVars in ReserveVarAndLitData");
	ReserveExactly(m_DecLevelsLastAppearenceCounter, GetNextVar(), 0, "m_DecLevelsLastAppearenceCounter in ReserveVarAndLitData");
	if (m_ParamReuseTrail) ReserveExactly(m_ReuseTrail, GetNextVar(), "m_TrailToReuse in ReserveVarAndLitData");	
	if (UseI2ELitMap()) ReserveExactly(m_I2ELitMap, GetNextVar(), "m_I2ELitMap in ReserveVarAndLitData");
	if (IsCbLearntOrDrat()) ReserveExactly(m_UserCls, GetNextVar(), "m_UserCls in ReserveVarAndLitData");
	if (m_ParamOnTheFlySubsumptionParentMinGlueToDisable > 0) ReserveExactly(m_CurrClsCounters, GetNextVar(), 0, "m_CurrClsCounters in ReserveVarAndLitData");
	if (m_ParamRestartStrategyInit == RESTART_STRAT_NUMERIC || m_ParamRestartStrategyS == RESTART_STRAT_NUMERIC || m_ParamRestartStrategyN == RESTART_STRAT_NUMERIC) ReserveExactly(m_RstNumericLocalConfsSinceRestartAtDecLevelCreation, GetNextVar(), 0, "m_RstArithLocalConfsSinceRestartAtDecLevelCreation in ReserveVarAndLitData");
	if (m_ParamCustomBtStrat > 0) ReserveExactly(m_BestScorePerDecLevel, GetNextVar(), 0, "m_ParamCustomBtStrat in ReserveVarAndLitData");
}

void CTopi::RemoveVarAndLitData(TUVar v)
{
	assert(!IsAssignedVar(v));
	for (uint8_t wInd = 0; wInd < 2; ++wInd)
	{
		TWatchInfo& wi = m_Watches[GetLit(v, (bool)wInd)];
		if (!wi.IsEmpty())
		{
			MarkWatchBufferChunkDeleted(wi);
		}		
	}	
}

void CTopi::MoveVarAndLitData(TUVar vFrom, TUVar vTo)
{
	assert(!IsAssignedVar(vTo));

	RemoveVarAndLitData(vTo);
	m_Watches[GetLit(vTo, false)] = move(m_Watches[GetLit(vFrom, false)]);
	m_Watches[GetLit(vTo, true)] = move(m_Watches[GetLit(vFrom, true)]);
	m_AssignmentInfo[vTo] = move(m_AssignmentInfo[vFrom]);
	if (m_PolarityInfoActivated) m_PolarityInfo[vTo] = move(m_PolarityInfo[vFrom]);
	m_VarInfo[vTo] = move(m_VarInfo[vFrom]);
	if (IsAssignedVar(vFrom))
	{
		if (m_VarInfo[vTo].m_TrailPrev != BadUVar)
		{
			m_VarInfo[m_VarInfo[vTo].m_TrailPrev].m_TrailNext = vTo;
		}

		if (m_VarInfo[vTo].m_TrailNext != BadUVar)
		{
			m_VarInfo[m_VarInfo[vTo].m_TrailNext].m_TrailPrev = vTo;
		}

		if (m_TrailStart == vFrom)
		{
			m_TrailStart = vTo;
		}

		if (m_TrailEnd == vFrom)
		{
			m_TrailEnd = vTo;
		}

		if (m_TrailLastVarPerDecLevel[GetAssignedDecLevelVar(vFrom)] == vFrom)
		{
			m_TrailLastVarPerDecLevel[GetAssignedDecLevelVar(vFrom)] = vTo;
		}
	}

	assert(m_ToPropagate.empty());

	// Will have to replace in heap later!
	m_VsidsHeap.replace_pos_score_vars(vFrom, vTo);

	if (UseI2ELitMap()) m_I2ELitMap[vTo] = move(m_I2ELitMap[vFrom]);
}

void CTopi::RecordDeletedLitsFromCls(TUV litsNum)
{
	m_Stat.RecordDeletedLitsFromCls(litsNum);
	m_BWasted += litsNum;
}

void CTopi::DeleteLitFromCls(TUInd clsInd, TULit l)
{
	auto cls = Cls(clsInd);
	assert(cls.size() > 3);

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
	
	// Creating or extending a deleted chunk
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
	if (IsCbLearntOrDrat())
	{
		cls = Cls(clsInd);
		NewLearntClsApplyCbLearntDrat(cls);
	}
}

void CTopi::DeleteBinaryCls(const span<TULit> binCls)
{
	assert(binCls.size() == 2);
	WLRemoveBinaryWatch(binCls[0], binCls[1]);
	WLRemoveBinaryWatch(binCls[1], binCls[0]);
	m_Stat.DeleteBinClauses(1);
}

void CTopi::DeleteCls(TUInd clsInd, array<TULit, 2>* newBinCls)
{
	auto cls = Cls(clsInd);

	if (unlikely(clsInd == m_FirstLearntClsInd))
	{
		assert(ClsGetIsLearnt(clsInd));
		m_FirstLearntClsInd = ClsEnd(clsInd);	
		while (m_FirstLearntClsInd < m_BNext && ClsChunkDeleted(m_FirstLearntClsInd))
		{
			m_FirstLearntClsInd = ClsEnd(m_FirstLearntClsInd);
		}
		assert(m_FirstLearntClsInd == m_BNext || ClsGetIsLearnt(clsInd));
	}
	
	m_BWasted += (TUInd)cls.size() + ClsLitsStartOffset(ClsGetIsLearnt(clsInd), ClsGetIsOversized(clsInd));

	m_Stat.DeleteClause(cls.size(), ClsGetIsLearnt(clsInd));
	for (uint8_t currWatchI = 0; currWatchI <= 1; ++currWatchI)
	{
		size_t clsWlInd = WLGetLongWatchInd(cls[currWatchI], clsInd);
		assert(clsWlInd != numeric_limits<size_t>::max());
		WLRemoveLongWatch(cls[currWatchI], clsWlInd);
	}

	if (ClsGetIsLearnt(clsInd))
	{
		m_B[clsInd] = (TULit)cls.size() + (TULit)ClsLitsStartOffset(ClsGetIsLearnt(clsInd), ClsGetIsOversized(clsInd)) - (TULit)1;		
	}
	assert(!ClsGetIsLearnt(clsInd));

	m_B[clsInd + 2] = BadULit;
	if (newBinCls != nullptr)
	{
		m_B[clsInd + 1] = (*newBinCls)[0];
		m_B[clsInd + 3] = (*newBinCls)[1];
	}
}

void CTopi::ClsDeletionInit()
{
	if (!m_ClsDelInfo.m_Initialized)
	{
		m_ClsDelInfo.m_ConfsPrev = 0;
		m_ClsDelInfo.m_TriggerNext = (uint64_t)m_ParamClsDelLowTriggerInit;
		m_ClsDelInfo.m_TriggerInc = (uint64_t)m_ParamClsDelLowTriggerInc;
		m_ClsDelInfo.m_TriggerMult = m_ParamClsDelLowTriggerMult;
		m_ClsDelInfo.m_TriggerMax = (uint64_t)m_ParamClsDelLowTriggerMax;
		m_ClsDelInfo.m_FracToDelete = m_ParamClsDelLowFracToDelete;		
		m_ClsDelInfo.m_GlueNeverDelete = (uint8_t)m_ParamClsDelGlueNeverDelete;
		m_ClsDelInfo.m_Clusters = (uint8_t)m_ParamClsDelGlueClusters;
		m_ClsDelInfo.m_MaxClusteredGlue = (uint8_t)m_ParamClsDelGlueMaxCluster;

		m_ClsDelInfo.m_Initialized = true;
	}	
}

void CTopi::ClsDeletionDecayActivity()
{
	if (m_ParamClsDelStrategy)
	{
		m_ClsDelOneTierActivityIncrease *= (1 / m_ParamClsLowDelActivityDecay);
	}
}

void CTopi::ClsDelNewLearntOrGlueUpdate(TUInd clsInd, TUV prevGlue)
{
	if (m_ParamClsDelStrategy)
	{
		const auto glue = ClsGetGlue(clsInd);
		const bool glueDecreased = glue < prevGlue;

		float currActivity = ClsGetActivity(clsInd);
		currActivity += (float)m_ClsDelOneTierActivityIncrease;
		ClsSetActivity(clsInd, currActivity);
		if (currActivity > 1e20)
		{
			// Rescale:
			for (TUInd clsIndLocal = m_FirstLearntClsInd; clsIndLocal < m_BNext; clsIndLocal = ClsEnd(clsIndLocal))
			{
				if (ClsChunkDeleted(clsIndLocal) || !ClsGetIsLearnt(clsIndLocal))
				{
					continue;
				}
				const auto clsRescaledActivity = ClsGetActivity(clsIndLocal);
				ClsSetActivity(clsIndLocal, clsRescaledActivity * 1e-20f);
			}

			m_ClsDelOneTierActivityIncrease *= 1e-20;
		}

		if (glueDecreased && glue <= GetGlueMinFreeze())
		{
			ClsSetSkipdel(clsInd, true);
		}
	}
}

void CTopi::DeleteClausesIfRequired()
{
	if (!m_ParamClsDelStrategy || IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT || 
		(ClsDeletionTrigger() < m_ClsDelInfo.m_TriggerNext) || (m_ParamClsDelDeleteOnlyAssumpDecLevel && m_DecLevel > m_DecLevelOfLastAssignedAssumption))
	{
		return;
	}

	assert(NV(1) || P("Clause deletion started for priority\n"));
	assert(NV(2) || P("The trail: " + STrail() + "\n"));

	assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(true));
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || DebugAssertWaste());

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		++m_Stat.m_ClssDel;
		
		m_ClsDelInfo.m_ConfsPrev = m_Stat.m_Conflicts;

		if (unlikely(m_FirstLearntClsInd != numeric_limits<decltype(m_FirstLearntClsInd)>::max() && m_FirstLearntClsInd >= m_BNext))
		{
			m_FirstLearntClsInd = numeric_limits<decltype(m_FirstLearntClsInd)>::max();
		}

		if (m_ParamReuseTrail)
		{
			m_ReuseTrail.clear();
		}

		assert(NV(1) || P("Clause deletion finished for priority\n"));
		assert(NV(2) || P("The trail: " + STrail() + "\n"));	

		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
		assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(true));
		assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || DebugAssertWaste());
	});


	static constexpr bool sizetGreaterThenActs = sizeof(size_t) > sizeof(decltype(m_Stat.m_ActiveLongLearntClss));
	const size_t learntsSz = sizetGreaterThenActs || m_Stat.m_ActiveLongLearntClss < (decltype(m_Stat.m_ActiveLongLearntClss))numeric_limits<size_t>::max() ? (size_t)m_Stat.m_ActiveLongLearntClss : (decltype(m_Stat.m_ActiveLongLearntClss))numeric_limits<size_t>::max();
	CVector<TUInd> learnts(learntsSz);
	if (learnts.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::DeleteClausesIfRequired: couldn't allocate learnts");
		return;
	}

	size_t undeletableButNotTouched = 0;
	// Mark parent clauses as skipped for this round
	for (TUVar v = m_TrailStart; v != BadUVar; v = GetTrailNextVar(v))
	{
		const auto& ai = m_AssignmentInfo[v];
		const auto& vi = m_VarInfo[v];

		assert(ai.m_IsAssigned);

		const TUInd clsInd = vi.m_ParentClsInd;
		// The "(vi.m_DecLevel > 0 || clsInd + 2 < m_B.cap())" part (and also "if (vi.m_DecLevel > 0)" below) is a work-around, 
		// so that parents of globally satisfied variables wouldn't be actually visited (with ClsSetSkipdel), 
		// yet they will be counted for clause deletion heuristic. Skipping this condition causes a correctness bug, since global parent aren't maintained,
		// while dealing with the global parents more aggressively (e.g., nullifying them in Assign) hurts the clause deletion heuristic
		if (!ai.IsAssignedBinary() && (vi.m_DecLevel > 0 || clsInd + 2 < m_B.cap()) && clsInd != BadClsInd && ClsGetIsLearnt(clsInd))
		{
			assert(!ClsChunkDeleted(clsInd));
			
			if (!ClsGetSkipdel(clsInd))
			{
				++undeletableButNotTouched;
				if (vi.m_DecLevel > 0)
				{
					ClsSetSkipdel(clsInd, true);
				}				
			}
		}
	}

	for (TUInd clsInd = m_FirstLearntClsInd; clsInd < m_BNext; clsInd = ClsEnd(clsInd))
	{
		if (ClsChunkDeleted(clsInd) || !ClsGetIsLearnt(clsInd) || ClsGetGlue(clsInd) <= m_ClsDelInfo.m_GlueNeverDelete)
		{
			if (!ClsChunkDeleted(clsInd) && ClsGetIsLearnt(clsInd))
			{
				assert(ClsGetGlue(clsInd) <= m_ClsDelInfo.m_GlueNeverDelete);
				++undeletableButNotTouched;
			}
			continue;
		}
		
		if (ClsGetSkipdel(clsInd))
		{
			ClsSetSkipdel(clsInd, false);
			continue;
		}


		learnts.push_back(clsInd);
	}
	
	auto learntsSpan = learnts.get_span();

	if (m_ClsDelInfo.m_Clusters == 0)
	{
		sort(learntsSpan.begin(), learntsSpan.end(), [&](TUInd clsInd1, TUInd clsInd2)
		{
			return ClsGetActivity(clsInd1) < ClsGetActivity(clsInd2) ||
				((ClsGetActivity(clsInd1) == ClsGetActivity(clsInd2)) && (ClsGetGlue(clsInd1) > ClsGetGlue(clsInd2)));
		});
	}
	else
	{
		sort(learntsSpan.begin(), learntsSpan.end(), [&](TUInd clsInd1, TUInd clsInd2)
		{
			const array<TUV, 2> glues = { ClsGetGlue(clsInd1), ClsGetGlue(clsInd2) };
			const array<uint8_t, 2> clusters = { m_ClsDelInfo.GetCluster(glues[0]), m_ClsDelInfo.GetCluster(glues[1]) };
			if (clusters[0] != clusters[1])
			{
				return clusters[0] > clusters[1];
			}

			const array<float, 2> acts = { ClsGetActivity(clsInd1), ClsGetActivity(clsInd2) };			
			return acts[0] < acts[1] || (acts[0] == acts[1] && glues[0] > glues[1]);
		});
	}
	
	
	m_ClsDelInfo.m_TriggerNext = ClsDeletionTrigger() + m_ClsDelInfo.m_TriggerInc;
	const double nextTriggerIncD = (double)m_ClsDelInfo.m_TriggerInc * m_ClsDelInfo.m_TriggerMult;
	if (nextTriggerIncD >= (double)m_ClsDelInfo.m_TriggerMax)
	{
		m_ClsDelInfo.m_TriggerInc = m_ClsDelInfo.m_TriggerMax;
	}
	else
	{
		m_ClsDelInfo.m_TriggerInc = (uint64_t)nextTriggerIncD;
	}	

	// Now mark bad learnt clauses as deleted
	size_t iLastExcl = (decltype(iLastExcl))((float)(ClsDeletionTrigger() - undeletableButNotTouched) * m_ClsDelInfo.m_FracToDelete);
	if (iLastExcl > learnts.size())
	{
		iLastExcl = learnts.size();
	}
	for (size_t i = 0; i < iLastExcl; ++i)
	{
		const TUInd clsInd = learnts[i];
		assert(!ClsChunkDeleted(clsInd));
		assert(ClsGetIsLearnt(clsInd));

		DeleteCls(clsInd);
		if (unlikely(m_FirstLearntClsInd == ClsEnd(clsInd)))
		{
			m_FirstLearntClsInd = ClsEnd(clsInd);
			while (m_FirstLearntClsInd < m_BNext && (ClsChunkDeleted(m_FirstLearntClsInd) || !ClsGetIsLearnt(m_FirstLearntClsInd)))
			{
				m_FirstLearntClsInd = ClsEnd(m_FirstLearntClsInd);
			}
			assert(m_FirstLearntClsInd >= m_BNext || ClsGetIsLearnt(m_FirstLearntClsInd));
		}
	}

}

void CTopi::SimplifyIfRequired()
{
	if (m_DecLevel > m_DecLevelOfLastAssignedAssumption || m_TrailLastVarPerDecLevel[0] == m_LastGloballySatisfiedLitAfterSimplify || m_ImplicationsTillNextSimplify > 0 || IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT)
	{
		return;
	}

	assert(NV(1) || P("Simplification started\n"));
	assert(NV(2) || P("The trail: " + STrail() + "\n"));

	assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(true));
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || DebugAssertWaste());

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		m_LastGloballySatisfiedLitAfterSimplify = m_TrailLastVarPerDecLevel[0];
		m_ImplicationsTillNextSimplify = (int64_t)m_Stat.GetActiveLongClsLen();
		++m_Stat.m_Simplifies;
		if (unlikely(m_FirstLearntClsInd != numeric_limits<decltype(m_FirstLearntClsInd)>::max() && m_FirstLearntClsInd >= m_BNext))
		{
			m_FirstLearntClsInd = numeric_limits<decltype(m_FirstLearntClsInd)>::max();
		}

		assert(NV(1) || P("Simplification finished\n"));
		assert(NV(2) || P("The trail: " + STrail() + "\n"));

		if (m_ParamReuseTrail)
		{
			m_ReuseTrail.clear();
		}

		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
		assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(true));
		assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || DebugAssertWaste());
	});

	// We assume that there is no conflict and no unit propagation now

	// We sift variable indices, if there are at least two globally assigned variables
	// We need to always leave one globally assigned variable to be able to map external globally assigned variables to it
	const bool siftVarIndices = m_TrailLastVarPerDecLevel[0] != BadUVar && GetTrailPrevVar(m_TrailLastVarPerDecLevel[0]) != BadUVar;
	TUVar newLastExistingVar = m_LastExistingVar;
	TUVar globallySatifiedVarLowestIndex = BadUVar;

	if (siftVarIndices)
	{
		// Visited will contain all the globally assigned variables
		for (TUVar currV = m_TrailLastVarPerDecLevel[0]; currV != BadUVar; currV = GetTrailPrevVar(currV))
		{
			MarkVisitedVar(currV);			
		}

		// Sorting the visited, so that m_VisitedVars.back() would have the smallest globaly assigned variable index 
		auto vvSpan = m_VisitedVars.get_span();
		sort(vvSpan.begin(), vvSpan.end(), greater<TUVar>());

		globallySatifiedVarLowestIndex = VisitedPopBack();
		assert(m_VisitedVars.size() > 0 && globallySatifiedVarLowestIndex < m_VisitedVars.back());
		const TULit globallySatifiedLitLowestIndex = GetAssignedLitForVar(globallySatifiedVarLowestIndex);

		// m_HandyLitsClearBefore[0] will hold the new variable indices for the sifted and removed (highest) variable indices
		m_HandyLitsClearBefore[0].memset(BadULit);

		auto GetGlobalSatLit = [&](TUVar v)
		{
			const TULit l = GetLit(v, false);
			return IsGloballySatisfied(l) ? globallySatifiedLitLowestIndex : Negate(globallySatifiedLitLowestIndex);
		};
		m_HandyLitsClearBefore[0][globallySatifiedVarLowestIndex] = GetGlobalSatLit(globallySatifiedVarLowestIndex);

		for (newLastExistingVar = m_LastExistingVar;
			(!m_VisitedVars.empty() && m_VisitedVars.back() < newLastExistingVar) ||
			(IsGloballyAssignedVar(newLastExistingVar) && m_HandyLitsClearBefore[0][newLastExistingVar] != GetGlobalSatLit(newLastExistingVar)); --newLastExistingVar)
		{
			if (IsGloballyAssignedVar(newLastExistingVar))
			{
				m_HandyLitsClearBefore[0][newLastExistingVar] = GetGlobalSatLit(newLastExistingVar);
				assert(NV(1) || P("\tGlobal variable removed: " + SVar(newLastExistingVar) + " (mapped to " + SLit(m_HandyLitsClearBefore[0][newLastExistingVar]) + ")\n"));
			}
			else
			{
				const TUVar toVar = VisitedPopBack();
				m_HandyLitsClearBefore[0][toVar] = GetGlobalSatLit(toVar);
				assert(NV(1) || P("\tGlobal variable removed: " + SVar(toVar) + " (mapped to " + SLit(m_HandyLitsClearBefore[0][toVar]) + ")\n"));
				m_HandyLitsClearBefore[0][newLastExistingVar] = GetLit(toVar, false);
				assert(NV(1) || P("\tVariable re-indexed to literal: " + SVar(newLastExistingVar) + " to " + SLit(m_HandyLitsClearBefore[0][newLastExistingVar]) + "\n"));
			}
		}

		if (m_ParamCustomBtStrat > 0 && m_BestScorePerDecLevel.cap() != 0)
		{
			if (m_ParamSimplifyGlobalLevelScoreStrat == 0)
			{
				m_BestScorePerDecLevel[0] = m_VsidsHeap.get_var_score(globallySatifiedVarLowestIndex);
			}
			else if (m_ParamSimplifyGlobalLevelScoreStrat == 1)
			{				
				m_BestScorePerDecLevel[0] = CalcMinDecLevelScore(0);
				m_VsidsHeap.set_var_score(globallySatifiedVarLowestIndex, m_BestScorePerDecLevel[0]);
			}
			else
			{
				assert(m_ParamSimplifyGlobalLevelScoreStrat == 2);
				assert(m_BestScorePerDecLevel[0] == CalcMaxDecLevelScore(0));
				m_VsidsHeap.set_var_score(globallySatifiedVarLowestIndex, m_BestScorePerDecLevel[0]);
			}
		}

		CleanVisited();
	}

	assert(NV(1) || P("Previous --> new last variable: " + to_string(m_LastExistingVar) + " --> " + to_string(newLastExistingVar) + "\n"));

	auto RetSiftedLit = [&](TULit l)
	{
		const TUVar v = GetVar(l);
		if (v > newLastExistingVar)
		{
			return IsNeg(l) ? Negate(m_HandyLitsClearBefore[0][v]) : m_HandyLitsClearBefore[0][v];
		}
		else
		{
			return l;
		}
	};

	// Go over the clause buffer
		// Delete globally falsified literals (that is, mark them for deletion for the garbage collector)
		// Delete globally satisfied clauses (that is, mark them for deletion for the garbage collector)
		// Move watches to the earliest satisfied literals for clauses satisfied by assumptions
		// Sift assumption-falsified literals towards the end of the clause in clauses, which are not satisfied
		// Sift variable indices, if required
	TUInd nextClsInd = BadClsInd;
	for (TUInd clsInd = LitsInPage; clsInd < m_BNext; clsInd = nextClsInd)
	{
		nextClsInd = ClsEnd(clsInd);

		bool isGloballySatisfied = false;
		TUV globallyFalsifiedLitsNum = 0;
		bool isAssumpSatisfied = false;
		TUV assumpFalsifiedLitsNum = 0;

		if (ClsChunkDeleted(clsInd))
		{
			if (unlikely(m_FirstLearntClsInd == clsInd))
			{
				m_FirstLearntClsInd = nextClsInd;
			}

			assert(NV(2) || P("\tChunk of size " + to_string(ClsGetSize(clsInd)) + " at " + to_string(clsInd) + " deleted!\n"));
			continue;
		}

		assert(NV(2) || P("New clause: " + SLits(Cls(clsInd)) + "\n"));

		span<TULit> cls = Cls(clsInd);
		// Binary clauses are inlined into WL's
		assert(cls.size() > 2);
		// Both the watches cannot be falsified, since there is no conflict now
		assert(!IsFalsified(cls[0]) || !IsFalsified(cls[1]));

		bool anySiftedVars = false;

		for (size_t i = 0; i < cls.size() && !isGloballySatisfied; ++i)
		{
			const TULit currLit = cls[i];
			if (IsAssigned(currLit))
			{
				const bool isSatisfied = IsSatisfied(currLit);
				const TUV decLevel = m_DecLevelOfLastAssignedAssumption == 0 ? 0 : GetAssignedDecLevel(currLit);
				if (isSatisfied)
				{
					(decLevel == 0 ? isGloballySatisfied : isAssumpSatisfied) = true;
				}
				else
				{
					++(decLevel == 0 ? globallyFalsifiedLitsNum : assumpFalsifiedLitsNum);
				}
			}

			if (siftVarIndices && !anySiftedVars && GetVar(currLit) > newLastExistingVar)
			{
				anySiftedVars = true;
			}
		}

		if (isGloballySatisfied)
		{
			assert(NV(2) || P("\tGlobally satisfied -- deleting...\n"));
			DeleteCls(clsInd);
			continue;
		}

		array<bool, 2> isCachedSetForWatch = { false, false };

		if (globallyFalsifiedLitsNum > 0)
		{
			assert(NV(2) || P("\t" + to_string(globallyFalsifiedLitsNum) + " globally falsified literals -- removing them from the clause...\n"));
			assert(globallyFalsifiedLitsNum < cls.size());
			// Having all literals but one globally falsified is impossible at this point,
			// since then the remaining literal must have been a globally satisfied one (since BCP guarantees no delayed implications at the end),
			// which would have made the clause deleted and the loop continued before reaching this point
			assert(cls.size() - globallyFalsifiedLitsNum >= 2);

			if (globallyFalsifiedLitsNum + 2 == cls.size())
			{
				// If all the literals but two are globally falsified,
				// The clause will now become binary, so we add a new binary clause and delete this one
				auto it1 = find_if(cls.begin(), cls.end(), [&](const TULit l) { return !IsGloballyFalsified(l); });
				assert(it1 != cls.end());
				auto it2 = find_if(it1 + 1, cls.end(), [&](const TULit l) { return !IsGloballyFalsified(l); });
				assert(it2 != cls.end());
				assert(find_if(it2 + 1, cls.end(), [&](const TULit l) { return !IsGloballyFalsified(l); }) == cls.end());

				array<TULit, 2> newBinCls = { *it1, *it2 };
				array<TULit, 2> newBinClsSifted = { RetSiftedLit(newBinCls[0]), RetSiftedLit(newBinCls[1]) };

				DeleteCls(clsInd, &newBinClsSifted);

				AddClsToBufferAndWatch(newBinCls, true);
				assert(NV(2) || P("\tThe clause has become binary: " + SLits(newBinCls) + "\n"));
				if (unlikely(IsUnrecoverable())) return;

				continue;
			}

			// Now we need to remove the globally falsified literals and let the clause be

			// If the globally falsified literal is one of the watches, swap it			
			for (uint8_t currWatchI = 0; currWatchI <= 1; ++currWatchI)
			{
				if (IsGloballyFalsified(cls[currWatchI]))
				{
					assert(IsSatisfied(cls[!(bool)currWatchI]));
					auto it = FindBestWLCand(cls, m_DecLevelOfLastAssignedAssumption);
					SwapWatch(clsInd, currWatchI, it);
					isCachedSetForWatch[currWatchI] = true;
					break;
				}
			}

			// Set the cached literals for watches unset by the previous loop to the 2nd watch (they might be incorrect, because some literals have been removed)
			for (uint8_t currWatchI = 0; currWatchI <= 1; ++currWatchI)
			{
				if (!isCachedSetForWatch[currWatchI])
				{
					WLSetCached(cls[currWatchI], clsInd, cls[!(bool)currWatchI]);
					isCachedSetForWatch[currWatchI] = true;
				}
			}

			// remove_if sifts the globally falsified literals towards the end of the clause, which is exactly what we need!
			[[maybe_unused]] auto itEndRemaining = remove_if(cls.begin() + 2, cls.end(), [&](TULit l) { return IsGloballyFalsified(l); });
			// Resizing our clause
			ClsSetSize(clsInd, (TUV)cls.size() - globallyFalsifiedLitsNum);
			// Renewing the span
			cls = Cls(clsInd);

			assert(NV(2) || P("\tAfter removing the globally falsified literals: " + SLits(cls) + "\n"));

			// Creating a deleted chunk out of the removed literals			
			const TUInd deletedChunkInd = ClsEnd(clsInd);
			const auto deletedChunkSize = globallyFalsifiedLitsNum - 1;
			m_B[deletedChunkInd] = deletedChunkSize;
			assert(ClsGetSize(deletedChunkInd) == deletedChunkSize);
			assert(!ClsGetIsLearnt(deletedChunkInd));
			if (deletedChunkSize > 2)
			{
				m_B[deletedChunkInd + 2] = BadULit;
			}
			// Updating the number of literal in long clauses
			RecordDeletedLitsFromCls(globallyFalsifiedLitsNum);
		}

		if (isAssumpSatisfied)
		{
			// Make cls[0] contain the lowest satisfied literal
			{
				auto lowestSatisfiedIt = GetSatisfiedLitLowestDecLevelIt(cls);
				assert(IsSatisfied(*lowestSatisfiedIt));
				if (lowestSatisfiedIt != cls.begin())
				{
					if (lowestSatisfiedIt == cls.begin() + 1)
					{
						swap(cls[0], cls[1]);
					}
					else
					{
						SwapWatch(clsInd, 0, lowestSatisfiedIt);
						WLSetCached(cls[1], clsInd, cls[0]);
						isCachedSetForWatch[false] = isCachedSetForWatch[true] = true;
					}
				}
			}

			// Make cls[1] contain the second lowest satisfied literal, if any
			{
				auto lowestSatisfiedIt = GetSatisfiedLitLowestDecLevelIt(cls, 1);
				if (IsSatisfied(*lowestSatisfiedIt) && lowestSatisfiedIt != cls.begin() + 1)
				{
					SwapWatch(clsInd, 1, lowestSatisfiedIt);
					WLSetCached(cls[0], clsInd, cls[1]);
					isCachedSetForWatch[false] = isCachedSetForWatch[true] = true;
				}
			}

			assert(NV(2) || P("\tSatisfied by assumption propagation; the new clause: " + SLits(cls) + "\n"));
		}

		// If there are some assumption-falsified literals and the clause is not assumption-satisfied,
		// we sift the assumption-falsified literals towards the end. 
		// Note that if the clause is assumption-satisfied, the rest of the clause won't be visited anyway (with the current assumptions), so we pass in that case
		if (assumpFalsifiedLitsNum > 0 && !IsSatisfied(cls[0]) && !IsSatisfied(cls[1]))
		{
			// remove_if wouldn't work, since it doesn't move the removed elements towards the end of the span
			// so we created move_it, which is identical to remove_it, expect that the  removed elements are moved towards the end of the span,
			// but their order is not preserved. The order of the remaining elements is still preserved
			[[maybe_unused]] auto itEndRemaining = move_if(cls.begin() + 2, cls.end(), [&](TULit l) { return IsFalsified(l); });
			assert(cls.end() - itEndRemaining == assumpFalsifiedLitsNum);
			assert(NV(2) || P("\tSome literals falsified by assumption propagation; the new clause: " + SLits(cls) + "\n"));
		}

		if (siftVarIndices && anySiftedVars)
		{
			for (uint8_t currWatchI = 0; currWatchI <= 1; ++currWatchI)
			{
				const bool cw = (bool)currWatchI;
				if (!isCachedSetForWatch[cw])
				{
					WLSetCached(cls[cw], clsInd, cls[!cw]);
				}
			}

			transform(cls.begin(), cls.end(), cls.begin(), [&](TULit l)
			{
				const TUVar v = GetVar(l);
				const bool isNeg = IsNeg(l);
				return v > newLastExistingVar ? (isNeg ? Negate(m_HandyLitsClearBefore[0][v]) : m_HandyLitsClearBefore[0][v]) : l;
			});

			assert(NV(2) || P("\tSifted some variable indices to: " + SLits(cls) + "\n"));
		}
	}

	// Go over the watches: delete satisfied clauses
	// Remove the globally satisfied variables, except for one and reuse their numbers by other variables. 
	// One globally satisfied variable is still required to map globally satisfied external variables into it.

	// We Mark all the relevant positive literals and Root all the relevant negative literals	
	// We also remove all the watches of the globally satisfied literals (that is, the remaining binary watches, since the long ones have already been removed),
	// and mark the chunks as deleted
	size_t binClssCountOnce(0), binClssCountTwice(0);
	for (TUVar currV = m_TrailLastVarPerDecLevel[0]; currV != BadUVar; currV = GetTrailPrevVar(currV))
	{
		const TULit currL = GetAssignedLitForVar(currV);
		TWatchInfo& wi = m_Watches[currL];
		if (!wi.IsEmpty())
		{
			// There should be no long watches for the globally satisfied literal
			assert(wi.m_LongWatches == 0);
			const span<TULit> binWatches = span<TULit>(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TULit secondLit : binWatches)
			{
				if (!IsGloballyAssignedVar(GetVar(secondLit)))
				{
					IsNeg(secondLit) ? MarkRooted(secondLit) : MarkVisited(secondLit);
					++binClssCountOnce;
				}
				else
				{
					++binClssCountTwice;
				}
			}

			// Removing all the watches (including binary watches) of the globally satisfied literal
			MarkWatchBufferChunkDeleted(wi);
			wi.m_BinaryWatches = wi.m_AllocatedEntries = 0;
		}

		// Removing all the watches (including binary watches) of the globally falsified literal
		TWatchInfo& wiNeg = m_Watches[Negate(currL)];
		if (!wiNeg.IsEmpty())
		{
			MarkWatchBufferChunkDeleted(wiNeg);
			assert(wiNeg.m_LongWatches == 0);
			binClssCountTwice += wiNeg.m_BinaryWatches;
			wiNeg.m_BinaryWatches = wiNeg.m_AllocatedEntries = 0;
		}
	}
	
	assert((binClssCountTwice & (size_t)1) == 0);
	m_Stat.DeleteBinClauses(binClssCountOnce + binClssCountTwice / 2);

	// Now, we remove any required binary watches

	auto RemoveSatisfiedLitsFromBinWatches = [&](TULit l)
	{
		assert(!IsGloballyAssignedVar(GetVar(l)));

		TWatchInfo& wi = m_Watches[l];
		span<TULit> binWatches = span<TULit>(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);

		auto itEndRemaining = remove_if(binWatches.begin(), binWatches.end(), [&](TULit otherLit) { return IsGloballySatisfied(otherLit); });
		assert(binWatches.end() - itEndRemaining != 0);
		wi.m_BinaryWatches = itEndRemaining - binWatches.begin();
	};

	for (TUVar varOfPositiveLit : m_VisitedVars.get_span())
	{
		RemoveSatisfiedLitsFromBinWatches(GetLit(varOfPositiveLit, false));
	}
	CleanVisited();

	for (TUVar varOfNegativeLit : m_RootedVars.get_span())
	{
		RemoveSatisfiedLitsFromBinWatches(GetLit(varOfNegativeLit, true));
	}
	CleanRooted();

	assert(NV(1) || P("Removed globally satisfied literals from binary\n"));

	// Now we want to sift the variable indices (to the removed-by-now globally assigned variables), if required
	// One globally assigned variable will remain in any case

	if (!siftVarIndices)
	{
		assert(NV(1) || P("No need to sift indices, exiting...\n"));
		return;
	}

	// Sift the indices
	assert(m_HandyLitsClearBefore[0].cap() - 1 == m_LastExistingVar);
	for (; m_LastExistingVar != newLastExistingVar; --m_LastExistingVar)
	{
		const TUVar vTo = GetVar(m_HandyLitsClearBefore[0][m_LastExistingVar]);
		if (vTo == globallySatifiedVarLowestIndex)
		{
			if (IsAssignedVar(m_LastExistingVar))
			{
				UnassignVar(m_LastExistingVar);
				RemoveVarAndLitData(m_LastExistingVar);
			}
		}
		else
		{
			Unassign(GetAssignedLitForVar(vTo));
			MoveVarAndLitData(m_LastExistingVar, vTo);
		}
	}

	// Mark all the variables, whose watches (binary and cached lits in long) contain the sifted indices
	for (TUVar vBefore = newLastExistingVar + 1; vBefore < m_HandyLitsClearBefore[0].cap(); ++vBefore)
	{
		const TUVar vTo = GetVar(m_HandyLitsClearBefore[0][vBefore]);
		if (IsGloballyAssignedVar(vTo))
		{
			continue;
		}

		for (uint8_t isNeg = 0; isNeg < 2; ++isNeg)
		{
			const TULit l = GetLit(vTo, (bool)isNeg);
			const TWatchInfo& wi = m_Watches[l];
			if (wi.IsEmpty())
			{
				continue;
			}
			const span<TULit> binWatches = span<TULit>(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TULit secondLit : binWatches)
			{
				IsNeg(secondLit) ? MarkRooted(secondLit) : MarkVisited(secondLit);
			}
			for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, m_W.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
			{
				const TUInd clsInd = *(TUInd*)(currLongWatchPtr + 1);
				span<TULit> cls = Cls(clsInd);
				assert(cls[0] == l || cls[1] == l);
				const TULit secondLit = cls[cls[0] == l];
				IsNeg(secondLit) ? MarkRooted(secondLit) : MarkVisited(secondLit);
			}
		}
	}

	auto SiftLitsInWatches = [&](TULit l)
	{
		const TWatchInfo& wi = m_Watches[l];
		span<TULit> binWatches = span<TULit>(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
		transform(binWatches.begin(), binWatches.end(), binWatches.begin(), [&](TULit secondLit)
		{
			return RetSiftedLit(secondLit);
		});

		for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, m_W.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
		{
			const TULit cachedLit = *currLongWatchPtr;
			*currLongWatchPtr = RetSiftedLit(cachedLit);
		}
	};

	for (TUVar varOfPositiveLit : m_VisitedVars.get_span())
	{
		TULit l = GetLit(varOfPositiveLit, false);
		SiftLitsInWatches(l);
	}
	CleanVisited();

	for (TUVar varOfNegativeLit : m_RootedVars.get_span())
	{
		TULit l = GetLit(varOfNegativeLit, true);
		SiftLitsInWatches(l);
	}
	CleanRooted();

	// Handle the trail
	m_AssignmentInfo[globallySatifiedVarLowestIndex].m_IsAssignedInBinary = false;
	m_VarInfo[globallySatifiedVarLowestIndex].m_ParentClsInd = BadClsInd;
	if (m_DecLevel > 0)
	{
		for (TUVar v = GetTrailNextVar(m_TrailLastVarPerDecLevel[0]); v != BadUVar; v = GetTrailNextVar(v))
		{
			assert(m_AssignmentInfo[v].m_IsAssigned);
			if (m_AssignmentInfo[v].m_IsAssignedInBinary)
			{
				m_VarInfo[v].m_BinOtherLit = RetSiftedLit(m_VarInfo[v].m_BinOtherLit);
			}
			else if (m_VarInfo[v].m_ParentClsInd != BadClsInd && ClsChunkDeleted(m_VarInfo[v].m_ParentClsInd))
			{
				m_AssignmentInfo[v].m_IsAssignedInBinary = true;
				span<TULit> cls = Cls(m_VarInfo[v].m_ParentClsInd);
				assert(cls.size() >= 3);
				assert(cls[1] == BadULit);
				assert(GetVar(cls[0]) == v || GetVar(cls[2]) == v);
				m_VarInfo[v].m_BinOtherLit = GetVar(cls[0]) == v ? cls[2] : cls[0];
			}
		}
	}

	// Handle the assumptions
	if (!m_Assumps.cap() == 0)
	{
		span<TULit> assumpSpan = m_Assumps.get_span_cap();

		assert(all_of(assumpSpan.begin(), assumpSpan.end(), [&](TULit assumpLit) { return !IsFalsified(assumpLit); }));

		transform(assumpSpan.begin(), assumpSpan.end(), assumpSpan.begin(), [&](TULit assumpLit)
		{
			return RetSiftedLit(assumpLit);
		});

		auto endUniquesIt = unique(assumpSpan.begin(), assumpSpan.end());
		m_Assumps.reserve_exactly(endUniquesIt - assumpSpan.begin());
	}

	auto RemovedLit2NewLit = [&](TULit l)
	{
		const auto v = GetVar(l);
		const TULit newLitForVar = m_HandyLitsClearBefore[0][v];
		if (newLitForVar == BadULit)
		{
			return l;
		}
		return IsNeg(l) ? Negate(newLitForVar) : newLitForVar;
	};

	assert(NV(2) || P("E2I before: " + SE2I() + "\n"));

	auto e2iSpan = m_E2ILitMap.get_span_cap();
	transform(e2iSpan.begin(), e2iSpan.end(), e2iSpan.begin(), [&](TULit l)
	{
		return RemovedLit2NewLit(l);
	});

	assert(NV(2) || P("E2I after: " + SE2I() + "\n"));

	// This function will shrink all the variable- and literal-dependent data structures
	ReserveVarAndLitData();

	m_Stat.UpdateMaxInternalVar(m_LastExistingVar);

	m_VsidsHeap.rebuild();

	assert(NV(1) || P("Finished renaming globally satisfied literals, but one\n"));
}

bool CTopi::DebugAssertWaste()
{
	TUInd w = 0;

	for (TUInd clsInd = LitsInPage; clsInd < m_BNext; clsInd = ClsEnd(clsInd))
	{
		if (ClsChunkDeleted(clsInd))
		{
			const auto sz = ClsGetSize(clsInd);
			w += (sz + 1);
		}
	}
	assert(m_BWasted == w);

	return true;
}

void CTopi::CompressBuffersIfRequired()
{
	if ((double)m_BWasted / (double)m_BNext <= m_ParamWastedFractionThrToDelete || IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT)
	{
		return;
	}

	assert(NV(1) || P("Compression started: wasted fraction is " + to_string((double)m_BWasted / (double)m_BNext) + " > " + to_string(m_ParamWastedFractionThrToDelete) + "\n"));
	assert(NV(2) || P("The trail: " + STrail() + "\n"));

	assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(true));
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || DebugAssertWaste());

	// ************************************
	// Compress the clause buffer
	// ************************************	

	[[maybe_unused]] const auto nextBefore = m_BNext;

	const bool isAboveGlobalDecLevel = m_DecLevel != 0;
	m_B.Compress(LitsInPage, m_BNext, [&](TUInd clsInd) { return ClsChunkDeleted(clsInd); }, [&](TUInd clsInd) { return ClsEnd(clsInd); }, [&](TUInd oldWlInd, TUInd newWlInd)
	{
		if (isAboveGlobalDecLevel)
		{
			auto UpdateParentIfRequired = [&](TULit l)
			{
				const TUVar v = GetVar(l);
				const bool isCurrClsParent = IsAssignedVar(v) && !m_AssignmentInfo[v].IsAssignedBinary() && m_VarInfo[v].m_ParentClsInd == oldWlInd;
				if (isCurrClsParent)
				{
					m_VarInfo[v].m_ParentClsInd = newWlInd;
				}
			};

			span<TULit> cls = Cls(oldWlInd);
			UpdateParentIfRequired(cls[0]);
			UpdateParentIfRequired(cls[1]);
		}

		if (unlikely(m_FirstLearntClsInd == oldWlInd))
		{
			m_FirstLearntClsInd = newWlInd;
		}
	});


	assert(nextBefore - m_BWasted == m_BNext);
	m_BWasted = 0;
	if (m_ParamReduceBuffersSizeAfterCompression && m_B.cap() > (size_t)((double)m_BNext * m_ParamMultClss))
	{
		m_B.reserve_exactly((size_t)((double)m_BNext * m_ParamMultClss));
		if (unlikely(IsUnrecoverable())) return;
	}

	// ************************************
	// Update and compress the watch buffer
	// ************************************

	// Update all the long watches with new locations

	// Start with initializing m_LongWatches with 0 for every literal
	// The counter is expected to go back to its value after the next loop
	for (TULit l = GetFirstLit(); l < GetNextLit(); ++l)
	{
		TWatchInfo& wi = m_Watches[l];
		wi.m_LongWatches = 0;
	}

	// Will now update all the clause indices

	auto AddLongWatchLocal = [&](bool watchInd, TUInd clsInd)
	{
		const span<TULit> cls = Cls(clsInd);
		const TULit currLit = cls[watchInd];
		const TULit inlinedLit = cls[!watchInd];
		TWatchInfo& wi = m_Watches[currLit];
		TULit* watchArena = m_W.get_ptr(wi.m_WBInd);
		auto indBeyondLongWatches = wi.GetLongEntries();
		watchArena[indBeyondLongWatches++] = inlinedLit;
		*(TUInd*)(watchArena + indBeyondLongWatches) = clsInd;
		++wi.m_LongWatches;
	};

	for (TUInd clsInd = LitsInPage; clsInd < m_BNext; clsInd = ClsEnd(clsInd))
	{
		// Note that we cannot because of correctness (and also should not because of efficiency) use the standard
		// WLAddLongWatch procedure, since the number of long watches doesn't change
		AddLongWatchLocal(false, clsInd);
		AddLongWatchLocal(true, clsInd);
	}

	// Save aside the two initial watch entries for every literal l
	// and place log2(allocated) && l to be able to, respectively: (1) Skip to the next chunk during compression; 
	// (2) Point from the watches to their updated location in the watch-buffer

	CDynArray<array<TULit, 2>> firstWLEntriesPerLit(GetNextLit(), 0);
	if (unlikely(IsUnrecoverable())) return;

	for (TULit l = GetFirstLit(); l < GetNextLit(); ++l)
	{
		TWatchInfo& wi = m_Watches[l];
		const auto usedEntries = wi.GetUsedEntries();

		if (m_ParamCompressAllocatedPerWatch && wi.m_AllocatedEntries > 0 && usedEntries == 0)
		{
			MarkWatchBufferChunkDeleted(wi);
			wi.m_AllocatedEntries = 0;
		}

		if (wi.m_AllocatedEntries > 0)
		{
			assert(has_single_bit(wi.m_AllocatedEntries));
			assert(wi.m_AllocatedEntries >= TWatchInfo::BinsInLongBitCeil);

			if (m_ParamCompressAllocatedPerWatch)
			{
				const TUInd allocatedEntriesBefore = wi.m_AllocatedEntries;
				wi.m_AllocatedEntries = bit_ceil(usedEntries);
				assert(wi.m_AllocatedEntries != 0);
				if (wi.m_AllocatedEntries < TWatchInfo::BinsInLongBitCeil)
				{
					wi.m_AllocatedEntries = TWatchInfo::BinsInLongBitCeil;
				}

				// Marking deleted regions
				TUInd bitFloor = BadClsInd;
				for (auto [nextEntry, remainingEntriesToMark] = make_tuple(wi.m_WBInd + wi.m_AllocatedEntries, allocatedEntriesBefore - wi.m_AllocatedEntries); remainingEntriesToMark != 0; remainingEntriesToMark -= bitFloor, nextEntry = WLEnd(nextEntry))
				{
					bitFloor = bit_floor((TUInd)remainingEntriesToMark);
					MarkWatchBufferChunkDeletedOrByLiteral(nextEntry, bitFloor);
				}
			}

			firstWLEntriesPerLit[l][0] = m_W[wi.m_WBInd];
			firstWLEntriesPerLit[l][1] = m_W[wi.m_WBInd + 1];

			MarkWatchBufferChunkDeletedOrByLiteral(wi.m_WBInd, wi.m_AllocatedEntries, l);
		}
	}

	// Compress the watch buffer
	m_W.Compress(LitsInPage, m_WNext, [&](TUInd wlInd)
	{
		return WLChunkDeleted(wlInd);
	}, [&](TUInd wlInd)
	{
		return WLEnd(wlInd);
	}, [&](TUInd oldWlInd, TUInd newWlInd)
	{
		assert(newWlInd >= LitsInPage);
		const TULit l = m_W[oldWlInd + 1];
		m_W[oldWlInd] = firstWLEntriesPerLit[l][0];
		m_W[oldWlInd + 1] = firstWLEntriesPerLit[l][1];

		m_Watches[l].m_WBInd = newWlInd;
	});

	if (m_ParamReduceBuffersSizeAfterCompression && m_W.cap() > (size_t)((double)m_WNext * m_ParamMultWatches))
	{
		m_W.reserve_exactly((size_t)((double)m_WNext * m_ParamMultWatches));
		if (unlikely(IsUnrecoverable())) return;
	}

	assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(true));
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || DebugAssertWaste());

	assert(NV(1) || P("Compression finished\n"));
	assert(NV(2) || P("The trail: " + STrail() + "\n"));
}

