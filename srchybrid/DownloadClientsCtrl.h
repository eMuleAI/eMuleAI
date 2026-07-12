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
#include <utility>

class CUpDownClient;

class CDownloadClientsCtrl : public CMuleListCtrl, public CListCtrlItemWalk
{
	DECLARE_DYNAMIC(CDownloadClientsCtrl)

public:
	typedef DWORD DownloadClientItemID;

	class ClientReference
	{
	public:
		ClientReference();
		~ClientReference();
		ClientReference(const ClientReference&) = delete;
		ClientReference& operator=(const ClientReference&) = delete;

		void			Attach(CUpDownClient* pClient);
		void			Release();
		CUpDownClient*	Get() const							{ return m_pClient; }
		CUpDownClient*	operator->() const					{ return m_pClient; }
		operator CUpDownClient*() const					{ return m_pClient; }

	private:
		CUpDownClient* m_pClient;
	};

	CDownloadClientsCtrl();
	virtual	~CDownloadClientsCtrl();

	void	Init();
	void	AddClient(CUpDownClient* client);
	void	RemoveClient(CUpDownClient* client);
	void	RefreshClient(CUpDownClient* client);
	void	HideClient(CUpDownClient* client);
	void	ShowClient(CUpDownClient* client);
	void	UpdateView();
	bool	IsFilteredOut(CUpDownClient* client);
	void	Hide()			{ ShowWindow(SW_HIDE); }
	void	Show()			{ ShowWindow(SW_SHOW); }
	void	Localize();
	void	ShowSelectedUserDetails();
	virtual CObject* GetNextSelectableItem() override;
	virtual CObject* GetPrevSelectableItem() override;

	typedef std::set<DownloadClientItemID> ListItemsMapType;
	ListItemsMapType m_ListItemsMap;
	void	AddClientInternal(CUpDownClient* client);
	void	RemoveClientInternal(CUpDownClient* client);
protected:
	CImageList	*m_pImageList;
	CCriticalSection m_uiClientUpdateLock;
	std::set<DownloadClientItemID> m_PendingAddRuntimeIDs;
	std::map<DownloadClientItemID, DownloadClientItemID> m_PendingRemoveRuntimeIDs;
	std::set<DownloadClientItemID> m_PendingRefreshRuntimeIDs;
	std::set<DownloadClientItemID> m_PendingRemovalRuntimeIDs;
	volatile LONG m_lUiAddClientPending;
	volatile LONG m_lUiRemoveClientPending;
	volatile LONG m_lClientRowUpdatePending;
	volatile LONG m_lUiRemoveStaleClientPending;

	static CUpDownClient* AcquireRuntimeClient(DownloadClientItemID uRuntimeID);
	bool	ResolveArchivedClientForActiveClient(CUpDownClient* client, ClientReference& clientRef) const;
	bool	TryReplaceArchivedClient(CUpDownClient* client);
	void	QueueTrackedClientRemoval(DownloadClientItemID uRuntimeID);
	void	QueueUiAddClient(DownloadClientItemID uRuntimeID);
	void	QueueUiRemoveClient(DownloadClientItemID uRuntimeID, DownloadClientItemID uArchivedRuntimeID);
	void	QueueClientRowUpdate(DownloadClientItemID uRuntimeID);
	void	QueueUiRemoveStaleClient(DownloadClientItemID uRuntimeID);
	void	RefreshClientByRuntimeID(DownloadClientItemID uRuntimeID);
	bool	ResolveTrackedClient(DownloadClientItemID uRuntimeID, ClientReference& clientRef);
	bool	GetClientFromItem(int iItem, ClientReference& clientRef, DownloadClientItemID* puRuntimeID = NULL);
	bool	GetSelectedClient(ClientReference& clientRef, DownloadClientItemID* puRuntimeID = NULL, int* piItem = NULL);
	bool	IsDisplayableClient(const CUpDownClient* client) const;
	bool	ReplaceTrackedClient(DownloadClientItemID uOldRuntimeID, CUpDownClient* pNewClient);
	void	RemoveTrackedClientByRuntimeID(DownloadClientItemID uRuntimeID, bool bUpdateCount = true);
	int		PurgeVisibleRows(DownloadClientItemID uRuntimeID, int iKeepItem = -1);
	int		FindItemIndexByRuntimeID(DownloadClientItemID uRuntimeID) const;
	int		FindSortedInsertIndex(DownloadClientItemID uRuntimeID);
	void SetAllIcons();
	CString GetItemDisplayText(CUpDownClient *client, int iSubItem) const;
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

	// Client row updates use the throttled list sort path.
	virtual bool ShouldMaintainSortOrderOnUpdate() const override { return GetSortItem() != -1; }
	virtual void MaintainSortOrderAfterUpdate() override;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnUiAddClient(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUiRemoveClient(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnClientRowUpdate(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUiRemoveStaleClient(WPARAM wParam, LPARAM lParam);

	enum {
		WM_DOWNLOADCLIENTSCTRL_ADD_CLIENT = WM_APP + 4050,
		WM_DOWNLOADCLIENTSCTRL_REMOVE_CLIENT = WM_APP + 4051,
		WM_DOWNLOADCLIENTSCTRL_UPDATE_CLIENT_ROW = WM_APP + 4052,
		WM_DOWNLOADCLIENTSCTRL_REMOVE_STALE_CLIENT = WM_APP + 4053
	};
};
