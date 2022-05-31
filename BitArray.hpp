// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <cstdint>
#include <stdexcept>
#include <array>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <bit>

#include "TableBitInfo.hpp"

// Defining likely/unlikely for helping branch prediction. 
// Will replace by C++20's [[likely]]/[[unlikely]] attributes, once properly and consistently implemented in both GCC and VS
#ifdef _WIN32
#define likely(x)       (x)
#define unlikely(x)     (x)
#else
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#endif

using namespace std;

namespace BitArray
{
	// GetElem extracts elemWidth bits from the buffer buf, starting from bit startBitNum
	static constexpr uint64_t BAGetElem(const uint64_t* buf, uint64_t startBitNum, uint8_t elemWidth)
	{
		const array<TBitInfo, 64>& bitInfo = tableByBit[elemWidth - 1];
		const size_t wordInd = (size_t)(startBitNum >> 6);
		const size_t startBitInWord = (size_t)(startBitNum & 63);

		return ((buf[wordInd] & bitInfo[startBitInWord].w0ReadMask) >> bitInfo[startBitInWord].w0ReadShr) |
			(buf[wordInd + 1] & bitInfo[startBitInWord].w1ReadMask);
	}

	// A more efficient constexpr version of BAGetElem, which should be used when 
	// (1) startBitNum and elemWidth are known at compile time
	// (2) Both startBitNum and elemWidth fit into 64 bits
	template <uint8_t startBitNum, uint8_t elemWidth>
	static constexpr uint64_t BAGetElem(const uint64_t* buf)
	{
		static_assert(startBitNum <= 64 && elemWidth <= 64);

		constexpr auto& bitInfo = tableByBit[elemWidth - 1];
		
		const auto w0Masked = buf[0] & bitInfo[startBitNum].w0ReadMask;
		
		if constexpr (startBitNum == 0)
		{
			return w0Masked;
		}
		else 
		{
			const auto w0MaskShifted = w0Masked >> bitInfo[startBitNum].w0ReadShr;
			
			if constexpr (startBitNum + elemWidth <= 64)
			{
				return w0MaskShifted;
			}
			else
			{
				return w0MaskShifted | (buf[1] & bitInfo[startBitNum].w1ReadMask);
			}
		}

	}

	static constexpr uint64_t BAGetElemAndGoToNext(const uint64_t* buf, uint64_t& bitNum, uint8_t elemWidth)
	{
		return BAGetElem(buf, bitNum+=elemWidth, elemWidth);
	}

	// Sets elemWidth bits in the buffer buf, starting from bit bitNum to elem
	static constexpr void BASetElem(uint64_t* buf, uint64_t bitNum, uint8_t elemWidth, uint64_t elem)
	{
		const array<TBitInfo, 64>& bitInfo = tableByBit[elemWidth - 1];
		const size_t wordInd = (size_t)(bitNum >> 6);
		const size_t startBitInWord = (size_t)(bitNum & 63);

		buf[wordInd] |= (elem & bitInfo[startBitInWord].W0WriteMask()) << (uint64_t)bitInfo[startBitInWord].W0WriteShl();
		buf[wordInd + 1] |= elem & bitInfo[startBitInWord].W1WriteMask();
	}

	// A more efficient constexpr version of BASetElem, which should be used when 
	// (1) startBitNum and elemWidth are known at compile time
	// (2) Both startBitNum and elemWidth fit into 64 bits
	template <uint8_t bitNum, uint8_t elemWidth>
	static constexpr void BASetElem(uint64_t* buf, uint64_t elem)
	{
		static_assert(bitNum <= 64 && elemWidth <= 64);

		constexpr auto& bitInfo = tableByBit[elemWidth - 1];
		
		const auto elemW0Masked = elem & bitInfo[bitNum].W0WriteMask();
		
		if constexpr (bitNum == 0)
		{
			buf[0] |= elemW0Masked;
		}
		else
		{
			buf[0] |= elemW0Masked << bitInfo[bitNum].W0WriteShl();
			if constexpr (bitNum + elemWidth > 64)
			{
				buf[1] |= elem & bitInfo[bitNum].W1WriteMask();
			}
		}
	}

	// SetElemAndGoToNext sets elemWidth bits in the buffer buf, starting from bit bitNum to elem and advances bitNum by elemWidth
	static constexpr void BASetElemAndGoToNext(uint64_t* buf, uint64_t& bitNum, uint8_t elemWidth, uint64_t elem)
	{
		BASetElem(buf, bitNum, elemWidth, elem);
		bitNum += elemWidth;
	}

	// A more efficient constexpr version of BAIncrAndGet, which should be used when 
	// (1) startBitNum and elemWidth are known at compile time
	// (2) Both startBitNum and elemWidth fit into 64 bits
	static constexpr uint64_t BAIncrAndGet(uint64_t* buf, uint64_t startBitNum, uint8_t elemWidth, uint64_t incrVal = 1)
	{
		uint64_t currElem = BAGetElem(buf, startBitNum, elemWidth);
		currElem += incrVal;
		assert(currElem != 0);
		BASetElem(buf, startBitNum, elemWidth, currElem);
		return currElem;
	}

	template <uint8_t startBitNum, uint8_t elemWidth>
	static constexpr uint64_t BAIncrAndGet(uint64_t* buf, uint64_t incrVal = 1)
	{
		uint64_t currElem = BAGetElem<startBitNum, elemWidth>(buf);
		currElem += incrVal;
		assert(currElem != 0);
		BASetElem<startBitNum, elemWidth>(buf, currElem);
		return currElem;
	}

	// The bit-array can work with:
	// (1) dynamic element size, if ElemWidthConstExpr = 0
	// (2) compile-time element size ElemWidthConstExpr
	// The compile-time size makes the array implementation for element sizes which are powers of 2 (1,2,4,8,16,32,64) more efficient
	// The dynamic-time size allows one to change the size dynamically with the function ChangeSize
	template <uint8_t ElemWidthConstExpr = 0>
	class CBitArray
	{
	public:
		CBitArray() : m_ElemWidth(ElemWidthConstExpr), m_ElemsNum(0), m_Buf(nullptr), m_BufAllocated(0)
		{
			static_assert(ElemWidthConstExpr <= 64);					
		}

		~CBitArray() 
		{ 
			free(m_Buf); 
		}
		
		inline void Push(uint64_t elemVal)
		{
			const uint8_t tableEntryInd = TableEntryInd(m_ElemsNum);
			const size_t wordInd = WordInd(m_ElemsNum);

			ReallocIfReq(wordInd, false);

			SetElemInternal(elemVal, wordInd, tableEntryInd);

			++m_ElemsNum;
		}

		inline void ForceElemSetNonExistingTo0(uint64_t elemNum, uint64_t elemVal)
		{
			const uint8_t tableEntryInd = TableEntryInd(elemNum);
			const size_t wordInd = WordInd(elemNum);

			if (elemNum >= m_ElemsNum)
			{
				m_ElemsNum = elemNum + 1;
				const size_t wordInd = WordInd(elemNum);
				ReallocIfReq(wordInd, true);
			}

			SetElemInternal(elemVal, wordInd, tableEntryInd);
		}

		inline void Set(uint64_t elemNum, uint64_t elemVal)
		{
			const uint8_t tableEntryInd = TableEntryInd(elemNum);
			const size_t wordInd = WordInd(elemNum);

			SetElemInternal(elemVal, wordInd, tableEntryInd);
		}

		inline void SetAllElemsTo0() { memset(m_Buf, 0, m_BufAllocated * sizeof(*m_Buf)); }

		inline uint64_t ElemsNum() const { return m_ElemsNum; }

		inline uint8_t ElemWidth() const 
		{ 
			if constexpr (ElemWidthConstExpr == 0)
			{
				return m_ElemWidth;
			}
			else
			{
				return ElemWidthConstExpr;
			}			
		}

		inline uint64_t Get(uint64_t elemNum) const
		{
			const uint8_t tableEntryInd = TableEntryInd(elemNum);
			const size_t wordInd = WordInd(elemNum);

			if constexpr (ElemWidthConstExpr == 64)
			{
				return m_Buf[wordInd];
			}
			else if constexpr (has_single_bit(ElemWidthConstExpr))
			{
				return ((m_Buf[wordInd] & tableByEntry[ElemWidthConstExpr - 1].second[tableEntryInd].w0ReadMask) >> tableByEntry[ElemWidthConstExpr - 1].second[tableEntryInd].w0ReadShr);
			}
			else
			{
				return ((m_Buf[wordInd] & tableByEntry[ElemWidth() - 1].second[tableEntryInd].w0ReadMask) >> tableByEntry[ElemWidth() - 1].second[tableEntryInd].w0ReadShr) |
					(m_Buf[wordInd + 1] & tableByEntry[ElemWidth() - 1].second[tableEntryInd].w1ReadMask);
			}			
		}

		void ChangeElemWidth(uint8_t newWidth)
		{
			static_assert(ElemWidthConstExpr == 0);
			if (unlikely(newWidth > 64 || newWidth == 0))
			{
				throw logic_error("ChangeElemWidth failed, since the new width == 0 or > 64");
			}
			
			m_ElemWidth = newWidth;
			
			if (m_BufAllocated == 0)
			{
				return;
			}
		}
	protected:
		uint8_t m_ElemWidth;
		uint64_t m_ElemsNum;
		uint64_t* m_Buf;
		size_t m_BufAllocated;				
		
		inline uint64_t BitNum(uint64_t elemNum) const 
		{ 
			if constexpr (has_single_bit(ElemWidthConstExpr))
			{
				return elemNum << countr_zero(ElemWidthConstExpr);
			}
			else
			{
				return elemNum * ElemWidth();
			}
		}
		
		inline size_t WordInd(uint64_t elemNum) const 
		{ 
			if constexpr (has_single_bit(ElemWidthConstExpr))
			{
				constexpr auto shrVal = 6 - countr_zero(ElemWidthConstExpr);
				return (size_t)elemNum >> shrVal;
			}
			else
			{
				return (size_t)BitNum(elemNum) >> 6;
			}
		}
		
		inline uint8_t TableEntryInd(uint64_t elemNum) const
		{
			return (uint8_t)(elemNum & (uint64_t)tableByEntry[ElemWidth() - 1].first);			
		}
		
		inline void SetElemInternal(uint64_t elem, const size_t wordInd, const uint8_t tableEntryInd)
		{
			if constexpr (ElemWidthConstExpr == 64)
			{
				m_Buf[wordInd] = elem;
			}
			else
			{
				m_Buf[wordInd] |= (elem & tableByEntry[ElemWidth() - 1].second[tableEntryInd].W0WriteMask()) << tableByEntry[ElemWidth() - 1].second[tableEntryInd].W0WriteShl();
			}

			if constexpr (!has_single_bit(ElemWidthConstExpr))
			{
				m_Buf[wordInd + 1] |= elem & tableByEntry[ElemWidth() - 1].second[tableEntryInd].W1WriteMask();
			}
		}
		inline void ReallocIfReq(size_t wordInd, bool isTight)
		{
			if (wordInd + 1 >= m_BufAllocated)
			{
				const auto saveAllocated = m_BufAllocated;
				// The +2 part is because we may need an extra-word after wordInd, if the element to insert extends to another word
				m_BufAllocated = (isTight ? wordInd : ((13 * wordInd) >> 3)) + 2;

				if (unlikely(m_Buf == nullptr))
				{
					m_Buf = (decltype(m_Buf))calloc(m_BufAllocated, sizeof(*m_Buf));
					if (unlikely(m_Buf == nullptr))
					{
						throw bad_alloc();
					}
				}
				else
				{
					m_Buf = (decltype(m_Buf))realloc(m_Buf, (m_BufAllocated * sizeof(*m_Buf)));
					if (unlikely(m_Buf == nullptr))
					{
						throw bad_alloc();
					}
										
					memset(m_Buf + saveAllocated, 0, (m_BufAllocated - saveAllocated) * sizeof(*m_Buf));										
				}
			}
		}
	};

	template <uint8_t ElemWidthConstExpr>
	class CBitArrayAligned
	{
	public:
		CBitArrayAligned() 
		{
			static_assert(ElemWidthConstExpr > 0 && ElemWidthConstExpr <= 64);
		}

		inline void Push(uint64_t elemVal)
		{
			m_Buf.emplace_back((TAlignedUInt)elemVal);
		}

		inline void ForceElemSetNonExistingTo0(uint64_t elemNum, uint64_t elemVal)
		{
			if (elemNum >= m_Buf.size())
			{
				//const auto oldSize = m_Buf.size();
				//m_Buf.resize((size_t)elemNum + 1);
				//memset(&m_Buf[oldSize], 0, (m_Buf.size() - oldSize) * sizeof(m_Buf[0]));
				m_Buf.resize((size_t)elemNum + 1, 0);
			}

			Set(elemNum, elemVal);			
		}

		inline void SetAllElemsTo0()
		{
			memset(&m_Buf[0], 0, m_Buf.size() * sizeof(m_Buf[0]));
		}

		inline uint64_t ElemsNum() const { return m_Buf.size(); }

		constexpr uint8_t ElemWidth() { return ElemWidthConstExpr; }

		inline void Set(uint64_t elemNum, uint64_t elemVal)
		{
			m_Buf[(size_t)elemNum] = (TAlignedUInt)elemVal;
		}

		inline uint64_t Get(uint64_t elemNum) const
		{
			return (uint64_t)m_Buf[(size_t)elemNum];
		}
	protected:
		using TAlignedUInt = conditional_t<ElemWidthConstExpr==1, bool, conditional_t<ElemWidthConstExpr <= 8, uint8_t, conditional_t<ElemWidthConstExpr<=16, uint16_t, conditional_t<ElemWidthConstExpr<=32, uint32_t, uint64_t>>>>;
		vector<TAlignedUInt> m_Buf;
	};
};

