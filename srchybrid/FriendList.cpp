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
#include <io.h>
#include "emule.h"
#include "UpDownClient.h"
#include "Friend.h"
#include "FriendList.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "opcodes.h"
#include "emuledlg.h"
#include "FriendListCtrl.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define EMFRIENDS_MET_FILENAME	_T("emfriends.met")
#define EMFRIENDS_MET_FILENAME_TMP	_T("emfriends.met.tmp")

CFriendList::CFriendList()
	: m_wndOutput()
{
	LoadList();
	m_nLastSaved = ::GetTickCount();
}

CFriendList::~CFriendList()
{
	SaveList();
	while (!m_listFriends.IsEmpty())
		delete m_listFriends.RemoveHead();
}

bool CFriendList::LoadList()
{
	CSafeBufferedFile file;
	if (!CFileOpen(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + EMFRIENDS_MET_FILENAME
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite
		, GetResString(_T("ERR_READEMFRIENDS"))))
	{
		return false;
	}

	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER) {
			file.Close();
			return false;
		}

		for (uint32 i = file.ReadUInt32(); i > 0; --i) { //number of records
			CFriend *Record = new CFriend();
			Record->LoadFromFile(file);
			m_listFriends.AddTail(Record);
		}
		file.Close();
		return true;
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_EMFRIENDSINVALID")));
		else {
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_READEMFRIENDS")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
		}
		ex->Delete();
	}
	return false;
}

void CFriendList::SaveList()
{
	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving friends list file \"%s\""), EMFRIENDS_MET_FILENAME);
	m_nLastSaved = ::GetTickCount();

	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CSafeBufferedFile file;
	if (!CFileOpen(file
		, sConfDir + EMFRIENDS_MET_FILENAME_TMP
		, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to save ") EMFRIENDS_MET_FILENAME_TMP))
	{
		return;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

	try {
		file.WriteUInt8(MET_HEADER);
		file.WriteUInt32((uint32)m_listFriends.GetCount());
		for (POSITION pos = m_listFriends.GetHeadPosition(); pos != NULL;)
			m_listFriends.GetNext(pos)->WriteToFile(file);
		CommitAndClose(file);
		MoveFileEx(sConfDir + EMFRIENDS_MET_FILENAME_TMP, sConfDir + EMFRIENDS_MET_FILENAME, MOVEFILE_REPLACE_EXISTING);
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s%s"), _T("Failed to save ") EMFRIENDS_MET_FILENAME, (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
}

CFriend* CFriendList::SearchFriend(const uchar *abyUserHash, CAddress dwIP, uint16 nPort) const
{
	for (POSITION pos = m_listFriends.GetHeadPosition(); pos != NULL;) {
		CFriend *cur_friend = m_listFriends.GetNext(pos);
		// to avoid that unwanted clients become a friend, we have to distinguish between friends with
		// a userhash and of friends which are identified by IP+port only.
		if (abyUserHash != NULL && cur_friend->HasUserhash()) {
			// check for a friend which has the same userhash as the specified one
			if (md4equ(cur_friend->m_abyUserhash, abyUserHash))
				return cur_friend;
		} else {
			if (cur_friend->m_LastUsedIP == dwIP && !dwIP.IsNull() && cur_friend->m_nLastUsedPort == nPort && nPort != 0)
				return cur_friend;
		}
	}
	return NULL;
}

void CFriendList::RefreshFriend(CFriend *torefresh) const
{
	if (m_wndOutput)
		m_wndOutput->RefreshFriend(torefresh);
}

void CFriendList::ShowFriends() const
{
	if (!m_wndOutput) {
		ASSERT(0);
		return;
	}
	m_wndOutput->DeleteAllItems();
	for (POSITION pos = m_listFriends.GetHeadPosition(); pos != NULL;)
		m_wndOutput->AddFriend(m_listFriends.GetNext(pos));
	m_wndOutput->UpdateList();
}

//You can add a friend without an IP to allow the IRC to trade links with lowID users.
bool CFriendList::AddFriend(const uchar* abyUserhash, uint32 dwLastSeen, const CAddress& dwLastUsedIP, uint16 nLastUsedPort,
	uint32 dwLastChatted, LPCTSTR pszName, uint32 dwHasHash)
{
	// client must have an IP (HighID) or a hash
	// TODO: check if this can be switched to a hybridID so clients with *.*.*.0 can be added.
	if (dwLastUsedIP.GetType() == CAddress::IPv4 && IsLowID(dwLastUsedIP.ToUInt32(false)) && dwHasHash==0)
		return false;
	if (SearchFriend(abyUserhash, dwLastUsedIP, nLastUsedPort) != NULL)
		return false;
	CFriend *Record = new CFriend(abyUserhash, dwLastSeen, dwLastUsedIP, nLastUsedPort, dwLastChatted, pszName, dwHasHash);
	m_listFriends.AddTail(Record);
	ShowFriends();
	SaveList();
	return true;
}


bool CFriendList::IsAlreadyFriend(const CString &strUserHash) const
{
	for (POSITION pos = m_listFriends.GetHeadPosition(); pos != NULL;) {
		const CFriend *cur_friend = m_listFriends.GetNext(pos);
		if (cur_friend->HasUserhash() && strUserHash.Compare(md4str(cur_friend->m_abyUserhash)) == 0)
			return true;
	}
	return false;
}

bool CFriendList::AddFriend(CUpDownClient *toadd)
{
	if (toadd->IsFriend())
		return false;
	// client must have an IP (HighID) or a hash
	if (toadd->HasLowID() && !toadd->HasValidHash())
		return false;
	if (SearchFriend(toadd->GetUserHash(), toadd->GetIP(), toadd->GetUserPort()) != NULL)
		return false;

	CFriend *NewFriend = new CFriend(toadd);
	toadd->m_Friend = NewFriend;
	m_listFriends.AddTail(NewFriend);
	if (m_wndOutput) {
		m_wndOutput->AddFriend(NewFriend);
		m_wndOutput->UpdateList();
	}
	SaveList();
	NewFriend->FindKadID(); // fetch the Kad ID of this friend if we don't have it already
	return true;
}

void CFriendList::RemoveFriend(CFriend *todel)
{
	POSITION pos = m_listFriends.Find(todel);
	if (!pos) {
		ASSERT(0);
		return;
	}

	todel->SetLinkedClient(NULL);

	if (m_wndOutput)
		m_wndOutput->RemoveFriend(todel);
	m_listFriends.RemoveAt(pos);
	delete todel;
	SaveList();
	if (m_wndOutput)
		m_wndOutput->UpdateList();
}

void CFriendList::RemoveAllFriendSlots()
{
	for (POSITION pos = m_listFriends.GetHeadPosition(); pos != NULL;)
		m_listFriends.GetNext(pos)->SetFriendSlot(false);
}

void CFriendList::Process()
{
	if (::GetTickCount() >= m_nLastSaved + MIN2MS(19))
		SaveList();
}

bool CFriendList::IsValid(CFriend *pToCheck) const
{
	// debug/sanity check function
	return m_listFriends.Find(pToCheck) != NULL;
}

bool CFriendList::IsAlreadyFriend(uchar userHash[]) const
{
	CFriend* cur_friend;

	POSITION pos = m_listFriends.GetHeadPosition();
	while (pos != NULL)
	{
		cur_friend = m_listFriends.GetNext(pos);
		if (memcmp(cur_friend->m_abyUserhash, userHash, 16 * sizeof(uchar)) == 0)
			return true;
	}
	return false;
}

bool CFriendList::AddEmfriendsMetToList(const CString& strFile)
{
	CSafeBufferedFile file;
	CFileException ex;
	if (!file.Open(strFile, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary, &ex)) {
		if (ex.m_cause != CFileException::fileNotFound)
			AddLogLine(true, GetResString(_T("ERR_READEMFRIENDS")), (LPCTSTR)EscPercent(CExceptionStrDash(ex)));
		return false;
	}

	try	{
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER) {
			file.Close();
			return false;
		}

		UINT nRecordsNumber = file.ReadUInt32();
		for (UINT i = 0; i < nRecordsNumber; i++) {
			CFriend* Record = new CFriend();
			Record->LoadFromFile(file);
			if (!IsAlreadyFriend(Record->m_abyUserhash))
				m_listFriends.AddTail(Record);
		}
		file.Close();
	} catch (CFileException* ex) {
		if (ex->m_cause == CFileException::endOfFile)
			AddLogLine(true, GetResString(_T("ERR_EMFRIENDSINVALID")));
		else
			AddLogLine(true, GetResString(_T("ERR_READEMFRIENDS")), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
		return false;
	}

	return true;
}