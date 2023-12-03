// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include "ToporVector.hpp"

// Variable scores handler, including a heap implementation

namespace Topor
{
	template <typename TUVar, typename TUV>
	class CVarScores 
	{	
	public:
		// The heap's 0 index must always be occupied, so that position 0 would mean not-in-the-heap
		CVarScores(double& varActivityInc) : m_VarActivityInc(varActivityInc), m_Heap(1, 0, 1) 
		{
			static_assert(std::is_same<TUVar, TUV>::value);
		}

		void SetInitOrder(bool initOrder)
		{
			m_InitOrder = initOrder;
		}

		void reserve_exactly(size_t beyondMaxVar)
		{
			m_PosScore.reserve_exactly(beyondMaxVar, 0);
			m_Heap.reserve_exactly(beyondMaxVar);			
		}

		inline bool uninitialized_or_erroneous() const
		{
			return m_PosScore.uninitialized_or_erroneous() || m_Heap.uninitialized_or_erroneous();
		}

		inline size_t size() const { return m_Heap.size() - 1; }
		
		inline bool empty() const { return size() == 0; }
		
		inline bool in_heap(TUVar v) const { assert(v < m_PosScore.cap());  return m_PosScore[v].m_Pos > 0; }
		
		bool increase_score(TUVar v, double mult = 1.0) 
		{ 
			bool isRescaled = false;
			if (unlikely((m_PosScore[v].m_Score += m_VarActivityInc * mult) > 1e100))
			{
				span<TPosScore> posScoreSpan = m_PosScore.get_span_cap();
				// Rescale
				for (auto& currPosScore : posScoreSpan)
				{
					currPosScore.m_Score *= 1e-100;
				}				

				m_VarActivityInc *= 1e-100;
				isRescaled = true;
			}

			if (in_heap(v))
			{
				percolate_up(m_PosScore[v].m_Pos);
			}

			return isRescaled;
		}

		void reinsert_if_not_in_heap(TUVar v)
		{			
			assert(!uninitialized_or_erroneous());
			assert(v < m_PosScore.cap());
			if (!in_heap(v))
			{
				m_PosScore[v].m_Pos = (TUV)m_Heap.size();
				m_Heap.push_back(v);				
				percolate_up(m_PosScore[v].m_Pos);
			}
		}

		void rebuild()
		{
			m_Heap.reserve_exactly(m_PosScore.cap());
			m_Heap.clear();
			m_Heap.emplace_back(0);
			for (TUVar v = 1; v < m_PosScore.cap(); ++v)
			{
				if (in_heap(v))
				{
					m_PosScore[v].m_Pos = 0;
					assert(!in_heap(v));
					insert(v, m_PosScore[v].m_Score);
					assert(in_heap(v));
				}
			}
		}

		void insert(TUVar v, double score)
		{
			if (v >= m_PosScore.cap())
			{
				m_PosScore.reserve_atleast(v + 1, (size_t)0);
				m_Heap.reserve_atleast(v + 1);
				if (uninitialized_or_erroneous())
				{
					return;
				}
			}			
			assert(!in_heap(v));

			m_PosScore[v] = TPosScore((TUV)m_Heap.size(), score);
			m_Heap.push_back(v);
			percolate_up(m_PosScore[v].m_Pos);
		}

		TUVar remove_min()
		{
			auto v = m_Heap[1];
			swap(m_Heap[1], m_Heap.back());
			m_PosScore[m_Heap[1]].m_Pos = 1;
			m_PosScore[v].m_Pos = 0;
			
			m_Heap.pop_back();
			
			if (m_Heap.size() > 2)
			{
				percolate_down(1);
			}
			
			return v;
		}

		inline TUVar get_min() const
		{
			return m_Heap[1];
		}

		inline bool var_score_exists(TUVar v) const { return v < m_PosScore.cap(); }

		inline double get_var_score(TUVar v) const { return m_PosScore[v].m_Score; }
		
		// Use set_var_score only if rebuild is surely scheduled soon, otherwise it will botch the data structure!
		inline void set_var_score(TUVar v, double newScore) { m_PosScore[v].m_Score = newScore; }

		inline void var_inc_update(double varDecay) { m_VarActivityInc *= (1. / varDecay); }

		inline void set_multiplier(double multiplier) { m_PosScore.SetMultiplier(multiplier); m_Heap.SetMultiplier(multiplier); }

		inline void replace_pos_score_vars(TUVar vFrom, TUVar vTo) { m_PosScore[vTo] = move(m_PosScore[vFrom]); }

		inline size_t memMb() const { return m_Activity.memMb() + m_Heap.memMb() + m_PosScore.memMb(); }
	protected:
		// The activity
		CVector<double> m_Activity;     
		double& m_VarActivityInc;
		// This variable allows one to simulate the two different insertion orders:
		// false: bigger variable indices first; true: smaller variable indices first (default)
		// We do the simulation by providing two implementations of the "better" predicate (see below)
		// See the parameter m_ParamVsidsInitOrder for more details
		bool m_InitOrder = false;
		// Heap of variables
		CVector<TUVar> m_Heap; 

		// TPosScore: 1) position in the heap (0 means not in the heap); 2) score
		struct TPosScore
		{
			TPosScore(TUV pos, double score) : m_Pos(pos), m_Score(score) {}
			TUV m_Pos;
			double m_Score;
		};
		// Variable-indexed array: position & score per variable
		CDynArray<TPosScore> m_PosScore;

		// Index "traversal" functions
		static constexpr TUV left(TUV i) { return i << 1; };
		static constexpr TUV right(TUV i) { return (i << 1) + 1; };
		static constexpr TUV parent(TUV i) { return i >> 1; };

		// Cannot put the static asserts in the class itself, since it's forbidden by C++ standard 
		// as the functions are undefined inside the class
		void static_assert_traversal()
		{
			static_assert(parent(2) == 1);
			static_assert(parent(3) == 1);
			static_assert(left(1) == 2);
			static_assert(right(1) == 3);

			static_assert(parent(4) == 2);
			static_assert(parent(5) == 2);
			static_assert(left(2) == 4);
			static_assert(right(2) == 5);

			static_assert(parent(6) == 3);
			static_assert(parent(7) == 3);
			static_assert(left(3) == 6);
			static_assert(right(3) == 7);
		}

		void percolate_up(TUV i)
		{
			const TUVar v = m_Heap[i];
			TUV p = parent(i);

			while (i != 1 && better(v, m_Heap[p])) 
			{
				m_Heap[i] = m_Heap[p];
				m_PosScore[m_Heap[p]].m_Pos = i;
				i = p;
				p = parent(p);
			}
			m_Heap[i] = v;
			m_PosScore[v].m_Pos = i;
		}

		void percolate_down(TUV i)
		{
			TUVar v = m_Heap[i];
			while (left(i) < m_Heap.size()) 
			{
				auto child = right(i) < m_Heap.size() && 
					better(m_Heap[right(i)], m_Heap[left(i)]) ? right(i) : left(i);
				if (!better(m_Heap[child], v)) break;
				m_Heap[i] = m_Heap[child];
				m_PosScore[m_Heap[i]].m_Pos = i;
				i = child;
			}
			m_Heap[i] = v;
			m_PosScore[v].m_Pos = i;
		}

		inline bool better(TUVar v1, TUVar v2) const { return m_InitOrder ? m_PosScore[v1].m_Score > m_PosScore[v2].m_Score : m_PosScore[v1].m_Score >= m_PosScore[v2].m_Score; }
	};
}
