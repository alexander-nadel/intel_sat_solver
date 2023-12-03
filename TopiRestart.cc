// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"
#include <climits>

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit,TUInd,Compress>::Restart()
{
	bool restartNow = false;	

	switch (m_CurrRestartStrat)
	{
	case RESTART_STRAT_NUMERIC:
		restartNow = m_DecLevel > 0 && m_ParamRestartNumericLocal ?
			m_ConfsSinceRestart - m_RstNumericLocalConfsSinceRestartAtDecLevelCreation[m_DecLevel] >= m_RstNumericCurrConfThr :
			m_ConfsSinceRestart >= m_RstNumericCurrConfThr;
		if (restartNow)
		{			
			if (m_ParamRestartNumericSubStrat == 1)
			{
				// Luby
				m_RstNumericCurrConfThr = (uint64_t)(RestartLubySequence(m_ParamRestartLubyConfIncr, m_Stat.m_Restarts) * (double)m_ParamRestartNumericInitConfThr);
			}			
			else
			{
				// Arithmetic
				m_RstNumericCurrConfThr += m_ParamRestartArithmeticConfIncr;
			}			
		}
		break;
	case RESTART_STRAT_LBD:
		restartNow = m_RstGlueLbdWin.IsFullWindow() && m_RstGlueLbdWin.GetAverage() * m_ParamRestartLbdAvrgMult > m_RstGlueGlobalLbdSum / (double)m_RstGlueAssertingGluedClss;
		if (restartNow)
		{
			m_RstGlueLbdWin.Clear();
		}
		break;
	default:
		break;
	}

	if (unlikely(restartNow))
	{
		m_ConfsSinceRestart = 0;
		++m_Stat.m_Restarts;
		++m_RestartsSinceInvStart;
		UpdateAllUipInfoAfterRestart();
		assert(m_QueryCurr != TQueryType::QUERY_NONE);
		const auto currUnforceRestartsFraction = GetCurrUnforceRestartsFraction();
		if (currUnforceRestartsFraction != 0. && currUnforceRestartsFraction != 1.)
		{
			const uint64_t restartInBlock = m_RestartsSinceInvStart % (uint64_t)m_ParamPhaseMngRestartsBlockSize;
			const double currRestartFractionCompleted = (double)restartInBlock / (double)m_ParamPhaseMngRestartsBlockSize;
			assert(currRestartFractionCompleted >= 0.);
			const double currRestartFractionLeft = 1. - currRestartFractionCompleted;			
			assert(currRestartFractionLeft >= 0.);
			if (m_PhaseStage == m_PhaseInitStage)
			{
				if (m_PhaseStage == TPhaseStage::PHASE_STAGE_STANDARD && currRestartFractionLeft <= currUnforceRestartsFraction)
				{
					m_PhaseStage = TPhaseStage::PHASE_STAGE_DONT_FORCE;
					assert(m_PhaseInitStage != m_PhaseStage);
					assert(NV(1) || P("Phase stage changed: " + GetPhaseStageStr() + "\n"));
				}
				else if (m_PhaseStage == TPhaseStage::PHASE_STAGE_DONT_FORCE && currRestartFractionCompleted >= currUnforceRestartsFraction)
				{
					m_PhaseStage = TPhaseStage::PHASE_STAGE_STANDARD;
					assert(m_PhaseInitStage != m_PhaseStage);
					assert(NV(1) || P("Phase stage changed: " + GetPhaseStageStr() + "\n"));
				}
			}
		
		}		
		assert(NV(1) || P("Restart #" + to_string(m_Stat.m_Restarts) + "\n"));
	}

	return restartNow;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::RstNewAssertingGluedCls(TUV glue)
{
	assert(m_CurrRestartStrat == RESTART_STRAT_LBD);

	if (m_ParamRestartLbdBlockingEnable)
	{
		m_RstGlueBlckAsgnWin.Enqueue(m_AssignedVarsNum);
		if (m_RstGlueAssertingGluedClss > m_ParamRestartLbdBlockingConfsToConsider && m_RstGlueLbdWin.IsFullWindow() && (double)m_AssignedVarsNum > m_ParamRestartLbdBlockingAvrgMult * m_RstGlueBlckAsgnWin.GetAverage())
		{
			m_RstGlueLbdWin.Clear();
			++m_Stat.m_RestartsBlocked;
		}
	}	
	
	const auto glueThr = glue > m_ParamRestartLbdThresholdGlueVal ? m_ParamRestartLbdThresholdGlueVal : glue;
	m_RstGlueLbdWin.Enqueue(glueThr);
	m_RstGlueGlobalLbdSum += (double)glueThr;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit,TUInd,Compress>::RestartInit()
{
	assert(m_QueryCurr != TQueryType::QUERY_NONE);

	auto InitGlue = [&]()
	{
		if (m_ParamRestartLbdBlockingEnable)
		{
			m_RstGlueBlckAsgnWin.Init(m_ParamRestartLbdBlockingWinSize);
			if (unlikely(m_RstGlueBlckAsgnWin.IsError())) SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "Couldn't reserve memory for m_RstGlueBlckAsgnWin");
		}
		m_RstGlueLbdWin.Init(m_ParamRestartLbdWinSize);
		if (unlikely(m_RstGlueLbdWin.IsError())) SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "Couldn't reserve memory for m_RstGlueLbdWin");
	};

	if (m_QueryCurr == TQueryType::QUERY_INIT)
	{
		m_CurrRestartStrat = m_ParamRestartStrategyInit;
		if (m_CurrRestartStrat == RESTART_STRAT_NUMERIC)
		{
			m_RstNumericCurrConfThr = m_ParamRestartNumericInitConfThr;	
			assert(NV(1) || P("Restarts initialized to arithmetic\n"));
		}
		else if (m_CurrRestartStrat == RESTART_STRAT_LBD)
		{
			InitGlue();
			assert(NV(1) || P("Restarts initialized to glue\n"));
		}				
	}
	else if (m_ParamRestartStrategyInit != m_ParamRestartStrategyS || m_ParamRestartStrategyInit != m_ParamRestartStrategyN)
	{
		const uint8_t prevRestartStrat = m_CurrRestartStrat;

		assert(m_QueryCurr == TQueryType::QUERY_INC_SHORT || m_QueryCurr == TQueryType::QUERY_INC_NORMAL);
		m_CurrRestartStrat = m_QueryCurr == TQueryType::QUERY_INC_SHORT ? m_ParamRestartStrategyS : m_ParamRestartStrategyN;
		
		if (m_CurrRestartStrat != prevRestartStrat)
		{
			assert(prevRestartStrat == RESTART_STRAT_LBD || prevRestartStrat == RESTART_STRAT_NUMERIC);
			assert(m_CurrRestartStrat == RESTART_STRAT_LBD || m_CurrRestartStrat == RESTART_STRAT_NUMERIC);

			if (m_CurrRestartStrat == RESTART_STRAT_NUMERIC)
			{
				// LBD --> NUMERIC
				assert(prevRestartStrat == RESTART_STRAT_LBD);
				m_RstNumericCurrConfThr = m_Stat.m_Conflicts + m_ParamRestartNumericInitConfThr;		
				if (m_ParamRestartLbdBlockingEnable)
				{
					m_RstGlueBlckAsgnWin.Clear();					
				}
				m_RstGlueLbdWin.Clear();		
				assert(NV(1) || P("Restarts switched to arithmetic\n"));
			}
			else
			{
				// NUMERIC --> LBD
				assert(prevRestartStrat == RESTART_STRAT_NUMERIC);
				InitGlue();
				assert(NV(1) || P("Restarts switched to glue\n"));
			}
		}
	}	

	const auto currUnforceRestartsFraction = GetCurrUnforceRestartsFraction();

	m_PhaseInitStage = m_PhaseStage = 
		currUnforceRestartsFraction == 0. ? TPhaseStage::PHASE_STAGE_STANDARD :
		currUnforceRestartsFraction == 1. ? TPhaseStage::PHASE_STAGE_DONT_FORCE :
		m_ParamPhaseMngStartInvStrat == 0 ? TPhaseStage::PHASE_STAGE_STANDARD : m_ParamPhaseMngStartInvStrat == 1 ? TPhaseStage::PHASE_STAGE_DONT_FORCE :
		((double)rand() / (double)INT_MAX <= currUnforceRestartsFraction ? TPhaseStage::PHASE_STAGE_DONT_FORCE : TPhaseStage::PHASE_STAGE_STANDARD);

	m_RestartsSinceInvStart = 0;

	assert(NV(1) || P("Phase stage set: " + GetPhaseStageStr() + "\n"));
}

template <typename TLit, typename TUInd, bool Compress>
double CTopi<TLit,TUInd,Compress>::RestartLubySequence(double y, uint64_t x)
{
	// Finite subsequences of the Luby-sequence:
	// 0: 1
	// 1: 1 1 2
	// 2: 1 1 2 1 1 2 4
	// 3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
	// ...

	// Find the finite subsequence that contains index 'x' and the size of that subsequence:
	uint64_t size = 1, seq = 0;
	for (; size < x + 1; seq++, size = 2 * size + 1);

	while (size - 1 != x) 
	{
		size = (size - 1) >> 1;
		seq--;
		x = x % size;
	}

	return pow(y, seq);
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
