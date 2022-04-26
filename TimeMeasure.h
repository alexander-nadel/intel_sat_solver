// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <chrono>
#include <ctime>
#include <limits>

struct CTimeOutException {};
struct CUserInterruptException {};

class CTimeMeasure
{
public:
	CTimeMeasure(bool genericModeCpuTime = false, unsigned timeoutTestModuloFactor = 1) :
		c_start(std::clock()),
		t_start(std::chrono::high_resolution_clock::now()),
		m_Timeout(TIME_INFINITY),
		m_GenericModeCpuTime(genericModeCpuTime),
		m_TimeoutTestModuloFactor(timeoutTestModuloFactor),
		m_TimeoutTestCounter(0),
		m_LatestResForTestCounter(0.)
	{}

	// Generic functions, which don't depend on the value of m_GenericModeCpuTime

	inline double GetTimeout() const 
	{
		return m_Timeout;
	}

	inline void SetModeWallTime()
	{
		m_GenericModeCpuTime = false;
	}

	inline void SetModeCpuTime()
	{
		m_GenericModeCpuTime = true;
	}

	inline bool IsTimeoutSet() const
	{
		return m_Timeout != TIME_INFINITY;
	}

	inline void SetTimeout(double timeout)
	{
		if (timeout > 0)
		{
			m_Timeout = timeout;
		}
	}

	inline void Stop()
	{
		m_Timeout = 0.00000001;
	}

	inline void Reset()
	{
		c_start = std::clock();
		t_start = std::chrono::high_resolution_clock::now();
	}

	// Generic functions, which depend on the value of m_GenericModeCpuTime

	inline double TimePassedSinceStartOrReset()
	{
		return m_GenericModeCpuTime ? CpuTimePassedSinceStartOrReset() : WallTimePassedSinceStartOrReset();
	}
	
	inline bool IsTimeout()
	{
		return m_GenericModeCpuTime ? CpuIsTimeout() : WallIsTimeout();		
	}

	inline void ThrowExceptionIfTimeoutPassed()
	{
		m_GenericModeCpuTime ? CpuTimeThrowExceptionIfTimeoutPassed() : WallTimeThrowExceptionIfTimeoutPassed();
	}

	inline double TimeLeftTillTimeout()
	{
		return m_GenericModeCpuTime ? CpuTimeLeftTillTimeout() : WallTimeLeftTillTimeout();
	}

	// Functionality per CPU time and per Wall time

	inline double CpuTimePassedSinceStartOrResetConst() const
	{
		return (std::clock() - c_start) / CLOCKS_PER_SEC;
	}

	double CpuTimePassedSinceStartOrReset()
	{
		if (m_TimeoutTestModuloFactor == 1)
		{
			return CpuTimePassedSinceStartOrResetConst();
		}		
		else
		{			
			if (m_TimeoutTestCounter % m_TimeoutTestModuloFactor == 0)
			{
				++m_TimeoutTestCounter;
				return m_LatestResForTestCounter = (std::clock() - c_start) / CLOCKS_PER_SEC;
			}
			else
			{
				++m_TimeoutTestCounter;
				return m_LatestResForTestCounter;
			}			
		}
	}

	inline double WallTimePassedSinceStartOrResetConst() const
	{
		const auto t_end = std::chrono::high_resolution_clock::now();
		return std::chrono::duration<double>(t_end - t_start).count();
	}

	double WallTimePassedSinceStartOrReset()
	{
		if (m_TimeoutTestModuloFactor == 1)
		{
			return WallTimePassedSinceStartOrResetConst();
		}
		else
		{
			if (m_TimeoutTestCounter % m_TimeoutTestModuloFactor == 0)
			{
				++m_TimeoutTestCounter;
				auto t_end = std::chrono::high_resolution_clock::now();
				return m_LatestResForTestCounter = std::chrono::duration<double>(t_end - t_start).count();
			}
			else
			{
				++m_TimeoutTestCounter;
				return m_LatestResForTestCounter;
			}
		}
	}

	inline bool CpuIsTimeout()
	{
		return m_Timeout - CpuTimePassedSinceStartOrReset() <= 0;
	}

	inline bool WallIsTimeout()
	{
		return m_Timeout < TIME_INFINITY && m_Timeout - WallTimePassedSinceStartOrReset() <= 0;
	}

	inline void CpuTimeThrowExceptionIfTimeoutPassed()
	{
		if (CpuIsTimeout())
		{
			throw CTimeOutException();
		}
	}

	inline void WallTimeThrowExceptionIfTimeoutPassed()
	{
		if (WallIsTimeout())
		{
			throw CTimeOutException();
		}
	}
	
	inline double WallTimeLeftTillTimeout()
	{
		return (m_Timeout == TIME_INFINITY) ? TIME_INFINITY : m_Timeout - WallTimePassedSinceStartOrReset();
	}

	inline double CpuTimeLeftTillTimeout()
	{
		return  (m_Timeout == TIME_INFINITY) ? TIME_INFINITY : m_Timeout - CpuTimePassedSinceStartOrReset();
	}

	constexpr static double TIME_INFINITY = (std::numeric_limits<double>::max)();

	inline void SetTestModuloFactor(unsigned timeoutTestModuloFactor) { m_TimeoutTestModuloFactor = timeoutTestModuloFactor; }

private:
	 std::clock_t c_start;
	 std::chrono::high_resolution_clock::time_point t_start;
	 double m_Timeout;
	 bool m_GenericModeCpuTime;
	 unsigned m_TimeoutTestModuloFactor;
	 unsigned m_TimeoutTestCounter;
	 double m_LatestResForTestCounter;
};

