// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include "ColorPrint.h"

namespace Topor
{
	// The solver status
	enum class TToporStatus : uint8_t
	{
		// Status unknown
		STATUS_UNDECIDED,
		// The latest invocation had returned SAT and no clauses contradicting the model were introduced since
		STATUS_SAT,
		// The latest invocation returned UNSAT, but this status might be temporary (under assumptions)
		STATUS_UNSAT,
		// The latest invocation returned USER_INTERRUPT
		STATUS_USER_INTERRUPT,
		/*
		* Only unrecoverable status values below
		*/
		STATUS_FIRST_UNRECOVERABLE,
		// The instance is forever contradictory. 
		STATUS_CONTRADICTORY = STATUS_FIRST_UNRECOVERABLE,
		/*
		* Only permanently erroneous values below
		*/
		STATUS_FIRST_PERMANENT_ERROR,
		// The instance is in memory-out state, meaning that one of the allocations fails.
		STATUS_ALLOC_FAILED = STATUS_FIRST_PERMANENT_ERROR,
		// Data doesn't fit into the buffer
		STATUS_INDEX_TOO_NARROW,
		// Parameter-related error
		STATUS_PARAM_ERROR,
		// Error while processing assumption-required queries
		STATUS_ASSUMPTION_REQUIRED_ERROR,
		// Global timeout reached
		STATUS_GLOBAL_TIMEOUT,
		// Problem when trying to access/write the DRAT file
		STATUS_DRAT_FILE_PROBLEM,
		// An explicit-clauses-only function invoked in compressed mode
		STATUS_COMPRESSED_MISMATCH,
		// So-far: cannot accommodate the last possible variable if sizeof(TLit) == sizeof(size_t), since the allocation will fail
		STATUS_EXOTIC_ERROR
	};

	// Return value of Topor solving functions
	enum class TToporReturnVal : uint8_t
	{
		// Satisfiable
		RET_SAT,
		// Unsatisfiable
		RET_UNSAT,
		// Time-out for the last Solve invocation (the run-time is beyond user-provided threshold value in Solve's parameter)
		RET_TIMEOUT_LOCAL,
		// Conflict-out (the number of conflicts is beyond user's threshold value)
		RET_CONFLICT_OUT,
		// Memory-out
		RET_MEM_OUT,
		// Interrupted by the user
		RET_USER_INTERRUPT,
		// Data doesn't fit into the buffer anymore; Try to compile and run Topor with a wider TUInd, if you get this error
		RET_INDEX_TOO_NARROW,
		// Parameter-related error
		RET_PARAM_ERROR,
		// Error while processing assumption-required queries
		RET_ASSUMPTION_REQUIRED_ERROR,
		// Global time-out (the run-time is beyond user-provided threshold value in /global/overall_timeout parameter: the solver is unusable, if this value is returned)
		RET_TIMEOUT_GLOBAL,
		// Problem with DRAT file generation
		RET_DRAT_FILE_PROBLEM,
		// Exotic error: see the error string for more information
		RET_EXOTIC_ERROR	
	};

	// Literal value in Topor                     
	enum class TToporLitVal : uint8_t
	{
		VAL_SATISFIED,
		VAL_UNSATISFIED,
		VAL_UNASSIGNED,
		VAL_DONT_CARE,
	};

	// Callbacks

	// Callbacks return TStopTopor, which indicates, whether the solver should be stopped
	enum class TStopTopor : bool 
	{
		VAL_STOP,
		VAL_CONTINUE
	};
	
	// New learnt clause report callback type
	template <typename TLit>
	using TCbNewLearntCls = std::function<TStopTopor(const std::span<TLit>)>;
	
	// Stop-now callback type
	using TCbStopNow = std::function<TStopTopor()>;	
}

