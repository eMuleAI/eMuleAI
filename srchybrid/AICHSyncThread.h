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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once

#include <vector>

class CKnownFile;
class CSafeFile;
/////////////////////////////////////////////////////////////////////////////////////////
///CAICHSyncThread
class CAICHSyncThread : public CWinThread
{
	DECLARE_DYNCREATE(CAICHSyncThread)
protected:
	CAICHSyncThread()
		: m_bBuildMissingHashsets(true)
		, m_uMaxHashsetsToBuild(0)
		, m_iPendingHashCount(0)
		, m_iDeferredPartHashSetCount(0)
		, m_iProcessedHashCount(0)
		, m_pvecDeferredAICHFileHashes(NULL)
	{
	}
	~CAICHSyncThread() = default;
public:
	virtual BOOL InitInstance();
	virtual int	Run();
	static bool RunStartupSync(bool bBuildMissingHashsets = true, INT_PTR *piPendingHashCount = NULL, UINT uMaxHashsetsToBuild = 0, INT_PTR *piProcessedHashCount = NULL, std::vector<BYTE> *pvecDeferredAICHFileHashes = NULL);
	static bool BuildStartupDeferredAICHHashset(const uchar *pucFileHash);

protected:
	bool ConvertKnown2ToKnown264(CSafeFile &TargetFile);
	static void AppendAICHFileHash(const CKnownFile *pFile, std::vector<BYTE>& vecFileHashes);
	void QueueDeferredAICHFileHash(const CKnownFile *pFile) const;
	void QueueMissingAICHFileHash(const CKnownFile *pFile);

private:
	std::vector<BYTE> m_vecToHashFileHashes;
	bool m_bBuildMissingHashsets;
	UINT m_uMaxHashsetsToBuild;
	INT_PTR m_iPendingHashCount;
	INT_PTR m_iDeferredPartHashSetCount;
	INT_PTR m_iProcessedHashCount;
	std::vector<BYTE> *m_pvecDeferredAICHFileHashes;
};