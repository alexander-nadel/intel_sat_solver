// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include <functional>
#include <string>
#include <bit>
#include <iterator> 
#include <unordered_set> 
#include "Topi.hpp"
#include "SetInScope.h"

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::SwapWatch(const TUInd clsInd, bool watchInd, typename CCls::TIterator newWatchIt)
{
	auto cls = Cls(clsInd);
	size_t cls1WlInd = WLGetLongWatchInd(cls[watchInd], clsInd);
	assert(cls1WlInd != numeric_limits<size_t>::max());
	WLRemoveLongWatch(cls[watchInd], cls1WlInd);
	swap(cls[watchInd], *newWatchIt);
	WLAddLongWatch(cls[watchInd], cls[!watchInd], clsInd);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::SwapCurrWatch(TULit l, typename CCls::TIterator newWatchIt, const TUInd clsInd, CCls& cls, size_t& currLongWatchInd, TULit*& currLongWatchPtr, TWatchInfo& wi)
{
	// Remove the current watch and adjust the indices, since removal moves the last watch into the current location
	WLRemoveLongWatch(Negate(l), currLongWatchInd);
	--currLongWatchInd; currLongWatchPtr -= TWatchInfo::BinsInLong;

	// Swap l and the visited literal in the clause and add watch to the visited literal
	swap(cls[0], *newWatchIt);
	WLAddLongWatch(cls[0], cls[1], clsInd);
	// The line below is required to support the (very rare) occasions  of WLB's realloc actually moving m_W in WLAddLongWatch
	// Using get_ptr_no_assert, since WLAddLongWatch might reallocate m_W, which might cause, 
	// when there are no long watches in wi, wi.m_WBInd to be higher than the capacity of m_W, which causes a failure assertion
	currLongWatchPtr = m_W.get_ptr_no_assert(wi.m_WBInd) + (currLongWatchInd * TWatchInfo::BinsInLong);
};

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::CCls::TIterator CTopi<TLit, TUInd, Compress>::FindBestWLCand(CCls& cls, TUV maxDecLevel)
{
	auto visitedLitIt = find_if(cls.begin() + 2, cls.end(), [&](const TULit visitedLit)
	{
		return UnassignedOrSatisfied(visitedLit);
	});

	if (visitedLitIt != cls.end())
	{
		return visitedLitIt;
	}

	TUV maxDecLevelClsSoFar = 0;
	auto maxIt = cls.begin() + 2;
	for (auto it = cls.begin() + 2; it != cls.end(); ++it)
	{
		const auto lDecLevel = GetAssignedDecLevel(*it);
		if (lDecLevel >= maxDecLevel)
		{
			return it;
		}
		if (lDecLevel > maxDecLevelClsSoFar)
		{
			maxDecLevelClsSoFar = lDecLevel;
			maxIt = it;
		}
	}

	return maxIt;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::BCPBacktrack(TUV decLevel, bool eraseDecLevel)
{
	// Only one literal of the highest decision level appears in the clause
	Backtrack(decLevel, true);

	auto EraseLit = [&](TULit l) { return !IsAssigned(l) || (eraseDecLevel && GetAssignedDecLevel(l) == decLevel); };

	m_ToPropagate.erase_if_may_reorder([&](TULit l)
	{
		assert(!IsAssigned(l) || GetAssignedDecLevel(l) <= decLevel);
		const bool eraseLit = EraseLit(l);
		if (eraseLit)
		{
			// Marking literals erased from toPropagate
			// for implementing ProcessDelayedImplication correctly (see the comments there)
			MarkVisited(l);
		}
		return eraseLit;
	});

	assert(m_CurrentlyPropagatedLit != BadULit);
	if (EraseLit(m_CurrentlyPropagatedLit))
	{
		MarkVisited(m_CurrentlyPropagatedLit);
	}
}

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::TContradictionInfo CTopi<TLit, TUInd, Compress>::BCP()
{
	// Returns true iff BCP should stop propagating the current literal
	auto NewContradiction = [&](TContradictionInfo&& newCi)
	{
		const TUV propagatedDecLevel = GetAssignedDecLevel(m_CurrentlyPropagatedLit);
		// Assert that we indeed have a new contradiction, 
		// where all the literals are falsified and the two first literals' decision level is higher than that of the rest
		assert(CiIsLegal(newCi, false));

		assert(NV(2) || P("***** previous contradictions = " + CisString(m_Cis.get_span()) + "\n"));

		const auto newCiSpanFirst2Lits = CiGetSpan(newCi, 2);
		const array<TUV, 2> newCiSpanDecLevels = { GetAssignedDecLevel(newCiSpanFirst2Lits[0]), GetAssignedDecLevel(newCiSpanFirst2Lits[1]) };
		auto maxDecLevelInContradictingCls = max(newCiSpanDecLevels[0], newCiSpanDecLevels[1]);

		if (newCiSpanDecLevels[0] != newCiSpanDecLevels[1])
		{
			BCPBacktrack(maxDecLevelInContradictingCls - 1, false);
			if (!m_Cis.empty())
			{
				// Clear the previous contradictions, if any, which must be unassigned after the backtracking				
				assert(all_of(m_Cis.get_span().begin(), m_Cis.get_span().end(), [&](TContradictionInfo& ci) { return !IsFalsified(CiGetSpanDebug(ci)[0]) && !IsFalsified(CiGetSpanDebug(ci)[1]); }));
				m_Cis.clear();
			}
			assert(IsAssigned(newCiSpanFirst2Lits[0]) != IsAssigned(newCiSpanFirst2Lits[1]) || P("Failure: " + SLits((span<TULit>)newCiSpanFirst2Lits) + "; trail: " + STrail() + "\n"));
			assert(IsAssigned(newCiSpanFirst2Lits[0]) != IsAssigned(newCiSpanFirst2Lits[1]));
			const TULit unassignedLit = IsAssigned(newCiSpanFirst2Lits[0]) ? newCiSpanFirst2Lits[1] : newCiSpanFirst2Lits[0];
			const TULit assignedLit = IsAssigned(newCiSpanFirst2Lits[0]) ? newCiSpanFirst2Lits[0] : newCiSpanFirst2Lits[1];
			assert(IsFalsified(assignedLit));
			if (!newCi.m_IsContradictionInBinaryCls && newCi.m_ParentClsInd != BadClsInd)
			{
				WLSetCached(assignedLit, newCi.m_ParentClsInd, unassignedLit);
			}
			Assign(unassignedLit, newCi.m_IsContradictionInBinaryCls ? BadClsInd : newCi.m_ParentClsInd, assignedLit, GetAssignedDecLevel(assignedLit));
			assert(NV(2) || P("***** NewContradiction finished; stop-propagation = " + to_string(propagatedDecLevel > maxDecLevelInContradictingCls - 1) + ": turned out to be a delayed implication; to-propagate: " + SLits(m_ToPropagate.get_span()) + "; trail: " + STrail() + "\n"));
			return propagatedDecLevel > maxDecLevelInContradictingCls - 1;
		}
		else
		{
			assert(m_Cis.empty() || all_of(m_Cis.get_span().begin(), m_Cis.get_span().end(), [&](TContradictionInfo& ci) { return maxDecLevelInContradictingCls <= GetAssignedDecLevel(CiGetSpan(ci)[0]) && maxDecLevelInContradictingCls <= GetAssignedDecLevel(CiGetSpan(ci)[1]); }));

			BCPBacktrack(maxDecLevelInContradictingCls, true);

			m_Cis.erase_if_may_reorder([&](TContradictionInfo& ci)
			{
				assert(ci.IsContradiction());
				assert(IsAssigned(CiGetSpanDebug(ci)[0]) == IsAssigned(CiGetSpanDebug(ci)[1]));
				return(!IsAssigned(CiGetSpanDebug(ci, 1)[0]));
			});

			m_Cis.emplace_back(move(newCi));

			assert(NV(2) || P("***** NewContradiction finished; stop-propagation = " + to_string(propagatedDecLevel > maxDecLevelInContradictingCls - 1) + ": NOT a delayed implication; to-propagate = " + SLits(m_ToPropagate.get_span()) + "\n"));
			return propagatedDecLevel >= maxDecLevelInContradictingCls;
		}
	};

	// Returns true iff BCP should stop propagating the current literal
	assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TrailAssertConsistency());
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(false));

	// assert(m_ParamAssertConsistency < 2 ||  P("DEBUG: " + SLits(GetCls(5304)) + "\n"));
	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		ToPropagateClear();
		CleanVisited();
		m_CurrentlyPropagatedLit = BadULit;
		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TrailAssertConsistency());
		assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(m_Cis.empty()));
		m_Cis.clear();
	});

	++m_Stat.m_BCPs;

	assert(NV(2) || P("***** BCP started; #" + to_string(m_Stat.m_BCPs) + "\n"));

	while (!m_ToPropagate.empty())
	{
		bool stopPropagating = false;
		m_CurrentlyPropagatedLit = ToPropagateBackAndPop();

		[[maybe_unused]] auto IsLStillPropagated = [&]() { return IsAssigned(m_CurrentlyPropagatedLit) && IsSatisfied(m_CurrentlyPropagatedLit); };

		assert(NV(2) || P("Propagating literal " + SLit(m_CurrentlyPropagatedLit) + "\n"));

		assert(IsLStillPropagated());
		const TUVar v = GetVar(m_CurrentlyPropagatedLit);
		const TVarInfo& vi = m_VarInfo[v];
		TWatchInfo& wi = m_Watches[Negate(m_CurrentlyPropagatedLit)];
		if (wi.IsEmpty())
		{
			continue;
		}
		const TUV lDecLevel = GetAssignedDecLevel(m_CurrentlyPropagatedLit);
		++m_Stat.m_Implications;
		if (m_ParamSimplify)
		{
			--m_ImplicationsTillNextSimplify;
		}

		// Go over the binary watches first. We would like to pre-fetch the longs too for cache reasons, otherwise we would have used
		// TSpanTULit binWatches = b.get_span(wi.m_BInd + wi.GetLongEntries(), wi.m_BinaryWatches);
		const volatile auto allWatches = m_W.get_ptr(wi.m_WBInd);
		TSpanTULit binWatches = TSpanTULit(allWatches + wi.GetLongEntries(), wi.m_BinaryWatches);

		// Have to use an old-fashioned index-based for loop, since binWatches might change inside the loop because of reallocation
		for (size_t otherWatchI = 0; otherWatchI < binWatches.size(); ++otherWatchI)
		{
			const auto otherWatch = binWatches[otherWatchI];

			const auto isOtherWatchAssigned = IsAssigned(otherWatch);
			const auto isOtherWatchNegated = IsAssignedNegated(otherWatch);

			assert(NV(2) || P("Visiting binary clause " + SLit(otherWatch) + "\n"));

			if (!isOtherWatchAssigned)
			{
				// Imply otherWatch at the decision level of l
				Assign(otherWatch, BadClsInd, Negate(m_CurrentlyPropagatedLit), vi.m_DecLevel);				
			}
			else if (isOtherWatchNegated)
			{
				// Contradiction				
				stopPropagating = NewContradiction(TContradictionInfo({ Negate(m_CurrentlyPropagatedLit) , otherWatch }));
				if (stopPropagating) break;
			}
			else
			{
				// Otherwise (that is, if both if conditions above do not hold), the clause is satisfied				
				if (lDecLevel < m_DecLevel && GetAssignedDecLevel(otherWatch) > lDecLevel)
				{
					// If the other watch is satisfied at a level higher than the current unsatisfied literal l, we have a delayed implication
					stopPropagating = ProcessDelayedImplication(otherWatch, Negate(m_CurrentlyPropagatedLit), BadClsInd, m_Cis);
					if (stopPropagating) break;
					// ProcessDelayedImplication might realloc, hence updating binWatches
					binWatches = TSpanTULit(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
					if (unlikely(m_CurrPropWatchModifiedDuringProcessDelayedImplication))
					{
						otherWatchI = -1;
					}
				}
			}
		}

		// Go over the long watches		
		for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, m_W.get_ptr(wi.m_WBInd)); !stopPropagating && currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
		{
			TULit& cachedLit = *currLongWatchPtr;

			assert(NV(2) || P("BCP: visiting long clause " + HexStr(*(TUInd*)(currLongWatchPtr + 1)) + ": cached " + SLit(cachedLit) + "; clause: " + SLits(Cls(*(TUInd*)(currLongWatchPtr + 1))) + "\n"));
			
			if (IsSatisfied(cachedLit) && GetAssignedDecLevel(cachedLit) <= lDecLevel)
			{
				// The cached literal is satisfied at decision level not higher than the current watch, we can continue without visiting the clause!
				continue;
			}

			// Fetching the clause
			const TUInd clsInd = *(TUInd*)(currLongWatchPtr + 1);

			auto cls = Cls(clsInd);

			assert(!ClsChunkDeleted(clsInd));
			assert(Compress || clsInd >= m_FirstLearntClsInd || !ClsGetIsLearnt(clsInd));

			// Making sure, our literal is the first one in the clause
			if (cls[1] == Negate(m_CurrentlyPropagatedLit)) swap(cls[0], cls[1]);
			assert(cls[0] == Negate(m_CurrentlyPropagatedLit));
			const TULit otherWatch = cls[1];
			const bool isOtherWatchSatisfied = IsSatisfied(otherWatch);

			// Update the cached literal to the second satisfied watch	
			// If the current watch is removed, this operation doesn't matter
			// However, it is essential for correctness of delayed implications processing, 
			// if our literal turns out to be the implying literal for the 2nd watch
			cachedLit = otherWatch;

			// Check the other watch			
			if (isOtherWatchSatisfied)
			{
				const TUV otherWatchDecLevel = GetAssignedDecLevel(cls[1]);
				if (otherWatchDecLevel <= lDecLevel)
				{
					// The decision level of the other watch is not higher than that of l, so, fortunately, no chance of a delayed implication					
					// Continue to the next clause
					continue;
				}
			}

			// Going over the rest of the clause (that is, skipping the watches) to find the best WL candidate to swap with l
			auto bestWLCandIt = FindBestWLCand(cls, m_DecLevel);
			const TULit bestWLCandLit = *bestWLCandIt;
			const bool bestWLCandAssigned = IsAssigned(bestWLCandLit);
			const bool bestWLCandSatisfied = IsSatisfied(bestWLCandLit);
			const bool bestWLUnassignedOrSatisfied = (!bestWLCandAssigned) | bestWLCandSatisfied;

			if (bestWLUnassignedOrSatisfied || (lDecLevel < m_DecLevel && GetAssignedDecLevel(bestWLCandLit) > lDecLevel))
			{
				// If the candidate is unassigned, satisfied or has a greater decision level, swap it with the current watch
				SwapCurrWatch(m_CurrentlyPropagatedLit, bestWLCandIt, clsInd, cls, currLongWatchInd, currLongWatchPtr, wi);
				if (unlikely(IsUnrecoverable())) return TContradictionInfo();

				if (bestWLUnassignedOrSatisfied)
				{
					// The candidate satisfied or it's unassigned --> we're done with this clause
					continue;
				}
			}

			if (IsFalsified(cls[0]) && IsFalsified(cls[1]))
			{
				// Contradiction!

				// There might be a case, when a literal in the clause is assigned after the 2nd watch w.r.t decision levels,
				// in which case we must swap them to prevent missed implications.
				// An illustrating example: 
				// (1) We have a conflict at decision level 20
				// (2) The new conflict clause C's highest level (excluding the new 1UIP luip) is 7
				// (3) We chronologically backtrack to 19 and run BCP, which implied luip at level 7
				// (4) As a result, a literal l10 of dec. level 10 is implied in some clause and a literal l15 of dec. level 15 is implied at another clause
				// (5) A clause D becomes unsatisfied at decision level 15 because of the two above implications. 
				// (6) D also has two non-WL literals, l17 and l18, assigned at levels 17 and 18, respectively
				// (7) Assume, the contradicting clause looks like this [l10, l15, l17, l18] and the contradiction is discovered when implying l10
				// (8) The code above will swap l10 and l18, so we get [l18, l15, l17, l10]
				// (9) Now we need to swap l15 and l17, since, otherwise, backtracking to decision level 16 and assigning l17 would result in a missed implication
				const TUV cls1DecLevel = GetAssignedDecLevel(cls[1]);
				if (cls1DecLevel < GetAssignedDecLevel(cls[0]))
				{
					auto maxNonWLDecLevelIt = GetAssignedLitsHighestDecLevelIt(cls, 2);
					if (cls1DecLevel < GetAssignedDecLevel(*maxNonWLDecLevelIt))
					{
						SwapWatch(clsInd, true, maxNonWLDecLevelIt);
						if (unlikely(IsUnrecoverable())) return TContradictionInfo();
						// The line below is required to support the (very rare) occasions of b's realloc actually moving b in WLAddLongWatch
						currLongWatchPtr = m_W.get_ptr_no_assert(wi.m_WBInd) + (currLongWatchInd * TWatchInfo::BinsInLong);
					}
				}

				stopPropagating = NewContradiction(clsInd);
			}
			else if (IsFalsified(cls[0]) && IsSatisfied(cls[1]) && GetAssignedDecLevel(cls[1]) > GetAssignedDecLevel(cls[0]))
				// Note that it is possible that the other watch is satisfied, the other watch's dl is greater than that of l,
				// but it's not a delayed implication any longer, since we swapped cls[0] with another falsified literal having a higher decision level			
			{
				// Delayed implication
				stopPropagating = ProcessDelayedImplication(cls[1], cls[0], clsInd, m_Cis);
				if (unlikely(!stopPropagating && m_CurrPropWatchModifiedDuringProcessDelayedImplication))
				{
					currLongWatchInd = -1;
					currLongWatchPtr = m_W.get_ptr_no_assert(wi.m_WBInd) - (1 * TWatchInfo::BinsInLong);
				}
				else
				{
					// The line below is required to support the (very rare) occasions  of b's realloc in ProcessDelayedImplication actually moving b in WLAddLongWatch
					currLongWatchPtr = m_W.get_ptr_no_assert(wi.m_WBInd) + (currLongWatchInd * TWatchInfo::BinsInLong);
				}
			}
			else if (IsFalsified(cls[0]) && !IsAssigned(cls[1]))
			{
				// The clause is unit
				assert(all_of(cls.begin() + 2, cls.end(), [&](TULit l) { return IsFalsified(l); }));
				// Imply the other watch now
				Assign(cls[1], clsInd, cls[0], GetAssignedDecLevel(cls[0]));				
			}
			else
			{
				// If we are here, it could have been a delayed implication candidate, but our watch was swapped with another one of a higher decision level			
				assert((IsFalsified(cls[0]) && IsSatisfied(cls[1]) && GetAssignedDecLevel(cls[1]) <= GetAssignedDecLevel(cls[0])) || P("Failure: " + SLits(cls)));
				assert(IsFalsified(cls[0]) && IsSatisfied(cls[1]) && GetAssignedDecLevel(cls[1]) <= GetAssignedDecLevel(cls[0]));
			}
		}
	}

	if (m_Cis.empty())
	{
		return TContradictionInfo();
	}
	else
	{
		span<TContradictionInfo> cisSpan = m_Cis.get_span();
		if (cisSpan.size() == 1 || m_ParamBestContradictionStrat == 2)
		{
			return m_Cis[0];
		}

		if (m_ParamBestContradictionStrat == 3)
		{
			return m_Cis.back();
		}

		if (m_ParamBestContradictionStrat == 0)
		{
			return *min_element(cisSpan.begin(), cisSpan.end(), [&](TContradictionInfo& ci)
			{
				return CiGetSize(ci);
			});
		}
		else
		{
			assert(m_ParamBestContradictionStrat == 1);
			return *min_element(cisSpan.begin(), cisSpan.end(), [&](TContradictionInfo& ci)
			{
				return GetGlueAndMarkCurrDecLevels(CiGetSpan(ci));
			});
		}
	}
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::ProcessDelayedImplication(TULit diL, TULit otherWatch, TUInd parentClsInd, CVector<TContradictionInfo>& cis)
{
	unordered_set<TUV> decLevelsRecalcBestScore;
	const auto initDl = m_DecLevel;
	auto& b = m_W;
	m_CurrPropWatchModifiedDuringProcessDelayedImplication = false;
	[[maybe_unused]] auto decLevelStart = m_DecLevel;
	// m_CurrentlyPropagatedLit can be BadULit only if the delayed implication emerged in a user-added clause
	const TUV propagatedDecLevel = m_CurrentlyPropagatedLit == BadULit ? BadUVar : GetAssignedDecLevel(m_CurrentlyPropagatedLit);
	const TUV cisMaxDecLevel = cis.empty() ? 0 : GetAssignedDecLevel(CiGetSpan(cis[0], 1)[0]);
	assert(cis.empty() || all_of(cis.get_span().begin(), cis.get_span().end(), [&](TContradictionInfo& ci) { return CiIsLegal(ci) && GetAssignedDecLevel(CiGetSpanDebug(ci)[0]) == cisMaxDecLevel && GetAssignedDecLevel(CiGetSpanDebug(ci)[1]) == cisMaxDecLevel; }));

	assert(NV(2) || P("***** ProcessDelayedImplication start: diL=" +
		SLit(diL) + "; otherWatch = " + (otherWatch == BadULit ? "BAD" : SLit(otherWatch)) + "; parent = " + (parentClsInd == BadClsInd ? "BAD" : SLits(Cls(parentClsInd))) + "\n" + STrail() + "\n"));

	assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TrailAssertConsistency());

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		assert(NV(2) || P("************************ ProcessDelayedImplication finish: l=" +
			SLit(diL) + "; otherWatch = " + (otherWatch == BadULit ? "BAD" : SLit(otherWatch)) + "; parent = " + (parentClsInd == BadClsInd ? "BAD" : SLits(Cls(parentClsInd))) + "\n" + STrail() + "\n"));
		//assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(false));
		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TrailAssertConsistency());
		m_Dis.clear();
		assert(decLevelStart == m_DecLevel || GetAssignedDecLevelVar(m_TrailEnd) != decLevelStart);
		if (m_CurrCustomBtStrat > 0 && !decLevelsRecalcBestScore.empty())
		{
			for (TUV dl : decLevelsRecalcBestScore)
			{
				if (dl <= m_DecLevel && !DecLevelIsCollapsed(dl))
				{
					m_BestScorePerDecLevel[dl] = CalcMaxDecLevelScore(dl);
				}
			}
		}
	});

	assert(IsSatisfied(diL) && ((otherWatch == BadULit && GetAssignedDecLevel(diL) > 0) || (IsFalsified(otherWatch) && GetAssignedDecLevel(diL) > GetAssignedDecLevel(otherWatch))));

	auto DisEmplaceBack = [&](TULit diL, TULit otherWatch, TUInd parentClsInd, uint64_t& stat)
	{
		assert(NV(2) || P("+++++++++++++++ ProcessDelayedImplication adding new literal: diL=" +
			SLit(diL) + "; otherWatch = " + (otherWatch == BadULit ? "BAD" : SLit(otherWatch)) + "; parent = " + (parentClsInd == BadClsInd ? "BAD" : SLits(Cls(parentClsInd))) + "\n"));

		assert(IsSatisfied(diL) && ((otherWatch == BadULit && GetAssignedDecLevel(diL) > 0) || (IsFalsified(otherWatch) && GetAssignedDecLevel(diL) > GetAssignedDecLevel(otherWatch))));

		m_Dis.emplace_back(TDelImpl(diL, otherWatch, parentClsInd));
		++stat;
	};

	DisEmplaceBack(diL, otherWatch, parentClsInd, m_Stat.m_DelayedImplicationsTrigerring);
	if (unlikely(IsUnrecoverable())) return false;

	while (!m_Dis.empty())
	{
		diL = m_Dis.back().m_L;
		parentClsInd = m_Dis.back().m_ParentClsInd;
		otherWatch = m_Dis.back().m_OtherWatch;
		const auto ccs = ConstClsSpan(parentClsInd);
		const auto cls = parentClsInd == BadClsInd ? span<TULit>(&otherWatch, 1) : (span<TULit>)ccs;

		if (unlikely(otherWatch != BadULit && parentClsInd != BadClsInd && otherWatch != cls[0] && otherWatch != cls[1]))
		{
			// This can happen, if l had already been processed during this very invocation of our function and the watch was changed
			assert(diL == cls[0] || diL == cls[1]);
			otherWatch = cls[diL == cls[0]];
		}

		m_Dis.pop_back();

		const TUV oldDecLevel = GetAssignedDecLevel(diL);
		const TUV newDecLevel = otherWatch == BadULit ? 0 : GetAssignedDecLevel(otherWatch);

		assert(NV(2) || P("**** ProcessDelayedImplication processing new literal: l=" +
			SLit(diL) + "; otherWatch = " + (otherWatch == BadULit ? "BAD" : SLit(otherWatch)) + "; parent = " + (parentClsInd == BadClsInd ? "BAD" : SLits(cls)) + "\n"));

		assert(IsSatisfied(diL) && (otherWatch == BadULit || IsFalsified(otherWatch)));

		if (oldDecLevel <= newDecLevel)
		{
			// This can happen, if l has already been processed during this very invocation of our function
			continue;
		}

		const bool decisionLevelCollapse = IsAssignedDec(diL);

		Unassign(diL);
		if (unlikely(decisionLevelCollapse))
		{
			++m_Stat.m_DelayedImplicationDecLevelsCollapsed;
			if (oldDecLevel == m_DecLevel)
			{
				// If l is the only (decision) literal at its decision level, the level will disappear after unassigning l
				// and propagating the unassignment below (in this invocation of the function)
				--m_DecLevel;
			}
		}
		else if (m_CurrCustomBtStrat > 0 && m_BestScorePerDecLevel[oldDecLevel] == m_VsidsHeap.get_var_score(GetVar(diL)))
		{
			decLevelsRecalcBestScore.insert(oldDecLevel);
		}

		Assign(diL, parentClsInd, otherWatch, newDecLevel, false);
		if (IsVisited(diL))
		{
			// If the literal was added to toPropagate, but its propagation hasn't been completed during the current BCP, 
			// we must re-propagate it in order not to miss implications
			// This might happen, e.g., if we discover a conflict at decision level 10 and remove l from toPropagate, since it was assigned at dec. level 10,
			// but then if the conflicting clause turns into an implication in this function, we need to re-propagate l
			ToPropagatePushBack(diL);
		}

		TWatchInfo& wi = m_Watches[Negate(diL)];
		if (wi.IsEmpty())
		{
			continue;
		}
		const volatile auto allWatches = b.get_ptr(wi.m_WBInd);
		TSpanTULit binWatches = TSpanTULit(allWatches + wi.GetLongEntries(), wi.m_BinaryWatches);

		for (auto otherWatchLocal : binWatches)
		{
			const bool isOtherWatchSatisfied = IsSatisfied(otherWatchLocal);
			if (isOtherWatchSatisfied && GetAssignedDecLevel(otherWatchLocal) > newDecLevel)
			{
				DisEmplaceBack(otherWatchLocal, Negate(diL), BadClsInd, m_Stat.m_DelayedImplicationsPropagated);
				if (unlikely(IsUnrecoverable())) return false;
			}
		}

		for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, b.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
		{
			TULit cachedLit = *currLongWatchPtr;
			const TUInd clsInd = *(TUInd*)(currLongWatchPtr + 1);
			
			assert(NV(2) || P("ProcessDelayedImplication: visiting long clause " + HexStr(*(TUInd*)(currLongWatchPtr + 1)) + ": cached " + SLit(cachedLit) + "; clause: " + SLits(Cls(*(TUInd*)(currLongWatchPtr + 1))) + "\n"));
			
			// Some extra-care must be taken if the literal hasn't been fully propagated
			// There might be a satisfied literal, which is not yet cached
			if (IsVisited(diL) && !IsSatisfied(cachedLit))
			{
				auto clsLocal = Cls(clsInd);
				assert(!ClsChunkDeleted(clsInd));
				auto itSat = find_if(clsLocal.begin(), clsLocal.end(), [&](TULit clsLit) { return IsSatisfied(clsLit) && GetAssignedDecLevel(clsLit) > newDecLevel; });
				if (itSat != clsLocal.end())
				{
					cachedLit = *currLongWatchPtr = *itSat;
					if (itSat - clsLocal.begin() >= 2)
					{
						assert(clsLocal[0] == Negate(diL) || clsLocal[1] == Negate(diL));
						const bool watchInd = clsLocal[0] == Negate(diL);
						if (unlikely(clsLocal[watchInd] == Negate(m_CurrentlyPropagatedLit)))
						{
							m_CurrPropWatchModifiedDuringProcessDelayedImplication = true;
						}
						SwapWatch(clsInd, watchInd, itSat);

						if (unlikely(IsUnrecoverable())) return false;
						// The line below is required to support the (very rare) occasions  of b's realloc actually moving b in WLAddLongWatch
						currLongWatchPtr = b.get_ptr(wi.m_WBInd) + (currLongWatchInd * TWatchInfo::BinsInLong);
					}
				}
			}
			// It is guaranteed that if the clause is satisfied with 1 literal and the rest are falsified, that literal must be cached by the falsified watch, 
			// unless BCP hasn't been completed for l, which is handled by the code above
			if (IsSatisfied(cachedLit))
			{
				auto clsLocal = Cls(clsInd);
				assert(!ClsChunkDeleted(clsInd));
				// Making sure, l is the first literal in the clause
				if (clsLocal[1] == Negate(diL)) swap(clsLocal[0], clsLocal[1]);
				assert(clsLocal[0] == Negate(diL));

				// Going over the rest of the clause (that is, skipping the watches) to find the best WL candidate to swap with l
				auto bestWLCandIt = FindBestWLCand(clsLocal, initDl);
				const TULit bestWLCandLit = *bestWLCandIt;

				if (!IsFalsified(bestWLCandLit))
				{
					// If the best-of-the-rest literal is not falsified, we just swap it with the current watch without further complications
					SwapCurrWatch(diL, bestWLCandIt, clsInd, clsLocal, currLongWatchInd, currLongWatchPtr, wi);
				}
				else
				{
					assert(IsFalsified(bestWLCandLit));

					const TULit litMaxDecLevel = *bestWLCandIt;
					const TUV maxDecLevel = GetAssignedDecLevel(litMaxDecLevel);
					assert(maxDecLevel == GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt(clsLocal, 2)));
					const TUV cachedDecLevel = GetAssignedDecLevel(cachedLit);

					if (maxDecLevel <= newDecLevel)
					{
						// If the maximal decision level of the rest is not greater the new l's level newDecLevel, then
						// l stays as the watch, but cachedLit should now implied at newDecLevel, if its current level is higher
						if (newDecLevel < cachedDecLevel)
						{
							DisEmplaceBack(cachedLit, Negate(diL), clsInd, m_Stat.m_DelayedImplicationsPropagated);
						}
						if (unlikely(IsUnrecoverable())) return false;
					}
					else
					{
						// If the maximal decision level of the rest is greater the new l's level newDecLevel, then
						// the maximal literal becomes the watch instead of l
						SwapCurrWatch(diL, bestWLCandIt, clsInd, clsLocal, currLongWatchInd, currLongWatchPtr, wi);

						// We re-imply cachedLit at maxDecLevel, if maxDecLevel < cachedDecLevel
						if (maxDecLevel < cachedDecLevel)
						{
							DisEmplaceBack(cachedLit, litMaxDecLevel, clsInd, m_Stat.m_DelayedImplicationsPropagated);
							if (unlikely(IsUnrecoverable())) return false;
						}
					}
				}
			}
		}
	}

	if (!cis.empty())
	{
		// The  loop below:
		// (1) Update the contradicting clauses, if required
		// (2) Checks if there was any change in the decision levels
		// (3) If there was a change, manages the backtrack level and whether that level is contradictory
		bool anyChange = false;
		bool isBacktrackLevelContradictory = false;
		TUV backtrackLevel = numeric_limits<TUV>::max();

		auto cisSpan = cis.get_span();
		for (TContradictionInfo& ci : cisSpan)
		{
			const auto ciSpan = CiGetSpan(ci);

			assert(NV(2) || P("Delayed implication contradiction handling start; clause: " + SLits((span<TULit>)ciSpan) + "\n"));

			if (!ci.m_IsContradictionInBinaryCls)
			{
				auto SwapWatchWithMaxNonWLIfRequired = [&](bool watchInd)
				{
					auto cls = Cls(ci.m_ParentClsInd);
					auto maxNonWLDecLevelIt = GetAssignedLitsHighestDecLevelIt(cls, 2);
					const TULit litMaxDecLevel = *maxNonWLDecLevelIt;
					const TUV maxDecLevel = GetAssignedDecLevel(litMaxDecLevel);

					if (maxDecLevel > GetAssignedDecLevel(ciSpan[watchInd]))
					{
						if (unlikely(cls[watchInd] == Negate(m_CurrentlyPropagatedLit)))
						{
							m_CurrPropWatchModifiedDuringProcessDelayedImplication = true;
						}
						SwapWatch(ci.m_ParentClsInd, watchInd, maxNonWLDecLevelIt);
						if (unlikely(cls[watchInd] == Negate(m_CurrentlyPropagatedLit)))
						{
							m_CurrPropWatchModifiedDuringProcessDelayedImplication = true;
						}
					}
				};
				SwapWatchWithMaxNonWLIfRequired(false);
				if (unlikely(IsUnrecoverable())) return false;
				SwapWatchWithMaxNonWLIfRequired(true);
				if (unlikely(IsUnrecoverable())) return false;
			}

			const bool currChanged = GetAssignedDecLevel(ciSpan[0]) != cisMaxDecLevel || GetAssignedDecLevel(ciSpan[1]) != cisMaxDecLevel;
			assert(NV(2) || P("Delayed implication contradiction handling end; clause: " + SLits((span<TULit>)ciSpan) + "; changed? = " + to_string(currChanged) + "\n"));

			if (currChanged)
			{
				anyChange = true;
				const bool currContradictory = GetAssignedDecLevel(ciSpan[0]) == GetAssignedDecLevel(ciSpan[1]);
				const TUV currBacktrackLevel = currContradictory ? GetAssignedDecLevel(ciSpan[0]) : max(GetAssignedDecLevel(ciSpan[0]), GetAssignedDecLevel(ciSpan[1])) - 1;

				if (currBacktrackLevel < backtrackLevel)
				{
					backtrackLevel = currBacktrackLevel;
					isBacktrackLevelContradictory = currContradictory;
				}
				else if (currBacktrackLevel == backtrackLevel && !isBacktrackLevelContradictory && currContradictory)
				{
					isBacktrackLevelContradictory = true;
				}
			}
		}

		if (anyChange)
		{
			BCPBacktrack(backtrackLevel, isBacktrackLevelContradictory);

			// Assign any units and mark for removal
			auto cisSpanLocal = cis.get_span();
			for (TContradictionInfo& ci : cisSpanLocal)
			{
				const auto ciSpan = CiGetSpan(ci, 2);

				// Note that, in a contradiction, one of the watches may have become satisfied after assigning a delayed implication for an earlier former contradiction
				// hence the condition IsAssigned(ciSpan[0]) != IsAssigned(ciSpan[1]) in the if below wouldn't have been sufficient
				if ((IsFalsified(ciSpan[0]) && !IsAssigned(ciSpan[1])) || (IsFalsified(ciSpan[1]) && !IsAssigned(ciSpan[0])))
				{
					const TULit unassignedLit = IsAssigned(ciSpan[0]) ? ciSpan[1] : ciSpan[0];
					const TULit assignedLit = IsAssigned(ciSpan[0]) ? ciSpan[0] : ciSpan[1];
					assert(IsFalsified(assignedLit));
					if (!ci.m_IsContradictionInBinaryCls && ci.m_ParentClsInd != BadClsInd)
					{
						// Update the cached literal in assignedLit to point to unassignedLit
						// Otherwise a *correctness* bug is possible, since the following condition, required for the correctness of ProcessDelayedImplication would not hold:
						// It is guaranteed that if the clause is satisfied with 1 literal and the rest are falsified, that literal must be cached by the falsified watch
						WLSetCached(assignedLit, ci.m_ParentClsInd, unassignedLit);
					}
					Assign(unassignedLit, ci.m_IsContradictionInBinaryCls ? BadClsInd : ci.m_ParentClsInd, assignedLit, GetAssignedDecLevel(assignedLit));
					// Marking the ci to be removed after this loop
					ci.m_IsContradiction = false;
				}
				else if (!(IsFalsified(ciSpan[0]) && IsFalsified(ciSpan[1])))
				{
					// Marking the ci to be removed after this loop
					ci.m_IsContradiction = false;
				}
			}

			// Remove any non-contradictory ci's (marked as non-contradictory in the previous loop)
			cis.erase_if_may_reorder([&](TContradictionInfo& ci)
			{
				return(!ci.IsContradiction());
			});

			return m_CurrentlyPropagatedLit == BadULit ? false : isBacktrackLevelContradictory ? propagatedDecLevel >= backtrackLevel : propagatedDecLevel > backtrackLevel;
		}
	}

	return false;
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
