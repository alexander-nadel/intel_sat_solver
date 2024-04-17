// Copyright(C) 2021-2023 Intel Corporation
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
	template <typename TLit, typename TUInd, bool Compress>
	class CTopi;

	// The template types are:
	// (1) The signed literal type TLit (used to provide literals by the user; Internally, the solver uses the corresponding unsigned type of the same width)
	// (2) The unsigned index into the clause and watch buffers TUInd 
	// The solver is designed to be able to work with any above types as long as sizeof(TUInd) >= sizeof(TLit) and sizeof(TUInd) <= sizeof(size_t)
	// (3) A Boolean specifying whether to compress the clause buffer
	
	template <typename TLit = int32_t, typename TUInd = uint32_t, bool Compress = false>
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
		template<class... T> void AddClause(TLit lit1, T... lits) { std::array v = { lit1, lits... }; return AddClause(v); }
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
		// Create an internal literal for l: for advanced usages to play with internal literal ordering
		void CreateInternalLit(TLit l);
		
		// Dump DRAT
		void DumpDrat(std::ofstream& openedDratFile, bool isDratBinary, bool dratSortEveryClause);
		// Backtrack to the provided decision level
		void Backtrack(TLit decLevel);
		// Changing the configuration to # configNum, so that every configNum generates a unique configuration, where 0 makes no change. This is used for enabling different configs in parallel solving. Returns a unique configuration string"
		std::string ChangeConfigToGiven(uint16_t configNum);

		// Interrupt now
		void InterruptNow();
		// Set callback: stop now
		void SetCbStopNow(TCbStopNow CbStopNow);
		// Set callback: new learnt clause
		void SetCbNewLearntCls(TCbNewLearntCls<TLit> CbNewLearntCls);

		// Get the value of a literal
		TToporLitVal GetLitValue(TLit l) const;
		// Get the whole model
		std::vector<TToporLitVal> GetModel() const;
		// Must be invoked immediately after a Solve invocation, which returned TToporReturnVal::RET_UNSAT
		// Is assumption which appeared at assumps[assumpInd] during the latest Solve call, that is, is it part of the UNSAT core
		// Sets the status to permanent error, if the latest Solve invocation didn't return TToporReturnVal::RET_UNSAT or if assumpInd >= AssumpsSize()
		bool IsAssumptionRequired(size_t assumpInd);
		// Get the decision level of the given literal (which must be assigned!)
		TLit GetLitDecLevel(TLit l) const;
		// Get the number of Solve invocations
		uint64_t GetSolveInvs() const;
		// Get the maximal user-provided variable that wasn't simplified away (since it, e.g., only participated in tautologies)
		TLit GetMaxUserVar() const;
		// The maximal internal variable number (can be thought of as the number of active variables in the solver)
		TLit GetMaxInternalVar() const;
		// Get a string with some statistics
		std::string GetStatStrShort(bool forcePrintingHead = false);
		// Get the number of conflicts so far
		uint64_t GetConflictsNumber() const;
		// Get the number of active clauses in the solver (undeleted binary and long clauses both initial & learnt)
		uint64_t GetActiveClss() const;
		// Get the number of active long (non-binary) learnt clauses in the solver
		uint64_t GetActiveLongLearntClss() const;
		// Get the number of backtracks
		uint64_t GetBacktracks() const;
		// The number of backtrack levels, saved by reusing assumptions
		uint64_t GetAssumpReuseBacktrackLevelsSaved() const;	
		// Get the  number of propagations (implications)
		uint64_t GetPropagations() const;

		// Is there an error in the solver?
		bool IsError() const;
		// Get the explanation of the current status
		// If IsError() holds, a non-empty explanation is mandatory, otherwise it might be empty (or not)
		std::string GetStatusExplanation() const;
		// Get the description of the parameters
		std::string GetParamsDescr() const;		

		void SetParallelData(unsigned threadId, std::function<void(unsigned threadId, int lit)> ReportUnitClause, std::function<int(unsigned threadId, bool reinit)> GetNextUnitClause);		
	protected:
		CTopi<TLit, TUInd, Compress>* m_Topi;
	};
}
