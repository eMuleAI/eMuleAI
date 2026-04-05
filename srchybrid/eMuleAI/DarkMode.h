//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include <map>
#include "ProgressCtrlX.h"
#include "resource.h"
#pragma comment(lib, "Dwmapi.lib")

#define COLOR_SHADEBASE						1000
#define COLOR_SEARCH_DOWNLOADING			1001
#define COLOR_SEARCH_STOPPED				1002
#define COLOR_SEARCH_SHARING				1003
#define COLOR_SEARCH_KNOWN					1004
#define COLOR_SEARCH_CANCELED				1005
#define COLOR_MAN_BLACKLIST					1006
#define COLOR_AUTO_BLACKLIST				1007
#define COLOR_SPAM							1008
#define COLOR_SERVER_CONNECTED				1009
#define COLOR_SERVER_FAILED					1010
#define COLOR_SERVER_DEAD					1011
#define COLOR_PROGRESSBAR					1012
#define COLOR_SELECTEDTABTOPLINE			1013
#define COLOR_TABBORDER						1014
#define COLOR_MENUXP_SIDEBAR_TEXT			1015	
#define COLOR_MENUXP_TITLE_TEXT				1016
#define COLOR_MENUXP_SIDEBAR_GRADIENT_START	1017
#define COLOR_MENUXP_SIDEBAR_GRADIENT_END	1018	
#define COLOR_MENUXP_TITLE_GRADIENT_START	1019	
#define COLOR_MENUXP_TITLE_GRADIENT_END		1020
#define COLOR_HELP_TEXT_BACKGROUND          1021
#define COLOR_IRC_INFO_MSG					1022
#define COLOR_IRC_STATUS_MSG				1023
#define COLOR_IRC_EVENT_MSG					1024
#define COLOR_IRC_ACTION_MSG				1025

struct TooltipThemeColors
{
	COLORREF crBackground;
	COLORREF crCaptionBackground;
	COLORREF crBorder;
	COLORREF crMainTitleText;
	COLORREF crTitleText;
	COLORREF crKeyText;
	COLORREF crValueText;
	COLORREF crSeparator;
};

bool IsDarkModeEnabled();
void GetSystemDarkModeStatus();
void ApplyTheme(HWND hWnd);
void ApplyThemeToWindow(HWND hWnd, bool bForceRedraw, bool bForceWindowsTheme);
BOOL HandleEraseBkgnd(HWND hWnd, CDC* pDC);
HBRUSH HandleCtlColor(HWND hWnd, CDC* pDC, HWND hChild, UINT nCtlColor);
COLORREF GetCustomSysColor(int nIndex, bool bForceDarkColor = false);
COLORREF GetServerListTextColor(int nColorIndex, bool bSelected);
COLORREF GetIrcColorByIndex(int nIndex);
const COLORREF* GetIrcColorTable();
UINT GetIrcColorTableSize();
TooltipThemeColors GetTooltipThemeColors();
void EnsureReadableTextColors(COLORREF& crForeground, COLORREF& crBackground, COLORREF crDefaultForeground, COLORREF crDefaultBackground);

LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);
LRESULT CALLBACK ComboBoxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);
LRESULT CALLBACK DarkEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);
LRESULT CALLBACK CustomListViewProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);
LRESULT CALLBACK DateTimeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);
LRESULT CALLBACK SpinButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);
LRESULT CALLBACK HeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);

// ---------------------------------------------------------------------------
//  CDarkMode
// ---------------------------------------------------------------------------
class CDarkMode
{
public:
    static void Initialize(); // Initialize static brushes; must be declared static to allow class-level call
    static void OnDestroy(); // clean up brush resources

    static HICON GetCustomSysIcon(UINT iconMask); // load appropriate icon
	// Dark mode aware replacement for AfxMessageBox
	static int MessageBox(LPCTSTR lpszText, UINT nType = MB_OK, UINT nHelpId = 0);
	static int MessageBoxWithCaption(LPCTSTR lpszText, LPCTSTR lpszCaption, UINT nType = MB_OK, UINT nHelpId = 0);
    static int MessageBox(UINT nPromptId, UINT nType = MB_OK, UINT nHelpId = 0);

    // Shows the folder picker with optional dark-mode styling
    static PIDLIST_ABSOLUTE BrowseForFolder(BROWSEINFO& bi);

    static CBrush m_brDefault;
    static CBrush m_brText;
    static CBrush m_brHighlight;
    static CBrush m_brBtn;
    static CBrush m_brBtnBorder;
    static CBrush m_brBtnBorderDisabled;
    static CBrush m_brBtnHover;
    static CBrush m_brBtnPressed;
    static CBrush m_brScrollbar;
    static CBrush m_brActiveCaption;
    static CBrush m_brInactiveCaption;
    static CBrush s_brHelpTextBackground;
    static CBrush m_brDefaultLight;

private:
    // Callback for SHBrowseForFolder initialization
    static int CALLBACK BrowseCallbackProc(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData);

    // Subclass for the main dialog window
    static LRESULT CALLBACK FolderDlgSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);

    // Subclass for child edit controls
    static LRESULT CALLBACK FolderChildSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/);

    // Enumerates tree-view and edit children to apply subclasses
    static void SubclassFolderDialogChildren(HWND hWnd);
};

// ---------------------------------------------------------------------------
//  CDarkModeMessageBoxDlg
// ---------------------------------------------------------------------------
class CDarkModeMessageBoxDlg : public CDialog
{
public:
	CDarkModeMessageBoxDlg(LPCTSTR text, UINT type, LPCTSTR caption, CWnd* pParent = nullptr);
    enum { IDD = IDD_DARKMODE_MSGBOX };

private:
    CString m_text;
	CString m_caption;
    UINT    m_type;
    HICON   m_hIcon;
    bool   m_bTrackingMouse;    // have we set up mouse leave tracking?
    UINT   m_uHoverBtnId;       // currently hovered button ID

    void ResizeToFitText();
    void CreateButtons();

protected:
    virtual BOOL OnInitDialog() override;

    DECLARE_MESSAGE_MAP()
    afx_msg void OnPaint();
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS);
    afx_msg BOOL OnCommand(WPARAM wParam, LPARAM lParam);    // handle button clicks
};
