#pragma once
#include "TreeOptionsCtrl.h"

typedef struct
{
	NMHDR nmhdr;
	HTREEITEM hItem;
} TREEOPTSCTRLNOTIFY;


// predefined treeview image list indices
#define	TREEOPTSCTRLIMG_COMMAND	10
#define	TREEOPTSCTRLIMG_EDIT	11
#define	TREEOPTSCTRLIMG_GROUP	13


///////////////////////////////////////////////////////////////////////////////
// CTreeOptionsCtrlEx

class CTreeOptionsCtrlEx : public CTreeOptionsCtrl
{
public:
	explicit CTreeOptionsCtrlEx(UINT uImageListColorFlags = ILC_COLOR);

	void SetEditLabel(HTREEITEM hItem, const CString &rstrLabel);
	HTREEITEM InsertGroup(LPCTSTR lpszItem, int nImage, HTREEITEM hParent = TVI_ROOT, HTREEITEM hAfter = TVI_LAST, DWORD dwItemData = _UI32_MAX);
	HTREEITEM InsertCommandButton(LPCTSTR lpszItem, HTREEITEM hParent, HTREEITEM hAfter = TVI_LAST, DWORD dwItemData = _UI32_MAX);
	BOOL IsCommandButton(HTREEITEM hItem);
	void UpdateCheckBoxGroup(HTREEITEM hItem);
	void SetImageListColorFlags(UINT uImageListColorFlags);

	virtual void OnCreateImageList();
	virtual void HandleChildControlLosingFocus();
	virtual BOOL SetRadioButton(HTREEITEM hParent, int nIndex);
	virtual BOOL SetRadioButton(HTREEITEM hItem);
	BOOL NotifyParent(UINT uCode, HTREEITEM hItem);

protected:
	UINT m_uImageListColorFlags;
	HTREEITEM m_hHotCommandButton;
	HTREEITEM m_hPressedCommandButton;
	BOOL m_bTrackingCommandButtonMouse;

	virtual void HandleCheckBox(HTREEITEM hItem, BOOL bCheck);
	HTREEITEM HitTestCommandButton(CPoint point);
	void InvalidateCommandButton(HTREEITEM hItem);
	CRect GetCommandButtonRect(HTREEITEM hItem);
	void DrawCommandButton(CDC &dc, HTREEITEM hItem);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg BOOL OnCustomDraw(LPNMHDR pNMHDR, LRESULT *pResult);
};

//Dialog Data exchange support

void DDX_TreeCheck(CDataExchange *pDX, int nIDC, HTREEITEM hItem, bool &bCheck);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, CString &sText);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, int &value);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, UINT &value);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, long &value);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, DWORD &value);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, float &value);
void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, double &value);


///////////////////////////////////////////////////////////////////////////////
// CNumTreeOptionsEdit

class CNumTreeOptionsEdit : public CTreeOptionsEdit
{
	DECLARE_DYNCREATE(CNumTreeOptionsEdit)

public:
	CNumTreeOptionsEdit()
		: m_bSelf(false)
	{
	}

	virtual DWORD GetWindowStyle()		{ return CTreeOptionsEdit::GetWindowStyle() | ES_NUMBER; }

protected:
	bool m_bSelf;

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnEnChange();

	DECLARE_MESSAGE_MAP()
};


///////////////////////////////////////////////////////////////////////////////
// CTreeOptionsEditEx

class CTreeOptionsEditEx : public CTreeOptionsEdit
{
	DECLARE_DYNCREATE(CTreeOptionsEditEx)

public:
	CTreeOptionsEditEx()
		: m_bSelf(false)
	{
	}

protected:
	bool m_bSelf;

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnEnChange();

	DECLARE_MESSAGE_MAP()
};