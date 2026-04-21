//This file is part of eMule AI
//Copyright (C)2026 eMule AI

// CustMenu.h: interface for the CMenuXP class.
//
//////////////////////////////////////////////////////////////////////
#include "Opcodes.h"

#if !defined(AFX_MENUXP_H__8FC060B7_2454_11D5_99BD_5254AB339987__INCLUDED_)
#define AFX_CUSTMENU_H__8FC060B7_2454_11D5_99BD_5254AB339987__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

////////////////////////////////////////////////////////////////////////////////////////////
//	CMenuXP is a class derived from CMenu, using ownerdraw technology.
//	I named it MenuXP because I once expected it to be like the menus
//	in OfficeXP and WindowsXP, but I failed to accomplish it. The big
//	difficulty is I cannot convert the 3D border of the menu into flat.
//	So I giveup. And I hope it still usefull to you.
//	
//	I construt the class from scribble, but some of the drawing code is copy from the class
//	CCoolMenuManager. I also use a class named CBCGKeyHelper from BCGControlBar to show
//	accelerator key text.
//
//	Usage:
//	Seee Example
//
//	Features:
//	1. Menu with icons, like in office 97
//	2. A sidebar in any level of the popup menu
//	3. Support button style menuitem, it appear in some drawing toolbars of office suite
//	4. All colors and font and size can be customized
//
//	Use it as you like. bug report and improvement is welcome.
//	
//	Created:	10/24/2001
//	Author:		Yao Zhifeng			yzf_nuaa@sina.com
//	
////////////////////////////////////////////////////////////////////////////////////////////

//The ownerdraw data
class CMenuXPItem
{
public:
	DWORD		m_dwMagicNum;	//A magic number to distingush our data
	DWORD		m_dwID;			//Menu ID
	bool		m_bSeparator;	//Separator
	bool		m_bSidebar;		//A gradient sidebar
	bool		m_bButtonOnly;	//Button only style item
	bool		m_bTitle;		// MenuXP Title [fafner] - fafner
	CString		m_strText;		//Menu item text
	HICON		m_hIcon;		//Menu icon
	int			m_nSize;		//Height of the item(Width of the sidebar if m_bSideBar is true)

public:
	CMenuXPItem() 
	{
		m_dwMagicNum = 0x0505a0a0;
		m_dwID = 0;
		m_bSeparator = false;
		m_bSidebar = false;
		m_bButtonOnly = false;
		m_bTitle = false;		// MenuXP Title [fafner] - fafner
		m_hIcon = NULL;
		m_nSize = 16;
	};

	virtual ~CMenuXPItem()
	{
		if (m_hIcon)
			::DestroyIcon(m_hIcon);
	}

	BOOL	IsMyData(void) { return m_dwMagicNum == 0x0505a0a0; };
};

///////////////////////////////////////////////////////////////////////////////////////////////
//	For convenient, derive some class from CMenuXPItem, 
//	and do the initialization in constructor
class CMenuXPText : public CMenuXPItem	//Normal item with text and an optional icon
{
public:
	CMenuXPText(DWORD dwID, LPCTSTR strText, HICON hIcon = NULL) : CMenuXPItem()
	{
		m_dwID = dwID;
		m_strText = strText;
		m_hIcon = hIcon;
	}
};

class CMenuXPSeparator : public CMenuXPItem //A separator item
{
public:
	CMenuXPSeparator() : CMenuXPItem()
	{
		m_bSeparator = true;
	}
};

class CMenuXPSidebar : public CMenuXPItem //A gradient sidebar
{
public:
	CMenuXPSidebar(int nSize = 32, LPCTSTR strText = NULL, HICON hIcon=NULL, DWORD dwID=0) : CMenuXPItem()
	{
		m_dwID = dwID;
		m_bSidebar = true;
		m_strText = strText;
		m_hIcon = hIcon;
		m_nSize = nSize;
		m_dwID = dwID;
	}
};

class CMenuXPButton : public CMenuXPItem //A button only item
{
public:
	CMenuXPButton(DWORD dwID, HICON hIcon = NULL) : CMenuXPItem()
	{
		m_dwID = dwID;
		m_bButtonOnly = true;
		m_hIcon = hIcon;
	}
};
class CMenuXPTitle : public CMenuXPItem
{
public:
	CMenuXPTitle(LPCTSTR strText) : CMenuXPItem()
	{
		m_bTitle = true;
		m_strText = strText;
	}
};

////////////////////////////////////////////////////////////////////////////////////////
// Class CMenuXP, an ownerdraw menu
class CMenuXP : public CMenu  
{
	DECLARE_DYNAMIC(CMenuXP)

public:
	CMenuXP();
	virtual ~CMenuXP();

	typedef	enum 
	{
		STYLE_OFFICE,		//Draw a float button around the icon
		STYLE_STARTMENU,	//show selected bar below the icon
		STYLE_XP			//use different color for the icon area
	} MENUSTYLE;

	//Below is the functions to build the menu
	void	AddMenuSidebar(CString strTitle, LPCTSTR strBackgroundJPG = NULL);
	BOOL	AddMenuTitle(LPCTSTR lpszTitle); // MenuXP Title [fafner] - fafner
	BOOL	AppendMenu(UINT nFlags, UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL, LPCTSTR lpszIconName = NULL);
	BOOL	InsertMenu(UINT nPosition, UINT nFlags, UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL, LPCTSTR lpszIconName = NULL);
	BOOL	RemoveMenu(UINT nPosition, UINT nFlags);

	void	EnableIcons() {}
	bool	HasEnabledItems() const;

	void	ChangeMenuIcon(UINT_PTR nIDNewItem, LPCTSTR lpszNewItem, LPCTSTR lpszIconName);
	BOOL	SetMenuText(UINT_PTR nIDNewItem, LPCTSTR lpszNewItem);
	BOOL	DestroyMenu();

	BOOL	AddSidebar(CMenuXPSidebar *pItem);
	BOOL	AppendODMenu(UINT nFlags, UINT_PTR nIDNewItem, CMenuXPItem* pItem, UINT nPosition = UINT_MAX, ACCEL *pAccel=0);
	BOOL	AppendSeparator(void);	
	void	Break(void);	//change a column(the next item added will be in a new column)
	void	BreakBar(void);	//change a column with a break line(same as Break, except that a break line is drawn between two columns)

protected:
	CFont		m_fontMenu;	
	BOOL		m_bBreak;		//if true, next item inserted into the menu will be added with the sytle MF_MENUBREAK
	BOOL		m_bBreakBar;	//if true, next item inserted into the menu will be added with the sytle MF_MENUBARBREAK
	HBITMAP		m_hBitmap;		//Background bitmap
	CDC			m_memDC;		//Memory dc holding the background bitmap
	MENUSTYLE	m_Style;	//menu style(currently support office or startmenu style)
	BOOL		m_EmbossedIcons;

public:	//User these functions to change the default attribute of the menu
	void	SetBackBitmap(HBITMAP hBmp);
	void	SetBackBitmap(LPCTSTR lpszResourceName, LPCTSTR pszResourceType);
	void	SetMenuStyle(MENUSTYLE	style) { m_Style = style; }
	void	SetEmbossedIcons(BOOL Embossed) { m_EmbossedIcons = Embossed; }
	BOOL	SetMenuFont(LOGFONT	lgfont);
	CMenuXP	*FindSubMenuFromID(DWORD dwID); //Find the popupmenu from a menuitem id, you may not need it

public:
	virtual void DrawItem( LPDRAWITEMSTRUCT lpDrawItemStruct );
	virtual void MeasureItem( LPMEASUREITEMSTRUCT lpMeasureItemStruct );
	static	LRESULT OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu);

protected:
	virtual void DrawBackGround(CDC *pDC, CRect rect, BOOL bSelected, BOOL bDisabled);
	virtual void DrawButton(CDC *pDC, CRect rect, BOOL bSelected, BOOL bDisabled, BOOL bChecked);
	virtual void DrawIcon(CDC *pDC, CRect rect, HICON hIcon, BOOL bSelected, BOOL bDisabled, BOOL bChecked);
	virtual void DrawSidebar(CDC *pDC, CRect rect, HICON hIcon, CString strText);
	virtual void DrawText(CDC *pDC, CRect rect, CString strText, BOOL bSelected, BOOL bDisabled, BOOL bBold, bool bTitle = false); // MenuXP Title [fafner] - fafner
	virtual void DrawCheckMark(CDC *pDC, CRect rect, BOOL bSelected);
	virtual void DrawMenuText(CDC& dc, CRect rc, CString text, COLORREF color);
	virtual void DrawIconArea(CDC *pDC, CRect rect, BOOL bSelected, BOOL bDisabled, BOOL bChecked);

	//helpers 
	HBITMAP		CreateGradientBMP(HDC hDC,COLORREF cl1,COLORREF cl2,int nWidth,int nHeight,int nDir,int nNumColors);
	void		DrawEmbossed(CDC *pDC, HICON hIcon, CRect rect, BOOL bColor = FALSE, BOOL bShadow = FALSE);
	void		FillRect(CDC *pDC, const CRect& rc, COLORREF color);

	static void FreeOwnerDrawData(HMENU hMenu);
};

#endif // !defined(AFX_MENUXP_H__8FC060B7_2454_11D5_99BD_5254AB339987__INCLUDED_)
