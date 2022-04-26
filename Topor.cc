// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topor.hpp"
#include "Topi.hpp"
#include <fstream> 

using namespace Topor;
using namespace std;

CTopor::CTopor(TLit varsNumHint) : m_Topi(new CTopi(varsNumHint)) {}

CTopor::~CTopor()
{
	delete m_Topi;
	m_Topi = nullptr;
}

void CTopor::AddClause(const span<TLit> c)
{
	m_Topi->AddUserClause(c);
}

TToporReturnVal CTopor::Solve(const span<TLit> assumps, pair<double, bool> toInSecIsCpuTime, uint64_t confThr)
{
	return m_Topi->Solve(assumps, toInSecIsCpuTime, confThr);
}

bool CTopor::IsAssumptionRequired(size_t assumpInd)
{
	return m_Topi->IsAssumptionRequired(assumpInd);
}

void CTopor::BoostScore(TLit v, double value)
{
	m_Topi->BoostScore(v, value);
}

void CTopor::FixPolarity(TLit l, bool onlyOnce)
{
	m_Topi->FixPolarity(l, onlyOnce);
}

void CTopor::ClearUserPolarityInfo(TLit v)
{
	m_Topi->ClearUserPolarityInfo(v);
}

void CTopor::SetParam(const string& paramName, double newVal)
{
	m_Topi->SetParam(paramName, newVal);
}

Topor::TToporLitVal CTopor::GetLitValue(TLit l) const
{
	return m_Topi->GetValue(l);
}

std::vector<TToporLitVal> CTopor::GetModel() const
{
	return m_Topi->GetModel();
}

Topor::TToporStatistics CTopor::GetStatistics() const
{
	return m_Topi->GetStatistics();
}

bool CTopor::IsError() const
{
	return m_Topi->IsError();
}

std::string CTopor::GetStatusExplanation() const
{
	return m_Topi->GetStatusExplanation();
}

std::string CTopor::GetParamsDescr() const
{
	return m_Topi->GetParamsDescr();
}

void CTopor::DumpDrat(ofstream& openedDratFile, bool isDratBinary)
{
	m_Topi->DumpDrat(openedDratFile, isDratBinary);
}

void CTopor::SetCbStopNow(TCbStopNow CbStopNow)
{
	m_Topi->SetCbStopNow(CbStopNow);
}

void CTopor::InterruptNow()
{
	m_Topi->InterruptNow();
}

void CTopor::SetCbNewLearntCls(TCbNewLearntCls CbNewLearntCls)
{
	m_Topi->SetCbNewLearntCls(CbNewLearntCls);
}

void CTopor::Backtrack(TLit decLevel)
{
	m_Topi->Backtrack(decLevel, false, false, true);
}

namespace Topor
{
	std::ostream& operator << (std::ostream& os, const TToporReturnVal& trv)
	{
		switch (trv)
		{
		case TToporReturnVal::RET_SAT: os << "SAT"; break;
		case TToporReturnVal::RET_UNSAT: os << "UNSAT"; break;
		case TToporReturnVal::RET_TIMEOUT_LOCAL: os << "TIMEOUT_LOCAL"; break;
		case TToporReturnVal::RET_CONFLICT_OUT: os << "CONFLICT_OUT"; break;
		case TToporReturnVal::RET_MEM_OUT: os << "MEM_OUT"; break;
		case TToporReturnVal::RET_USER_INTERRUPT: os << "USER_INTERRUPT"; break;
		case TToporReturnVal::RET_INDEX_TOO_NARROW: os << "INDEX_TOO_NARROW"; break;
		case TToporReturnVal::RET_PARAM_ERROR: os << "PARAM_ERROR"; break;
		case TToporReturnVal::RET_ASSUMPTION_REQUIRED_ERROR: os << "ASSUMPTION_REQUIRED_ERROR"; break;
		case TToporReturnVal::RET_TIMEOUT_GLOBAL: os << "TIMEOUT_GLOBAL"; break;
		case TToporReturnVal::RET_DRAT_FILE_PROBLEM: os << "DRAT_FILE_PROBLEM"; break;
		case TToporReturnVal::RET_EXOTIC_ERROR: os << "EXOTIC_ERROR"; break;
		default: os << "TToporReturnVal : undefined value!";
		}
		return os;
	}
}