// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include "ToporDynArray.hpp"

namespace Topor
{

	// Stores a window of the last N values, including a possibility to get an average
	// Used for Glucose-inspired restart strategies
	template <typename TUV>
	class TWinAverage
	{
	public:
		TWinAverage() {}
		inline void Init(uint16_t maxCap)
		{
			m_DynArray.reserve_exactly(maxCap, 0);
		}
		void Enqueue(TUV newVal)
		{
			assert(newVal != 0);

			if (unlikely(IsError())) return;

			m_Sum -= m_DynArray[m_NextInd];
			m_DynArray[m_NextInd] = newVal;
			m_NextInd = (m_NextInd + 1) % MaxCap();

			m_Sum += (double)newVal;
		}

		inline bool IsFullWindow() const { return m_DynArray[m_NextInd] != 0; }
		inline double GetAverage() const { return m_Sum / (double)(IsFullWindow() ? MaxCap() : m_NextInd); }

		inline void Clear(bool invalidateForever = false)
		{ 
			if (unlikely(invalidateForever))
			{
				m_DynArray.reserve_exactly(0);
			}
			else
			{
				m_DynArray.memset(0);
			}
			m_Sum = 0.;
			m_NextInd = 0;
		}

		inline bool IsError() const { return m_DynArray.uninitialized_or_erroneous(); }
		inline uint16_t MaxCap() const { return (uint16_t)m_DynArray.cap(); }
	protected:
		CDynArray<TUV> m_DynArray;
		double m_Sum = 0.;
		uint16_t m_NextInd = 0;
	};
}
