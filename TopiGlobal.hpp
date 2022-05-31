// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <bit>

// Defining likely/unlikely for helping branch prediction. 
// Will replace by C++20's [[likely]]/[[unlikely]] attributes, once properly and consistently implemented in both GCC and VS
#ifdef _WIN32
#define likely(x)       (x)
#define unlikely(x)     (x)
#else
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#endif

using namespace std;

namespace Topor
{
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
		// So-far: cannot accommodate the last possible variable if sizeof(TLit) == sizeof(size_t), since the allocation will fail
		STATUS_EXOTIC_ERROR
	};

	// The TLit type, representing the external literals, unsigned
	// Will be used for storing internal literals, whose sign is determined by the LSB, rather than the MSB
	using TULit = make_unsigned<TLit>::type;
	static_assert(numeric_limits<TULit>::digits == numeric_limits<TLit>::digits + 1);

	// Bad literal
	TULit static constexpr BadULit = 0;
	
	// TUL is used to hold unsigned integers, whose number is not greater than that of the literaks; used only for code clarity
	using TUL = TULit;
	// TUVar is used to hold variables; used only for code clarity
	using TUVar = TULit;
	// TUV is used to hold unsigned integers, whose number is not greater than that of the variables; used only for code clarity
	using TUV = TUVar;
	// Bad variable
	TULit static constexpr BadUVar = 0;
	
	TUVar static constexpr GetVar(TULit l) { return l >> 1; }
	static_assert(GetVar(0b1001) == 0b100 && GetVar(0b1000) == 0b100);

	TULit static constexpr GetLit(TUVar v, bool isNegative) { return (v << 1) + (TULit)isNegative; }
	static_assert(GetLit(0b100, true) == 0b1001 && GetLit(0b100, false) == 0b1000);

	TULit static constexpr GetFirstLit() { return GetLit(1, false); }
	static_assert(GetFirstLit() == 2);

	bool static constexpr IsPos(TULit l) { return (l & 1) == 0; }
	static_assert(IsPos(0b1000));

	bool static constexpr IsNeg(TULit l) { return (bool)(l & 1); }
	static_assert(IsNeg(0b1001));

	TULit static constexpr Negate(TULit l) { return l ^ 1; }
	static_assert(Negate(0b1001) == 0b1000 && Negate(0b1000) == 0b1001);

	TULit static constexpr GetMaxLit(TULit l) { return IsNeg(l) ? l : Negate(l); }
	static_assert(GetMaxLit(0b1001) == 0b1001 && GetMaxLit(0b1000) == 0b1001);

	// Initialization-related: the initial number of entries in the fused buffer: set the original size to 4Mb
	static constexpr size_t InitEntriesInB = 0x400000 / sizeof(TULit);
	
	// The assumed number of bytes in one page
	static constexpr size_t BytesInPage = 64;
	static_assert(has_single_bit(BytesInPage));

	// The number of literals in one page; relevant for watch allocation
	static constexpr size_t LitsInPage = BytesInPage / sizeof(TULit);
	static_assert(has_single_bit(LitsInPage));
	// Make sure that, initially, we allocate a sufficient number of entries in the buffer to accommodate LitsInPage literals
	static_assert(LitsInPage <= InitEntriesInB);

	// counter type for discovering duplicates, tautologies, contradictions etc., e.g., in incoming clauses and assumptions
	// In the context of handling new clauses,
	// we did some tests and found that int32_t for the counter works better than int64_t, int16_t (both are 2x slower) and int8_t (significantly slower)
	// We also tested options using bit-arrays CBitArray<2> and CBitArrayAligned<2> with memset-0 after each clause, 
	// but the former is much slower than the current implementation, while the latter is impossibly slow.
	typedef	int32_t TCounterType;
	static_assert(is_signed<TCounterType>::value);

	// Bad parent clause index
	TUInd static constexpr BadClsInd = 0;

	template<class ForwardIt, class UnaryPredicate>
	ForwardIt move_if(ForwardIt first, ForwardIt last, UnaryPredicate p)
	{
		first = std::find_if(first, last, p);
		if (first != last)
			for (ForwardIt i = first; ++i != last; )
				if (!p(*i))
				{
					//*first++ = std::move(*i);
					swap(*first, *i);
					first++;
				}
		return first;
	}
}