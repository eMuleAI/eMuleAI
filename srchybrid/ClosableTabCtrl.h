#pragma once

class CClosableTabCtrl : public CTabCtrl
{
	DECLARE_DYNAMIC(CClosableTabCtrl)

public:
	CClosableTabCtrl();
	virtual	~CClosableTabCtrl();
	BOOL DeleteItem(int nItem);

	bool m_bClosable;
	bool m_bShowCloseButton;

	bool m_bDownloadCategoryStyle;
	UINT GetLastMovementSource() const { return m_nSrcTab; }
	UINT GetLastMovementDestionation() const { return m_nDstTab; }
	BOOL ReorderTab(unsigned int nSrcTab, unsigned int nDstTab);
	void SetTabTextColor(int index, DWORD color);
	bool m_bDragging;     // Specifies that whether drag 'n drop is in progress.

	int GetTabUnderContextMenu() const;

	LONG InsertItem(int nItem, TCITEM* pTabCtrlItem);
	LONG InsertItem(int nItem, LPCTSTR lpszItem);
	LONG InsertItem(int nItem, LPCTSTR lpszItem, int nImage);

	CSpinButtonCtrl* m_pSpinCtrl;

private:
	void CatchSpinControl();
	int m_nHoverTabIndex = -1; // Index of the tab currently hovered by the mouse

protected:
	CImageList m_ImgLstCloseButton;
	IMAGEINFO m_iiCloseButton;
	CPoint m_ptCtxMenu;

	void InternalInit();
	void SetAllIcons();
	void GetCloseButtonRect(int iItem, const CRect &rcItem, CRect &rcCloseButton, bool bItemSelected, bool bVistaThemeActive);
	int GetTabUnderPoint(const CPoint &point) const;
	bool SetDefaultContextMenuPos();

	virtual void PreSubclassWindow();
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	UINT m_nSrcTab;       // Specifies the source tab that is going to be moved.
	UINT m_nDstTab;       // Specifies the destination tab (drop position).
	bool m_bHotTracking;  // Specifies the state of whether the tab control has hot tracking enabled.
	CRect m_InsertPosRect;
	CPoint m_lclickPoint;

	BOOL DragDetectPlus(CWnd* Handle, CPoint p);
	bool DrawIndicator(CPoint point);

	DECLARE_MESSAGE_MAP()
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnSysColorChange();
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg LRESULT _OnThemeChanged();
	afx_msg HBRUSH CtlColor(CDC*, UINT);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMeasureItem(int, LPMEASUREITEMSTRUCT);
	afx_msg void MeasureItem(LPMEASUREITEMSTRUCT);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd*);
	afx_msg void OnPaint();
	afx_msg void OnMouseLeave();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
};