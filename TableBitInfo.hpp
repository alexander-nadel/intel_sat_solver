// Copyright(C) 2021-2022 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <cstdint>
#include <array>
#include <bit>

using namespace std;

struct TBitInfo
{
	uint64_t w0ReadMask;
	uint64_t w1ReadMask;
	uint8_t w0ReadShr;
	constexpr uint64_t W0WriteMask() const
	{
		return ~w1ReadMask;
	}
	constexpr uint8_t W0WriteShl() const
	{
		return w0ReadShr;
	}
	constexpr uint64_t W1WriteMask() const
	{
		return w1ReadMask;
	}
};

static constexpr uint64_t LsbMask(uint8_t b)
{
	return b == 64 ? (uint64_t)-1 : ((uint64_t)1 << (uint64_t)b) - (uint64_t)1;
};

static_assert(LsbMask(1) == 1);
static_assert(LsbMask(2) == 3);
static_assert(LsbMask(3) == 7);
static_assert(LsbMask(10) == 1023);
static_assert(LsbMask(64) == (uint64_t)-1);

constexpr uint8_t MAX_BITS_NUM = 64;

// The goal of the compile-time-created array tableByBit is to enable writing and reading any number b of bits 
// (b<=64 bits; it can be easily extended to 128 bits, if a uint128_t type is available),
// *without branching*, starting from any bit-number in a uint64_t* buffer
// An entry i-1 in tableByBit (whose simple syntax works only starting from C++17) corresponds to i bits
// The idea is to decompose the b-bit number into MSB part, which goes into 64-word-0, and the LSB part (if any), which goes into 64-word-1
// Information per b bits: 
//          For every initial bit number (from 1 to b): 
//          (a) A mask to extract the MSB's from word-0 (w0ReadMask)
//          (b) Right-shift value to adjust the MSB's from word-0 (w0ReadShr)
//          (c) A mask to extract the LSB's from word-1
// For an example, consider i+1=26
// tableByBit[25][52].w1ReadMask = (1 << (52+26-64))-1 = (1 << 14)-1
// tableByBit[25][52].w0ReadMask = ~((1 << 52) - 1)
// tableByBit[25][52].w0ReadShr = 52-(52+26-64=14)=38; 
// See also the static_assert's after the table for more examples

constexpr array<array<TBitInfo, 64>, MAX_BITS_NUM> tableByBit = []
{
	array<array<TBitInfo, 64>, MAX_BITS_NUM> byBitInfo = {};

	for (uint8_t bitsInLit = 1; bitsInLit <= MAX_BITS_NUM; bitsInLit++)
	{
		for (uint8_t currBit = 0; currBit < MAX_BITS_NUM; ++currBit)
		{
			if (currBit + bitsInLit > 64)
			{
				const uint8_t lsbBits = currBit + bitsInLit - 64;
				byBitInfo[bitsInLit - 1][currBit].w0ReadMask = ~LsbMask(currBit);
				byBitInfo[bitsInLit - 1][currBit].w0ReadShr = currBit - lsbBits;
				byBitInfo[bitsInLit - 1][currBit].w1ReadMask = LsbMask(lsbBits);
			}
			else
			{
				byBitInfo[bitsInLit - 1][currBit].w0ReadMask = LsbMask(bitsInLit) << currBit;
				byBitInfo[bitsInLit - 1][currBit].w0ReadShr = currBit;
				byBitInfo[bitsInLit - 1][currBit].w1ReadMask = 0;
			}
		}
	}
	return byBitInfo;
}();

// Some static asserts to assert correctness

// 1 bit

static_assert(tableByBit[1 - 1][0].w0ReadMask == 1);
static_assert(tableByBit[1 - 1][0].w0ReadShr == 0);
static_assert(tableByBit[1 - 1][0].w1ReadMask == 0);

static_assert(tableByBit[1 - 1][1].w0ReadMask == 2);
static_assert(tableByBit[1 - 1][1].w0ReadShr == 1);
static_assert(tableByBit[1 - 1][1].w1ReadMask == 0);

static_assert(tableByBit[1 - 1][2].w0ReadMask == 4);
static_assert(tableByBit[1 - 1][2].w0ReadShr == 2);
static_assert(tableByBit[1 - 1][2].w1ReadMask == 0);

static_assert(tableByBit[1 - 1][9].w0ReadMask == (uint64_t)1 << 9);
static_assert(tableByBit[1 - 1][9].w0ReadShr == 9);
static_assert(tableByBit[1 - 1][9].w1ReadMask == 0);

static_assert(tableByBit[1 - 1][63].w0ReadMask == (uint64_t)1 << 63);
static_assert(tableByBit[1 - 1][63].w0ReadShr == 63);
static_assert(tableByBit[1 - 1][63].w1ReadMask == 0);

// 2 bits

static_assert(tableByBit[2 - 1][0].w0ReadMask == 3);
static_assert(tableByBit[2 - 1][0].w0ReadShr == 0);
static_assert(tableByBit[2 - 1][0].w1ReadMask == 0);

static_assert(tableByBit[2 - 1][1].w0ReadMask == 3 << 1);
static_assert(tableByBit[2 - 1][1].w0ReadShr == 1);
static_assert(tableByBit[2 - 1][1].w1ReadMask == 0);

static_assert(tableByBit[2 - 1][2].w0ReadMask == 3 << 2);
static_assert(tableByBit[2 - 1][2].w0ReadShr == 2);
static_assert(tableByBit[2 - 1][2].w1ReadMask == 0);

static_assert(tableByBit[2 - 1][62].w0ReadMask == (uint64_t)3 << 62);
static_assert(tableByBit[2 - 1][62].w0ReadShr == 62);
static_assert(tableByBit[2 - 1][62].w1ReadMask == 0);

static_assert(tableByBit[2 - 1][63].w0ReadMask == (uint64_t)1 << 63);
static_assert(tableByBit[2 - 1][63].w0ReadShr == 62);
static_assert(tableByBit[2 - 1][63].w1ReadMask == 1);

// 26 bits

static_assert(tableByBit[26 - 1][52].w0ReadMask == ~(((uint64_t)1 << 52) - 1));
static_assert(tableByBit[26 - 1][52].w0ReadShr == 52 - (52 + 26 - 64));
static_assert(tableByBit[26 - 1][52].w1ReadMask == ((uint64_t)1 << (52 + 26 - 64)) - 1);

// 64 bits

static_assert(tableByBit[64 - 1][0].w0ReadMask == (uint64_t)-1);
static_assert(tableByBit[64 - 1][0].w0ReadShr == 0);
static_assert(tableByBit[64 - 1][0].w1ReadMask == 0);

static_assert(tableByBit[64 - 1][1].w0ReadMask == (uint64_t)-1 - 1);
static_assert(tableByBit[64 - 1][1].w0ReadShr == 0);
static_assert(tableByBit[64 - 1][1].w1ReadMask == 1);

static_assert(tableByBit[64 - 1][2].w0ReadMask == (uint64_t)-1 - 3);
static_assert(tableByBit[64 - 1][2].w0ReadShr == 0);
static_assert(tableByBit[64 - 1][2].w1ReadMask == 3);

static_assert(tableByBit[64 - 1][3].w0ReadMask == (uint64_t)-1 - 7);
static_assert(tableByBit[64 - 1][3].w0ReadShr == 0);
static_assert(tableByBit[64 - 1][3].w1ReadMask == 7);

static_assert(tableByBit[64 - 1][63].w0ReadMask == (uint64_t)1 << 63);
static_assert(tableByBit[64 - 1][63].w0ReadShr == 0);
static_assert(tableByBit[64 - 1][63].w1ReadMask == (uint64_t)-1 - ((uint64_t)1 << 63));

// The goal of the compile-time-created array tableByEntry is to enable writing and reading *elements* of b of bits 
// (b<=64 bits; it can be trivially extended to 128 bits, if a uint128_t type is available),
// *without branching*, starting from any element in a uint64_t* buffer, representing a b-bit array
// The difference between tableByEntry and tableByBit is that the latter enables indexing by bit, while the former enables indexing by element number
// Hence, tableByEntry enables implementing b-bit arrays, while tableByBit enables working with a bit-indexed buffer, knowing the starting bit number and the bit width
// An entry i-1 in tableByEntry (whose simple syntax works only starting from C++17) corresponds to i bits
// The idea is similar to tableByBit: we decompose the b-bit number into MSB part, which goes into 64-word-0, and the LSB part (if any), which goes into 64-word-1
// Information per b bits: 
//		(1) Given an entry number in an b-bit array, it holds a mask M to get the starting bit-number in its entry in the uint64_t* buffer
//			In an b-bit array, this information saves a costly multiplication operation (entry*b) to get the first bit number
//      (2) For every initial bit number from 1 to b, the following information: 
//          (a) A mask to extract the MSB's from word-0 (w0ReadMask)
//          (b) Right-shift value to adjust the MSB's from word-0 (w0ReadShr)
//          (c) A mask to extract the LSB's from word-1
// For an example, consider i+1=26
// We have the following cycle of 32 possible entries, starting from 0: 
// 0, 26, 52, 14, 40, 2, 28, 54, 16, 42, 4, 30, 56, 18, 44, 6, 32, 58, 20, 46, 8, 34, 60, 22, 48, 10, 36, 62, 24, 50, 12, 38
// Hence, we have tableByEntry[25].first = 31, while:
// tableByEntry[25].second[0] = tableByBit[25][0]
// tableByEntry[25].second[1] = tableByBit[25][26]
// tableByEntry[25].second[2] = tableByBit[25][52]
// tableByEntry[25].second[3] = tableByBit[25][14]
// ...............................................
// tableByEntry[25].second[31] = tableByBit[25][38]

constexpr array<pair<uint8_t, array<TBitInfo, 64>>, MAX_BITS_NUM> tableByEntry = []
{
	array<pair<uint8_t, array<TBitInfo, 64>>, MAX_BITS_NUM> byEntryInfo = {};

	for (uint8_t bitsInLit = 1; bitsInLit <= MAX_BITS_NUM; bitsInLit++)
	{
		// 0-initialization is the default
		array<bool, MAX_BITS_NUM> isVisited({});

		uint8_t currRem = 0;

		for (uint8_t currBit = 0; !isVisited[currBit]; currBit = (currBit + bitsInLit) % 64, ++currRem)
		{
			isVisited[currBit] = true;

			byEntryInfo[bitsInLit - 1].second[currRem].w0ReadMask = tableByBit[bitsInLit - 1][currBit].w0ReadMask;
			byEntryInfo[bitsInLit - 1].second[currRem].w0ReadShr = tableByBit[bitsInLit - 1][currBit].w0ReadShr;
			byEntryInfo[bitsInLit - 1].second[currRem].w1ReadMask = tableByBit[bitsInLit - 1][currBit].w1ReadMask;
		}
		
		// currRem must always be a power of 2 for our table to work
		// This is what indeed happens: we static_assert it below for all possible values, but a mathematician should be able to prove it
		byEntryInfo[bitsInLit - 1].first = currRem - 1;
	}
	return byEntryInfo;
}();

// Some static asserts to assert correctness

// In order for tableByEntry to work, all the cycles must be power of 2
static_assert(has_single_bit<uint8_t>(tableByEntry[0].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[1].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[2].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[3].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[4].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[5].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[6].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[7].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[8].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[9].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[10].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[11].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[12].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[13].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[14].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[15].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[16].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[17].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[18].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[19].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[20].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[21].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[22].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[23].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[24].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[25].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[26].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[27].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[28].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[29].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[30].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[31].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[32].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[33].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[34].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[35].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[36].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[37].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[38].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[39].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[40].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[41].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[42].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[43].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[44].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[45].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[46].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[47].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[48].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[49].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[50].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[51].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[52].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[53].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[54].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[55].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[56].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[57].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[58].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[59].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[60].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[61].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[62].first + 1));
static_assert(has_single_bit<uint8_t>(tableByEntry[63].first + 1));
// Asserting some of the hard-wired cycle values 
static_assert(tableByEntry[1 - 1].first == 63);
static_assert(tableByEntry[2 - 1].first == 31);
static_assert(tableByEntry[26 - 1].first == 31);
// Asserting some of the corresponding entries between tableByEntry and tableByBit
static_assert(tableByEntry[26 - 1].second[0].w0ReadMask == tableByBit[26 - 1][0].w0ReadMask);
static_assert(tableByEntry[26 - 1].second[0].w0ReadShr == tableByBit[26 - 1][0].w0ReadShr);
static_assert(tableByEntry[26 - 1].second[0].w1ReadMask == tableByBit[26 - 1][0].w1ReadMask);
static_assert(tableByEntry[26 - 1].second[1].w0ReadMask == tableByBit[26 - 1][26].w0ReadMask);
static_assert(tableByEntry[26 - 1].second[1].w0ReadShr == tableByBit[26 - 1][26].w0ReadShr);
static_assert(tableByEntry[26 - 1].second[1].w1ReadMask == tableByBit[26 - 1][26].w1ReadMask);
static_assert(tableByEntry[26 - 1].second[2].w0ReadMask == tableByBit[26 - 1][52].w0ReadMask);
static_assert(tableByEntry[26 - 1].second[2].w0ReadShr == tableByBit[26 - 1][52].w0ReadShr);
static_assert(tableByEntry[26 - 1].second[2].w1ReadMask == tableByBit[26 - 1][52].w1ReadMask);
static_assert(tableByEntry[26 - 1].second[3].w0ReadMask == tableByBit[26 - 1][14].w0ReadMask);
static_assert(tableByEntry[26 - 1].second[3].w0ReadShr == tableByBit[26 - 1][14].w0ReadShr);
static_assert(tableByEntry[26 - 1].second[3].w1ReadMask == tableByBit[26 - 1][14].w1ReadMask);
static_assert(tableByEntry[26 - 1].second[31].w0ReadMask == tableByBit[26 - 1][38].w0ReadMask);
static_assert(tableByEntry[26 - 1].second[31].w0ReadShr == tableByBit[26 - 1][38].w0ReadShr);
static_assert(tableByEntry[26 - 1].second[31].w1ReadMask == tableByBit[26 - 1][38].w1ReadMask);
