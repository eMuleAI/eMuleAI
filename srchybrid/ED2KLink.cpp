//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include <wininet.h>
#include "resource.h"
#include "opcodes.h"
#include "ED2KLink.h"
#include "SafeFile.h"
#include "StringConversion.h"
#include "preferences.h"
#include "ATLComTime.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace {
	inline unsigned int FromHexDigit(TCHAR digit) {
		switch (digit) {
		case _T('0'): return 0;
		case _T('1'): return 1;
		case _T('2'): return 2;
		case _T('3'): return 3;
		case _T('4'): return 4;
		case _T('5'): return 5;
		case _T('6'): return 6;
		case _T('7'): return 7;
		case _T('8'): return 8;
		case _T('9'): return 9;
		case _T('A'): return 10;
		case _T('B'): return 11;
		case _T('C'): return 12;
		case _T('D'): return 13;
		case _T('E'): return 14;
		case _T('F'): return 15;
		case _T('a'): return 10;
		case _T('b'): return 11;
		case _T('c'): return 12;
		case _T('d'): return 13;
		case _T('e'): return 14;
		case _T('f'): return 15;
		default: throw GetResString(_T("ERR_ILLFORMEDHASH"));
		}
	}
}

/////////////////////////////////////////////
// CED2KServerListLink implementation
/////////////////////////////////////////////
CED2KServerListLink::CED2KServerListLink(LPCTSTR address)
	: m_address(address)
{
}

void CED2KServerListLink::GetLink(CString &lnk) const
{
	lnk.Format(_T("ed2k://|serverlist|%s|/"), (LPCTSTR)m_address);
}

/////////////////////////////////////////////
// CED2KNodesListLink implementation
/////////////////////////////////////////////
CED2KNodesListLink::CED2KNodesListLink(LPCTSTR address)
	: m_address(address)
{
}

void CED2KNodesListLink::GetLink(CString &lnk) const
{
	lnk.Format(_T("ed2k://|nodeslist|%s|/"), (LPCTSTR)m_address);
}

/////////////////////////////////////////////
// CED2KServerLink implementation
/////////////////////////////////////////////
CED2KServerLink::CED2KServerLink(LPCTSTR ip, LPCTSTR port)
	: m_strAddress(ip)
{
	unsigned long uPort = _tcstoul(port, 0, 10);
	if (uPort > _UI16_MAX)
		throw GetResString(_T("ERR_BADPORT"));
	m_port = static_cast<uint16>(uPort);
	m_defaultName.Format(_T("Server %s:%s"), ip, port);
}

void CED2KServerLink::GetLink(CString &lnk) const
{
	lnk.Format(_T("ed2k://|server|%s|%u|/"), (LPCTSTR)GetAddress(), (unsigned)GetPort());
}


/////////////////////////////////////////////
// CED2KSearchLink implementation
/////////////////////////////////////////////
CED2KSearchLink::CED2KSearchLink(LPCTSTR pszSearchTerm)
	: m_strSearchTerm(OptUtf8ToStr(URLDecode(pszSearchTerm)))
{
}

void CED2KSearchLink::GetLink(CString &lnk) const
{
	lnk.Format(_T("ed2k://|search|%s|/"), (LPCTSTR)EncodeUrlUtf8(m_strSearchTerm));
}


/////////////////////////////////////////////
// CED2KFileLink implementation
/////////////////////////////////////////////
CED2KFileLink::CED2KFileLink(LPCTSTR pszName, LPCTSTR pszSize, LPCTSTR pszHash
		, const CStringArray &astrParams, LPCTSTR pszSources)
	: SourcesList()
	, m_hashset()
	, m_name(OptUtf8ToStr(URLDecode(pszName)).Trim())
	, m_size(pszSize)
	, m_bAICHHashValid()
{
	// Here we have a little problem. Actually the proper solution would be to decode from UTF-8,
	// only if the string does contain escape sequences. But if user pastes a raw UTF-8 encoded
	// string (for whatever reason), we would miss to decode that string. On the other side,
	// always decoding UTF-8 can give flaws in case the string is valid for Unicode and UTF-8
	// at the same time. However, to avoid the pasting of raw UTF-8 strings (which would lead
	// to a greater mess in the network) we always try to decode from UTF-8, even if the string
	// did not contain escape sequences.
	LPCTSTR uid = EMPTY;
	if (m_name.IsEmpty())
		uid = _T("ERR_NOTAFILELINK");
	else if (_tcslen(pszHash) != 32)
		uid = _T("ERR_ILLFORMEDHASH");
	else {
		const sint64 iSize = _tstoi64(pszSize);
		if (iSize <= 0)
			uid = _T("ERR_NOTAFILELINK");
		else if ((uint64)iSize > MAX_EMULE_FILE_SIZE)
			uid = _T("ERR_TOOLARGEFILE");
		else if ((uint64)iSize > OLD_MAX_EMULE_FILE_SIZE && !thePrefs.CanFSHandleLargeFiles(0))
			uid = _T("ERR_FSCANTHANDLEFILE");
		else if (!strmd4(pszHash, m_hash))
			uid = _T("ERR_ILLFORMEDHASH");
	}
	if (!IsEmpty(uid))
		throw GetResString(uid);

	bool bError = false;
	for (INT_PTR i = 0; !bError && i < astrParams.GetCount(); ++i) {
		const CString &strParam(astrParams[i]);
		ASSERT(!strParam.IsEmpty());

		CString strTok;
		int iPos = strParam.Find(_T('='));
		if (iPos >= 0)
			strTok = strParam.Left(iPos);
		switch (strTok[0]) {
		case _T('p'):
			{
				const CString &strPartHashes(strParam.Tokenize(_T("="), iPos));

				if (m_hashset != NULL) {
					ASSERT(0);
					bError = true;
					break;
				}

				m_hashset = new CSafeMemFile(256);
				m_hashset->WriteHash16(m_hash);
				m_hashset->WriteUInt16(0);

				uint16 iHashCount = 0;
				for (int jPos = 0;;) {
					const CString &strHash(strPartHashes.Tokenize(_T(":"), jPos));
					if (strHash.IsEmpty())
						break;
					uchar aucPartHash[MDX_DIGEST_SIZE];
					if (!strmd4(strHash, aucPartHash)) {
						bError = true;
						break;
					}
					m_hashset->WriteHash16(aucPartHash);
					++iHashCount;
				}
				if (!bError) {
					m_hashset->Seek(16, CFile::begin);
					m_hashset->WriteUInt16(iHashCount);
					m_hashset->Seek(0, CFile::begin);
				}
			}
			break;
		case _T('h'):
			if (strParam[iPos + 1]) { //not empty
				if (DecodeBase32(CPTR(strParam, iPos + 1), m_AICHHash.GetRawHash(), CAICHHash::GetHashSize()) == CAICHHash::GetHashSize()) {
					m_bAICHHashValid = true;
					ASSERT(m_AICHHash.GetString().CompareNoCase(CPTR(strParam, iPos + 1)) == 0);
					break;
				}
			}
		default:
			ASSERT(0);
		}
	}

	if (bError) {
		delete m_hashset;
		m_hashset = NULL;
	}

	if (!pszSources || !*pszSources)
		return;
	LPCTSTR pCh = pszSources;
	pCh = _tcsstr(pCh, _T("sources"));
	if (pCh == NULL)
		return;
	pCh += 7; // point to char after "sources"
	LPCTSTR pEnd = pCh; // make a pointer to the terminating NUL
	while (*pEnd)
		++pEnd;

	// if there's an expiration date...
	if (*pCh == _T('@')) {
		if (pEnd - pCh <= 7)
			return;
		TCHAR date[3];
		date[2] = 0; // terminate the string

		struct tm tmexp = {};
		date[0] = *++pCh;
		date[1] = *++pCh;
		tmexp.tm_year = (int)_tcstol(date, NULL, 10) + (2000 - 1900); //since 1900
		date[0] = *++pCh;
		date[1] = *++pCh;
		tmexp.tm_mon = (int)_tcstol(date, NULL, 10) - 1;
		date[0] = *++pCh;
		date[1] = *++pCh;
		tmexp.tm_mday = (int)_tcstol(date, NULL, 10);
		time_t tExpire = mktime(&tmexp);
		//no time zone information, assume UTC
		if (tExpire == (time_t)-1 || tExpire <= time(NULL))
			return;
		++pCh;
	}

	if (++pCh >= pEnd) //make pCh to point to the first "ip:port" and check for sources
		return;
	int nInvalid = 0;
	SourcesList = new CSafeMemFile(256);
	uint16 nCount = 0;
	SourcesList->WriteUInt16(nCount); // init to 0, we'll fix this at the end.
	// for each "ip:port" source string until the end
	// limit to prevent overflow (uint16 due to CPartFile::AddClientSources)
	while (*pCh != 0 && nCount < MAXSHORT) {
		LPCTSTR pIP = pCh;
		LPCTSTR pNext;
		// find the end of this ip:port string & start of next ip:port string.
		if ((pCh = _tcschr(pCh, _T(','))) != NULL)
			pNext = pCh++; // ends the current "ip:port" and point to next "ip:port"
		else
			pNext = pCh = pEnd;

		LPCTSTR pPort = _tcschr(pIP, _T(':'));
		// if port is not present for this ip, skip to the next ip
		if (pPort == NULL || pPort >= pNext) {
			++nInvalid;
			continue;
		}
		CStringA sIPa(pIP, static_cast<int>(pPort - pIP));
		++pPort;	// move to port string
		unsigned long uPort = _tcstoul(pPort, NULL, 10);
		// skip bad ips and ports
		if (!uPort || uPort > _UI16_MAX) {
			++nInvalid;
			continue;
		}
		unsigned long dwID = inet_addr(sIPa);
		if (dwID == INADDR_NONE) {	// host name?
			if (_tcslen(pIP) > 512) {
				++nInvalid;
				continue;
			}
			SUnresolvedHostname *hostname = new SUnresolvedHostname;
			hostname->strHostname = sIPa;
			hostname->nPort = static_cast<uint16>(uPort);
			m_HostnameSourcesList.AddTail(hostname);
			continue;
		}
		//TODO: This will filter out *.*.*.0 clients. Is there a nice way to fix?
		if (::IsLowID(dwID)) { // ip
			++nInvalid;
			continue;
		}

		SourcesList->WriteUInt32(dwID);
		SourcesList->WriteUInt16(static_cast<uint16>(uPort));
		SourcesList->WriteUInt32(0); // dwServerIP
		SourcesList->WriteUInt16(0); // nServerPort
		++nCount;
	}

	if (nCount) {
		SourcesList->SeekToBegin();
		SourcesList->WriteUInt16(nCount);
		SourcesList->SeekToBegin();
	} else {
		delete SourcesList;
		SourcesList = NULL;
	}
}

CED2KFileLink::~CED2KFileLink()
{
	delete SourcesList;
	while (!m_HostnameSourcesList.IsEmpty())
		delete m_HostnameSourcesList.RemoveHead();
	delete m_hashset;
}

void CED2KFileLink::GetLink(CString &lnk) const
{
	lnk.Format(_T("ed2k://|file|%s|%s|%s|/")
		, (LPCTSTR)EncodeUrlUtf8(m_name)
		, (LPCTSTR)m_size
		, (LPCTSTR)EncodeBase16(m_hash, 16));
}

CED2KLink* CED2KLink::CreateLinkFromUrl(LPCTSTR uri)
{
	CString strURI(uri);
	strURI.Trim(); // This function is used for various sources, trim the string again.
	int iPos = 0;
	CString strTok(GetNextString(strURI, _T('|'), iPos));
	if (strTok.CompareNoCase(_T("ed2k://")) == 0) {
		strTok = GetNextString(strURI, _T('|'), iPos);
		if (strTok == _T("file")) {
			const CString &strName(GetNextString(strURI, _T('|'), iPos));
			if (!strName.IsEmpty()) {
				const CString &strSize(GetNextString(strURI, _T('|'), iPos));
				if (!strSize.IsEmpty()) {
					const CString &strHash(GetNextString(strURI, _T('|'), iPos));
					if (!strHash.IsEmpty()) {
						CStringArray astrEd2kParams;
						bool bEmuleExt = false;
						CString strEmuleExt;

						CString strLastTok;
						while (!(strTok = GetNextString(strURI, _T('|'), iPos)).IsEmpty()) {
							strLastTok = strTok;
							if (strTok == _T("/")) {
								if (bEmuleExt)
									break;
								bEmuleExt = true;
							} else {
								if (bEmuleExt) {
									if (!strEmuleExt.IsEmpty())
										strEmuleExt += _T('|');
									strEmuleExt += strTok;
								} else
									astrEd2kParams.Add(strTok);
							}
						}

						if (strLastTok == _T("/"))
							return new CED2KFileLink(strName, strSize, strHash, astrEd2kParams, strEmuleExt);
					}
				}
			}
		} else if (strTok == _T("serverlist")) {
			const CString &strURL(GetNextString(strURI, _T('|'), iPos));
			if (!strURL.IsEmpty() && GetNextString(strURI, _T('|'), iPos) == _T("/"))
				return new CED2KServerListLink(strURL);
		} else if (strTok == _T("server")) {
			const CString &strServer(GetNextString(strURI, _T('|'), iPos));
			if (!strServer.IsEmpty()) {
				const CString &strPort(GetNextString(strURI, _T('|'), iPos));
				if (!strPort.IsEmpty() && GetNextString(strURI, _T('|'), iPos) == _T("/"))
					return new CED2KServerLink(strServer, strPort);
			}
		} else if (strTok == _T("nodeslist")) {
			const CString &strURL(GetNextString(strURI, _T('|'), iPos));
			if (!strURL.IsEmpty() && GetNextString(strURI, _T('|'), iPos) == _T("/"))
				return new CED2KNodesListLink(strURL);
		} else if (strTok == _T("search")) {
			const CString &strSearchTerm(GetNextString(strURI, _T('|'), iPos));
			// might be extended with more parameters in future versions
			if (!strSearchTerm.IsEmpty())
				return new CED2KSearchLink(strSearchTerm);
		}
		else if (strTok == _T("friend")) {
			CString sNick = GetNextString(strURI, _T("|"), iPos);
			if (!sNick.IsEmpty())
			{
				CString sHash = GetNextString(strURI, _T("|"), iPos);
				if (!sHash.IsEmpty() && GetNextString(strURI, _T("|"), iPos) == _T("/"))
					return new CED2KFriendLink(sNick, sHash);
			}
		} else if (strTok == _T("friendlist")) {
			CString sURL = GetNextString(strURI, _T("|"), iPos);
			if (!sURL.IsEmpty() && GetNextString(strURI, _T("|"), iPos) == _T("/"))
				return new CED2KFriendListLink(sURL);
		}
	} else {
		iPos = 0;
		if (GetNextString(strURI, _T('?'), iPos).Compare(_T("magnet:")) == 0) {
			CString strName, strSize, strHash, strEmuleExt;
			CStringArray astrEd2kParams;
			for (;;) {
				strTok = GetNextString(strURI, _T('&'), iPos);
				if (iPos < 0)
					return new CED2KFileLink(strName, strSize, strHash, astrEd2kParams, strEmuleExt);
				if (strTok[2] != _T('='))
					continue;
				const CString &strT(strTok.Left(2));
				strTok.Delete(0, 3);
				if (strT == _T("as")) { //acceptable source
					if (strTok.Left(7).CompareNoCase(_T("http://")) == 0)
						astrEd2kParams.Add(_T("s=") + strTok); //http source
				} else if (strT == _T("dn")) { //display name
					strName = strTok; //file name
				} else if (strT == _T("xl")) { //eXact length
					strSize = strTok; //file size
				} else if (strT == _T("xs") && strTok.Left(10) == _T("ed2kftp://")) {//eXact source
					strTok.Delete(0, 10);
					int i = strTok.Find(_T('/'));
					if (i > 0)
						astrEd2kParams.Add(_T("sources,") + strTok.Left(i)); //source IP:port
				} else if (strT == _T("xt")) {//eXact topic
					if (strTok.Left(9) == _T("urn:ed2k:"))
						strHash = strTok.Mid(9); //file ID
					else if (strTok.Left(13) == _T("urn:ed2khash:"))
						strHash = strTok.Mid(13); //file ID
					else if (strTok.Left(9) == _T("urn:aich:"))
						astrEd2kParams.Add(_T("h=") + strTok.Mid(9)); //AICH root hash
				}
			}
		}
	}

	throw GetResString(_T("ERR_NOSLLINK"));
}

CED2KFriendLink::CED2KFriendLink(LPCTSTR userName, LPCTSTR userHash)
{
	if (_tcslen(userHash) != 32)
		throw GetResString(_T("ERR_ILLFORMEDHASH"));

	m_sUserName = userName;

	for (int idx = 0; idx < 16; ++idx)
	{
		m_hash[idx] = (uchar)FromHexDigit(*userHash++) * 16;
		m_hash[idx] += (uchar)FromHexDigit(*userHash++);
	}
}

CED2KFriendLink::CED2KFriendLink(LPCTSTR userName, uchar userHash[])
{
	m_sUserName = userName;
	memcpy(m_hash, userHash, 16 * sizeof(uchar));
}

void CED2KFriendLink::GetLink(CString& lnk) const
{
	lnk = _T("ed2k://|friend|");
	lnk += m_sUserName + _T("|");
	for (int idx = 0; idx < 16; ++idx)
	{
		unsigned int ui1 = m_hash[idx] / 16;
		unsigned int ui2 = m_hash[idx] % 16;
		lnk += static_cast<TCHAR>(ui1 < 10 ? (_T('0') + ui1) : (_T('A') + (ui1 - 10)));
		lnk += static_cast<TCHAR>(ui2 < 10 ? (_T('0') + ui2) : (_T('A') + (ui2 - 10)));
	}
	lnk += _T("|/");
}

CED2KServerListLink* CED2KFriendLink::GetServerListLink()
{
	return NULL;
}

CED2KServerLink* CED2KFriendLink::GetServerLink()
{
	return NULL;
}

CED2KFileLink* CED2KFriendLink::GetFileLink()
{
	return NULL;
}

CED2KLink::LinkType CED2KFriendLink::GetKind() const
{
	return kFriend;
}

CED2KFriendListLink::CED2KFriendListLink(LPCTSTR address)
{
	m_address = address;
}

void CED2KFriendListLink::GetLink(CString& lnk) const
{
	lnk.Format(_T("ed2k://|friendlist|%s|/"), m_address);
}

CED2KServerListLink* CED2KFriendListLink::GetServerListLink()
{
	return NULL;
}

CED2KServerLink* CED2KFriendListLink::GetServerLink()
{
	return NULL;
}

CED2KFileLink* CED2KFriendListLink::GetFileLink()
{
	return NULL;
}

CED2KLink::LinkType CED2KFriendListLink::GetKind() const
{
	return kFriendList;
}