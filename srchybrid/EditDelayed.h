// EditDelayed.h
#pragma once

//////////////////////////////////////////////////////////////////////////////
// CIconWnd

class CIconWnd : public CStatic
{
public:
	CIconWnd();
	virtual	~CIconWnd();

	void	SetImageList(CImageList* pImageList) { m_pImageList = pImageList; }

protected:
	CImageList* m_pImageList;
	int m_nCurrentIcon;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
};

//////////////////////////////////////////////////////////////////////////////
// CEditDelayed

class CEditDelayed : public CEdit
{
	DECLARE_DYNAMIC(CEditDelayed) // Enable runtime class information

public:
	CEditDelayed();

	void	OnInit(CHeaderCtrl* pColumnHeader, CArray<int, int>* paIgnoredColumns = NULL);
	void	ShowColumnText(bool bShow);

	// when not using pColumnHeader this text will be shown when the control is empty and has no focus
	void	SetAlternateText(const CString& rstrText) { m_strAlternateText = rstrText; }
	bool	m_bShowsColumnText;

	struct SFilterParam
	{
		bool bForceApply;
		uint32  uColumnIndex;
	};

protected:
	bool		m_bShuttingDown;
	UINT_PTR	m_uTimerResult;
	DWORD		m_dwLastModified;
	CString		m_strLastEvaluatedContent;
	CIconWnd	m_iwReset;
	CIconWnd	m_iwPaste;
	CIconWnd	m_iwColumn;
	HCURSOR		m_hCursor;
	CPoint		m_pointMousePos;
	bool		m_bShowResetButton;
	int			m_nCurrentColumnIdx;
	CString		m_strAlternateText;
	CHeaderCtrl* m_pctrlColumnHeader;
	CArray<int, int> m_aIgnoredColumns;

	void	DoDelayedEvalute(bool bForce = false);
	void	SetEditRect();
	virtual void PreSubclassWindow();
	HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnEnChange();
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnCommand(WPARAM wParam, LPARAM);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnNcDestroy(); // cleanup for DarkMode prop
};