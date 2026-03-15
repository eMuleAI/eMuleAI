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
#include "UploadListCtrl.h"
#include "DownloadListCtrl.h"
#include "QueueListCtrl.h"
#include "ClientListCtrl.h"
#include "DownloadClientsCtrl.h"
#include "TransferWnd.h"
#include "ToolbarWnd.h"

class CTransferDlg : public CFrameWnd
{
	DECLARE_DYNCREATE(CTransferDlg)

public:
	CTransferDlg();           // protected constructor used by dynamic creation
	CTransferWnd *m_pwndTransfer;

	BOOL CreateWnd(CWnd *pParent);

	//Wrappers
	void Localize();
	void ShowQueueCount(INT_PTR number);
	void UpdateCatTabTitles(bool force = true);
	void UpdateCatTabTitlesIfDirty(bool force = false); // wrapper for conditional update
	void InvalidateCatTabInfo(); // wrapper to mark dirty
	
	// Active download tracking system wrappers for performance optimization
	void AddActiveDownload(class CPartFile* pFile);
	void RemoveActiveDownload(class CPartFile* pFile);
	void UpdateActiveDownloadStatus(class CPartFile* pFile);
	void RebuildActiveDownloadCache();
	int  GetActiveDownloadCount() const;
	
	void VerifyCatTabSize();
	int	 AddCategoryInteractive();
	void SwitchUploadList();
	void ResetTransToolbar(bool bShowToolbar, bool bResetLists = true);
	void SetToolTipsDelay(DWORD dwDelay);
	void OnDisableList();
	int	 AddCategory(const CString &newtitle, const CString &newincoming, const CString &newcomment, const CString &newautocat, bool addTab = true);
	void ShowToolbar(bool bShow);

	CUploadListCtrl*		GetUploadList();
	CDownloadListCtrl*		GetDownloadList();
	CQueueListCtrl*			GetQueueList();
	CClientListCtrl*		GetClientList();
	CDownloadClientsCtrl*	GetDownloadClientsList();

	CToolbarWnd m_wndToolbar;
protected:

	virtual BOOL PreTranslateMessage(MSG *pMsg);
	void DockToolbarWnd();

	DECLARE_MESSAGE_MAP()
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	afx_msg void OnClose();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
};
