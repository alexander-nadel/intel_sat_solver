// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <utility>

#include "ToporExternalTypes.hpp"
#include "ToporDynArray.hpp"
#include "ToporVector.hpp"
#include "TopiGlobal.hpp"

using namespace std;

namespace Topor
{	
	// Removing duplicates, discovering tautologies and storing incoming clauses
	class CHandleNewCls
	{
	public:		
		CHandleNewCls(size_t initVarNum) : m_LastAppearenceCounter(initVarNum, (size_t)0), m_Counter(0) {}

		inline void NewClause()
		{			
			++m_Counter;
			if (unlikely(m_Counter < 0))
			{
				m_LastAppearenceCounter.memset(0);
				m_Counter = 1;
			}
			m_Cls.clear();
		}

		tuple<bool, bool, bool> NewLitIsTauIsDuplicate(TULit newLit)
		{
			bool isTau(false), isDuplicate(false);

			const bool isNegative = IsNeg(newLit);
			const TUVar newVar = GetVar(newLit);
			const bool varExists = newVar < (TUVar)m_LastAppearenceCounter.cap();

			if (!varExists)
			{
				m_LastAppearenceCounter.reserve_atleast((size_t)newVar + 1, (size_t)0);
				if (m_LastAppearenceCounter.uninitialized_or_erroneous())
				{
					return make_tuple(isTau, isDuplicate, true);
				}
				m_LastAppearenceCounter[newVar] = isNegative ? -m_Counter : m_Counter;
			}
			else
			{
				const auto elemVal = m_LastAppearenceCounter[newVar];
				if (unlikely((llabs(elemVal) == llabs(m_Counter))))
				{
					isDuplicate = (elemVal < 0) == isNegative;
					isTau = !isDuplicate;
				}
				else
				{
					m_LastAppearenceCounter[newVar] = isNegative ? -m_Counter : m_Counter;
				}
			}

			if (likely(!isTau && !isDuplicate))
			{
				m_Cls.push_back(newLit);
				if (m_LastAppearenceCounter.uninitialized_or_erroneous())
				{
					return make_tuple(isTau, isDuplicate, true);
				}
			}

			return make_tuple(isTau, isDuplicate, false);
		}

		inline span<TULit> GetCurrCls() { return m_Cls.get_span(); }
		inline void SetMultiplier(double multiplier) { m_LastAppearenceCounter.SetMultiplier(multiplier); }
	protected:
		CVector<TULit> m_Cls;
		CDynArray<TCounterType> m_LastAppearenceCounter;
		TCounterType m_Counter;	
	};
};

