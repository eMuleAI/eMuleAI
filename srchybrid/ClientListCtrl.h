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

	DECLARE_DYNAMIC(CClientListCtrl)

	//CImageList	*m_pImageList;
	CImageList		m_IconList;
public:
	CClientListCtrl();
	virtual	~CClientListCtrl();

	void	Init();
	void	AddClient(CUpDownClient* client);
	void	RemoveClient(CUpDownClient *client);
	void	RefreshClient(CUpDownClient* client, const int iIndex = -1);
	void	ReloadList(const bool bOnlySort, const EListStateField LsfFlag);
	void	RebuildListedItemsMap();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : reinterpret_cast<DWORD_PTR>(m_ListedItemsVector[iItem])); } // Return null if index invalid, otherwise return the pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const;
	void	SaveArchive(CUpDownClient* client);
	void	LoadArchive(CUpDownClient* client, const CString strCallingMethod);
	CUpDownClient* ArchivedToActive(CUpDownClient* client);
	std::vector<CUpDownClient*> m_ListedItemsVector; // This vector is used to list, iterate and sort clients.
	typedef	CMap<CUpDownClient*, CUpDownClient*, int, int&> CListedItemsMap;
	CListedItemsMap m_ListedItemsMap; // This map is used to lookup client index.
	void	Hide()					{ ShowWindow(SW_HIDE); }
	void	Show()					{ ShowWindow(SW_SHOW); }
	void	Localize();
	void	ShowSelectedUserDetails();
	bool	IsFilteredOut(const CUpDownClient* client);

protected:
	void SetAllIcons();
	CString GetItemDisplayText(const CUpDownClient *client, int iSubItem) const;
	static LPARAM	m_pSortParam;
	int 			m_iDataSize;
	const static bool SortFunc(const CUpDownClient* first, const CUpDownClient* second);
	int				m_iCountryFlagCount;

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	
	// Override to maintain sort order after updates
	virtual void MaintainSortOrderAfterUpdate() override;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnNMClick(NMHDR* pNMHDR, LRESULT* pResult);
};