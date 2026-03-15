#pragma once
#include "MuleListCtrl.h"
#include "ToolTipCtrlX.h"

class CServerList;
class CServer;

class CServerListCtrl : public CMuleListCtrl
{
	DECLARE_DYNAMIC(CServerListCtrl)

	CImageList	*m_pImageList;
public:
	CServerListCtrl();

	bool	Init();
	bool	AddServer(const CServer *pServer, bool bAddToList = true, bool bRandom = false);
	void	RemoveServer(const CServer *pServer);
	bool	AddServerMetToList(const CString &strFile);
	void	RefreshServer(const CServer *pServer);
	void	RefreshAllServer();
	void	RemoveAllDeadServers();
	void	RemoveAllFilteredServers();
	void	Hide()									{ ShowWindow(SW_HIDE); }
	void	Visible()								{ ShowWindow(SW_SHOW); }
	void	Localize();
	void	ShowServerCount();
	bool	StaticServerFileAppend(CServer *pServer);
	bool	StaticServerFileRemove(CServer *pServer);
private:
	static int Undefined_at_bottom(const uint32 i1, const uint32 i2);
	static int Undefined_at_bottom(const CString &s1, const CString &s2);
	int FindServer(const CServer *pServer);

protected:
	CToolTipCtrlX	m_tooltip;

	CString CreateSelectedServersURLs();
	void DeleteSelectedServers();

	void SetSelectedServersPriority(UINT uPriority);
	void SetAllIcons();
	CString GetItemDisplayText(const CServer *server, int iSubItem) const;
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

	CImageList imagelist;
	afx_msg void OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT*);
	afx_msg void OnSysColorChange();
};