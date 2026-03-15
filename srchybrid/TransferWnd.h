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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "ResizableLib\ResizableFormView.h"
#include "SplitterControl.h"
#include "ClosableTabCtrl.h"
#include "UploadListCtrl.h"
#include "DownloadListCtrl.h"
#include "QueueListCtrl.h"
#include "ClientListCtrl.h"
#include "DownloadClientsCtrl.h"
#include "DropDownButton.h"
#include "EditDelayed.h"
#include "ComboBoxEx2.h"
#include <unordered_set>

class CTransferWnd : public CResizableFormView
{
	DECLARE_DYNCREATE(CTransferWnd)

	enum
	{
		IDD = IDD_TRANSFER
	};

	enum EWnd1Icon
	{
		w1iSplitWindow = 0,
		w1iDownloadFiles,
		w1iUploading,
		w1iDownloading,
		w1iOnQueue,
		w1iClientsKnown
	};

	enum EWnd2Icon
	{
		w2iUploading = 0,
		w2iDownloading,
		w2iOnQueue,
		w2iClientsKnown
	};

public:
	enum EWnd2
	{
		wnd2Downloading = 0,
		wnd2Uploading = 1,
		wnd2OnQueue = 2,
		wnd2Clients = 3
	};

	explicit CTransferWnd(CWnd *pParent = NULL);   // standard constructor
	virtual	~CTransferWnd();
	CTransferWnd(const CTransferWnd&) = delete;
	CTransferWnd& operator=(const CTransferWnd&) = delete;

	void ShowQueueCount(INT_PTR number);
	void UpdateListCount();
	void Localize();
	void UpdateCatTabTitles(bool force = true);
	void UpdateCatTabTitlesIfDirty(bool force = false); // updates only if dirty unless forced
	void InvalidateCatTabInfo(); // mark cached cat tab info dirty
	void VerifyCatTabSize();
	int	 AddCategory(const CString &newtitle, const CString &newincoming, const CString &newcomment, const CString &newautocat, bool addTab = true);
	int	 AddCategoryInteractive();
	void SwitchUploadList();
	void ResetTransToolbar(bool bShowToolbar, bool bResetLists = true);
	void SetToolTipsDelay(DWORD dwDelay);
	void OnDisableList();

	// Active download tracking system for performance optimization
	void	AddActiveDownload(class CPartFile* pFile);
	void	RemoveActiveDownload(class CPartFile* pFile);
	void	UpdateActiveDownloadStatus(class CPartFile* pFile);
	void	RebuildActiveDownloadCache(); // full rebuild when needed
	int		GetActiveDownloadCount() const;

	CUploadListCtrl			uploadlistctrl;
	CDownloadListCtrl		downloadlistctrl;
	CQueueListCtrl			queuelistctrl;
	CClientListCtrl			clientlistctrl;
	CDownloadClientsCtrl	downloadclientsctrl;

	CStringArray m_astrFilterDownloadList;
	CStringArray m_astrFilterUploadList;
	CStringArray m_astrFilterDownloadClients;
	CStringArray m_astrFilterQueueList;
	CStringArray m_astrFilterClientList;
	uint32	GetFilterColumnDownloadList() const { return m_nFilterColumnDownloadList; }
	uint32	GetFilterColumnUploadList() const { return m_nFilterColumnUploadList; }
	uint32	GetFilterColumnDownloadClients() const { return m_nFilterColumnDownloadClients; }
	uint32	GetFilterColumnQueueList() const { return m_nFilterColumnQueueList; }
	uint32	GetFilterColumnClientList() const { return m_nFilterColumnClientList; }
protected:
	POINT		m_pLastMousePoint;
	CSplitterControl m_wndSplitter;
public:
	CDropDownButton	m_btnWnd2;
	CDropDownButton	m_btnWnd1;
	EWnd2	m_uWnd2;
protected:
	CToolTipCtrlX m_tooltipCats;
	//TabControl	m_dlTab;
	CClosableTabCtrl m_dlTab;
	CImageList	*m_pDragImage;
public:
	uint32		m_dwShowListIDC;
protected:
	int		m_rightclickindex;
	int		m_nDragIndex;
	int		m_nDropIndex;
	int		m_nLastCatTT;
	int		m_isetcatmenu;
	bool	m_bIsDragging;
	bool	downloadlistactive;
	bool	m_bLayoutInited;
	CStringArray m_astrFilterTemp;
	bool	m_bColumnDiff;
	CString	m_strFullFilterExpr;
	uint32	m_nFilterColumnLastAppliedDownloadList;
	uint32	m_nFilterColumnLastAppliedUploadList;
	uint32	m_nFilterColumnLastAppliedDownloadClients;
	uint32	m_nFilterColumnLastAppliedQueueList;
	uint32	m_nFilterColumnLastAppliedClientList;

	void	ShowWnd2(EWnd2 uWnd2);
	void	SetWnd2(EWnd2 uWnd2);
	void	DoResize(int delta);
	void	UpdateSplitterRange();
	void	SetAllIcons();
	void	SetWnd1Icons();
	void	SetWnd2Icons();
	void	UpdateTabToolTips()				{ UpdateTabToolTips(-1); }
	void	UpdateTabToolTips(int tab);
	CString	GetTabStatistic(int tab);
	bool    m_bCatTabInfoDirty = true; // true if tab info needs to be updated
	int     m_cachedActiveDwl = -1; // number of active (transferring & visible) downloads last computed
	
	// Active download tracking system for performance optimization  
	std::unordered_set<class CPartFile*> m_activeDownloads; // set of files currently transferring
	
	int		GetTabUnderMouse(const CPoint &point);
	int		GetItemUnderMouse(CListCtrl &ctrl);
	CString	GetCatTitle(int catid);
	void	EditCatTabLabel(int index, CString newlabel);
	void	EditCatTabLabel(int index);
	void	ShowList(uint32 dwListIDC);
	void	SetWnd1Icon(EWnd1Icon iIcon);
	void	SetWnd2Icon(EWnd2Icon iIcon);
	void	ShowSplitWindow(bool bReDraw = false);
	void	LocalizeToolbars();

	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual void OnInitialUpdate();
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM);

	CEditDelayed m_ctlFilterDownloadList;
	CEditDelayed m_ctlFilterUploadList;
	CEditDelayed m_ctlFilterDownloadClients;
	CEditDelayed m_ctlFilterQueueList;
	CEditDelayed m_ctlFilterClientList;
	//CComboBox m_FileTypeComboBox;
	CComboBoxEx2 m_FileTypeComboBox;
	CHeaderCtrl m_ctlListHeaderDownloadList;
	CHeaderCtrl m_ctlListHeaderUploadList;
	CHeaderCtrl m_ctlListHeaderDownloadClients;
	CHeaderCtrl m_ctlListHeaderQueueList;
	CHeaderCtrl m_ctlListHeaderClientList;
	uint32 m_nFilterColumnDownloadList;
	uint32 m_nFilterColumnUploadList;
	uint32 m_nFilterColumnDownloadClients;
	uint32 m_nFilterColumnQueueList;
	uint32 m_nFilterColumnClientList;

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnInvalidateCatTabInfo(WPARAM, LPARAM);
	afx_msg LRESULT OnUpdateCatTabTitlesIfDirty(WPARAM, LPARAM);
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnBnClickedChangeView();
	afx_msg void OnBnClickedQueueRefreshButton();
	afx_msg void OnDblClickDltab();
	afx_msg void OnHoverDownloadList(LPNMHDR, LRESULT *pResult);
	afx_msg void OnHoverUploadList(LPNMHDR, LRESULT *pResult);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLvnBeginDragDownloadList(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnKeydownDownloadList(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnNmRClickDltab(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg void OnSplitterMoved(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnTabMovement(LPNMHDR, LRESULT*);
	afx_msg void OnTcnSelchangeDltab(LPNMHDR, LRESULT *pResult);
	afx_msg void OnWnd1BtnDropDown(LPNMHDR, LRESULT*);
	afx_msg void OnWnd2BtnDropDown(LPNMHDR, LRESULT*);
	afx_msg void OnPaint();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg LRESULT OnChangeFilter(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedPreview();
	afx_msg void OnFileTypeChange();
	afx_msg void OnBnClickedFreeze();
	afx_msg void OnBnClickedArchived();
	afx_msg void OnBnClickedQueryable();
	afx_msg void OnBnClickedQueried();
	afx_msg void OnBnClickedValidIP();
	afx_msg void OnBnClickedHighId();
	afx_msg void OnBnClickedBadClient();
};
