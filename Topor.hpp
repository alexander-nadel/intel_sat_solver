// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <span>
#include <limits>
#include <utility>
#include <vector>
#include <initializer_list>
#include <algorithm>

#include "ToporExternalTypes.hpp"

namespace Topor
{
	class CTopi;

	class CTopor
	{
	public:
		// varNumHint is the expected number of variables; it is a non-mandatory hint, which, if provided correctly, helps the solver initialize faster 
		CTopor(TLit varsNumHint = 0);
		~CTopor();
		// Add a clause, where, currently, clauses are permanent and cannot be deleted
		// span is a C++20 concept. When calling AddClause, the actual parameter may be an std::vector, an std::array,
		// or any other sized contiguous sequence of literals (e.g., one can create a span from a C array TLit* a of size sz as follows: std::span(a, sz)).
		// A 0 in the span is interpreted as end-of-clause (so, the solver stops reading the clause, if 0 is encountered)
		// However, the span doesn't have to contain a 0.		
		void AddClause(const std::span<TLit> c);
		// This version allows the user to provide the clause as an arbitrary number of literals to the function, e.g.,: AddClause(1,-2,3)
		template<class... T> void AddClause(TLit lit1, T... lits) { std::vector<TLit> v({ lit1, lits... }); return AddClause(v); }
		// This version allows the user to provide the clause as follows: AddClause({1,-2,3})
		void AddClause(std::initializer_list<TLit> lits) { std::vector<TLit> v(lits); return AddClause(v); }
		// Solve, given the added clauses and the following optional information, relevant only for the current invocation:
		// - A set of assumptions (can, optionally, be 0-ended)
		// - A time-out in seconds in toInSecIsCpuTime
		//		where the second member of the pair is true, iff the timeout refers to CPU time, otherwise, it's Wall time. 
		// - A conflict threshold on the current invocation
		TToporReturnVal Solve(const std::span<TLit> assumps = {}, std::pair<double, bool> toInSecIsCpuTime = std::make_pair((std::numeric_limits<double>::max)(), true), uint64_t confThr = (std::numeric_limits<uint64_t>::max)());
		// Set a parameter value; using double for the value, since it encompasses all the arithmetic types, which can be used for parameters, that is:
		// Signed and unsigned integers of at most 32 bits and floating-points of at most the size of a C++ double 
		// Sets the status to permanent error, if the name/value combination is wrong. Run GetErrorStatus for details.
		void SetParam(const std::string& paramName, double newVal);
		
		// Boost the score of the variable v by value (the greater the value is, the greater the bump is)
		void BoostScore(TLit v, double value = 1.0);
		// Fix the polarity of the variable |l| to l
		// onlyOnce = false: change until ClearUserPolarityInfo(|l|) is invoked (or, otherwise, forever)
		// onlyOnce = true: change only once right now
		void FixPolarity(TLit l, bool onlyOnce = false);
		// Clear any polarity information of the variable v, provided by the user (so, the default solver's heuristic will be used to determine polarity)
		void ClearUserPolarityInfo(TLit v);
		
		// Dump DRAT
		void DumpDrat(std::ofstream& openedDratFile, bool isDratBinary);
		// Backtrack to the provided decision level
		void Backtrack(TLit decLevel);

		// Interrupt now
		void InterruptNow();
		// Set callback: stop now
		void SetCbStopNow(TCbStopNow CbStopNow);
		// Set callback: new learnt clause
		void SetCbNewLearntCls(TCbNewLearntCls CbNewLearntCls);

		// Get the value of a literal
		TToporLitVal GetLitValue(TLit l) const;
		// Get the whole model
		std::vector<TToporLitVal> GetModel() const;
		// Must be invoked immediately after a Solve invocation, which returned TToporReturnVal::RET_UNSAT
		// Is assumption which appeared at assumps[assumpInd] during the latest Solve call, that is, is it part of the UNSAT core
		// Sets the status to permanent error, if the latest Solve invocation didn't return TToporReturnVal::RET_UNSAT or if assumpInd >= assumps.size()
		bool IsAssumptionRequired(size_t assumpInd);

		// Is there an error in the solver?
		bool IsError() const;
		// Get the explanation of the current status
		// If IsError() holds, a non-empty explanation is mandatory, otherwise it might be empty (or not)
		std::string GetStatusExplanation() const;
		// Get statistics
		TToporStatistics GetStatistics() const;
		// Get the description of the parameters
		std::string GetParamsDescr() const;		
	protected:
		CTopi* m_Topi;
	};
}
