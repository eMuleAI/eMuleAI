//This file is part of eMule AI
//Copyright (C)2026 eMule AI

// Shield = Customizable Leecher Protection
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"
#include "resource.h"
#include "emule.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "UpdownClient.h"
#include "Packets.h"
#include "ClientCredits.h"
#include "UploadQueue.h"
#include "Statistics.h"
#include <regex>
#include "Shield.h"
#include "eMuleAI/AntiNick.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CShield::CShield()
{
	LoadShieldFile();
	m_CategoryPunishmentMap.InitHashTable(53);
	FillCategoryPunishmentMap();
}

CShield::~CShield()
{
}

void CShield::LoadShieldFile() {
	// This member function will be called by the reload button on the settings screen. So we need to clean all lists first.
	HardLeecherModNamesList.RemoveAll();
	SoftLeecherModNamesList.RemoveAll();
	HardLeecherUserNamesList.RemoveAll();
	SoftLeecherUserNamesList.RemoveAll();
	SoftLeecherVeryCDUserNamesList.RemoveAll();
	HardLeecherUserHashesList.RemoveAll();
	SoftLeecherUserHashesList.RemoveAll();
	UnknownHelloTagsList.RemoveAll();
	UnknownInfoTagsList.RemoveAll();
	SpamMessagesList.RemoveAll();

	m_bShieldFileLoaded = false;
	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CString strFullPath;
	strFullPath.Format(_T("%s") SHIELDFILE, (LPCTSTR)sConfDir);
	
	if (!::PathFileExists(strFullPath)) {
		LogWarning(GetResString(_T("FILE_NOT_FOUND")), (LPCTSTR)SHIELDFILE);
		return;
	}

	CStdioFile ShieldFile;
	// check for BOM open the text file either as ANSI (text) or Unicode (binary), this way we can read old and new files with almost the same code.
	if (ShieldFile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (IsUnicodeFile(strFullPath) ? CFile::typeBinary : 0))) {
		try {
			if (IsUnicodeFile(strFullPath)) // check for BOM
				ShieldFile.Seek(sizeof(WORD), CFile::begin); // skip BOM

			CString m_strCurrentLine = NULL;
			uint8 m_uMod = 0;
			while (ShieldFile.ReadString(m_strCurrentLine)) {
				CString m_strCurrentLineForLog = (LPCTSTR)EscPercent(m_strCurrentLine.Trim());

				if (m_strCurrentLine.TrimLeft()[0] == _T('#') || m_strCurrentLine.Trim(_T("\t\r\n")).IsEmpty()) // Ignore empty or commented out lines with #
					continue;
				else if (m_strCurrentLine.Left(48) == _T("<<< Hard Leecher Mod Name and Client Version >>>")) {
					m_uMod = 1;
					continue;
				} else if (m_strCurrentLine.Left(48) == _T("<<< Soft Leecher Mod Name and Client Version >>>")) {
					m_uMod = 2;
					continue;
				} else if (m_strCurrentLine.Left(30) == _T("<<< Hard Leecher User Name >>>")) {
					m_uMod = 3;
					continue;
				} else if (m_strCurrentLine.Left(30) == _T("<<< Soft Leecher User Name >>>")) {
					m_uMod = 4;
					continue;
				} else if (m_strCurrentLine.Left(37) == _T("<<< Soft Leecher VeryCD User Name >>>")) {
					m_uMod = 5;
					continue;
				} else if (m_strCurrentLine.Left(30) == _T("<<< Hard Leecher User Hash >>>")) {
					m_uMod = 6;
					continue;
				} else if (m_strCurrentLine.Left(30) == _T("<<< Soft Leecher User Hash >>>")) {
					m_uMod = 7;
					continue;
				} else if (m_strCurrentLine.Left(26) == _T("<<< Unknown Hello Tags >>>")) {
					m_uMod = 8;
					continue;
				} else if (m_strCurrentLine.Left(25) == _T("<<< Unknown Info Tags >>>")) {
					m_uMod = 9;
					continue;
				} else if (m_strCurrentLine.Left(21) == _T("<<< Spam Messages >>>")) {
					m_uMod = 10;
					continue;
				}
				
				//Find and remove comments
				int iPos = m_strCurrentLine.Find(_T("#"));
				if (iPos != -1)
					m_strCurrentLine = m_strCurrentLine.Left(iPos);

				if (m_uMod == 1 || m_uMod == 2) {
					LeecherModNamesType toadd;
					iPos = m_strCurrentLine.Find(_T("&&"));
					if (iPos != -1) {
						toadd.strModVersion = m_strCurrentLine.Left(iPos);
						if (toadd.strModVersion.Left(1) == _T("^")) { //Check if case sensitive flag exists
							toadd.bCaseSensitiveMod = true;
							toadd.strModVersion = toadd.strModVersion.Right(toadd.strModVersion.GetLength() - 1); //Trim ^
						} else {
							toadd.bCaseSensitiveMod = false;
							toadd.strModVersion.MakeLower(); // Making this lowercase once loading. This way later case insensitive checks will be faster.
						}

						toadd.strClientVersion = m_strCurrentLine.Mid(iPos+2, m_strCurrentLine.GetLength());
						if (toadd.strClientVersion.Left(1) == _T("^")) { //Check if case sensitive flag exists
							toadd.bCaseSensitiveClient = true;
							toadd.strClientVersion = toadd.strClientVersion.Right(toadd.strClientVersion.GetLength() - 1); //Trim ^
						} else {
							toadd.bCaseSensitiveClient = false;
							toadd.strClientVersion.MakeLower(); // Making this lowercase once loading. This way later case insensitive checks will be faster.
						}
					} else {
						toadd.strModVersion = m_strCurrentLine;
						if (toadd.strModVersion.Left(1) == _T("^")) { //Check if case sensitive flag exists
							toadd.bCaseSensitiveMod = true;
							toadd.strModVersion = toadd.strModVersion.Right(toadd.strModVersion.GetLength() - 1); //Trim ^
						} else {
							toadd.bCaseSensitiveMod = false;
							toadd.strModVersion.MakeLower(); // Making this lowercase once loading. This way later case insensitive checks will be faster.
						}
					}

					if (m_uMod == 1) {
						if (!HardLeecherModNamesList.Find(toadd))
							HardLeecherModNamesList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					} else if (m_uMod == 2) {
						if (!SoftLeecherModNamesList.Find(toadd))
							SoftLeecherModNamesList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					}
				} else if (m_uMod == 3 || m_uMod == 4 || m_uMod == 5 || m_uMod == 10) {
					TextAndCaseType toadd;
					if (m_strCurrentLine.Left(1) == _T("^")) { //Check if case sensitive flag exists
						toadd.bCaseSensitive = true;
						toadd.strText = m_strCurrentLine.Right(m_strCurrentLine.GetLength()-1); //Trim ^
					} else {
						toadd.bCaseSensitive = false;
						toadd.strText = m_strCurrentLine.MakeLower(); // Making this lowercase once loading. This way later case insensitive checks will be faster.
					}

					if (m_uMod == 3) {
						if (!HardLeecherUserNamesList.Find(toadd))
							HardLeecherUserNamesList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					} else if (m_uMod == 4) {
						if (!SoftLeecherUserNamesList.Find(toadd))
							SoftLeecherUserNamesList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					} else if (m_uMod == 5) {
						if (!SoftLeecherVeryCDUserNamesList.Find(toadd))
							SoftLeecherVeryCDUserNamesList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					} else if (m_uMod == 10) {
						if (!SpamMessagesList.Find(toadd))
							SpamMessagesList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					}
				} else if (m_uMod == 6) {
					CString m_strCurrentLineUpper = m_strCurrentLine.MakeUpper();
					if (!HardLeecherUserHashesList.Find(m_strCurrentLineUpper))
						HardLeecherUserHashesList.AddTail(m_strCurrentLineUpper);
					else
						LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
				} else if (m_uMod == 7) {
					CString m_strCurrentLineUpper = m_strCurrentLine.MakeUpper();
					if (!SoftLeecherUserHashesList.Find(m_strCurrentLineUpper))
						SoftLeecherUserHashesList.AddTail(m_strCurrentLineUpper);
					else
						LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
				} else if (m_uMod == 8 || m_uMod == 9) {
					UnknownTagsType toadd;
					iPos = m_strCurrentLine.Find(_T("&&"));
					if (iPos != -1) {
						toadd.uTagID = _tcstoul(m_strCurrentLine.Left(iPos), nullptr, 16);
						toadd.strDescription = m_strCurrentLine.Mid(iPos+2, m_strCurrentLine.GetLength());

						if (toadd.strDescription.Left(1) == _T("^")) //Check if case sensitive flag exists
							toadd.strDescription = toadd.strDescription.Right(toadd.strDescription.GetLength() - 1); //Trim ^
						else
							toadd.strDescription.MakeUpper(); // Making this lowercase once loading. This way later case insensitive checks will be faster.
					} else
						toadd.uTagID = _tcstoul(m_strCurrentLine, nullptr, 16);

					if (m_uMod == 8) {
						if (!UnknownHelloTagsList.Find(toadd))
							UnknownHelloTagsList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					} else if (m_uMod == 9) {
						if (!UnknownInfoTagsList.Find(toadd))
							UnknownInfoTagsList.AddTail(toadd);
						else
							LogWarning(GetResString(_T("SHIELD_DUPLICATE_DEFINITION")), m_strCurrentLineForLog);
					}
				}
			}

			m_bShieldFileLoaded = true;
			Log(GetResString(_T("SHIELD_LOADED")), GetLoadedDefinitionCount());
		} catch (CFileException* ex) {
			ASSERT(0);
			ex->Delete();
		}
		ShieldFile.Close();
	}

	if (!m_bShieldFileLoaded)
		LogWarning(GetResString(_T("SHIELD_NOT_LOADED")));

	return;
}

void CShield::FillCategoryPunishmentMap()
{
	m_CategoryPunishmentMap.RemoveAll();
	m_CategoryPunishmentMap[PR_WRONGTAGORDER] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGHELLOSIZE] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGHASH] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGMD4STRING] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGUPLOADREQ] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGAICH] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGFORMAT] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGINFOSIZE] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_WRONGTAGINFOFORMAT] = thePrefs.GetWrongTagPunishment();
	m_CategoryPunishmentMap[PR_UNKOWNTAG] = thePrefs.GetUnknownTagPunishment();
	m_CategoryPunishmentMap[PR_GHOSTMOD] = thePrefs.GetGhostModPunishment();
	m_CategoryPunishmentMap[PR_BADMODSOFT] = thePrefs.GetSoftLeecherPunishment();
	m_CategoryPunishmentMap[PR_BADUSERSOFT] = thePrefs.GetSoftLeecherPunishment();
	m_CategoryPunishmentMap[PR_BADHASHSOFT] = thePrefs.GetSoftLeecherPunishment();
	m_CategoryPunishmentMap[PR_BADMODUSERHASHHARD] = thePrefs.GetHardLeecherPunishment();
	m_CategoryPunishmentMap[PR_MODTHIEF] = thePrefs.GetModThiefPunishment();
	m_CategoryPunishmentMap[PR_SPAMMER] = thePrefs.GetSpamPunishment();
	m_CategoryPunishmentMap[PR_XSEXPLOITER] = thePrefs.GetXSExploiterPunishment();
	m_CategoryPunishmentMap[PR_FAKEMULEVERSION] = thePrefs.GetFakeEmulePunishment();
	m_CategoryPunishmentMap[PR_FAKEMULEVERSIONVAGAA] = thePrefs.GetFakeEmulePunishment();
	m_CategoryPunishmentMap[PR_USERNAMETHIEF] = thePrefs.GetUserNameThiefPunishment();
	m_CategoryPunishmentMap[PR_EMCRYPT] = thePrefs.GetEmcryptPunishment();
	m_CategoryPunishmentMap[PR_HASHTHIEF] = thePrefs.GetHashThiefPunishment();
	m_CategoryPunishmentMap[PR_BADCOMMUNITY] = thePrefs.GetCommunityPunishment();
	m_CategoryPunishmentMap[PR_UPLOADFAKER] = thePrefs.GetUploadFakerPunishment();
	m_CategoryPunishmentMap[PR_AGGRESSIVE] = thePrefs.GetAgressivePunishment();
	m_CategoryPunishmentMap[PR_FILEFAKER] = thePrefs.GetFileFakerPunishment();
	m_CategoryPunishmentMap[PR_EMPTYUSERNAME] = thePrefs.GetEmptyUserNameEmulePunishment();
	m_CategoryPunishmentMap[PR_HEXMODNAME] = thePrefs.GetHexModNamePunishment();
	m_CategoryPunishmentMap[PR_USERNAMECHANGER] = thePrefs.GetUserNameChangerPunishment();
	m_CategoryPunishmentMap[PR_TCPERRORFLOODER] = thePrefs.GetTCPErrorFlooderPunishment();
	m_CategoryPunishmentMap[PR_MODCHANGER] = thePrefs.GetModChangerPunishment();
	m_CategoryPunishmentMap[PR_NONSUIMLDONKEY] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_NONSUIEDONKEY] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_NONSUIEDONKEYHYBRID] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_NONSUISHAREAZA] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_NONSUILPHANT] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_NONSUIAMULE] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_NONSUIEMULE] = thePrefs.GetNonSuiPunishment() + 10;
	m_CategoryPunishmentMap[PR_WRONGPACKAGE] = P_IPUSERHASHBAN;

	if (thePrefs.GetAntiP2PBotsPunishment() > 1)
		m_CategoryPunishmentMap[PR_ANTIP2PBOT] = P_NOPUNISHMENT;
	else
		m_CategoryPunishmentMap[PR_ANTIP2PBOT] = thePrefs.GetAntiP2PBotsPunishment();
}

void CShield::SetPunishment(CUpDownClient* client, const CString& strReason, const uint8 uNewBadClientCategory, const uint8 uNewPunishment)
{
	if (client->m_uBadClientCategory == uNewBadClientCategory && uNewBadClientCategory != PR_MANUAL) // Don't proceed if the client is already set in this category
		return;

	uint8 uNewBadClientCategoryTemp = uNewBadClientCategory;
	uint8 uNewPunishmentTemp = 0;

	if (uNewBadClientCategoryTemp == PR_MANUAL // For this case, the punishment value is decided by the user and sent directly via context menu as an input variable.
		|| m_CategoryPunishmentMap.Lookup(uNewBadClientCategoryTemp, uNewPunishmentTemp) == 0) // Or if uNewBadClientCategory not found in the map. Otherwise uNewPunishmentTemp will be set by Lookup()
		uNewPunishmentTemp = uNewPunishment;

	if (uNewBadClientCategoryTemp != PR_MANUAL && client->m_uBadClientCategory != PR_NOTBADCLIENT && uNewPunishmentTemp >= client->m_uPunishment) // Don't proceed if it is already banned with a lower punishment value, though manuel punishment forces to proceed.
		return;

	if (uNewBadClientCategoryTemp == PR_MANUAL && uNewPunishmentTemp == P_NOPUNISHMENT)
		uNewBadClientCategoryTemp = PR_NOTBADCLIENT; // If manual punishment is set to no punishment, then update the category to not bad client. This is used to check if the client is already bad or not.

	if (uNewBadClientCategoryTemp == PR_NOTBADCLIENT) { // Cancel punishment
		if (client->m_uPunishment == P_IPUSERHASHBAN || client->m_uPunishment == P_USERHASHBAN)
			client->UnBan();
		else { // Force rechecks if user reenable function
			client->m_uPunishment = P_NOPUNISHMENT;
			client->m_tPunishmentStartTime = 0;

			if (client->m_uBadClientCategory == PR_BADMODSOFT || client->m_uBadClientCategory == PR_BADMODUSERHASHHARD)
				client->m_bForceRecheckShield = true; 

			if (client->m_uBadClientCategory == PR_USERNAMECHANGER) {
				client->m_uUserNameChangerCounter = 0;
				client->m_dwUserNameChangerIntervalStart = GetTickCount();
			}

			if (client->m_uBadClientCategory == PR_MODCHANGER) {
				client->m_uModChangeCounter = 0;
				client->m_dwModChangerIntervalStart = GetTickCount();
			}

			if (client->m_uBadClientCategory == PR_TCPERRORFLOODER) {
				client->m_uTCPErrorCounter = 0;
				client->m_dwTCPErrorFlooderIntervalStart = GetTickCount();
			}

			AddProtectionLogLine(false, _T("<Punishment Cancellation - %u> - Client %s"), client->m_uBadClientCategory, (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			client->m_uBadClientCategory = PR_NOTBADCLIENT;
			client->m_strPunishmentReason.Empty();
			client->m_strPunishmentMessage.Empty();
		}

		if (!strReason.IsEmpty()) {
			client->m_strPunishmentMessage.Format(_T("%s"), strReason);
			client->ProcessBanMessage();
		}

		return;
	}

	if (UploaderPunishmentPreventionActive(client)) {
		client->m_uPunishment = P_NOPUNISHMENT;
		client->m_strPunishmentMessage.Format(_T("<Uploader Punishment Prevention> - Client %s"), client->DbgGetClientInfo());
		client->ProcessBanMessage();
		return;
	} else if (thePrefs.IsDontPunishFriends() && client->IsFriend()) {
		client->m_uPunishment = P_NOPUNISHMENT;
		client->m_strPunishmentMessage.Format(_T("<Friend Punishment Prevention> - Client %s"), client->DbgGetClientInfo());
		client->ProcessBanMessage();
		return;
	}

	if (uNewPunishmentTemp > P_USERHASHBAN && client->m_uPunishment <= P_USERHASHBAN && uNewBadClientCategoryTemp == PR_MANUAL) // If this is manual repunishment of an already banned client and new punishment is a score reducing or upload ban
		client->UnBan();

	client->m_uPunishment = uNewPunishmentTemp;
	client->m_tPunishmentStartTime = time(NULL);
	client->m_uBadClientCategory = uNewBadClientCategoryTemp;

	if (client->m_uPunishment == P_IPUSERHASHBAN) // m_strPunishmentMessage won't be used by Ban function
		client->m_strPunishmentReason = client->m_uBadClientCategory == PR_MANUAL ? GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")) : strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason; // => Reason at Clientdetail
	else if (client->m_uPunishment == P_USERHASHBAN) // m_strPunishmentMessage won't be used by Ban function
		client->m_strPunishmentReason = client->m_uBadClientCategory == PR_MANUAL ? GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")) : strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason;
	else if (client->m_uPunishment == P_UPLOADBAN) {
		client->m_strPunishmentReason = client->m_uBadClientCategory == PR_MANUAL ? GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")) : strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason;
		client->m_strPunishmentMessage.Format(_T("<Punishment - %u> [%s] (Upload ban) - Client %s"), client->m_uBadClientCategory, client->m_uBadClientCategory == PR_MANUAL ? GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")) : strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason, client->DbgGetClientInfo());

		// If client is currently uploading, immediately stop transfer and send it back to queue. Enforce expected behavior: terminate payload asap and re-queue at worst rank.
		if (client->GetUploadState() == US_UPLOADING || client->GetUploadState() == US_CONNECTING) {
			client->FlushSendBlocks(); // drop queued standard packets
			Packet *pOutOfPartReqs = new Packet(OP_OUTOFPARTREQS, 0);
			theStats.AddUpDataOverheadFileRequest(pOutOfPartReqs->size); // Immediately send control to make peer switch to DS_ONQUEUE
			
			if (client->socket)
				client->socket->SendPacket(pOutOfPartReqs, true, 0, true);
			else
				client->SendPacket(pOutOfPartReqs);
			
			theApp.uploadqueue->AddClientToQueue(client, true); // Put client back to waiting list (tail)

			// Aggressively terminate the TCP stream to prevent further payload leakage.
			if (client->socket)
				client->socket->Close();

			theApp.uploadqueue->RemoveFromUploadQueue(client, _T("Upload ban applied"), true); // Finally free current upload slot
		}
	} else if (client->m_uPunishment <= P_SCOREX09) {
		client->m_strPunishmentReason = client->m_uBadClientCategory == PR_MANUAL ? GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")) : strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason;
		client->m_strPunishmentMessage.Format(_T("<Punishment - %u> [%s] (Score *0.%i) - Client %s"), client->m_uBadClientCategory, client->m_uBadClientCategory == PR_MANUAL ? GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")) : strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason, client->m_uPunishment, client->DbgGetClientInfo());
	} else {
		client->m_strPunishmentReason = strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason;
		client->m_strPunishmentMessage.Format(_T("<No Punishment - %u> [%s] - Client %s"), client->m_uBadClientCategory, strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_NONE")) : strReason, client->DbgGetClientInfo());
	}

	if (client->m_uPunishment <= P_USERHASHBAN) // if this is P_USERHASHBAN or P_IPBAN+P_USERHASHBAN
		client->Ban(client->m_strPunishmentReason, client->m_uBadClientCategory, client->m_uPunishment);
	else
		client->ProcessBanMessage();
}

void CShield::CheckClient(CUpDownClient* client)
{
	if (client->m_bUploaderPunishmentPreventionActive || (thePrefs.IsDontPunishFriends() && client->IsFriend())) // => Don't ban friends - sFrQlXeRt
		return;

	if (thePrefs.IsDetectAntiP2PBots() && IsBadUserHash(client->m_achUserHash)) //IsHarderPunishment isn't necessary necessary here since the cost is low
		SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_ANTIP2P_BOT")), PR_ANTIP2PBOT);

	if (thePrefs.IsDetectModNames() || thePrefs.IsDetectUserNames || thePrefs.IsDetectUserHashes()) { // Check if any of these options selected by the user
		// Check if this is a forced recheck or any input have changed since last check:
		if (client->m_bForceRecheckShield || client->GetUserName() != client->m_pszUsernameShield || !md4equ(client->m_achUserHash, client->m_achUserHashShield) || client->GetClientModVer() != client->m_strModVersionShield || client->GetClientSoftVer() != client->m_strClientSoftwareShield) {
			client->m_pszUsernameShield = client->GetUserName();
			md4cpy(client->m_achUserHashShield, client->GetUserHash());
			client->m_strModVersionShield = client->GetClientModVer();
			client->m_strClientSoftwareShield = client->GetClientSoftVer();
			client->m_bForceRecheckShield = false;
			CheckLeecher(client); // Punishment will be set inside this function. IsHarderPunishment checks will be also done there.
		}
	}

	if (thePrefs.IsDetectEmptyUserNameEmule() && client->GetClientSoft() == SO_EMULE && !client->GetUserName()) // IsHarderPunishment isn't necessary here since the cost is low
		SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_EMPTY_USER_NAME_EMULE")), PR_EMPTYUSERNAME);

	if (thePrefs.IsDetectHexModName() && IsHarderPunishment(client->m_uPunishment, PR_HEXMODNAME) && IsTypicalHex(client->GetClientModVer()))
		SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_HEX_MOD_NAME")), PR_HEXMODNAME);

	if (thePrefs.IsDetectCommunity() && IsHarderPunishment(client->m_uPunishment, PR_BADCOMMUNITY) && (IsSnakeOrGamer(client) || (client->m_bUnitedComm)))
		SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_COMMUNITY")), PR_BADCOMMUNITY);

	if (thePrefs.IsDetectFakeEmule() && IsHarderPunishment(client->m_uPunishment, PR_FAKEMULEVERSION) &&
		(((GetVersion() > 589) && ((client->GetSourceExchange1Version() > 0) || (client->GetExtendedRequestsVersion() > 0)) && (client->GetClientSoft() == SO_EDONKEY)) //Fake Donkeys
			|| ((client->GetClientSoft() == SO_EDONKEYHYBRID) && ((client->GetSourceExchange1Version() > 0) || (client->GetExtendedRequestsVersion() > 0))) //Fake Hybrids
			|| (client->m_bySupportSecIdent != 0 && (client->GetClientSoft() == SO_EDONKEYHYBRID || client->GetClientSoft() == SO_EDONKEY)) // this clients don't support sui
			|| (GetVersion() > MAKE_CLIENT_VERSION(0, 30, 0) && client->GetMuleVersion() > 0 && client->GetMuleVersion() != 0x99 && client->GetClientSoft() == SO_EMULE) //Fake emuleVersion
			|| (GetVersion() == MAKE_CLIENT_VERSION(0, 44, 3) && client->GetClientModVer().IsEmpty() && client->GetUnicodeSupport() == UTF8strNone && client->GetClientSoft() == SO_EMULE)
			)) {
		CString str;
		str.Format(GetResString(_T("PUNISHMENT_REASON_FAKE_EMULE_VERSION")) + _T(": %i/%u.%u.%u.%u/0.%x"), (UINT)client->GetClientSoft(), (GetVersion() >> 17) & 0x7f, (GetVersion() >> 10) & 0x7f, (GetVersion() >> 7) & 0x07, GetVersion() & 0x7f, (UINT)client->GetMuleVersion());
		SetPunishment(client, str, PR_FAKEMULEVERSION);
	}

	const float m_fModVersionNumber = client->GetModVersionNumber(client->GetClientModVer());
	if (thePrefs.IsDetectModThief() && IsHarderPunishment(client->m_uPunishment, PR_MODTHIEF)) {
		if ((m_fModVersionNumber == static_cast<float>(MOD_MAIN_VER) + MOD_MIN_VER / 10.0f && client->GetClientSoftVer() != MAKE_CLIENT_VERSION(CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, CemuleApp::m_nVersionUpd)) ||
		(m_fModVersionNumber >= 1.4f && CString(client->GetUserName()).Right(client->GetClientModVer().GetLength() + 1) != client->GetClientModVer() + _T("\xBB")))
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_MOD_THIEF")), PR_MODTHIEF);
		else {
			static const CString testModName[] = { _T("Xtreme"), _T("ScarAngel"), _T("Mephisto"), _T("MorphXT"), _T("EastShare"), _T("StulleMule"), /* _T("Magic Angel"), */ _T("DreaMule"), _T("X-Mod"), _T("RaJiL") };
			static const float testMinVer[] = { 4.4f, 2.5f, 1.5f, 10.0f, 13.0f, 6.0f, /* 3.0f, */ 3.0f, 0.0f, 2.2f };

			for (int i = 0; i < 9; i++) { //9 is number of strings
				bool tag1 = (CString(client->GetUserName()).Find(_T('«') + testModName[i]) != -1);
				if (client->GetClientModVer().Find(testModName[i]) != -1) {
					float version = (float)_tstof(client->GetClientModVer().Right(4));
					if (!tag1 && ((testMinVer[i] == 0.0f) || (version == 9.7f) || (version >= testMinVer[i])))
						SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_MOD_THIEF")), PR_MODTHIEF);
				} else if (tag1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_MOD_THIEF")), PR_MODTHIEF);
			}
		}
	}

	if (thePrefs.IsDetectUserNameThief() && IsHarderPunishment(client->m_uPunishment, PR_USERNAMETHIEF)) {
		const LPCTSTR pszUserName = client->GetUserName();
		if (pszUserName != NULL && pszUserName[0] != _T('\0')) {
			const bool bMirrorsCurrentAntiTag = !client->m_strAntiUserNameThiefNick.IsEmpty()
				&& StrStrI(pszUserName, client->m_strAntiUserNameThiefNick) != NULL;
			const bool bMirrorsGlobalAntiTag = theAntiNickClass.FindOurTagIn(pszUserName);
			if (bMirrorsCurrentAntiTag || bMirrorsGlobalAntiTag) {
				bool bSkipPunishment = false;
				if (theAntiNickClass.ContainsReservedAntiUserNameThiefTag(pszUserName) && client->IsAIModClient() && client->credits != NULL) {
					const EIdentState identState = client->credits->GetCurrentIdentState(client->GetConnectIP());
					// Keep trusted AI peers exempt while SUI is pending or verified.
					bSkipPunishment = (identState == IS_IDENTIFIED || identState == IS_IDNEEDED);
				}
				if (!bSkipPunishment)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_USER_NAME_THIEF")), PR_USERNAMETHIEF);
			}
		}
	}
}

void CShield::CheckLeecher(CUpDownClient* client)
{
	CString m_strUserName = client->GetUserName();
	CString m_strUserHash = client->HasValidHash() ? md4str(client->GetUserHash()) : EMPTY;
	CString m_strModVersion = client->GetClientModVer();
	CString m_strClientVersion = client->GetClientSoftVer();

	if (m_strUserName.IsEmpty() && m_strUserHash.IsEmpty() && m_strModVersion.IsEmpty() && m_strClientVersion.IsEmpty())
		return;

	///////////////////////////////////////// Hard Leecher User Mod Check Starts ////////////////////////////////////////
	if (thePrefs.IsDetectModNames() && !m_strModVersion.IsEmpty() && !m_strClientVersion.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_BADMODUSERHASHHARD)) {
		CString strModVersion;
		CString strClientVersion;
		for (POSITION pos = HardLeecherModNamesList.GetHeadPosition(); pos != NULL;) {
			LeecherModNamesType curItem = HardLeecherModNamesList.GetNext(pos);
			strModVersion = m_strModVersion;
			strClientVersion = m_strClientVersion;

			if (!curItem.bCaseSensitiveMod)
				strModVersion.MakeLower();

			if (!curItem.bCaseSensitiveClient)
				strClientVersion.MakeLower();

			if (!curItem.strModVersion.IsEmpty() && !curItem.strClientVersion.IsEmpty()) {
				if (strModVersion.Find(curItem.strModVersion) != -1 && strClientVersion.Find(curItem.strClientVersion) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODUSERHASHHARD);
			} else if (!curItem.strModVersion.IsEmpty()) {
				if (strModVersion.Find(curItem.strModVersion) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODUSERHASHHARD);
			} else if (!curItem.strClientVersion.IsEmpty()) {
				if (strClientVersion.Find(curItem.strClientVersion) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODUSERHASHHARD);
			}
		}

		CString strModVersionLower = m_strModVersion;
		CString strClientVersionLower = m_strClientVersion;
		strModVersionLower.MakeLower();
		strClientVersionLower.MakeLower();

		//Hard coded checks-1
		if ((m_strClientVersion.CompareNoCase(_T("eMule")) == 0) || //the client did not send client version
			(strModVersionLower.Find(_T("morph")) != -1 && (strModVersionLower.Find(_T("max")) != -1 || strModVersionLower.Find(_T("+")) != -1 || strModVersionLower.Find(_T("×")) != -1)) || // Originally "Morph" and "Max", lowercased for the comparison.
			(!m_strModVersion.IsEmpty() && CString(m_strModVersion).Trim().IsEmpty()) || //pruma, korean leecher, modversion is a space
			(m_strModVersion.Find(_T(" 091113")) != -1 && m_strModVersion.Find(_T("VeryCD")) == -1) || //compatible client in china, but no src //tetris
			(m_strModVersion.GetLength() > 0 && (strClientVersionLower.Find(_T("edonkey")) != -1 || m_strModVersion[0] == _T('['))) || //1. donkey user with mod name, 2. mod name begins with [ this is a known leecher
			(strModVersionLower.Find(_T("Xtreme")) != -1 && strClientVersionLower.Find(_T("]")) == -1))  //bad Xtreme mod
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODUSERHASHHARD);

		//Hard coded checks-2: Bad Mod Name Scheme by WiZaRd
		if (m_strModVersion.IsEmpty() ||
			(m_strModVersion.Find(_T("CHN ")) == 0 && m_strModVersion.GetLength() > 8) ||
			(m_strModVersion.Find(_T("Apollo")) == 0) || //Apollo is a Portugal Mod
			(m_strModVersion.Find(_T("sivka")) == 0) ||
			(m_strModVersion.Find(_T("aMule CVS")) == 0))
			; //do nothing
		else {
			if (m_strClientVersion.Find(_T("eMule v")) != -1 && m_strModVersion.GetLength() <= 4) //most of them are fincan
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODUSERHASHHARD);

			int iNumberFound = -1;
			_TINT ch = 0;
			bool bBad = false;
			bool bNotEnd = false;

			for (int i = 0; i < m_strModVersion.GetLength() && bBad == false; ++i) {
				ch = m_strModVersion.GetAt(i);
				if (ch == L'.' || ch == L' ') {
					bNotEnd = true; //these chars should not be the end of mod name
					iNumberFound = -1; //this is a simple hack to not punish mods like TK4 or Spike2 :)
					continue; //skip "legal" chars
				}

				if (ch == L'-' /* || ch == L'+' */) { //connector characters, connect two string or two numbers
					bNotEnd = true; //these chars should not be the end of mod name
					if (iNumberFound != -1)
						iNumberFound++; //exclude some mod name like v#.#-a1
					continue;
				}

				if (_istpunct(ch) || _istspace(ch))
					bBad = true; //illegal punctuation or whitespace character
				else if (_istcntrl(ch))
					bBad = true; //control character in mod name!?
				else {
					bNotEnd = false;
					if (_istdigit(ch))
						iNumberFound = i;
					else if ((iNumberFound == i - 1) && _istxdigit(ch)) //abcdef is legal in the end of version number, also exclude bowlfish tk4 and so on
						; //do nothing
					else if (iNumberFound != -1)
						bBad = true; //that is: number out of row, e.g. not MorphXT v11.9 but Morph11XT.9
				}
			}

			if (bBad || bNotEnd)
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODUSERHASHHARD);
		}
	}
	///////////////////////////////////////// Hard Leecher User Mod Check Ends ////////////////////////////////////////

	//////////////////////////////////////// Hard Leecher User Name Check Starts //////////////////////////////////////
	if (thePrefs.IsDetectUserNames() && !m_strUserName.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_BADMODUSERHASHHARD)) {
		CString m_strUserNameCopy;
		for (POSITION pos = HardLeecherUserNamesList.GetHeadPosition(); pos != NULL;) {
			TextAndCaseType curItem = HardLeecherUserNamesList.GetNext(pos);
			m_strUserNameCopy = m_strUserName;

			if (!curItem.bCaseSensitive)
				m_strUserNameCopy.MakeLower();

			if (!curItem.strText.IsEmpty() && m_strUserNameCopy.Find(curItem.strText) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_USERNAME")), PR_BADMODUSERHASHHARD);
		}

		//Hard coded check
		//New Ketamine:
		if (m_strUserName.GetLength() >= 14) {
			LPCTSTR tempstr = StrStr(m_strUserName, _T("[ePlus]"));
			if (tempstr && _tcslen(tempstr) >= 7 && StrStr(tempstr + 7, _T("[ePlus]")))
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_KETAMINE")), PR_BADMODUSERHASHHARD);
		}
	}
	//////////////////////////////////////// Hard Leecher User Name Check Ends ///////////////////////////////////////

	//////////////////////////////////////// Hard Leecher User Hash Check Starts /////////////////////////////////////
	if (thePrefs.IsDetectUserHashes() && !m_strUserHash.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_BADMODUSERHASHHARD)) {
		CString strUserHash = m_strUserHash;
		strUserHash.MakeUpper();
		for (POSITION pos = HardLeecherUserHashesList.GetHeadPosition(); pos != NULL;) {
			CString curItem = HardLeecherUserHashesList.GetNext(pos);
			if (!curItem.IsEmpty() && strUserHash.Find(curItem) != -1)
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_USER_HASH")), PR_BADMODUSERHASHHARD);
		}
	}
	//////////////////////////////////////// Hard Leecher User Hash Check Ends ///////////////////////////////////////

	//////////////////////////////////////// Soft Leecher User Mod Check Starts //////////////////////////////////////
	if (thePrefs.IsDetectModNames() && !m_strModVersion.IsEmpty() && !m_strClientVersion.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_BADMODSOFT)) {
		CString strModVersion;
		CString strClientVersion;
		for (POSITION pos = SoftLeecherModNamesList.GetHeadPosition(); pos != NULL;) {
			LeecherModNamesType curItem = SoftLeecherModNamesList.GetNext(pos);
			strModVersion = m_strModVersion;
			strClientVersion = m_strClientVersion;

			if (!curItem.bCaseSensitiveMod)
				strModVersion.MakeLower();

			if (!curItem.bCaseSensitiveClient)
				strClientVersion.MakeLower();

			if (!curItem.strModVersion.IsEmpty() && !curItem.strClientVersion.IsEmpty()) {
				if (strModVersion.Find(curItem.strModVersion) != -1 && strClientVersion.Find(curItem.strClientVersion) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODSOFT);
			} else if (!curItem.strModVersion.IsEmpty()) {
				if (strModVersion.Find(curItem.strModVersion) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODSOFT);
			} else if (!curItem.strClientVersion.IsEmpty()) {
				if (strClientVersion.Find(curItem.strClientVersion) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_MOD_NAME")), PR_BADMODSOFT);
			}
		}

		//Hard coded checks-1: Added by cyrex
		if (m_strModVersion.GetLength() == 10 && m_strUserName.GetLength() > 4 && StrStr(m_strUserName.Right(4), _T("/]")) && StrStr(m_strUserName, _T("[SE]")))
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_MYSTERY_MOD_STRING")), PR_BADMODSOFT);

		//Hard coded checks-2: Random Mod Name check
		if (wcsstr(m_strUserName, L" http://emule-project.net ") != nullptr || wcsstr(m_strUserName, L" beba.eMuleFuture.de ") != nullptr) {
			static const std::wregex SDC_Pattern_RandomModName_Modversion(L"^[A-Z][a-z]{4,10} [1-9]\\.[0-9]$");
			static const std::wregex SDC_Pattern_RandomModName_Username(L"^(\\([A-Za-z]{4,10}\\)|\\{[A-Za-z]{4,10}\\}) .{19,24} \\[[A-Za-z]{4,10}\\]$"); //Middle part of Username is limited to possible length, which should be able to improve performance.
			if (std::regex_match(m_strModVersion.GetString(), SDC_Pattern_RandomModName_Modversion) == true &&
				std::regex_match(m_strUserName.GetString(), SDC_Pattern_RandomModName_Username) == true)
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_RANDOM_MOD_NAME")), PR_BADMODSOFT);
		}
	}
	//////////////////////////////////////// Soft Leecher User Mod Check Ends //////////////////////////////////////

	/////////////////////////////////////// Soft Leecher User Name Check Starts ////////////////////////////////////
	if (thePrefs.IsDetectUserNames() &&  !m_strUserName.IsEmpty() && (IsHarderPunishment(client->m_uPunishment, PR_BADUSERSOFT) || IsHarderPunishment(client->m_uPunishment, PR_GHOSTMOD) || IsHarderPunishment(client->m_uPunishment, PR_BADMODSOFT))) {
		CString m_strUserNameCopy;
		for (POSITION pos = SoftLeecherUserNamesList.GetHeadPosition(); pos != NULL;) {
			TextAndCaseType curItem = SoftLeecherUserNamesList.GetNext(pos);
			m_strUserNameCopy = m_strUserName;

			if (!curItem.bCaseSensitive)
				m_strUserNameCopy.MakeLower();

			if (!curItem.strText.IsEmpty() && m_strUserNameCopy.Find(curItem.strText) != -1)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_USERNAME")), PR_BADUSERSOFT);
		}

		//Hard coded checks-1: Check for aedit: An unmodded emule can't send a space at last sign.
		if (m_strModVersion.IsEmpty() && m_strUserName.Right(1) == L" ") //CStringT::Right returns substring. [aMule-dlp]
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_AEDIT")), PR_BADUSERSOFT);

		//Hard coded checks-2
		if (m_strUserName.GetLength() >= 4 && m_strUserName[3] <= 0x1F && m_strUserName[1] <= 0x1F)  //Community (based on LSD or it's smasher)
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_COMMUNITY_USER_NAME")), PR_BADUSERSOFT);
		else if (m_strUserName.GetLength() >= 4 && m_strUserName[0] == _T('v') && m_strUserName.Find(_T(":com ")) != -1) //X-Treme
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_X_TREME")), PR_BADUSERSOFT);

		//Hard coded checks-3:  Doubled  «...» in the username, like "username «Xtreme #.#» «abcd»" zz_fly
		int posr1 = m_strUserName.Find(_T('»'));
		int posr2 = m_strUserName.ReverseFind(_T('»'));
		if ((posr1 > 5) && (posr2 - posr1 > 5) && ((m_strUserName.GetAt(posr1 - 5) == _T('«')) || (m_strUserName.GetAt(posr2 - 5) == _T('«'))))
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_USERNAME_PADDINGS")), PR_BADUSERSOFT);

		//Hard coded checks-4: Community check
		if (m_strUserName.GetLength() >= 7 && m_strUserName.Right(1) == _T("]")) {
			//Check for special nickaddon
			int find = m_strUserName.ReverseFind(_T('['));
			if (find >= 0) {
				CString addon = m_strUserName.Mid(find + 1);
				if (addon.GetLength() > 2 && IsTypicalHex(addon.Left(addon.GetLength() - 1))) //Check for Hex (e.g. X-Treme)
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_HEX_CODE_ADDON")), PR_BADUSERSOFT);

				if (find == m_strUserName.GetLength() - 6) {
					bool bFoundRandomPadding = false;
					_TINT ch = 0;

					for (int i = 1; i < 5; i++) {
						ch = m_strUserName.GetAt(find + i);
						if (_istpunct(ch) || /* _istspace(ch) || */ _istcntrl(ch)) {
							bFoundRandomPadding = true;
							break;
						}
					}

					if (bFoundRandomPadding && !m_strModVersion.IsEmpty() && (m_strUserName.Find(_T("http://emule-project.net [")) == 0) && (find == 25))
						SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_TLH_COMMUNITY")), PR_BADUSERSOFT); //username like "http://emule-project.net [random]"

					if (thePrefs.IsDetectGhostMod() && bFoundRandomPadding && m_strModVersion.IsEmpty() && (find == m_strUserName.Find(_T('['))))
						SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_GHOST_MOD")), PR_GHOSTMOD); // Username has a random padding [random], it should be a mod function, but there is no mod name

					if (bFoundRandomPadding && (m_strUserName.Find(_T("Silver Surfer User")) == 0) && (m_strModVersion.Find(_T("Silver")) == -1))
						SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_FAKE_SILVER_SURFER")), PR_BADMODSOFT); //**Riso64Bit** :: Fake silver surfer
				}
			}
		}
	}
	//////////////////////////////////////// Soft Leecher User Name Check Ends ///////////////////////////////////////

	//////////////////////////////////// Soft Leecher VeryCD User Name Check Starts //////////////////////////////////
	if (thePrefs.IsDetectUserNames() && !m_strUserName.IsEmpty() && m_strModVersion.Find(_T("VeryCD")) != -1 && IsHarderPunishment(client->m_uPunishment, PR_BADUSERSOFT)) {
		CString m_strUserNameCopy;
		for (POSITION pos = SoftLeecherVeryCDUserNamesList.GetHeadPosition(); pos != NULL;) {
			TextAndCaseType curItem = SoftLeecherVeryCDUserNamesList.GetNext(pos);
			m_strUserNameCopy = m_strUserName;

			if (!curItem.bCaseSensitive)
				m_strUserNameCopy.MakeLower();

			if (!curItem.strText.IsEmpty() && m_strUserNameCopy.Find(curItem.strText) != -1)
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_VERYCD_DEFAULT_NICKNAMES")), PR_BADUSERSOFT);
		}
	}
	//////////////////////////////////// Soft Leecher VeryCD User Name Check Ends //////////////////////////////////

	//////////////////////////////////////// Soft Leecher User Hash Check Ends ///////////////////////////////////////
	if (thePrefs.IsDetectUserHashes() && !m_strUserHash.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_BADHASHSOFT)) {
		CString strUserHash = m_strUserHash;
		strUserHash.MakeUpper();
		for (POSITION pos = SoftLeecherUserHashesList.GetHeadPosition(); pos != NULL;) {
			CString curItem = SoftLeecherUserHashesList.GetNext(pos);
			if (!curItem.IsEmpty() && strUserHash.Find(curItem) != -1)
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_BAD_USER_HASH")), PR_BADHASHSOFT);
		}
	}
	//////////////////////////////////////// Soft Leecher User Hash Check Ends ///////////////////////////////////////

	return;
}

const bool CShield::CheckSpamMessage(CUpDownClient* client, const CString& strMessage)
{
	if (strMessage.IsEmpty() || !IsHarderPunishment(client->m_uPunishment, PR_SPAMMER))
		return false;


	CString m_strSpamMessage;
	for (POSITION pos = SpamMessagesList.GetHeadPosition(); pos != NULL;) {
		TextAndCaseType curItem = SpamMessagesList.GetNext(pos);
		m_strSpamMessage = strMessage;

		if (!curItem.bCaseSensitive)
			m_strSpamMessage.MakeLower();

		if (!curItem.strText.IsEmpty() && m_strSpamMessage.Find(curItem.strText) != -1) {
			SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_SPAMMER")), PR_SPAMMER);
			return true;
		}
	}

	//Hard coded check:
	if (CString(strMessage).Trim().IsEmpty()) {
		SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_SPAMMER")), PR_SPAMMER);
		return true;
	}

	return false;
}

void CShield::CheckHelloTag(CUpDownClient* client, CTag& tag)
{
	if (!thePrefs.IsDetectWrongTag() && !thePrefs.IsDetectUnknownTag())
		return;

	if (thePrefs.IsDetectUnknownTag() && IsHarderPunishment(client->m_uPunishment, PR_UNKOWNTAG)) {
		for (POSITION pos = UnknownHelloTagsList.GetHeadPosition(); pos != NULL;) {
			UnknownTagsType curItem = UnknownHelloTagsList.GetNext(pos);
			if (curItem.uTagID == tag.GetNameID()) {
				CString m_strPunishmentReason;
				m_strPunishmentReason.Format(_T("%s: [%s] %s"), GetResString(_T("PUNISHMENT_REASON_SUSPECT_HELLO_TAG")), curItem.strDescription, tag.GetFullInfo());
				SetPunishment(client, m_strPunishmentReason, PR_UNKOWNTAG);
				return;
			}
		}
	}

	if (thePrefs.IsDetectWrongTag() && tag.IsStr() && tag.GetStr().GetLength() >= 32 && IsHarderPunishment(client->m_uPunishment, PR_WRONGTAGMD4STRING)) {
		CString m_strPunishmentReason;
		m_strPunishmentReason.Format(GetResString(_T("PUNISHMENT_REASON_MD4_STRING_IN_OPCODE")), tag.GetFullInfo());
		SetPunishment(client, m_strPunishmentReason, PR_WRONGTAGMD4STRING);
		return;
	}

	if (thePrefs.IsDetectUnknownTag() && IsHarderPunishment(client->m_uPunishment, PR_UNKOWNTAG)) {
		CString m_strPunishmentReason;
		m_strPunishmentReason.Format(_T("%s: 0x%x"), GetResString(_T("PUNISHMENT_REASON_UNKNOWN_TAG")), tag.GetNameID());
		SetPunishment(client, m_strPunishmentReason, PR_UNKOWNTAG);
	}
}

void CShield::CheckInfoTag(CUpDownClient* client, CTag& tag)
{
	if (!thePrefs.IsDetectUnknownTag() || !IsHarderPunishment(client->m_uPunishment, PR_UNKOWNTAG))
		return;

	if (thePrefs.IsDetectUnknownTag() && IsHarderPunishment(client->m_uPunishment, PR_UNKOWNTAG)) {
		for (POSITION pos = UnknownInfoTagsList.GetHeadPosition(); pos != NULL;) {
			UnknownTagsType curItem = UnknownInfoTagsList.GetNext(pos);
			if (curItem.uTagID == tag.GetNameID()) {
				CString m_strPunishmentReason;
				m_strPunishmentReason.Format(_T("%s: [%s] %s"), GetResString(_T("PUNISHMENT_REASON_SUSPECT_INFO_TAG")), curItem.strDescription, tag.GetFullInfo());
				SetPunishment(client, m_strPunishmentReason, PR_UNKOWNTAG);
				return;
			}
		}
	}

	if (thePrefs.IsDetectUnknownTag() && IsHarderPunishment(client->m_uPunishment, PR_UNKOWNTAG)) {
		CString m_strPunishmentReason;
		m_strPunishmentReason.Format(_T("%s: 0x%x"), GetResString(_T("PUNISHMENT_REASON_UNKNOWN_TAG")), tag.GetNameID());
		SetPunishment(client, m_strPunishmentReason, PR_UNKOWNTAG);
	}
}

void CShield::CheckUserNameChanger(CUpDownClient* client, const CString& strOldUserName) {
	if (thePrefs.IsDetectUserNameChanger() && client->GetUserName() != NULL && !strOldUserName.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_USERNAMECHANGER)) {
		if (::GetTickCount() - client->m_dwUserNameChangerIntervalStart < thePrefs.GetUserNameChangerInterval()) {
			if (CString(client->GetUserName()).Compare(strOldUserName) != 0) { // Is this a user name change?
				// We're in a valid interval. Incrementing counter and checking for the specified threshold value:
				client->m_uUserNameChangerCounter++;
				if (client->m_uUserNameChangerCounter >= thePrefs.GetUserNameChangerThreshold())
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_USER_NAME_CHANGER")), PR_USERNAMECHANGER);
			}
		} else {
			// Previous interval finished. We now start a new one:
			client->m_uTCPErrorCounter = CString(client->GetUserName()).Compare(strOldUserName) == 0 ? 0 : 1; // If this is a user name change, we'll include it.
			client->m_dwUserNameChangerIntervalStart = ::GetTickCount();
		}
	}
}


void CShield::CheckModChanger(CUpDownClient* client, const CString& strOldModVersion) {
	if (thePrefs.IsDetectModChanger() && !client->GetClientModVer().IsEmpty() && !strOldModVersion.IsEmpty() && IsHarderPunishment(client->m_uPunishment, PR_MODCHANGER)) {
		if (::GetTickCount() - client->m_dwModChangerIntervalStart < thePrefs.GetModChangerInterval()) {
			if (client->GetClientModVer().Compare(strOldModVersion) != 0) { // Is this a mod change?
				// We're in a valid interval. Incrementing counter and checking for the specified threshold value:
				client->m_uModChangeCounter++;
				if (client->m_uModChangeCounter >= thePrefs.GetModChangerThreshold())
					SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_MOD_CHANGER")), PR_MODCHANGER);
			}
		} else {
			// Previous interval finished. We now start a new one:
			client->m_uTCPErrorCounter = client->GetClientModVer().Compare(strOldModVersion) == 0 ? 0 : 1; // If this is a mod change, we'll include it.
			client->m_dwModChangerIntervalStart = ::GetTickCount();
		}
	}
}

void CShield::CheckTCPErrorFlooder(CUpDownClient* client) {
	if (thePrefs.IsDetectTCPErrorFlooder() && IsHarderPunishment(client->m_uPunishment, PR_TCPERRORFLOODER)) {
		if (::GetTickCount() - client->m_dwTCPErrorFlooderIntervalStart < thePrefs.GetTCPErrorFlooderInterval()) {
			// We're in a valid interval. Incrementing counter and checking for the specified threshold value:
			client->m_uTCPErrorCounter++;
			if (client->m_uTCPErrorCounter >= thePrefs.GetTCPErrorFlooderThreshold())
				SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_TCP_ERROR_FLOODER")), PR_TCPERRORFLOODER);
		} else {
			// Previous interval finished. We now start a new one:
			client->m_uTCPErrorCounter = 1; // We include the error reported
			client->m_dwTCPErrorFlooderIntervalStart = ::GetTickCount();
		}
	}
}

bool CShield::UploaderPunishmentPreventionActive(CUpDownClient* client)
{
	// Don't activate Uploader Punishment Prevention if it is disabled or client has credit/no uploads to us or client has corrupt/banned user hash.
	if (thePrefs.GetUploaderPunishmentPrevention() && client->credits != NULL && client->credits->GetDownloadedTotal() != NULL && !IsCorruptOrBadUserHash(client->GetUserHash())) {
		switch (thePrefs.GetUploaderPunishmentPreventionCase()) {
		case CS_1:
		{
			client->m_bUploaderPunishmentPreventionActive = client->credits->GetDownloadedTotal() >= (int)thePrefs.GetUploaderPunishmentPreventionLimit() * 1024 << 20;
			return client->m_bUploaderPunishmentPreventionActive;
		}
		break;
		case CS_2:
		{
			if (client->credits->GetUploadedTotal() != NULL && client->credits->GetUploadedTotal() < client->credits->GetDownloadedTotal()) {
				client->m_bUploaderPunishmentPreventionActive = client->credits->GetDownloadedTotal() - client->credits->GetUploadedTotal() >= (int)thePrefs.GetUploaderPunishmentPreventionLimit() * 1024 << 20;
				return client->m_bUploaderPunishmentPreventionActive;
			} else {
				client->m_bUploaderPunishmentPreventionActive = false;
				return client->m_bUploaderPunishmentPreventionActive;
			}
		}
		break;
		case CS_3:
		{
			if (client->credits->GetUploadedTotal() != NULL && client->credits->GetUploadedTotal() < client->credits->GetDownloadedTotal()) {
				if (client->GetAntiUploaderCaseThree()) {
					client->m_bUploaderPunishmentPreventionActive = true;
					return client->m_bUploaderPunishmentPreventionActive;
				} else if (client->credits->GetDownloadedTotal() - client->credits->GetUploadedTotal() >= (int)thePrefs.GetUploaderPunishmentPreventionLimit() * 1024 << 20) {
					client->m_bAntiUploaderCaseThree = true;
					client->m_bUploaderPunishmentPreventionActive = true;
					return client->m_bUploaderPunishmentPreventionActive;
				} else {
					client->m_bUploaderPunishmentPreventionActive = false;
					return client->m_bUploaderPunishmentPreventionActive;
				}
			}
		}
		break;
		}
	}

	client->m_bUploaderPunishmentPreventionActive = false;
	return client->m_bUploaderPunishmentPreventionActive;
}

const bool CShield::IsHarderPunishment(const uint8 uCurrentPunishment, const uint8 uNewBadClientCategory) const
{
	uint8 m_uNewPunishment;
	return m_CategoryPunishmentMap.Lookup(uNewBadClientCategory, m_uNewPunishment) != 0 // Category not found in the map
		&& m_uNewPunishment < uCurrentPunishment; // and new punishment has a smaller value than current punishment, which means harder punishment
}

const bool CShield::IsTypicalHex(const CString& addon) const
{
	if (addon.GetLength() > 25 || addon.GetLength() < 5)
		return false;

	short bigalpha = 0;
	short smallalpha = 0;
	short numeric = 0;
	int endpos = addon.GetLength();
	int i = 0;

	for (i = 0; i < endpos; i++) {
		if ((addon.GetAt(i) >= _T('0') && addon.GetAt(i) <= _T('9')))
			numeric++;
		else if ((addon.GetAt(i) >= _T('A') && addon.GetAt(i) <= _T('F')))
			bigalpha++;
		else if ((addon.GetAt(i) >= _T('a') && addon.GetAt(i) <= _T('f')))
			smallalpha++;
		else
			break;
	}

	if (i == endpos) {
		if (numeric > 0 && (smallalpha > 0 && bigalpha == 0 || smallalpha == 0 && bigalpha > 0))
			return true;
	}

	return false;
}

const bool CShield::IsSnakeOrGamer(const CUpDownClient* client) const
{
	if (client->HasValidHash()) {
		CString m_strUserHash = md4str(client->GetUserHash());
		CString m_strUserName = client->GetUserName();

		//community check
		if (m_strUserName.GetLength() >= 7 && m_strUserName.Right(1) == _T("]")) {
			//check for gamer
			//two checks should be enough.
			if (m_strUserName.Right(6).Left(1) == m_strUserHash.Mid(5, 1) && m_strUserName.Right(3).Left(1) == m_strUserHash.Mid(7, 1))
				return true;

			//check for snake 
			int find = m_strUserName.ReverseFind(_T('['));
			if (find >= 0) {
				CString addon = m_strUserName.Mid(find + 1);
				int endpos = addon.GetLength() - 1;
				if (addon.GetLength() > 2) {
					int i = 0;
					for (; i < endpos; i++)
						if (!(addon.GetAt(i) >= _T('0') && addon.GetAt(i) <= _T('9')))
							i = endpos + 1;
					if (i == endpos)
						return true;

					if (thePrefs.IsDetectHexModName() && IsTypicalHex(addon.Left(addon.GetLength() - 1)))
						return true;
				}
			}
		}
	}
	return false;
}

const bool CShield::IsValidUserName(const CString& strUserName) const
{
	if (strUserName.IsEmpty())
		return false;

	CString m_strUserNameCopy;

	for (POSITION pos = HardLeecherUserNamesList.GetHeadPosition(); pos != NULL;) {
		TextAndCaseType curItem = HardLeecherUserNamesList.GetNext(pos);
		m_strUserNameCopy = strUserName;

		if (!curItem.bCaseSensitive)
			m_strUserNameCopy.MakeLower();

		if (!curItem.strText.IsEmpty() && m_strUserNameCopy.Find(curItem.strText) != -1)
			return false;
	}

	for (POSITION pos = SoftLeecherUserNamesList.GetHeadPosition(); pos != NULL;) {
		TextAndCaseType curItem = SoftLeecherUserNamesList.GetNext(pos);
		m_strUserNameCopy = strUserName;

		if (!curItem.bCaseSensitive)
			m_strUserNameCopy.MakeLower();

		if (!curItem.strText.IsEmpty() && m_strUserNameCopy.Find(curItem.strText) != -1)
			return false;
	}

	for (POSITION pos = SoftLeecherVeryCDUserNamesList.GetHeadPosition(); pos != NULL;) {
		TextAndCaseType curItem = SoftLeecherVeryCDUserNamesList.GetNext(pos);
		m_strUserNameCopy = strUserName;

		if (!curItem.bCaseSensitive)
			m_strUserNameCopy.MakeLower();

		if (!curItem.strText.IsEmpty() && m_strUserNameCopy.Find(curItem.strText) != -1)
			return false;
	}

	return true;
}

const UINT CShield::GetLoadedDefinitionCount() const
{
	return	HardLeecherModNamesList.GetCount() + SoftLeecherModNamesList.GetCount() + HardLeecherUserNamesList.GetCount() + SoftLeecherUserNamesList.GetCount() +
			SoftLeecherVeryCDUserNamesList.GetCount() + HardLeecherUserHashesList.GetCount() + SoftLeecherUserHashesList.GetCount() + UnknownHelloTagsList.GetCount() +
			UnknownInfoTagsList.GetCount() + SpamMessagesList.GetCount();
}
