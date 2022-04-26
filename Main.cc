// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <array>
#include <string>
#include <filesystem>
#include <functional>
#include <cstring>
#include <iterator>
#include <cstdio>
#include <type_traits>

#ifdef __CYGWIN__
extern "C" FILE * popen(const char* command, const char* mode);
extern "C" void pclose(FILE * pipe);
#endif

#ifndef SKIP_ZLIB
#ifdef WIN32
#   include "win/zlib.h"
#else
#   include <zlib.h>
#endif
#endif
#include "SetInScope.h"

#ifdef WIN32
#define popen _popen
#define pclose _pclose
#define fileno _fileno
#endif

template<char delimiter>
class WordDelimitedBy : public std::string
{};

namespace fs = std::filesystem;

#include "Topor.hpp"

using namespace std;
using namespace Topor;

// Converts enum class to the underlying type
template <typename E>
constexpr auto U(E e) noexcept
{
	return static_cast<std::underlying_type_t<E>>(e);
}

// The supported archive file types
enum class TArchiveFileType : uint8_t
{
	XZ = 0,
	LZMA = 1,
	BZ = 2,
	GZ = 3,
	SevenZ = 4,
	None = 5
};

// The supported archive file types' file signatures
constexpr uint8_t MaxSigLength = 7;
constexpr static array<array<int, MaxSigLength + 1>, U(TArchiveFileType::None)> fileSig = { {
	 { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00, 0x00, EOF},
	 { 0x5D, 0x00, 0x00, 0x80, 0x00, EOF, EOF, EOF},
	 { 0x42, 0x5A, 0x68, EOF, EOF, EOF, EOF, EOF },
	 { 0x1F, 0x8B, EOF, EOF, EOF, EOF, EOF, EOF },
	 { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, EOF, EOF }
} };

static constexpr bool AllSigsDifferByFirstInt()
{
	for (size_t i = 0; i < fileSig.size(); ++i)
	{
		for (size_t j = i + 1; j < fileSig.size(); ++j)
		{
			if (fileSig[i][0] == fileSig[j][0])
			{
				return false;
			}
		}
	}
	return true;
}

// We count all signatures' first integer being different in the code, which determines the file's type
static_assert(AllSigsDifferByFirstInt());

// Strings before and after the command for each archive type
static array<pair<string, string>, U(TArchiveFileType::None)> commandStringBeforeAndAfter = {
	make_pair("xz -c -d",""),
	make_pair("lzma -c -d",""),
	make_pair("bzip2 -c -d",""),
	make_pair("gzip -c -d",""),
	make_pair("7z x -so","2>/dev/null"),
};

static constexpr int BadRetVal = -1;

int OnFinishingSolving(CTopor& topor, TToporReturnVal ret, bool printModel)
{
	CApplyFuncOnExitFromScope<> printStatusExplanation([&]() 
	{ 
		const string expl = topor.GetStatusExplanation();
		if (!expl.empty())
		{
			cout << "c " << expl << endl;
		}
	});

	switch (ret)
	{
	case Topor::TToporReturnVal::RET_SAT:
		cout << "s SATISFIABLE" << endl;
		if (printModel)
		{
			cout << "v";
			for (TLit v = 1; v <= topor.GetStatistics().m_MaxUserVar; ++v)
			{
				const auto vVal = topor.GetLitValue(v);
				assert(vVal != TToporLitVal::VAL_UNASSIGNED);
				cout << " " << (vVal != TToporLitVal::VAL_UNSATISFIED ? v : -v);
			}
			cout << " 0" << endl;
		}
		return 10;
	case Topor::TToporReturnVal::RET_UNSAT:
		cout << "s UNSATISFIABLE" << endl;
		return 20;
	case Topor::TToporReturnVal::RET_TIMEOUT_LOCAL:
		cout << "s TIMEOUT_LOCAL" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_CONFLICT_OUT:
		cout << "s CONFLICT_OUT" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_MEM_OUT:
		cout << "s MEMORY_OUT" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_USER_INTERRUPT:
		cout << "s USER_INTERRUPT" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_INDEX_TOO_NARROW:
		cout << "s INDEX_TOO_NARROW" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_PARAM_ERROR:
		cout << "s PARAM_ERROR" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_TIMEOUT_GLOBAL:
		cout << "s TIMEOUT_GLOBAL" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_DRAT_FILE_PROBLEM:
		cout << "s DRAT_FILE_PROBLEM" << endl;
		return BadRetVal;
	case Topor::TToporReturnVal::RET_EXOTIC_ERROR:
		cout << "s EXOTIC_ERROR" << endl;
		return BadRetVal;
	default:
		cout << "s UNEXPECTED_ERROR" << endl;
		return BadRetVal;
	}
}

int main(int argc, char** argv)
{
	if (argc == 1 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
	{
		cout << print_as_color <ansi_color_code::red>("c Usage:") << endl;
		cout << "\tc topor_tool/topor_static/topor_debug/topor <CNF> OPTIONAL: <Param1> <Val1> <Param2> <Val2> ... <ParamN> <ValN>" << endl;
		cout << "\tc <CNF> can either be a text file or an archive file in one of the following formats: .xz, .lzma, .bz2, .gz, .7z (the test is based on the file signature)" << endl;
		cout << "\tc <CNF> is expected to be in simplified DIMACS format, used at SAT Competitions (http://www.satcompetition.org/2011/format-benchmarks2011.html) with the following optional extension to support incrementality:" << endl;
		cout << "\tc The following Intel(R) SAT Solver-specific commands are also legal (ignore \"c \" below): " << endl;
		cout << "\tc r <ParamName> <ParamVal>" << endl;
		cout << "\tc ot <TimeOut> <IsCpuTimeOut>" << endl;
		cout << "\tc oc <ConflictThreshold>" << endl;
		cout << "\tc lb <BoostScoreLit> <Mult>" << endl;
		cout << "\tc lf <FixPolarityLit> <OnlyOnce>" << endl;
		cout << "\tc lc <ClearUserPolarityInfoLit>" << endl;
		cout << "\tc b <BacktrackLevel>" << endl;
		cout << "\tc s <Lit1 <Lit2> ... <Litn>: solve under the assumptions {<Lit1 <Lit2> ... <Litn>}" << endl;		
		cout << "\tc The solver parses the p cnf vars clss line, but it ignores the number of clauses and uses the number of variables as a non-mandatory hint" << endl;
		cout << print_as_color <ansi_color_code::red>("c Intel(R) SAT Solver parameters:") << endl;
		cout << "\tc " << print_as_color <ansi_color_code::cyan>("/topor_tool/bin_drat_file") << " : string; default = " << print_as_color<ansi_color_code::green>("\"\"") << " : " << "path to a file to write down a binary DRAT proof\n";
		cout << "\tc " << print_as_color <ansi_color_code::cyan>("/topor_tool/text_drat_file") << " : string; default = " << print_as_color<ansi_color_code::green>("\"\"") << " : " << "path to a file to write down a text DRAT proof (if more than one /topor_tool/bin_drat_file and /topor_tool/text_drat_file parameters provided, only the last one is applied, rest are ignored)\n";
		cout << "\tc " << print_as_color <ansi_color_code::cyan>("/topor_tool/print_model") << " : bool (0 or 1); default = " << print_as_color<ansi_color_code::green>("1") << " : " << "print the models for satisfiable invocations?\n";
		cout << "\tc " << print_as_color <ansi_color_code::cyan>("/topor_tool/verify_model") << " : bool (0 or 1); default = " << print_as_color<ansi_color_code::green>("0") << " : " << "verify the models for satisfiable invocations?\n";

		CTopor topor;
		cout << topor.GetParamsDescr();
		return 0;
	}

	cout << "c Intel(R) SAT Solver started" << endl;

	if (argc & 1)
	{
		cout << "c topor_tool ERROR: the number of arguments (excluding the executable name) must be odd. Run without parameters for more information." << endl;
		return BadRetVal;
	}

	bool isDratBinary = true;
	string dratName("");
	bool printModel = true;
	bool verifyModel = false;

	auto ParseParameters = [&](CTopor& topor)
	{
		// Parse parameters
		for (int currArgNum = 2; currArgNum < argc; currArgNum += 2)
		{			
			const string paramNameStr = (string)argv[currArgNum];
			const string paramValStr = (string)argv[currArgNum + 1];

			auto ReadBoolParam = [&](string& errMsg)
			{
				int intVal = 0;
				stringstream ss;
				try
				{
					intVal = stoi(paramValStr);
				}
				catch (...)
				{
					ss << "c ERROR: couldn't convert " << paramValStr << " to an integer" << endl;					
				}

				if (intVal != 0 && intVal != 1)
				{
					ss << "c ERROR: " << paramValStr << " must be 0 or 1" << endl;					
				}

				errMsg = move(ss.str());

				return (bool)intVal;
			};

			// /topor_tool/ prefix length
			static constexpr size_t ttPrefixLen = 12;
			if (paramNameStr.substr(0, ttPrefixLen) == "/topor_tool/")
			{
				const string param = paramNameStr.substr(ttPrefixLen, paramNameStr.size() - ttPrefixLen);
				if (param == "bin_drat_file")
				{
					dratName = paramValStr;
					isDratBinary = true;
					cout << "c /topor_tool/bin_drat_file " << dratName << endl;
				}
				else if (param == "text_drat_file")
				{
					dratName = paramValStr;
					isDratBinary = false;
					cout << "c /topor_tool/text_drat_file " << dratName << endl;
				}
				else if (param == "print_model")
				{
					cout << "c /topor_tool/print_model " << paramValStr << endl;
					string errMsg;
					printModel = ReadBoolParam(errMsg);
					if (!errMsg.empty())
					{
						cout << errMsg;
						return true;
					}
				}
				else if (param == "verify_model")
				{
					cout << "c /topor_tool/verify_model " << paramValStr << endl;
					string errMsg;
					verifyModel = ReadBoolParam(errMsg);
					if (!errMsg.empty())
					{
						cout << errMsg;
						return true;
					}
				}
				else
				{
					cout << "c ERROR: unrecognized /topor_tool/ parameter: " << paramNameStr << endl;
					return true;
				}
			}
			else
			{
				auto [paramName, paramValue] = make_pair(paramNameStr, (double)0.0);

				try
				{
					paramValue = std::stod(paramValStr);
				}
				catch (...)
				{
					cout << "c topor_tool ERROR: could not convert " << argv[currArgNum + 1] << " to double" << endl;
					return true;
				}

				topor.SetParam(paramName, paramValue);
				const bool isError = topor.IsError();							
				if (isError)
				{
					const string errorDescr = topor.GetStatusExplanation();
					cout << "c ERROR in Topor parameter: " << errorDescr << endl;
					return true;
				}
			}
			
		}
		return false;
	};

	/*
	* Identify the input file type, read it, read the parameters too
	*/
	
	const string inputFileName = argv[1];
	if (!filesystem::exists(inputFileName))
	{
		cout << "c topor_tool ERROR: the input file " << inputFileName << " doesn't exist" << endl;
		return BadRetVal;
	}
	
	// Does the signature match one of the archive types we support?

	FILE* tmp = fopen(inputFileName.c_str(), "r");
	if (tmp == nullptr)
	{
		cout << "c topor_tool ERROR: couldn't open the input file " << inputFileName << " to verify the signature" << endl;
		return BadRetVal;
	}

	TArchiveFileType aFileType = TArchiveFileType::None;

	int c = getc(tmp);	
	for (size_t currType = 0; c != EOF && currType < fileSig.size() && aFileType == TArchiveFileType::None; ++currType)
	{		
		const auto& currSig = fileSig[currType];

		if (c == currSig[0])
		{
			bool rightType = true;
			c = getc(tmp);
			for (uint8_t i = 1; c != EOF && currSig[i] != EOF; ++i, c = getc(tmp))
			{				
				if (c != currSig[i])
				{
					rightType = false;
					break;
				}
			}

			if (rightType)
			{
				aFileType = (TArchiveFileType)currType;
				cout << "c topor_tool: file type determined to an archive file.";
#ifndef SKIP_ZLIB
				if (aFileType != TArchiveFileType::GZ)
				{
					cout << " The following command will be used to read it through a pipe : " <<
						commandStringBeforeAndAfter[U(aFileType)].first << " " << inputFileName << " " << commandStringBeforeAndAfter[U(aFileType)].second;
				}
				else
				{
					cout << " It will be read using gzlib.";
				}				
#else
				cout << " The following command will be used to read it through a pipe : " <<
					commandStringBeforeAndAfter[U(aFileType)].first << " " << inputFileName << " " << commandStringBeforeAndAfter[U(aFileType)].second;
#endif
				cout << endl;
			}
		}
	}
	
	fclose(tmp);

	// Open the input file: it can be an archive file, in which case it is read as a pipe
	// or a plain text file, which is read as a file
#ifndef SKIP_ZLIB
	const auto useZlib = aFileType == TArchiveFileType::GZ || aFileType == TArchiveFileType::None;
	FILE* f = useZlib ? (FILE*)gzopen(inputFileName.c_str(), "r") : 
		popen((commandStringBeforeAndAfter[U(aFileType)].first + " " + inputFileName + " " + commandStringBeforeAndAfter[U(aFileType)].second).c_str(), "r");
#else
	const auto useFopen = aFileType == TArchiveFileType::None;
	FILE* f = useFopen ? (FILE*)fopen(inputFileName.c_str(), "r") : popen((commandStringBeforeAndAfter[U(aFileType)].first + " " + inputFileName + " " + commandStringBeforeAndAfter[U(aFileType)].second).c_str(), "r");
#endif
	if (f == nullptr)
	{
		cout << "c topor_tool ERROR: couldn't open the input file" << endl;
		return BadRetVal;
	}

	CTopor* topor = nullptr;
	TToporReturnVal ret = TToporReturnVal::RET_EXOTIC_ERROR;

	ofstream dratFile;
	
	CApplyFuncOnExitFromScope<> onExit([&]() 
	{ 
#ifndef SKIP_ZLIB
		useZlib ? gzclose(gzFile(f)) : pclose(f);
#else
		pclose(f);
#endif
		if (dratName != "")
		{
			dratFile.close();

			if (ret == TToporReturnVal::RET_SAT)
			{
				remove(dratName.c_str());
			}
		}
		delete topor;		
	});

	/*
	* File reading loop
	*/

	// Inside the loop, we expect to read either:
	// c...: a comment
	// p cnf vars clss: the header (must appear before clauses; handled as recommendation only)
	// Lit1 Lit2 ... LitN 0: 0-ended clause
	// "s Lit1 Lit2 ... LitN": solve under the given assumption
	
	uint64_t lineNum = 1;
	bool pLineRead = false;
	int retValBasedOnLatestSolve = BadRetVal;

	auto CreateToporInst = [&](TLit varsNumHint = 0)
	{
		topor = new CTopor(varsNumHint);
		if (topor == nullptr)
		{
			cout << "c topor_tool ERROR: couldn't create Topor instance" << endl;
			return BadRetVal;
		}

		pLineRead = true;
		if (ParseParameters(*topor)) return BadRetVal;

		if (dratName != "")
		{
			dratFile.open(dratName.c_str());
			if (dratFile.bad())
			{
				cout << "c topor_tool ERROR: couldn't open DRAT file " << dratName << endl;
				return BadRetVal;
			}
			topor->DumpDrat(dratFile, isDratBinary);
		}

		return 0;
	};

	// Populated if verify_model is on
	vector<vector<TLit>> vmClss;
	// Returns 10 upon success and BadRetVal upon failure
	auto VerifyModel = [&](vector<TLit>* assumps = nullptr)
	{
		// Verify the model
		cout << "c topor_tool: before verifying that the model satisfies " << (assumps == nullptr ? "the clauses" : "the assumptions and the clauses") << endl;
		if (assumps != nullptr)
		{

			// Verify the assumptions first
			for (TLit a : *assumps)
			{
				if (a != 0)
				{
					TToporLitVal v = topor->GetLitValue(a);
					if (v != TToporLitVal::VAL_SATISFIED && v != TToporLitVal::VAL_DONT_CARE)
					{
						cout << "c ERROR: assumptions " << a << " is not satisfied!" << endl;
						return BadRetVal;
					}
				}			
			}
			cout << "c topor_tool: assumptions verified!" << endl;
		}
		for (vector<TLit>& cls : vmClss)
		{
			bool isVerified = false;
			for (TLit l : cls)
			{
				if (l != 0)
				{
					TToporLitVal v = topor->GetLitValue(l);
					if (v == TToporLitVal::VAL_SATISFIED || v == TToporLitVal::VAL_DONT_CARE)
					{
						isVerified = true;
						break;
					}
				}
			}
			if (!isVerified)
			{
				cout << "c ERROR: the following clause is not satisfied:";
				for (TLit l : cls)
				{
					cout << " " << l;
				}
				cout << endl;
				return BadRetVal;
			}
		}
		cout << "c topor_tool: clauses verified!" << endl;
		return 10;
	};

	const size_t maxSz = (size_t)1 << 28;	
	char* line = (char*)malloc(maxSz);
	if (line == nullptr)
	{
		cout << "c topor_tool ERROR: couldn't allocate " + to_string(maxSz) + " bytes for reading the lines" << endl;
		return BadRetVal;
	}
	
	auto ReadLine = [&](FILE* f, char* l, size_t maxChars)
	{
#ifndef SKIP_ZLIB
		return useZlib ? gzgets((gzFile)f, line, maxSz) : fgets(line, maxSz, f);
#else
		return fgets(line, maxSz, f);
#endif
	};

	pair<double, bool> nextSolveToInSecIsCpuTime = make_pair(numeric_limits<double>::max(), false);
	uint64_t nextSolveConfThr = numeric_limits<uint64_t>::max();

	while (ReadLine(f, line, maxSz) != nullptr)
	{
		const size_t len = strlen(line);
		CApplyFuncOnExitFromScope<> beforeNextLoop([&]() { ++lineNum; });

		size_t currLineI = 0;
		auto SkipWhitespaces = [&]()
		{
			while (line[currLineI] == ' ' && currLineI < len)
			{
				++currLineI;
			}
		};

		SkipWhitespaces();
		if (currLineI >= len)
		{
			// Empty line
			continue;
		}

		if (line[currLineI] == 'c')
		{
			// A comment
			continue;
		}

		if (line[currLineI] == 'r')
		{
			string lStr = line;
			
			const size_t paramNameStart = 2;
			const size_t paramNameEnd = lStr.find(' ', 2);
			if (paramNameEnd == string::npos)
			{
				throw logic_error("c topor_tool ERROR: expected <paramName> never ended at line number " + to_string(lineNum));
			}
			const string paramName = lStr.substr(paramNameStart, paramNameEnd - paramNameStart);
			const string paramVal = lStr.substr(paramNameEnd + 1);
			
			double paramValDouble = numeric_limits<double>::infinity();
			try
			{
				paramValDouble = stod(paramVal);				
			}
			catch (...)
			{
				throw logic_error("c topor_tool ERROR: couldn't convert the parameter value to double at line number " + to_string(lineNum));
			}
			
			topor->SetParam(paramName, paramValDouble);

			continue;
		}

		if (line[currLineI] == 'o')
		{
			string lStr = line;
				
			// cout << "\tc ot <TimeOut> <IsCpuTimeOut>" << endl;
			// cout << "\tc oc <ConflictThreshold>" << endl;
			if (lStr[1] != 't' && lStr[1] != 'c')
			{
				throw logic_error("c topor_tool ERROR: The 2nd character must be either t or c at line number " + to_string(lineNum));
			}

			if (lStr[2] != ' ')
			{
				throw logic_error("c topor_tool ERROR: The 3nd character must be a space at line number " + to_string(lineNum));
			}

			if (lStr[1] == 't')
			{
				const size_t toNameStart = 2;
				const size_t toNameEnd = lStr.find(' ', 3);
				if (toNameEnd == string::npos)
				{
					throw logic_error("c topor_tool ERROR: expected <TimeOut> <IsCpuTimeOut> at line number " + to_string(lineNum));
				}
				const string toStr = lStr.substr(toNameStart, toNameEnd - toNameStart);
				double to = numeric_limits<double>::infinity();
				try
				{
					to = stod(toStr);
				}
				catch (...)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert <TimeOut> to double at line number " + to_string(lineNum));
				}
				
				const string isCpuTimeOutStr = lStr.substr(toNameEnd + 1);
				int isCpuTimeOut = numeric_limits<int>::max();
				try
				{
					isCpuTimeOut = stoi(isCpuTimeOutStr);
				}
				catch (...)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert <IsCpuTimeOut> to int at line number " + to_string(lineNum));
				}
				
				if (isCpuTimeOut < 0 || isCpuTimeOut > 1)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert <IsCpuTimeOut> to 0 or 1 at line number " + to_string(lineNum));
				}
				nextSolveToInSecIsCpuTime = make_pair(to, (bool)isCpuTimeOut);
			}
			else
			{
				assert(lStr[1] == 'c');
				const string cThrStr = lStr.substr(3);
				
				try
				{
					nextSolveConfThr = (uint64_t)stoull(cThrStr);
				}
				catch (...)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert <ConflictThreshold> to uint64_t at line number " + to_string(lineNum));
				}
			}

			continue;
		}

		auto ParseNumber = [&]()
		{
			SkipWhitespaces();
			if (currLineI >= len)
			{
				throw logic_error("c topor_tool ERROR: no number after skipping white-spaces at line number " + to_string(lineNum));
			}
			bool isNeg = line[currLineI] == '-';
			if (isNeg)
			{
				++currLineI;
			}
			if (!isdigit(line[currLineI]))
			{
				throw logic_error("c topor_tool ERROR: the first character is expected to be a digit at line number " + to_string(lineNum));
			}

			long long res = 0;

			while (isdigit(line[currLineI]))
			{
				const auto currDigit = line[currLineI++] - '0';
				res = res * 10 + (long long)(currDigit);
			}

			if (isNeg)
			{
				res = -res;
			}

			return res;
		};

		if (line[currLineI] == 'l')
		{			
			string lStr = line;

			// cout << "\tc lb <BoostScoreLit> <Mult>" << endl;
			// cout << "\tc lf <FixPolarityLit> <OnlyOnce>" << endl;
			// cout << "\tc lc <ClearUserPolarityInfoLit>" << endl;
			if (lStr[1] != 'b' && lStr[1] != 'f' && lStr[1] != 'c')
			{
				throw logic_error("c topor_tool ERROR: The 2nd character must be either b or f or c at line number " + to_string(lineNum));
			}

			if (lStr[2] != ' ')
			{
				throw logic_error("c topor_tool ERROR: The 3nd character must be a space at line number " + to_string(lineNum));
			}

			currLineI += 2;			
			TLit lit = (TLit)ParseNumber();

			if (lStr[1] == 'c')
			{
				topor->ClearUserPolarityInfo(lit);
			}
			else if (lStr[1] == 'f')
			{
				const string isOnlyOnceStr = lStr.substr(currLineI + 1);
				int isOnlyOnce = numeric_limits<int>::max();
				try
				{
					isOnlyOnce = stoi(isOnlyOnceStr);
				}
				catch (...)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert <FixPolarityLit> to int at line number " + to_string(lineNum));
				}

				if (isOnlyOnce < 0 || isOnlyOnce > 1)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert <FixPolarityLit> to 0 or 1 at line number " + to_string(lineNum));
				}
				topor->FixPolarity(lit, (bool)isOnlyOnce);
			}
			else
			{
				assert(lStr[1] == 'b');
				const string multStr = lStr.substr(currLineI + 1);

				double mult = numeric_limits<double>::infinity();
				try
				{
					mult = stod(multStr);
				}
				catch (...)
				{
					throw logic_error("c topor_tool ERROR: couldn't convert the <Mult> value to double at line number " + to_string(lineNum));
				}

				topor->BoostScore(lit, mult);
			}

			continue;
		}
	
		if (line[currLineI] == 'b')
		{
			// cout << "\tc b <BacktrackLevel>" << endl;
			if (line[1] != ' ')
			{
				throw logic_error("c topor_tool ERROR: The 2nd character must be a space at line number " + to_string(lineNum));
			}

			++currLineI;
			auto dl = ParseNumber();
			
			topor->Backtrack((TLit)dl);

			continue;
		}

		if (line[currLineI] == 'p')
		{
			if (pLineRead)
			{
				cout << "c topor_tool ERROR: second line starting with p at line number " << lineNum << endl;
				return BadRetVal;
			}

			currLineI += 6;
			// currLineI should be at the first number now
			if (line[currLineI - 5] != ' ' || line[currLineI - 4] != 'c' || line[currLineI - 3] != 'n' || line[currLineI - 2] != 'f' || line[currLineI - 1] != ' ')
			{
				cout << "c topor_tool ERROR: couldn't parse the p-line as 'p cnf <VARS> <CLSS>' at line number " << lineNum << endl;
				return BadRetVal;
			}
			TLit vars = 0;

			try
			{
				long long varsLL = ParseNumber();
				long long clssLL = ParseNumber();
				cout << "c topor_tool: suggested #variables : " << varsLL << "; suggested #clauses : " << clssLL << endl;
				if (varsLL > numeric_limits<TLit>::max() || varsLL <= 0)
				{
					cout << "c topor_tool warning: the suggested #variables " << varsLL << " is greater than the maximal number or is <=0, thus it will be ignored" << endl;
				}
				else
				{
					vars = (TLit)varsLL;
				}
			}
			catch (const logic_error& le)
			{
				cout << "c topor_tool ERROR: couldn't parse the p-line as 'p cnf <VARS> <CLSS>': " << le.what() << " at line number " << lineNum << endl;
				return BadRetVal;
			}
			catch (...)
			{
				cout << "c topor_tool ERROR: couldn't parse the p-line as 'p cnf <VARS> <CLSS>': couldn't read the variables or the clauses at line number " << lineNum << endl;
				return BadRetVal;
			}

			SkipWhitespaces();
			if (line[currLineI] != '\n')
			{
				cout << "c topor_tool ERROR: couldn't parse the p-line as 'p cnf <VARS> <CLSS>': new-line wasn't found where expected at line number " << lineNum << endl;
				return BadRetVal;
			}

			assert(topor == nullptr);
			if (CreateToporInst(vars) == BadRetVal)
			{
				return BadRetVal;
			}

			continue;
		}

		// If we're here and topor is still missing, create it without the number-of-variables hint -- no p-line is expected anymore
		if (topor == nullptr)
		{
			if (CreateToporInst() == BadRetVal)
			{
				return BadRetVal;
			}
		}

		vector<TLit> lits;

		auto BufferToLits = [&]()
		{
			string errorString = "";

			lits.clear();

			long long currLit = numeric_limits<long long>::max();
			while (currLit != 0)
			{
				try
				{
					currLit = ParseNumber();
					if (currLit > numeric_limits<TLit>::max() || currLit < numeric_limits<TLit>::min())
					{
						errorString = "c topor_tool ERROR: the literal " + to_string(currLit) + " is too big or too small\n";
						lits.clear();
						break;
					}
					lits.push_back(TLit(currLit));
				}
				catch (...)
				{
					errorString = "c topor_tool ERROR: couldn't translate the following line or parts of it into a vector of literals at line number " + to_string(lineNum) + "\n";
					lits.clear();
					break;
				}
			}

			return make_pair(errorString, lits);
		};

		if (line[currLineI] == 's')
		{
			++currLineI;
			SkipWhitespaces();

			auto [errString, assumps] = BufferToLits();
			if (!errString.empty())
			{
				cout << errString;
				return BadRetVal;
			}

			ret = topor->Solve(assumps, nextSolveToInSecIsCpuTime, nextSolveConfThr);
			nextSolveToInSecIsCpuTime = make_pair(numeric_limits<double>::max(), false);
			nextSolveConfThr = numeric_limits<uint64_t>::max();

			retValBasedOnLatestSolve = topor ? OnFinishingSolving(*topor, ret, printModel) : BadRetVal;

			if (verifyModel && retValBasedOnLatestSolve == 10)
			{
				if (VerifyModel(&assumps) == BadRetVal) return BadRetVal;
			}
			continue;
		}

		// New clause
		auto [errString, cls] = BufferToLits();
		if (!errString.empty())
		{
			cout << errString;
			return BadRetVal;
		}
		if (verifyModel)
		{
			vmClss.push_back(cls);
		}
		topor->AddClause(cls);
	}

	free(line);
	
	if (topor && topor->GetStatistics().m_SolveInvs == 0)
	{
		ret = topor->Solve();
		retValBasedOnLatestSolve = OnFinishingSolving(*topor, ret, printModel);
		if (verifyModel && retValBasedOnLatestSolve == 10)
		{
			if (VerifyModel() == BadRetVal) return BadRetVal;
		}
	}

	return retValBasedOnLatestSolve;
}
