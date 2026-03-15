//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "MuleListCtrl.h"

class CUpDownClient;
namespace Kademlia
{
	class CEntry;
};

class CCommentListCtrl : public CMuleListCtrl
{
	DECLARE_DYNAMIC(CCommentListCtrl)

public:
	CCommentListCtrl() = default;

	void Init();
	void AddItem(const CUpDownClient *client);
	void AddItem(const Kademlia::CEntry *entry);

private:
	enum ECols
	{
		colRating = 0,
		colComment,
		colFileName,
		colUserName,
		colOrigin
	};

protected:
	struct SComment
	{
		SComment(const void *pClientCookie, int iRating, const CString &strComment,
			     const CString &strFileName, const CString &strUserName, int iOrigin)
			: m_pClientCookie(pClientCookie)
			, m_strComment(strComment)
			, m_strFileName(strFileName)
			, m_strUserName(strUserName)
			, m_iOrigin(iOrigin)
			, m_iRating(iRating)
		{
		}

		const void *m_pClientCookie;
		CString m_strComment;
		CString m_strFileName;
		CString m_strUserName;
		int m_iOrigin;	// 0=eD2K, 1=Kad
		int m_iRating;
	};
	void AddComment(const SComment *pComment);
	int FindClientComment(const void *pClientCookie);

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnDeleteItem(LPNMHDR pNMHDR, LRESULT *pResult);
};