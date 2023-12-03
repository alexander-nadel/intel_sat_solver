// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#include "Topi.hpp"

using namespace std;
using namespace Topor;

template <typename TLit, typename TUInd, bool Compress>
string CTopi<TLit, TUInd, Compress>::SVar(TUVar v) const
{
	stringstream ss;

	const string sv = to_string(v);

	ss << sv << setprecision(10);

	if (IsAssignedVar(v))
	{
		ss << "@" << m_VarInfo[v].m_DecLevel;
		[[maybe_unused]] const auto s = m_VsidsHeap.get_var_score(v);
		/*if (s != 0)
		{
			ss << "=" << s;
		}*/
	}

	return ss.str();
}

template <typename TLit, typename TUInd, bool Compress>
std::string CTopi<TLit, TUInd, Compress>::SLit(TULit l) const
{
	auto s = IsNeg(l) ? "-" + SVar(GetVar(l)) : SVar(GetVar(l));
	if (IsAssigned(l))
	{
		s += "[" + (string)(IsSatisfied(l) ? "S" : "U") + "]";
	}
	return s;
}

template <typename TLit, typename TUInd, bool Compress>
string CTopi<TLit, TUInd, Compress>::SE2I()
{
	stringstream ss;

	ss << "E2I:\n";

	for (TLit externalLit = 1; externalLit < (TLit)m_E2ILitMap.cap(); ++externalLit)
	{
		ss << "\t" << externalLit << " : " << SLit(m_E2ILitMap[externalLit]) << "; ";
	}

	ss << "\n";

	return ss.str();
}

template <typename TLit, typename TUInd, bool Compress>
string CTopi<TLit, TUInd, Compress>::STrail()
{
	stringstream ss;

	ss << "Current trail (reversed):\n";

	for (TUVar v = m_TrailEnd; v != BadUVar; v = m_VarInfo[v].m_TrailPrev)
	{
		TULit l = GetAssignedLitForVar(v);
		ss << "\t";

		[[maybe_unused]] const TUV vDecLevel = GetAssignedDecLevelVar(v);
		[[maybe_unused]] const TUV prevDecLevel = m_VarInfo[v].m_TrailPrev == BadUVar ? numeric_limits<TUV>::max() : GetAssignedDecLevelVar(m_VarInfo[v].m_TrailPrev);

		if (vDecLevel != prevDecLevel)
		{
			ss << " DL " << vDecLevel << " *** ";
		}

		ss << SLit(l) << " {";

		const auto& ai = m_AssignmentInfo[GetVar(l)];
		const auto& vi = m_VarInfo[GetVar(l)];

		if (ai.m_IsAssigned && ai.IsAssignedBinary())
		{
			ss << SLit(vi.m_BinOtherLit);
		}
		else if (vi.m_ParentClsInd != BadClsInd)
		{
			ss << SLits(Cls(vi.m_ParentClsInd));
		}

		ss << "}; ";
	}

	ss << endl;

	return ss.str();
}

template <typename TLit, typename TUInd, bool Compress>
string CTopi<TLit, TUInd, Compress>::SUserLits(const span<TLit> litSpan) const
{
	stringstream ss;

	for (TLit l : litSpan)
	{
		ss << l << " ";
	}

	return ss.str().substr(0, ss.str().size() - 1);
}

template <typename TLit, typename TUInd, bool Compress>
string CTopi<TLit, TUInd, Compress>::SVars(const TSpanTULit varSpan) const
{
	stringstream ss;

	for (TULit v : varSpan)
	{
		ss << SVar(v) << " ";
	}

	return ss.str().substr(0, ss.str().size() - 1);
}

template <typename TLit, typename TUInd, bool Compress>
bool CTopi<TLit, TUInd, Compress>::P(const string& s)
{
	cout << s << flush; return true;
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::PrintDebugModel(TToporReturnVal trv)
{
	if (trv != TToporReturnVal::RET_SAT)
	{
		cout << "m_DebugModel = FAILED, since the return value is not SAT; it's " << trv << endl;
		return;
	}
	cout << "m_DebugModel = {false";

	for (TLit v = 1; v <= m_Stat.m_MaxUserVar; ++v)
	{
		const TToporLitVal externalVal = GetValue(v);
		assert(externalVal == TToporLitVal::VAL_SATISFIED || externalVal == TToporLitVal::VAL_UNSATISFIED);
		cout << ", " << (externalVal == TToporLitVal::VAL_SATISFIED ? "true" : "false");
		if (v % 100 == 0)
		{
			cout << "\n";
		}
	}

	cout << "};\n";
}

template <typename TLit, typename TUInd, bool Compress>
void CTopi<TLit, TUInd, Compress>::VerifyDebugModel()
{
	if (m_DebugModel.empty())
	{
		return;
	}
	assert((TLit)m_DebugModel.size() == m_Stat.m_MaxUserVar + 1);
	// -1: undefined; 0: false; 1: true
	CDynArray<uint8_t> internalVarVals((size_t)m_Stat.m_MaxInternalVar + 1, -1);

	auto ExpectedVal = [&](TULit l)
	{
		const TUVar v = GetVar(l);
		const uint8_t expectedValVar = internalVarVals[v];
		if (expectedValVar == -1)
		{
			return (uint8_t)-1;
		}

		if (IsNeg(l))
		{
			return expectedValVar == 1 ? (uint8_t)0 : (uint8_t)1;
		}

		return expectedValVar;
	};

	auto SInternalLitData = [&](TULit l)
	{
		stringstream ss;

		const auto elit = GetExternalLit(l);
		ss << "{index " << l << " : ilit " << SLit(l) << " : elit " << elit << "; debugModel = " << (elit > 0 ? m_DebugModel[elit] : m_DebugModel[-elit]) << "}";

		return ss.str();
	};

	//cout << "E2I: " << endl;
	for (TLit externalV = 1; externalV <= m_Stat.m_MaxUserVar; ++externalV)
	{
		const TUVar l = m_E2ILitMap[externalV];
		const TUVar v = GetVar(l);
		internalVarVals[v] = IsNeg(l) ? (uint8_t)!m_DebugModel[externalV] : (uint8_t)m_DebugModel[externalV];
		//cout << "m_DebugModel[" << externalV << "] = " << m_DebugModel[externalV] << " : " << SLit(l) << endl;
	}

	for (TUVar v = m_TrailStart; v != BadUVar; v = m_VarInfo[v].m_TrailNext)
	{
		const TULit l = GetLit(v, false);
		const uint8_t expectedVal = ExpectedVal(l);
		if (expectedVal != -1)
		{
			assert(IsSatisfied(l) == (bool)expectedVal || P("\t" + SLit(GetLit(v, false)) + "\n"));
			if (IsSatisfied(l) != (bool)expectedVal)
			{
				cout << "VerifyDebugModel ERROR in verifying the trail at literal " << SInternalLitData(l) << ". Exiting...\n";
				exit(-1);
			}
		}
	}

	for (TULit l = 1; l < GetNextLit(); ++l)
	{
		TWatchInfo& wi = m_Watches[l];

		const auto expectedValL = ExpectedVal(l);
		if (wi.IsEmpty() || expectedValL != 0)
		{
			continue;
		}

		if (wi.m_BinaryWatches != 0)
		{
			TSpanTULit binWatches = m_W.get_span_cap(wi.m_WBInd + wi.GetLongEntries(), wi.m_BinaryWatches);
			for (TULit secondLit : binWatches)
			{
				const auto expectedValSecondLit = ExpectedVal(secondLit);
				assert(expectedValSecondLit != 0 || P("\t" + SLit(l) + " " + SLit(secondLit) + "\n"));
				if (expectedValSecondLit == 0)
				{
					cout << "[l secondLit] = " << "[" << SInternalLitData(l) << ", " << SInternalLitData(secondLit) << "]" << endl;
					cout << "VerifyDebugModel ERROR in verifying a binary clause! Exiting...\n";
					exit(-1);
				}
			}
		}
	}

	int dbg = 0;
	for (TUInd clsInd = ClsLoopFirst(false); !ClsLoopCompleted(); clsInd = ClsLoopNext(), ++dbg)
	{
		if (ClsChunkDeleted(clsInd))
		{
			continue;
		}

		const auto cls = ConstClsSpan(clsInd);

		bool isOK = false;
		for (TULit l : cls)
		{
			const auto expectedValL = ExpectedVal(l);
			if (expectedValL != 0)
			{
				isOK = true;
				break;
			}

		}

		assert(isOK || P("\t" + SLits(cls) + "\n"));
		if (!isOK)
		{
			for (TULit l : cls)
			{
				cout << SInternalLitData(l) << ", ";
			}
			cout << "\nVerifyDebugModel ERROR in verifying a long clause! Exiting...\n";
			exit(-1);
		}
	}

	cout << "VerifyDebugModel VERIFIED!\n";
}

template class Topor::CTopi<int32_t, uint32_t, false>;
template class Topor::CTopi<int32_t, uint64_t, false>;
template class Topor::CTopi<int32_t, uint64_t, true>;
