// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#include <functional>
#include <string>
#include <bit>
#include <iterator> 
#include <any>
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

void CTopi::DumpSetUp(const char* filePrefix)
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

CTopi::CTopi(TLit varNumHint) : m_InitVarNumAlloc(varNumHint <= 0 ? InitEntriesInB : (size_t)varNumHint + 1), m_E2ILitMap(m_InitVarNumAlloc, (size_t)0),
m_HandleNewUserCls(m_InitVarNumAlloc), m_Watches(GetInitLitNumAlloc(), (size_t)0), m_TrailLastVarPerDecLevel(1, BadClsInd),
m_VarInfo(m_InitVarNumAlloc, (size_t)0), m_Stat(m_B.cap(), m_BNext, m_ParamVarActivityInc), m_VsidsHeap(m_Stat.m_VarActivityInc)
{
	m_AssignmentInfo.reserve_exactly(m_InitVarNumAlloc, (size_t)0);

	static bool diamondInvokedTopor = false;
	DIAMOND("topor", diamondInvokedTopor);

	const char* fileName = getenv("TOPOR_DUMP_NAME");
	if (fileName != NULL)
	{
		DumpSetUp(fileName);
		assert(m_DumpFile);
		if (m_DumpFile) (*m_DumpFile) << "p cnf " << varNumHint << " " << 0 << endl;
	}

	if (m_Params.IsError())
	{
		SetStatus(TToporStatus::STATUS_PARAM_ERROR, m_Params.GetErrorDescr());
	} else if (m_B.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::CTopi: couldn't allocate the main buffer");
	} else if (m_InitVarNumAlloc != 0 && m_E2ILitMap.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::CTopi: couldn't allocate m_E2IVarMap");
	}
	else if (GetInitLitNumAlloc() != 0 && m_Watches.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::CTopi: couldn't allocate m_Watches");
	}
	else if (m_InitVarNumAlloc != 0 && m_AssignmentInfo.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::CTopi: couldn't allocate m_AssignmentInfo");
	}
	else if (m_InitVarNumAlloc != 0 && m_VarInfo.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::CTopi: couldn't allocate m_VarInfo");
	}
	else if (m_TrailLastVarPerDecLevel.uninitialized_or_erroneous())
	{
		SetStatus(TToporStatus::STATUS_ALLOC_FAILED, "CTopi::CTopi: couldn't allocate m_TrailLastVarPerDecLevel");
	}	

	SetMultipliers();	

	// m_DebugModel = { false, true, false, true, false, false, false, true, false, false, false, false, true, false, true, false, false, true, true, true, true };
}

void CTopi::SetParam(const string& paramName, double newVal)
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

bool CTopi::IsError() const
{
	return IsErroneous();
}

size_t CTopi::GetInitLitNumAlloc() const
{
	assert(m_InitVarNumAlloc <= rotr((size_t)1, 1));
	const size_t initLitNumAlloc = m_InitVarNumAlloc << 1;
	return initLitNumAlloc == 0 ? numeric_limits<size_t>::max() : initLitNumAlloc;
}


TToporReturnVal CTopi::StatusToRetVal() const
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

TToporReturnVal CTopi::UnrecStatusToRetVal() const
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

void CTopi::HandleIncomingUserVar(TLit v, bool isUndoable)
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

void CTopi::DumpSpan(const span<TLit> c, const string& prefix, const string& suffix, bool addNewLine)
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

void CTopi::AddUserClause(const span<TLit> c)
{
	if (m_DumpFile) DumpSpan(c, "", " 0");

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

	auto clsStart = AddClsToBufferAndWatch(cls, false);
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

bool CTopi::IsAssumptionRequired(size_t assumpInd)
{
	if (m_Stat.m_SolveInvs == 0 || m_Status != TToporStatus::STATUS_UNSAT || assumpInd >= m_UserAssumps.size())
	{
		SetStatus(TToporStatus::STATUS_ASSUMPTION_REQUIRED_ERROR, m_Stat.m_SolveInvs == 0 ? "No Solve invocations so far" : m_Status != TToporStatus::STATUS_UNSAT ? "The latest Solve didn't return TToporStatus::STATUS_UNSAT" : "The assumption ID is beyond the number of assumptions in the latest Solve invocation");
		return false;
	}

	if (m_LatestAssumpUnsatCoreSolveInvocation != m_Stat.m_SolveInvs)
	{
		assert(m_SelfContrOrGloballyUnsatAssumpSolveInv == m_Stat.m_SolveInvs || m_LatestEarliestFalsifiedAssumpSolveInv == m_Stat.m_SolveInvs);
		assert(m_SelfContrOrGloballyUnsatAssumpSolveInv != m_LatestEarliestFalsifiedAssumpSolveInv);

		if (m_LatestEarliestFalsifiedAssumpSolveInv == m_Stat.m_SolveInvs && !IsAssigned(m_LatestEarliestFalsifiedAssump))
		{
			NewDecLevel();

			[[maybe_unused]] bool isContraditory = Assign(m_LatestEarliestFalsifiedAssump, BadULit, BadULit, m_DecLevel);
			assert(!isContraditory);

			TContradictionInfo contradictionInfo = BCP();
			assert(contradictionInfo.IsContradiction());
			m_Status = TToporStatus::STATUS_UNDECIDED;
			ConflictAnalysisLoop(contradictionInfo, false, true);
			if (unlikely(IsUnrecoverable())) return false;
			assert(IsAssignedNegated(m_LatestEarliestFalsifiedAssump));
			m_Status = TToporStatus::STATUS_UNSAT;
		}

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

	return m_UserAssumps[assumpInd] != 0 && IsVisited(E2I(m_UserAssumps[assumpInd]));
}

void CTopi::HandleAssumptions(const span<TLit> userAssumps)
{
	m_UserAssumps = userAssumps;

	m_DecLevelOfLastAssignedAssumption = 0;

	if (userAssumps.empty() || userAssumps[0] == 0)
	{
		return;
	}

	ReserveExactly(m_Assumps, userAssumps.size(), "m_Assumps in HandleAssumptions");
	if (unlikely(IsUnrecoverable())) return;

	{
		span<TULit> assumpsSpan = m_Assumps.get_span_cap();

		transform(userAssumps.begin(), userAssumps.end(), assumpsSpan.begin(), [&](TLit userLit) {
			return E2I(userLit);
		});
	}
	
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
				m_SelfContrOrGloballyUnsatAssumpSolveInv = m_Stat.m_SolveInvs;
				SetStatus(TToporStatus::STATUS_UNSAT, "An assumption is falsified at decision level 0");
				return;
			}
			else
			{
				RemoveAssump();
			}
		} else if (unlikely(ai.m_IsAssump))
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

	// Assign all the remaining assumptions
	if (m_ParamAssumpsConflictStrat == 0)
	{
		if (firstUnassignedAssumpInd != m_Assumps.cap())
		{
			span<TULit> potentiallyUnassignedAssumpsSpan = m_Assumps.get_span_cap(firstUnassignedAssumpInd);
			for (size_t assumpLitI = 0; assumpLitI < potentiallyUnassignedAssumpsSpan.size(); ++assumpLitI)
			{
				const TULit assumpLit = potentiallyUnassignedAssumpsSpan[assumpLitI];
				if (!IsAssigned(assumpLit))
				{
					NewDecLevel();

					[[maybe_unused]] bool isContraditory = Assign(assumpLit, BadULit, BadULit, m_DecLevel);
					assert(!isContraditory);

					TContradictionInfo contradictionInfo = BCP();

					if (!contradictionInfo.IsContradiction() && m_EarliestFalsifiedAssump != BadULit)
					{
						m_LatestEarliestFalsifiedAssump = m_EarliestFalsifiedAssump;
						m_LatestEarliestFalsifiedAssumpSolveInv = m_Stat.m_SolveInvs;
						SetStatus(TToporStatus::STATUS_UNSAT, "Falsified assumption discovered after setting and propagating an assumption at decision level " + to_string(m_DecLevel));
						return;
					}

					if (contradictionInfo.IsContradiction())
					{
						m_LatestEarliestFalsifiedAssump = assumpLit;
						m_LatestEarliestFalsifiedAssumpSolveInv = m_Stat.m_SolveInvs;

						const span<TULit> contradictingCls = CiGetSpan(contradictionInfo);
						auto maxDecLevelInContradictingCls = max(GetAssignedDecLevel(contradictingCls[0]), GetAssignedDecLevel(contradictingCls[1]));
						if (unlikely(maxDecLevelInContradictingCls == 0))
						{
							// Global contradiction!
							SetStatus(TToporStatus::STATUS_CONTRADICTORY, "Global contradiction!");
							return;
						}

						SetStatus(TToporStatus::STATUS_UNSAT, "Contradiction discovered after setting and propagating an assumption at decision level " + to_string(m_DecLevel));
						// The case of one literal of the highest decision level is taken care of in BCP
						assert(GetAssignedDecLevel(contradictingCls[0]) == GetAssignedDecLevel(contradictingCls[1]));
						Backtrack(maxDecLevelInContradictingCls - 1);
						return;
					}
				}
				else
				{
					assert(!IsFalsified(assumpLit));
				}
			}
			assert(all_of(potentiallyUnassignedAssumpsSpan.begin(), potentiallyUnassignedAssumpsSpan.end(), [&](TULit l) { return IsAssigned(l); }));
		}
	} else
	{
		assert(m_ParamAssumpsConflictStrat == 1);
		m_DecLevelOfLastAssignedAssumption = m_DecLevel;
		if (firstUnassignedAssumpInd != m_Assumps.cap())
		{
			span<TULit> potentiallyUnassignedAssumpsSpan = m_Assumps.get_span_cap(firstUnassignedAssumpInd);
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
							SetStatus(TToporStatus::STATUS_UNSAT, "Falsified assumption discovered after setting and propagating an assumption at decision level " + to_string(m_DecLevel));
							return;
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
}

TToporReturnVal CTopi::Solve(const span<TLit> userAssumps, pair<double, bool> toInSecIsCpuTime, uint64_t confThr)
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
	
	const bool restoreParamsOnExit = m_QueryCurr == TQueryType::QUERY_INC_SHORT && !m_ShortInvLifetimeParamVals.empty();
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
		}	
		cout << m_Params.GetAllParamsCurrValues();
	}

	if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();

	m_IsSolveOngoing = true;

	m_Stat.NewSolveInvocation(m_QueryCurr == CTopi::TQueryType::QUERY_INC_SHORT);

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
	ReserveVarAndLitData();
	ClsDeletionInit();
	RestartInit();	
	DecisionInit();	
	BacktrackingInit();
	
	if (unlikely(IsUnrecoverable())) return trv = UnrecStatusToRetVal();

	assert(m_ParamAssertConsistency < 2 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || WLAssertConsistency(false));

	if (UseI2ELitMap())
	{		
		for (TLit externalVar = 1; externalVar <= m_Stat.m_MaxUserVar; ++externalVar)
		{
			assert(externalVar < (TLit)m_E2ILitMap.cap() && GetVar(m_E2ILitMap[externalVar]) < m_I2ELitMap.cap());
			const auto iLit = m_E2ILitMap[externalVar];
			m_I2ELitMap[GetVar(iLit)] = IsAssignedNegated(iLit) ? -externalVar : externalVar;
		}		
	}

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
		
		SimplifyIfRequired();
		DeleteClausesIfRequired();		
		CompressBuffersIfRequired();

		if (unlikely(IsUnrecoverable() || m_Status == TToporStatus::STATUS_USER_INTERRUPT)) return trv = StatusToRetVal();

		NewDecLevel();
		m_FlippedLit = BadULit;
		TULit l = Decide();
		assert(l != BadULit);
		assert(NV(2) || P("***** Decision at level " + to_string(m_DecLevel) + ": " + SLit(l) + "\n"));

		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());
		[[maybe_unused]] bool isContraditory = Assign(l, BadClsInd, BadULit, m_DecLevel);
		assert(!isContraditory);
		assert(m_ParamAssertConsistency < 1 || m_Stat.m_Conflicts < (uint64_t)m_ParamAssertConsistencyStartConf || TraiAssertConsistency());

		TContradictionInfo contradictionInfo = BCP();		
		const bool isContradictionBeforeConflictAnalysis = contradictionInfo.IsContradiction();
		
		ConflictAnalysisLoop(contradictionInfo, m_ParamReuseTrail);

		if (unlikely(IsUnrecoverable())) return trv = StatusToRetVal();

		if (m_EarliestFalsifiedAssump != BadULit)
		{
			SetStatus(TToporStatus::STATUS_UNSAT, "Assumption flipped!");			
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
		} else if (m_Stat.m_Conflicts >= confThrAfterThisConfNumReached)
		{
			// Local conflict threshold reached
			SetStatus(TToporStatus::STATUS_UNDECIDED, "Conflicts threshold of " + to_string(confThr) + " reached");
			return trv = TToporReturnVal::RET_CONFLICT_OUT;
		} else if (m_Stat.m_TimeSinceLastSolveStart.IsTimeoutSet() && m_Stat.m_TimeSinceLastSolveStart.IsTimeout())
		{
			SetStatusLocalTimeout();
			return trv = TToporReturnVal::RET_TIMEOUT_LOCAL;
		}

		if (m_Status == TToporStatus::STATUS_UNDECIDED && isContradictionBeforeConflictAnalysis && Restart())
		{
			Backtrack(m_DecLevelOfLastAssignedAssumption, false, m_ParamReuseTrail);
		}
	}
		
	return trv = StatusToRetVal();
}

Topor::TToporLitVal CTopi::GetValue(TLit l) const
{	
	const TULit litInternal = E2I(l);
	assert(litInternal < GetNextLit());

	// litInternal can be BadULit if the external variable doesn't appear on any clauses
	return litInternal == BadULit ? TToporLitVal::VAL_DONT_CARE : !IsAssigned(litInternal) ? TToporLitVal::VAL_UNASSIGNED : IsFalsified(litInternal) ? TToporLitVal::VAL_UNSATISFIED : TToporLitVal::VAL_SATISFIED;
}

vector<Topor::TToporLitVal> CTopi::GetModel() const
{
	vector<Topor::TToporLitVal> m(m_E2ILitMap.cap());
	for (TLit v = 1; v < (TLit)m.size(); ++v)
	{
		m[v] = GetValue(v);
	}
	return m;
}

Topor::TToporStatistics CTopi::GetStatistics() const
{
	return m_Stat;
}

string CTopi::GetParamsDescr() const
{
	return m_Params.GetAllParamsDescr();
}

void CTopi::SetOverallTimeoutIfAny()
{
	if (m_ParamOverallTimeout != numeric_limits<double>::max())
	{
		m_ParamOverallTimeoutIsCpu ? m_Stat.m_OverallTime.SetModeCpuTime() : m_Stat.m_OverallTime.SetModeWallTime();
		m_Stat.m_OverallTime.SetTimeout(m_ParamOverallTimeout);
	}
}

void CTopi::PrintAxe()
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

Topor::TUV CTopi::GetDecLevelWithBestScore(TUV dlLowestIncl, TUV dlHighestExcl)
{
	assert(m_ParamCustomBtStrat > 0);

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
		if (currElem > maxElem || (m_ParamCustomBtStrat == 1 && currElem == maxElem))
		{
			maxElem = currElem;
			maxElemI = i;
		}		
		assert(CalcMaxDecLevelScore(currDecLevel) == m_BestScorePerDecLevel[currDecLevel]);
	}

	return dlLowestIncl + maxElemI;
}

void CTopi::NewDecLevel()
{
	++m_DecLevel;
	if (m_RstNumericLocalConfsSinceRestartAtDecLevelCreation.cap() != 0)
	{
		m_RstNumericLocalConfsSinceRestartAtDecLevelCreation[m_DecLevel] = m_ConfsSinceRestart;
	}
}

void CTopi::SetMultipliers()
{
	m_B.SetMultiplier(m_ParamMultClss);
	m_AssignmentInfo.SetMultiplier(m_ParamMultVars);
	m_VarInfo.SetMultiplier(m_ParamMultVars);
	m_E2ILitMap.SetMultiplier(m_ParamMultVars);
	m_Watches.SetMultiplier(m_ParamMultVars);
	m_HandleNewUserCls.SetMultiplier(m_ParamMultVars);
	m_VsidsHeap.set_multiplier(m_ParamMultVars);
	m_W.SetMultiplier(m_ParamMultWatches);
}

void CTopi::MarkWatchBufferChunkDeletedOrByLiteral(TUInd wlbInd, TUInd allocatedEntries, TULit l)
{
	// Mark the moved region as deleted in the following format:
	// [log_2(allocated-entries) 0] or as marked by a literal if, l != 0
	// The latter mode is relevant during compression
	m_W[wlbInd] = countr_zero(allocatedEntries);
	m_W[wlbInd + 1] = l;
}

void CTopi::MarkWatchBufferChunkDeleted(TWatchInfo& wi)
{	
	assert(has_single_bit(wi.m_AllocatedEntries));
	assert(wi.m_AllocatedEntries >= 2);
	MarkWatchBufferChunkDeletedOrByLiteral(wi.m_WBInd, wi.m_AllocatedEntries);
}

bool CTopi::IsVisitedConsistent() const
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

double CTopi::CalcMaxDecLevelScore(TUV dl) const
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

double CTopi::CalcMinDecLevelScore(TUV dl) const
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

/*
* Future
*/

// #topor: conflict clause analysis: 1) vivification: see Kissat paper; 2) replace glue with 2glue?
