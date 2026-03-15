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
#include "stdafx.h"
#include "emule.h"
#include "PreferencesDlg.h"
#include "eMuleAI/DarkMode.h"
#include <atlimage.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const UINT PREFS_BANNER_CTRL_ID = 0x7EE1;
	const int PREFS_BANNER_MIN_WIDTH = 128;
	const int PREFS_BANNER_MAX_WIDTH = 176;
	const int PREFS_BANNER_MARGIN = 4;
	const int PREFS_BANNER_PADDING = 2;
	const double PREFS_BANNER_WIDTH_SCALE = 0.92;
	const double PREFS_BANNER_WIDTH_REDUCTION_SCALE = 0.50;
	const double PREFS_BANNER_PREVIOUS_SIZE_SCALE = 0.67;
	const int PREFS_BANNER_ALPHA_THRESHOLD = 10;
	const int PREFS_BANNER_BRIGHTNESS_THRESHOLD = 20;
	const UINT PREFS_BANNER_RESOURCE_ID_DARK = IDR_MOD_BANNER;
	const UINT PREFS_BANNER_RESOURCE_ID_LIGHT = IDR_MOD_BANNER_LIGHT;

	class CPreferencesBannerWnd : public CWnd
	{
	public:
		CPreferencesBannerWnd() = default;
		virtual ~CPreferencesBannerWnd()
		{
			m_imgBanner.Destroy();
		}

		BOOL Create(CWnd* pParentWnd, UINT nCtrlId)
		{
			const CString strClassName(AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)), NULL));
			return CWnd::CreateEx(WS_EX_NOPARENTNOTIFY, strClassName, _T("PreferencesBanner"), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pParentWnd, nCtrlId);
		}

		bool LoadBannerFromResource(UINT nResId)
		{
			m_imgBanner.Destroy();

			const HMODULE hResModule = AfxGetResourceHandle();
			const HRSRC hResInfo = ::FindResource(hResModule, MAKEINTRESOURCE(nResId), RT_RCDATA);
			if (hResInfo == NULL)
				return false;

			const DWORD dwResSize = ::SizeofResource(hResModule, hResInfo);
			if (dwResSize == 0)
				return false;

			const HGLOBAL hResData = ::LoadResource(hResModule, hResInfo);
			if (hResData == NULL)
				return false;

			const void* pResBytes = ::LockResource(hResData);
			if (pResBytes == NULL)
				return false;

			const HGLOBAL hGlobalCopy = ::GlobalAlloc(GMEM_MOVEABLE, dwResSize);
			if (hGlobalCopy == NULL)
				return false;

			void* pGlobalCopy = ::GlobalLock(hGlobalCopy);
			if (pGlobalCopy == NULL) {
				::GlobalFree(hGlobalCopy);
				return false;
			}

			memcpy(pGlobalCopy, pResBytes, dwResSize);
			::GlobalUnlock(hGlobalCopy);

			IStream* pStream = NULL;
			HRESULT hr = ::CreateStreamOnHGlobal(hGlobalCopy, TRUE, &pStream);
			if (FAILED(hr) || pStream == NULL) {
				::GlobalFree(hGlobalCopy);
				return false;
			}

			hr = m_imgBanner.Load(pStream);
			pStream->Release();
			if (FAILED(hr)) {
				TRACE(_T("Preferences banner resource load failed (0x%08X), id=%u\n"), static_cast<unsigned int>(hr), nResId);
				return false;
			}

			UpdateContentRect();
			return true;
		}

		int GetSuggestedWidth(int nTargetHeight) const
		{
			if (nTargetHeight <= 0 || m_imgBanner.IsNull())
				return PREFS_BANNER_MIN_WIDTH;

			const int nImageWidth = m_imgBanner.GetWidth();
			const int nImageHeight = m_imgBanner.GetHeight();
			if (nImageWidth <= 0 || nImageHeight <= 0)
				return PREFS_BANNER_MIN_WIDTH;

			return max(1, static_cast<int>(nTargetHeight * (static_cast<double>(nImageWidth) / static_cast<double>(nImageHeight)) * PREFS_BANNER_WIDTH_SCALE + 0.5));
		}

	protected:
		void UpdateContentRect()
		{
			m_rcContent.SetRectEmpty();
			if (m_imgBanner.IsNull())
				return;

			const int nImageWidth = m_imgBanner.GetWidth();
			const int nImageHeight = m_imgBanner.GetHeight();
			const int nBpp = m_imgBanner.GetBPP();
			if (nImageWidth <= 0 || nImageHeight <= 0) {
				return;
			}

			if (nBpp != 24 && nBpp != 32) {
				m_rcContent.SetRect(0, 0, nImageWidth, nImageHeight);
				return;
			}

			int nMinX = nImageWidth;
			int nMinY = nImageHeight;
			int nMaxX = -1;
			int nMaxY = -1;
			for (int y = 0; y < nImageHeight; ++y)
				for (int x = 0; x < nImageWidth; ++x) {
					const BYTE* pPixel = reinterpret_cast<const BYTE*>(m_imgBanner.GetPixelAddress(x, y));
					if (pPixel == NULL)
						continue;

					const int nBlue = pPixel[0];
					const int nGreen = pPixel[1];
					const int nRed = pPixel[2];
					const int nAlpha = (nBpp == 32) ? pPixel[3] : 255;
					if (nAlpha <= PREFS_BANNER_ALPHA_THRESHOLD)
						continue;

					if (nRed + nGreen + nBlue <= PREFS_BANNER_BRIGHTNESS_THRESHOLD)
						continue;

					nMinX = min(nMinX, x);
					nMinY = min(nMinY, y);
					nMaxX = max(nMaxX, x);
					nMaxY = max(nMaxY, y);
				}

			if (nMaxX >= nMinX && nMaxY >= nMinY) {
				m_rcContent.SetRect(nMinX, nMinY, nMaxX + 1, nMaxY + 1);
				m_rcContent.InflateRect(2, 2);
				m_rcContent.IntersectRect(m_rcContent, CRect(0, 0, nImageWidth, nImageHeight));
			} else {
				m_rcContent.SetRect(0, 0, nImageWidth, nImageHeight);
			}
		}

		afx_msg BOOL OnEraseBkgnd(CDC*)
		{
			return TRUE;
		}

		afx_msg void OnPaint()
		{
			CPaintDC dc(this);
			CRect rectClient;
			GetClientRect(&rectClient);
			const COLORREF crBannerBackground = IsDarkModeEnabled() ? GetCustomSysColor(COLOR_MENUXP_TITLE_GRADIENT_START, true) : GetCustomSysColor(COLOR_WINDOW);
			dc.FillSolidRect(&rectClient, crBannerBackground);

			if (m_imgBanner.IsNull() || rectClient.IsRectEmpty())
				return;

			const int nImageWidth = m_imgBanner.GetWidth();
			const int nImageHeight = m_imgBanner.GetHeight();
			if (nImageWidth <= 0 || nImageHeight <= 0)
				return;

			CRect rectSource = m_rcContent;
			if (rectSource.IsRectEmpty())
				rectSource.SetRect(0, 0, nImageWidth, nImageHeight);

			CRect rectDrawArea(rectClient);
			rectDrawArea.DeflateRect(PREFS_BANNER_PADDING, PREFS_BANNER_PADDING);
			if (rectDrawArea.IsRectEmpty())
				return;

			const int nSourceWidth = rectSource.Width();
			const int nSourceHeight = rectSource.Height();
			if (nSourceWidth <= 0 || nSourceHeight <= 0)
				return;

			const double fScaleX = static_cast<double>(rectDrawArea.Width()) / static_cast<double>(nSourceWidth);
			const double fScaleY = static_cast<double>(rectDrawArea.Height()) / static_cast<double>(nSourceHeight);
			// Keep aspect ratio and draw close to the previous visual size while avoiding over-shrinking.
			const double fContainScale = min(fScaleX, fScaleY);
			const double fCoverScale = max(fScaleX, fScaleY);
			const double fScale = max(fContainScale, fCoverScale * PREFS_BANNER_PREVIOUS_SIZE_SCALE);
			const int nDrawWidth = max(1, static_cast<int>(nSourceWidth * fScale + 0.5));
			const int nDrawHeight = max(1, static_cast<int>(nSourceHeight * fScale + 0.5));
			const int nDrawX = rectDrawArea.left + (rectDrawArea.Width() - nDrawWidth) / 2;
			const int nDrawY = rectDrawArea.top + (rectDrawArea.Height() - nDrawHeight) / 2;
			m_imgBanner.Draw(dc.GetSafeHdc(), nDrawX, nDrawY, nDrawWidth, nDrawHeight, rectSource.left, rectSource.top, nSourceWidth, nSourceHeight);

			CRect rectBorder(rectClient);
			rectBorder.DeflateRect(0, 0, 1, 1);
			const COLORREF crBorder = GetCustomSysColor(COLOR_BTNSHADOW);
			dc.Draw3dRect(&rectBorder, crBorder, crBorder);
		}

		DECLARE_MESSAGE_MAP()

	private:
		CImage m_imgBanner;
		CRect m_rcContent;
	};

	BEGIN_MESSAGE_MAP(CPreferencesBannerWnd, CWnd)
		ON_WM_ERASEBKGND()
		ON_WM_PAINT()
	END_MESSAGE_MAP()
}

IMPLEMENT_DYNAMIC(CPreferencesDlg, CTreePropSheet)

BEGIN_MESSAGE_MAP(CPreferencesDlg, CTreePropSheet)
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPreferencesDlg::CPreferencesDlg()
{
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_wndGeneral.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndDisplay.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndConnection.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndServer.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndDirectories.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndFiles.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndStats.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndIRC.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndWebServer.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndTweaks.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndSecurity.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndScheduler.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndProxy.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndMessages.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndMod.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndProtectionPanel.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndBlacklistPanel.m_psp.dwFlags &= ~PSH_HASHELP;
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	m_wndDebug.m_psp.dwFlags &= ~PSH_HASHELP;
#endif

	CTreePropSheet::SetPageIcon(&m_wndGeneral, _T("Preferences"));
	CTreePropSheet::SetPageIcon(&m_wndDisplay, _T("DISPLAY"));
	CTreePropSheet::SetPageIcon(&m_wndConnection, _T("CONNECTION"));
	CTreePropSheet::SetPageIcon(&m_wndProxy, _T("PROXY"));
	CTreePropSheet::SetPageIcon(&m_wndServer, _T("SERVER"));
	CTreePropSheet::SetPageIcon(&m_wndDirectories, _T("FOLDERS"));
	CTreePropSheet::SetPageIcon(&m_wndFiles, _T("Transfer"));
	CTreePropSheet::SetPageIcon(&m_wndNotify, _T("NOTIFICATIONS"));
	CTreePropSheet::SetPageIcon(&m_wndStats, _T("STATISTICS"));
	CTreePropSheet::SetPageIcon(&m_wndIRC, _T("IRC"));
	CTreePropSheet::SetPageIcon(&m_wndSecurity, _T("SECURITY"));
	CTreePropSheet::SetPageIcon(&m_wndScheduler, _T("SCHEDULER"));
	CTreePropSheet::SetPageIcon(&m_wndWebServer, _T("WEB"));
	CTreePropSheet::SetPageIcon(&m_wndTweaks, _T("TWEAK"));
	CTreePropSheet::SetPageIcon(&m_wndMessages, _T("MESSAGES"));
	CTreePropSheet::SetPageIcon(&m_wndMod, _T("AAAEMULEAPP"));
	CTreePropSheet::SetPageIcon(&m_wndProtectionPanel, _T("PROTECTION_PANEL"));
	CTreePropSheet::SetPageIcon(&m_wndBlacklistPanel, _T("Blacklist_PANEL"));
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	CTreePropSheet::SetPageIcon(&m_wndDebug, _T("Preferences"));
#endif

	AddPage(&m_wndGeneral);
	AddPage(&m_wndDisplay);
	AddPage(&m_wndConnection);
	AddPage(&m_wndProxy);
	AddPage(&m_wndServer);
	AddPage(&m_wndDirectories);
	AddPage(&m_wndFiles);
	AddPage(&m_wndNotify);
	AddPage(&m_wndStats);
	AddPage(&m_wndIRC);
	AddPage(&m_wndMessages);
	AddPage(&m_wndSecurity);
	AddPage(&m_wndScheduler);
	AddPage(&m_wndWebServer);
	AddPage(&m_wndTweaks);
	AddPage(&m_wndMod);
	AddPage(&m_wndProtectionPanel);
	AddPage(&m_wndBlacklistPanel);
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	AddPage(&m_wndDebug);
#endif

	// The height of the option dialog is already too large for 640x480. To show as much as
	// possible we do not show a page caption (which is a decorative element only anyway).
	SetTreeViewMode(TRUE, ::GetSystemMetrics(SM_CYSCREEN) >= 600, TRUE);
	SetTreeWidth(170);

	m_pPshStartPage = NULL;
	m_bSaveIniFile = false;
	m_bApplyButtonClicked = false;
	m_pBannerWnd = NULL;
	m_nBannerWidth = 0;
}

void CPreferencesDlg::OnDestroy()
{
	if (m_pBannerWnd) {
		if (::IsWindow(m_pBannerWnd->GetSafeHwnd()))
			m_pBannerWnd->DestroyWindow();
		delete m_pBannerWnd;
		m_pBannerWnd = NULL;
	}
	m_nBannerWidth = 0;

	CTreePropSheet::OnDestroy();
	if (m_bSaveIniFile) {
		thePrefs.Save();
		m_bSaveIniFile = false;
	}
	m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
}

BOOL CPreferencesDlg::OnInitDialog()
{
	ASSERT(!m_bSaveIniFile);
	BOOL bResult = CTreePropSheet::OnInitDialog();
	InitWindowStyles(this);
	InitSideBanner();

	for (int i = (int)m_pages.GetCount(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == m_pPshStartPage) {
			SetActivePage(i);
			break;
		}

	Localize();
	return bResult;
}

bool CPreferencesDlg::InitSideBanner()
{
	if (m_pBannerWnd != NULL)
		return true;

	const UINT nPrimaryBannerResourceId = IsDarkModeEnabled() ? PREFS_BANNER_RESOURCE_ID_DARK : PREFS_BANNER_RESOURCE_ID_LIGHT;
	const UINT nSecondaryBannerResourceId = IsDarkModeEnabled() ? PREFS_BANNER_RESOURCE_ID_LIGHT : PREFS_BANNER_RESOURCE_ID_DARK;

	CPreferencesBannerWnd* pBannerWnd = new CPreferencesBannerWnd;
	if (!pBannerWnd->LoadBannerFromResource(nPrimaryBannerResourceId) && !pBannerWnd->LoadBannerFromResource(nSecondaryBannerResourceId)) {
		TRACE(_T("Preferences banner could not be loaded from resources (ids=%u,%u)\n"), nPrimaryBannerResourceId, nSecondaryBannerResourceId);
		delete pBannerWnd;
		return false;
	}

	CRect rectClient;
	GetClientRect(&rectClient);
	const int nTargetBannerHeight = max(1, rectClient.Height() - (PREFS_BANNER_MARGIN * 2));
	const int nSuggestedWidth = pBannerWnd->GetSuggestedWidth(nTargetBannerHeight);
	const int nLayoutLimit = max(PREFS_BANNER_MIN_WIDTH, rectClient.Width() / 3);
	const int nBaseBannerWidth = min(min(PREFS_BANNER_MAX_WIDTH, nLayoutLimit), max(PREFS_BANNER_MIN_WIDTH, nSuggestedWidth));
	m_nBannerWidth = max(1, static_cast<int>(nBaseBannerWidth * PREFS_BANNER_WIDTH_REDUCTION_SCALE + 0.5));

	const int nDialogGrowWidth = m_nBannerWidth + PREFS_BANNER_MARGIN + PREFS_BANNER_MARGIN;
	CRect rectWindow;
	GetWindowRect(&rectWindow);
	SetWindowPos(NULL, 0, 0, rectWindow.Width() + nDialogGrowWidth, rectWindow.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	CenterWindow();

	if (!pBannerWnd->Create(this, PREFS_BANNER_CTRL_ID)) {
		TRACE(_T("Failed to create preferences banner window\n"));
		delete pBannerWnd;
		m_nBannerWidth = 0;
		return false;
	}

	m_pBannerWnd = pBannerWnd;
	UpdateBannerLayout();
	return true;
}

void CPreferencesDlg::UpdateBannerLayout()
{
	if (m_pBannerWnd == NULL || !::IsWindow(m_pBannerWnd->GetSafeHwnd()) || m_nBannerWidth <= 0)
		return;

	CRect rectClient;
	GetClientRect(&rectClient);
	const CRect rectBanner(rectClient.right - PREFS_BANNER_MARGIN - m_nBannerWidth, PREFS_BANNER_MARGIN, rectClient.right - PREFS_BANNER_MARGIN, rectClient.bottom - PREFS_BANNER_MARGIN);
	if (!rectBanner.IsRectEmpty())
		m_pBannerWnd->MoveWindow(&rectBanner);
}

void CPreferencesDlg::OnSize(UINT nType, int cx, int cy)
{
	CTreePropSheet::OnSize(nType, cx, cy);
	UpdateBannerLayout();
}

void CPreferencesDlg::LocalizeItemText(int i, LPCTSTR strid)
{
	GetPageTreeControl()->SetItemText(GetPageTreeItem(i), GetResNoAmp(strid));
}

void CPreferencesDlg::Localize()
{
	SetTitle(GetResNoAmp(_T("EM_PREFS")));

	m_wndGeneral.Localize();
	m_wndDisplay.Localize();
	m_wndConnection.Localize();
	m_wndServer.Localize();
	m_wndDirectories.Localize();
	m_wndFiles.Localize();
	m_wndStats.Localize();
	m_wndNotify.Localize();
	m_wndIRC.Localize();
	m_wndSecurity.Localize();
	m_wndTweaks.Localize();
	m_wndWebServer.Localize();
	m_wndScheduler.Localize();
	m_wndProxy.Localize();
	m_wndMessages.Localize();
	m_wndMod.Localize();
	m_wndProtectionPanel.Localize();
	m_wndBlacklistPanel.Localize();

	if (GetPageTreeControl()) {
		static const LPCTSTR uids[18] =
		{
			_T("PW_GENERAL"), _T("PW_DISPLAY"), _T("CONNECTION"), _T("PW_PROXY"), _T("PW_SERVER"),
			_T("PW_DIR"), _T("PW_FILES"), _T("PW_EKDEV_OPTIONS"), _T("STATSSETUPINFO"), _T("IRC"),
			_T("MESSAGESCOMMENTS"), _T("SECURITY"), _T("SCHEDULER"), _T("PW_WS"), _T("PW_TWEAK"), _T("PW_MOD"), _T("PW_PROTECTION_PANEL"), _T("PW_BLACKLIST_PANEL")
		};

		int c;
		for (c = 0; c < _countof(uids); ++c)
			LocalizeItemText(c, uids[c]);
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
		GetPageTreeControl()->SetItemText(GetPageTreeItem(c), _T("Debug"));
#endif
	}

	UpdateCaption();
}

void CPreferencesDlg::OnHelp()
{
	int iCurSel = GetActiveIndex();
	if (iCurSel >= 0) {
		CPropertyPage *pPage = GetPage(iCurSel);
		if (pPage) {
			HELPINFO hi = {};
			hi.cbSize = (UINT)sizeof hi;
			hi.iContextType = HELPINFO_WINDOW;
			hi.hItemHandle = pPage->m_hWnd;
			pPage->SendMessage(WM_HELP, 0, (LPARAM)&hi);
			return;
		}
	}

	theApp.ShowHelp(0, HELP_CONTENTS);
}

BOOL CPreferencesDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case ID_HELP:
		return OnHelpInfo(NULL);
	case IDOK:
		m_bApplyButtonClicked = false; 
		m_bSaveIniFile = true;
		break;
	case ID_APPLY_NOW:
		m_bApplyButtonClicked = true;
		m_bSaveIniFile = true;
	}
	return __super::OnCommand(wParam, lParam);
}

BOOL CPreferencesDlg::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}
