// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include <functional>
#include <string>
#include <bit>
#include <iterator> 
#include <unordered_set> 
#include "Topi.hpp"

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::WLBinaryWatchExists(TULit l, TULit otherWatch)
{
	assert(l < m_Watches.cap());
	assert(otherWatch < m_Watches.cap());
	const TWatchInfo& wi = m_Watches[l];
	if (wi.m_BinaryWatches == 0)
	{
		return false;
	}
	const TSpanTULit binWatches = TSpanTULit(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
	return find(binWatches.begin(), binWatches.end(), otherWatch) != binWatches.end();
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::WLRemoveBinaryWatch(TULit l, TULit otherWatch)
{
	assert(WLBinaryWatchExists(l, otherWatch));

	TWatchInfo& wi = m_Watches[l];
	TSpanTULit binWatches = TSpanTULit(m_W.get_ptr(wi.m_WBInd) + wi.GetLongEntries(), wi.m_BinaryWatches);
	auto it = find(binWatches.begin(), binWatches.end(), otherWatch);
	*it = binWatches.back();

	assert(wi.m_BinaryWatches > 0);
	--wi.m_BinaryWatches;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::WLAddBinaryWatch(TULit l, TULit otherWatch)
{
	assert(l < m_Watches.cap());
	assert(otherWatch < m_Watches.cap());

	// Prepare the watch arena for our literal
	TULit* watchArena = WLPrepareArena(l, true, false);
	assert(watchArena != nullptr || IsUnrecoverable());
	// || watchArena == nullptr was added because of a Klocwork warning. WLPrepareArena guarantees that IsUnrecoverable() holds if watchArena == nullptr.
	if (unlikely(IsUnrecoverable()) || watchArena == nullptr) return;

	// Add the binary watch
	watchArena[m_Watches[l].GetUsedEntries()] = otherWatch;

	// Increase the number of binary watches in the watch-info
	++m_Watches[l].m_BinaryWatches;
}

template <typename TLit, typename TUInd, bool Compress>
size_t CTopi<TLit, TUInd, Compress>::WLGetLongWatchInd(TULit l, TUInd clsInd)
{
	// Go over the long watches
	TWatchInfo& wi = m_Watches[l];
	for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, m_W.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
	{
		const TUInd currClsInd = *(TUInd*)(currLongWatchPtr + 1);
		if (currClsInd == clsInd)
		{
			return currLongWatchInd;
		}
	}

	return numeric_limits<size_t>::max();
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::WLSetCached(TULit l, TUInd clsInd, TULit cachedLit)
{
	// Go over the long watches
	TWatchInfo& wi = m_Watches[l];
	for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, m_W.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
	{
		const TUInd currClsInd = *(TUInd*)(currLongWatchPtr + 1);
		if (currClsInd == clsInd)
		{
			*currLongWatchPtr = cachedLit;
			return;
		}
	}

	assert(0);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::WLReplaceInd(TULit l, TUInd clsInd, TUInd newClsInd)
{
	// Go over the long watches
	TWatchInfo& wi = m_Watches[l];
	for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, m_W.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
	{
		TUInd& currClsInd = *(TUInd*)(currLongWatchPtr + 1);
		if (currClsInd == clsInd)
		{
			currClsInd = newClsInd;
			return;
		}
	}

	assert(0);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::WLRemoveLongWatch(TULit l, size_t longWatchInd)
{
	assert(l < m_Watches.cap());

	TWatchInfo& wi = m_Watches[l];
	assert(longWatchInd < wi.m_LongWatches);
	TULit* watchArena = m_W.get_ptr(wi.m_WBInd);

	--wi.m_LongWatches;
	if (longWatchInd != wi.m_LongWatches)
	{
		memcpy(&watchArena[wi.GetLongEntry(longWatchInd)], &watchArena[wi.GetLongEntries()], TWatchInfo::BinsInLongBytes);
	}

	switch (wi.m_BinaryWatches)
	{
	case 0:
		break;
	case 1:
		watchArena[wi.GetLongEntries()] = watchArena[wi.GetLongEntries() + TWatchInfo::BinsInLong];
		break;
	default:
		if constexpr (TWatchInfo::BinsInLong <= 2)
		{
			// Copy the binary watches from the end of the binary arena to the end of the updated long arena
			memcpy(&watchArena[wi.GetLongEntries()], &watchArena[wi.GetUsedEntries()], TWatchInfo::BinsInLongBytes);
		}
		else
		{
			if (wi.m_BinaryWatches >= TWatchInfo::BinsInLong)
			{
				memcpy(&watchArena[wi.GetLongEntries()], &watchArena[wi.GetUsedEntries()], TWatchInfo::BinsInLongBytes);
			}
			else
			{
				memcpy(&watchArena[wi.GetLongEntries()], &watchArena[wi.GetUsedEntries() + TWatchInfo::BinsInLong - wi.m_BinaryWatches], wi.m_BinaryWatches * sizeof(TULit));
			}

		}

		break;
	}
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::WLAddLongWatch(TULit l, TULit inlinedLit, TUInd clsInd)
{
	assert(l < m_Watches.cap());
	assert(Compress || clsInd < m_B.cap());

	// Prepare the watch arena for our literal
	TULit* watchArena = WLPrepareArena(l, false, true);
	assert(watchArena != nullptr || IsUnrecoverable());
	// || watchArena == nullptr was added because of a Klocwork warning. WLPrepareArena guarantees that IsUnrecoverable() holds if watchArena == nullptr.
	if (unlikely(IsUnrecoverable()) || watchArena == nullptr) return;

	// Add the long watch
	TWatchInfo& wi = m_Watches[l];

	// Free space by moving binary watches, if required
	switch (wi.m_BinaryWatches)
	{
	case 0:
		break;
	case 1:
		watchArena[wi.GetUsedEntries() + LitsInInd] = watchArena[wi.GetLongEntries()];
		break;
	default:
		if constexpr (TWatchInfo::BinsInLong <= 2)
		{
			memcpy(&watchArena[wi.GetUsedEntries()], &watchArena[wi.GetLongEntries()], TWatchInfo::BinsInLongBytes);
		}
		else
		{
			if (wi.m_BinaryWatches >= TWatchInfo::BinsInLong)
			{
				memcpy(&watchArena[wi.GetUsedEntries()], &watchArena[wi.GetLongEntries()], TWatchInfo::BinsInLongBytes);
			}
			else
			{
				memcpy(&watchArena[wi.GetUsedEntries() + TWatchInfo::BinsInLong - wi.m_BinaryWatches], &watchArena[wi.GetLongEntries()], wi.m_BinaryWatches * sizeof(TULit));
			}
		}
		break;
	}

	// Insert the long watch
	auto indBeyondLongWatches = wi.GetLongEntries();
	watchArena[indBeyondLongWatches++] = inlinedLit;
	*(TUInd*)(watchArena + indBeyondLongWatches) = clsInd;

	// Increase the number of long watches in the watch-info
	++wi.m_LongWatches;
}

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::TULit* CTopi<TLit, TUInd, Compress>::WLPrepareArena(TULit l, bool allowNewBinaryWatch, bool allowNewLongWatch)
{
	TWatchInfo& wi = m_Watches[l];

	auto AllocateNewArenaAndCopyOldArenaIfAny = [&](TUInd sz) -> TULit*
	{
		TUInd allocatedWordsRequired = m_WNext + sz;
		if (unlikely(allocatedWordsRequired < m_WNext) || 
			((double)(m_WWasted + m_WNext) > (double)m_WNext * m_ParamMultWasteWatches))
		{
			CompressWLs();
			allocatedWordsRequired = m_WNext + sz;
			if (unlikely(allocatedWordsRequired < m_WNext))
			{
				SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, "WLPrepareArena: reached the end of the buffer");
				return nullptr;
			}
		}


		m_W.reserve_beyond_if_requried(allocatedWordsRequired, true);
		if (unlikely(m_W.uninitialized_or_erroneous()))
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "BReallocBeyond: couldn't reserve m_B");
			return nullptr;
		}

		if (!wi.IsEmpty())
		{
			m_W.memcpy(m_WNext, wi.m_WBInd, wi.GetUsedEntries());
			MarkWatchBufferChunkDeleted(wi);

		}
		wi.PointToNewArena(m_WNext, sz);
		m_WNext += wi.m_AllocatedEntries;
		return m_W.get_ptr(m_WNext - wi.m_AllocatedEntries);
	};

	const TUInd currRequiredEntries = (TUInd)allowNewBinaryWatch + ((TUInd)allowNewLongWatch * TWatchInfo::BinsInLong);
	if (unlikely(wi.IsEmpty()))
	{
		return AllocateNewArenaAndCopyOldArenaIfAny(m_ParamInitEntriesPerWL < currRequiredEntries ? bit_ceil(currRequiredEntries) : m_ParamInitEntriesPerWL);
	}

	assert(!wi.IsEmpty());

	const TUInd requiredEntries = wi.GetUsedEntries() + currRequiredEntries;

	if (requiredEntries <= wi.m_AllocatedEntries)
	{
		return m_W.get_ptr(wi.m_WBInd);
	}

	if (unlikely(requiredEntries < wi.GetUsedEntries()))
	{
		// Overflow
		SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, "The actual number of entries for one literal doesn't fit into the buffer");
		return nullptr;
	}

	// Re-allocation is required

	if (unlikely(wi.m_AllocatedEntries == TWatchInfo::MaxWatchInfoAlloc))
	{
		// Reached the maximum allocation!
		SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, "The watch list for one literal doesn't fit into the buffer");
		return nullptr;
	}

	m_WWasted += wi.m_AllocatedEntries;
	return AllocateNewArenaAndCopyOldArenaIfAny(wi.m_AllocatedEntries << 1);
}

/*bool CTopi<TLit,TUInd,Compress>::WLAssertConsistency(bool testMissedImplications)
{
	using TBinCls = pair<TULit, TULit>;

	TULit l = 29;

	{
		TWatchInfo& wi = m_Watches[l];

		[[maybe_unused]] const TUInd bInd = wi.m_BInd;
		const TUInd binaryWatches = wi.m_BinaryWatches;


		if (binaryWatches != 0)
		{
			TSpanTULit binWatches = m_W.get_span_cap(wi.m_BInd + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TULit secondLit : binWatches)
			{
				assert(secondLit != 0);
				assert(secondLit < GetNextLit());
				TBinCls clsBinCls = make_pair(l, secondLit);
				array<TULit, 2> clsArray = { clsBinCls.first, clsBinCls.second };
				TSpanTULit cls(clsArray);
				if (testMissedImplications && (IsFalsified(cls[0]) || IsFalsified(cls[1])))
				{
					if (IsFalsified(cls[0]) && IsFalsified(cls[1])) cout << "***ASSERTION-FAILURE FF-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(!(IsFalsified(cls[0]) && IsFalsified(cls[1])));

					if (!(IsSatisfied(cls[0]) || IsSatisfied(cls[1]))) cout << "***ASSERTION-FAILURE one-falsified-no-satisfied-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(IsSatisfied(cls[0]) || IsSatisfied(cls[1]));

					const TULit satLit = cls[IsSatisfied(cls[1])];
					const TULit falseLit = cls[IsFalsified(cls[1])];
					assert(satLit != falseLit);

					if (!(GetAssignedDecLevel(satLit) <= GetAssignedDecLevel(falseLit))) cout << "***ASSERTION-FAILURE one-falsified-satisfied-higherdl-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(GetAssignedDecLevel(satLit) <= GetAssignedDecLevel(falseLit));
				}
			}
		}
	}

	return true;
}*/

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::WLAssertNoMissedImplications()
{
	auto& b = m_W;
	for (TUVar v = m_TrailStart; v != BadUVar; v = m_VarInfo[v].m_TrailNext)
	{
		const TULit l = Negate(GetAssignedLitForVar(v));
		TWatchInfo& wi = m_Watches[l];

		[[maybe_unused]] const TUInd bInd = wi.m_WBInd;
		[[maybe_unused]] const TUInd longWatches = wi.m_LongWatches;
		const TUInd allocatedEntries = wi.m_AllocatedEntries;
		const TUInd binaryWatches = wi.m_BinaryWatches;

		if (allocatedEntries == 0)
		{
			continue;
		}

		assert((size_t)bInd < b.cap());
		assert((size_t)bInd + allocatedEntries <= b.cap());
		assert(longWatches * 2 + binaryWatches <= allocatedEntries);

		if (binaryWatches != 0)
		{
			TSpanTULit binWatches = b.get_span_cap(wi.m_WBInd + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TULit secondLit : binWatches)
			{
				assert(secondLit != 0);
				assert(secondLit < GetNextLit());
				array<TULit, 2> clsArray = { l, secondLit };
				TSpanTULit cls(clsArray);
				if (IsFalsified(cls[0]) || IsFalsified(cls[1]))
				{
					if (IsFalsified(cls[0]) && IsFalsified(cls[1])) cout << "***ASSERTION-FAILURE FF-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(!(IsFalsified(cls[0]) && IsFalsified(cls[1])));

					if (!(IsSatisfied(cls[0]) || IsSatisfied(cls[1]))) cout << "***ASSERTION-FAILURE one-falsified-no-satisfied-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(IsSatisfied(cls[0]) || IsSatisfied(cls[1]));

					const TULit satLit = cls[IsSatisfied(cls[1])];
					const TULit falseLit = cls[IsFalsified(cls[1])];
					assert(satLit != falseLit);

					if (!(GetAssignedDecLevel(satLit) <= GetAssignedDecLevel(falseLit))) cout << "***ASSERTION-FAILURE one-falsified-satisfied-higherdl-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(GetAssignedDecLevel(satLit) <= GetAssignedDecLevel(falseLit));
				}
			}
		}

		for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, b.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
		{
			[[maybe_unused]] const TULit cachedLit = *currLongWatchPtr;

			const TUInd clsInd = *(TUInd*)(currLongWatchPtr + 1);
			assert(Compress || clsInd < m_B.cap() - 1);

			const auto cls = ConstClsSpan(clsInd);

			if constexpr (!Compress)
			{
				if (!(clsInd >= m_FirstLearntClsInd || !ClsGetIsLearnt(clsInd))) cout << "***ASSERTION-FAILURE First-Learnt at " << SLits(cls) << endl << STrail() << endl;
				assert(clsInd >= m_FirstLearntClsInd || !ClsGetIsLearnt(clsInd));
			}

			assert(cls[0] == l || cls[1] == l);
			if (find(cls.begin(), cls.end(), cachedLit) == cls.end())
			{
				cout << "***ASSERTION-FAILURE CL- at cached " << SLit(cachedLit) << "; cls = " << SLits(cls) << endl << STrail() << endl;
				assert(0);
			}

			if (IsFalsified(cls[0]) || IsFalsified(cls[1]))
			{
				const TULit falseLit = cls[IsFalsified(cls[1])];
				const TUV falseLitDecLevel = GetAssignedDecLevel(falseLit);
				const TULit otherLit = cls[!IsFalsified(cls[1])];
				const TUV otherLitDecLevel = IsAssigned(otherLit) ? GetAssignedDecLevel(otherLit) : BadUVar;

				if (IsSatisfied(otherLit))
				{
					auto it = find_if(cls.begin() + 2, cls.end(), [&](TULit l)
					{
						return IsSatisfied(l) && GetAssignedDecLevel(l) <= falseLitDecLevel;
					});
					if (!(otherLitDecLevel <= falseLitDecLevel || it != cls.end())) cout << "***ASSERTION-FAILURE FS at " << SLits(cls) << endl << STrail() << endl;
					assert(otherLitDecLevel <= falseLitDecLevel || it != cls.end());
				}

				if (!IsAssigned(otherLit))
				{
					auto it = find_if(cls.begin() + 2, cls.end(), [&](TULit l)
					{
						return IsSatisfied(l) && GetAssignedDecLevel(l) <= falseLitDecLevel;
					});
					if (!(it != cls.end())) cout << "***ASSERTION-FAILURE F- at " << SLits(cls) << endl << STrail() << endl;
					assert(it != cls.end());
				}

				if (IsFalsified(otherLit))
				{
					auto it = find_if(cls.begin() + 2, cls.end(), [&](TULit l)
					{
						return IsSatisfied(l) && GetAssignedDecLevel(l) <= falseLitDecLevel && GetAssignedDecLevel(l) <= otherLitDecLevel;
					});
					if (!(it != cls.end())) cout << "***ASSERTION-FAILURE FF at " << SLits(cls) << endl << STrail() << endl;
					assert(it != cls.end());
				}
			}
		}
	}

	return true;
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::WLAssertConsistency(bool testMissedImplications)
{
	if (m_ParamAssertConsistency < 3)
	{
		return testMissedImplications ? WLAssertNoMissedImplications() : true;
	}

	auto& b = m_W;

	unordered_map<TUInd, uint8_t> longCls2Watches;

	using TBinCls = pair<TULit, TULit>;

	struct hash_pair {
		size_t operator()(const TBinCls& p) const
		{
			auto hash1 = hash<TULit>{}(p.first);
			auto hash2 = hash<TULit>{}(p.second);
			return hash1 ^ hash2;
		}
	};

	unordered_set<TBinCls, hash_pair> binClssTwice;

	for (TULit l = 1; l < GetNextLit(); ++l)
	{
		TWatchInfo& wi = m_Watches[l];

		const TUInd allocatedEntries = wi.m_AllocatedEntries;
		if (allocatedEntries == 0)
		{
			continue;
		}

		[[maybe_unused]] const TUInd wbInd = wi.m_WBInd;
		[[maybe_unused]] const TUInd longWatches = wi.m_LongWatches;

		const TUInd binaryWatches = wi.m_BinaryWatches;

		assert((size_t)wbInd < m_W.cap());
		assert((size_t)wbInd + allocatedEntries <= m_W.cap());
		assert(longWatches * 2 + binaryWatches <= allocatedEntries);

		if (binaryWatches != 0)
		{
			TSpanTULit binWatches = b.get_span_cap(wi.m_WBInd + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TULit secondLit : binWatches)
			{
				assert(secondLit != 0);
				assert(secondLit < GetNextLit());
				TBinCls clsBinCls = make_pair(l, secondLit);
				assert(binClssTwice.find(clsBinCls) == binClssTwice.end());
				binClssTwice.insert(clsBinCls);
				array<TULit, 2> clsArray = { clsBinCls.first, clsBinCls.second };
				TSpanTULit cls(clsArray);
				if (testMissedImplications && (IsFalsified(cls[0]) || IsFalsified(cls[1])))
				{
					if (IsFalsified(cls[0]) && IsFalsified(cls[1])) cout << "***ASSERTION-FAILURE FF-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(!(IsFalsified(cls[0]) && IsFalsified(cls[1])));

					if (!(IsSatisfied(cls[0]) || IsSatisfied(cls[1]))) cout << "***ASSERTION-FAILURE one-falsified-no-satisfied-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(IsSatisfied(cls[0]) || IsSatisfied(cls[1]));

					const TULit satLit = cls[IsSatisfied(cls[1])];
					const TULit falseLit = cls[IsFalsified(cls[1])];
					assert(satLit != falseLit);

					if (!(GetAssignedDecLevel(satLit) <= GetAssignedDecLevel(falseLit))) cout << "***ASSERTION-FAILURE one-falsified-satisfied-higherdl-for-binary at " << SLits(cls) << endl << STrail() << endl;
					assert(GetAssignedDecLevel(satLit) <= GetAssignedDecLevel(falseLit));
				}
			}
		}

		for (auto [currLongWatchInd, currLongWatchPtr] = make_pair((size_t)0, b.get_ptr(wi.m_WBInd)); currLongWatchInd < wi.m_LongWatches; ++currLongWatchInd, currLongWatchPtr += TWatchInfo::BinsInLong)
		{
			[[maybe_unused]] const TULit cachedLit = *currLongWatchPtr;

			const TUInd clsInd = *(TUInd*)(currLongWatchPtr + 1);
			assert(Compress || clsInd < m_B.cap() - 1);

			const auto cls = ConstClsSpan(clsInd);

			if constexpr (!Compress)
			{
				if (!(clsInd >= m_FirstLearntClsInd || !ClsGetIsLearnt(clsInd))) cout << "***ASSERTION-FAILURE First-Learnt at " << SLits(cls) << endl << STrail() << endl;
				assert(clsInd >= m_FirstLearntClsInd || !ClsGetIsLearnt(clsInd));
			}

			assert(cls[0] == l || cls[1] == l);
			if (!(find(cls.begin(), cls.end(), cachedLit) != cls.end())) cout << "***ASSERTION-FAILURE F- at " << SLits(cls) << endl << STrail() << endl;
			assert(find(cls.begin(), cls.end(), cachedLit) != cls.end());

			if (testMissedImplications && (IsFalsified(cls[0]) || IsFalsified(cls[1])))
			{
				const TULit falseLit = cls[IsFalsified(cls[1])];
				const TUV falseLitDecLevel = GetAssignedDecLevel(falseLit);
				const TULit otherLit = cls[!IsFalsified(cls[1])];
				const TUV otherLitDecLevel = IsAssigned(otherLit) ? GetAssignedDecLevel(otherLit) : BadUVar;

				if (IsSatisfied(otherLit))
				{
					auto it = find_if(cls.begin() + 2, cls.end(), [&](TULit l)
					{
						return IsSatisfied(l) && GetAssignedDecLevel(l) <= falseLitDecLevel;
					});
					if (!(otherLitDecLevel <= falseLitDecLevel || it != cls.end())) cout << "***ASSERTION-FAILURE FS at " << SLits(cls) << endl << STrail() << endl;
					assert(otherLitDecLevel <= falseLitDecLevel || it != cls.end());
				}

				if (!IsAssigned(otherLit))
				{
					auto it = find_if(cls.begin() + 2, cls.end(), [&](TULit l)
					{
						return IsSatisfied(l) && GetAssignedDecLevel(l) <= falseLitDecLevel;
					});
					if (!(it != cls.end())) cout << "***ASSERTION-FAILURE F- at " << SLits(cls) << endl << STrail() << endl;
					assert(it != cls.end());
				}

				if (IsFalsified(otherLit))
				{
					auto it = find_if(cls.begin() + 2, cls.end(), [&](TULit l)
					{
						return IsSatisfied(l) && GetAssignedDecLevel(l) <= falseLitDecLevel && GetAssignedDecLevel(l) <= otherLitDecLevel;
					});
					if (!(it != cls.end())) cout << "***ASSERTION-FAILURE FF at " << SLits(cls) << endl << STrail() << endl;
					assert(it != cls.end());
				}
			}

			auto it = longCls2Watches.find(clsInd);
			if (it == longCls2Watches.end())
			{
				longCls2Watches[clsInd] = 1;
			}
			else
			{
				assert(it->second == 1);
				++it->second;
			}
		}
	}


	assert(longCls2Watches.size() == m_Stat.m_ActiveLongClss);
	assert(all_of(longCls2Watches.begin(), longCls2Watches.end(), [&](const pair<TUInd, uint8_t>& p) { return p.second == 2; }));

	assert(binClssTwice.size() == m_Stat.m_ActiveBinaryClss * 2);

	for (const TBinCls& binCls : binClssTwice)
	{
		const TULit l1 = binCls.first;
		const TULit l2 = binCls.second;
		[[maybe_unused]] const TBinCls binClsReversed = make_pair(l2, l1);
		assert(find(binClssTwice.begin(), binClssTwice.end(), binClsReversed) != binClssTwice.end());
	}

	return true;
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::WLIsLitBetter(TULit lCand, TULit lOther) const
{
	const auto lCandWLEntries = m_ParamBCPWLChoice == 0 ? m_Watches[lCand].GetUsedEntries() : m_ParamBCPWLChoice == 1 ? m_Watches[lOther].GetUsedEntries() : 1;
	const auto lOtherWLEntries = m_ParamBCPWLChoice == 0 ? m_Watches[lOther].GetUsedEntries() : m_ParamBCPWLChoice == 1 ? m_Watches[lCand].GetUsedEntries() : 0;

	if (IsSatisfied(lCand))
	{
		if (!IsSatisfied(lOther))
		{
			return true;
		}

		return GetAssignedDecLevel(lCand) < GetAssignedDecLevel(lOther) ||
			(GetAssignedDecLevel(lCand) == GetAssignedDecLevel(lOther) && lCandWLEntries < lOtherWLEntries);
	}
	else if (!IsAssigned(lCand))
	{
		if (IsSatisfied(lOther))
		{
			return false;
		}

		if (IsFalsified(lOther))
		{
			return true;
		}

		assert(!IsAssigned(lOther));

		return lCandWLEntries < lOtherWLEntries;
	}
	else
	{
		assert(IsFalsified(lCand));

		if (IsSatisfied(lOther) || !IsAssigned(lOther))
		{
			return false;
		}

		return GetAssignedDecLevel(lCand) > GetAssignedDecLevel(lOther) ||
			(GetAssignedDecLevel(lCand) == GetAssignedDecLevel(lOther) && lCandWLEntries < lOtherWLEntries);
	}
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
