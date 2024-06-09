// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"
#include "SetInScope.h"

#include <fstream> 
#include <numeric>

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
TUInd CTopi<TLit, TUInd, Compress>::AddClsToBufferAndWatch(const TSpanTULit cls, bool isLearntNotForDeletion, bool isPartOfProof)
{
	if (isPartOfProof && IsCbLearntOrDrat())
	{
		NewLearntClsApplyCbLearntDrat(cls);
	}

	TUInd clsStart = BadClsInd;

	if (cls.size() == 2)
	{
		if (m_ParamExistingBinWLStrat >= 2 || !WLBinaryWatchExists(cls[0], cls[1]))
		{
			m_Stat.NewClause(cls.size(), isLearntNotForDeletion);
			WLAddBinaryWatch(cls[0], cls[1]);
			if (unlikely(IsUnrecoverable())) return clsStart;
			WLAddBinaryWatch(cls[1], cls[0]);
			if (unlikely(IsUnrecoverable())) return clsStart;
		}
		else
		{
			assert(WLBinaryWatchExists(cls[1], cls[0]));
			if (m_ParamExistingBinWLStrat == 1)
			{
				UpdateScoreVar(GetVar(cls[0]), m_ParamBinWLScoreBoostFactor);
				UpdateScoreVar(GetVar(cls[1]), m_ParamBinWLScoreBoostFactor);
			}
		}
	}
	else if (cls.size() > 2)
	{
		m_Stat.NewClause(cls.size(), isLearntNotForDeletion);
		// Long clause		

		WLAddLongWatch(cls[0], cls[1]);
		if (unlikely(IsUnrecoverable())) return clsStart;

		WLAddLongWatch(cls[1], cls[0]);
		if (unlikely(IsUnrecoverable())) return clsStart;

		auto PointFromWatches = [&](TUInd clsInd)
		{
			const array<TUInd, 2> clsIndPtrs = { LastWLEntry(cls[0]), LastWLEntry(cls[1]) };
			*((TUInd*)(m_W.get_ptr() + clsIndPtrs[0])) = *((TUInd*)(m_W.get_ptr() + clsIndPtrs[1])) = clsInd;
			return clsInd;
		};

		if constexpr (!Compress)
		{
			const bool isOversized = isLearntNotForDeletion && cls.size() > ClsLearntMaxSizeWithGlue;

			const TUInd newBNext = m_BNext + (TUInd)cls.size() + EClsLitsStartOffset(isLearntNotForDeletion, isOversized);

			if (unlikely(newBNext < m_BNext))
			{
				SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, "AddClsToBufferAndWatch: too many literals in all the clauses combined");
				return clsStart;
			}

			if (unlikely(newBNext >= m_B.cap()))
			{
				m_B.reserve_atleast(newBNext);
				if (unlikely(m_B.uninitialized_or_erroneous()))
				{
					SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "AddClsToBufferAndWatch: couldn't reserve the buffer");
					return clsStart;
				}
			}

			// The clause will start at m_BNext; point to it correctly from both watches
			clsStart = PointFromWatches(m_BNext);
			// Order of setting the fields is important, since ClsSetSize depends on ClsSetSize and ClsSetGlue depends on both
			EClsSetIsLearnt(m_BNext, isLearntNotForDeletion);
			ClsSetSize(m_BNext, (TUV)cls.size());
			if (isLearntNotForDeletion)
			{
				ClsSetGlue(m_BNext, GetGlueAndMarkCurrDecLevels(cls));
				if (unlikely(m_BNext < m_FirstLearntClsInd))
				{
					m_FirstLearntClsInd = m_BNext;
				}
				if (m_ParamClsDelStrategy > 0)
				{
					ClsSetActivityAndSkipdelTo0(m_BNext);
				}
			}
			// Copy the clause from the input span
			memcpy(m_B.get_ptr(m_BNext + EClsLitsStartOffset(isLearntNotForDeletion, isOversized)), &cls[0], cls.size() * sizeof(cls[0]));
			// Update m_BNext
			m_BNext = newBNext;
		}
		else
		{
			clsStart = PointFromWatches(BCCompress(cls, isLearntNotForDeletion, isLearntNotForDeletion ? GetGlueAndMarkCurrDecLevels(cls) : 0));
		}
	}

	return clsStart;

}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::UpdateAllUipInfoAfterRestart()
{
	if (m_ParamAllUipMode == 0 || m_Stat.m_Restarts <= m_ParamAllUipFirstRestart || (m_ParamAllUipLastRestart != numeric_limits<uint32_t>::max() && m_Stat.m_Restarts >= m_ParamAllUipLastRestart))
	{
		return;
	}

	const bool allUipFailed = m_AllUipAttemptedCurrRestart > 0 && (double)m_AllUipSucceededCurrRestart / (double)m_AllUipAttemptedCurrRestart < m_ParamAllUipFailureThr;
	if (allUipFailed)
	{
		++m_AllUipGap;
	}
	else
	{
		m_AllUipGap = m_AllUipGap == 0 ? 0 : m_AllUipGap - 1;
	}

	m_AllUipSucceededCurrRestart = 0;
	m_AllUipAttemptedCurrRestart = 0;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::MinimizeClauseBin(CVector<TULit>& cls)
{
	[[maybe_unused]] const auto clsSizeBefore = cls.size();

	assert(NV(2) || P("Minimize-clause-binary start: " + SLits(cls.get_const_span()) + "\n"));

	assert(m_RootedVars.empty());

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		CleanRooted();
	});

	TWatchInfo& wi = m_Watches[cls[0]];
	if (wi.m_BinaryWatches != 0)
	{
		TSpanTULit binWatches = m_W.get_span_cap(wi.m_WBInd + wi.GetLongEntries(), wi.m_BinaryWatches);
		bool someMarked = false;
		for (TULit l : binWatches)
		{
			if (IsSatisfied(l))
			{
				someMarked = true;
				MarkRooted(l);
			}
		}

		if (someMarked)
		{
			cls.erase_if_may_reorder([&](TULit l)
			{
				return IsRooted(l);
			}, 1);
		}
	}
	assert(NV(2) || P("Minimize-clause-binary finish; " + (cls.size() == clsSizeBefore ? "couldn't minimize" : "minimized and saved " + to_string(clsSizeBefore - cls.size()) + " literals") + ": " + SLits(cls.get_const_span()) + "\n"));
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::GenerateAllUipClause(CVector<TULit>& cls)
{
	if (m_ParamAllUipMode == 0 || m_Stat.m_Restarts < m_ParamAllUipFirstRestart)
	{
		return false;
	}

	assert(m_RootedVars.empty());

	const auto initGlue = GetGlueAndMarkCurrDecLevels(cls.get_const_span());
	if (cls.size() <= m_AllUipGap + initGlue)
	{
		assert(NV(2) || P("GenerateAllUipClause early exit: cls.size() <= m_AllUipGap + initGlue (" + to_string(cls.size()) + " " + to_string(m_AllUipGap) + " " + to_string(initGlue) + ")\n"));
		return false;
	}

	assert(NV(2) || P("GenerateAllUipClause attempt #" + to_string(m_Stat.m_AllUipAttempted + 1) + " started with the following clause " + SLits(cls.get_const_span()) + "\n"));

	CVector<TULit> res;
	bool cancelAllUipClauseGeneration = false;

	for (auto l : cls.get_span())
	{
		MarkRooted(l);
	}

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		++m_Stat.m_AllUipAttempted;
		++m_AllUipAttemptedCurrRestart;

		if (!cancelAllUipClauseGeneration)
		{
			m_Stat.m_LitsRemovedByAllUip += cls.size() - res.size();
			cls.clear();
			cls.append(res.get_span());
			++m_Stat.m_AllUipSucceeded;
			++m_AllUipSucceededCurrRestart;
			assert(NV(2) || P("GenerateAllUipClause succeeded, exiting; the new clause is: " + SLits(cls.get_const_span()) + "\n"));
		}
		else
		{
			assert(NV(2) || P("GenerateAllUipClause failed, exiting: " + SLits(cls.get_const_span()) + "\n"));
		}

		CleanRooted();
	});

	auto [initMarkedDecLevelsCounter, decLevels] = GetDecLevelsAndMarkInHugeCounter(cls);
	if (unlikely(IsUnrecoverable())) return false;

	auto UnvisitedNum = [&](TUV decLevel)
	{
		return m_HugeCounterPerDecLevel[decLevel] <= initMarkedDecLevelsCounter ? 0 : m_HugeCounterPerDecLevel[decLevel] - initMarkedDecLevelsCounter;
	};

	while (!decLevels.empty())
	{
		const TUV decLevel = decLevels.top();
		decLevels.pop();

		assert(UnvisitedNum(decLevel) >= 1);

		assert(NV(2) || P("Decision level " + to_string(decLevel) + " : started with " + to_string(UnvisitedNum(decLevel)) + " unvisited variables\n"));

		for (TUVar v = m_TrailLastVarPerDecLevel[decLevel]; UnvisitedNum(decLevel) > 0; v = m_VarInfo[v].m_TrailPrev)
		{
			if (IsRootedVar(v))
			{
				assert(NV(2) || P("New rooted variable " + SVar(v) + "\n"));

				auto IsVPushedToRes = [&]()
				{
					return res.size() != 0 && GetVar(res.back()) == v;
				};

				auto PushVToRes = [&]()
				{
					const TULit l = Negate(GetAssignedLitForVar(v));
					assert(NV(2) || P("Pushing the following literal to res " + SLit(l) + "\n"));
					assert(find(res.get_span().begin(), res.get_span().end(), l) == res.get_span().end());
					res.push_back(l);
				};

				auto CancelAllUipClauseGenerationIfRequired = [&]()
				{
					if (res.size() + decLevels.size() >= cls.size())
					{
						assert(NV(2) || P("Canceled AllUIP clause generation sue to exceeding size\n"));
						cancelAllUipClauseGeneration = true;
						return true;
					}
					return false;
				};

				assert(!IsVPushedToRes());

				--m_HugeCounterPerDecLevel[decLevel];

				if (decLevel == m_DecLevel || UnvisitedNum(decLevel) == 0)
				{
					// It's either the latest decision level, or 
					// We reached the UIP for a certain level (in which case the for loop will exit)
					PushVToRes();
					if (CancelAllUipClauseGenerationIfRequired())
					{
						return false;
					}
					continue;
				}

				const auto parentSpan = GetAssignedNonDecParentSpanVar(v);
				assert(NV(2) || P("\tParent " + SLits((span<TULit>)parentSpan) + "\n"));
				for (auto lParent : parentSpan)
				{
					if (IsRooted(lParent))
					{
						continue;
					}
					const TUV lParentDecLevel = GetAssignedDecLevel(lParent);
					if (UnvisitedNum(lParentDecLevel) == 0)
					{
						// The parent contains an unvisited level --> v will be included in the final clause as we don't want to resolve it away
						PushVToRes();
						if (CancelAllUipClauseGenerationIfRequired())
						{
							return false;
						}
						break;
					}
				}

				if (!IsVPushedToRes())
				{
					// Now we know that v can be resolved away without adding new levels
					assert(NV(2) || P("\tNow we know that " + SVar(v) + " can be resolved away without adding new levels\n"));
					for (auto lParent : parentSpan)
					{
						if (IsRooted(lParent))
						{
							continue;
						}
						MarkRooted(lParent);
						const TUV lParentDecLevel = GetAssignedDecLevel(lParent);
						++m_HugeCounterPerDecLevel[lParentDecLevel];
						if (m_HugeCounterPerDecLevel[lParentDecLevel] > m_HugeCounterDecLevels)
						{
							m_HugeCounterDecLevels = m_HugeCounterPerDecLevel[lParentDecLevel];
						}
					}
				}
			}
		}
	}

	return true;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::MinimizeClauseMinisat(CVector<TULit>& cls)
{
	[[maybe_unused]] const auto clsSizeBefore = cls.size();

	assert(NV(2) || P("Minimize-clause-Minisat start: " + SLits(cls.get_const_span()) + "\n"));

	assert(m_RootedVars.empty());

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		CleanRooted();
	});

	if (unlikely(IsUnrecoverable())) return;

	// After this function, all the decision levels in cls have m_DecLevelsLastAppearenceCounter[decLevel] == m_CurrDecLevelsCounter
	GetGlueAndMarkCurrDecLevels(cls.get_const_span());

	// All the variables in clause are the root
	// The minimization algorithm in a nutshell: for every root literal, if we reach the roots only by parent chain visiting, the literal can be removed
	m_RootedVars.reserve_exactly(cls.size());
	if (unlikely(IsUnrecoverable())) return;

	for (auto l : cls.get_span())
	{
		MarkRooted(l);
	}

	CVector<TUVar> toTestForRemoval;

	cls.erase_if_may_reorder([&](TULit l)
	{
		if (unlikely(IsUnrecoverable())) return false;

		toTestForRemoval.clear();

		bool canBeRemoved = true;

		assert(IsAssigned(l) && GetAssignedDecLevel(l) != 0);

		if (IsAssignedDec(l))
		{
			// The current literal is a decision literal, so it must have a marked decision level, and we cannot remove it
			canBeRemoved = false;
			assert(IsAssignedMarkedDecLevel(l));
		}
		else
		{
			auto ProcessParentDecideIfRemovalStillPossible = [&](TUVar currVar, const span<TULit> parent)
			{
				for (TULit parentLit : parent)
				{
					if (GetVar(parentLit) == currVar)
					{
						continue;
					}

					if (!IsAssignedMarkedDecLevel(parentLit))
					{
						canBeRemoved = false;
						break;
					}

					if (!IsRooted(parentLit))
					{
						if (IsAssignedDec(parentLit))
						{
							canBeRemoved = false;
							break;
						}
						toTestForRemoval.push_back(GetVar(parentLit));
						if (unlikely(IsUnrecoverable())) return;
					}
				}
			};

			ProcessParentDecideIfRemovalStillPossible(GetVar(l), GetAssignedNonDecParentSpan(l));
			if (unlikely(IsUnrecoverable())) return false;

			while (canBeRemoved && !toTestForRemoval.empty())
			{
				const TUVar currTestedVar = toTestForRemoval.back();

				assert(!IsAssignedDecVar(currTestedVar));
				assert(IsAssignedMarkedDecLevelVar(currTestedVar));

				// We never add a rooted var to toTestForRemoval, but a tested variable may be marked as rooted after being successfully tested for removal
				if (!IsRootedVar(currTestedVar))
				{
					size_t toTestBefore = toTestForRemoval.size();
					ProcessParentDecideIfRemovalStillPossible(currTestedVar, GetAssignedNonDecParentSpanVar(currTestedVar));
					if (unlikely(IsUnrecoverable())) return false;
					if (canBeRemoved && toTestForRemoval.size() == toTestBefore)
					{
						MarkRootedVar(currTestedVar);
						// This line is unnecessary for correctness, since the next iteration would've removed currTestedVar from toTestForRemoval anyway,
						// since the variable is marked as rooted, but we save an iteration by doing it here
						toTestForRemoval.pop_back();
					}
				}
				else
				{
					toTestForRemoval.pop_back();
				}
			}

		}

		return canBeRemoved;
	});

	assert(NV(2) || P("Minimize-clause-Minisat finish; " + (cls.size() == clsSizeBefore ? "couldn't minimize" : "minimized and saved " + to_string(clsSizeBefore - cls.size()) + " literals") + ": " + SLits(cls.get_const_span()) + "\n"));
}

template <typename TLit, typename TUInd, bool Compress>
size_t CTopi<TLit, TUInd, Compress>::SizeWithoutDecLevel0(const span<TULit> cls) const
{
	return accumulate(cls.begin(), cls.end(), (size_t)0, [&](size_t sum, TULit l)
	{
		return sum + (GetAssignedDecLevel(l) != 0);
	});
}

template <typename TLit, typename TUInd, bool Compress>
pair<typename CTopi<TLit, TUInd, Compress>::TSpanTULit, TUInd> CTopi<TLit, TUInd, Compress>::LearnAndUpdateHeuristics(TContradictionInfo& contradictionInfo, CVector<TULit>& clsBeforeAllUipOrEmptyIfAllUipFailed)
{
	clsBeforeAllUipOrEmptyIfAllUipFailed.clear();
	++m_Stat.m_Conflicts;
	++m_ConfsSinceRestart;
	++m_ConfsSinceNewInv;

	if (m_ParamVerbosity > 0 && m_Stat.m_Conflicts % (decltype(m_Stat.m_Conflicts))m_ParamStatPrintOutConfs == 0)
	{
		cout << m_Stat.StatStrShort();
	}

	// Giving an alias to m_VisitedLits to reflect the current usage for readability
	CVector<TULit>& visitedNegLitsPrevDecLevels = m_HandyLitsClearBefore[0];
	// Clear from any previous usage
	visitedNegLitsPrevDecLevels.clear();
	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		CleanVisited();
	});

	assert(CiIsLegal(contradictionInfo));

	auto contradictingCls = CiGetSpan(contradictionInfo);

	assert(NV(2) || P("************************ Conflict #" + to_string(m_Stat.m_Conflicts) + "\n" + STrail() + "\n" + "Contradicting clause: " + SLits((span<TULit>)contradictingCls) + "\n"));
	
	// The other case is taken care of by the CDCL-loop in Solve
	assert(GetAssignedDecLevel(contradictingCls[0]) == GetAssignedDecLevel(contradictingCls[1]));
	// Again, any backtracking is taken care of by the CDCL-loop in Solve
	assert(GetAssignedDecLevel(contradictingCls[0]) == m_DecLevel);

	TUV varsToVisitCurrDecLevel = 0;

	auto VisitLit = [&](TULit l)
	{
		TUVar v = GetVar(l);
		const TUV decLevel = GetAssignedDecLevel(l);

		if (!m_AssignmentInfo[v].m_Visit && decLevel != 0)
		{
			MarkVisitedVar(v);

			if (decLevel == m_DecLevel)
			{
				++varsToVisitCurrDecLevel;
			}
			else
			{
				visitedNegLitsPrevDecLevels.push_back(IsSatisfied(l) ? Negate(l) : l);
			}
			UpdateScoreVar(v, m_ParamVarActivityUseMapleLevelBreaker ? 0.5 : 1.0);
		}

	};

	auto VisitCls = [&](const span<TULit> cls, TUInd longClsInd, bool updateClsCounter)
	{
		if (updateClsCounter)
		{
			++m_CurrClsCounter;
			if (unlikely(m_CurrClsCounter < 0))
			{
				m_CurrClsCounters.memset(0);
				m_CurrClsCounter = 1;
			}
		}

		for (TULit l : cls)
		{
			VisitLit(l);
			if (updateClsCounter)
			{
				m_CurrClsCounters[GetVar(l)] = m_CurrClsCounter;
			}
		}
		if (longClsInd != BadClsInd && ClsGetIsLearnt(longClsInd))
		{
			const TUV newGlue = GetGlueAndMarkCurrDecLevels(cls);
			const TUV oldGlue = ClsGetGlue(longClsInd);
			if (newGlue < oldGlue)
			{
				ClsSetGlue(longClsInd, newGlue);
			}
			ClsDelNewLearntOrGlueUpdate(longClsInd, oldGlue);
		}

		assert(NV(2) || !IsCbLearntOrDrat() || P("Deriving external clause for conflict " + to_string(m_Stat.m_Conflicts) + ": " + SLits(cls, true) + " 0\n"));
	};

	VisitCls(contradictingCls, contradictionInfo.m_IsContradictionInBinaryCls ? BadClsInd : contradictionInfo.m_ParentClsInd, false);

	
	// Used to maintain all the heuristics working if the contradicting clause manages to stay the 1UIP
	TUVar trailEndBeforeOnTheFlySubsumption = m_TrailEnd;

	m_VarsParentSubsumed.clear();

	TUVar v = m_TrailEnd;
	const TUV vDecLevel = GetAssignedDecLevelVar(v);
	const TUVar vDecVar = GetDecVar(vDecLevel);
	const bool isAssumpLevel = IsAssumpVar(vDecVar);
	// Removing a subsumed contradicting clause at assumption level might result in a correctness problem, 
	// because the algorithm doesn't stop at first UIP, so it won't record the subsuming clause
	bool contradictingIsLearnt = IsOnTheFlySubsumptionContradictingOn() && !isAssumpLevel;
	for (; varsToVisitCurrDecLevel != 1 || (isAssumpLevel && !(IsSatisfiedAssump(v) && m_AssignmentInfo[v].m_Visit)); v = m_VarInfo[v].m_TrailPrev)	
	{
		auto& ai = m_AssignmentInfo[v];
		auto& vi = m_VarInfo[v];

		if (ai.m_Visit)
		{
			--varsToVisitCurrDecLevel;
			assert(ai.m_IsAssigned);
			if (ai.IsAssignedBinary() || vi.m_ParentClsInd != BadClsInd)
			{
				auto ContradictingTrinary2BinaryByResolvingCurrVar = [&]()
				{
					const TULit l1 = contradictingCls[GetVar(contradictingCls[0]) == v ? 1 : 0];
					const TULit l2 = contradictingCls[GetVar(contradictingCls[2]) == v ? 1 : 2];
					DeleteCls(contradictionInfo.m_ParentClsInd);
					contradictionInfo.m_IsContradictionInBinaryCls = true;
					contradictionInfo.m_BinClause = { l1, l2 };
					// The 2nd parameter doesn't matter since the clause is binary
					AddClsToBufferAndWatch(contradictionInfo.m_BinClause, true, true);
				};

				auto Contradicting2BinaryByRemovingLevel0 = [&]()
				{
					size_t currGoodLitInd = 0;
					for (TULit l : contradictingCls)
					{
						if (GetAssignedDecLevel(l) != 0)
						{
							contradictionInfo.m_BinClause[currGoodLitInd++] = l;
						}
					}
					assert(currGoodLitInd == 2);
					contradictionInfo.m_IsContradictionInBinaryCls = true;
					// The 2nd parameter doesn't matter since the clause is binary
					AddClsToBufferAndWatch(contradictionInfo.m_BinClause, true, true);
				};

				// If the clause is binary, the parent will contain only the other literal (without l), but we don't need l anyway
				// If the clause isn't binary, the parent will be complete, which is fine too
				auto parent = GetAssignedNonDecParentSpanVI(ai, vi);
				const auto psNo0 = parent.size() == 1 ? 2 : SizeWithoutDecLevel0(parent);
				assert(NV(2) || P("Visited var: " + SVar(v) + 
					(!IsCbLearntOrDrat() ? "" : "; external-lit = " + to_string(GetExternalLit(GetAssignedLitForVar(v)))) +
					"; Visited clause: " + SLits((span<TULit>)parent) + "\n"));
				const auto visitedBefore = m_VisitedVars.size();
				VisitCls(parent, ai.IsAssignedBinary() ? BadClsInd : vi.m_ParentClsInd,
					IsOnTheFlySubsumptionParentOn() && psNo0 > 2 && (IsParentLongInitial(ai, vi) || psNo0 < m_ParamOnTheFlySubsumptionParentMinGlueToDisable));
				if (contradictingIsLearnt)
				{
					if (GetVar(m_FlippedLit) == v)
					{
						assert(NV(2) || P("Visited flipped var while still minimizing the contradicting clause, hence disable flipped recording for this conflict; var = " + SVar(v) + "\n"));
						m_FlippedLit = BadULit;
					}

					const auto csNo0 = SizeWithoutDecLevel0(contradictingCls);

					if (m_VisitedVars.size() == visitedBefore &&
						((!contradictionInfo.m_IsContradictionInBinaryCls && !ClsGetIsLearnt(contradictionInfo.m_ParentClsInd)) ||
							csNo0 < (size_t)m_ParamOnTheFlySubsumptionContradictingMinGlueToDisable))
					{
						// The contradicting clause can be subsumed!
						// Is the parent clause subsumed by the contradicting clause too?												
						const bool parentSubsumedByContradicting = psNo0 == csNo0;
						bool longInitParentSubsumedByLearntContradicting = parentSubsumedByContradicting && psNo0 > 2 &&
							!ClsGetIsLearnt(vi.m_ParentClsInd) && ClsGetIsLearnt(contradictionInfo.m_ParentClsInd);

						if (contradictingCls.size() == 2)
						{
							// A binary clause --> it can be deleted						
							assert(contradictionInfo.m_IsContradictionInBinaryCls);
							// We have a new unit clause, which will serve as the learnt clause
							assert(varsToVisitCurrDecLevel == 1);
							DeleteBinaryCls(contradictingCls);
							contradictingCls = span(&contradictionInfo.m_BinClause[v == GetVar(contradictionInfo.m_BinClause[0]) ? 1 : 0], 1);
							if (IsCbLearntOrDrat())
							{
								NewLearntClsApplyCbLearntDrat(contradictingCls);
							}
						}
						else
						{
							// A trinary clause, which will now convert into a binary one
							if (contradictingCls.size() == 3)
							{
								ContradictingTrinary2BinaryByResolvingCurrVar();
								longInitParentSubsumedByLearntContradicting = false;
							}
							else
							{
								assert(contradictingCls.size() > 3);
								DeleteLitFromCls(contradictionInfo.m_ParentClsInd, Negate(GetAssignedLitForVar(v)));
							}
							contradictingCls = CiGetSpan(contradictionInfo);
						}
						m_Stat.m_LitsRemovedByConfSubsumption++;

						assert(NV(2) || P("Contradicting clause after reduction: " + SLits((span<TULit>)contradictingCls) + "\n"));

						if (parentSubsumedByContradicting)
						{
							assert(NV(2) || P("Parent clause deleted, since subsumed by contradicting!\n"));
							if (parent.size() == 1)
							{
								assert(ai.m_IsAssignedInBinary);
								array<TULit, 2> binCls = { vi.m_BinOtherLit, GetAssignedLitForVar(v) };
								DeleteBinaryCls(binCls);
								m_Stat.m_LitsRemovedByConfSubsumption += 2;
							}
							else
							{
								if (longInitParentSubsumedByLearntContradicting)
								{
									assert(NV(2) || P("Parent and contradicting swapped, being a long-initial and learnt, respectively!\n"));
									m_Stat.m_LitsRemovedByConfSubsumption++;
									if (ClsGetSize(vi.m_ParentClsInd) == 3)
									{
										Contradicting2BinaryByRemovingLevel0();
									}
									else
									{
										DeleteLitFromCls(vi.m_ParentClsInd, GetAssignedLitForVar(v));
										swap(vi.m_ParentClsInd, contradictionInfo.m_ParentClsInd);
									}
									contradictingCls = CiGetSpan(contradictionInfo);
								}
								m_Stat.m_LitsRemovedByConfSubsumption += ClsGetSize(vi.m_ParentClsInd);
								DeleteCls(vi.m_ParentClsInd);
							}
						}

						while (m_TrailEnd != v)
						{
							UnassignVar(m_TrailEnd);
						}
						UnassignVar(m_TrailEnd);
						while (!IsVisitedVar(m_TrailEnd))
						{
							UnassignVar(m_TrailEnd);
						}
						if (m_CurrCustomBtStrat > 0)
						{
							m_BestScorePerDecLevel[GetAssignedDecLevelVar(m_TrailEnd)] = CalcMaxDecLevelScore(GetAssignedDecLevelVar(m_TrailEnd));
						}
					}
					else
					{
						contradictingIsLearnt = false;
					}
				}
				else if (IsOnTheFlySubsumptionParentOn() && psNo0 > 2 && (IsParentLongInitial(ai, vi) || psNo0 < m_ParamOnTheFlySubsumptionParentMinGlueToDisable))
				{
					bool parentSubsumedByCurrResolvent = visitedNegLitsPrevDecLevels.size() + varsToVisitCurrDecLevel + 1 <= psNo0;
					if (!parentSubsumedByCurrResolvent)
					{
						continue;
					}

					const auto visitedNegLitsPrevDecLevelsSpan = visitedNegLitsPrevDecLevels.get_const_span();
					for (TULit l : visitedNegLitsPrevDecLevelsSpan)
					{
						const TUVar vL = GetVar(l);
						if (m_CurrClsCounters[vL] != m_CurrClsCounter)
						{
							parentSubsumedByCurrResolvent = false;
							break;
						}
					}

					if (!parentSubsumedByCurrResolvent)
					{
						continue;
					}

					TUV varsVisitedNum = 0;
					for (TUVar u = m_VarInfo[v].m_TrailPrev; varsVisitedNum < varsToVisitCurrDecLevel; u = m_VarInfo[u].m_TrailPrev)
					{
						if (IsVisitedVar(u))
						{
							if (m_CurrClsCounters[u] != m_CurrClsCounter)
							{
								parentSubsumedByCurrResolvent = false;
								break;
							}
							++varsVisitedNum;
						}
					}

					if (parentSubsumedByCurrResolvent)
					{
						try
						{
							m_VarsParentSubsumed.push_back(TParentSubsumed(GetAssignedLitForVar(v), m_AssignmentInfo[v].IsAssignedBinary(), m_VarInfo[v].m_ParentClsInd));
						}
						catch (...)
						{
							SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "m_VarsParentSubsumed.push_back allocation failure");
						}
															
						if (unlikely(IsUnrecoverable())) return make_pair(visitedNegLitsPrevDecLevels.get_span(), BadClsInd);
						assert(NV(2) || P("On-the-fly subsumption will remove the pivot " + SVar(v) + " from the parent " + HexStr(m_VarInfo[v].m_ParentClsInd) + " : " + SLits((span<TULit>)parent) + "\n"));
					}
				}
			}
		}
	}

	if (m_ParamMinimizeClausesMinisat && visitedNegLitsPrevDecLevels.size() > 1)
	{
		MinimizeClauseMinisat(visitedNegLitsPrevDecLevels);
		if (unlikely(IsUnrecoverable())) return make_pair(visitedNegLitsPrevDecLevels.get_span(), BadClsInd);
	}


	// Find the first UIP
	while (!m_AssignmentInfo[v].m_Visit)
	{
		v = m_VarInfo[v].m_TrailPrev;
	}
	const TULit firstUIPNegated = Negate(GetAssignedLitForVar(v));

	if (m_ParamAllUipMode == 1 || m_ParamAllUipMode == 3)
	{
		clsBeforeAllUipOrEmptyIfAllUipFailed = visitedNegLitsPrevDecLevels;
		bool allUipGenerated = GenerateAllUipClause(visitedNegLitsPrevDecLevels);
		if (!allUipGenerated)
		{
			clsBeforeAllUipOrEmptyIfAllUipFailed.clear();
		}
		else
		{
			clsBeforeAllUipOrEmptyIfAllUipFailed.push_back(firstUIPNegated);
		}
		if (clsBeforeAllUipOrEmptyIfAllUipFailed.uninitialized_or_erroneous())
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "clsBeforeAllUipOrEmptyIfAllUipFailed allocation failure");
		}
		if (unlikely(IsUnrecoverable())) return make_pair(visitedNegLitsPrevDecLevels.get_span(), BadClsInd);
	}

	// Forming the 1UIP conflict clause, ensuring that literal 0 has the highest decision level, while literal 1 is the second highest
	visitedNegLitsPrevDecLevels.push_back(firstUIPNegated);
	swap(visitedNegLitsPrevDecLevels[0], visitedNegLitsPrevDecLevels.back());

	if (visitedNegLitsPrevDecLevels.size() <= m_ParamMinimizeClausesBinMaxSize && GetGlueAndMarkCurrDecLevels(visitedNegLitsPrevDecLevels.get_const_span()) <= m_ParamMinimizeClausesBinMaxLbd)
	{
		MinimizeClauseBin(visitedNegLitsPrevDecLevels);
	}

	if (unlikely(IsUnrecoverable())) return make_pair(visitedNegLitsPrevDecLevels.get_span(), BadClsInd);

	if (visitedNegLitsPrevDecLevels.size() > 2)
	{
		auto highestDecLevelLitIt = GetAssignedLitsHighestDecLevelIt(visitedNegLitsPrevDecLevels.get_span(), 1);

		if (*highestDecLevelLitIt != visitedNegLitsPrevDecLevels[1])
		{
			swap(*highestDecLevelLitIt, visitedNegLitsPrevDecLevels[1]);
		}
	}

	assert(NV(1) || P("New-clause at level " + to_string(m_DecLevel) + " : " + SLits(visitedNegLitsPrevDecLevels.get_const_span()) + "\n"));

	bool addInitCls = false;
	TUInd clsStart = BadClsInd;

	if (contradictingIsLearnt)
	{
		assert(NV(2) || P("The contradicting clause was reduced all the way to 1UIP\n"));
		// The contradicting clause was reduced all the way to 1UIP

		// Disabling flipped clause recording
		m_FlippedLit = BadULit;
		if (!contradictionInfo.m_IsContradictionInBinaryCls)
		{
			// Is clause initial
			addInitCls = !ClsGetIsLearnt(contradictionInfo.m_ParentClsInd);
		}
		if (contradictingCls.size() > visitedNegLitsPrevDecLevels.size())
		{
			assert(NV(2) || P("The contradicting clause was reduced further by minimization and/or binary-resolution\n"));
			if (contradictionInfo.m_IsContradictionInBinaryCls)
			{
				m_Stat.m_LitsRemovedByConfSubsumption += 2;
				DeleteBinaryCls(contradictingCls);
			}
			else
			{
				m_Stat.m_LitsRemovedByConfSubsumption += ClsGetSize(contradictionInfo.m_ParentClsInd);
				DeleteCls(contradictionInfo.m_ParentClsInd);
			}

			contradictingIsLearnt = false;
		}
		else
		{
			assert(NV(2) || P("The contradicting clause will serve as the asserting clause, no need to record a new one!\n"));
			if (!contradictionInfo.m_IsContradictionInBinaryCls)
			{
				clsStart = contradictionInfo.m_ParentClsInd;
			}
		}
	}

	if (!contradictingIsLearnt)
	{
		// Always part of proof, even if stays initial (that is, not deletable)
		// Added this part to fix a DRAT bug
		clsStart = AddClsToBufferAndWatch(visitedNegLitsPrevDecLevels, !addInitCls, true);
	}

	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(false));

	if (m_ParamVarActivityUseMapleLevelBreaker)
	{
		const TUV secondHighestDecLevel = visitedNegLitsPrevDecLevels.size() <= 1 ? 0 : min(GetAssignedDecLevel(visitedNegLitsPrevDecLevels[0]), GetAssignedDecLevel(visitedNegLitsPrevDecLevels[1]));
		TUV decLevelMinToUpdate = secondHighestDecLevel - m_ParamVarActivityMapleLevelBreakerDecrease;
		if (unlikely(decLevelMinToUpdate > secondHighestDecLevel))
		{
			decLevelMinToUpdate = 0;
		}
		for (TUVar vv : m_VisitedVars.get_span())
		{
			if (m_VarInfo[vv].m_DecLevel >= decLevelMinToUpdate)
			{
				UpdateScoreVar(vv, 1.0);
			}
		}
	}

	const bool updateGlue = visitedNegLitsPrevDecLevels.size() > 2 && !addInitCls;
	const auto glue = updateGlue ? ClsGetGlue(clsStart) : 0;
	assert(IsOnTheFlySubsumptionContradictingOn() || trailEndBeforeOnTheFlySubsumption == m_TrailEnd);
	UpdateDecisionStrategyOnNewConflict(glue, GetVar(firstUIPNegated), trailEndBeforeOnTheFlySubsumption);

	if (updateGlue)
	{
		ClsDelNewLearntOrGlueUpdate(clsStart, glue);
	}

	return make_pair(visitedNegLitsPrevDecLevels.get_span(), clsStart);
}

template <typename TLit, typename TUInd, bool Compress>
pair<typename CTopi<TLit, TUInd, Compress>::TSpanTULit, TUInd> CTopi<TLit, TUInd, Compress>::RecordFlipped(TContradictionInfo& contradictionInfo, typename CTopi<TLit, TUInd, Compress>::TSpanTULit mainClsBeforeAllUip)
{
	// Giving an alias to m_VisitedLits to reflect the current usage for readability
	CVector<TULit>& visitedNegLitsPrevFlippedLevels = m_HandyLitsClearBefore[1];
	// Clear from any previous usage
	visitedNegLitsPrevFlippedLevels.clear();

	if (m_ParamFlippedRecordingMaxLbdToRecord == 0 || m_FlippedLit == BadULit || GetAssignedDecLevel(m_FlippedLit) != m_DecLevel)
	{
		visitedNegLitsPrevFlippedLevels.clear();
		return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);
	}

	const TUVar flippedVar = GetVar(m_FlippedLit);
	for (TUVar v = m_TrailEnd; v != flippedVar; v = m_VarInfo[v].m_TrailPrev)
	{
		assert(v != BadUVar);
		MarkRootedVar(v);
	}
	MarkRootedVar(flippedVar);

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		CleanVisited();
		CleanRooted();
	});

	assert(NV(2) || contradictionInfo.m_IsContradictionInBinaryCls || P("Contradicting cls: " + SLits(Cls(contradictionInfo.m_ParentClsInd)) + "\n"));

	assert(CiIsLegal(contradictionInfo));

	auto contradictingCls = CiGetSpan(contradictionInfo);

	assert(NV(2) || P("Will try to record a flipped clause\n"));

	// The other case is taken care of by the CDCL-loop in Solve
	assert(GetAssignedDecLevel(contradictingCls[0]) == GetAssignedDecLevel(contradictingCls[1]));
	// Again, any backtracking is taken care of by the CDCL-loop in Solve
	assert(GetAssignedDecLevel(contradictingCls[0]) == m_DecLevel);

	TUV varsToVisitCurrFlippedLevel = 0;

	auto VisitLit = [&](TULit l)
	{
		TUVar v = GetVar(l);
		const TUV decLevel = GetAssignedDecLevel(l);

		if (!m_AssignmentInfo[v].m_Visit && decLevel != 0)
		{
			MarkVisitedVar(v);

			if (IsRootedVar(v))
			{
				++varsToVisitCurrFlippedLevel;
			}
			else
			{
				visitedNegLitsPrevFlippedLevels.push_back(IsSatisfied(l) ? Negate(l) : l);
			}
		}

	};

	auto VisitCls = [&](const span<TULit> cls, TUInd longClsInd)
	{
		for (TULit l : cls) VisitLit(l);
	};

	VisitCls(contradictingCls, contradictionInfo.m_IsContradictionInBinaryCls ? BadClsInd : contradictionInfo.m_ParentClsInd);

	if (varsToVisitCurrFlippedLevel == 1)
	{
		assert(NV(2) || P("Flipped is skipped, since the contradicting clause has only one literal beyond the flipped (which might have happened because of on-the-fly-subsumption)\n"));
		visitedNegLitsPrevFlippedLevels.clear();
		return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);
	}

	TUVar v = m_TrailEnd;

	for (; varsToVisitCurrFlippedLevel != 1; v = m_VarInfo[v].m_TrailPrev)
	{
		auto& ai = m_AssignmentInfo[v];
		auto& vi = m_VarInfo[v];

		if (ai.m_Visit)
		{
			--varsToVisitCurrFlippedLevel;
			assert(ai.m_IsAssigned);
			if (ai.IsAssignedBinary() || vi.m_ParentClsInd != BadClsInd)
			{
				// If the clause is binary, the parent will contain only the other literal (without l), but we don't need l anyway
				// If the clause isn't binary, the parent will be complete, which is fine too
				auto parent = GetAssignedNonDecParentSpanVI(ai, vi);
				assert(NV(2) || P("Visited var: " + SVar(v) + "; Visited clause: " + SLits((span<TULit>)parent) + "\n"));
				VisitCls(parent, ai.IsAssignedBinary() ? BadClsInd : vi.m_ParentClsInd);
			}
		}
	}

	// Find the first UIP w.r.t the flipped level
	while (!m_AssignmentInfo[v].m_Visit)
	{
		v = m_VarInfo[v].m_TrailPrev;
	}

	if (m_ParamFlippedRecordDropIfSubsumed)
	{
		bool isSubsumedByMain = true;
		for (TULit mainClsLit : mainClsBeforeAllUip)
		{
			if (!IsVisited(mainClsLit) && GetVar(mainClsLit) != v)
			{
				isSubsumedByMain = false;
				break;
			}
		}

		if (isSubsumedByMain)
		{
			assert(NV(2) || P("Flipped is subsumed by the main clause: skipping; the clause so far: " + SLit(Negate(GetAssignedLitForVar(v))) + " " + SLits(visitedNegLitsPrevFlippedLevels.get_const_span()) + "\n"));
			visitedNegLitsPrevFlippedLevels.clear();
			return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);
		}
	}

	// Must be clean before MinimizeClauseMinisat, since we don't need it anymore in this function, whereas MinimizeClauseMinisat uses it
	CleanRooted();

	if (m_ParamMinimizeClausesMinisat && visitedNegLitsPrevFlippedLevels.size() > 1)
	{
		MinimizeClauseMinisat(visitedNegLitsPrevFlippedLevels);
		if (unlikely(IsUnrecoverable()))
		{
			visitedNegLitsPrevFlippedLevels.clear();
			return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);
		}
	}

	if (m_ParamAllUipMode == 2 || m_ParamAllUipMode == 3)
	{
		GenerateAllUipClause(visitedNegLitsPrevFlippedLevels);
		if (unlikely(IsUnrecoverable()))
		{
			visitedNegLitsPrevFlippedLevels.clear();
			return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);
		}
	}

	const TULit firstUIPNegated = Negate(GetAssignedLitForVar(v));

	// Forming the 1UIP conflict clause, ensuring that literal 0 has the highest decision level, while literal 1 is the second highest
	visitedNegLitsPrevFlippedLevels.push_back(firstUIPNegated);
	swap(visitedNegLitsPrevFlippedLevels[0], visitedNegLitsPrevFlippedLevels.back());

	if (visitedNegLitsPrevFlippedLevels.size() <= m_ParamMinimizeClausesBinMaxSize && GetGlueAndMarkCurrDecLevels(visitedNegLitsPrevFlippedLevels.get_const_span()) <= m_ParamMinimizeClausesBinMaxLbd)
	{
		MinimizeClauseBin(visitedNegLitsPrevFlippedLevels);
	}

	if (unlikely(IsUnrecoverable())) return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);

	const auto glue = GetGlueAndMarkCurrDecLevels(visitedNegLitsPrevFlippedLevels.get_const_span());
	if (glue > m_ParamFlippedRecordingMaxLbdToRecord)
	{
		assert(NV(2) || P("Flipped glue " + to_string(glue) + " is higher than the threshold " + to_string(m_ParamFlippedRecordingMaxLbdToRecord) + ", so flipped recording will be skipped\n"));
		visitedNegLitsPrevFlippedLevels.clear();
		return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), BadClsInd);
	}

	if (visitedNegLitsPrevFlippedLevels.size() > 2)
	{
		auto highestDecLevelLitIt = GetAssignedLitsHighestDecLevelIt(visitedNegLitsPrevFlippedLevels.get_span(), 1);

		if (*highestDecLevelLitIt != visitedNegLitsPrevFlippedLevels[1])
		{
			swap(*highestDecLevelLitIt, visitedNegLitsPrevFlippedLevels[1]);
		}
	}

	assert(NV(1) || P("New flipped clause at level " + to_string(m_DecLevel) + " : " + SLits(visitedNegLitsPrevFlippedLevels.get_const_span()) + "\n"));

	auto clsStart = AddClsToBufferAndWatch(visitedNegLitsPrevFlippedLevels, true, true);

	assert(NV(2) || P("Flipped clause recorded: " + SLits(visitedNegLitsPrevFlippedLevels.get_const_span()) + "\n"));
	++m_Stat.m_FlippedClauses;

	if (visitedNegLitsPrevFlippedLevels.size() > 2)
	{
		ClsDelNewLearntOrGlueUpdate(clsStart, glue);
	}

	return make_pair(visitedNegLitsPrevFlippedLevels.get_span(), clsStart);
}


template <typename TLit, typename TUInd, bool Compress>
void Topor::CTopi<TLit, TUInd, Compress>::OnBadDratFile()
{
	ofstream& o = *m_OpenedDratFile;
	assert(!o.good());
	SetStatus(TToporStatus::STATUS_DRAT_FILE_PROBLEM, (string)"Problem with DRAT file generation: " +
		(string)((o.rdstate() & std::ifstream::badbit) ? "Read/writing error on i/o operation" :
			(o.rdstate() & std::ifstream::failbit) ? "Logical error on i/o operation" :
			(o.rdstate() & std::ifstream::eofbit) ? "End-of-File reached on input operation" : "Unknown error"));
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::NewLearntClsApplyCbLearntDrat(const span<TULit> learntCls)
{
	assert(IsCbLearntOrDrat());

	m_UserCls.resize(learntCls.size());
	auto userClsSpan = m_UserCls.get_span();
	transform(learntCls.begin(), learntCls.end(), userClsSpan.begin(), [&](TULit l)
	{
		return GetExternalLit(l);
	});
	
	if (m_DratSortEveryClause)
	{
		sort(userClsSpan.begin(), userClsSpan.end(), [&](TLit l1, TLit l2)
		{
			const auto l1Abs = l1 < 0 ? -l1 : l1;
			const auto l2Abs = l2 < 0 ? -l2 : l2;
			return l1Abs < l2Abs;
		});
	}

	if (m_OpenedDratFile != nullptr)
	{
		ofstream& o = *m_OpenedDratFile;

		if (!o.good())
		{
			OnBadDratFile();
			return;
		}

		if (m_IsDratBinary)
		{
			o << 'a';
			for (TLit l : userClsSpan)
			{
				uint64_t ul64 = l > 0 ? (uint64_t)l << (uint64_t)1 : ((uint64_t)-l << (uint64_t)1) + 1;

				while (ul64 & ~0x7f)
				{
					const unsigned char ch = (ul64 & 0x7f) | 0x80;
					o << ch;
					ul64 >>= 7;
				}
				o << (unsigned char)ul64;
			}
			o << '\0';
		}
		else
		{
			for (TLit l : userClsSpan)
			{
				o << l << " ";
			}
			o << "0" << endl;
			assert(NV(2) || P("New DRAT line: " + SLits(learntCls, true) + " 0\n"));
		}
	}

	if (M_CbNewLearntCls != nullptr)
	{
		TStopTopor stopTopor = M_CbNewLearntCls(userClsSpan);
		if (stopTopor == TStopTopor::VAL_STOP)
		{
			SetStatus(TToporStatus::STATUS_USER_INTERRUPT, "User interrupt requested during callback (new-learnt-clause)");
		}
	}
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::CiIsLegal(TContradictionInfo& ci, bool assertTwoLitsSameDecLevel)
{
	assert(ci.IsContradiction());
	const auto cls = CiGetSpan(ci);
	// The contradicting clause's size is at least 2
	assert(cls.size() >= 2);
	// All the literals in the contradicting clause must be assigned false
	assert(all_of(cls.begin(), cls.end(), [&](TULit l) { return IsFalsified(l); }));
	// The first two literals must have the same highest decision level(BCP ensures this invariant)
	assert(!assertTwoLitsSameDecLevel || GetAssignedDecLevel(cls[0]) == GetAssignedDecLevel(cls[1]));
	assert(cls.size() == 2 || GetAssignedDecLevel(cls[0]) >= GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt((span<TULit>)cls, 2)));
	assert(cls.size() == 2 || GetAssignedDecLevel(cls[1]) >= GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt((span<TULit>)cls, 2)));
	return true;
}

template <typename TLit, typename TUInd, bool Compress>
pair<uint64_t, priority_queue<typename CTopi<TLit, TUInd, Compress>::TUV>> CTopi<TLit, TUInd, Compress>::GetDecLevelsAndMarkInHugeCounter(TSpanTULit cls)
{
	priority_queue<TUV> decLevels;

	if (m_HugeCounterPerDecLevel.cap() <= m_DecLevel)
	{
		m_HugeCounterPerDecLevel.reserve_atleast_with_max((size_t)m_DecLevel, GetNextVar(), 0);
		if (m_HugeCounterPerDecLevel.uninitialized_or_erroneous())
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "GetDecLevelsAndMarkInHugeCounter: allocation failed");
		}
	}

	const auto initMarkedDecLevelsCounter = m_HugeCounterDecLevels;

	for (TULit l : cls)
	{
		assert(IsAssigned(l));
		const TUV decLevel = GetAssignedDecLevel(l);
		if (m_HugeCounterPerDecLevel[decLevel] <= initMarkedDecLevelsCounter)
		{
			decLevels.push(decLevel);
			m_HugeCounterPerDecLevel[decLevel] = initMarkedDecLevelsCounter + 1;
		}
		else
		{
			++m_HugeCounterPerDecLevel[decLevel];
		}

		if (m_HugeCounterPerDecLevel[decLevel] > m_HugeCounterDecLevels)
		{
			m_HugeCounterDecLevels = m_HugeCounterPerDecLevel[decLevel];
		}
	}

	return make_pair(initMarkedDecLevelsCounter, decLevels);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::ConflictAnalysisLoop(TContradictionInfo& contradictionInfo)
{
	while (m_Status == TToporStatus::STATUS_UNDECIDED && contradictionInfo.IsContradiction())
	{
		//assert(NV(2) || P("************************ DEBUGGING (contradiction--first-line-in-while)\n" + STrail() + "\n"));
		// See the contradicting clause invariants asserted in GetContradictingClauseSpan
		assert(CiIsLegal(contradictionInfo));
		const auto contradictingCls = CiGetSpan(contradictionInfo, 2);
		// BCP guarantees that the two first literals (comprising the watched literals) 
		// have the highest decision level in the contradicting clause			
		auto maxDecLevelInContradictingCls = max(GetAssignedDecLevel(contradictingCls[0]), GetAssignedDecLevel(contradictingCls[1]));

		if (unlikely(maxDecLevelInContradictingCls == 0))
		{
			// Global contradiction!
			SetStatus(TToporStatus::STATUS_CONTRADICTORY, "Global contradiction!");
			continue;
		}

		// The case of one literal of the highest decision level is taken care of in BCP
		assert(GetAssignedDecLevel(contradictingCls[0]) == GetAssignedDecLevel(contradictingCls[1]));

		Backtrack(maxDecLevelInContradictingCls);
		CVector<TULit> clsBeforeAllUipOrEmptyIfAllUipFailed;
		auto [cls, assertingClsInd] = LearnAndUpdateHeuristics(contradictionInfo, clsBeforeAllUipOrEmptyIfAllUipFailed);
		//assert(m_ParamVerbosityLevel <= 2 || P("***** Learnt clause " + SLits(cls) + "\n"));
		if (unlikely(IsUnrecoverable())) return;
		const bool conflictAtAssumptionLevel = m_DecLevel <= m_DecLevelOfLastAssignedAssumption;
		if (m_EarliestFalsifiedAssump != BadULit || conflictAtAssumptionLevel) m_FlippedLit = BadULit;
		auto [additionalCls, additionalAssertingClsInd] = RecordFlipped(contradictionInfo, clsBeforeAllUipOrEmptyIfAllUipFailed.empty() ? cls : clsBeforeAllUipOrEmptyIfAllUipFailed);
		if (unlikely(IsUnrecoverable())) return;

		if (!additionalCls.empty() &&
			((cls.size() > 1 && additionalCls.size() == 1) ||
				(cls.size() != 1 && additionalCls.size() != 1 &&
					GetAssignedDecLevel(additionalCls[0]) != GetAssignedDecLevel(additionalCls[1]) &&
					GetAssignedDecLevel(additionalCls[1]) < GetAssignedDecLevel(cls[1]))))
		{
			// Swap the flipped clause with the 1UIP clause, since the former one is also an asserting clause of a lower 2nd dec. level
			assert(NV(2) || P("Swapped the flipped clause with the 1UIP clause\n"));
			++m_Stat.m_FlippedClausesSwapped;
			swap(cls, additionalCls);
			swap(assertingClsInd, additionalAssertingClsInd);
		}


		// The asserting clause must be non-empty
		assert(cls.size() > 0);
		// All the literals in the asserting clause must be assigned false
		assert(all_of(cls.begin(), cls.end(), [&](TULit l) { return IsFalsified(l); }));
		// The first literal's level must not be smaller than the rest
		assert(cls.size() == 1 || GetAssignedDecLevel(cls[0]) >= GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt(cls, 1)));
		// The second literal's level must not be smaller than the rest, but the first one
		assert(cls.size() <= 2 || GetAssignedDecLevel(cls[1]) >= GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt(cls, 2)));

		TUV ncbBtLevel = cls.size() > 1 ? GetAssignedDecLevel(cls[1]) : 0;
		
		if (!conflictAtAssumptionLevel && ncbBtLevel < m_DecLevelOfLastAssignedAssumption)
		{
			// We don't want to backtrack lower than the assumptions to prevent assumption re-propagation
			ncbBtLevel = m_DecLevelOfLastAssignedAssumption;
		}

		if (cls.size() > 2)
		{
			++m_RstGlueAssertingGluedClss;
			if (m_CurrRestartStrat == RESTART_STRAT_LBD)
			{
				// Might be initial because of on-the-fly subsumption
				RstNewAssertingGluedCls(ClsGetIsLearnt(assertingClsInd) ? ClsGetGlue(assertingClsInd) : GetGlueAndMarkCurrDecLevels(ConstClsSpan(assertingClsInd)));
			}
		}

		// Determine how to backtrack 		
		const bool isChronoBt = m_EarliestFalsifiedAssump != BadULit || conflictAtAssumptionLevel || (m_ConfsSinceNewInv >= m_ParamConflictsToPostponeChrono && m_DecLevel - ncbBtLevel > m_CurrChronoBtIfHigher) || maxDecLevelInContradictingCls <= m_DecLevelOfLastAssignedAssumption;
		const auto btLevel = isChronoBt ? (m_EarliestFalsifiedAssump != BadULit || conflictAtAssumptionLevel || m_CurrCustomBtStrat == 0 || ncbBtLevel + 1 == m_DecLevel ? m_DecLevel - 1 : GetDecLevelWithBestScore(ncbBtLevel + 1, m_DecLevel)) : ncbBtLevel;
		Backtrack(btLevel, false);
		Assign(m_FlippedLit = cls[0], cls.size() >= 2 ? assertingClsInd : BadClsInd, cls.size() == 1 ? BadULit : cls[1], cls.size() == 1 ? 0 : GetAssignedDecLevel(cls[1]));
		assert(NV(2) || P("***** Flipped former UIP to " + SLit(cls[0]) + "\n"));
		
		AdditionalAssign(additionalCls, additionalAssertingClsInd);
		if (IsOnTheFlySubsumptionParentOn() && !m_VarsParentSubsumed.empty())
		{
			assert(IsOnTheFlySubsumptionParentOn());
			for (TParentSubsumed& vps : m_VarsParentSubsumed)
			{
				TUVar v = GetVar(vps.m_L);
				if (!vps.m_IsBinary)
				{
					auto parentCls = Cls(vps.m_ParentClsInd);
					if (parentCls.size() == 3)
					{
						assert(NV(2) || P("On-the-fly subsumption converted the long parent to a binary; pivot = " + SVar(v) + "\n"));
						const TULit l1 = parentCls[GetVar(parentCls[0]) == v ? 1 : 0];
						const TULit l2 = parentCls[GetVar(parentCls[2]) == v ? 1 : 2];
						DeleteCls(vps.m_ParentClsInd);
						array<TULit, 2> binCls = { l1, l2 };
						// The 2nd parameter doesn't matter since the clause is binary
						AddClsToBufferAndWatch(binCls, true, true);
						AdditionalAssign(binCls, BadClsInd);
					}
					else
					{
						assert(NV(2) || P("Before on-the-fly subsumption deleted the pivot " + SVar(v) + " from the long parent " + HexStr(vps.m_ParentClsInd) + "; clause: " + SLits(parentCls) + "\n"));
						DeleteLitFromCls(vps.m_ParentClsInd, vps.m_L);
						assert(NV(2) || P("After on-the-fly subsumption deleted the pivot " + SVar(v) + " from the long parent " + HexStr(vps.m_ParentClsInd) + "; clause: " + SLits(parentCls) + "\n"));
						AdditionalAssign(Cls(vps.m_ParentClsInd), vps.m_ParentClsInd);
					}
					m_Stat.m_LitsRemovedByConfSubsumption++;
				}
				else
				{
					array<TULit, 1> newUnit({ vps.m_BinOtherLit});
					AdditionalAssign(newUnit, BadClsInd);
				}
			}
			
			m_VarsParentSubsumed.clear();
		}

		contradictionInfo = BCP();
		ClsDeletionDecayActivity();
		//assert(m_ParamVerbosityLevel <= 2 || P("************************ DEBUGGING (after BCP-conflict-loop)\n" + STrail() + "\n"));
	}
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::MarkDecisionsInConeAsVisited(TULit triggeringLit)
{
	assert(IsAssigned(triggeringLit));

	MarkVisited(triggeringLit);

	if (IsAssignedDec(triggeringLit))
	{
		return;
	}

	for (TUVar v = GetVar(triggeringLit); v != BadUVar && GetAssignedDecLevelVar(v) != 0; v = m_VarInfo[v].m_TrailPrev)
	{
		if (IsVisitedVar(v) && !IsAssignedDecVar(v))
		{
			const auto cls = GetAssignedNonDecParentSpanVar(v);
			for (auto clsLit : cls)
			{
				MarkVisited(clsLit);
			}
			if (v != GetVar(triggeringLit))
			{
				m_AssignmentInfo[v].m_Visit = false;
			}
		}
	}

	m_VisitedVars.erase_if_may_reorder([&](TUVar v)
	{
		return m_AssignmentInfo[v].m_Visit == false;
	});

	assert(IsVisitedConsistent());
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
