#include "ToporIpasir.h"
#include "Topor.hpp"
#include <vector>
#include <string>
#include <unordered_map>

using namespace std;

namespace Topor
{
	class CToporIpasirWrapper : public CTopor<>
	{
	public:
		void Add(int lit) 
		{ 
			if (lit == 0)
			{
				AddClause(m_CurrCls);
				m_CurrCls.clear();
			}
			else
			{
				m_CurrCls.emplace_back(lit);
			}			
		}		
		
		void Assume(int lit)
		{
			if (m_Assump2Ind.find(lit) == m_Assump2Ind.end())
			{
				m_Assump2Ind.insert(make_pair(lit, m_CurrAssumps.size()));
				m_CurrAssumps.push_back(lit);
			}							
		}

		int Solve()
		{

			TToporReturnVal trv = CTopor::Solve(m_CurrAssumps);
			m_PrevAssump2Ind = move(m_Assump2Ind);
			m_Assump2Ind.clear();
			m_CurrAssumps.clear();

			switch (trv)
			{
			case Topor::TToporReturnVal::RET_SAT:
				return 10;
			case Topor::TToporReturnVal::RET_UNSAT:
				return 20;			
			default:
				return 0;
			}
		}

		int Val(int lit)
		{
			TToporLitVal tlv = GetLitValue(lit);
			switch (tlv)
			{
			case Topor::TToporLitVal::VAL_SATISFIED:
			case Topor::TToporLitVal::VAL_DONT_CARE:
				return lit;
			case Topor::TToporLitVal::VAL_UNSATISFIED:
				return -lit;
			default:
				return 0;
			}		
		}

		int Failed(int lit)
		{
			return IsAssumptionRequired(m_PrevAssump2Ind[lit]);
		}

		void SetTerminate(void* state, int (*terminate)(void* state))
		{
			if (terminate == nullptr)
			{
				m_CurrTerminateFunc = nullptr;
				m_CurrTerminateState = nullptr;
				return;
			}

			m_CurrTerminateFunc = terminate;
			m_CurrTerminateState = state;
			auto StopTopor = [&]()
			{
				if (m_CurrTerminateFunc && m_CurrTerminateFunc(m_CurrTerminateState))
				{
					return TStopTopor::VAL_STOP;
				}
				else
				{
					return TStopTopor::VAL_CONTINUE;
				}
			};

			SetCbStopNow(StopTopor);
		}

		void SetParam(const char* paramName, const char* paramVal)
		{
			CTopor::SetParam((string)paramName, stod((string)paramVal));
		}

	protected:
		vector<int> m_CurrCls;
		vector<int> m_CurrAssumps;		
		unordered_map<int,size_t> m_Assump2Ind;
		unordered_map<int, size_t> m_PrevAssump2Ind;
		void* m_CurrTerminateState = nullptr;
		int (*m_CurrTerminateFunc)(void* state) = nullptr;
	};
}

using namespace Topor;

extern "C" {
	 
	const char* ipasir_signature() 
	{
		return "IntelSatSolver";
	}

	void* ipasir_init() 
	{
		CToporIpasirWrapper* tiw = new CToporIpasirWrapper();

		return (void*)tiw;
	}

	void ipasir_release(void* solver)
	{
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		delete tiw;
	}

	void ipasir_add(void* solver, int lit) 
	{
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		tiw->Add(lit);
	}

	void ipasir_assume(void* solver, int lit) 
	{
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		tiw->Assume(lit);
	}

	int ipasir_solve(void* solver) 
	{
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		return tiw->Solve();
	}

	int ipasir_val(void* solver, int lit) 
	{
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		return tiw->Val(lit);		
	}

	int ipasir_failed(void* solver, int lit) 
	{
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		return tiw->Failed(lit);
	}

	void ipasir_set_terminate(void* solver, void* state, int (*terminate)(void* state)) {
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		tiw->SetTerminate(state, terminate);
	}

	void ipasir_set_parameter(void* solver, const char* paramName, const char* paramVal) {
		CToporIpasirWrapper* tiw = reinterpret_cast<CToporIpasirWrapper*>(solver);
		tiw->SetParam(paramName, paramVal);
	}

}
