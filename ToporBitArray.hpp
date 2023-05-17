// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include "ToporDynArray.hpp"
#include "ToporBitArrayBuffer.hpp"

using namespace BitArray;

namespace Topor
{
	class CBitArray : public CDynArray<uint64_t>
	{
	public:
		
		void bit_reserve_new_chunk(uint64_t newBitsRequired, unsigned char initVal = 0)
		{
			const auto newCap = BitEntry2Uint64Entry(m_NextBit + newBitsRequired) + 1;
			if (newCap > cap())
			{
				reserve_atleast(newCap, initVal);
			}			
		}

		inline void bit_remove_last_bits(uint64_t bitsRemoved)
		{
			assert(m_NextBit >= bitsRemoved);
			m_NextBit -= bitsRemoved;
		}
		
		void bit_resize_and_compress(uint64_t newBitsRequired)
		{
			m_NextBit = newBitsRequired;
			reserve_exactly(BitEntry2Uint64Entry(m_NextBit) + (m_NextBit != 0));
		}

		void compress()
		{
			bit_resize_and_compress(m_NextBit);
		}

		template <class TConvertableToUint64>
		void bit_push(TConvertableToUint64 elem, uint8_t elemWidth)
		{
			CSetElemAndGoToNext(m_B, m_NextBit, elemWidth, (uint64_t)elem);
		}

		template <class TConvertableToUint64>
		void bit_set(TConvertableToUint64 elem, uint8_t elemWidth, uint64_t bitNum)
		{
			CSetElem(m_B, bitNum, elemWidth, (uint64_t)elem);
		}

		template <class TConvertableToUint64>
		void bit_set_and_advance(TConvertableToUint64 elem, uint8_t elemWidth, uint64_t& bitNum)
		{
			CSetElemAndGoToNext(m_B, bitNum, elemWidth, (uint64_t)elem);
		}

		uint64_t bit_get(uint64_t b, uint8_t elemWidth) const
		{
			return CGetElem(m_B, b, elemWidth);
		}

		uint64_t bit_get_and_advance(uint64_t& b, uint8_t elemWidth) const
		{
			return CGetElemAndGoToNext(m_B, b, elemWidth);
		}

		inline uint64_t bit_get_next_bit() const { return m_NextBit; }

		inline void bit_set_next_bit(uint64_t nextBit) { m_NextBit = nextBit; }

		void copy(uint64_t bFrom, uint64_t bTo, uint8_t elemWidth)
		{
			assert(m_NextBit - elemWidth < m_NextBit);
			assert(bFrom <= m_NextBit - elemWidth);
			assert(bTo <= m_NextBit - elemWidth);			
			const auto v = CGetElem(m_B, bFrom, elemWidth);
			CSetElem(m_B, bTo, elemWidth, v);
			assert(CGetElem(m_B, bTo, elemWidth) == v);
		}	

		void copy_block(uint64_t bFrom, uint64_t bTo, uint64_t overallWidth)
		{
			assert(m_NextBit - overallWidth < m_NextBit);
			assert(bFrom <= m_NextBit - overallWidth);
			assert(bTo <= m_NextBit - overallWidth);

			if (overallWidth <= 64)
			{
				copy(bFrom, bTo, (uint8_t)overallWidth);
				return;
			}

			auto GetDiv = [&](uint64_t n) { return n >> 6; };
			auto GetRem = [&](uint64_t n) { return n & 63; };

			if (GetRem(bFrom) == 0 && GetRem(bTo) == 0)
			{
				auto MemMove = [&](size_t outStartInd, size_t inpStartInd, size_t entriesToCopy)
				{
					if (outStartInd >= inpStartInd + entriesToCopy || inpStartInd >= outStartInd + entriesToCopy)
					{
						std::memcpy(m_B + outStartInd, m_B + inpStartInd, entriesToCopy * sizeof(*m_B));
					}
					else
					{
						std::memmove(m_B + outStartInd, m_B + inpStartInd, entriesToCopy * sizeof(*m_B));
					}
				};

				MemMove((size_t)GetDiv(bTo), (size_t)GetDiv(bFrom), (size_t)GetDiv(overallWidth));
				const auto overallWidthRem = GetRem(overallWidth);
				if (overallWidthRem != 0)
				{
					copy(bFrom + overallWidth - overallWidthRem, bTo + overallWidth - overallWidthRem, (uint8_t)overallWidthRem);
				}
			}
			else
			{
				const auto bToRem = GetRem(bTo);
				if (bToRem != 0)
				{
					copy(bFrom, bTo, (uint8_t)bToRem);
				}
				for (bFrom += bToRem, bTo += bToRem, overallWidth -= bToRem; overallWidth >= 64; overallWidth -= 64, bFrom += 64, bTo += 64)
				{
					copy(bFrom, bTo, 64);
				}
				if (overallWidth != 0)
				{
					copy(bFrom, bTo, (uint8_t)overallWidth);
				}
			}
		}

	protected:
		uint64_t m_NextBit = 0;
		constexpr size_t BitEntry2Uint64Entry(uint64_t bitEntry) { return (size_t)(bitEntry >> 6); }
	};
};
