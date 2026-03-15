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
#include <share.h>
#include <fcntl.h>
#include <io.h>
#include "emule.h"
#include "IPFilter.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "Preferences.h"
#include "emuledlg.h"
#include "Log.h"
#include "kademlia/utils/UInt128.h"
#include "eMuleAI/Address.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	DFLT_FILTER_LEVEL	100 // default filter level if non specified

CIPFilter::CIPFilter()
	: m_pLastHit()
	, m_bModified()
{
	LoadFromDefaultFile(false);
}

CIPFilter::~CIPFilter()
{
	if (m_bModified) {
		try {
			SaveToDefaultFile();
		} catch (const CString&) {
		}
	}
	RemoveAllIPFilters();
}

static int __cdecl CmpSIPFilterByStartAddr(const void* p1, const void* p2)
{
	const SIPFilter* rng1 = *(SIPFilter**)p1;
	const SIPFilter* rng2 = *(SIPFilter**)p2;
	return rng1->start.CompareTo(rng2->start);
}

static int __cdecl CompareByStartIP(const void *p1, const void *p2) noexcept
{
	return (*(SIPFilter**)p1)->start.CompareTo((*(SIPFilter**)p2)->start);
}

CString CIPFilter::GetDefaultFilePath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DFLT_IPFILTER_FILENAME;
}

const INT_PTR	CIPFilter::LoadFromDefaultFile(const bool bShowResponse)
{
	RemoveAllIPFilters();
	m_bModified = false;

	AddFromFileWhite(GetDefaultWhiteFilePath());
	AddFromFileStatic(GetDefaultStaticFilePath());
	LoadAllP2PFiles();

	return AddFromFile(GetDefaultFilePath(), bShowResponse);
}

// Finds and loads all file swith p2p extention in the config directory.
void CIPFilter::LoadAllP2PFiles()
{
	CString m_strPath;
	CFileFind current;
	BOOL bFound = current.FindFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("*.p2p"));
	if (bFound) {
		do {
			bFound = current.FindNextFile();
			if (!current.IsDirectory() && current.GetLength() > 0)
				AddFromFile(current.GetFilePath(), false);
		} while (bFound);
	}
}

// NOTE: Local helper for robust full-line reads without partial parses. It assembles a logical line by concatenating chunks until newline or EOF. 
// Returns true if any characters were read; outLine is trimmed of trailing CRLF.Comments are ASCII only for consistency.
static bool ReadWholeLine(FILE* fp, CStringA& outLine)
{
	outLine.Empty();
	char buf[1024];
	bool any = false;
	while (fgets(buf, _countof(buf), fp) != NULL) {
		any = true;
		outLine += buf;
		// if line ended, stop accumulating
		size_t len = outLine.GetLength();
		if (len > 0 && (outLine[len - 1] == '\n'))
			break;
	}
	// normalize line-endings and trim trailing spaces/tabs/CR/LF
	outLine.TrimRight(" \t\r\n");
	return any;
}

const INT_PTR CIPFilter::AddFromFile(const LPCTSTR pszFilePath, const bool bShowResponse)
{
	const DWORD dwStart = ::GetTickCount();
	FILE *readFile = _tfsopen(pszFilePath, _T("r"), _SH_DENYWR);
	if (readFile != NULL) {
		int iFoundRanges = 0;
		int iLine = 0;
		try {
			enum EIPFilterFileType
			{
				Unknown = 0,
				FilterDat = 1,		// ipfilter.dat/ip.prefix format
				PeerGuardian = 2,	// PeerGuardian text format
				// eMule AI: PeerGuardian2 v1, v2 and v3 doesn't support IPv6, so I just removed PeerGuardian2 support in this code.
				// v4 is still a draft by February 2026 and it aims to support IPv6. When it's released, v4 support should be added here.
			} eFileType = Unknown;

			::setvbuf(readFile, NULL, _IOFBF, 32768);
			TCHAR szNam[_MAX_FNAME];
			TCHAR szExt[_MAX_EXT];
			_tsplitpath(pszFilePath, NULL, NULL, szNam, szExt);
			if (_tcsicmp(szExt, _T(".p2p")) == 0 || (_tcsicmp(szNam, _T("guarding.p2p")) == 0 && _tcsicmp(szExt, _T(".txt")) == 0))
				eFileType = PeerGuardian;
			else if (_tcsicmp(szExt, _T(".prefix")) == 0)
				eFileType = FilterDat;

			CStringA sbuffer;
			CHAR szBuffer[1024]; // kept for DEBUG_ONLY logging paths

			// IMPORTANT: Use ReadWholeLine to prevent partial parsing and leaks on long lines.
			while (ReadWholeLine(readFile, sbuffer)) {
				++iLine;

				// ignore empty or too short lines first to avoid sbuffer[0] access on empty strings
				if (sbuffer.IsEmpty()) {
					DEBUG_ONLY(TRACE2("IP filter: ignored empty line %u\n", iLine));
					continue;
				}

				// ignore comments and obviously too short lines
				if (sbuffer.GetLength() < 15 || sbuffer[0] == '#' || sbuffer[0] == '/') {
					DEBUG_ONLY(TRACE2("IP filter: ignored line %u\n", iLine));
					continue;
				}

				if (eFileType == Unknown) {
					// try to detect format
					// looks like html
					if (sbuffer.Find('>') >= 0 && sbuffer.Find('<') >= 0)
						sbuffer.Delete(0, sbuffer.ReverseFind('>') + 1);

					// check for IPv4 or IPv6 range "IP - IP" at start of line
					UINT u1, u2, u3, u4, u5, u6, u7, u8;
					struct in6_addr startip, endip;
					if ((sscanf(sbuffer, "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u",
								&u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8) == 8) ||
						(sscanf(sbuffer, "%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx - %2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx",
								&startip.u.Byte[3], &startip.u.Byte[2], &startip.u.Byte[1], &startip.u.Byte[0],
								&startip.u.Byte[7], &startip.u.Byte[6], &startip.u.Byte[5], &startip.u.Byte[4],
								&startip.u.Byte[11], &startip.u.Byte[10], &startip.u.Byte[9], &startip.u.Byte[8],
								&startip.u.Byte[15], &startip.u.Byte[14], &startip.u.Byte[13], &startip.u.Byte[12],
								&endip.u.Byte[3], &endip.u.Byte[2], &endip.u.Byte[1], &endip.u.Byte[0],
								&endip.u.Byte[7], &endip.u.Byte[6], &endip.u.Byte[5], &endip.u.Byte[4],
								&endip.u.Byte[11], &endip.u.Byte[10], &endip.u.Byte[9], &endip.u.Byte[8],
								&endip.u.Byte[15], &endip.u.Byte[14], &endip.u.Byte[13], &endip.u.Byte[12]) == 32))
						eFileType = FilterDat;
					else {
						// check for "<desc> : IP - IP"
						int iColon = sbuffer.Find(':');
						if (iColon >= 0) {
							if ((sscanf(CPTRA(sbuffer, iColon + 1), "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u",
										&u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8) == 8) ||
								(sscanf(CPTRA(sbuffer, iColon + 1), "%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx - %2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx:%2hhx%2hhx",
										&startip.u.Byte[3], &startip.u.Byte[2], &startip.u.Byte[1], &startip.u.Byte[0],
										&startip.u.Byte[7], &startip.u.Byte[6], &startip.u.Byte[5], &startip.u.Byte[4],
										&startip.u.Byte[11], &startip.u.Byte[10], &startip.u.Byte[9], &startip.u.Byte[8],
										&startip.u.Byte[15], &startip.u.Byte[14], &startip.u.Byte[13], &startip.u.Byte[12],
										&endip.u.Byte[3], &endip.u.Byte[2], &endip.u.Byte[1], &endip.u.Byte[0],
										&endip.u.Byte[7], &endip.u.Byte[6], &endip.u.Byte[5], &endip.u.Byte[4],
										&endip.u.Byte[11], &endip.u.Byte[10], &endip.u.Byte[9], &endip.u.Byte[8],
										&endip.u.Byte[15], &endip.u.Byte[14], &endip.u.Byte[13], &endip.u.Byte[12]) == 32))
								eFileType = PeerGuardian;
						}
					}
				}

				bool bValid = false;
				uint32 level = 0;
				Kademlia::CUInt128 start, end;
				CStringA desc;
				if (eFileType == FilterDat)
					bValid = ParseIPFilterDatLine(sbuffer, start, end, level, desc);
				else if (eFileType == PeerGuardian)
					bValid = ParsePeerGuardianLine(sbuffer, start, end, level, desc);
				else
					bValid = false;
				// add a filter
				if (bValid) {
						AddIPRange(start, end, level, desc);
						++iFoundRanges;
				} else
					DEBUG_ONLY(TRACE2("IP filter: ignored line %u\n", iLine));
			}
		} catch (...) {
			AddDebugLogLine(false, _T("Exception when loading IP filters - %s"), _tcserror(errno));
			fclose(readFile);
			throw;
		}
		fclose(readFile);

		// sort the filter list by starting address of IP ranges
		qsort(m_iplist.GetData(), m_iplist.GetCount(), sizeof(m_iplist[0]), CompareByStartIP);

		// merge overlapping and adjacent filter ranges
		int iDuplicate = 0;
		int iMerged = 0;
		if (m_iplist.GetCount() >= 2) {
			// On large IP-filter lists there is a noticeable performance problem when merging the list.
			// The 'CIPFilterArray::RemoveAt' call is way too expensive to get called during the merging,
			// thus we use temporary helper arrays to copy only the entries into the final list which
			// are not get deleted.

			// Reserve a byte array (its used as a boolean array actually) as large as the current
			// IP-filter list, so we can set a 'to delete' flag for each entry in the current IP-filter list.
			char *pcToDelete = new char[m_iplist.GetCount()]{};
			int iNumToDelete = 0;

			SIPFilter *pPrv = m_iplist[0];
			for (INT_PTR i = 1; i < m_iplist.GetCount(); ++i) {
				SIPFilter *pCur = m_iplist[i];
				Kademlia::CUInt128 pPrvEndNext = pPrv->end;
				pPrvEndNext = pPrvEndNext+1; // CUInt128 doesn't support "pCur->start == pPrv->end + 1", it increments pPrv->end.
				if (pCur->start >= pPrv->start && pCur->start <= pPrv->end	 // overlapping
					|| pCur->start == pPrvEndNext && pCur->level == pPrv->level) { // adjacent
					if (pCur->start != pPrv->start || pCur->end != pPrv->end) { // don't merge identical entries
						//TODO: not yet handled, overlapping entries with different 'level'
						if (pCur->end > pPrv->end)
							pPrv->end = pCur->end;
						++iMerged;
					} else {
						// if we have identical entries, use the lowest 'level'
						if (pCur->level < pPrv->level)
							pPrv->level = pCur->level;
						++iDuplicate;
					}
					delete pCur;
					pcToDelete[i] = 1;		// mark this entry as 'to delete'
					++iNumToDelete;
				} else
					pPrv = pCur;
			}

			// Create new IP-filter list which contains only the entries from
			// the original IP-filter list which are not to be deleted.
			if (iNumToDelete > 0) {
				CIPFilterArray newList;
				int iNewListIndex = (int)(m_iplist.GetCount() - iNumToDelete);
				newList.SetSize(iNewListIndex);
				for (INT_PTR i = m_iplist.GetCount(); --i >= 0;)
					if (!pcToDelete[i])
						newList[--iNewListIndex] = m_iplist[i];

				ASSERT(!iNewListIndex); //everything has been copied

				// Replace current list with new list. Dump, but still fast enough (only 1 memcpy)
				m_iplist.RemoveAll();
				m_iplist.Append(newList);
				newList.RemoveAll();
				m_bModified = true;
			}
			delete[] pcToDelete;
		}

		AddLogLine(bShowResponse, GetResString(_T("IPFILTERLOADED2")), m_iplist.GetCount(), (LPCTSTR)EscPercent(pszFilePath));
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("Parsed lines/entries:%u  Found IP ranges:%u  Duplicate:%u  Merged:%u  Time:%s"), iLine, iFoundRanges, iDuplicate, iMerged, (LPCTSTR)CastSecondsToHM((::GetTickCount() - dwStart + 500) / 1000));
	}
	return m_iplist.GetCount();
}

void CIPFilter::SaveToDefaultFile()
{
	const CString &strFilePath(GetDefaultFilePath());
	FILE *fp = _tfsopen(strFilePath, _T("wt"), _SH_DENYWR);
	if (fp != NULL) {
		for (INT_PTR i = 0; i < m_iplist.GetCount(); ++i) {
			const SIPFilter *flt = m_iplist[i];
			CStringA szStart = (CStringA)ipstr(CAddress(flt->start, true));
			CStringA szEnd = (CStringA)ipstr(CAddress(flt->end, true));
			if (fprintf(fp, "%-15s - %-15s , %3u , %s\n", (LPCSTR)szStart, (LPCSTR)szEnd, flt->level, (LPCSTR)flt->desc) == 0 || ferror(fp)) {

				CString strError;
				strError.Format(_T("Failed to save IP filter to file \"%s\" - %s"), (LPCTSTR)strFilePath, _tcserror(errno));
				fclose(fp);
				throw strError;
			}
		}
		fclose(fp);
		m_bModified = false;
	} else {
		CString strError;
		strError.Format(_T("Failed to save IP filter to file \"%s\" - %s"), (LPCTSTR)strFilePath, _tcserror(errno));
		throw strError;
	}
}

bool CIPFilter::ParseIPFilterDatLine(const CStringA &sbuffer, Kademlia::CUInt128 &ip1, Kademlia::CUInt128 &ip2, uint32 &level, CStringA &desc)
{
	UINT uLevel = DFLT_FILTER_LEVEL;
	int iDescStart = 0;
	int iItems = 0;

	// Try IPv4 parsing first.
	UINT u1, u2, u3, u4, u5, u6, u7, u8;

	// Guard against extremely short lines early (defensive; upstream caller also checks).
	if (sbuffer.GetLength() < 3) {
		level = DFLT_FILTER_LEVEL;
		desc.Empty();
		return false;
	}

	iItems = sscanf(sbuffer, "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u , %3u , %n", &u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8, &uLevel, &iDescStart);
	if (iItems >= 8) {
		((BYTE*)&ip1)[15] = (BYTE)u1;
		((BYTE*)&ip1)[14] = (BYTE)u2;
		((BYTE*)&ip1)[13] = (BYTE)u3;
		((BYTE*)&ip1)[12] = (BYTE)u4;
		((BYTE*)&ip1)[11] = 0;
		((BYTE*)&ip1)[10] = 0;
		((BYTE*)&ip1)[9] = (BYTE)0xFF;
		((BYTE*)&ip1)[8] = (BYTE)0xFF;
		((BYTE*)&ip1)[7] = 0;
		((BYTE*)&ip1)[6] = 0;
		((BYTE*)&ip1)[5] = 0;
		((BYTE*)&ip1)[4] = 0;
		((BYTE*)&ip1)[3] = 0;
		((BYTE*)&ip1)[2] = 0;
		((BYTE*)&ip1)[1] = 0;
		((BYTE*)&ip1)[0] = 0;

		((BYTE*)&ip2)[15] = (BYTE)u5;
		((BYTE*)&ip2)[14] = (BYTE)u6;
		((BYTE*)&ip2)[13] = (BYTE)u7;
		((BYTE*)&ip2)[12] = (BYTE)u8;
		((BYTE*)&ip2)[11] = 0;
		((BYTE*)&ip2)[10] = 0;
		((BYTE*)&ip2)[9] = (BYTE)0xFF;
		((BYTE*)&ip2)[8] = (BYTE)0xFF;
		((BYTE*)&ip2)[7] = 0;
		((BYTE*)&ip2)[6] = 0;
		((BYTE*)&ip2)[5] = 0;
		((BYTE*)&ip2)[4] = 0;
		((BYTE*)&ip2)[3] = 0;
		((BYTE*)&ip2)[2] = 0;
		((BYTE*)&ip2)[1] = 0;
		((BYTE*)&ip2)[0] = 0;

		if (iItems == 8) {
			level = DFLT_FILTER_LEVEL;	// set default level
			return true;
		}
	} else { // IPv4 parsing failed. Try IPv6 parsing now.
		char m_acStart[40];
		char m_acEnd[40];
		iItems = sscanf(sbuffer, "%039s - %039s , %3u , %n", m_acStart, m_acEnd, &uLevel, &iDescStart);
		if (iItems < 2)
			return false; // ipfilter.dat line parsing failed.

		if (_inet_pton(AF_INET6, m_acStart, &ip1) != 1)
			return false;  // Start IPv6 parsing failed.

		if (_inet_pton(AF_INET6, m_acEnd, &ip2) != 1)
			return false;  // End IPv6 parsing failed.

		ip1 = ntohl(ip1);
		ip2 = ntohl(ip2);

		if (iItems == 2) {
			level = DFLT_FILTER_LEVEL;	// set default level
			return true;
		}
	}

	level = uLevel;
	if (iDescStart > 0) {
		LPCSTR pszDescStart = CPTRA(sbuffer, iDescStart);
		int iDescLen = sbuffer.GetLength() - iDescStart;
		while (iDescLen > 0 && pszDescStart[iDescLen - 1] < ' ') //any control characters
			--iDescLen;
		desc = CStringA(pszDescStart, iDescLen);
	}

	return true;
}

bool CIPFilter::ParsePeerGuardianLine(const CStringA &sbuffer, Kademlia::CUInt128 &ip1, Kademlia::CUInt128 &ip2, uint32 &level, CStringA &desc)
{
	int iPos = sbuffer.Find(':');
	if (iPos < 0)
		return false;

	desc = sbuffer.Left(iPos);
	desc.Replace("PGIPDB", "");
	desc.Trim();

	// Try IPv4 parsing first.
	unsigned u1, u2, u3, u4, u5, u6, u7, u8, u9, u10, u11, u12, u13, u14, u15, u16;
	if (sscanf(CPTRA(sbuffer, iPos + 1), "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u", &u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8) == 8) {
		((BYTE*)&ip1)[15] = (BYTE)u1;
		((BYTE*)&ip1)[14] = (BYTE)u2;
		((BYTE*)&ip1)[13] = (BYTE)u3;
		((BYTE*)&ip1)[12] = (BYTE)u4;
		((BYTE*)&ip1)[11] = 0;
		((BYTE*)&ip1)[10] = 0;
		((BYTE*)&ip1)[9] = (BYTE)0xFF;
		((BYTE*)&ip1)[8] = (BYTE)0xFF;
		((BYTE*)&ip1)[7] = 0;
		((BYTE*)&ip1)[6] = 0;
		((BYTE*)&ip1)[5] = 0;
		((BYTE*)&ip1)[4] = 0;
		((BYTE*)&ip1)[3] = 0;
		((BYTE*)&ip1)[2] = 0;
		((BYTE*)&ip1)[1] = 0;
		((BYTE*)&ip1)[0] = 0;

		((BYTE*)&ip2)[15] = (BYTE)u5;
		((BYTE*)&ip2)[14] = (BYTE)u6;
		((BYTE*)&ip2)[13] = (BYTE)u7;
		((BYTE*)&ip2)[12] = (BYTE)u8;
		((BYTE*)&ip2)[11] = 0;
		((BYTE*)&ip2)[10] = 0;
		((BYTE*)&ip2)[9] = (BYTE)0xFF;
		((BYTE*)&ip2)[8] = (BYTE)0xFF;
		((BYTE*)&ip2)[7] = 0;
		((BYTE*)&ip2)[6] = 0;
		((BYTE*)&ip2)[5] = 0;
		((BYTE*)&ip2)[4] = 0;
		((BYTE*)&ip2)[3] = 0;
		((BYTE*)&ip2)[2] = 0;
		((BYTE*)&ip2)[1] = 0;
		((BYTE*)&ip2)[0] = 0;
	} else { // IPv4 parsing failed. Try IPv6 parsing now.
		char m_acStart[40];
		char m_acEnd[40];
		if (sscanf(CPTRA(sbuffer, iPos + 1), "%039s - %039s", m_acStart, m_acEnd) != 2)
			return false; // PeerGuardian line parsing failed.

		if (_inet_pton(AF_INET6, m_acStart, &ip1) != 1)
			return false;  // Start IPv6 parsing failed.

		if (_inet_pton(AF_INET6, m_acEnd, &ip2) != 1)
			return false;  // End IPv6 parsing failed.

		ip1 = ntohl(ip1);
		ip2 = ntohl(ip2);
	}

	level = DFLT_FILTER_LEVEL;
	return true;
}

void CIPFilter::RemoveAllIPFilters()
{
	for (INT_PTR i = m_iplist.GetCount(); --i >= 0;)
	// Deleting the description-String can throw an exception
	{

		try	{
			delete m_iplist[i];
		}
		catch (...)	{
			//nothing
		}
	}

	m_iplist.RemoveAll();

	for (int i = 0; i < m_iplist_White.GetCount(); i++)
	{
		try	{
			delete m_iplist_White[i];
		}
		catch (...)	{
			//do nothing
		}
	}
	m_iplist_White.RemoveAll();

	m_pLastHit = NULL;
}

static int __cdecl CmpSIPFilterByAddr(const void *pvKey, const void *pvElement) noexcept
{
	Kademlia::CUInt128 ip = *(Kademlia::CUInt128*)pvKey;
	const SIPFilter *pIPFilter = *(SIPFilter**)pvElement;

	if (ip < pIPFilter->start)
		return -1;
	if (ip > pIPFilter->end)
		return 1;
	return 0;
}

const bool CIPFilter::IsFiltered(uint32 dwIP) /*const*/
{
	return IsFiltered(CAddress(dwIP, false));
}

const bool CIPFilter::IsFiltered(const CAddress& IP) /*const*/
{
	if (IP.IsNull() || m_iplist.IsEmpty() || (thePrefs.GetDontFilterPrivateIPs() && !IP.IsPublicIP()))
		return false;

	const Kademlia::CUInt128 IP128 = ntohl(IP.ToUInt128(false));
	const uint32 m_uLevel = thePrefs.GetIPFilterLevel();

	// to speed things up we use a binary search
	//	*)	the IP filter list must be sorted by IP range start addresses
	//	*)	the IP filter list is not allowed to contain overlapping IP ranges (see also the IP range merging code when
	//		loading the list)
	//	*)	the filter 'level' is ignored during the binary search and is evaluated only for the found element
	//
	// TODO: this can still be improved even more:
	//	*)	use a pre-assembled list of IP ranges which contains only the IP ranges for the currently used filter level
	//	*)	use a dumb plain array for storing the IP range structures. this will give more cache hits when processing
	//		the list. but(!) this would require to also use a dumb SIPFilter structure (don't use data items with ctors).
	//		otherwise the creation of the array would be rather slow.

	if (m_iplist_White.GetCount() > 0) {
		SIPFilter** ppFound_White = (SIPFilter**)bsearch(&IP128, m_iplist_White.GetData(), m_iplist_White.GetCount(), sizeof(m_iplist_White[0]), CmpSIPFilterByAddr);
		if (ppFound_White && (*ppFound_White)->level < m_uLevel)
		{
			(*ppFound_White)->hits++;
			if (thePrefs.GetVerbose() && thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("Prevented filtering IP %s in range: %s - %s Description: %s Hits: %u"), ipstr(CAddress(IP128, true)), ipstr(CAddress((*ppFound_White)->start, true)), ipstr(CAddress((*ppFound_White)->end, true)), (LPCTSTR)EscPercent(CString((*ppFound_White)->desc)), (*ppFound_White)->hits);
			return false;
		}
	}

	SIPFilter **ppFound = (SIPFilter**)bsearch(&IP128, m_iplist.GetData(), m_iplist.GetCount(), sizeof m_iplist[0], CmpSIPFilterByAddr);
	if (ppFound && (*ppFound)->level < m_uLevel) {
		(*ppFound)->hits++;
		m_pLastHit = *ppFound;
		return true;
	}

	return false;
}

const CString CIPFilter::GetLastHit() const
{
	return CString(m_pLastHit ? m_pLastHit->desc : "Not available");
}

const CIPFilterArray& CIPFilter::GetIPFilter() const
{
	return m_iplist;
}

bool CIPFilter::RemoveIPFilter(const SIPFilter *pFilter)
{
	for (INT_PTR i = m_iplist.GetCount(); --i >= 0;)
		if (m_iplist[i] == pFilter) {
			delete m_iplist[i];
			m_iplist.RemoveAt(i);
			return true;
		}

	return false;
}

void CIPFilter::AddFromFileStatic(const LPCTSTR pszFilePath)
{
	FILE* readFile = _tfsopen(pszFilePath, _T("r"), _SH_DENYWR);
	if (readFile != NULL)
	{
		_setmode(fileno(readFile), _O_TEXT);

		int iLine = 0;
		CStringA sbuffer;
		CHAR szBuffer[1024];
		while (fgets(szBuffer, _countof(szBuffer), readFile) != NULL)
		{
			iLine++;
			sbuffer = szBuffer;

			// ignore comments & too short lines
			if (sbuffer.GetAt(0) == '#' || sbuffer.GetAt(0) == '/' || sbuffer.GetLength() < 5) {
				sbuffer.Trim(" \t\r\n");
				DEBUG_ONLY2((!sbuffer.IsEmpty()) ? TRACE2("IP filter (static): ignored line %u\n", iLine) : 0);
				continue;
			}

			bool bValid = false;
			Kademlia::CUInt128 start = Kademlia::CUInt128(0ul);
			Kademlia::CUInt128 end = Kademlia::CUInt128(0ul);
			uint32 level = 0;
			CStringA desc;
			bValid = ParseIPFilterDatLine(sbuffer, start, end, level, desc);

			// add a filter
			if (bValid)	{
					AddIPRange(start, end, level, desc);
				DEBUG_ONLY(TRACE2("Added Static Entry - start: %u end: %u level: %u desc: %s\n", start, end, level, desc));
			} else {
				sbuffer.Trim(" \t\r\n");
				DEBUG_ONLY((!sbuffer.IsEmpty()) ? TRACE2("IP filter (static): ignored line %u\n", iLine) : 0);
			}
		}
		fclose(readFile);

		// sort the IP filter list by IP range start addresses
		qsort(m_iplist.GetData(), m_iplist.GetCount(), sizeof(m_iplist[0]), CmpSIPFilterByStartAddr);

		// merge overlapping and adjacent filter ranges
		if (m_iplist.GetCount() >= 2) {
			// On large IP-filter lists there is a noticeable performance problem when merging the list.
			// The 'CIPFilterArray::RemoveAt' call is way too expensive to get called during the merging,
			// thus we use temporary helper arrays to copy only the entries into the final list which
			// are not get deleted.

			// Reserve a byte array (its used as a boolean array actually) as large as the current 
			// IP-filter list, so we can set a 'to delete' flag for each entry in the current IP-filter list.
			char* pcToDelete = new char[m_iplist.GetCount()];
			memset(pcToDelete, 0, m_iplist.GetCount());
			int iNumToDelete = 0;

			SIPFilter* pPrv = m_iplist[0];
			int i = 1;
			while (i < m_iplist.GetCount())	{
				SIPFilter* pCur = m_iplist[i];
				Kademlia::CUInt128 pPrvEndNext = pPrv->end;
				pPrvEndNext = pPrvEndNext+1; // CUInt128 doesn't support "pCur->start == pPrv->end + 1", it increments pPrv->end.
				if (pCur->start >= pPrv->start && pCur->start <= pPrv->end	 // overlapping
					|| pCur->start == pPrvEndNext && pCur->level == pPrv->level) { // adjacent
					if (pCur->start != pPrv->start || pCur->end != pPrv->end) { // don't merge identical entries
						//TODO: not yet handled, overlapping entries with different 'level'
						if (pCur->end > pPrv->end)
							pPrv->end = pCur->end;
					} else {
						// if we have identical entries, use the lowest 'level'
						if (pCur->level < pPrv->level)
							pPrv->level = pCur->level;
					}
					delete pCur;
					pcToDelete[i] = 1;		// mark this entry as 'to delete'
					iNumToDelete++;
					i++;
					continue;
				}
				pPrv = pCur;
				i++;
			}

			// Create new IP-filter list which contains only the entries from the original IP-filter list
			// which are not to be deleted.
			if (iNumToDelete > 0) {
				CIPFilterArray newList;
				newList.SetSize(m_iplist.GetCount() - iNumToDelete);
				int iNewListIndex = 0;
				for (int i = 0; i < m_iplist.GetCount(); i++) {
					if (!pcToDelete[i])
						newList[iNewListIndex++] = m_iplist[i];
				}
				ASSERT(iNewListIndex == newList.GetSize());

				// Replace current list with new list. Dump, but still fast enough (only 1 memcpy)
				m_iplist.RemoveAll();
				m_iplist.Append(newList);
				newList.RemoveAll();
				m_bModified = true;
			}
			delete[] pcToDelete;
		}
	}
	return;
}

const CString CIPFilter::GetDefaultStaticFilePath() const
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DFLT_STATIC_IPFILTER_FILENAME;
}

void CIPFilter::AddFromFileWhite(const LPCTSTR pszFilePath)
{
	FILE* readFile = _tfsopen(pszFilePath, _T("r"), _SH_DENYWR);
	if (readFile != NULL) {
		_setmode(fileno(readFile), _O_TEXT);

		int iLine = 0;
		CStringA sbuffer;
		CHAR szBuffer[1024];
		while (fgets(szBuffer, _countof(szBuffer), readFile) != NULL) {
			iLine++;
			sbuffer = szBuffer;

			// ignore comments & too short lines
			if (sbuffer.GetAt(0) == '#' || sbuffer.GetAt(0) == '/' || sbuffer.GetLength() < 5) {
				sbuffer.Trim(" \t\r\n");
				DEBUG_ONLY2((!sbuffer.IsEmpty()) ? TRACE2("IP filter (white): ignored line %u\n", iLine) : 0);
				continue;
			}

			bool bValid = false;
			Kademlia::CUInt128 start = Kademlia::CUInt128(0ul);
			Kademlia::CUInt128 end = Kademlia::CUInt128(0ul);
			uint32 level = 0;
			CStringA desc;
			bValid = ParseIPFilterDatLine(sbuffer, start, end, level, desc);

			// add a filter
			if (bValid) {
				AddIPRangeWhite(start, end, level, desc);
				DEBUG_ONLY(TRACE2("Added White Entry - start: %u end: %u level: %u desc: %s\n", start, end, level, desc));
			} else {
				sbuffer.Trim(" \t\r\n");
				DEBUG_ONLY((!sbuffer.IsEmpty()) ? TRACE2("IP filter (white list): ignored line %u\n", iLine) : 0);
			}
		}
		fclose(readFile);

		if (m_iplist_White.GetCount() > 0) {
			AddLogLine(false, GetResString(_T("IPFILTERWHITELOADED")), m_iplist_White.GetCount());

			// sort the IP filter list by IP range start addresses
			qsort(m_iplist_White.GetData(), m_iplist_White.GetCount(), sizeof(m_iplist_White[0]), CmpSIPFilterByStartAddr);
		}

		// merge overlapping and adjacent filter ranges
		if (m_iplist_White.GetCount() >= 2) {
			// On large IP-filter lists there is a noticeable performance problem when merging the list.
			// The 'CIPFilterArray::RemoveAt' call is way too expensive to get called during the merging,
			// thus we use temporary helper arrays to copy only the entries into the final list which
			// are not get deleted.

			// Reserve a byte array (its used as a boolean array actually) as large as the current 
			// IP-filter list, so we can set a 'to delete' flag for each entry in the current IP-filter list.
			char* pcToDelete = new char[m_iplist_White.GetCount()];
			memset(pcToDelete, 0, m_iplist_White.GetCount());
			int iNumToDelete = 0;

			SIPFilter* pPrv = m_iplist_White[0];
			int i = 1;
			while (i < m_iplist_White.GetCount()) {
				SIPFilter* pCur = m_iplist_White[i];
				Kademlia::CUInt128 pPrvEndNext = pPrv->end;
				pPrvEndNext = pPrvEndNext+1; // CUInt128 doesn't support "pCur->start == pPrv->end + 1", it increments pPrv->end.
				if (pCur->start >= pPrv->start && pCur->start <= pPrv->end	 // overlapping
					|| pCur->start == pPrvEndNext && pCur->level == pPrv->level) { // adjacent
					if (pCur->start != pPrv->start || pCur->end != pPrv->end) {// don't merge identical entries
						//TODO: not yet handled, overlapping entries with different 'level'
						if (pCur->end > pPrv->end)
							pPrv->end = pCur->end;
					} else {
						// if we have identical entries, use the lowest 'level'
						if (pCur->level < pPrv->level)
							pPrv->level = pCur->level;
					}
					delete pCur;
					pcToDelete[i] = 1;		// mark this entry as 'to delete'
					iNumToDelete++;
					i++;
					continue;
				}
				pPrv = pCur;
				i++;
			}

			// Create new IP-filter list which contains only the entries from the original IP-filter list
			// which are not to be deleted.
			if (iNumToDelete > 0) {
				CIPFilterArray newList;
				newList.SetSize(m_iplist_White.GetCount() - iNumToDelete);
				int iNewListIndex = 0;
				for (int i = 0; i < m_iplist_White.GetCount(); i++) {
					if (!pcToDelete[i])
						newList[iNewListIndex++] = m_iplist_White[i];
				}
				ASSERT(iNewListIndex == newList.GetSize());

				// Replace current list with new list. Dump, but still fast enough (only 1 memcpy)
				m_iplist_White.RemoveAll();
				m_iplist_White.Append(newList);
				newList.RemoveAll();
				m_bModified = true;
			}
			delete[] pcToDelete;
		}
	}

	return;
}

const CString CIPFilter::GetDefaultWhiteFilePath() const
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DFLT_WHITE_IPFILTER_FILENAME;
}
