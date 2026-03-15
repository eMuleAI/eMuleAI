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
#include "ToolTipCtrlX.h"

class CToolBarCtrlX : public CToolBarCtrl
{
	DECLARE_DYNAMIC(CToolBarCtrlX)
public:
	void AdjustFont(int iMaxPointSize, CSize sizButton);
	void RecalcLayout();
	void DeleteAllButtons();

	CString GetBtnText(int nID);
	void SetBtnText(int nID, LPCTSTR pszString);

	int GetBtnWidth(int nID);
	void SetBtnWidth(int nID, int iWidth);

	CSize GetPadding();
	void SetPadding(CSize sizPadding);

	DWORD GetBtnStyle(int id);
	DWORD AddBtnStyle(int id, DWORD dwStyle);
	DWORD RemoveBtnStyle(int id, DWORD dwStyle);

	int AddString(const CString &strToAdd);
	void EnsureCustomToolTips();

protected:
	CToolTipCtrlX m_tooltip;

	virtual void PreSubclassWindow();

	DECLARE_MESSAGE_MAP()
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
};
