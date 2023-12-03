// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <span>
#include <cassert>
#include <algorithm>
#include <limits>
#include <cstring>

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

namespace Topor
{
	template<class T> class CDynArray
	{
	public:
		CDynArray(size_t initCap = 0) : m_Cap(initCap), m_B(initCap > MaxCapacity || initCap == 0 ? nullptr : (T*)malloc(initCap* TSize)) {}
		CDynArray(size_t initCap, unsigned char initVal) : m_Cap(initCap), m_B(InitAlloc(initCap, initVal)) {}
		// Copy constructor
		CDynArray(const CDynArray& da) : CDynArray(da.m_Cap)
		{ 
			if (unlikely(uninitialized_or_erroneous()))
			{
				m_Cap = 0;
			}
			else
			{
				std::memcpy(m_B, da.m_B, da.m_Cap * sizeof(*m_B));
			}
		}
		// Assignment operator
		CDynArray& operator =(const CDynArray& da) 
		{ 
			reserve_exactly(da.m_Cap);
			if (unlikely(uninitialized_or_erroneous()))
			{
				m_Cap = 0;				
			}
			else
			{
				std::memcpy(m_B, da.m_B, da.m_Cap * sizeof(*m_B));
				m_Cap = da.m_Cap;
				m_Multiplier = da.m_Multiplier;
			}
			return *this;
		}
		// Move constructor
		CDynArray(CDynArray&& da) { *this = move(da); }
		// Move assignment operator
		CDynArray& operator=(CDynArray&& da) 
		{ 
			free(m_B);
			m_B = da.m_B;
			m_Cap = da.m_Cap;
			m_Multiplier = da.m_Multiplier;
			da.m_B = nullptr;
			da.m_Cap = 0;
			da.m_Multiplier = 0;
			return *this;
		}
		
		~CDynArray() { free(m_B); }

#ifdef _WIN32
		void reserve_exactly(size_t newCap)
#else
		// Preventing inlining is required to work around an apparent GCC bug, manifesting itself in the following error:
		// error: argument 2 range [9223372036854775808, 18446744073709551608] exceeds maximum object size 9223372036854775807 [-Werror=alloc-size-larger-than=] auto tmp = (T*)realloc(m_B, s);
		void __attribute__((noinline)) reserve_exactly(size_t newCap)
#endif
		{
			if (newCap > MaxCapacity || newCap == 0)
			{
				ClearB();
			}
			else
			{
				auto tmp = (T*)realloc(m_B, (m_Cap = newCap) * TSize);
				if (unlikely(tmp == nullptr))
				{
					ClearB();
				}
				else
				{
					m_B = tmp;
				}
			}			
		}

		void reserve_exactly(size_t newCap, unsigned char initVal)
		{
			if (newCap > MaxCapacity || newCap == 0)
			{
				ClearB();
			} else if (m_B == nullptr)
			{
				assert(m_Cap == 0);
				m_B = InitAlloc(newCap, initVal);	
				m_Cap = newCap;		
			}
			else
			{
				auto tmp = (T*)realloc((void*)m_B, newCap * TSize);
				if (unlikely(tmp == nullptr))
				{
					ClearB();
				}
				else
				{
					m_B = tmp;					
					if (newCap > m_Cap)
					{
						std::memset((void*)(m_B + m_Cap), initVal, (newCap - m_Cap) * TSize);
					}
					m_Cap = newCap;
				}
			}			
		}

		void reserve_atleast(size_t newCap)
		{			
			reserve_exactly(GetNewCap(newCap));
		}

		void reserve_atleast_with_max(size_t newCap, size_t maxCap)
		{
			const auto tentativeNewCap = GetNewCap(newCap);
			reserve_exactly(tentativeNewCap > maxCap ? maxCap : tentativeNewCap);
		}

		void reserve_atleast_with_max(size_t newCap, size_t maxCap, unsigned char initVal)
		{
			const auto tentativeNewCap = GetNewCap(newCap);
			reserve_exactly(tentativeNewCap > maxCap ? maxCap : tentativeNewCap, initVal);
		}

		void reserve_atleast(size_t newCap, unsigned char initVal)
		{
			reserve_exactly(GetNewCap(newCap), initVal);
		}

		void reserve_beyond_if_requried(size_t indToInclude, bool isResizeAtLeast)
		{
			if (indToInclude >= cap())
			{
				if (isResizeAtLeast)
				{
					reserve_atleast(indToInclude + 1);
				}
				else
				{
					reserve_exactly(indToInclude + 1);
				}
			}
		}

		void memset(unsigned char newVal, size_t startIndIncl = 0)
		{
			memset(newVal, startIndIncl, m_Cap);
		}

		void memset(unsigned char newVal, size_t startIndIncl, size_t endIndExcl)
		{
			assert(startIndIncl < endIndExcl && endIndExcl <= cap());
			std::memset((void*)(m_B + startIndIncl), newVal, (endIndExcl - startIndIncl) * TSize);			
		}

		// Non-overlapping memcpy
		void memcpy(size_t outStartInd, size_t inpStartInd, size_t entriesToCopy)
		{
			assert(inpStartInd < m_Cap && outStartInd < m_Cap);
			assert(inpStartInd + entriesToCopy > inpStartInd && inpStartInd + entriesToCopy <= m_Cap);
			assert(outStartInd + entriesToCopy > outStartInd && outStartInd + entriesToCopy <= m_Cap);
			// No overlap
			assert(outStartInd >= inpStartInd + entriesToCopy || inpStartInd >= outStartInd + entriesToCopy);
			std::memcpy(m_B + outStartInd, m_B + inpStartInd, entriesToCopy * sizeof(*m_B));
		}

		// Potentially-overlapping memmove
		void memmove(size_t outStartInd, size_t inpStartInd, size_t entriesToCopy)
		{
			if (outStartInd >= inpStartInd + entriesToCopy || inpStartInd >= outStartInd + entriesToCopy)
			{
				memcpy(outStartInd, inpStartInd, entriesToCopy);
			}
			else
			{
				assert(inpStartInd < m_Cap && outStartInd < m_Cap);
				assert(inpStartInd + entriesToCopy > inpStartInd && inpStartInd + entriesToCopy <= m_Cap);
				assert(outStartInd + entriesToCopy > outStartInd && outStartInd + entriesToCopy <= m_Cap);
				std::memmove(m_B + outStartInd, m_B + inpStartInd, entriesToCopy * sizeof(*m_B));
			}
		}

		inline auto get_span_cap(size_t startIndIncl = 0)
		{
			assert(startIndIncl < m_Cap);
			return span(m_B + startIndIncl, m_Cap - startIndIncl);
		}

		inline auto get_span_cap(size_t startIndIncl, size_t sz)
		{
			assert(startIndIncl + sz >= startIndIncl);
			assert(startIndIncl + sz <= m_Cap);
			return span(m_B + startIndIncl, sz);
		}

		inline auto get_const_span_cap(size_t startIndIncl = 0) const
		{
			assert(startIndIncl < m_Cap);
			return span(m_B + startIndIncl, m_Cap - startIndIncl);
		}

		inline auto get_const_span_cap(size_t startIndIncl, size_t sz) const
		{
			assert(startIndIncl + sz >= startIndIncl);
			assert(startIndIncl + sz <= m_Cap);
			return span(m_B + startIndIncl, sz);
		}

		inline T* get_ptr(size_t i = 0) 
		{ 
			assert(i < m_Cap);
			return m_B + i;
		}

		inline T* get_ptr_no_assert(size_t i = 0)
		{
			return m_B + i;
		}

		inline T* get_const_ptr(size_t i = 0) const
		{
			assert(i < m_Cap);
			return m_B + i;
		}

		inline T& operator [](size_t i) const
		{ 
			assert(i < cap());
			return m_B[i]; 
		}

		inline const size_t& cap() const
		{
			return m_Cap;
		}

		inline size_t memMb() const
		{
			return (m_Cap * TSize) / 1000;
		}

		inline const bool empty() const 
		{
			return cap() == 0;
		}

		inline bool uninitialized_or_erroneous() const { return m_B == nullptr; }		

		inline void remove_if_equal_and_cut_capacity(T eqVal)
		{
			size_t currIndWrite(0);

			for (size_t currIndRead = 0; currIndRead != m_Cap; ++currIndRead)
			{
				if (m_B[currIndRead] != eqVal)
				{
					m_B[currIndWrite++] = m_B[currIndRead];
				}
			}
			reserve_exactly(currIndWrite);
		}

		template <typename TUInd>
		void RemoveGarbage(TUInd startInd, TUInd& endInd, function<bool(TUInd clsInd)> IsChunkDeleted, function<TUInd(TUInd clsInd)> ChunkEnd,
			function<void(TUInd oldWlInd, TUInd newWlInd)> NotifyAboutRemainingChunkMove = nullptr)
		{
			auto FindNextDeleted = [&](TUInd firstInd, TUInd lastInd, function<bool(TUInd clsInd)> IsChunkDeleted, function<TUInd(TUInd clsInd)> ChunkEnd, TUInd toInd = numeric_limits<T>::max(), function<void(TUInd oldWlInd, TUInd newWlInd)> NotifyAboutRemainingChunkMove = nullptr)
			{
				const auto initFirstInd = firstInd;
				while (firstInd < lastInd && !IsChunkDeleted(firstInd))
				{
					const auto nextFirstInd = ChunkEnd(firstInd);
					if (NotifyAboutRemainingChunkMove != nullptr)
					{
						NotifyAboutRemainingChunkMove(firstInd, toInd + firstInd - initFirstInd);						
					}
					firstInd = nextFirstInd;
				}
				return firstInd;
			};

			auto FindNextNotDeleted = [&](TUInd firstInd, TUInd lastInd, function<bool(TUInd clsInd)> IsChunkDeleted, function<TUInd(TUInd clsInd)> ChunkEnd)
			{
				while (firstInd < lastInd && IsChunkDeleted(firstInd))
				{
					firstInd = ChunkEnd(firstInd);					
				}
				return firstInd;
			};

			// Find the first deleted chunk and put its index in toInd
			TUInd toInd(FindNextDeleted(startInd, endInd, IsChunkDeleted, ChunkEnd, startInd, NotifyAboutRemainingChunkMove));
			// Find the first clause after the first deleted chunk and put its index in fromInd
			TUInd fromInd(FindNextNotDeleted(toInd, endInd, IsChunkDeleted, ChunkEnd));

			// Copy chunks from fromInd to toInd
			while (fromInd < endInd)
			{
				TUInd fromIndEnd = FindNextDeleted(fromInd, endInd, IsChunkDeleted, ChunkEnd, toInd, NotifyAboutRemainingChunkMove);
				const auto copiedInds = fromIndEnd - fromInd;
				// memmove will still use memcpy, if there is no overlap
				memmove(toInd, fromInd, copiedInds);
				toInd += copiedInds;
				fromInd = FindNextNotDeleted(fromIndEnd, endInd, IsChunkDeleted, ChunkEnd);
			}
			endInd = toInd;
		}

		static constexpr double MultiplierDef = 1.625;
		inline void SetMultiplier(double multiplier = MultiplierDef) { assert(multiplier >= 1.);  m_Multiplier = multiplier; }
		inline double GetMultiplier() const { return m_Multiplier; }
	protected:
		static constexpr size_t TSize = sizeof(T);
		static constexpr size_t MaxCapacity = (std::numeric_limits<size_t>::max)() / TSize;		
		T* InitAlloc(size_t initCap, unsigned char initVal) 
		{
			// #topor: are there more page-fault-friendly ways to allocate: (1) 0-initialized memory; (2) non-initialized memory?
			// For non-initialized memory, see: https://stackoverflow.com/questions/56411164/can-i-ask-the-kernel-to-populate-fault-in-a-range-of-anonymous-pages
			// For 0-initialized initialized memory, first note that gcc now rewrites malloc & memset(0) into calloc. 
			// However, once this change in gcc has been made, there was a substantial deterioration in the performance of a random-access hash-table in FB (see https://github.com/facebook/folly/issues/1508)
			// One way to block this optimization in gcc is as follows:
			// auto p = malloc(n);
			// asm volatile ("":"+r"(p));
			// // another way to block GCC's optimization is: asm volatile ("":::"memory"); 
			// memset(p, '\0', n);
			// We're using calloc directly for now; may want to explore other options later

			if (initCap == 0 || initCap > MaxCapacity)
			{
				return nullptr;
			}

			if (initVal == 0)
			{
				return (T*)calloc(initCap, TSize);
			}

			auto mallocRes = (T*)malloc(initCap * TSize);
			if (unlikely(mallocRes == nullptr))
			{
				return nullptr;
			}
			
			std::memset((void*)mallocRes, initVal, initCap * TSize);

			return mallocRes;
		}
		
		inline size_t GetNewCap(size_t cap) const
		{
			const double newCapDouble = cap * m_Multiplier + 2;
			return newCapDouble > (double)(std::numeric_limits<size_t>::max)() ? (std::numeric_limits<size_t>::max)() : (size_t)newCapDouble;
		}
		
		inline void ClearB()
		{
			free(m_B);
			m_B = nullptr;
			m_Cap = 0;
		}			
		
		size_t m_Cap;
		double m_Multiplier = MultiplierDef;
		T* m_B = nullptr;
	};
};
