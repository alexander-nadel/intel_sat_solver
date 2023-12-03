// Copyright(C) 2021-2023 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

#include <unordered_map>
#include <map>
#include <string>
#include <sstream>
#include <type_traits>
#include <functional>
#include <cmath>
#include <variant>

using namespace std;

namespace Topor
{
	// The number of parameter modes
	using TModeType = uint8_t;
	static constexpr TModeType m_Modes = 9;
	static string m_ModeParamName = "/mode/value";	

	class CTopiParams
	{
	public:
		CTopiParams() {}
		
		void SetParam(const string& paramName, double paramVal)
		{
			if (IsError())
			{
				return;
			}

			auto paramIt = m_Name2DescrUpdateGetval.find(paramName);

			if (paramIt == m_Name2DescrUpdateGetval.end())
			{
				SetError(TErrorType::NAME_DOESNT_EXIST_WHEN_SETTING_PARAMETER, "Parameter " + paramName + " doesn't exists. See all parameters description below:\n" + GetAllParamsDescr());
			}
			else
			{
				string errDescr = get<1>(paramIt->second)(paramVal);
				if (!errDescr.empty())
				{
					SetError(TErrorType::ERROR_WHEN_SETTING_PARAMETER, GetNameDescr(paramIt) + ": couldn't set this parameter : " + errDescr);
				}
				else if (paramName == m_ModeParamName)
				{
					m_Mode = (TModeType)paramVal;
				}
			}

			if (paramName == m_ModeParamName)
			{
				for (auto& nd : m_Name2DescrUpdateGetval)
				{
					if (nd.first != m_ModeParamName)
					{
						get<1>(nd.second)((TModeType)paramVal);
					}
				}
			}
		}

		bool IsError() const 
		{
			return m_ErrorCode != TErrorType::NO_ERR;
		}

		string GetErrorDescr() const
		{
			return m_ErrorDescr;
		}
		
		string GetAllParamsCurrValues()
		{
			bool headerPrinted = false;
			const bool firstInvocation = m_Name2PrevVal.empty();
			stringstream ss;			

			string prevClass = "";
			for (auto currParam : m_Name2DescrUpdateGetval)
			{
				const string& currName = currParam.first;
				const string currVal = get<2>(currParam.second)();

				if (!firstInvocation)
				{
					auto currParamName2PrevIt = m_Name2PrevVal.find(currName);
					if (currVal == currParamName2PrevIt->second)
					{
						continue;
					}					
					currParamName2PrevIt->second = currVal;
				}
				else
				{
					m_Name2PrevVal.insert(make_pair(currName, currVal));
				}				

				if (!headerPrinted)
				{
					ss << print_as_color<ansi_color_code::red>("c Parameter values:");
					if (!firstInvocation)
					{
						ss << print_as_color<ansi_color_code::red>(" (only the modified ones are printed)");
					}
					ss << endl;
					headerPrinted = true;
				}

				auto currClass = GetParamClass(currName);
				if (prevClass != currClass)
				{
					ss << SeparatorLine() << endl;
				}

				ss << "c " << print_as_color<ansi_color_code::magenta>(currName) << " " << currVal << endl;


				prevClass = move(currClass);
			}

			ss << SeparatorLine() << endl;

			return ss.str();

		}

		string GetAllParamsDescr() const
		{
			stringstream ss;

			ss << print_as_color<ansi_color_code::red>("c solver library parameters:") << endl;

			string prevClass = "";
			for (auto currParam : m_Name2DescrUpdateGetval)
			{
				auto currClass = GetParamClass(currParam.first);
				if (prevClass != currClass)
				{
					ss << SeparatorLine() << endl;
				}

				ss << "c " << print_as_color<ansi_color_code::magenta>(currParam.first) << " : " << get<0>(currParam.second) << endl;
				
				
				prevClass = move(currClass);
			}

			ss << SeparatorLine() << endl;

			return ss.str();
		}
	protected:
		inline string SeparatorLine() const { return "************************************************************"; }

		using TUpdateFunc = function<string(variant<double, TModeType> newValOrNewMode)>;
		using TGetValFunc = function<string()>;
		enum class TErrorType : uint8_t
		{
			NO_ERR = 0,
			NAME_EXISTS_WHEN_CREATING_PARAMETER = 1,
			NAME_DOESNT_EXIST_WHEN_SETTING_PARAMETER = 2,
			ERROR_WHEN_SETTING_PARAMETER = 3
		};
		
		TErrorType m_ErrorCode = TErrorType::NO_ERR;
		TModeType m_Mode = 0;
		string m_ErrorDescr;	

		// m_Name2DescrUpdateGetval.insert(make_pair(paramName, make_tuple(paramDescr, Update, GetVal)));
		map<string, tuple<string, TUpdateFunc, TGetValFunc>> m_Name2DescrUpdateGetval;
		unordered_map<string, string> m_Name2PrevVal;
		
		string GetParamClass(const string& paramName) const
		{
			auto firstSlashIt = paramName.find("/");
			auto secondSlashIt = paramName.find("/", 1);
			if (firstSlashIt == string::npos || secondSlashIt == string::npos)
			{
				return (string)"";
			}
			else
			{
				return paramName.substr(firstSlashIt, secondSlashIt - firstSlashIt);
			}
		}

		void NewParam(const string& paramName, const string& paramDescr, TUpdateFunc Update, TGetValFunc GetVal)
		{
			if (IsError())
			{
				return;
			}

			auto paramIt = m_Name2DescrUpdateGetval.find(paramName);

			if (paramIt != m_Name2DescrUpdateGetval.end())
			{	
				SetError(TErrorType::NAME_EXISTS_WHEN_CREATING_PARAMETER, "Parameter already exists; internal error; create Topor developers. The parameter: " + GetNameDescr(paramIt));
			}
			else
			{
				m_Name2DescrUpdateGetval.insert(make_pair(paramName, make_tuple(paramDescr, Update, GetVal)));
			}
		}

		template <class TIterator>
		string GetNameDescr(TIterator it)
		{
			return it->first + " : " + get<0>(it->second);
		}

		void SetError(TErrorType errCode, const string& errDescr)
		{
			m_ErrorCode = errCode;
			m_ErrorDescr = errDescr;
		}

		template <class T>
		friend class CTopiParam;
	};	

	template <class T>
	class CTopiParam
	{
	public:
		CTopiParam(CTopiParams& params, const string& paramName, const string& descr, T initVal, T minVal = numeric_limits<T>::lowest(), T maxVal = numeric_limits<T>::max()) : m_Val(initVal)
		{
			AssertMinMax(params, initVal, minVal, maxVal);

			if (params.IsError())
			{
				return;
			}

			m_Val = initVal;

			string updatedDescr = GetTypeName() + "; default = " + print_as_color<ansi_color_code::green>(Val2Str((T)initVal) + " in [" + Val2Str((T)minVal) + ", " + Val2Str((T)maxVal) + "]") + " : " + descr;

			params.NewParam(paramName, updatedDescr, [minVal, maxVal, this](variant<double, uint8_t> newValOrNewMode)
			{
				// newValOrNewMode.index() of 0 means that newValOrNewMode stores a double value (rather than a uint8_t mode)
				// We need to handle value setting only, since this constructor has one initial value for all modes, so mode setting has no impact
				if (newValOrNewMode.index() == 0)
				{
					return UpdateValue(get<double>(newValOrNewMode), minVal, maxVal);
				}
				else
				{
					string s = "";
					return s;
				}
			}, [this]()
			{
				return Val2Str(T(m_Val));
			});
		}

		CTopiParam(CTopiParams& params, const string& paramName, const string& descr, array<T, m_Modes> initValsPerMode, T minVal = numeric_limits<T>::min(), T maxVal = numeric_limits<T>::max())
		{
			for (auto initVal : initValsPerMode)
			{
				AssertMinMax(params, initVal, minVal, maxVal);
			}			
			
			if (params.IsError())
			{
				return;
			}

			// Picking the value of the default mode, since any mode change is expected to occur later anyways
			m_Val = initValsPerMode[0];

			string updatedDescr = GetTypeName() + "; default = " + print_as_color<ansi_color_code::green>(Val2Str(initValsPerMode) + " in [" + Val2Str(minVal) + ", " + Val2Str(maxVal) + "]") + " : " + descr;

			params.NewParam(paramName, updatedDescr, 
				[minVal, maxVal, initValsPerMode, this](variant<double, uint8_t> newValOrNewMode)
			{
				// newValOrNewMode.index() of 0 means that newValOrNewMode stores a double value (rather than a uint8_t mode)
				if (newValOrNewMode.index() == 0)
				{
					// Value
					return UpdateValue(get<double>(newValOrNewMode), minVal, maxVal);
				}
				else
				{
					// Mode
					return UpdateValue(initValsPerMode[get<uint8_t>(newValOrNewMode)], minVal, maxVal);
				}
			}, 
				[this]()
			{
				return Val2Str(T(m_Val));
			});
		}

		inline operator T() const { return m_Val; }

		inline static const string errValBelowMin = "the value is below the minimal value";
		inline static const string errValAboveMax = "the value is above the maximal value";
	protected:
		T m_Val;

		inline string GetTypeName() { return (string)typeid(T).name(); }

		string Val2Str(T anyVal)
		{
			if constexpr (!is_floating_point<T>::value)
			{
				return to_string(anyVal);
			}

			if (anyVal == numeric_limits<T>::min())
			{
				return "min(" + GetTypeName() + ")";
			}

			if (anyVal == numeric_limits<T>::lowest())
			{
				return "lowest(" + GetTypeName() + ")";
			}

			if (anyVal == numeric_limits<T>::max())
			{
				return "max(" + GetTypeName() + ")";
			}

			if (anyVal == numeric_limits<T>::epsilon())
			{
				return "epsilon(" + GetTypeName() + ")";
			}

			return to_string(anyVal);
		};

		string Val2Str(const array<T, m_Modes>& initVals)
		{
			string res = "{";

			for (auto anyVal : initVals)
			{
				res += Val2Str(anyVal);
				res += ", ";
			}

			res.pop_back();
			res.pop_back();
			res += "}";

			return res;
		};

		void TypeStaticAsserts()
		{
			static_assert(is_arithmetic<T>::value);
			// Integer's of greater than 32 bits aren't allowed, since all the values must fit into a double without loss of precision
			// Floating-point's must be at most as large as double's to fit into a double
			static_assert((is_integral<T>::value && sizeof(T) <= (sizeof(double) >> 1)) || (is_floating_point<T>::value && sizeof(T) <= sizeof(double)));
		}

		void AssertMinMax(CTopiParams& params, T initVal, T minVal, T maxVal)
		{
			if (initVal < minVal)
			{
				params.SetError(CTopiParams::TErrorType::ERROR_WHEN_SETTING_PARAMETER, errValBelowMin);
				return;
			}

			if (initVal > maxVal)
			{
				params.SetError(CTopiParams::TErrorType::ERROR_WHEN_SETTING_PARAMETER, errValAboveMax);
				return;
			}
		}

		string UpdateValue(const double newVal, T minVal, T maxVal)
		{
			if constexpr (is_integral<T>::value)
			{
				if (std::trunc(newVal) != newVal)
				{
					string s = "the value is " + to_string(newVal) + ", but it must be an integer";
					return s;
				}
			}

			m_Val = (T)newVal;

			if (newVal < (double)minVal)
			{
				string s = "the value " + to_string((int64_t)newVal) + " is below the minimal value";
				return s;
			}

			if (newVal > (double)maxVal)
			{
				string s = "the value " + to_string((int64_t)newVal) + " is above the maximal value";
				return s;
			}

			string s = "";
			return s;
		}
	};
}

