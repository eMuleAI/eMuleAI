//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "eMuleAI/Address.h"

class CPartFile;
class CUpDownClient;
struct SaveSourcesData;

class CSourceSaver
{
public:
	CSourceSaver(void);
	~CSourceSaver(void);
	bool Process(CPartFile* file);
	void DeleteFile(CPartFile* file);
	CString CalcExpiration(int nDays);

	class CSourceData
	{
	public:

		CSourceData(CAddress cSourceIP, uint32 dwSourceID, uint16 wSourcePort, uint32 dwServerIP, uint16 wServerPort, const CString& strExpiration, uint8 uSXVer)
		{
			sourceIP = cSourceIP;
			sourceID = dwSourceID;
			sourcePort = wSourcePort;
			serverip = dwServerIP;
			serverport = wServerPort;
			partsavailable = 0;
			expiration = strExpiration;
			nSrcExchangeVer = uSXVer;
			bWasDownloading = false;
			bWasOnQueue = false;
		}

		CSourceData(CUpDownClient* client, const CString& strExpiration);

		CSourceData(CSourceData* pOld)
		{
			sourceIP = pOld->sourceIP;
			sourceID = pOld->sourceID; 
			sourcePort = pOld->sourcePort; 
			serverip = pOld->serverip;
			serverport = pOld->serverport;
			partsavailable = pOld->partsavailable;
			expiration = pOld->expiration;
			nSrcExchangeVer = pOld->nSrcExchangeVer;
			bWasDownloading = pOld->bWasDownloading;
			bWasOnQueue = pOld->bWasOnQueue;
		}

		bool Compare(CSourceData* tocompare) { return ((sourceIP == tocompare->sourceIP) && (sourcePort == tocompare->sourcePort)); }

		CAddress sourceIP;
		uint32	sourceID;
		uint16	sourcePort;
		uint32	serverip;
		uint16	serverport;
		uint32	partsavailable;
		CString	expiration;
		uint8	nSrcExchangeVer;
		// Same-session stop/resume state. Not serialized.
		bool	bWasDownloading;
		bool	bWasOnQueue;
	};

	typedef CTypedPtrList<CPtrList, CSourceData*> Sources;
	Sources sources;

	static CString GetSourcesFilePath(const CPartFile* file);
	void LoadSourcesFromFile(CPartFile* file);
	void SaveSources(CPartFile* file, bool bForce);
	UINT SaveSourcesOnStop(CPartFile* file);
	UINT LoadSourcesOnResume(CPartFile* file);
	SaveSourcesData* BuildSaveSourcesSnapshot(CPartFile* file, bool bForce, bool bMarkInQueue);
	static bool WriteSourcesSnapshotNow(const SaveSourcesData& data, bool bShutdownFallback);
	void AddSourcesToDownload(CPartFile* file);

protected:
	void ClearSources();
	UINT AddSourcesToDownload(CPartFile* file, Sources& sourceList);

	uint32	m_dwLastTimeLoaded;
	uint32  m_dwLastTimeSaved;
	
	bool IsExpired(CString expirationdate);
};
