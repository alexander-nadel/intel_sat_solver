// Copyright(C) 2021 Intel Corporation
// SPDX - License - Identifier: MIT

#pragma once

// The class is to be used for setting a variable to the value inScopeValue for the whole life span of the declared variable of CSetInScope
// The variable is set to outOfScopeValue once it goes out-of-scope

template <class T>
class CSetInScope 
{
public:	
	CSetInScope(T& r, const T inScopeValue, const T outOfScopeValue) : m_Reference(r), m_OutOfScopeValue(outOfScopeValue), m_DontRestore(false)
	{
		m_Reference = inScopeValue;
	}

	CSetInScope(T& r, const T inScopeValue) : m_Reference(r), m_OutOfScopeValue(m_Reference), m_DontRestore(false)
	{
		m_Reference = inScopeValue;
	}

	CSetInScope(T& r) : m_Reference(r), m_OutOfScopeValue(r), m_DontRestore(false) {}

	const T& GetOutOfScopeVal() { return m_OutOfScopeValue; }

	~CSetInScope()
	{
		if (!m_DontRestore) m_Reference = m_OutOfScopeValue;
	}

	void DontRestore() { m_DontRestore = true; }
protected:
	T& m_Reference;
	const T m_OutOfScopeValue;
	bool m_DontRestore;
};

template <class T = void, class F = std::function<void()>>
class CApplyFuncOnExitFromScope
{
public:
	CApplyFuncOnExitFromScope(bool isReallyApply, T& i, F Func) : m_I(i), M_F(Func), m_IsExitVal(false), m_IsReallyApply(isReallyApply) {}
	CApplyFuncOnExitFromScope(T& i, F Func) : m_I(i), M_F(Func), m_IsExitVal(false), m_IsReallyApply(true) {}
	CApplyFuncOnExitFromScope(T& i, F Func, const T exitVal) : m_I(i), M_F(Func), m_ExitVal(exitVal), m_IsExitVal(true), m_IsReallyApply(true) {}
	void SetReallyApply(bool isReallyApply) { m_IsReallyApply = isReallyApply; }
	~CApplyFuncOnExitFromScope() 
	{ 
		if (m_IsReallyApply)
		{
			M_F(m_I);
			if (m_IsExitVal) m_I = m_ExitVal;
		}		
	}	
protected:
	T& m_I;
	F M_F;
	T m_ExitVal;
	bool m_IsExitVal;
	bool m_IsReallyApply;
};

template <class F>
class CApplyFuncOnExitFromScope<void, F>
{
public:
	CApplyFuncOnExitFromScope(bool isReallyApply, F Func) : M_F(Func), m_IsReallyApply(isReallyApply) {}
	CApplyFuncOnExitFromScope(F Func) : M_F(Func), m_IsReallyApply(true) {}
	void SetReallyApply(bool isReallyApply) { m_IsReallyApply = isReallyApply; }
	~CApplyFuncOnExitFromScope()
	{
		if (m_IsReallyApply)
		{
			M_F();
		}
	}
protected:
	F M_F;
	bool m_IsReallyApply;
};
