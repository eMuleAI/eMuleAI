#pragma once

#include "SafeFile.h"
#include "OtherFunctions.h"

///////////////////////////////////////////////////////////////////////////////
// ESearchType

//NOTE: The numbers are *equal* to items position in the combo box -> TODO: use item data
enum ESearchType : uint8
{
	SearchTypeAutomatic = 0,
	SearchTypeEd2kServer,
	SearchTypeEd2kGlobal,
	SearchTypeKademlia
};


#define	MAX_SEARCH_EXPRESSION_LEN	512

///////////////////////////////////////////////////////////////////////////////
// SSearchParams

struct SSearchParams
{
	SSearchParams()
		: ullMinSize()
		, ullMaxSize()
		, dwSearchID(_UI32_MAX)
		, uAvailability()
		, uComplete()
		, uiMinBitrate()
		, uiMinLength()
		, eType(SearchTypeEd2kServer)
		, bClientSharedFiles()
		, bMatchKeywords()
		, m_strClientHash()
		, strExtension()
		, strCodec()
		, strTitle()
		, strAlbum()
		, strArtist()
		, strSearchServerName()
		, dwSearchServerIP()
		, nSearchServerPort()
	{
	}

	explicit SSearchParams(CFileDataIO &rFile, uint8 byStoredSearchesVersion = 102)
		: ullMinSize()
		, ullMaxSize()
		, strMinSize()
		, strMaxSize()
		, bMatchKeywords()
	{
		dwSearchID = rFile.ReadUInt32();
		eType = (ESearchType)rFile.ReadUInt8();
		bClientSharedFiles = rFile.ReadUInt8() > 0;
		strSpecialTitle = rFile.ReadString(true);
		strExpression = rFile.ReadString(true);
		strFileType = rFile.ReadString(true);
		m_strClientHash = rFile.ReadString(true);
		uAvailability = rFile.ReadUInt32();
		uComplete = rFile.ReadUInt32();
		uiMinBitrate = rFile.ReadUInt32();
		uiMinLength = rFile.ReadUInt32();
		strMinSize = rFile.ReadString(true);
		strMaxSize = rFile.ReadString(true);
		strExtension = rFile.ReadString(true);
		strCodec = rFile.ReadString(true);
		strTitle = rFile.ReadString(true);
		strAlbum = rFile.ReadString(true);
		strArtist = rFile.ReadString(true);
		if (byStoredSearchesVersion >= 102) {
			strSearchServerName = rFile.ReadString(true);
			dwSearchServerIP = rFile.ReadUInt32();
			nSearchServerPort = rFile.ReadUInt16();
		}
	}

	void StorePartially(CFileDataIO &rFile) const
	{
		rFile.WriteUInt32(dwSearchID);
		rFile.WriteUInt8(static_cast<uint8>(eType));
		rFile.WriteUInt8(static_cast<uint8>(bClientSharedFiles));
		rFile.WriteString(strSpecialTitle, UTF8strRaw);
		rFile.WriteString((strBooleanExpr.IsEmpty() ? strExpression : strBooleanExpr), UTF8strRaw);
		rFile.WriteString(strFileType, UTF8strRaw);
		rFile.WriteString(m_strClientHash, UTF8strRaw);
		rFile.WriteUInt32(uAvailability);
		rFile.WriteUInt32(uComplete);
		rFile.WriteUInt32(uiMinBitrate);
		rFile.WriteUInt32(uiMinLength);
		rFile.WriteString(strMinSize, UTF8strRaw);
		rFile.WriteString(strMaxSize, UTF8strRaw);
		rFile.WriteString(strExtension, UTF8strRaw);
		rFile.WriteString(strCodec, UTF8strRaw);
		rFile.WriteString(strTitle, UTF8strRaw);
		rFile.WriteString(strAlbum, UTF8strRaw);
		rFile.WriteString(strArtist, UTF8strRaw);
		rFile.WriteString(strSearchServerName, UTF8strRaw);
		rFile.WriteUInt32(dwSearchServerIP);
		rFile.WriteUInt16(nSearchServerPort);
}

	CString strSearchTitle;
	CString strExpression;
	CStringW strKeyword;
	CString strBooleanExpr;
	CString strFileType;
	CString strMinSize;
	CString strMaxSize;
	CString strExtension;
	CString strCodec;
	CString strTitle;
	CString strAlbum;
	CString strArtist;
	CString strSpecialTitle;
	CString strSearchServerName;
	uint64 ullMinSize;
	uint64 ullMaxSize;
	uint32 dwSearchID;
	UINT uAvailability;
	UINT uComplete;
	UINT uiMinBitrate;
	UINT uiMinLength;
	ESearchType eType;
	bool bClientSharedFiles;
	bool bMatchKeywords;
	CString m_strClientHash;
	uint32 dwSearchServerIP;
	uint16 nSearchServerPort;
};

bool GetSearchPacket(CSafeMemFile &data, SSearchParams *pParams, bool bTargetSupports64Bit, bool *pbPacketUsing64Bit);
