#include "Topi.hpp"
#include "ToporBitArrayBuffer.hpp"
#include "SetInScope.h"
#include <numeric>

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
pair<bool, bool> CTopi<TLit, TUInd, Compress>::BCDeleteLitsByMovingToOtherBufferIfRequiredAssumingLastDeleted(TUInd& clsInd, TUV origSize, TUV lits2Remove, bool insertToSpareIfIteratorsBecomeInvalidated)
{
	if (!BCDeleteLitsCausesMoveToAnotherBuffer(origSize, lits2Remove))
	{
		return make_pair(false, false);
	}
	
	// This is the corner-case, when, after deleting the literals, the clause must move to another compressed clause buffer
	auto ccs = ConstClsSpan(clsInd);	
	bool spareUsed = false;
	const TBCInd newBcInd = BCCompress(ccs.subspan(0, origSize - lits2Remove), ClsGetIsLearnt(clsInd),
		ClsGetIsLearnt(clsInd) ? ClsGetGlue(clsInd) : 0,
		ClsGetIsLearnt(clsInd) ? ClsGetSkipdel(clsInd) : false,
		ClsGetIsLearnt(clsInd) ? ClsGetActivity(clsInd) : 0.0f,
		insertToSpareIfIteratorsBecomeInvalidated, &spareUsed);
	
	for (uint8_t currWatchI = 0; currWatchI <= 1; ++currWatchI)
	{
		WLReplaceInd(ccs[currWatchI], clsInd, (TUInd)newBcInd);
	}
	
	// Marking the old clause as removed
	const TBCInd bcInd = clsInd;
	const TBCHashId bcHashInd = bcInd.GetHashId();
	BCGetBitArray(bcHashInd).bit_set(0, bcInd.m_BitsForLit, bcInd.BitFirstLit());
	
	// Counting the removed header and the rest of the removed literals as waste (the removed literals themselves are supposed to be counted as waste by the caller)
	m_BWasted += bcHashInd.GetFirstLitOffset() + bcHashInd.m_BitsForLit * (origSize - lits2Remove);

	// Updating clsInd to the new clause location
	clsInd = (TUInd)newBcInd;

	assert(NV(2) || insertToSpareIfIteratorsBecomeInvalidated || P("\tBCDeleteLitsByMovingToOtherBufferIfRequiredAssumingLastDeleted, new clause: " + SLits(Cls(clsInd)) + "\n"));

	return make_pair(true, spareUsed);
}

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::TBCInd CTopi<TLit, TUInd, Compress>::BCCompress(span<TULit> cls, bool isLearnt, TUV glue, bool stayForAnotherRound, float activity, bool insertToSpareIfIteratorsBecomeInvalidated, bool* spareUsedinCompressed)
{
	const TBCHashId bcHashId(isLearnt, BCClsSize2Bits(cls.size()), BCMaxLitWidth(cls));
	
	pair<unordered_map<uint16_t, CBitArray>::iterator, bool> itExists;

	const auto bcSizeBefore = m_BC.size();

	try
	{		
		if (insertToSpareIfIteratorsBecomeInvalidated && 
			bcSizeBefore + 1 >= m_BC.max_load_factor() * m_BC.bucket_count() && 
			m_BC.find(bcHashId) == m_BC.end())
		{
			// Iterators are going to be invalidated, if the member is new (to test if it is, test if the size will have been changed)!
			itExists = m_BCSpare.insert(make_pair(bcHashId, CBitArray()));
			if (spareUsedinCompressed != nullptr)
			{
				*spareUsedinCompressed = true;
			}
		}
		else
		{
			itExists = m_BC.insert(make_pair(bcHashId, CBitArray()));
			if (spareUsedinCompressed != nullptr)
			{
				*spareUsedinCompressed = false;
			}
		}				
	}
	catch (...)
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "BAddInitClause: couldn't insert into m_BC");
		return TBCInd();
	}
	
	CBitArray& ba = itExists.first->second;
	
	const uint64_t newBitsRequired = (uint64_t)bcHashId.m_BitsForClsSize + (uint64_t)cls.size() * (uint64_t)bcHashId.m_BitsForLit + (isLearnt ? bcHashId.GetBitsGlue() + 32 : 0);
	
	ba.bit_reserve_new_chunk(newBitsRequired);
	if (unlikely(ba.uninitialized_or_erroneous()))
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "BCCompress: couldn't reserve new bit chunk");
		return TBCInd();
	}

	const uint64_t bitStart = ba.bit_get_next_bit();
	
	// Push the size
	if (bcHashId.m_BitsForClsSize != 0)
	{
		ba.bit_push(BCClsSize2EncodedClsSize(cls.size()), bcHashId.m_BitsForClsSize);
		assert(ba.bit_get(bitStart, bcHashId.m_BitsForClsSize) == BCClsSize2EncodedClsSize(cls.size()));
	}	

	if (isLearnt)
	{
		// Push the glue
		if (glue > bcHashId.MaxGlue())
		{
			glue = bcHashId.MaxGlue();
		}
		ba.bit_push(glue, bcHashId.GetBitsGlue());
		assert(ba.bit_get(bitStart + bcHashId.m_BitsForClsSize, bcHashId.GetBitsGlue()) == glue);
		// Push stay-for-another-round
		ba.bit_push(stayForAnotherRound, 1);
		assert(ba.bit_get(bitStart + bcHashId.m_BitsForClsSize + bcHashId.GetBitsGlue(), 1) == (uint64_t)stayForAnotherRound);
		// Push the activity
		assert(activity >= 0.f);
		const uint32_t* uint32ActivityPtr = (uint32_t*)&activity;
		ba.bit_push(*uint32ActivityPtr, 31);
		assert(ba.bit_get(bitStart + bcHashId.m_BitsForClsSize + bcHashId.GetBitsGlue() + 1, 31) == *uint32ActivityPtr);
	}	

	// Push the literals
	for (const TULit l : cls)
	{
		ba.bit_push(l, bcHashId.m_BitsForLit);
		assert(ba.bit_get(ba.bit_get_next_bit() - bcHashId.m_BitsForLit, bcHashId.m_BitsForLit) == l);
	}

	assert(ba.bit_get_next_bit() == bitStart + newBitsRequired);

	const TBCInd bci(bcHashId, bitStart);
	if (bci.IsError())
	{
		stringstream ss;
		ss << "BCCompress: index too narrow -- bcHashId = " << hex << bcHashId << "; bitStart = " << dec << bitStart;
		SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, ss.str());
	}

	return bci;
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::BCIsIdenticalToDecompressed(TUInd clsInd, const TBCHashId currHashId, uint64_t bStart)
{
	CBitArray& ba = m_BC.at(currHashId);	
	const CCompressedCls cc(ba, TBCInd(currHashId, bStart));

	const bool isOversized = currHashId.m_IsLearnt && cc.size() > ClsLearntMaxSizeWithGlue;

	if (isOversized)
	{
		assert(0);
		return false;
	}

	if (ClsGetSize<false>(clsInd) != cc.size())
	{
		assert(0);
		return false;
	}

	if (ClsGetIsLearnt<false>(clsInd) != cc.IsLearnt())
	{
		assert(0);
		return false;
	}

	if (cc.IsLearnt())
	{
		const TUV glueC = ClsGetGlue<false>(clsInd);
		const TUV glueCC = cc.BufferGetGlue();
		if (glueC != glueCC)
		{
			assert(0);
			return false;
		}
		
		const bool skipDelC = ClsGetSkipdel<false>(clsInd);
		const bool skipDelCC = cc.BufferGetSkipDel();
		if (skipDelC != skipDelCC)
		{
			assert(0);
			return false;
		}		

		const bool activityC = ClsGetActivity<false>(clsInd);
		const bool activityCC = cc.BufferGetActivity();
		if (activityC != activityCC)
		{
			assert(0);
			return false;
		}
	}

	const CCompressedClsConstSnapshotDense cls(ba, clsInd);
	for (TUInd i = 0; i < cls.size(); ++i)
	{
		if (cls[i] != cc[i])
		{
			assert(0);
			return false;
		}
	}

	return true;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::BCDecompress(const TBCHashId currHashId, uint64_t& b)
{
	[[maybe_unused]] const auto bStart = b;
	CBitArray& ba = m_BC[currHashId];
	const uint16_t encodedClsSize = (uint16_t)ba.bit_get_and_advance(b, currHashId.m_BitsForClsSize);
	const TUInd clsSize = BCEncodedClsSize2ClsSizeConst(encodedClsSize, currHashId.m_BitsForClsSize);

	const bool isOversized = currHashId.m_IsLearnt && clsSize > ClsLearntMaxSizeWithGlue;
	assert(!isOversized);

	const auto clsLitsStartOffset = EClsLitsStartOffset(currHashId.m_IsLearnt, isOversized);
	const TUInd newBNext = m_BNext + clsLitsStartOffset + clsSize;

	if (unlikely(newBNext < m_BNext))
	{
		SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, "BCExtractClause: too many literals in all the clauses combined");
		return;
	}

	if (unlikely(newBNext >= m_B.cap()))
	{
		m_B.reserve_atleast(newBNext);
		if (unlikely(m_B.uninitialized_or_erroneous()))
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "BCExtractClause: couldn't reserve the buffer");
			return;
		}
	}

	// Order of setting the fields is important, since ClsSetSize depends on ClsSetSize and ClsSetGlue depends on both
	EClsSetIsLearnt(m_BNext, currHashId.m_IsLearnt);
	ClsSetSize(m_BNext, (TUV)clsSize);
	if (currHashId.m_IsLearnt)
	{
		const uint8_t glueBits = currHashId.GetBitsGlue();
		const TUV glue = (TUV)ba.bit_get_and_advance(b, glueBits);
		ClsSetGlue<false>(m_BNext, glue);

		const bool skipDel = (bool)ba.bit_get_and_advance(b, 1);
		ClsSetSkipdel<false>(m_BNext, skipDel);

		const uint32_t activityUint32 = (uint32_t)ba.bit_get_and_advance(b, 31);
		const float activity = *(float*)&activityUint32;
		ClsSetActivity<false>(m_BNext, activity);
	}

	const auto litsStart = m_BNext + clsLitsStartOffset;
	for (TUInd i = 0; i < clsSize; ++i)
	{
		const TUV l = (TUV)ba.bit_get_and_advance(b, currHashId.m_BitsForLit);
		m_B[litsStart + i] = l;
	}

	assert(BCIsIdenticalToDecompressed(m_BNext, currHashId, bStart));
}

template <typename TLit, typename TUInd, bool Compress>
size_t CTopi<TLit, TUInd, Compress>::BCNextBitSum() const
{
	const auto r = accumulate(m_BC.begin(), m_BC.end(), (size_t)0, [&](size_t sum, auto& it)
	{
		return sum + (size_t)it.second.bit_get_next_bit();
	});

	return r;
}

template <typename TLit, typename TUInd, bool Compress>
size_t CTopi<TLit, TUInd, Compress>::BCCapacitySum() const
{
	const auto r = accumulate(m_BC.begin(), m_BC.end(), (size_t)0, [&](size_t sum, auto& it)
	{
		return sum + (size_t)it.second.cap();
	});

	return r;
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
