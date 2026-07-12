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

#pragma once
#define SHIELDFILE _T("shield.conf")

enum UploaderPunishmentPreventionCaseSelection {
	CS_1 = 0,
	CS_2,
	CS_3
};

enum EBadClientCategory {
	PR_NOTBADCLIENT = 0,	//Not A Bad Client - Cancel punishment
	PR_WRONGTAGORDER,		//Wrong Tag: Big German leecher community
	PR_WRONGTAGFORMAT,		//Wrong Tag: Invalid hello tag format (Reduce Score for leecher - Stulle)
	PR_WRONGTAGINFOSIZE,	//Wrong Tag: Extra Bytes In Info Packet / Darkmule or buggy + ban
	PR_WRONGTAGINFOFORMAT,	//Wrong Tag: Invalid info tag format (Reduce Score for leecher - Stulle)
	PR_WRONGTAGMD4STRING,	//Wrong Tag: MD4 string / Snafu
	PR_WRONGTAGUPLOADREQ,	//Wrong Tag: Wrong Startuploadrequest (Bionic Community)
	PR_WRONGTAGAICH,		//Wrong Tag: wrong m_fSupportsAICH (Applejuice )
	PR_WRONGTAGHELLOSIZE,	//Wrong Tag: Extra Bytes In Hello Packet / Darkmule or buggy + ban
	PR_WRONGTAGHASH,		//Wrong Tag: Wrong HashSize + reduce score / New United Community
	PR_UNKOWNTAG,			//Unknown Tag - Snafu (Snafu = old leecher = hard ban)
	PR_BADMODSOFT,			//Mod Name Soft
	PR_BADMODUSERHASHHARD,	//Mod Name/User Name/User Hash Hard
	PR_BADUSERSOFT,			//User Name Soft
	PR_BADCOMMUNITY,		//Bad Community
	PR_EMCRYPT,				//eMCrypt
	PR_GHOSTMOD,			//Ghost Mod (Webcache tag without modstring)
	PR_FAKEMULEVERSION,		//Fake eMule Version
	PR_FAKEMULEVERSIONVAGAA,//Fake eMule Version (Vagaa Client)
	PR_HEXMODNAME,			//Hex Mod Name
	PR_EMPTYUSERNAME,		//Empty User Name Emule
	PR_MODCHANGER,			//Mod Changer
	PR_USERNAMECHANGER,		//User Name Changer
	PR_MODTHIEF,			//Mod Thief
	PR_USERNAMETHIEF,		//User Name Thief
	PR_HASHTHIEF,			//Hash Thief (Credit Hack)
	PR_SPAMMER,				//Spammer
	PR_XSEXPLOITER,			//XS-Exploiter
	PR_FILEFAKER,			//File Faker - sFrQlXeRt
	PR_UPLOADFAKER,			//Upload Faker
	PR_AGGRESSIVE,			//Agressive Client - sFrQlXeRt
	PR_NONSUIMLDONKEY,		//Non SUI MLDonkey: No Secure User Identification
	PR_NONSUIEDONKEY,		//Non SUI eDonkey: No Secure User Identification
	PR_NONSUIEDONKEYHYBRID,	//Non SUI eDonkey Hybrid: No Secure User Identification
	PR_NONSUISHAREAZA,		//Non SUI Shareaza: No Secure User Identification
	PR_NONSUILPHANT,		//Non SUI Lphant: No Secure User Identification
	PR_NONSUIAMULE,			//Non SUI aMule: No Secure User Identification
	PR_NONSUIEMULE,			//Non SUI eMule: No Secure User Identification
	PR_ANTIP2PBOT,			//AntiP2P Bot
	PR_OFFICIALBAN,			//Banned By eMule's Official Checks
	PR_MANUAL,				//Banned or punished manually via the client context menu by the user
	PR_BADHASHSOFT,			//User Hash Soft
	PR_TCPERRORFLOODER,		//TCP Error Flooder
	PR_WRONGPACKAGE,			//Wrong Package
	PR_UPLOADREQUESTABUSE	//Upload Request Abuse
};

enum EPunishment {
	P_IPUSERHASHBAN = 0,
	P_USERHASHBAN,
	P_UPLOADBAN,
	P_SCOREX01,
	P_SCOREX02,
	P_SCOREX03,
	P_SCOREX04,
	P_SCOREX05,
	P_SCOREX06,
	P_SCOREX07,
	P_SCOREX08,
	P_SCOREX09,
	P_NOPUNISHMENT
};

class CShield
{
public:
	CShield();
	~CShield();
	void LoadShieldFile();
	void SetPunishment(CUpDownClient* client, const CString& strReason, const uint8 uBadClientCategory, const uint8 uNewPunishment = P_NOPUNISHMENT);
	void CheckClient(CUpDownClient* client);
	void CheckLeecher(CUpDownClient* client);
	const bool CheckSpamMessage(CUpDownClient* client, const CString& strMessage);
	void CheckHelloTag(CUpDownClient* client, CTag& tag);
	void CheckInfoTag(CUpDownClient* client, CTag& tag);
	void CheckUserNameChanger(CUpDownClient* client, const CString& strOldUserName);
	void CheckModChanger(CUpDownClient* client, const CString& strOldModVersion);
	void CheckTCPErrorFlooder(CUpDownClient* client);
	const bool IsTypicalHex(const CString& addon) const;
	const bool IsSnakeOrGamer(const CUpDownClient* client) const; // Snake / Gamer Check [Xman] - Stulle
	const bool IsValidUserName(const CString& strUserName) const;
	void FillCategoryPunishmentMap();
	const bool IsHarderPunishment(const uint8 uCurrentPunishment, const uint8 uNewBadClientCategory) const;
	bool UploaderPunishmentPreventionActive(CUpDownClient* client);
	const UINT GetLoadedDefinitionCount() const;

	CMap <uint8, uint8, uint8, uint8> m_CategoryPunishmentMap;
	bool m_bShieldFileLoaded;
protected:
	struct LeecherModNamesType
	{
		CString strModVersion = EMPTY;
		bool bCaseSensitiveMod = true; // Initializing this as true just to prevent unnecessary future MakeLower executions when string is empty.
		CString strClientVersion = EMPTY;
		bool bCaseSensitiveClient = true; // Initializing this as true just to prevent unnecessary future MakeLower executions when string is empty.

		inline bool operator==(const LeecherModNamesType& o) const
		{
			return	strModVersion == o.strModVersion &&
					bCaseSensitiveMod == o.bCaseSensitiveMod &&
					strClientVersion == o.strClientVersion &&
					bCaseSensitiveClient == o.bCaseSensitiveClient;
		}
	};

	CList<LeecherModNamesType, LeecherModNamesType&> HardLeecherModNamesList;
	CList<LeecherModNamesType, LeecherModNamesType&> SoftLeecherModNamesList;

	struct TextAndCaseType
	{
		CString strText = EMPTY;
		bool bCaseSensitive = true; // Initializing this as true just to prevent unnecessary future MakeLower executions when string is empty.

		inline bool operator==(const TextAndCaseType& o) const
		{
			return	strText == o.strText &&
				bCaseSensitive == o.bCaseSensitive;
		}

	};

	CList<TextAndCaseType, TextAndCaseType&> HardLeecherUserNamesList;
	CList<TextAndCaseType, TextAndCaseType&> SoftLeecherUserNamesList;
	CList<TextAndCaseType, TextAndCaseType&> SoftLeecherVeryCDUserNamesList;
	CList<TextAndCaseType, TextAndCaseType&> SpamMessagesList;

	struct UnknownTagsType
	{
		UINT uTagID = 0;
		CString strDescription = EMPTY;

		inline bool operator==(const UnknownTagsType& o) const
		{
			return	uTagID == o.uTagID && strDescription == o.strDescription;
		}
	};
	CList<UnknownTagsType, UnknownTagsType&> UnknownHelloTagsList;
	CList<UnknownTagsType, UnknownTagsType&> UnknownInfoTagsList;

	CStringList HardLeecherUserHashesList;
	CStringList SoftLeecherUserHashesList;
};
