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
#pragma once
#include "MuleListCtrl.h"
#include "ListCtrlItemWalk.h"
#include "UploadQueue.h"
#include <map>
#include <set>
#include <vector>
#include "OtherFunctions.h"

class CUpDownClient;

class CClientListCtrl : public CMuleListCtrl, public CListCtrlItemWalk, public CListStateTemplate<CClientListCtrl, CUpDownClient>
{
	friend class CListStateTemplate<CClientListCtrl, CUpDownClient>;

private:
	using ListStateHelper = CListStateTemplate<CClientListCtrl, CUpDownClient>;
public:
	using ListStateHelper::SaveListState;
	using ListStateHelper::RestoreListState;

	enum ERefreshSortImpact
	{
		kSortImpactNone = 0x00000000,
		kSortImpactUserName = 0x00000001,
		kSortImpactUploadState = 0x00000002,
		kSortImpactTransferredUp = 0x00000004,
		kSortImpactDownloadState = 0x00000008,
		kSortImpactTransferredDown = 0x00000010,
		kSortImpactSoftware = 0x00000020,
		kSortImpactClientStatus = 0x00000040,
		kSortImpactHash = 0x00000080,
		kSortImpactIpPort = 0x00000100,
		kSortImpactGeolocation = 0x00000200,
		kSortImpactSharedFiles = 0x00000400,
		kSortImpactFriend = 0x00000800,
		kSortImpactIdType = 0x00001000,
		kSortImpactPunishment = 0x00002000,
		kSortImpactFirstSeen = 0x00004000,
		kSortImpactLastSeen = 0x00008000,
		kSortImpactNote = 0x00010000,
		kSortImpactAll = 0x7FFFFFFF
	};

	DECLARE_DYNAMIC(CClientListCtrl)

	//CImageList	*m_pImageList;
	CImageList		m_IconList;
public:
	CClientListCtrl();
	virtual	~CClientListCtrl();

	void	Init();
	void	AddClient(CUpDownClient* client);
	void	RemoveClient(CUpDownClient *client);
	void	RefreshClient(CUpDownClient* client, const int iIndex = -1, const uint32 uSortImpactFlags = kSortImpactAll);
	void	ReloadList(const bool bOnlySort, const EListStateField LsfFlag);
	void	MarkDeferredReload();
	void	FlushDeferredReload(const EListStateField LsfFlag);
	void	RebuildListedItemsMap();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : static_cast<DWORD_PTR>(iItem + 1)); } // Owner-data row data is a stable visible index, not a backend pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const;
	void	SaveArchive(CUpDownClient* client);
	void	LoadArchive(CUpDownClient* client, const CString strCallingMethod);
	CUpDownClient* ArchivedToActive(CUpDownClient* client);
	std::vector<CUpDownClient*> m_ListedItemsVector; // This vector is used to list, iterate and sort clients.
	std::vector<DWORD> m_ListedItemRuntimeIDs; // Parallel immutable runtime IDs for stale-safe resolution.
	typedef DWORD ClientListItemID;
	typedef	CMap<CUpDownClient*, CUpDownClient*, int, int&> CListedItemsMap;
	CListedItemsMap m_ListedItemsMap; // This map is used to lookup client index.
	typedef std::map<ClientListItemID, int> CListedRuntimeIndexMap;
	CListedRuntimeIndexMap m_ListedRuntimeIndexMap;
	bool m_bDeferredReload;
	void	Hide()					{ ShowWindow(SW_HIDE); }
	void	Show()					{ ShowWindow(SW_SHOW); }
	void	Localize();
	void	ShowSelectedUserDetails();
	bool	IsFilteredOut(const CUpDownClient* client);
	virtual CObject* GetNextSelectableItem() override;
	virtual CObject* GetPrevSelectableItem() override;

protected:
	struct SQueuedClientRowUpdate
	{
		int iIndex;
		uint32 uSortImpactFlags;
	};

	CCriticalSection m_uiClientRefreshLock;
	std::set<ClientListItemID> m_PendingAddRuntimeIDs;
	std::map<ClientListItemID, ClientListItemID> m_PendingRemoveRuntimeIDs;
	std::map<ClientListItemID, SQueuedClientRowUpdate> m_PendingClientRowUpdates;
	volatile LONG m_lUiAddClientPending;
	volatile LONG m_lUiRemoveClientPending;
	volatile LONG m_lClientRowUpdatePending;

	void SetAllIcons();
	void AddClientInternal(CUpDownClient* client);
	void RemoveClientInternal(CUpDownClient* client);
	void RemoveClientByRuntimeID(ClientListItemID uClientRuntimeID, ClientListItemID uArchivedRuntimeID);
	void RemoveClientsByRuntimeIDBatch(const std::map<ClientListItemID, ClientListItemID>& pendingRemoves);
	void QueueUiAddClient(ClientListItemID uRuntimeID);
	void QueueUiRemoveClient(ClientListItemID uRuntimeID, ClientListItemID uArchivedRuntimeID);
	void QueueClientRowUpdate(ClientListItemID uRuntimeID, int iIndex, uint32 uSortImpactFlags);
	int FindListedClientIndexByRuntimeID(ClientListItemID uRuntimeID) const;
	void RefreshClientInternal(CUpDownClient* client, const int iIndex = -1, const uint32 uSortImpactFlags = kSortImpactAll);
	bool IsSortOrderAffectedByRefresh(const uint32 uSortImpactFlags) const;
	bool RepositionUpdatedClient(int iIndex);
	void RefreshLocalizedTextCache();
	CString GetClientStatusText(const CUpDownClient *client) const;
	CString GetSharedFilesStatusText(const CUpDownClient *client) const;
	void RequestClientListRedrawForRange(int iFirst, int iLast);
	void RequestClientListRedraw();
	CString GetItemDisplayText(const CUpDownClient *client, int iSubItem) const;
	virtual int GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const override;
	static LPARAM	m_pSortParam;
	int 			m_iDataSize;
	const static bool SortFunc(const CUpDownClient* first, const CUpDownClient* second);
	int				m_iCountryFlagCount;
	CString	m_strUnknown;
	CString	m_strYes;
	CString	m_strNo;
	CString	m_strIdLow;
	CString	m_strIdHigh;
	CString	m_strArchived;
	CString	m_strConnected;
	CString	m_strDisconnected;
	CString	m_strDisabled;
	CString	m_strSharedFilesQuerying;
	CString	m_strSharedFilesAutoQuery;
	CString	m_strSharedFilesNotQueried;
	CString	m_strSharedFilesNoResponse;
	CString	m_strSharedFilesReceived;
	CString	m_strSharedFilesAccessDenied;

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	
	// Client refreshes reposition only the affected virtual row when the active sort needs it.
	virtual bool ShouldMaintainSortOrderOnUpdate() const override { return false; }
	virtual void MaintainSortOrderAfterUpdate() override;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnNMClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnUiAddClient(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUiRemoveClient(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnClientRowUpdate(WPARAM wParam, LPARAM lParam);

	enum
	{
		WM_CLIENTLISTCTRL_ADD_CLIENT = WM_APP + 4060,
		WM_CLIENTLISTCTRL_REMOVE_CLIENT = WM_APP + 4061,
		WM_CLIENTLISTCTRL_UPDATE_CLIENT_ROW = WM_APP + 4062
	};
};
