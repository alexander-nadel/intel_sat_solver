// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include <functional>
#include <string>
#include <bit>
#include <iterator> 
#include <any>
#include <algorithm>
#include <numeric>
#include "Topi.hpp"
#include "SetInScope.h"
#include "Diamond.h"
#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#define getpid _getpid
#endif

using namespace Topor;
using namespace std;

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::DumpSetUp(const char* filePrefix)
{
	assert(filePrefix != nullptr);

	stringstream ss;

	ss << filePrefix << "_";

	time_t now;
	time(&now);
	struct tm* current = localtime(&now);

	ss << (void*)this << '_' << getpid() << '_';
	if (current != NULL)
	{
		ss << current->tm_mday << "." << (current->tm_mon + 1) << "." << 1900 + current->tm_year << '_' <<
			current->tm_hour << "_" << current->tm_min << "_" << current->tm_sec;
	}
	ss << ".cnf";

	m_DumpFile.reset(new std::ofstream(ss.str(), ofstream::out));
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::ReadAnyParamsFromFile()
{
	const char* configFileName = getenv("TOPOR_CONFIG_FILE");
	if (configFileName == nullptr)
	{
		return;
	}

	fstream tcf(configFileName, ios::in);
	if (!tcf.is_open())
	{
		SetStatus(TToporStatus::STATUS_PARAM_ERROR, "Cannot open the configuration parameter file " + (string)configFileName);
		return;
	}

	string paramSetting;

	auto Trim = [&](string_view s)
	{
		s.remove_prefix(min(s.find_first_not_of(" \t\r\v\n"), s.size()));
		s.remove_suffix(min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));

		return s;
	};


	unsigned lineNum = 1;
	while (getline(tcf, paramSetting))
	{
		auto SetErrStatus = [&](const string& reason)
		{
			SetStatus(TToporStatus::STATUS_PARAM_ERROR, "Could parse line" + to_string(lineNum) + " : " + paramSetting + " (after trimming \\t\\r\\v\\n); Reason: " + reason);
		};

		// Consider paramSetting = " /q/w 1\n"
		// After Trim, we'll have paramSetting = "/q/w 1"
		Trim(paramSetting);
		
		// spaceInd = 4 for "/q/w 1"
		auto spaceInd = paramSetting.find(' ');
		if (spaceInd == string::npos)
		{
			SetErrStatus("space not found");
			return;
		}
		
		// paramName = "/q/w" for "/q/w 1"
		const string paramName = paramSetting.substr(0, spaceInd);
		// paramVal = "1" for "/q/w 1"
		const string paramVal = paramSetting.substr(spaceInd + 1, string::npos);
		double paramValDouble;
		try
		{
			paramValDouble = stod(paramVal);
		}
		catch (...)
		{
			SetErrStatus("couldn't convert the value " + paramVal + " to double");
			return;
		}

		SetParam(paramName, paramValDouble);
		if (IsUnrecoverable())
		{
			return;
		}

		++lineNum;
	}

	tcf.close();
}

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::CTopi(TLit varNumHint) : m_InitVarNumAlloc(varNumHint <= 0 ? InitEntriesInB : (size_t)varNumHint + 1), m_E2ILitMap(m_InitVarNumAlloc, (size_t)0),
m_HandleNewUserCls(m_InitVarNumAlloc), m_Watches(GetInitLitNumAlloc(), (size_t)0), m_TrailLastVarPerDecLevel(1, BadClsInd),
m_VarInfo(m_InitVarNumAlloc, (size_t)0),
m_Stat([&]() { return Compress ? m_BC.size() : 1; }, [&]() { return Compress ? BCCapacitySum() : m_B.cap(); }, [&]() { return Compress ? BCNextBitSum() / 64 + 1 : m_BNext; }, [&]() { return GetMemoryLayout(); }, m_ParamVarActivityInc), m_VsidsHeap(m_Stat.m_VarActivityInc)
{
	m_AssignmentInfo.reserve_exactly(m_InitVarNumAlloc, (size_t)0);

	static bool diamondInvokedTopor = false;
	DIAMOND("topor", diamondInvokedTopor);

	const char* dumpFileName = getenv("TOPOR_DUMP_NAME");
	if (dumpFileName != nullptr)
	{
		DumpSetUp(dumpFileName);
		assert(m_DumpFile);
		if (m_DumpFile) (*m_DumpFile) << "p cnf " << varNumHint << " " << 0 << endl;
	}

	if (m_Params.IsError())
	{
		SetStatus(TToporStatus::STATUS_PARAM_ERROR, m_Params.GetErrorDescr());
	}
	else if (m_B.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi<TLit,TUInd,Compress>::CTopi: couldn't allocate the main buffer");
	}
	else if (m_InitVarNumAlloc != 0 && m_E2ILitMap.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi<TLit,TUInd,Compress>::CTopi: couldn't allocate m_E2IVarMap");
	}
	else if (GetInitLitNumAlloc() != 0 && m_Watches.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi<TLit,TUInd,Compress>::CTopi: couldn't allocate m_Watches");
	}
	else if (m_InitVarNumAlloc != 0 && m_AssignmentInfo.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi<TLit,TUInd,Compress>::CTopi: couldn't allocate m_AssignmentInfo");
	}
	else if (m_InitVarNumAlloc != 0 && m_VarInfo.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi<TLit,TUInd,Compress>::CTopi: couldn't allocate m_VarInfo");
	}
	else if (m_TrailLastVarPerDecLevel.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi<TLit,TUInd,Compress>::CTopi: couldn't allocate m_TrailLastVarPerDecLevel");
	}

	SetMultipliers();

	ReadAnyParamsFromFile();

	// m_DebugModel = { false, true, false, true, false, false, false, true, false, false, false, false, true, false, true, false, false, true, true, true, true };
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::SetParam(const string& paramName, double newVal)
{
	if (m_DumpFile) (*m_DumpFile) << "r " << paramName << " " << newVal << endl;

	if (IsUnrecoverable())
	{
		return;
	}

	auto HandleContextParam = [&](const string& contextParamPrefix, vector<pair<string, double>>& paramVals)
	{
		if (paramName.starts_with(contextParamPrefix))
		{
			paramVals.emplace_back(make_pair(paramName.substr(contextParamPrefix.size()), newVal));
			return true;
		}
		return false;
	};

	if (HandleContextParam(m_ContextParamAfterInitInvPrefix, m_AfterInitInvParamVals))
	{
		return;
	}

	if (HandleContextParam(m_ContextParamShortInvLifetimePrefix, m_ShortInvLifetimeParamVals))
	{
		return;
	}

	m_Params.SetParam(paramName, newVal);
	if (m_Params.IsError())
	{
		SetStatus(TToporStatus::STATUS_PARAM_ERROR, m_Params.GetErrorDescr());
	}

	SetOverallTimeoutIfAny();

	if (IsMultiplierParam(paramName) || paramName == m_ModeParamName)
	{
		SetMultipliers();
	}

	if (IsVsidsInitOrderParam(paramName) || paramName == m_ModeParamName)
	{
		m_VsidsHeap.SetInitOrder(m_ParamVsidsInitOrder);
	}
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::IsError() const
{
	return IsErroneous();
}

template <typename TLit, typename TUInd, bool Compress>
size_t CTopi<TLit, TUInd, Compress>::GetInitLitNumAlloc() const
{
	assert(m_InitVarNumAlloc <= rotr((size_t)1, 1));
	const size_t initLitNumAlloc = m_InitVarNumAlloc << 1;
	return initLitNumAlloc == 0 ? numeric_limits<size_t>::max() : initLitNumAlloc;
}

template <typename TLit, typename TUInd, bool Compress>
TToporReturnVal CTopi<TLit, TUInd, Compress>::StatusToRetVal() const
{
	if (IsUnrecoverable())
	{
		return UnrecStatusToRetVal();
	}

	if (m_Status == TToporStatus::STATUS_SAT)
	{
		return TToporReturnVal::RET_SAT;
	}

	if (m_Status == TToporStatus::STATUS_UNSAT)
	{
		return TToporReturnVal::RET_UNSAT;
	}

	if (m_Status == TToporStatus::STATUS_USER_INTERRUPT)
	{
		return TToporReturnVal::RET_USER_INTERRUPT;
	}

	return TToporReturnVal::RET_EXOTIC_ERROR;
}

template <typename TLit, typename TUInd, bool Compress>
TToporReturnVal CTopi<TLit, TUInd, Compress>::UnrecStatusToRetVal() const
{
	assert(IsUnrecoverable());

	if (unlikely(m_Status == TToporStatus::STATUS_CONTRADICTORY)) return TToporReturnVal::RET_UNSAT;
	if (unlikely(m_Status == TToporStatus::STATUS_ALLOC_FAILED)) return TToporReturnVal::RET_MEM_OUT;
	if (unlikely(m_Status == TToporStatus::STATUS_INDEX_TOO_NARROW)) return TToporReturnVal::RET_INDEX_TOO_NARROW;
	if (unlikely(m_Status == TToporStatus::STATUS_PARAM_ERROR)) return TToporReturnVal::RET_PARAM_ERROR;
	if (unlikely(m_Status == TToporStatus::STATUS_ASSUMPTION_REQUIRED_ERROR)) return TToporReturnVal::RET_ASSUMPTION_REQUIRED_ERROR;
	if (unlikely(m_Status == TToporStatus::STATUS_GLOBAL_TIMEOUT)) return TToporReturnVal::RET_TIMEOUT_GLOBAL;
	if (unlikely(m_Status == TToporStatus::STATUS_DRAT_FILE_PROBLEM)) return TToporReturnVal::RET_DRAT_FILE_PROBLEM;

	assert(m_Status == TToporStatus::STATUS_EXOTIC_ERROR);

	return TToporReturnVal::RET_EXOTIC_ERROR;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::HandleIncomingUserVar(TLit v, bool isUndoable)
{
	if (unlikely((size_t)v >= m_E2ILitMap.cap()))
	{
		m_E2ILitMap.reserve_atleast((size_t)v + 1, (size_t)BadULit);
		if (unlikely(m_E2ILitMap.uninitialized_or_erroneous()))
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "HandleIncomingUserVar: couldn't reserve m_E2IVarMap");
			return;
		}
	}

	if (m_E2ILitMap[v] == 0)
	{
		m_E2ILitMap[v] = GetLit(++m_LastExistingVar, false);
		if (UseI2ELitMap())
		{
			m_I2ELitMap.reserve_atleast(m_LastExistingVar + 1, 0);
			if (unlikely(m_I2ELitMap.uninitialized_or_erroneous()))
			{
				SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "HandleIncomingUserVar: couldn't reserve m_I2ELitMap");
				return;
			}
			m_I2ELitMap[m_LastExistingVar] = v;
		}

		m_Stat.UpdateMaxInternalVar(m_LastExistingVar);

		if (!isUndoable)
		{
			m_VsidsHeap.insert(m_LastExistingVar, 0.0);

			if (unlikely(m_VsidsHeap.uninitialized_or_erroneous()))
			{
				SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "HandleIncomingUserVar: couldn't insert into m_VsidsHeap");
				return;
			}
		}
		else
		{
			m_NewExternalVarsAddUserCls.push_back(v);
			if (unlikely(m_NewExternalVarsAddUserCls.uninitialized_or_erroneous()))
			{
				SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "HandleIncomingUserVar: couldn't push into m_NewExternalVarsAddUserCls");
				return;
			}
		}

		if (unlikely(m_LastExistingVar >= m_AssignmentInfo.cap()))
		{
			m_AssignmentInfo.reserve_atleast(GetNextVar(), (size_t)0);
			if (unlikely(m_AssignmentInfo.uninitialized_or_erroneous()))
			{
				SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "HandleIncomingUserVar: couldn't realloc m_AssignmentInfo");
				return;
			}
		}

		if (unlikely(m_LastExistingVar >= m_VarInfo.cap()))
		{
			m_VarInfo.reserve_atleast(GetNextVar(), (size_t)0);
			if (unlikely(m_VarInfo.uninitialized_or_erroneous()))
			{
				SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "HandleIncomingUserVar: couldn't realloc m_VarInfo");
				return;
			}
		}
	}

	m_Stat.m_MaxUserVar = max(m_Stat.m_MaxUserVar, v);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::DumpSpan(const span<TLit> c, const string& prefix, const string& suffix, bool addNewLine)
{
	(*m_DumpFile) << prefix;
	for (size_t i = 0; i < c.size(); ++i)
	{
		(*m_DumpFile) << c[i];
		if (i != c.size() - 1)
		{
			(*m_DumpFile) << ' ';
		}
	}
	(*m_DumpFile) << suffix;
	if (addNewLine)
	{
		(*m_DumpFile) << endl;
	}
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::AddUserClause(const span<TLit> c)
{
	if (m_DumpFile && !m_ParamDontDumpClauses) DumpSpan(c, "", " 0");

	AssumpUnsatCoreCleanUpIfRequired();

	if (m_ParamAddClsAtLevel0 && m_DecLevel != 0)
	{
		Backtrack(0);
	}

	const auto lastExistingVarStart = m_LastExistingVar;

	bool isSuccess = false;
	bool boostScores = false;

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		if (isSuccess)
		{
			for (TLit ev : m_NewExternalVarsAddUserCls.get_span())
			{
				m_VsidsHeap.insert(GetVar(m_E2ILitMap[ev]), 0.0);

				if (unlikely(m_VsidsHeap.uninitialized_or_erroneous()))
				{
					SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "AddUserClause: couldn't insert into m_VsidsHeap");
					return;
				}
			}

			if (boostScores)
			{
				auto cls = m_HandleNewUserCls.GetCurrCls();
				const double mult = InitClssBoostScoreStratIsClauseSizeAware() ? m_CurrInitClssBoostScoreMult * (1. / (double)(cls.size() - 1)) : m_CurrInitClssBoostScoreMult;
				for (TULit l : cls)
				{
					const TUVar v = GetVar(l);
					UpdateScoreVar(v, mult);
				}

				if (InitClssBoostScoreStratIsReversedOrder())
				{
					if (m_CurrInitClssBoostScoreMult < m_ParamInitClssBoostMultHighest)
					{
						m_CurrInitClssBoostScoreMult += m_ParamInitClssBoostMultDelta;
					}
				}
				else
				{
					if (m_CurrInitClssBoostScoreMult > m_ParamInitClssBoostMultLowest)
					{
						m_CurrInitClssBoostScoreMult -= m_ParamInitClssBoostMultDelta;
					}
				}
			}
		}
		else
		{
			m_LastExistingVar = lastExistingVarStart;
			m_Stat.UpdateMaxInternalVar(m_LastExistingVar);

			for (TLit ev : m_NewExternalVarsAddUserCls.get_span())
			{
				m_E2ILitMap[ev] = BadULit;
			}
		}
		m_NewExternalVarsAddUserCls.clear();
	});

	++m_Stat.m_AddClauseInvs;

	if (unlikely(IsUnrecoverable())) return;

	if (unlikely(c.empty() || c[0] == 0))
	{
		// An empty clause		
		SetStatus(TToporStatus::STATUS_CONTRADICTORY, "AddClause: an empty clause provided");
		return;
	}

	// Storing the new clause while detecting tautologies and filtering out duplicates
	m_HandleNewUserCls.NewClause();

	for (const TLit l : c)
	{
		if (unlikely(l == 0))
		{
			// End-of-clause; we are done with the loop
			break;
		}

		static_assert(sizeof(TLit) <= sizeof(size_t));
		if constexpr (sizeof(TLit) == sizeof(size_t))
		{
			if (unlikely(l == numeric_limits<TLit>::max() || l == numeric_limits<TLit>::min()))
			{
				// We cannot accommodate the last possible variable if  sizeof(TLit) == sizeof(size_t), since the allocation will fail
				SetStatus(TToporStatus::STATUS_INDEX_TOO_NARROW, "AddClause: we cannot accommodate the last possible variable if  sizeof(TLit) == sizeof(size_t), since allocation will fail");
				return;
			}
		}

		// External variable
		const TLit v = ExternalLit2ExternalVar(l);
		HandleIncomingUserVar(v, true);
		if (unlikely(IsUnrecoverable())) return;

		const TULit litInternal = E2I(l);

		if (IsFalsified(litInternal) && GetAssignedDecLevel(litInternal) == 0)
		{
			// The literal is falsified at decision level 0: no need to add it to the clause
			continue;
		}

		auto [isTau, isDuplicate, isAllocFailed] = m_HandleNewUserCls.NewLitIsTauIsDuplicate(litInternal);

		if (unlikely(isAllocFailed))
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "AddClause: allocation failed during tautology&duplication test");
			return;
		}

		if (unlikely(isTau))
		{
			return;
		}

		if (unlikely(isDuplicate))
		{
			// Duplicate literal
			continue;
		}

		if (m_ParamAddClsRemoveClssGloballySatByLitMinSize != numeric_limits<uint32_t>::max() && c.size() > m_ParamAddClsRemoveClssGloballySatByLitMinSize && IsSatisfied(litInternal) && GetAssignedDecLevel(litInternal) == 0)
		{
			// The clause is globally satisfied
			return;
		}

		// Handling the watches now		
		if (unlikely(GetMaxLit(litInternal) >= m_Watches.cap()))
		{
			m_Watches.reserve_atleast((size_t)GetMaxLit(litInternal) + 1, (size_t)BadULit);
			if (m_Watches.uninitialized_or_erroneous())
			{
				SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "AddClause: couldn't reserve m_Watches");
				return;
			}
		}

		auto cls = m_HandleNewUserCls.GetCurrCls();

		if (cls.size() == 2 && WLIsLitBetter(cls[1], cls[0]))
		{
			swap(cls[0], cls[1]);
		}

		if (cls.size() > 2)
		{
			if (WLIsLitBetter(litInternal, cls[0]))
			{
				swap(cls[0], cls.back());
				swap(cls.back(), cls[1]);
			}
			else if (WLIsLitBetter(litInternal, cls[1]))
			{
				swap(cls[1], cls.back());
			}
		}
	}

	auto cls = m_HandleNewUserCls.GetCurrCls();

	if (cls.size() == 0)
	{
		// The clause must have been contradictory (with some 0-level literals removed)
		SetStatus(TToporStatus::STATUS_CONTRADICTORY, "AddClause: the clause is contradictory");
		return;
	}

	if (cls.size() == 1 && m_NewExternalVarsAddUserCls.size() == 1 && m_TrailStart != BadUVar && GetAssignedDecLevelVar(m_TrailStart) == 0 && (!IsAssigned(cls[0]) || GetAssignedDecLevel(cls[0]) != 0))
	{
		// Exchange cls[0] by m_TrailStart's satisfied literal

		const TULit iSatLit = GetAssignedLitForVar(m_TrailStart);
		const TLit eSatVar = m_NewExternalVarsAddUserCls[0];

		const auto eSatLitIt = find_if(c.begin(), c.end(), [&](TLit l) { return ExternalLit2ExternalVar(l) == eSatVar; });
		assert(eSatLitIt != c.end());

		m_E2ILitMap[eSatVar] = *eSatLitIt == eSatVar ? iSatLit : Negate(iSatLit);
		m_NewExternalVarsAddUserCls.clear();
		return;
	}

	isSuccess = true;

	assert(NV(2) || P("The user clause to internal clause: " + SUserLits(c) + " to " + SLits(cls) + "\n"));

	auto OnContradiction = [&]()
	{
		const auto decLevel = GetAssignedDecLevel(cls[0]);
		assert(decLevel != 0);
		Backtrack(decLevel - 1);
		assert(!IsAssigned(cls[0]));
		m_ToPropagate.erase_if_may_reorder([&](TULit l)
		{
			return !IsAssigned(l);
		});
	};

	if (cls.size() == 1)
	{
		// Unary clause: add an implication (or a delayed implication)
		if (IsSatisfied(cls[0]))
		{
			if (GetAssignedDecLevel(cls[0]) != 0)
			{
				CVector<TContradictionInfo> cis;
				ProcessDelayedImplication(cls[0], BadULit, BadClsInd, cis);
			}
		}
		else
		{
			if (IsFalsified(cls[0]))
			{
				OnContradiction();
			}
			Assign(cls[0], BadClsInd, BadULit, 0);

			// If we've just assigned the variable globally and the variable is new,
			// there is no need to run Simplify, since it doesn't satisfy or appear falsified in any existing clauses, and
			// this very function will make sure that any future clauses satisfied by it are skipped and any falsified appearances are skipped too
			if (GetVar(cls[0]) == m_LastExistingVar && m_LastExistingVar != lastExistingVarStart && m_LastGloballySatisfiedLitAfterSimplify == m_VarInfo[GetVar(cls[0])].m_TrailPrev)
			{
				m_LastGloballySatisfiedLitAfterSimplify = m_LastExistingVar;
			}
		}

		return;
	}

	auto clsStart = AddClsToBufferAndWatch(cls, false, false);
	assert(NV(2) || P("Clause start is " + HexStr(clsStart) + "\n"));
	if (unlikely(IsUnrecoverable())) return;

	if (InitClssBoostScoreStrat() != 0 && cls.size() > 1)
	{
		boostScores = true;
	}

	// Take care of unit and contradictory clauses
	if (IsFalsified(cls[0]) || (!IsAssigned(cls[0]) && IsFalsified(cls[1])))
	{
		if (IsFalsified(cls[0]))
		{
			// Contradictory!
			OnContradiction();
		}

		// By now the clause is either unit or with two unassigned literals
		if (IsFalsified(cls[1]))
		{
			assert(!IsAssigned(cls[0]));
			Assign(cls[0], clsStart, cls[1], GetAssignedDecLevel(cls[1]));
		}
	}

	// Take care of delayed implications
	if (IsSatisfied(cls[0]) && IsFalsified(cls[1]) && GetAssignedDecLevel(cls[0]) > GetAssignedDecLevel(cls[1]))
	{
		CVector<TContradictionInfo> cis;
		ProcessDelayedImplication(cls[0], cls[1], clsStart, cis);
	}
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::IsAssumptionRequired(size_t assumpInd)
{
	if (m_Status == TToporStatus::STATUS_CONTRADICTORY)
	{
		return false;
	}

	if (m_Stat.m_SolveInvs == 0 || m_Status != TToporStatus::STATUS_UNSAT || assumpInd >= m_UserAssumps.size())
	{
		SetStatus(TToporStatus::STATUS_ASSUMPTION_REQUIRED_ERROR, m_Stat.m_SolveInvs == 0 ? "No Solve invocations so far" : m_Status != TToporStatus::STATUS_UNSAT ? "The latest Solve didn't return TToporStatus::STATUS_UNSAT" : "The assumption ID is beyond the number of assumptions in the latest Solve invocation");
		return false;
	}

	if (m_LatestAssumpUnsatCoreSolveInvocation != m_Stat.m_SolveInvs)
	{
		assert(m_SelfContrOrGloballyUnsatAssumpSolveInv == m_Stat.m_SolveInvs || m_LatestEarliestFalsifiedAssumpSolveInv == m_Stat.m_SolveInvs);
		assert(m_SelfContrOrGloballyUnsatAssumpSolveInv != m_LatestEarliestFalsifiedAssumpSolveInv);
		assert(m_LatestEarliestFalsifiedAssumpSolveInv != m_Stat.m_SolveInvs || IsAssigned(m_LatestEarliestFalsifiedAssump));		
		
		// Making sure that we may return true only for one external variable out of a group mapped to the same internal variable
		for (TLit& externalLit : m_UserAssumps)
		{
			if (externalLit != 0)
			{
				TULit l = E2I(externalLit);
				assert(l != BadULit);
				if ((IsNeg(l) && IsRooted(l)) || (!IsNeg(l) && IsVisited(l)))
				{
					externalLit = 0;
				}
				else
				{
					IsNeg(l) ? MarkRooted(l) : MarkVisited(l);
				}
			}
		}

		CleanRooted();
		CleanVisited();

		if (m_SelfContrOrGloballyUnsatAssumpSolveInv == m_Stat.m_SolveInvs)
		{
			MarkVisited(m_SelfContrOrGloballyUnsatAssump);
		}

		if (m_LatestEarliestFalsifiedAssumpSolveInv == m_Stat.m_SolveInvs)
		{
			MarkDecisionsInConeAsVisited(m_LatestEarliestFalsifiedAssump);
		}

		m_LatestAssumpUnsatCoreSolveInvocation = m_Stat.m_SolveInvs;
	}

	const bool nonDuplicatedAndVisited = m_UserAssumps[assumpInd] != 0 && IsVisited(E2I(m_UserAssumps[assumpInd]));
	const bool if0DecLevelThenFalsified = m_LatestEarliestFalsifiedAssumpSolveInv == m_Stat.m_SolveInvs || !IsAssigned(m_SelfContrOrGloballyUnsatAssump) || GetAssignedDecLevel(m_SelfContrOrGloballyUnsatAssump) != 0 || IsFalsified(E2I(m_UserAssumps[assumpInd]));
	return nonDuplicatedAndVisited && if0DecLevelThenFalsified;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::AssignAssumptions(size_t firstUnassignedAssumpInd)
{
	m_DecLevelOfLastAssignedAssumption = m_DecLevel;
	if (firstUnassignedAssumpInd != m_Assumps.cap())
	{
		TSpanTULit potentiallyUnassignedAssumpsSpan = m_Assumps.get_span_cap(firstUnassignedAssumpInd);
		bool someAssumpsAreUnassigned = true;
		while (someAssumpsAreUnassigned)
		{
			someAssumpsAreUnassigned = false;
			for (size_t assumpLitI = 0; assumpLitI < potentiallyUnassignedAssumpsSpan.size(); ++assumpLitI)
			{
				const TULit assumpLit = potentiallyUnassignedAssumpsSpan[assumpLitI];

				if (!IsAssigned(assumpLit))
				{
					NewDecLevel();

					[[maybe_unused]] bool isContraditory = Assign(assumpLit, BadULit, BadULit, m_DecLevel);
					assert(!isContraditory);

					TContradictionInfo contradictionInfo = BCP();
					ConflictAnalysisLoop(contradictionInfo);
					if (unlikely(IsUnrecoverable())) return;

					if (m_EarliestFalsifiedAssump != BadULit)
					{
						if (IsAssigned(m_EarliestFalsifiedAssump) && IsAssumpFalsifiedGivenVar(GetVar(m_EarliestFalsifiedAssump)))
						{
							SetStatus(TToporStatus::STATUS_UNSAT, "Falsified assumption discovered after setting and propagating an assumption at decision level " + to_string(m_DecLevel));
							return;
						}			
						else
						{
							// BCP backtracked
							size_t newFirstUnassignedAssumpInd = FindFirstUnassignedAssumpIndex(firstUnassignedAssumpInd + assumpLitI + 1);
							AssignAssumptions(newFirstUnassignedAssumpInd);
							return;
						}
					}

					if (!IsAssigned(assumpLit))
					{
						someAssumpsAreUnassigned = true;
					}
				}
				else
				{
					assert(!IsFalsified(assumpLit));
				}
			}
		}
		assert(all_of(potentiallyUnassignedAssumpsSpan.begin(), potentiallyUnassignedAssumpsSpan.end(), [&](TULit l) { return IsAssigned(l); }));
	}
}

template <typename TLit, typename TUInd, bool Compress>
size_t CTopi<TLit, TUInd, Compress>::FindFirstUnassignedAssumpIndex(size_t indexBeyondHandledAssumps)
{
	assert(indexBeyondHandledAssumps <= m_Assumps.cap());
	for (size_t assumpI = 0; assumpI < indexBeyondHandledAssumps; ++assumpI)
	{
		const TULit lAssump = m_Assumps[assumpI];
		if (!IsAssigned(lAssump))
		{
			return assumpI;
		}
	}

	return indexBeyondHandledAssumps;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::HandleAssumptionsIfBacktrackedBeyondThem()
{
	assert(m_DecLevel < m_DecLevelOfLastAssignedAssumption);	
	assert(m_Assumps.cap() > 0);
	size_t firstUnassignedAssumpInd = FindFirstUnassignedAssumpIndex(m_Assumps.cap());
	AssignAssumptions(firstUnassignedAssumpInd);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::HandleAssumptions(const span<TLit> userAssumps)
{
	m_UserAssumps.assign(userAssumps.begin(), userAssumps.end());
	
	assert(NV(1) || P("Call " + to_string(m_Stat.m_SolveInvs) + "; User assumptions: " + SUserLits(userAssumps) + "\n"));

	m_DecLevelOfLastAssignedAssumption = 0;

	if (userAssumps.empty() || userAssumps[0] == 0)
	{
		return;
	}

	ReserveExactly(m_Assumps, userAssumps.size(), "m_Assumps in HandleAssumptions");
	if (unlikely(IsUnrecoverable())) return;

	{
		TSpanTULit assumpsSpan = m_Assumps.get_span_cap();

		transform(userAssumps.begin(), userAssumps.end(), assumpsSpan.begin(), [&](TLit userLit) {
			return E2I(userLit);
		});
	}

	assert(NV(1) || P("Call " + to_string(m_Stat.m_SolveInvs) + "; Intr assumptions: " + SLits(m_Assumps.get_span_cap()) + "\n"));

	// Remove the trailing zero (if any)
	if (m_Assumps[m_Assumps.cap() - 1] == BadULit)
	{
		m_Assumps.reserve_exactly(m_Assumps.cap() - 1);
	}

	assert(m_Assumps.cap() > 0 && m_Assumps[m_Assumps.cap() - 1] != BadULit);

	for (size_t assumpI = 0; assumpI < m_Assumps.cap(); ++assumpI)
	{
		const TULit lAssump = m_Assumps[assumpI];
		assert(lAssump != BadULit);
		const TUVar vAssump = GetVar(lAssump);
		TAssignmentInfo& ai = m_AssignmentInfo[vAssump];

		auto RemoveAssump = [&]()
		{
			if (m_ParamAssumpsSimpAllowReorder)
			{
				m_Assumps[assumpI--] = m_Assumps[m_Assumps.cap() - 1];
				m_Assumps.reserve_exactly(m_Assumps.cap() - 1);
			}
			else
			{
				m_Assumps[assumpI] = BadULit;
			}
		};

		if (IsAssigned(lAssump) && GetAssignedDecLevel(lAssump) == 0)
		{
			if (IsFalsified(lAssump))
			{
				m_SelfContrOrGloballyUnsatAssump = lAssump;
				assert(NV(1) || P("The external assumption " + to_string(*find_if(m_UserAssumps.begin(), m_UserAssumps.end(), [&](TLit l) { return E2I(l) == lAssump; })) + " is falsified at level 0\n"));
				m_SelfContrOrGloballyUnsatAssumpSolveInv = m_Stat.m_SolveInvs;
				SetStatus(TToporStatus::STATUS_UNSAT, "An assumption is falsified at decision level 0");
				return;
			}
			else
			{
				RemoveAssump();
			}
		}
		else if (unlikely(ai.m_IsAssump))
		{
			if (IsNeg(lAssump) != ai.m_IsAssumpNegated)
			{
				m_SelfContrOrGloballyUnsatAssump = lAssump;
				m_SelfContrOrGloballyUnsatAssumpSolveInv = m_Stat.m_SolveInvs;
				SetStatus(TToporStatus::STATUS_UNSAT, "Discovered two assumption literals representing the same variable in the two different polarities");
				return;
			}
			else
			{
				RemoveAssump();
			}
		}
		else
		{
			ai.m_IsAssump = true;
			ai.m_IsAssumpNegated = IsNeg(lAssump);
		}
	}

	if (!m_ParamAssumpsSimpAllowReorder)
	{
		m_Assumps.remove_if_equal_and_cut_capacity(BadULit);
	}

	// Find the first decision level, whose decision variable doesn't appear on the list of assumptions
	TUV btLevel = 0;
	for (TUV dl = 1; dl <= m_DecLevel; ++dl, ++btLevel)
	{
		const TUVar currDecVar = GetDecVar(dl);
		assert(!m_AssignmentInfo[currDecVar].m_IsAssignedInBinary && m_VarInfo[currDecVar].m_ParentClsInd == BadClsInd);
		if (!IsAssumpVar(currDecVar) || IsAssumpFalsifiedGivenVar(currDecVar))
		{
			break;
		}
	}

	// Backtrack to the first uncovered decision level
	m_Stat.m_AssumpReuseBacktrackLevelsSaved += btLevel;
	Backtrack(btLevel);

	// Find the first unassigned assumption and check if there is no contradiction
	size_t firstUnassignedAssumpInd = m_Assumps.cap();
	for (size_t assumpI = 0; assumpI < m_Assumps.cap(); ++assumpI)
	{
		const TULit lAssump = m_Assumps[assumpI];
		if (firstUnassignedAssumpInd == m_Assumps.cap() && !IsAssigned(lAssump))
		{
			firstUnassignedAssumpInd = assumpI;
		}

		const auto isAssigned = IsAssigned(lAssump);
		const auto isNegated = IsAssignedNegated(lAssump);
		if (isAssigned && isNegated)
		{
			if (IsAssignedDec(lAssump))
			{
				m_SelfContrOrGloballyUnsatAssump = lAssump;
				m_SelfContrOrGloballyUnsatAssumpSolveInv = m_Stat.m_SolveInvs;
			}
			else
			{
				m_LatestEarliestFalsifiedAssump = lAssump;
				m_LatestEarliestFalsifiedAssumpSolveInv = m_Stat.m_SolveInvs;
			}
			SetStatus(TToporStatus::STATUS_UNSAT, "Contradiction between assumptions");
			return;
		}
	}

	AssignAssumptions(firstUnassignedAssumpInd);
}

template <typename TLit, typename TUInd, bool Compress>
TToporReturnVal CTopi<TLit, TUInd, Compress>::Solve(const span<TLit> userAssumps, pair<double, bool> toInSecIsCpuTime, uint64_t confThr)
{
	if (m_DumpFile)
	{
		// cout << "\tc ot <TimeOut> <IsCpuTimeOut>" << endl;
		(*m_DumpFile) << "ot " << toInSecIsCpuTime.first << " " << (int)toInSecIsCpuTime.second << endl;
		// cout << "\tc oc <ConflictThreshold>" << endl;
		(*m_DumpFile) << "oc " << confThr << endl;
		DumpSpan(userAssumps, "s ", " 0");
	}

	assert((m_Stat.m_SolveInvs == 0) == (m_QueryCurr == TQueryType::QUERY_NONE));
	m_QueryCurr = m_QueryCurr == TQueryType::QUERY_NONE ? TQueryType::QUERY_INIT : confThr <= (uint64_t)m_ParamShortQueryConfThrInv ? TQueryType::QUERY_INC_SHORT : TQueryType::QUERY_INC_NORMAL;

	const bool restoreParamsOnExit = m_QueryCurr == TQueryType::QUERY_INC_SHORT && !m_ShortInvLifetimeParamVals.empty() && !IsUnrecoverable();
	CTopiParams* paramsToRestore(nullptr);

	TToporReturnVal trv = TToporReturnVal::RET_EXOTIC_ERROR;

	if (restoreParamsOnExit)
	{
		assert(m_QueryCurr == TQueryType::QUERY_INC_SHORT && !m_ShortInvLifetimeParamVals.empty());

		paramsToRestore = new CTopiParams(m_Params);
		if (unlikely(paramsToRestore == nullptr))
		{
			SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "Solve: paramsToRestore allocation failed");
			return trv = UnrecStatusToRetVal();
		}

		for (auto& pv : m_ShortInvLifetimeParamVals)
		{
			SetParam(pv.first, pv.second);
			if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();
		}
	}

	AssumpUnsatCoreCleanUpIfRequired();

	if (m_ParamAddClsAtLevel0 && m_DecLevel != 0)
	{
		Backtrack(0);
	}

	if (m_ParamVerbosity > 0)
	{
		if (!m_AxePrinted)
		{
			PrintAxe();
			cout << print_as_color<ansi_color_code::blue>("c Literal bit-width= ") << print_as_color<ansi_color_code::blue>(to_string(sizeof(TLit) << 3)) <<
				print_as_color < ansi_color_code::blue>("; Clause-index bit-width= ") << print_as_color<ansi_color_code::blue>(to_string(sizeof(TUInd) << 3)) <<
				print_as_color < ansi_color_code::blue>("; Compress = ") << print_as_color<ansi_color_code::blue>(Compress ? "1" : "0") << endl;
		}
		cout << m_Params.GetAllParamsCurrValues();
	}

	m_Stat.NewSolveInvocation(m_QueryCurr == CTopi<TLit, TUInd, Compress>::TQueryType::QUERY_INC_SHORT);
	if (IsCbLearntOrDrat() && m_Status == TToporStatus::STATUS_CONTRADICTORY)
	{
		vector<TULit> emptyCls;
		NewLearntClsApplyCbLearntDrat(emptyCls);
	}

	CApplyFuncOnExitFromScope<> textDratAddCommentOnExit(m_OpenedDratFile != nullptr && !m_IsDratBinary, [&]()
	{
		ofstream& o = *m_OpenedDratFile;
		if (!o.good())
		{
			OnBadDratFile();
			return;
		}
		o << "c query completed " << m_Stat.m_SolveInvs << "\n";
	});

	if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();

	m_IsSolveOngoing = true;

	CApplyFuncOnExitFromScope<> onExit([&]()
	{
		if (IsCbLearntOrDrat() && (m_Status == TToporStatus::STATUS_UNSAT || m_Status == TToporStatus::STATUS_CONTRADICTORY))
		{
			vector<TULit> emptyCls;
			NewLearntClsApplyCbLearntDrat(emptyCls);
		}

		// Clean-up the assumptions
		if (m_Assumps.cap() != 0)
		{
			for (TULit lAssump : m_Assumps.get_span_cap())
			{
				const TUVar vAssump = GetVar(lAssump);
				m_AssignmentInfo[vAssump].m_IsAssump = false;
			}

			m_Assumps.reserve_exactly(0);
		}

		m_EarliestFalsifiedAssump = BadULit;

		if (m_ParamVerbosity > 0) cout << m_Stat.StatStrShort();

		if (m_Stat.m_SolveInvs == m_ParamPrintDebugModelInvocation)
		{
			PrintDebugModel(trv);
		}

		if (m_DumpFile)
		{
			(*m_DumpFile) << "c " << trv << endl;
		}

		m_IsSolveOngoing = false;

		if (restoreParamsOnExit)
		{
			assert(paramsToRestore != nullptr);
			m_Params = move(*paramsToRestore);
			delete paramsToRestore;
		}

		if (m_QueryCurr == TQueryType::QUERY_INIT && m_ParamVarActivityUseMapleLevelBreakerAi != m_ParamVarActivityUseMapleLevelBreaker)
		{
			m_ParamVarActivityUseMapleLevelBreaker = m_ParamVarActivityUseMapleLevelBreakerAi;
		}

		if (m_QueryCurr == TQueryType::QUERY_INIT && m_ParamAddClsRemoveClssGloballySatByLitMinSizeAi != m_ParamAddClsRemoveClssGloballySatByLitMinSize)
		{
			m_ParamAddClsRemoveClssGloballySatByLitMinSize = m_ParamAddClsRemoveClssGloballySatByLitMinSizeAi;
		}

		if (m_QueryCurr == TQueryType::QUERY_INIT)
		{
			m_VsidsHeap.SetInitOrder(m_ParamVsidsInitOrderAi);
		}

		if (m_QueryCurr == TQueryType::QUERY_INIT && !m_AfterInitInvParamVals.empty())
		{
			for (auto& pv : m_AfterInitInvParamVals)
			{
				SetParam(pv.first, pv.second);
			}
		}

		m_QueryPrev = m_QueryCurr;

		if (m_ParamPhaseMngForceSolution && m_Status == TToporStatus::STATUS_SAT)
		{
			for (TUVar v = 1; v < GetNextVar(); ++v)
			{
				FixPolarityInternal(GetAssignedLitForVar(v));
			}
		}
	});

	auto SetStatusLocalTimeout = [&]() { SetStatus(TToporStatus::STATUS_UNDECIDED, (string)(toInSecIsCpuTime.second ? "CPU" : "Wall") + " timeout of " + to_string(toInSecIsCpuTime.first) + " for the current Solve invocation reached"); };
	auto SetStatusGlobalTimeout = [&]() { SetStatus(TToporStatus::STATUS_GLOBAL_TIMEOUT, "Global " + (string)(toInSecIsCpuTime.second ? "CPU" : "Wall") + " timeout of " + to_string(toInSecIsCpuTime.first) + " reached"); };

	if (toInSecIsCpuTime.first < 1e100)
	{
		if (toInSecIsCpuTime.first == 0)
		{
			SetStatusLocalTimeout();
			return trv = TToporReturnVal::RET_TIMEOUT_LOCAL;
		}
		toInSecIsCpuTime.second ? m_Stat.m_TimeSinceLastSolveStart.SetModeCpuTime() : m_Stat.m_TimeSinceLastSolveStart.SetModeWallTime();
		m_Stat.m_TimeSinceLastSolveStart.SetTimeout(toInSecIsCpuTime.first);
	}

	if (m_Stat.m_OverallTime.IsTimeoutSet() && m_Stat.m_OverallTime.IsTimeout())
	{
		// Global timeout
		SetStatusGlobalTimeout();
	}

	// Make sure to create an internal variable for any new variables amongst the assumptions (may increase m_Stat.m_MaxUserVar)
	for (const TLit userLit : userAssumps)
	{
		if (userLit != 0)
		{
			const auto userVar = ExternalLit2ExternalVar(userLit);
			HandleIncomingUserVar(userVar);
			if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();
		}
	}

	ReserveExactly(m_E2ILitMap, m_Stat.m_MaxUserVar + 1, "m_E2IVarMap in Solve");
	ReserveVarAndLitData(userAssumps.size() - (userAssumps.size() > 0 && userAssumps.back() == 0));
	ClsDeletionInit();
	RestartInit();
	DecisionInit();
	BacktrackingInit();
	
	if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();
	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(false));

	TContradictionInfo pi = BCP();
	if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();

	SetStatus(TToporStatus::STATUS_UNDECIDED);

	if (pi.IsContradiction())
	{
		if (m_DecLevel == 0)
		{
			SetStatus(TToporStatus::STATUS_CONTRADICTORY, "Global contradiction: discovered by BCP at decision level 0");
			return trv = TToporReturnVal::RET_UNSAT;
		}
		else
		{
			ConflictAnalysisLoop(pi);
			if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();

			if (m_EarliestFalsifiedAssump != BadULit)
			{
				SetStatus(TToporStatus::STATUS_UNSAT, "Falsified assumption discovered after the initial BCP");
				return trv = TToporReturnVal::RET_UNSAT;
			}
		}
	}

	// Handling the assumptions, if any
	HandleAssumptions(userAssumps);
	if (m_Status != TToporStatus::STATUS_UNDECIDED)
	{
		return trv = StatusToRetVal();
	}

	if (m_Stat.m_SolveInvs == m_ParamVerifyDebugModelInvocation)
	{
		VerifyDebugModel();
	}

	if (m_AssignedVarsNum == m_LastExistingVar)
	{
		// All the variables have been assigned -> a model!
		SetStatus(TToporStatus::STATUS_SAT, "A model: discovered by the initial BCP before the search");
		return trv = TToporReturnVal::RET_SAT;
	}

	const auto confThrAfterThisConfNumReached = confThr == numeric_limits<decltype(confThr)>::max() ? numeric_limits<decltype(m_Stat.m_Conflicts)>::max() : m_Stat.m_Conflicts + confThr;

	if (m_ParamVerbosity > 0) cout << m_Stat.StatStrShort();

	m_DecLevelOfLastAssignedAssumption = m_Assumps.cap() == 0 ? 0 : GetAssignedDecLevel(*GetAssignedLitsHighestDecLevelIt(m_Assumps.get_span_cap(), 0));

	if (m_ParamInitPolarityStrat != 1 && m_PrevAiCap < m_AssignmentInfo.cap())
	{
		auto aiSpan = m_AssignmentInfo.get_span_cap(m_PrevAiCap);
		for (TAssignmentInfo& currAi : aiSpan)
		{
			if (!currAi.m_IsAssigned)
			{
				currAi.m_IsNegated = m_ParamInitPolarityStrat == 0 ? true : rand() % 2;
			}
		}
		m_PrevAiCap = m_AssignmentInfo.cap();
	}

	// CDCL loop
	while (m_Status == TToporStatus::STATUS_UNDECIDED)
	{
		if (m_InterruptNow || (M_CbStopNow != nullptr && M_CbStopNow() == TStopTopor::VAL_STOP))
		{
			if (m_InterruptNow)
			{
				SetStatus(TToporStatus::STATUS_USER_INTERRUPT, "Interrupt by the Interrupt callback");
				m_InterruptNow = false;
			}
			else
			{
				SetStatus(TToporStatus::STATUS_USER_INTERRUPT, "Interrupt by the StopNow callback");
			}
		}
		if (unlikely(IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT)) return trv = StatusToRetVal();

		InprocessIfRequired();
		SimplifyIfRequired();
		DeleteClausesIfRequired();
		CompressBuffersIfRequired();

		if (unlikely(IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT)) return trv = StatusToRetVal();

		NewDecLevel();
		m_FlippedLit = BadULit;
		TULit l = Decide();
		assert(l != BadULit);
		assert(NV(2) || P("***** Decision at level " + to_string(m_DecLevel) + ": " + SLit(l) + "\n"));

		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TrailAssertConsistency());
		[[maybe_unused]] bool isContraditory = Assign(l, BadClsInd, BadULit, m_DecLevel);
		assert(!isContraditory);
		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TrailAssertConsistency());

		TContradictionInfo contradictionInfo = BCP();
		const bool isContradictionBeforeConflictAnalysis = contradictionInfo.IsContradiction();

		ConflictAnalysisLoop(contradictionInfo);
		if (unlikely(IsUnrecoverable())) return trv = StatusToRetVal();

		// The earliest falsified assumption might be assigned&satisfied in the corner case of a reimplication in BCP
		if (!contradictionInfo.IsContradiction() && (m_EarliestFalsifiedAssump == BadULit || !IsFalsified(m_EarliestFalsifiedAssump)) && m_DecLevel < m_DecLevelOfLastAssignedAssumption)
		{
			assert(NV(2) || P("The earliest falsified assumption is assigned&satisfied (corner case of a reimplication in BCP). The trail: " + STrail() + "\n"));
			m_EarliestFalsifiedAssump = m_FlippedLit = BadULit;
			// Trying to find a replacement (otherwise there would be a correctness bug)
			for (TULit l : m_Assumps.get_const_span_cap())
			{
				auto v = GetVar(l);
				assert(m_AssignmentInfo[v].m_IsAssump);
				if (IsAssigned(l) && IsAssumpFalsifiedGivenVar(v) &&
					(m_EarliestFalsifiedAssump == BadULit || GetAssignedDecLevel(l) <= GetAssignedDecLevel(m_EarliestFalsifiedAssump)))
				{
					m_LatestEarliestFalsifiedAssump = m_EarliestFalsifiedAssump = l;
					m_LatestEarliestFalsifiedAssumpSolveInv = m_Stat.m_SolveInvs;
					assert(NV(2) || P("Found a new earliest latest earliest falsified assumption: " + SLit(l)));
				}
			}

			if (m_EarliestFalsifiedAssump == BadULit)
			{
				HandleAssumptionsIfBacktrackedBeyondThem();
				m_DecLevelOfLastAssignedAssumption = m_Assumps.cap() == 0 ? 0 : GetDecLevel0ForUnassigned(*GetLitsHighestDecLevel0ForUnassignedIt(m_Assumps.get_span_cap(), 0));
			}
		}

		if (unlikely(IsUnrecoverable())) return trv = StatusToRetVal();

		if (m_EarliestFalsifiedAssump != BadULit)
		{
			assert(IsAssigned(m_EarliestFalsifiedAssump));
			if (unlikely(!IsAssigned(m_EarliestFalsifiedAssump)))
			{
				SetStatus(TToporStatus::STATUS_ASSUMPTION_REQUIRED_ERROR, "Internal error: earliest falsified assumption must be assigned");
			}
			else
			{
				SetStatus(TToporStatus::STATUS_UNSAT, "Assumption flipped!");
			}
		}
		else if (!contradictionInfo.IsContradiction() && m_AssignedVarsNum == m_LastExistingVar)
		{
			// All the variables have been assigned -> a model!
			SetStatus(TToporStatus::STATUS_SAT, "A model!");
		}		
		else if (m_Stat.m_OverallTime.IsTimeoutSet() && m_Stat.m_OverallTime.IsTimeout())
		{
			// Global timeout
			SetStatusGlobalTimeout();
		}
		else if (m_Stat.m_Conflicts >= confThrAfterThisConfNumReached)
		{
			// Local conflict threshold reached
			SetStatus(TToporStatus::STATUS_UNDECIDED, "Conflicts threshold of " + to_string(confThr) + " reached");
			return trv = TToporReturnVal::RET_CONFLICT_OUT;
		}
		else if (m_Stat.m_TimeSinceLastSolveStart.IsTimeoutSet() && m_Stat.m_TimeSinceLastSolveStart.IsTimeout())
		{
			SetStatusLocalTimeout();
			return trv = TToporReturnVal::RET_TIMEOUT_LOCAL;
		}

		if (m_Status == TToporStatus::STATUS_UNDECIDED && isContradictionBeforeConflictAnalysis && Restart())
		{
			Backtrack(m_DecLevelOfLastAssignedAssumption, false);
			if (M_GetNextUnitClause != nullptr)
			{
				const auto assignedVarsNumBefore = m_AssignedVarsNum;
				
				bool isContradictionWithAssumptions = false;
				for (TLit eLit = M_GetNextUnitClause(m_ThreadId, true); eLit; eLit = M_GetNextUnitClause(m_ThreadId, false))
				{
					TULit l = E2I(eLit);
					bool isContraditory = Assign(l, BadClsInd, BadULit, 0);
					if (isContraditory)
					{
						if (GetAssignedDecLevel(l) == 0)
						{
							assert(NV(1) || P("Global contradiction when assigning an external unit clause\n"));
							return TToporReturnVal::RET_UNSAT;
						}	
						else
						{
							isContradictionWithAssumptions = true;
							Backtrack(GetLitDecLevel(l) - 1);							
						}
					}					
				}

				if (isContradictionWithAssumptions)
				{
					assert(NV(1) || P("Contradiction with assumptions when assigning an external unit clause\n"));
					return TToporReturnVal::RET_UNSAT;
				}

				if (m_AssignedVarsNum > assignedVarsNumBefore)
				{
					TContradictionInfo contradictionInfo = BCP();
					if (contradictionInfo.IsContradiction())
					{
						assert(NV(1) || P("Contradiction when propagating external unit clauses\n"));
						return TToporReturnVal::RET_UNSAT;
					}
				}								
			}
		}
	}

	return trv = StatusToRetVal();
}

template <typename TLit, typename TUInd, bool Compress>
Topor::TToporLitVal CTopi<TLit, TUInd, Compress>::GetValue(TLit l) const
{
	// This is in order not to crash when the user asks for a value beyond m_MaxUserVar, 
	// which can legally happen (e.g., consider the input {4 0} {4 5 6 7 0})

	if (ExternalLit2ExternalVar(l) > m_Stat.m_MaxUserVar)
	{
		return TToporLitVal::VAL_DONT_CARE;
	}

	const TULit litInternal = E2I(l);
	assert(litInternal < GetNextLit());

	// litInternal can be BadULit if the external variable doesn't appear on any clauses
	return litInternal == BadULit ? TToporLitVal::VAL_DONT_CARE : !IsAssigned(litInternal) ? TToporLitVal::VAL_UNASSIGNED : IsFalsified(litInternal) ? TToporLitVal::VAL_UNSATISFIED : TToporLitVal::VAL_SATISFIED;
}

template <typename TLit, typename TUInd, bool Compress>
TLit CTopi<TLit, TUInd, Compress>::GetLitDecLevel(TLit l) const
{
	const TULit litInternal = E2I(l);
	assert(litInternal < GetNextLit());

	return GetAssignedDecLevel(litInternal);
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetSolveInvs() const
{
	return m_Stat.m_SolveInvs;
}

template <typename TLit, typename TUInd, bool Compress>
TLit CTopi<TLit, TUInd, Compress>::GetMaxUserVar() const
{
	return m_Stat.m_MaxUserVar;
}

template <typename TLit, typename TUInd, bool Compress>
TLit CTopi<TLit, TUInd, Compress>::GetMaxInternalVar() const
{
	return (TLit)m_Stat.m_MaxInternalVar;
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetActiveLongLearntClss() const
{
	return m_Stat.m_ActiveLongLearntClss;
}

template <typename TLit, typename TUInd, bool Compress>
std::string CTopi<TLit, TUInd, Compress>::GetStatStrShort(bool forcePrintingHead)
{
	return m_Stat.StatStrShort(forcePrintingHead);
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetConflictsNumber() const
{
	return (uint64_t)m_Stat.m_Conflicts;
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetActiveClss() const
{
	return (uint64_t)m_Stat.GetActiveClss();
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetBacktracks() const
{
	return m_Stat.m_Backtracks;
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetPropagations() const
{
	return m_Stat.m_Implications;
}

template <typename TLit, typename TUInd, bool Compress>
uint64_t CTopi<TLit, TUInd, Compress>::GetAssumpReuseBacktrackLevelsSaved() const
{
	return m_Stat.m_AssumpReuseBacktrackLevelsSaved;
}

template <typename TLit, typename TUInd, bool Compress>
vector<Topor::TToporLitVal> CTopi<TLit, TUInd, Compress>::GetModel() const
{
	vector<Topor::TToporLitVal> m(m_E2ILitMap.cap());
	for (TLit v = 1; v < (TLit)m.size(); ++v)
	{
		m[v] = GetValue(v);
	}
	return m;
}

template <typename TLit, typename TUInd, bool Compress>
Topor::TToporStatistics<TLit, TUInd> CTopi<TLit, TUInd, Compress>::GetStatistics() const
{
	return m_Stat;
}

template <typename TLit, typename TUInd, bool Compress>
string CTopi<TLit, TUInd, Compress>::GetParamsDescr() const
{
	return m_Params.GetAllParamsDescr();
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::SetOverallTimeoutIfAny()
{
	if (m_ParamOverallTimeout != numeric_limits<double>::max())
	{
		m_ParamOverallTimeoutIsCpu ? m_Stat.m_OverallTime.SetModeCpuTime() : m_Stat.m_OverallTime.SetModeWallTime();
		m_Stat.m_OverallTime.SetTimeout(m_ParamOverallTimeout);
	}
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::PrintAxe()
{
	cout << "\
c Intel(R) SAT Solver by Alexander Nadel\n\
c          A\n\
c         /!\\\n\
c        / ! \\\n\
c  /\\    )___(\n\
c ( `.____(_)_________\n\
c |          __..--\"\"\n\
c (      _.-|\n\
c  \\   ,' | |\n\
c   \\ /   | |\n\
c    \\(   | |\n\
c     `   | |\n\
c         | |\n\
c         | |\n\
c         | |\n";
	m_AxePrinted = true;
}

template <typename TLit, typename TUInd, bool Compress>
CTopi<TLit, TUInd, Compress>::TUV CTopi<TLit, TUInd, Compress>::GetDecLevelWithBestScore(CTopi::TUV dlLowestIncl, CTopi::TUV dlHighestExcl)
{
	assert(m_CurrCustomBtStrat > 0);

	if (dlHighestExcl <= dlLowestIncl + 1)
	{
		return dlHighestExcl - 1;
	}

	const auto currDlSpan = m_BestScorePerDecLevel.get_const_span_cap(dlLowestIncl, dlHighestExcl - dlLowestIncl);

	TUV maxElemI = 0;
	double maxElem = 0.;
	for (TUV i = 0; i < (TUV)currDlSpan.size(); ++i)
	{
		const auto currDecLevel = dlLowestIncl + i;

		if (DecLevelIsCollapsed(currDecLevel))
		{
			continue;
		}
		const double currElem = currDlSpan[i];
		if (currElem > maxElem || (m_CurrCustomBtStrat == 1 && currElem == maxElem))
		{
			maxElem = currElem;
			maxElemI = i;
		}
		assert(CalcMaxDecLevelScore(currDecLevel) == m_BestScorePerDecLevel[currDecLevel]);
	}

	return dlLowestIncl + maxElemI;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::NewDecLevel()
{
	++m_DecLevel;
	if (m_RstNumericLocalConfsSinceRestartAtDecLevelCreation.cap() != 0)
	{
		m_RstNumericLocalConfsSinceRestartAtDecLevelCreation[m_DecLevel] = m_ConfsSinceRestart;
	}
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::SetMultipliers()
{
	m_B.SetMultiplier(m_ParamMultClss);
	m_AssignmentInfo.SetMultiplier(m_ParamMultVars);
	m_VarInfo.SetMultiplier(m_ParamMultVars);
	m_E2ILitMap.SetMultiplier(m_ParamMultVars);
	if (UseI2ELitMap()) m_I2ELitMap.SetMultiplier(m_ParamMultVars);
	m_Watches.SetMultiplier(m_ParamMultVars);
	m_HandleNewUserCls.SetMultiplier(m_ParamMultVars);
	m_VsidsHeap.set_multiplier(m_ParamMultVars);
	m_W.SetMultiplier(m_ParamMultWatches);
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::MarkWatchBufferChunkDeletedOrByLiteral(TUInd wlbInd, TUInd allocatedEntries, TULit l)
{
	// Mark the moved region as deleted in the following format:
	// [log_2(allocated-entries) 0] or as marked by a literal if, l != 0
	// The latter mode is relevant during compression
	m_W[wlbInd] = countr_zero(allocatedEntries);
	m_W[wlbInd + 1] = l;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::MarkWatchBufferChunkDeleted(TWatchInfo& wi)
{
	assert(has_single_bit(wi.m_AllocatedEntries));
	assert(wi.m_AllocatedEntries >= 2);
	MarkWatchBufferChunkDeletedOrByLiteral(wi.m_WBInd, wi.m_AllocatedEntries);
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::IsVisitedConsistent() const
{
	for (TUVar v = 1; v < GetNextVar(); ++v)
	{
		const bool isVisitedFlag = m_AssignmentInfo[v].m_Visit;
		const bool isInVisitedVec = find(m_VisitedVars.get_const_span().begin(), m_VisitedVars.get_const_span().end(), v) != m_VisitedVars.get_const_span().end();
		assert(isVisitedFlag == isInVisitedVec);
		if (isVisitedFlag != isInVisitedVec)
		{
			return false;
		}
	}
	return true;
}

template <typename TLit, typename TUInd, bool Compress>
double CTopi<TLit, TUInd, Compress>::CalcMaxDecLevelScore(TUV dl) const
{
	double bestScore = 0;
	for (TUVar v = dl == 0 ? m_TrailStart : GetDecVar(dl); v != BadUVar && GetAssignedDecLevelVar(v) == dl; v = m_VarInfo[v].m_TrailNext)
	{
		const double currScore = m_VsidsHeap.get_var_score(v);
		if (currScore > bestScore)
		{
			bestScore = currScore;
		}
	}
	return bestScore;
}

template <typename TLit, typename TUInd, bool Compress>
double CTopi<TLit, TUInd, Compress>::CalcMinDecLevelScore(TUV dl) const
{
	double bestScore = numeric_limits<double>::max();
	for (TUVar v = dl == 0 ? m_TrailStart : GetDecVar(dl); v != BadUVar && GetAssignedDecLevelVar(v) == dl; v = m_VarInfo[v].m_TrailNext)
	{
		const double currScore = m_VsidsHeap.get_var_score(v);
		if (currScore < bestScore)
		{
			bestScore = currScore;
		}
	}
	return bestScore;
}

template <typename TLit, typename TUInd, bool Compress>
string Topor::CTopi<TLit, TUInd, Compress>::GetMemoryLayout() const
{
	if (!m_ParamPrintMemoryProfiling)
	{
		return "";
	}

	unordered_map<string, size_t> name2Mb;

	name2Mb["m_BC"] = 0;
	for (auto& i : m_BC)
	{
		name2Mb["m_BC"] += i.second.memMb();
	}

	name2Mb["m_E2ILitMap"] = m_E2ILitMap.memMb();
	name2Mb["m_I2ELitMap"] = m_I2ELitMap.memMb();
	name2Mb["m_B"] = m_B.memMb();	
	name2Mb["m_W"] = m_W.memMb();
	name2Mb["m_Watches"] = m_Watches.memMb();
	name2Mb["m_TrailLastVarPerDecLevel"] = m_TrailLastVarPerDecLevel.memMb();
	name2Mb["m_BestScorePerDecLevel"] = m_BestScorePerDecLevel.memMb();
	name2Mb["m_AssignmentInfo"] = m_AssignmentInfo.memMb();
	name2Mb["m_VarInfo"] = m_VarInfo.memMb();
	name2Mb["m_PolarityInfo"] = m_PolarityInfo.memMb();
	name2Mb["m_Assumps"] = m_Assumps.memMb();
	name2Mb["m_HugeCounterPerDecLevel"] = m_HugeCounterPerDecLevel.memMb();
	name2Mb["m_DecLevelsLastAppearenceCounter"] = m_DecLevelsLastAppearenceCounter.memMb();
	name2Mb["m_CurrClsCounters"] = m_CurrClsCounters.memMb();
	name2Mb["m_RstNumericLocalConfsSinceRestartAtDecLevelCreation"] = m_RstNumericLocalConfsSinceRestartAtDecLevelCreation.memMb();
	name2Mb["m_HandleNewUserCls"] = m_HandleNewUserCls.memMb();
	
	name2Mb["m_NewExternalVarsAddUserCls"] = m_NewExternalVarsAddUserCls.memMb();
	name2Mb["m_ToPropagate"] = m_ToPropagate.memMb();
	name2Mb["m_Cis"] = m_Cis.memMb();
	name2Mb["m_Dis"] = m_Dis.memMb();
	name2Mb["m_VarsParentSubsumed"] = memMb(m_VarsParentSubsumed);
	name2Mb["m_HandyLitsClearBefore"] = m_HandyLitsClearBefore[0].memMb() + m_HandyLitsClearBefore[1].memMb();
	name2Mb["m_VisitedVars"] = m_VisitedVars.memMb();
	name2Mb["m_RootedVars"] = m_RootedVars.memMb();
	name2Mb["m_UserCls"] = m_UserCls.memMb();

	name2Mb["m_VsidsHeap"] = m_VsidsHeap.memMb();
	name2Mb["m_TmpClss"] = accumulate(m_TmpClss.begin(), m_TmpClss.end(), (size_t)0, [&](size_t sum, auto& it)
	{
		return sum + it.memMb();
	});
	name2Mb["m_TmpClssDebug"] = accumulate(m_TmpClssDebug.begin(), m_TmpClssDebug.end(), (size_t)0, [&](size_t sum, auto& it)
	{
		return sum + it.memMb();
	});
	
	size_t overallSzInMb = 0;
	vector<pair<string, size_t>> name2MbVec(name2Mb.size());
	transform(name2Mb.begin(), name2Mb.end(), name2MbVec.begin(), [&](auto ssPair) { overallSzInMb += ssPair.second;  return ssPair; });
	sort(name2MbVec.begin(), name2MbVec.end(), [&](auto& i1, auto& i2) { return i1.second > i2.second; });

	stringstream ss;
	ss << "c MEMORY -- all : " << overallSzInMb << " *** ";
	for (auto& nm: name2MbVec)
	{
		ss << nm.first << " : " << nm.second << "; ";
	}
	return ss.str();
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::SetParallelData(unsigned threadId, std::function<void(unsigned threadId, int lit)> ReportUnitClause, std::function<int(unsigned threadId, bool reinit)> GetNextUnitClause)
{
	m_ThreadId = threadId;
	M_ReportUnitCls = ReportUnitClause;
	M_GetNextUnitClause = GetNextUnitClause;
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;

/*
* Future
*/

// #topor: conflict clause analysis: 1) vivification: see Kissat paper; 2) replace glue with 2glue?

