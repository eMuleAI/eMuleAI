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

#include "ResizableLib/ResizablePage.h"
#include "ResizableLib/ResizableSheet.h"
#include "ListViewWalkerPropertySheet.h"
#include "ClosableTabCtrl.h"

class CUpDownClient;

///////////////////////////////////////////////////////////////////////////////
// CClientDetailPage

class CClientDetailPage : public CResizablePage
{
	DECLARE_DYNAMIC(CClientDetailPage)

	enum
	{
		IDD = IDD_SOURCEDETAILWND
	};

public:
	CClientDetailPage();   // standard constructor
	virtual BOOL OnInitDialog();
	void Localize();

	void SetClients(const CSimpleArray<CObject*> *paClients)
	{
		m_paClients = paClients;
		m_bDataChanged = true;
	}

protected:
	const CSimpleArray<CObject*> *m_paClients;
	bool m_bDataChanged;


	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnSetActive();

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);

private:
	HICON countryflag;
};


///////////////////////////////////////////////////////////////////////////////
// CClientDetailDialog

class CClientDetailDialog : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CClientDetailDialog)

	void Localize();
public:
	explicit CClientDetailDialog(CUpDownClient *pClient, CListCtrlItemWalk *pListCtrl = NULL);
	explicit CClientDetailDialog(const CSimpleArray<CUpDownClient*> *paClients, CListCtrlItemWalk *pListCtrl = NULL);
	virtual BOOL OnInitDialog();


protected:
	CClientDetailPage m_wndClient;
	CClosableTabCtrl m_tabDark;

	void Construct();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
};