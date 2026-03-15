#pragma once
#include "DialogMinTrayBtn.h"
#include "ResizableLib\ResizableDialog.h"

class CTrayDialog : public CDialogMinTrayBtn<CResizableDialog>
{
protected:
	typedef CDialogMinTrayBtn<CResizableDialog> CTrayDialogBase;

public:
	explicit CTrayDialog(UINT uIDD, CWnd *pParent = NULL);   // standard constructor

	void TraySetMinimizeToTray(bool *pbMinimizeToTray);
	BOOL TraySetMenu(UINT nResourceID);
	BOOL TraySetMenu(HMENU hMenu);
	BOOL TraySetMenu(LPCTSTR lpszMenuName);
	BOOL TrayUpdate();
	bool TrayShow();
	bool TrayHide();
	void TrayReset();
	void TraySetToolTip(LPCTSTR lpszToolTip);
	void TraySetIcon(HICON hIcon, bool bDelete = false);
	void TraySetIcon(UINT nResourceID);
	void TraySetIcon(LPCTSTR lpszResourceName);
	bool TrayIconVisible();

	virtual void TrayMinimizeToTrayChange();
	virtual void RestoreWindow();
	virtual void OnTrayLButtonDown();
	virtual void OnTrayLButtonUp();
	virtual void OnTrayLButtonDblClk();
	virtual void OnTrayRButtonUp(CPoint); //parameter used in the overriding method
	virtual void OnTrayRButtonDblClk();
	virtual void OnTrayMouseMove();

protected:
	bool *m_pbMinimizeToTray;
	HICON m_hPrevIconDelete;
	CMenu m_mnuTrayMenu;
	NOTIFYICONDATA m_nidIconData;
	UINT_PTR m_uSingleClickTimer;
	UINT u_DblClickSpeed;
	byte m_uLButtonDown;
	bool m_bCurIconDelete;
	bool m_bTrayIconVisible;
//	UINT m_nDefaultMenuItem;

private:
	void KillSingleClickTimer();

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnTrayNotify(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnTaskBarCreated(WPARAM wParam, LPARAM lParam);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
};