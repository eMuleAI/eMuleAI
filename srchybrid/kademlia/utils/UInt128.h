/*
This file is part of eMule AI

Copyright (C)2003 Barry Dunne (https://www.emule-project.net)
Copyright (C)2026 eMule AI

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// Note To Mods //
/*
Please do not change anything here and release it.
There is going to be a new forum created just for the Kademlia side of the client.
If you feel there is an error or a way to improve something, please
post it in the forum first and let us look at it. If it is a real improvement,
it will be added to the official client. Changing something without knowing
what all it does, can cause great harm to the network if released in mass form.
Any mod that changes anything within the Kademlia side will not be allowed to advertise
their client on the eMule forum.
*/

#pragma once
#include "Opcodes.h"

namespace Kademlia
{
	class CUInt128
	{
	public:
		CUInt128() throw() : m_uData01(0ULL), m_uData23(0ULL) {} // netfinity: Shouldn't really initialize here but kept temporarily for backwards compability
		// netfinity: Special constructor, can be used to avoid copying of return values
		CUInt128(uint32 hhword, uint32 hlword, uint32 lhword, uint32 llword) throw() : m_uData0(hhword), m_uData1(hlword), m_uData2(lhword), m_uData3(llword) {}
	private:
		CUInt128(uint64 hword, uint64 lword) throw() : m_uData01(hword), m_uData23(lword) {}
	public:
		CUInt128(bool bFill) throw() : m_uData01(bFill ? ~(0ULL) : 0ULL), m_uData23(bFill ? ~(0ULL) : 0ULL) {}
		CUInt128(ULONG uValue) throw() : m_uData01(0ULL), m_uData23(static_cast<ULONGLONG>(uValue) << 32) {}
#if (defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM64)) && (_MSC_FULL_VER > 13009037)
		CUInt128(const byte* pbyValueBE) :
			m_uData0(_byteswap_ulong((reinterpret_cast<const ULONG*>(pbyValueBE))[0])),
			m_uData1(_byteswap_ulong((reinterpret_cast<const ULONG*>(pbyValueBE))[1])),
			m_uData2(_byteswap_ulong((reinterpret_cast<const ULONG*>(pbyValueBE))[2])),
			m_uData3(_byteswap_ulong((reinterpret_cast<const ULONG*>(pbyValueBE))[3]))
		{}
#else
		CUInt128(const byte* pbyValueBE);
#endif
		//Generates a new number, copying the most significant 'numBits' bits from 'value'.
		//The remaining bits are randomly generated.
		CUInt128(const CUInt128& uValue, UINT uNumBits/* = 128*/);
		// netfinity: Copy constructor - This one is extra fast as it is completly inline!!!
		CUInt128(const CUInt128& uValue) throw() : m_uData01(uValue.m_uData01), m_uData23(uValue.m_uData23) {}
		
		const byte* GetData() const { return (byte*)m_uData; };
		byte* GetDataPtr() const { return (byte*)m_uData; };
		/** Bit at position 0 being the most significant. */
		UINT	GetBitNumber(UINT uBit) const throw();
		int		CompareTo(const CUInt128 &uOther) const throw();
		int		CompareTo(ULONG uValue) const throw();
		void	ToHexString(CString &str) const throw();
		CString	ToHexString() const throw();
		const CString ToDecimalString() const;
		void	ToBinaryString(CString &str, bool bTrim = false) const throw();
		void	ToByteArray(byte *pby) const throw();
		ULONG	Get32BitChunk(int iVal) const throw();
		CUInt128& SetValue(const CUInt128 &uValue) throw();
		CUInt128& SetValue(ULONG uValue) throw();
		CUInt128& SetValueBE(const byte *pbyValueBE) throw();
		CUInt128& SetValueRandom() throw();
		CUInt128& SetValueGUID() throw();
		CUInt128& SetBitNumber(UINT uBit, UINT uValue) throw();
		CUInt128& ShiftLeft(UINT uBits) throw();
		CUInt128& Add(const CUInt128 &uValue) throw();
		CUInt128& Add(ULONG uValue) throw();
		CUInt128& Subtract(const CUInt128 &uValue);
		CUInt128& Subtract(ULONG uValue) throw();
		CUInt128& Xor(const CUInt128 &uValue) throw();
		CUInt128& XorBE(const byte *pbyValueBE) throw();
		CUInt128 operator~  () const throw();
		CUInt128 operator|  (const CUInt128& uValue) const throw();
		CUInt128 operator&  (const CUInt128& uValue) const throw();
		CUInt128 operator^  (const CUInt128& uValue) const throw();
		CUInt128& operator+ (const CUInt128 &uValue) throw();
		CUInt128& operator- (const CUInt128 &uValue) throw();
		CUInt128& operator= (const CUInt128 &uValue) throw();
		bool operator<  (const CUInt128 &uValue) const throw();
		bool operator>  (const CUInt128 &uValue) const throw();
		bool operator<= (const CUInt128 &uValue) const throw();
		bool operator>= (const CUInt128 &uValue) const throw();
		bool operator== (const CUInt128 &uValue) const throw();
		bool operator!= (const CUInt128 &uValue) const throw();
		CUInt128& operator+ (ULONG uValue);
		CUInt128& operator- (ULONG uValue);
		CUInt128& operator= (ULONG uValue) noexcept { SetValue(uValue); return *this; }
		bool operator<  (ULONG uValue) const throw() { return (CompareTo(uValue) < 0); }
		bool operator>  (ULONG uValue) const throw() { return (CompareTo(uValue) > 0); }
		bool operator<= (ULONG uValue) const throw() { return (CompareTo(uValue) <= 0); }
		bool operator>= (ULONG uValue) const throw() { return (CompareTo(uValue) >= 0); }
		bool operator== (ULONG uValue) const throw() { return (CompareTo(uValue) == 0); }
		bool operator!= (ULONG uValue) const throw() { return (CompareTo(uValue) != 0); }
		union
		{
			ULONG	m_uData[4];
			uint64	m_u64Data[2];
			// Constructors can't initialize arrays inline
			struct
			{
				ULONG	m_uData0;
				ULONG	m_uData1;
				ULONG	m_uData2;
				ULONG	m_uData3;
			};
			struct
			{
				ULONGLONG	m_uData01;
				ULONGLONG	m_uData23;
			};
		};
	};

	inline int CUInt128::CompareTo(const CUInt128& uOther) const throw()
	{
		for (int iIndex = 0; iIndex < 4; ++iIndex) {
			if (m_uData[iIndex] < uOther.m_uData[iIndex])
				return -1;
			if (m_uData[iIndex] > uOther.m_uData[iIndex])
				return 1;
		}
		return 0;
	}

	inline int CUInt128::CompareTo(ULONG uValue) const throw()
	{
		if ((m_u64Data[0] > 0) || (m_uData[2] > 0) || (m_uData[3] > uValue))
			return 1;
		return (m_uData[3] < uValue) ? -1 : 0;
	}

	inline void CUInt128::ToHexString(CString& str) const throw()
	{
		str = EMPTY;
		wchar_t element[10];
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 8; ++j)
			{
				TCHAR	const digit = static_cast<TCHAR>((m_uData[i] >> (j * 4)) & 0xF);
				if (digit < 10)
					element[7 - j] = _T('0') + digit;
				else
					element[7 - j] = _T('A') + (digit - TCHAR(10));
			}
			element[8] = _T('\0');
			str.Append(element);
		}
	}

	inline CString CUInt128::ToHexString() const throw()
	{
		CString str;
		wchar_t element[10];
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 8; ++j)
			{
				TCHAR	const digit = (TCHAR)(m_uData[i] >> (j * 4)) & 0xF;
				if (digit < 10)
					element[7 - j] = _T('0') + digit;
				else
					element[7 - j] = _T('A') + (digit - TCHAR(10));
			}
			element[8] = _T('\0');
			str.Append(element);
		}
		return str;
	}

	inline void CUInt128::ToBinaryString(CString& str, bool bTrim) const throw()
	{
		wchar_t element[130];
		int iBit, iStoreCnt = 0;
		for (int iIndex = 0; iIndex < 128; iIndex++)
		{
			iBit = GetBitNumber(iIndex);
			if ((!bTrim) || (iBit != 0))
			{
				// netfinity: Reduced CPU usage
				if (iBit == 1)
					//pstr->Append(_T("1") /*element*/);
					element[iStoreCnt++] = _T('1');
				else
					element[iStoreCnt++] = _T('0');
				bTrim = false;
			}
		}
		if (iStoreCnt == 0)
			element[iStoreCnt++] = _T('0');
		element[iStoreCnt] = _T('\0');
		str = element;
	}

	inline void CUInt128::ToByteArray(byte* pby) const throw()
	{
#if (_MSC_FULL_VER > 13009037) && (defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM64))
		((uint32*)pby)[0] = _byteswap_ulong(m_uData[0]);
		((uint32*)pby)[1] = _byteswap_ulong(m_uData[1]);
		((uint32*)pby)[2] = _byteswap_ulong(m_uData[2]);
		((uint32*)pby)[3] = _byteswap_ulong(m_uData[3]);
#else
		for (int iIndex = 0; iIndex < 16; ++iIndex)
			pby[iIndex] = (byte)(m_uData[iIndex / 4] >> (8 * (3 - (iIndex % 4))));
#endif
	}

	inline const CString CUInt128::ToDecimalString() const throw()
	{
		CString str;
		str.Format(_T("%lu%lu%lu%lu"), m_uData[0], m_uData[1], m_uData[2], m_uData[3]);
		str.TrimLeft(_T("0"));
		return str;
	}

	inline CUInt128& CUInt128::SetValue(const CUInt128 &uValue) throw()
	{
		m_uData01 = uValue.m_uData01;
		m_uData23 = uValue.m_uData23;
		return *this;
	}

	inline CUInt128& CUInt128::SetValue(ULONG uValue) throw()
	{
		m_uData01 = 0;
		m_uData[2] = 0;
		m_uData[3] = uValue;
		return *this;
	}

	inline CUInt128& CUInt128::Xor(const CUInt128& uValue) throw()
	{
		m_uData01 ^= uValue.m_uData01;
		m_uData23 ^= uValue.m_uData23;
		return *this;
	}

	inline CUInt128& CUInt128::XorBE(const byte* pbyValueBE) throw()
	{
		CUInt128 uValue(pbyValueBE);
		m_uData01 ^= uValue.m_uData01;
		m_uData23 ^= uValue.m_uData23;
		return *this;
	}

	inline CUInt128& CUInt128::Add(const CUInt128& uValue) throw()
	{
		if (uValue == 0)
			return *this;
		__int64 iSum = 0;
		for (int iIndex = 4; --iIndex >= 0;) {
			iSum += m_uData[iIndex];
			iSum += uValue.m_uData[iIndex];
			m_uData[iIndex] = (ULONG)iSum;
			iSum = iSum >> 32;
		}
		return *this;
	}

	inline CUInt128& CUInt128::Add(ULONG uValue) throw()
	{
		if (uValue == 0)
			return *this;

		CUInt128 nValue(uValue);
		__int64 iSum = 0;
		for (int iIndex = 4; --iIndex >= 0;) {
			iSum += m_uData[iIndex];
			iSum += nValue.m_uData[iIndex];
			m_uData[iIndex] = (ULONG)iSum;
			iSum = iSum >> 32;
		}
		return *this;
	}

	inline CUInt128& CUInt128::Subtract(const CUInt128& uValue) throw()
	{
		if (uValue != 0) {
			__int64 iSum = 0;
			for (int iIndex = 4; --iIndex >= 0;) {
				iSum += m_uData[iIndex];
				iSum -= uValue.m_uData[iIndex];
				m_uData[iIndex] = (ULONG)iSum;
				iSum = iSum >> 32;
			}
		}
		return *this;
	}

	inline CUInt128& CUInt128::Subtract(ULONG uValue) throw()
	{
		if (uValue == 0)
			return *this;

		CUInt128 nValue(uValue);
		__int64 iSum = 0;
		for (int iIndex = 4; --iIndex >= 0;) {
			iSum += m_uData[iIndex];
			iSum -= nValue.m_uData[iIndex];
			m_uData[iIndex] = (ULONG)iSum;
			iSum = iSum >> 32;
		}
		return *this;
	}

	inline ULONG CUInt128::Get32BitChunk(int iVal) const
	{
		return m_uData[iVal];
	}

	inline CUInt128& CUInt128::operator+ (const CUInt128& uValue) throw()
	{
		if (uValue == 0)
			return *this;
		__int64 iSum = 0;
		for (int iIndex = 4; --iIndex >= 0;) {
			iSum += m_uData[iIndex];
			iSum += uValue.m_uData[iIndex];
			m_uData[iIndex] = (ULONG)iSum;
			iSum = iSum >> 32;
		}
		return *this;
	}

	inline CUInt128& CUInt128::operator- (const CUInt128& uValue) throw()
	{
		if (uValue != 0) {
			__int64 iSum = 0;
			for (int iIndex = 4; --iIndex >= 0;) {
				iSum += m_uData[iIndex];
				iSum -= uValue.m_uData[iIndex];
				m_uData[iIndex] = (ULONG)iSum;
				iSum = iSum >> 32;
			}
		}
		return *this;
	}

	inline CUInt128& CUInt128::operator= (const CUInt128& uValue) throw()
	{
		m_uData01 = uValue.m_uData01;
		m_uData23 = uValue.m_uData23;
		return *this;
	}

	inline CUInt128 CUInt128::operator~ () const throw()
	{
		return CUInt128(~m_uData01, ~m_uData23);
	}

	inline CUInt128 CUInt128::operator& (const CUInt128& uValue) const throw()
	{
		return CUInt128(m_uData01 & uValue.m_uData01, m_uData23 & uValue.m_uData23);
	}

	inline CUInt128 CUInt128::operator| (const CUInt128& uValue) const throw()
	{
		return CUInt128(m_uData01 | uValue.m_uData01, m_uData23 | uValue.m_uData23);
	}

	inline CUInt128 CUInt128::operator^ (const CUInt128& uValue) const throw()
	{
		return CUInt128(m_uData01 ^ uValue.m_uData01, m_uData23 ^ uValue.m_uData23);
	}

	inline bool CUInt128::operator<  (const CUInt128& uValue) const throw()
	{
		if (m_uData[0] == uValue.m_uData[0])
			if (m_uData[1] == uValue.m_uData[1])
				if (m_uData[2] == uValue.m_uData[2])
					return (m_uData[3] < uValue.m_uData[3]);
				else
					return (m_uData[2] < uValue.m_uData[2]);
			else
				return (m_uData[1] < uValue.m_uData[1]);
		else
			return (m_uData[0] < uValue.m_uData[0]);
	}

	inline bool CUInt128::operator>  (const CUInt128& uValue) const throw()
	{
		if (m_uData[0] == uValue.m_uData[0])
			if (m_uData[1] == uValue.m_uData[1])
				if (m_uData[2] == uValue.m_uData[2])
					return (m_uData[3] > uValue.m_uData[3]);
				else
					return (m_uData[2] > uValue.m_uData[2]);
			else
				return (m_uData[1] > uValue.m_uData[1]);
		else
			return (m_uData[0] > uValue.m_uData[0]);
	}

	inline bool CUInt128::operator<= (const CUInt128& uValue) const throw()
	{
		if (m_uData[0] == uValue.m_uData[0])
			if (m_uData[1] == uValue.m_uData[1])
				if (m_uData[2] == uValue.m_uData[2])
					return (m_uData[3] <= uValue.m_uData[3]);
				else
					return (m_uData[2] < uValue.m_uData[2]);
			else
				return (m_uData[1] < uValue.m_uData[1]);
		else
			return (m_uData[0] < uValue.m_uData[0]);
	}

	inline bool CUInt128::operator>= (const CUInt128& uValue) const throw()
	{
		if (m_uData[0] == uValue.m_uData[0])
			if (m_uData[1] == uValue.m_uData[1])
				if (m_uData[2] == uValue.m_uData[2])
					return (m_uData[3] >= uValue.m_uData[3]);
				else
					return (m_uData[2] > uValue.m_uData[2]);
			else
				return (m_uData[1] > uValue.m_uData[1]);
		else
			return (m_uData[0] > uValue.m_uData[0]);
	}

	inline bool CUInt128::operator== (const CUInt128& uValue) const throw()
	{
		return ((m_uData01 ^ uValue.m_uData01 | m_uData23 ^ uValue.m_uData23) == 0ULL);
	}

	inline bool CUInt128::operator!= (const CUInt128& uValue) const throw()
	{
		return ((m_uData01 ^ uValue.m_uData01 | m_uData23 ^ uValue.m_uData23) != 0ULL);
	}

	inline CUInt128& CUInt128::operator+ (ULONG uValue) throw()
	{
		if (uValue == 0)
			return *this;

		CUInt128 nValue(uValue);
		__int64 iSum = 0;
		for (int iIndex = 4; --iIndex >= 0;) {
			iSum += m_uData[iIndex];
			iSum += nValue.m_uData[iIndex];
			m_uData[iIndex] = (ULONG)iSum;
			iSum = iSum >> 32;
		}
		return *this;
	}

	inline CUInt128& CUInt128::operator- (ULONG uValue) throw()
	{
		if (uValue == 0)
			return *this;

		CUInt128 nValue(uValue);
		__int64 iSum = 0;
		for (int iIndex = 4; --iIndex >= 0;) {
			iSum += m_uData[iIndex];
			iSum -= nValue.m_uData[iIndex];
			m_uData[iIndex] = (ULONG)iSum;
			iSum = iSum >> 32;
		}
		return *this;
	}
}
