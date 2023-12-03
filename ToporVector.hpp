// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include "ToporDynArray.hpp"

namespace Topor
{
	template<class T> 
	class CVector : public CDynArray<T>
	{
	public:
		using Par = CDynArray<T>;
		
		CVector(size_t initSz = 0, size_t next = 0) : CDynArray<T>(initSz), m_Next(next) {}
		CVector(size_t initSz, unsigned char initVal, size_t next) : CDynArray<T>(initSz, initVal), m_Next(next) {}
		// Copy constructor
		CVector(const CVector& v) : CDynArray<T>(v) { m_Next = v.m_Next; }
		// Assignment operator
		CVector& operator =(const CVector& v) { CDynArray<T>::operator=(v); m_Next = v.m_Next; return *this; }
		// Move constructor
		CVector(CVector&& v) : CDynArray<T>(move(v)) { m_Next = v.m_Next; v.m_Next = 0; }
		// Move assignment operator
		CVector& operator=(CVector&& v) { CDynArray<T>::operator=(move(v)); m_Next = v.m_Next; v.m_Next = 0; return *this; }
		
		inline void push_back(const T& elem)
		{
			if (unlikely(m_Next >= Par::m_Cap))
			{
				Par::reserve_atleast(m_Next + 1);
				if (Par::m_B == nullptr) return;
			}
			Par::m_B[m_Next++] = elem;
		}

		inline void emplace_back(T&& elem)
		{
			if (unlikely(m_Next >= Par::m_Cap))
			{
				Par::reserve_atleast(m_Next + 1);
				if (Par::m_B == nullptr) return;
			}
			Par::m_B[m_Next++] = move(elem);
		}

		inline void append(span<T> s)
		{
			assert(m_Next + s.size() >= m_Next);
			if (unlikely(m_Next + s.size() >= Par::m_Cap))
			{
				Par::reserve_atleast(m_Next + s.size());
				if (Par::m_B == nullptr) return;
			}
			std::memcpy((void*)Par::get_ptr(m_Next), &s[0], s.size() * sizeof(s[0]));
			m_Next += s.size();
		}

		inline T&& pop_back()
		{
			return move(Par::m_B[--m_Next]);
		}

		inline T& back()
		{
			return Par::m_B[m_Next - 1];
		}

		inline void clear()
		{
			m_Next = 0;
		}

		template <class TFuncApplyBeforeCleaning>
		inline void clear(TFuncApplyBeforeCleaning ApplyBeforeCleaning)
		{
			if (!empty())
			{
				auto bSpan = get_span();
				for_each(bSpan.begin(), bSpan.end(), [&](T elem) { ApplyBeforeCleaning(elem); });
			}			
			clear();
		}

		inline size_t size() const
		{
			return m_Next;
		}

		inline bool empty() const
		{
			return m_Next == 0;
		}
		
		void resize(size_t sz)
		{
			if (unlikely(sz > Par::cap()))
			{
				Par::reserve_atleast((size_t)sz + 1);
				if (Par::m_B == nullptr) return;
			}
			m_Next = sz;
		}

		inline auto get_span(size_t startIndIncl = 0)
		{
			assert(startIndIncl == 0 || startIndIncl < m_Next);
			return span(Par::m_B + startIndIncl, m_Next - startIndIncl);
		}

		inline auto get_span(size_t startIndIncl, size_t sz)
		{
			assert(startIndIncl + sz >= startIndIncl);
			assert(startIndIncl + sz <= m_Next);
			return span(Par::m_B + startIndIncl, sz);
		}

		inline auto get_const_span(size_t startIndIncl = 0) const
		{
			assert(startIndIncl == 0 || startIndIncl < m_Next);
			return span(Par::m_B + startIndIncl, m_Next - startIndIncl);
		}

		inline auto get_const_span(size_t startIndIncl, size_t sz) const
		{
			assert(startIndIncl + sz >= startIndIncl);
			assert(startIndIncl + sz <= m_Next);
			return span(Par::m_B + startIndIncl, sz);
		}

		inline operator span<T>()
		{
			return get_span();
		}

		template <class TFuncCond>
		inline void erase_if_may_reorder(TFuncCond EraseCond, size_t startInd = 0)
		{
			for (size_t currInd = startInd; currInd < m_Next; ++currInd)
			{
				if (EraseCond(Par::m_B[currInd]))
				{
					Par::m_B[currInd--] = Par::m_B[--m_Next];
				}
			}			
		}
	protected:
		size_t m_Next;		
	};
};
