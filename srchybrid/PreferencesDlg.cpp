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
#include "OtherFunctions.h"
#include "Opcodes.h"
#include "eMuleAI/DarkMode.h"
#include <atlimage.h>
#include <cstddef>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	void OpenPreferencesHelpPage()
	{
		BrowserOpen(MOD_PAGES_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	}

	int s_iOptionsVisualScalePercent = OPTIONS_WINDOW_SCALE_DISABLED;

	int ScaleOptionsMetric(int iValue)
	{
		return ::MulDiv(iValue, s_iOptionsVisualScalePercent, 100);
	}

#pragma pack(push, 1)
	struct SDialogTemplateExHeader
	{
		WORD dlgVer;
		WORD signature;
		DWORD helpId;
		DWORD exStyle;
		DWORD style;
		WORD itemCount;
		short x;
		short y;
		short cx;
		short cy;
	};

	struct SDialogItemTemplateExHeader
	{
		DWORD helpId;
		DWORD exStyle;
		DWORD style;
		short x;
		short y;
		short cx;
		short cy;
		DWORD id;
	};

	struct SDialogTemplateHeader
	{
		DWORD style;
		DWORD exStyle;
		WORD itemCount;
		short x;
		short y;
		short cx;
		short cy;
	};

	struct SDialogItemTemplateHeader
	{
		DWORD style;
		DWORD exStyle;
		short x;
		short y;
		short cx;
		short cy;
		WORD id;
	};
#pragma pack(pop)

	static_assert(sizeof(SDialogTemplateExHeader) == 26);
	static_assert(sizeof(SDialogItemTemplateExHeader) == 24);
	static_assert(sizeof(SDialogTemplateHeader) == 18);
	static_assert(sizeof(SDialogItemTemplateHeader) == 18);

	BYTE* AlignDialogTemplatePointer(BYTE* pData, BYTE* pEnd)
	{
		const ULONG_PTR uAligned = (reinterpret_cast<ULONG_PTR>(pData) + 3u) & ~static_cast<ULONG_PTR>(3u);
		BYTE* pAligned = reinterpret_cast<BYTE*>(uAligned);
		return pAligned <= pEnd ? pAligned : NULL;
	}

	bool SkipDialogTemplateField(BYTE*& pData, BYTE* pEnd)
	{
		if (pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(WORD)))
			return false;

		WORD uValue = 0;
		memcpy(&uValue, pData, sizeof(uValue));
		pData += sizeof(uValue);
		if (uValue == 0)
			return true;
		if (uValue == 0xFFFF) {
			if (pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(WORD)))
				return false;
			pData += sizeof(WORD);
			return true;
		}

		while (uValue != 0) {
			if (pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(WORD)))
				return false;
			memcpy(&uValue, pData, sizeof(uValue));
			pData += sizeof(uValue);
		}
		return true;
	}

	void ScaleDialogTemplateRect(short& x, short& y, short& cx, short& cy, int iPercent)
	{
		const int iRight = static_cast<int>(x) + static_cast<int>(cx);
		const int iBottom = static_cast<int>(y) + static_cast<int>(cy);
		const int iScaledX = ::MulDiv(x, iPercent, 100);
		const int iScaledY = ::MulDiv(y, iPercent, 100);
		x = static_cast<short>(iScaledX);
		y = static_cast<short>(iScaledY);
		cx = static_cast<short>(::MulDiv(iRight, iPercent, 100) - iScaledX);
		cy = static_cast<short>(::MulDiv(iBottom, iPercent, 100) - iScaledY);
	}

	bool IsDialogTemplateOrdinalClass(const BYTE* pData, const BYTE* pEnd, WORD uClassOrdinal)
	{
		if (pData == NULL || pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(WORD) * 2))
			return false;

		WORD uMarker = 0;
		WORD uOrdinal = 0;
		memcpy(&uMarker, pData, sizeof(uMarker));
		memcpy(&uOrdinal, pData + sizeof(uMarker), sizeof(uOrdinal));
		return uMarker == 0xFFFF && uOrdinal == uClassOrdinal;
	}

	bool IsSingleLineTextStaticControl(DWORD dwStyle, const BYTE* pClassData, const BYTE* pEnd)
	{
		const WORD DIALOG_CLASS_STATIC = 0x0082;
		if (!IsDialogTemplateOrdinalClass(pClassData, pEnd, DIALOG_CLASS_STATIC))
			return false;

		switch (dwStyle & SS_TYPEMASK) {
		case SS_LEFT:
		case SS_CENTER:
		case SS_RIGHT:
		case SS_SIMPLE:
		case SS_LEFTNOWORDWRAP:
			return true;
		default:
			return false;
		}
	}

	void EnsureScaledStaticControlHeight(short iOriginalHeight, short& cy, DWORD dwStyle, const BYTE* pClassData, const BYTE* pEnd, int iPercent)
	{
		if (iPercent > OPTIONS_WINDOW_SCALE_DISABLED && iOriginalHeight <= 9 && IsSingleLineTextStaticControl(dwStyle, pClassData, pEnd))
			++cy;
	}

	void EnsureScaledGeneralStartupFrameClearance(UINT uDialogId, UINT uControlId, short& cy, int iPercent)
	{
		if (uDialogId == IDD_PPG_GENERAL && uControlId == IDC_STARTUP
			&& (iPercent == OPTIONS_WINDOW_SCALE_20_PERCENT || iPercent == OPTIONS_WINDOW_SCALE_30_PERCENT
				|| iPercent == OPTIONS_WINDOW_SCALE_40_PERCENT || iPercent == OPTIONS_WINDOW_SCALE_50_PERCENT))
			++cy;
	}

	bool SkipDialogItemData(BYTE*& pData, BYTE* pEnd)
	{
		if (!SkipDialogTemplateField(pData, pEnd) || !SkipDialogTemplateField(pData, pEnd))
			return false;
		if (pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(WORD)))
			return false;

		WORD uExtraDataSize = 0;
		memcpy(&uExtraDataSize, pData, sizeof(uExtraDataSize));
		pData += sizeof(uExtraDataSize);
		if (pEnd - pData < static_cast<std::ptrdiff_t>(uExtraDataSize))
			return false;
		pData += uExtraDataSize;
		return true;
	}

	bool ScaleDialogTemplate(BYTE* pTemplate, size_t uTemplateSize, int iPercent, UINT uDialogId)
	{
		if (pTemplate == NULL || uTemplateSize < sizeof(SDialogTemplateHeader))
			return false;

		BYTE* const pEnd = pTemplate + uTemplateSize;
		WORD uDlgVer = 0;
		WORD uSignature = 0;
		memcpy(&uDlgVer, pTemplate, sizeof(uDlgVer));
		memcpy(&uSignature, pTemplate + sizeof(uDlgVer), sizeof(uSignature));
		const bool bExtendedTemplate = uDlgVer == 1 && uSignature == 0xFFFF;
		BYTE* pData = pTemplate;
		WORD uItemCount = 0;
		DWORD dwDialogStyle = 0;

		if (bExtendedTemplate) {
			if (uTemplateSize < sizeof(SDialogTemplateExHeader))
				return false;
			SDialogTemplateExHeader* pHeader = reinterpret_cast<SDialogTemplateExHeader*>(pData);
			uItemCount = pHeader->itemCount;
			dwDialogStyle = pHeader->style;
			ScaleDialogTemplateRect(pHeader->x, pHeader->y, pHeader->cx, pHeader->cy, iPercent);
			pData += sizeof(SDialogTemplateExHeader);
		} else {
			SDialogTemplateHeader* pHeader = reinterpret_cast<SDialogTemplateHeader*>(pData);
			uItemCount = pHeader->itemCount;
			dwDialogStyle = pHeader->style;
			ScaleDialogTemplateRect(pHeader->x, pHeader->y, pHeader->cx, pHeader->cy, iPercent);
			pData += sizeof(SDialogTemplateHeader);
		}

		if (!SkipDialogTemplateField(pData, pEnd) || !SkipDialogTemplateField(pData, pEnd) || !SkipDialogTemplateField(pData, pEnd))
			return false;
		if ((dwDialogStyle & DS_SETFONT) != 0) {
			const size_t uFontHeaderSize = bExtendedTemplate ? 6u : 2u;
			if (pEnd - pData < static_cast<std::ptrdiff_t>(uFontHeaderSize))
				return false;
			pData += uFontHeaderSize;
			if (!SkipDialogTemplateField(pData, pEnd))
				return false;
		}

		for (WORD i = 0; i < uItemCount; ++i) {
			pData = AlignDialogTemplatePointer(pData, pEnd);
			if (pData == NULL)
				return false;

			if (bExtendedTemplate) {
				if (pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(SDialogItemTemplateExHeader)))
					return false;
				SDialogItemTemplateExHeader* pItem = reinterpret_cast<SDialogItemTemplateExHeader*>(pData);
				const short iOriginalHeight = pItem->cy;
				ScaleDialogTemplateRect(pItem->x, pItem->y, pItem->cx, pItem->cy, iPercent);
				EnsureScaledGeneralStartupFrameClearance(uDialogId, pItem->id, pItem->cy, iPercent);
				pData += sizeof(SDialogItemTemplateExHeader);
				EnsureScaledStaticControlHeight(iOriginalHeight, pItem->cy, pItem->style, pData, pEnd, iPercent);
			} else {
				if (pEnd - pData < static_cast<std::ptrdiff_t>(sizeof(SDialogItemTemplateHeader)))
					return false;
				SDialogItemTemplateHeader* pItem = reinterpret_cast<SDialogItemTemplateHeader*>(pData);
				const short iOriginalHeight = pItem->cy;
				ScaleDialogTemplateRect(pItem->x, pItem->y, pItem->cx, pItem->cy, iPercent);
				EnsureScaledGeneralStartupFrameClearance(uDialogId, pItem->id, pItem->cy, iPercent);
				pData += sizeof(SDialogItemTemplateHeader);
				EnsureScaledStaticControlHeight(iOriginalHeight, pItem->cy, pItem->style, pData, pEnd, iPercent);
			}
			if (!SkipDialogItemData(pData, pEnd))
				return false;
		}
		return true;
	}

	const UINT PREFS_BANNER_CTRL_ID = 0x7EE1;
	const int PREFS_BANNER_MIN_WIDTH = 96;
	const int PREFS_BANNER_MAX_WIDTH = 144;
	const int PREFS_BANNER_MARGIN = 4;
	const int PREFS_BANNER_PADDING = 4;
	const int PREFS_BANNER_LAYOUT_DIVISOR = 4;
	const double PREFS_BANNER_FRAME_WIDTH_SCALE = 0.88;
	const int PREFS_BANNER_ALPHA_THRESHOLD = 10;
	const int PREFS_BANNER_BRIGHTNESS_THRESHOLD = 20;
	const UINT UM_OPTIONS_CLOSE_FOR_REOPEN = WM_APP + 1;
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
				return ScaleOptionsMetric(PREFS_BANNER_MIN_WIDTH);

			const CRect rectSource = GetSourceRect();
			const int nSourceWidth = rectSource.Width();
			const int nSourceHeight = rectSource.Height();
			if (nSourceWidth <= 0 || nSourceHeight <= 0)
				return ScaleOptionsMetric(PREFS_BANNER_MIN_WIDTH);

			const int nPadding = ScaleOptionsMetric(PREFS_BANNER_PADDING);
			const int nAvailableHeight = max(1, nTargetHeight - (nPadding * 2));
			const int nContentWidth = max(1, static_cast<int>(nAvailableHeight * (static_cast<double>(nSourceWidth) / static_cast<double>(nSourceHeight)) + 0.5));
			return nContentWidth + (nPadding * 2);
		}

		protected:
		CRect GetSourceRect() const
		{
			if (!m_rcContent.IsRectEmpty())
				return m_rcContent;

			if (!m_imgBanner.IsNull())
				return CRect(0, 0, m_imgBanner.GetWidth(), m_imgBanner.GetHeight());

			return CRect(0, 0, 0, 0);
		}

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

				const CRect rectSource = GetSourceRect();

			CRect rectDrawArea(rectClient);
			const int nPadding = ScaleOptionsMetric(PREFS_BANNER_PADDING);
			rectDrawArea.DeflateRect(nPadding, nPadding);
			if (rectDrawArea.IsRectEmpty())
				return;

			const int nSourceWidth = rectSource.Width();
			const int nSourceHeight = rectSource.Height();
			if (nSourceWidth <= 0 || nSourceHeight <= 0)
				return;

			const double fScaleX = static_cast<double>(rectDrawArea.Width()) / static_cast<double>(nSourceWidth);
			const double fScaleY = static_cast<double>(rectDrawArea.Height()) / static_cast<double>(nSourceHeight);
			// Always fit the full banner content inside the frame to avoid edge clipping on narrower layouts.
			const double fScale = min(fScaleX, fScaleY);
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

	struct SOptionToolTipOverride
	{
		UINT uDialogId;
		UINT uControlId;
		LPCTSTR pszKey;
	};

	static const SOptionToolTipOverride s_aOptionToolTipOverrides[] =
	{
		{ IDD_PPG_GENERAL, IDC_WEBSVEDIT, _T("OPTIONS_TIP_WEB_SERVICES") },
		{ IDD_PPG_GENERAL, IDC_ED2KFIX, _T("OPTIONS_TIP_ED2K_LINKS") },
		{ IDD_PPG_DISPLAY, IDC_3DDEP, _T("OPTIONS_TIP_PROGRESS_STYLE") },
		{ IDD_PPG_DISPLAY, IDC_3DDEPTH, _T("OPTIONS_TIP_PROGRESS_STYLE") },
		{ IDD_PPG_DISPLAY, IDC_FLAT, _T("OPTIONS_TIP_PROGRESS_STYLE") },
		{ IDD_PPG_DISPLAY, IDC_ROUND, _T("OPTIONS_TIP_PROGRESS_STYLE") },
		{ IDD_PPG_DISPLAY, IDC_PREVIEW, _T("OPTIONS_TIP_PROGRESS_STYLE") },
		{ IDD_PPG_DISPLAY, IDC_TOOLTIPDELAY_LBL, _T("OPTIONS_TIP_TOOLTIP_DELAY") },
		{ IDD_PPG_DISPLAY, IDC_TOOLTIPDELAY, _T("OPTIONS_TIP_TOOLTIP_DELAY") },
		{ IDD_PPG_DISPLAY, IDC_TOOLTIPDELAY_SPIN, _T("OPTIONS_TIP_TOOLTIP_DELAY") },
		{ IDD_PPG_DISPLAY, IDC_STATIC_CPUMEM, _T("OPTIONS_TIP_CPU_MEMORY") },
		{ IDD_PPG_DISPLAY, IDC_MINTRAY, _T("OPTIONS_TIP_MINIMIZE_TRAY") },
		{ IDD_PPG_DISPLAY, IDC_DBLCLICK, _T("OPTIONS_TIP_DOWNLOAD_EXPAND_CLICK") },
		{ IDD_PPG_DISPLAY, IDC_DBLCLICKFILENAMEPREVIEW, _T("OPTIONS_TIP_FILENAME_PREVIEW") },
		{ IDD_PPG_DISPLAY, IDC_SHOWDWLPERCENT, _T("OPTIONS_TIP_DOWNLOAD_PERCENT") },
		{ IDD_PPG_DISPLAY, IDC_SHOWRATEONTITLE, _T("OPTIONS_TIP_RATE_IN_TITLE") },
		{ IDD_PPG_DISPLAY, IDC_SHOWCATINFO, _T("OPTIONS_TIP_CATEGORY_COUNTS") },
		{ IDD_PPG_DISPLAY, IDC_CLEARCOMPL, _T("OPTIONS_TIP_CLEAR_COMPLETED") },
		{ IDD_PPG_DISPLAY, IDC_SHOWTRANSTOOLBAR, _T("OPTIONS_TIP_TRANSFER_TOOLBAR") },
		{ IDD_PPG_DISPLAY, IDC_STORESEARCHES, _T("OPTIONS_TIP_REMEMBER_SEARCHES") },
		{ IDD_PPG_DISPLAY, IDC_WIN7TASKBARGOODIES, _T("OPTIONS_TIP_TASKBAR_EFFECTS") },
		{ IDD_PPG_DISPLAY, IDC_SHOW_OPTIONS_TOOLTIPS, _T("OPTIONS_TIP_SHOW_TOOLTIPS") },
		{ IDD_PPG_DISPLAY, IDC_OPTIONS_WINDOW_SCALE_LABEL, _T("OPTIONS_TIP_WINDOW_SIZE") },
		{ IDD_PPG_DISPLAY, IDC_OPTIONS_WINDOW_SCALE, _T("OPTIONS_TIP_WINDOW_SIZE") },
		{ IDD_PPG_DISPLAY, IDC_DISABLEKNOWNLIST, _T("OPTIONS_TIP_DISABLE_KNOWN_CLIENTS") },
		{ IDD_PPG_DISPLAY, IDC_DISABLEQUEUELIST, _T("OPTIONS_TIP_DISABLE_QUEUE_LIST") },
		{ IDD_PPG_DISPLAY, IDC_SELECT_HYPERTEXT_FONT, _T("OPTIONS_TIP_SELECT_WINDOW_FONT") },
		{ IDD_PPG_DISPLAY, IDC_DISABLEHIST, _T("OPTIONS_TIP_AUTOCOMPLETE_HISTORY") },
		{ IDD_PPG_DISPLAY, IDC_RESETHIST, _T("OPTIONS_TIP_RESET_AUTOCOMPLETE") },
		{ IDD_PPG_CONNECTION, IDC_STARTTEST, _T("OPTIONS_TIP_TEST_PORTS") },
		{ IDD_PPG_CONNECTION, IDC_PREF_UPNPONSTART, _T("OPTIONS_TIP_UPNP") },
		{ IDD_PPG_CONNECTION, IDC_OPEN_PORTS_WINDOWS_FIREWALL, _T("OPTIONS_TIP_FIREWALL_PORTS") },
		{ IDD_PPG_CONNECTION, IDC_RANDOMIZE_PORTS_ON_STARTUP, _T("OPTIONS_TIP_RANDOM_PORTS") },
		{ IDD_PPG_SECURITY, IDC_RELOADFILTER, _T("OPTIONS_TIP_IPFILTER_RELOAD") },
		{ IDD_PPG_SECURITY, IDC_EDITFILTER, _T("OPTIONS_TIP_IPFILTER_EDIT") },
		{ IDD_PPG_SECURITY, IDC_LOADURL, _T("OPTIONS_TIP_IPFILTER_DOWNLOAD") },
		{ IDD_PPG_TWEAKS, IDC_OPENPREFINI, _T("OPTIONS_TIP_OPEN_PREFERENCES_INI") },
		{ IDD_PPG_DOWNLOAD_VALIDATOR, IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX, _T("OPTIONS_TIP_DOWNLOAD_VALIDATOR_RULES") },
		{ IDD_PPG_DOWNLOAD_VALIDATOR, IDC_DOWNLOAD_VALIDATOR_REGEX_VALIDATE, _T("OPTIONS_TIP_VALIDATE_RULES") },
		{ IDD_PPG_DOWNLOAD_VALIDATOR, IDC_DOWNLOAD_VALIDATOR_REGEX_RELOAD, _T("OPTIONS_TIP_RELOAD_RULES") },
		{ IDD_PPG_PROTECTION_PANEL, IDC_SHIELD_STATIC, _T("OPTIONS_TIP_SHIELD_STATUS") },
		{ IDD_PPG_PROTECTION_PANEL, IDC_SHIELD_RELOAD, _T("OPTIONS_TIP_SHIELD_RELOAD") },
		{ IDD_PPG_BLACKLIST_PANEL, IDC_BLACKLIST_DEFINITIONS_TEXTBOX, _T("OPTIONS_TIP_BLACKLIST_RULES") },
		{ IDD_PPG_BLACKLIST_PANEL, IDC_BLACKLIST_VALIDATE, _T("OPTIONS_TIP_VALIDATE_RULES") },
		{ IDD_PPG_BLACKLIST_PANEL, IDC_BLACKLIST_RELOAD, _T("OPTIONS_TIP_RELOAD_RULES") },
		{ IDD_PPG_BLACKLIST_PANEL, IDC_BLACKLIST_PANEL_HELP_TEXTBOX, _T("OPTIONS_TIP_BLACKLIST_HELP") },
		{ IDD_PPG_GENERAL, IDC_NICK_FRM, _T("OPTIONS_TIP_USER_NAME") },
		{ IDD_PPG_GENERAL, IDC_NICK, _T("OPTIONS_TIP_USER_NAME") },
		{ IDD_PPG_GENERAL, IDC_LANG_FRM, _T("OPTIONS_TIP_LANGUAGE") },
		{ IDD_PPG_GENERAL, IDC_LANGS, _T("OPTIONS_TIP_LANGUAGE") },
		{ IDD_PPG_GENERAL, IDC_MISC_FRM, _T("OPTIONS_TIP_GENERAL_MISC") },
		{ IDD_PPG_GENERAL, IDC_BRINGTOFOREGROUND, _T("OPTIONS_TIP_BRING_TO_FRONT") },
		{ IDD_PPG_GENERAL, IDC_EXIT, _T("OPTIONS_TIP_PROMPT_ON_EXIT") },
		{ IDD_PPG_GENERAL, IDC_ONLINESIG, _T("OPTIONS_TIP_ONLINE_SIGNATURE") },
		{ IDD_PPG_GENERAL, IDC_MINIMULE, _T("OPTIONS_TIP_MINIMULE") },
		{ IDD_PPG_GENERAL, IDC_PREVENTSTANDBY, _T("OPTIONS_TIP_PREVENT_STANDBY") },
		{ IDD_PPG_GENERAL, IDC_STARTUP, _T("OPTIONS_TIP_STARTUP_GROUP") },
		{ IDD_PPG_GENERAL, IDC_SPLASHON, _T("OPTIONS_TIP_SPLASH_SCREEN") },
		{ IDD_PPG_GENERAL, IDC_STARTMIN, _T("OPTIONS_TIP_START_MINIMIZED") },
		{ IDD_PPG_GENERAL, IDC_STARTWIN, _T("OPTIONS_TIP_START_WITH_WINDOWS") },
		{ IDD_PPG_DISPLAY, IDC_SELECT_HYPERTEXT_FONT, _T("OPTIONS_TIP_SELECT_WINDOW_FONT") },
		{ IDD_PPG_DISPLAY, IDC_DISABLEHIST, _T("OPTIONS_TIP_AUTOCOMPLETE_HISTORY") },
		{ IDD_PPG_CONNECTION, IDC_CAPACITIES_FRM, _T("OPTIONS_TIP_CONNECTION_CAPACITIES") },
		{ IDD_PPG_CONNECTION, IDC_DOWNLOAD_CAP, _T("OPTIONS_TIP_DOWNLOAD_CAPACITY") },
		{ IDD_PPG_CONNECTION, IDC_DCAP_LBL, _T("OPTIONS_TIP_DOWNLOAD_CAPACITY") },
		{ IDD_PPG_CONNECTION, IDC_KBS2, _T("OPTIONS_TIP_DOWNLOAD_CAPACITY") },
		{ IDD_PPG_CONNECTION, IDC_UPLOAD_CAP, _T("OPTIONS_TIP_UPLOAD_CAPACITY") },
		{ IDD_PPG_CONNECTION, IDC_UCAP_LBL, _T("OPTIONS_TIP_UPLOAD_CAPACITY") },
		{ IDD_PPG_CONNECTION, IDC_KBS3, _T("OPTIONS_TIP_UPLOAD_CAPACITY") },
		{ IDD_PPG_CONNECTION, IDC_LIMITS_FRM, _T("OPTIONS_TIP_CONNECTION_LIMITS") },
		{ IDD_PPG_CONNECTION, IDC_DLIMIT_LBL, _T("OPTIONS_TIP_DOWNLOAD_LIMIT") },
		{ IDD_PPG_CONNECTION, IDC_MAXDOWN_SLIDER, _T("OPTIONS_TIP_DOWNLOAD_LIMIT") },
		{ IDD_PPG_CONNECTION, IDC_KBS1, _T("OPTIONS_TIP_DOWNLOAD_LIMIT") },
		{ IDD_PPG_CONNECTION, IDC_ULIMIT_LBL, _T("OPTIONS_TIP_UPLOAD_LIMIT") },
		{ IDD_PPG_CONNECTION, IDC_MAXUP_SLIDER, _T("OPTIONS_TIP_UPLOAD_LIMIT") },
		{ IDD_PPG_CONNECTION, IDC_KBS4, _T("OPTIONS_TIP_UPLOAD_LIMIT") },
		{ IDD_PPG_CONNECTION, IDC_CLIENTPORT_FRM, _T("OPTIONS_TIP_CLIENT_PORTS") },
		{ IDD_PPG_CONNECTION, IDC_PORT, _T("OPTIONS_TIP_TCP_PORT") },
		{ IDD_PPG_CONNECTION, IDC_UDPPORT, _T("OPTIONS_TIP_UDP_PORT") },
		{ IDD_PPG_CONNECTION, IDC_UDPPORTLABEL, _T("OPTIONS_TIP_UDP_PORT") },
		{ IDD_PPG_CONNECTION, IDC_UDPDISABLE, _T("OPTIONS_TIP_DISABLE_UDP") },
		{ IDD_PPG_CONNECTION, IDC_RANDOM_PORT_RANGE_FRM, _T("OPTIONS_TIP_RANDOM_PORT_RANGE") },
		{ IDD_PPG_CONNECTION, IDC_RANDOM_PORT_RANGE_START_LABEL, _T("OPTIONS_TIP_RANDOM_PORT_START") },
		{ IDD_PPG_CONNECTION, IDC_RANDOM_PORT_RANGE_START, _T("OPTIONS_TIP_RANDOM_PORT_START") },
		{ IDD_PPG_CONNECTION, IDC_RANDOM_PORT_RANGE_END_LABEL, _T("OPTIONS_TIP_RANDOM_PORT_END") },
		{ IDD_PPG_CONNECTION, IDC_RANDOM_PORT_RANGE_END, _T("OPTIONS_TIP_RANDOM_PORT_END") },
		{ IDD_PPG_CONNECTION, IDC_MAXSRC_FRM, _T("OPTIONS_TIP_MAX_SOURCES") },
		{ IDD_PPG_CONNECTION, IDC_MAXSRCHARD_LBL, _T("OPTIONS_TIP_MAX_SOURCES") },
		{ IDD_PPG_CONNECTION, IDC_MAXSOURCEPERFILE, _T("OPTIONS_TIP_MAX_SOURCES") },
		{ IDD_PPG_CONNECTION, IDC_MAXCONN_FRM, _T("OPTIONS_TIP_MAX_CONNECTIONS") },
		{ IDD_PPG_CONNECTION, IDC_MAXCONLABEL, _T("OPTIONS_TIP_MAX_CONNECTIONS") },
		{ IDD_PPG_CONNECTION, IDC_MAXCON, _T("OPTIONS_TIP_MAX_CONNECTIONS") },
		{ IDD_PPG_CONNECTION, IDC_AUTOCONNECT, _T("OPTIONS_TIP_AUTOCONNECT") },
		{ IDD_PPG_CONNECTION, IDC_RECONN, _T("OPTIONS_TIP_RECONNECT") },
		{ IDD_PPG_CONNECTION, IDC_SHOWOVERHEAD, _T("OPTIONS_TIP_SHOW_OVERHEAD") },
		{ IDD_PPG_CONNECTION, IDC_FORCE_SPEEDS_TO_KB, _T("OPTIONS_TIP_FORCE_KB") },
		{ IDD_PPG_CONNECTION, IDC_WIZARD, _T("OPTIONS_TIP_CONNECTION_WIZARD") },
		{ IDD_PPG_CONNECTION, IDC_CONNECTION_NETWORK, _T("OPTIONS_TIP_NETWORK_SELECTION") },
		{ IDD_PPG_CONNECTION, IDC_NETWORK_KADEMLIA, _T("OPTIONS_TIP_ENABLE_KAD") },
		{ IDD_PPG_CONNECTION, IDC_NETWORK_ED2K, _T("OPTIONS_TIP_ENABLE_ED2K") },
		{ IDD_PPG_PROXY, IDC_AUTH_LBL2, _T("OPTIONS_TIP_PROXY_SETTINGS") },
		{ IDD_PPG_PROXY, IDC_ENABLEPROXY, _T("OPTIONS_TIP_PROXY_ENABLE") },
		{ IDD_PPG_PROXY, IDC_PROXYTYPE_LBL, _T("OPTIONS_TIP_PROXY_TYPE") },
		{ IDD_PPG_PROXY, IDC_PROXYTYPE, _T("OPTIONS_TIP_PROXY_TYPE") },
		{ IDD_PPG_PROXY, IDC_PROXYNAME_LBL, _T("OPTIONS_TIP_PROXY_ADDRESS") },
		{ IDD_PPG_PROXY, IDC_PROXYNAME, _T("OPTIONS_TIP_PROXY_ADDRESS") },
		{ IDD_PPG_PROXY, IDC_PROXYPORT_LBL, _T("OPTIONS_TIP_PROXY_PORT") },
		{ IDD_PPG_PROXY, IDC_PROXYPORT, _T("OPTIONS_TIP_PROXY_PORT") },
		{ IDD_PPG_PROXY, IDC_AUTH_LBL, _T("OPTIONS_TIP_PROXY_AUTH_GROUP") },
		{ IDD_PPG_PROXY, IDC_ENABLEAUTH, _T("OPTIONS_TIP_PROXY_AUTH_ENABLE") },
		{ IDD_PPG_PROXY, IDC_USERNAME_LBL, _T("OPTIONS_TIP_PROXY_USER") },
		{ IDD_PPG_PROXY, IDC_USERNAME_A, _T("OPTIONS_TIP_PROXY_USER") },
		{ IDD_PPG_PROXY, IDC_PASSWORD_LBL, _T("OPTIONS_TIP_PROXY_PASSWORD") },
		{ IDD_PPG_PROXY, IDC_PASSWORD, _T("OPTIONS_TIP_PROXY_PASSWORD") },
		{ IDD_PPG_SERVER, IDC_LBL_UPDATE_SERVERS, _T("OPTIONS_TIP_SERVER_UPDATE_GROUP") },
		{ IDD_PPG_SERVER, IDC_REMOVEDEAD, _T("OPTIONS_TIP_DEAD_SERVER_RETRIES") },
		{ IDD_PPG_SERVER, IDC_SERVERRETRIES, _T("OPTIONS_TIP_DEAD_SERVER_RETRIES") },
		{ IDD_PPG_SERVER, IDC_RETRIES_LBL, _T("OPTIONS_TIP_DEAD_SERVER_RETRIES") },
		{ IDD_PPG_SERVER, IDC_AUTOSERVER, _T("OPTIONS_TIP_SERVER_AUTO_UPDATE") },
		{ IDD_PPG_SERVER, IDC_EDITADR, _T("OPTIONS_TIP_SERVER_EDIT_ADDRESSES") },
		{ IDD_PPG_SERVER, IDC_UPDATESERVERCONNECT, _T("OPTIONS_TIP_SERVER_UPDATE_ON_SERVER") },
		{ IDD_PPG_SERVER, IDC_UPDATESERVERCLIENT, _T("OPTIONS_TIP_SERVER_UPDATE_ON_CLIENT") },
		{ IDD_PPG_SERVER, IDC_LBL_MISC, _T("OPTIONS_TIP_SERVER_MISC_GROUP") },
		{ IDD_PPG_SERVER, IDC_SMARTIDCHECK, _T("OPTIONS_TIP_SMART_LOW_ID") },
		{ IDD_PPG_SERVER, IDC_SAFESERVERCONNECT, _T("OPTIONS_TIP_SAFE_SERVER_CONNECT") },
		{ IDD_PPG_SERVER, IDC_AUTOCONNECTSTATICONLY, _T("OPTIONS_TIP_STATIC_SERVERS_ONLY") },
		{ IDD_PPG_SERVER, IDC_SCORE, _T("OPTIONS_TIP_SERVER_PRIORITY") },
		{ IDD_PPG_SERVER, IDC_MANUALSERVERHIGHPRIO, _T("OPTIONS_TIP_MANUAL_SERVER_PRIORITY") },
		{ IDD_PPG_DIRECTORIES, IDC_INCOMING_FRM, _T("OPTIONS_TIP_INCOMING_DIRECTORY") },
		{ IDD_PPG_DIRECTORIES, IDC_INCFILES, _T("OPTIONS_TIP_INCOMING_DIRECTORY") },
		{ IDD_PPG_DIRECTORIES, IDC_SELINCDIR, _T("OPTIONS_TIP_INCOMING_DIRECTORY") },
		{ IDD_PPG_DIRECTORIES, IDC_TEMP_FRM, _T("OPTIONS_TIP_TEMP_DIRECTORIES") },
		{ IDD_PPG_DIRECTORIES, IDC_TEMPFILES, _T("OPTIONS_TIP_TEMP_DIRECTORIES") },
		{ IDD_PPG_DIRECTORIES, IDC_SELTEMPDIR, _T("OPTIONS_TIP_TEMP_DIRECTORIES") },
		{ IDD_PPG_DIRECTORIES, IDC_SELTEMPDIRADD, _T("OPTIONS_TIP_ADD_TEMP_DIRECTORY") },
		{ IDD_PPG_DIRECTORIES, IDC_SHARED_FRM, _T("OPTIONS_TIP_SHARED_DIRECTORIES") },
		{ IDD_PPG_DIRECTORIES, IDC_SHARESELECTOR, _T("OPTIONS_TIP_SHARED_DIRECTORIES") },
		{ IDD_PPG_DIRECTORIES, IDC_UNCADD, _T("OPTIONS_TIP_ADD_UNC_SHARE") },
		{ IDD_PPG_FILES, IDC_ONND, _T("OPTIONS_TIP_NEW_DOWNLOADS_GROUP") },
		{ IDD_PPG_FILES, IDC_ADDNEWFILESPAUSED, _T("OPTIONS_TIP_ADD_FILES_PAUSED") },
		{ IDD_PPG_FILES, IDC_UAP, _T("OPTIONS_TIP_AUTO_UPLOAD_PRIORITY") },
		{ IDD_PPG_FILES, IDC_DAP, _T("OPTIONS_TIP_AUTO_DOWNLOAD_PRIORITY") },
		{ IDD_PPG_FILES, IDC_FNCLEANUP, _T("OPTIONS_TIP_CLEAN_FILE_NAMES") },
		{ IDD_PPG_FILES, IDC_FNC, _T("OPTIONS_TIP_EDIT_FILE_NAME_CLEANUP") },
		{ IDD_PPG_FILES, IDC_LBL_MISC, _T("OPTIONS_TIP_FILE_MISC_GROUP") },
		{ IDD_PPG_FILES, IDC_FULLCHUNKTRANS, _T("OPTIONS_TIP_FULL_CHUNK_UPLOAD") },
		{ IDD_PPG_FILES, IDC_PREVIEWPRIO, _T("OPTIONS_TIP_PREVIEW_CHUNKS") },
		{ IDD_PPG_FILES, IDC_WATCHCB, _T("OPTIONS_TIP_WATCH_CLIPBOARD") },
		{ IDD_PPG_FILES, IDC_PF_TIMECALC, _T("OPTIONS_TIP_ADVANCED_REMAINING_TIME") },
		{ IDD_PPG_FILES, IDC_STARTNEXTFILE, _T("OPTIONS_TIP_START_NEXT_FILE") },
		{ IDD_PPG_FILES, IDC_STARTNEXTFILECAT, _T("OPTIONS_TIP_START_NEXT_SAME_CATEGORY") },
		{ IDD_PPG_FILES, IDC_STARTNEXTFILECAT2, _T("OPTIONS_TIP_START_NEXT_ONLY_CATEGORY") },
		{ IDD_PPG_FILES, IDC_REMEMBERDOWNLOADED, _T("OPTIONS_TIP_REMEMBER_DOWNLOADED") },
		{ IDD_PPG_FILES, IDC_REMEMBERCANCELLED, _T("OPTIONS_TIP_REMEMBER_CANCELLED") },
		{ IDD_PPG_FILES, IDC_STATICVIDEOPLAYER, _T("OPTIONS_TIP_VIDEO_PLAYER_GROUP") },
		{ IDD_PPG_FILES, IDC_VIDEOPLAYER_CMD_LBL, _T("OPTIONS_TIP_VIDEO_PLAYER_COMMAND") },
		{ IDD_PPG_FILES, IDC_VIDEOPLAYER, _T("OPTIONS_TIP_VIDEO_PLAYER_COMMAND") },
		{ IDD_PPG_FILES, IDC_BROWSEV, _T("OPTIONS_TIP_VIDEO_PLAYER_COMMAND") },
		{ IDD_PPG_FILES, IDC_VIDEOPLAYER_ARGS_LBL, _T("OPTIONS_TIP_VIDEO_PLAYER_ARGUMENTS") },
		{ IDD_PPG_FILES, IDC_VIDEOPLAYER_ARGS, _T("OPTIONS_TIP_VIDEO_PLAYER_ARGUMENTS") },
		{ IDD_PPG_FILES, IDC_VIDEOBACKUP, _T("OPTIONS_TIP_VIDEO_PREVIEW_BACKUP") },
		{ IDD_PPG_NOTIFY, IDC_TBN_DISPLAYMODE_LABEL, _T("OPTIONS_TIP_NOTIFICATION_DISPLAY_MODE") },
		{ IDD_PPG_NOTIFY, IDC_TBN_DISPLAYMODE, _T("OPTIONS_TIP_NOTIFICATION_DISPLAY_MODE") },
		{ IDD_PPG_NOTIFY, IDC_TASKBARNOTIFIER, _T("OPTIONS_TIP_NOTIFICATION_SOUND_GROUP") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_NOSOUND, _T("OPTIONS_TIP_NOTIFICATION_NO_SOUND") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_USESOUND, _T("OPTIONS_TIP_NOTIFICATION_PLAY_SOUND") },
		{ IDD_PPG_NOTIFY, IDC_EDIT_TBN_WAVFILE, _T("OPTIONS_TIP_NOTIFICATION_SOUND_FILE") },
		{ IDD_PPG_NOTIFY, IDC_BTN_BROWSE_WAV, _T("OPTIONS_TIP_NOTIFICATION_SOUND_FILE") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_USESPEECH, _T("OPTIONS_TIP_NOTIFICATION_SPEECH") },
		{ IDD_PPG_NOTIFY, IDC_TEST_NOTIFICATION, _T("OPTIONS_TIP_NOTIFICATION_TEST") },
		{ IDD_PPG_NOTIFY, IDC_TBN_OPTIONS, _T("OPTIONS_TIP_NOTIFICATION_EVENTS") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_ONLOG, _T("OPTIONS_TIP_NOTIFY_LOG") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_ONCHAT, _T("OPTIONS_TIP_NOTIFY_CHAT_START") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_POP_ALWAYS, _T("OPTIONS_TIP_NOTIFY_CHAT_MESSAGE") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_ONNEWDOWNLOAD, _T("OPTIONS_TIP_NOTIFY_DOWNLOAD_ADDED") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_ONDOWNLOAD, _T("OPTIONS_TIP_NOTIFY_DOWNLOAD_FINISHED") },
		{ IDD_PPG_NOTIFY, IDC_CB_TBN_IMPORTATNT, _T("OPTIONS_TIP_NOTIFY_URGENT") },
		{ IDD_PPG_NOTIFY, IDC_EMAILNOT_GROUP, _T("OPTIONS_TIP_EMAIL_NOTIFICATIONS_GROUP") },
		{ IDD_PPG_NOTIFY, IDC_CB_ENABLENOTIFICATIONS, _T("OPTIONS_TIP_EMAIL_NOTIFICATIONS_ENABLE") },
		{ IDD_PPG_NOTIFY, IDC_SMTPSERVER, _T("OPTIONS_TIP_SMTP_SETTINGS") },
		{ IDD_PPG_NOTIFY, IDC_TXT_RECEIVER, _T("OPTIONS_TIP_EMAIL_RECEIVER") },
		{ IDD_PPG_NOTIFY, IDC_EDIT_RECEIVER, _T("OPTIONS_TIP_EMAIL_RECEIVER") },
		{ IDD_PPG_NOTIFY, IDC_TXT_SENDER, _T("OPTIONS_TIP_EMAIL_SENDER") },
		{ IDD_PPG_NOTIFY, IDC_EDIT_SENDER, _T("OPTIONS_TIP_EMAIL_SENDER") },
		{ IDD_PPG_STATS, IDC_GRAPHS, _T("OPTIONS_TIP_STATS_GRAPHS_GROUP") },
		{ IDD_PPG_STATS, IDC_SLIDERINFO, _T("OPTIONS_TIP_GRAPH_UPDATE_INTERVAL") },
		{ IDD_PPG_STATS, IDC_SLIDER, _T("OPTIONS_TIP_GRAPH_UPDATE_INTERVAL") },
		{ IDD_PPG_STATS, IDC_SLIDERINFO3, _T("OPTIONS_TIP_AVERAGE_GRAPH_TIME") },
		{ IDD_PPG_STATS, IDC_SLIDER3, _T("OPTIONS_TIP_AVERAGE_GRAPH_TIME") },
		{ IDD_PPG_STATS, IDC_PREFCOLORS, _T("OPTIONS_TIP_GRAPH_COLORS") },
		{ IDD_PPG_STATS, IDC_COLORSELECTOR, _T("OPTIONS_TIP_GRAPH_COLORS") },
		{ IDD_PPG_STATS, IDC_COLOR_BUTTON, _T("OPTIONS_TIP_GRAPH_COLOR_BUTTON") },
		{ IDD_PPG_STATS, IDC_FILL_GRAPHS, _T("OPTIONS_TIP_FILLED_GRAPHS") },
		{ IDD_PPG_STATS, IDC_STATIC_CGRAPHSCALE, _T("OPTIONS_TIP_CONNECTION_GRAPH_SCALE") },
		{ IDD_PPG_STATS, IDC_CGRAPHSCALE, _T("OPTIONS_TIP_CONNECTION_GRAPH_SCALE") },
		{ IDD_PPG_STATS, IDC_STATIC_CGRAPHRATIO, _T("OPTIONS_TIP_ACTIVE_CONNECTION_RATIO") },
		{ IDD_PPG_STATS, IDC_CRATIO, _T("OPTIONS_TIP_ACTIVE_CONNECTION_RATIO") },
		{ IDD_PPG_STATS, IDC_STREE, _T("OPTIONS_TIP_STATS_TREE_GROUP") },
		{ IDD_PPG_STATS, IDC_SLIDERINFO2, _T("OPTIONS_TIP_STATS_TREE_INTERVAL") },
		{ IDD_PPG_STATS, IDC_SLIDER2, _T("OPTIONS_TIP_STATS_TREE_INTERVAL") },
		{ IDD_PPG_IRC, IDC_IRC_SERVER_FRM, _T("OPTIONS_TIP_IRC_SERVER") },
		{ IDD_PPG_IRC, IDC_IRC_SERVER_BOX, _T("OPTIONS_TIP_IRC_SERVER") },
		{ IDD_PPG_IRC, IDC_IRC_NICK_FRM, _T("OPTIONS_TIP_IRC_NICK") },
		{ IDD_PPG_IRC, IDC_IRC_NICK_BOX, _T("OPTIONS_TIP_IRC_NICK") },
		{ IDD_PPG_IRC, IDC_IRC_FILTER_FRM, _T("OPTIONS_TIP_IRC_CHANNEL_FILTER_GROUP") },
		{ IDD_PPG_IRC, IDC_IRC_USECHANFILTER, _T("OPTIONS_TIP_IRC_CHANNEL_FILTER_ENABLE") },
		{ IDD_PPG_IRC, IDC_IRC_NAME_TEXT, _T("OPTIONS_TIP_IRC_CHANNEL_NAME") },
		{ IDD_PPG_IRC, IDC_IRC_NAME_BOX, _T("OPTIONS_TIP_IRC_CHANNEL_NAME") },
		{ IDD_PPG_IRC, IDC_IRC_MINUSER_TEXT, _T("OPTIONS_TIP_IRC_MIN_USERS") },
		{ IDD_PPG_IRC, IDC_IRC_MINUSER_BOX, _T("OPTIONS_TIP_IRC_MIN_USERS") },
		{ IDD_PPG_IRC, IDC_IRC_PERFORM_FRM, _T("OPTIONS_TIP_IRC_PERFORM_GROUP") },
		{ IDD_PPG_IRC, IDC_IRC_USEPERFORM, _T("OPTIONS_TIP_IRC_PERFORM_ENABLE") },
		{ IDD_PPG_IRC, IDC_IRC_PERFORM_BOX, _T("OPTIONS_TIP_IRC_PERFORM_STRING") },
		{ IDD_PPG_IRC, IDC_IRC_MISC_FRM, _T("OPTIONS_TIP_IRC_MISC_GROUP") },
		{ IDD_PPG_MESSAGES, IDC_MSG, _T("OPTIONS_TIP_MESSAGES_GROUP") },
		{ IDD_PPG_MESSAGES, IDC_FILTERLABEL, _T("OPTIONS_TIP_MESSAGE_FILTER") },
		{ IDD_PPG_MESSAGES, IDC_FILTER, _T("OPTIONS_TIP_MESSAGE_FILTER") },
		{ IDD_PPG_MESSAGES, IDC_MSGONLYFRIENDS, _T("OPTIONS_TIP_MESSAGES_FRIENDS_ONLY") },
		{ IDD_PPG_MESSAGES, IDC_ADVSPAMFILTER, _T("OPTIONS_TIP_ADVANCED_SPAM_FILTER") },
		{ IDD_PPG_MESSAGES, IDC_USECAPTCHAS, _T("OPTIONS_TIP_CAPTCHA") },
		{ IDD_PPG_MESSAGES, IDC_MSHOWSMILEYS, _T("OPTIONS_TIP_MESSAGE_SMILEYS") },
		{ IDD_PPG_MESSAGES, IDC_STATIC_COMMENTS, _T("OPTIONS_TIP_COMMENTS_GROUP") },
		{ IDD_PPG_MESSAGES, IDC_FILTERCOMMENTSLABEL, _T("OPTIONS_TIP_COMMENT_FILTER") },
		{ IDD_PPG_MESSAGES, IDC_COMMENTFILTER, _T("OPTIONS_TIP_COMMENT_FILTER") },
		{ IDD_PPG_MESSAGES, IDC_INDICATERATINGS, _T("OPTIONS_TIP_RATING_ICONS") },
		{ IDD_PPG_SECURITY, IDC_STATIC_IPFILTER, _T("OPTIONS_TIP_IP_FILTER_GROUP") },
		{ IDD_PPG_SECURITY, IDC_FILTER_SERVER_BY_IPFILTER, _T("OPTIONS_TIP_FILTER_SERVERS") },
		{ IDD_PPG_SECURITY, IDC_DONTFILTERPRIVATEIPS, _T("OPTIONS_TIP_ALLOW_PRIVATE_IPS") },
		{ IDD_PPG_SECURITY, IDC_STATIC_FILTERLEVEL, _T("OPTIONS_TIP_FILTER_LEVEL") },
		{ IDD_PPG_SECURITY, IDC_STATIC_FILTERLEVEL2, _T("OPTIONS_TIP_FILTER_LEVEL") },
		{ IDD_PPG_SECURITY, IDC_FILTERLEVEL, _T("OPTIONS_TIP_FILTER_LEVEL") },
		{ IDD_PPG_SECURITY, IDC_STATIC_UPDATEFROM, _T("OPTIONS_TIP_IP_FILTER_UPDATE_URL") },
		{ IDD_PPG_SECURITY, IDC_UPDATEURL, _T("OPTIONS_TIP_IP_FILTER_UPDATE_URL") },
		{ IDD_PPG_SECURITY, IDC_DD, _T("OPTIONS_TIP_IP_FILTER_URL_HISTORY") },
		{ IDD_PPG_SECURITY, IDC_AUTOUPDATE_IPFILTER, _T("OPTIONS_TIP_IP_FILTER_AUTO_UPDATE") },
		{ IDD_PPG_SECURITY, IDC_PERIODDAYS_LABEL, _T("OPTIONS_TIP_IP_FILTER_PERIOD") },
		{ IDD_PPG_SECURITY, IDC_IPFILTERPERIOD, _T("OPTIONS_TIP_IP_FILTER_PERIOD") },
		{ IDD_PPG_SECURITY, IDC_SEEMYSHARE_FRM, _T("OPTIONS_TIP_SHARED_FILES_ACCESS") },
		{ IDD_PPG_SECURITY, IDC_SEESHARE1, _T("OPTIONS_TIP_SHARED_FILES_EVERYONE") },
		{ IDD_PPG_SECURITY, IDC_SEESHARE2, _T("OPTIONS_TIP_SHARED_FILES_FRIENDS") },
		{ IDD_PPG_SECURITY, IDC_SEESHARE3, _T("OPTIONS_TIP_SHARED_FILES_NOBODY") },
		{ IDD_PPG_SECURITY, IDC_SEC_OBFUSCATIONBOX, _T("OPTIONS_TIP_OBFUSCATION_GROUP") },
		{ IDD_PPG_SECURITY, IDC_ENABLEOBFUSCATION, _T("OPTIONS_TIP_ENABLE_OBFUSCATION") },
		{ IDD_PPG_SECURITY, IDC_ONLYOBFUSCATED, _T("OPTIONS_TIP_ONLY_OBFUSCATED") },
		{ IDD_PPG_SECURITY, IDC_DISABLEOBFUSCATION, _T("OPTIONS_TIP_DISABLE_OBFUSCATION") },
		{ IDD_PPG_SECURITY, IDC_SEC_MISC, _T("OPTIONS_TIP_SECURITY_MISC_GROUP") },
		{ IDD_PPG_SECURITY, IDC_USESECIDENT, _T("OPTIONS_TIP_SECURE_IDENT") },
		{ IDD_PPG_SECURITY, IDC_RUNASUSER, _T("OPTIONS_TIP_RUN_UNPRIVILEGED") },
		{ IDD_PPG_SECURITY, IDC_SEARCHSPAMFILTER, _T("OPTIONS_TIP_SEARCH_SPAM_FILTER") },
		{ IDD_PPG_SECURITY, IDC_CHECK_FILE_OPEN, _T("OPTIONS_TIP_UNTRUSTED_FILE_WARNING") },
		{ IDD_PPG_SCHEDULER, IDC_ENABLE, _T("OPTIONS_TIP_SCHEDULER_ENABLE") },
		{ IDD_PPG_SCHEDULER, IDC_NEW, _T("OPTIONS_TIP_SCHEDULER_NEW") },
		{ IDD_PPG_SCHEDULER, IDC_REMOVE, _T("OPTIONS_TIP_SCHEDULER_REMOVE") },
		{ IDD_PPG_SCHEDULER, IDC_SCHEDLIST, _T("OPTIONS_TIP_SCHEDULER_LIST") },
		{ IDD_PPG_SCHEDULER, IDC_S_ENABLE, _T("OPTIONS_TIP_SCHEDULE_ENTRY_ENABLE") },
		{ IDD_PPG_SCHEDULER, IDC_STATIC_S_TITLE, _T("OPTIONS_TIP_SCHEDULE_TITLE") },
		{ IDD_PPG_SCHEDULER, IDC_S_TITLE, _T("OPTIONS_TIP_SCHEDULE_TITLE") },
		{ IDD_PPG_SCHEDULER, IDC_STATIC_S_TIME, _T("OPTIONS_TIP_SCHEDULE_TIME_MODE") },
		{ IDD_PPG_SCHEDULER, IDC_TIMESEL, _T("OPTIONS_TIP_SCHEDULE_TIME_MODE") },
		{ IDD_PPG_SCHEDULER, IDC_DATETIMEPICKER1, _T("OPTIONS_TIP_SCHEDULE_START_TIME") },
		{ IDD_PPG_SCHEDULER, IDC_DATETIMEPICKER2, _T("OPTIONS_TIP_SCHEDULE_END_TIME") },
		{ IDD_PPG_SCHEDULER, IDC_CHECKNOENDTIME, _T("OPTIONS_TIP_SCHEDULE_NO_END") },
		{ IDD_PPG_SCHEDULER, IDC_STATIC_S_ACTION, _T("OPTIONS_TIP_SCHEDULE_ACTIONS") },
		{ IDD_PPG_SCHEDULER, IDC_SCHEDACTION, _T("OPTIONS_TIP_SCHEDULE_ACTIONS") },
		{ IDD_PPG_SCHEDULER, IDC_APPLY, _T("OPTIONS_TIP_SCHEDULE_APPLY_ENTRY") },
		{ IDD_PPG_SCHEDULER, IDC_STATIC_DETAILS, _T("OPTIONS_TIP_SCHEDULE_DETAILS") },
		{ IDD_PPG_WEBSRV, IDC_STATIC_GENERAL, _T("OPTIONS_TIP_WEB_GENERAL_GROUP") },
		{ IDD_PPG_WEBSRV, IDC_WSENABLED, _T("OPTIONS_TIP_WEB_ENABLE") },
		{ IDD_PPG_WEBSRV, IDC_WS_GZIP, _T("OPTIONS_TIP_WEB_GZIP") },
		{ IDD_PPG_WEBSRV, IDC_WSUPNP, _T("OPTIONS_TIP_WEB_UPNP") },
		{ IDD_PPG_WEBSRV, IDC_WSPORT_LBL, _T("OPTIONS_TIP_WEB_PORT") },
		{ IDD_PPG_WEBSRV, IDC_WSPORT, _T("OPTIONS_TIP_WEB_PORT") },
		{ IDD_PPG_WEBSRV, IDC_TEMPLATE, _T("OPTIONS_TIP_WEB_TEMPLATE") },
		{ IDD_PPG_WEBSRV, IDC_TMPLPATH, _T("OPTIONS_TIP_WEB_TEMPLATE") },
		{ IDD_PPG_WEBSRV, IDC_TMPLBROWSE, _T("OPTIONS_TIP_WEB_TEMPLATE_BROWSE") },
		{ IDD_PPG_WEBSRV, IDC_WSRELOADTMPL, _T("OPTIONS_TIP_WEB_TEMPLATE_RELOAD") },
		{ IDD_PPG_WEBSRV, IDC_WSTIMEOUTLABEL, _T("OPTIONS_TIP_WEB_SESSION_TIMEOUT") },
		{ IDD_PPG_WEBSRV, IDC_WSTIMEOUT, _T("OPTIONS_TIP_WEB_SESSION_TIMEOUT") },
		{ IDD_PPG_WEBSRV, IDC_MINS, _T("OPTIONS_TIP_WEB_SESSION_TIMEOUT") },
		{ IDD_PPG_WEBSRV, IDC_WEB_HTTPS, _T("OPTIONS_TIP_WEB_HTTPS") },
		{ IDD_PPG_WEBSRV, IDC_WEB_GENERATE, _T("OPTIONS_TIP_WEB_GENERATE_CERT") },
		{ IDD_PPG_WEBSRV, IDC_WEB_CERT, _T("OPTIONS_TIP_WEB_CERTIFICATE") },
		{ IDD_PPG_WEBSRV, IDC_CERTPATH, _T("OPTIONS_TIP_WEB_CERTIFICATE") },
		{ IDD_PPG_WEBSRV, IDC_CERTBROWSE, _T("OPTIONS_TIP_WEB_CERTIFICATE") },
		{ IDD_PPG_WEBSRV, IDC_WEB_KEY, _T("OPTIONS_TIP_WEB_PRIVATE_KEY") },
		{ IDD_PPG_WEBSRV, IDC_KEYPATH, _T("OPTIONS_TIP_WEB_PRIVATE_KEY") },
		{ IDD_PPG_WEBSRV, IDC_KEYBROWSE, _T("OPTIONS_TIP_WEB_PRIVATE_KEY") },
		{ IDD_PPG_WEBSRV, IDC_STATIC_ADMIN, _T("OPTIONS_TIP_WEB_ADMIN_GROUP") },
		{ IDD_PPG_WEBSRV, IDC_WSPASS_LBL, _T("OPTIONS_TIP_WEB_ADMIN_PASSWORD") },
		{ IDD_PPG_WEBSRV, IDC_WSPASS, _T("OPTIONS_TIP_WEB_ADMIN_PASSWORD") },
		{ IDD_PPG_WEBSRV, IDC_WS_ALLOWHILEVFUNC, _T("OPTIONS_TIP_WEB_HIGH_LEVEL_ACTIONS") },
		{ IDD_PPG_WEBSRV, IDC_STATIC_LOWUSER, _T("OPTIONS_TIP_WEB_LOW_USER_GROUP") },
		{ IDD_PPG_WEBSRV, IDC_WSENABLEDLOW, _T("OPTIONS_TIP_WEB_LOW_USER_ENABLE") },
		{ IDD_PPG_WEBSRV, IDC_WSPASS_LBL2, _T("OPTIONS_TIP_WEB_LOW_USER_PASSWORD") },
		{ IDD_PPG_WEBSRV, IDC_WSPASSLOW, _T("OPTIONS_TIP_WEB_LOW_USER_PASSWORD") },
		{ IDD_PPG_TWEAKS, IDC_WARNING, _T("OPTIONS_TIP_EXTENDED_WARNING") },
		{ IDD_PPG_TWEAKS, IDC_BTL_TEXT, _T("OPTIONS_TIP_FILE_BUFFER_TIME") },
		{ IDD_PPG_TWEAKS, IDC_BTL, _T("OPTIONS_TIP_FILE_BUFFER_TIME") },
		{ IDD_PPG_TWEAKS, IDC_FILEBUFFERSIZE_STATIC, _T("OPTIONS_TIP_FILE_BUFFER_SIZE") },
		{ IDD_PPG_TWEAKS, IDC_FILEBUFFERSIZE, _T("OPTIONS_TIP_FILE_BUFFER_SIZE") },
		{ IDD_PPG_TWEAKS, IDC_QUEUESIZE_STATIC, _T("OPTIONS_TIP_UPLOAD_QUEUE_SIZE") },
		{ IDD_PPG_TWEAKS, IDC_QUEUESIZE, _T("OPTIONS_TIP_UPLOAD_QUEUE_SIZE") },
		{ IDD_PPG_TWEAKS, IDC_PREFINI_STATIC, _T("OPTIONS_TIP_EXTENDED_INI") },
		{ IDD_PPG_BLACKLIST_PANEL, IDC_BLACKLIST_OPT_FRM, _T("OPTIONS_TIP_BLACKLIST_OPTIONS_GROUP") },
		{ IDD_PPG_BLACKLIST_PANEL, IDC_BLACKLIST_DEF_FRM, _T("OPTIONS_TIP_BLACKLIST_DEFINITIONS_GROUP") },
	};

	CString FormatOptionToolTipText(const CString& strText)
	{
		CString strNormalized(strText);
		strNormalized.Replace(_T("\r\n"), _T("\n"));
		strNormalized.Replace(_T('\r'), _T('\n'));

		CString strResult;
		int iParagraphStart = 0;
		const int iWrapColumn = 88;
		while (iParagraphStart <= strNormalized.GetLength()) {
			const int iParagraphEnd = strNormalized.Find(_T('\n'), iParagraphStart);
			CString strRemaining = iParagraphEnd >= 0
				? strNormalized.Mid(iParagraphStart, iParagraphEnd - iParagraphStart)
				: strNormalized.Mid(iParagraphStart);

			if (strRemaining.IsEmpty()) {
				if (!strResult.IsEmpty())
					strResult += _T("\r\n");
			} else {
				while (!strRemaining.IsEmpty()) {
					if (strRemaining.GetLength() <= iWrapColumn) {
						if (!strResult.IsEmpty())
							strResult += _T("\r\n");
						strResult += strRemaining;
						break;
					}

					int iBreakPos = strRemaining.Left(iWrapColumn + 1).ReverseFind(_T(' '));
					if (iBreakPos <= 0)
						iBreakPos = iWrapColumn;

					CString strWrappedLine = strRemaining.Left(iBreakPos);
					strWrappedLine.TrimRight();
					if (!strResult.IsEmpty())
						strResult += _T("\r\n");
					strResult += strWrappedLine;

					strRemaining = strRemaining.Mid(iBreakPos);
					strRemaining.TrimLeft();
				}
			}

			if (iParagraphEnd < 0)
				break;
			iParagraphStart = iParagraphEnd + 1;
		}

		strResult.TrimRight(_T("\r\n"));
		return strResult;
	}

	bool IsOptionToolTipExcluded(UINT uDialogId, UINT uControlId)
	{
		return (uDialogId == IDD_PPG_DOWNLOAD_VALIDATOR && uControlId == IDC_DOWNLOAD_VALIDATOR_VIEW_TABS)
			|| (uDialogId == IDD_PPG_BLACKLIST_PANEL && uControlId == IDC_BLACKLIST_VIEW_TABS);
	}

	bool HasPageLocalOptionToolTip(UINT uDialogId, UINT uControlId)
	{
		if (uDialogId == IDD_PPG_NETWORK_INTERFACE)
			return true;
		if (uControlId == IDC_MOD_OPTS || uControlId == IDC_DOWNLOAD_VALIDATOR_OPTIONS || uControlId == IDC_PROTECTION_PANEL_OPTS
			|| uControlId == IDC_EXT_OPTS || uControlId == IDC_MISC_IRC
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
			|| uControlId == IDC_DEBUG_OPTS
#endif
			)
			return true;
		if (uDialogId == IDD_PPG_BLACKLIST_PANEL
			&& (uControlId == IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX || uControlId == IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX
				|| uControlId == IDC_BLACKLIST_LOG_CHECKBOX || uControlId == IDC_BLACKLIST_AUTOREMOVE_CHECKBOX))
			return true;
		return false;
	}
}

IMPLEMENT_DYNAMIC(CPreferencesDlg, CTreePropSheet)

BEGIN_MESSAGE_MAP(CPreferencesDlg, CTreePropSheet)
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_HELPINFO()
	ON_MESSAGE(UM_OPTIONS_CLOSE_FOR_REOPEN, OnCloseForModalReopen)
END_MESSAGE_MAP()

CPreferencesDlg::CPreferencesDlg()
{
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_wndGeneral.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndDisplay.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndConnection.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndNetworkInterface.m_psp.dwFlags &= ~PSH_HASHELP;
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
	m_wndDownloadValidator.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndProtectionPanel.m_psp.dwFlags &= ~PSH_HASHELP;
	m_wndBlacklistPanel.m_psp.dwFlags &= ~PSH_HASHELP;
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	m_wndDebug.m_psp.dwFlags &= ~PSH_HASHELP;
#endif

	CTreePropSheet::SetPageIcon(&m_wndGeneral, _T("Preferences"));
	CTreePropSheet::SetPageIcon(&m_wndDisplay, _T("DISPLAY"));
	CTreePropSheet::SetPageIcon(&m_wndConnection, _T("CONNECTION"));
	CTreePropSheet::SetPageIcon(&m_wndNetworkInterface, _T("NETWORK_INTERFACE"));
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
	CTreePropSheet::SetPageIcon(&m_wndDownloadValidator, _T("PREFERENCESBLUE"));
	CTreePropSheet::SetPageIcon(&m_wndProtectionPanel, _T("PROTECTION_PANEL"));
	CTreePropSheet::SetPageIcon(&m_wndBlacklistPanel, _T("Blacklist_PANEL"));
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	CTreePropSheet::SetPageIcon(&m_wndDebug, _T("Preferences"));
#endif

	AddPage(&m_wndGeneral);
	AddPage(&m_wndDisplay);
	AddPage(&m_wndConnection);
	AddPage(&m_wndNetworkInterface);
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
	AddPage(&m_wndDownloadValidator);
	AddPage(&m_wndProtectionPanel);
	AddPage(&m_wndBlacklistPanel);
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	AddPage(&m_wndDebug);
#endif

	SetTreeWidth(170);

	m_uPshStartPageId = 0;
	m_bSaveIniFile = false;
	m_bApplyButtonClicked = false;
	m_pBannerWnd = NULL;
	m_nBannerWidth = 0;
	m_bShowOptionsToolTips = thePrefs.GetShowOptionsToolTips();
	m_bOptionsWindowScaleRefreshPending = false;
	m_uReopenPageId = 0;
	m_bModalReopenClosePosted = false;
	m_bClosingForModalReopen = false;
	m_hRegisteredOptionPage = NULL;
	m_hActiveTreeOptionToolTip = NULL;
}

INT_PTR CPreferencesDlg::DoModal()
{
	ASSERT(m_hWnd == NULL);
	m_bApplyButtonClicked = false;
	m_bOptionsWindowScaleRefreshPending = false;
	m_uReopenPageId = 0;
	m_bModalReopenClosePosted = false;
	m_bClosingForModalReopen = false;
	s_iOptionsVisualScalePercent = thePrefs.GetOptionsWindowScalePercent();
	if (!SetVisualScalePercent(s_iOptionsVisualScalePercent))
		return -1;

	// The height of the option dialog is already too large for 640x480. To show as much as
	// possible we do not show a page caption (which is a decorative element only anyway).
	CRect rectWorkArea;
	if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &rectWorkArea, 0))
		rectWorkArea.SetRect(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	SetTreeViewMode(TRUE, rectWorkArea.Height() >= ScaleOptionsMetric(600), TRUE);

	if (!PrepareScaledPageTemplates())
		AfxThrowResourceException();
	return CTreePropSheet::DoModal();
}

UINT CPreferencesDlg::GetPageDialogId(const CPropertyPage* pPage) const
{
	if (pPage == &m_wndGeneral) return IDD_PPG_GENERAL;
	if (pPage == &m_wndDisplay) return IDD_PPG_DISPLAY;
	if (pPage == &m_wndConnection) return IDD_PPG_CONNECTION;
	if (pPage == &m_wndNetworkInterface) return IDD_PPG_NETWORK_INTERFACE;
	if (pPage == &m_wndProxy) return IDD_PPG_PROXY;
	if (pPage == &m_wndServer) return IDD_PPG_SERVER;
	if (pPage == &m_wndDirectories) return IDD_PPG_DIRECTORIES;
	if (pPage == &m_wndFiles) return IDD_PPG_FILES;
	if (pPage == &m_wndNotify) return IDD_PPG_NOTIFY;
	if (pPage == &m_wndStats) return IDD_PPG_STATS;
	if (pPage == &m_wndIRC) return IDD_PPG_IRC;
	if (pPage == &m_wndMessages) return IDD_PPG_MESSAGES;
	if (pPage == &m_wndSecurity) return IDD_PPG_SECURITY;
	if (pPage == &m_wndScheduler) return IDD_PPG_SCHEDULER;
	if (pPage == &m_wndWebServer) return IDD_PPG_WEBSRV;
	if (pPage == &m_wndTweaks) return IDD_PPG_TWEAKS;
	if (pPage == &m_wndMod) return IDD_PPG_MOD;
	if (pPage == &m_wndDownloadValidator) return IDD_PPG_DOWNLOAD_VALIDATOR;
	if (pPage == &m_wndProtectionPanel) return IDD_PPG_PROTECTION_PANEL;
	if (pPage == &m_wndBlacklistPanel) return IDD_PPG_BLACKLIST_PANEL;
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	if (pPage == &m_wndDebug) return IDD_PPG_DEBUG;
#endif
	return 0;
}

bool CPreferencesDlg::PrepareScaledPageTemplates()
{
	struct SPageTemplateEntry
	{
		CPropertyPage* pPage;
		UINT uDialogId;
	};
	const SPageTemplateEntry aPages[] = {
		{ &m_wndGeneral, IDD_PPG_GENERAL },
		{ &m_wndDisplay, IDD_PPG_DISPLAY },
		{ &m_wndConnection, IDD_PPG_CONNECTION },
		{ &m_wndNetworkInterface, IDD_PPG_NETWORK_INTERFACE },
		{ &m_wndProxy, IDD_PPG_PROXY },
		{ &m_wndServer, IDD_PPG_SERVER },
		{ &m_wndDirectories, IDD_PPG_DIRECTORIES },
		{ &m_wndFiles, IDD_PPG_FILES },
		{ &m_wndNotify, IDD_PPG_NOTIFY },
		{ &m_wndStats, IDD_PPG_STATS },
		{ &m_wndIRC, IDD_PPG_IRC },
		{ &m_wndMessages, IDD_PPG_MESSAGES },
		{ &m_wndSecurity, IDD_PPG_SECURITY },
		{ &m_wndScheduler, IDD_PPG_SCHEDULER },
		{ &m_wndWebServer, IDD_PPG_WEBSRV },
		{ &m_wndTweaks, IDD_PPG_TWEAKS },
		{ &m_wndMod, IDD_PPG_MOD },
		{ &m_wndDownloadValidator, IDD_PPG_DOWNLOAD_VALIDATOR },
		{ &m_wndProtectionPanel, IDD_PPG_PROTECTION_PANEL },
		{ &m_wndBlacklistPanel, IDD_PPG_BLACKLIST_PANEL },
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
		{ &m_wndDebug, IDD_PPG_DEBUG },
#endif
	};

	const HINSTANCE hResourceInstance = AfxGetResourceHandle();
	for (size_t i = 0; i < _countof(aPages); ++i) {
		aPages[i].pPage->m_psp.dwFlags &= ~PSP_DLGINDIRECT;
		aPages[i].pPage->m_psp.pszTemplate = MAKEINTRESOURCE(aPages[i].uDialogId);
		aPages[i].pPage->m_psp.hInstance = hResourceInstance;
	}
	m_aScaledPageTemplates.clear();

	if (s_iOptionsVisualScalePercent == OPTIONS_WINDOW_SCALE_DISABLED)
		return true;

	std::vector<std::unique_ptr<BYTE[]> > aScaledTemplates;
	aScaledTemplates.reserve(_countof(aPages));
	for (size_t i = 0; i < _countof(aPages); ++i) {
		const HRSRC hResource = ::FindResource(hResourceInstance, MAKEINTRESOURCE(aPages[i].uDialogId), RT_DIALOG);
		if (hResource == NULL)
			return false;
		const DWORD dwResourceSize = ::SizeofResource(hResourceInstance, hResource);
		const HGLOBAL hResourceData = ::LoadResource(hResourceInstance, hResource);
		const void* pResourceData = hResourceData != NULL ? ::LockResource(hResourceData) : NULL;
		if (dwResourceSize == 0 || pResourceData == NULL)
			return false;

		std::unique_ptr<BYTE[]> pScaledTemplate(new BYTE[dwResourceSize]);
		memcpy(pScaledTemplate.get(), pResourceData, dwResourceSize);
		if (!ScaleDialogTemplate(pScaledTemplate.get(), dwResourceSize, s_iOptionsVisualScalePercent, aPages[i].uDialogId))
			return false;
		aScaledTemplates.push_back(std::move(pScaledTemplate));
	}

	for (size_t i = 0; i < _countof(aPages); ++i) {
		aPages[i].pPage->m_psp.dwFlags |= PSP_DLGINDIRECT;
		aPages[i].pPage->m_psp.pResource = reinterpret_cast<LPCDLGTEMPLATE>(aScaledTemplates[i].get());
	}
	m_aScaledPageTemplates.swap(aScaledTemplates);
	return true;
}

int CPreferencesDlg::ScaleOptionsValue(int iValue)
{
	return ScaleOptionsMetric(iValue);
}

UINT CPreferencesDlg::GetActivePageDialogId()
{
	const int iActivePage = GetActiveIndex();
	if (iActivePage < 0)
		return 0;

	return GetPageDialogId(GetPage(iActivePage));
}

bool CPreferencesDlg::IsOptionToolTipWindowRegistered(HWND hWnd) const
{
	for (INT_PTR i = 0; i < m_aOptionToolTipWindows.GetCount(); ++i)
		if (m_aOptionToolTipWindows[i] == hWnd)
			return true;
	return false;
}

CString CPreferencesDlg::BuildOptionToolTipText(CWnd* pPage, CWnd* pControl)
{
	if (pPage == NULL || pControl == NULL)
		return CString();

	const UINT uDialogId = GetActivePageDialogId();
	const UINT uControlId = static_cast<UINT>(pControl->GetDlgCtrlID());
	for (size_t i = 0; i < _countof(s_aOptionToolTipOverrides); ++i) {
		if (s_aOptionToolTipOverrides[i].uDialogId != uDialogId || s_aOptionToolTipOverrides[i].uControlId != uControlId)
			continue;

		CString strResult(GetResString(s_aOptionToolTipOverrides[i].pszKey));
		if (_tcscmp(s_aOptionToolTipOverrides[i].pszKey, _T("OPTIONS_TIP_DOWNLOAD_CAPACITY")) == 0
			|| _tcscmp(s_aOptionToolTipOverrides[i].pszKey, _T("OPTIONS_TIP_UPLOAD_CAPACITY")) == 0) {
			const UINT uUnitControlId = _tcscmp(s_aOptionToolTipOverrides[i].pszKey, _T("OPTIONS_TIP_DOWNLOAD_CAPACITY")) == 0 ? IDC_KBS2 : IDC_KBS3;
			CString strUnit;
			CWnd* pUnitControl = pPage->GetDlgItem(uUnitControlId);
			if (pUnitControl != NULL)
				pUnitControl->GetWindowText(strUnit);
			strUnit.Trim();
			if (!strUnit.IsEmpty()) {
				CString strFormatted;
				strFormatted.Format(strResult, (LPCTSTR)strUnit);
				strResult = strFormatted;
			}
		}
		return FormatOptionToolTipText(strResult);
	}
	return CString();
}

void CPreferencesDlg::RegisterActivePageToolTips(bool bUpdateExisting)
{
	if (m_tooltipOptions.GetSafeHwnd() == NULL)
		return;

	const int iActivePage = GetActiveIndex();
	CPropertyPage* pPage = iActivePage >= 0 ? GetPage(iActivePage) : NULL;
	if (pPage == NULL || pPage->GetSafeHwnd() == NULL)
		return;

	const UINT uDialogId = GetActivePageDialogId();
	const HWND hPage = pPage->GetSafeHwnd();
	if (m_hRegisteredOptionPage != hPage) {
		m_hRegisteredOptionPage = hPage;
		m_hActiveTreeOptionToolTip = NULL;
		m_strActiveTreeOptionToolTip.Empty();
	}

	struct SSheetToolTipEntry
	{
		UINT uControlId;
		LPCTSTR pszKey;
	};
	static const SSheetToolTipEntry aSheetEntries[] = {
		{ IDOK, _T("OPTIONS_TIP_OK") },
		{ IDCANCEL, _T("OPTIONS_TIP_CANCEL") },
		{ ID_APPLY_NOW, _T("OPTIONS_TIP_APPLY") },
		{ IDC_OPTIONS_RESET, _T("OPTIONS_TIP_RESET_PAGE") },
		{ ID_HELP, _T("OPTIONS_TIP_HELP") },
		{ IDHELP, _T("OPTIONS_TIP_HELP") }
	};
	for (size_t i = 0; i < _countof(aSheetEntries); ++i) {
		CWnd* pControl = GetDlgItem(aSheetEntries[i].uControlId);
		if (pControl == NULL || pControl->GetSafeHwnd() == NULL)
			continue;
		const CString strToolTip(FormatOptionToolTipText(GetResString(aSheetEntries[i].pszKey)));
		if (!IsOptionToolTipWindowRegistered(pControl->GetSafeHwnd())) {
			if (m_tooltipOptions.AddTool(pControl, strToolTip))
				m_aOptionToolTipWindows.Add(pControl->GetSafeHwnd());
		}
		else if (bUpdateExisting)
			m_tooltipOptions.UpdateTipText(strToolTip, pControl);
	}

	for (CWnd* pControl = pPage->GetWindow(GW_CHILD); pControl != NULL; pControl = pControl->GetNextWindow()) {
		const HWND hControl = pControl->GetSafeHwnd();
		if (hControl == NULL || hControl == m_tooltipOptions.GetSafeHwnd())
			continue;
		const UINT uControlId = static_cast<UINT>(pControl->GetDlgCtrlID());
		if (HasPageLocalOptionToolTip(uDialogId, uControlId) || IsOptionToolTipExcluded(uDialogId, uControlId))
			continue;

		const CString strToolTip(BuildOptionToolTipText(pPage, pControl));
		if (strToolTip.IsEmpty())
			continue;

		if (!IsOptionToolTipWindowRegistered(hControl)) {
			if (m_tooltipOptions.AddTool(pControl, strToolTip))
				m_aOptionToolTipWindows.Add(hControl);
		}
		else if (bUpdateExisting)
			m_tooltipOptions.UpdateTipText(strToolTip, pControl);
	}
}

void CPreferencesDlg::UpdateActiveTreeOptionToolTip(MSG* pMsg)
{
	if (!m_bShowOptionsToolTips || pMsg == NULL
		|| (pMsg->message != WM_MOUSEMOVE && pMsg->message != WM_NCMOUSEMOVE && pMsg->message != WM_LBUTTONDOWN
			&& pMsg->message != WM_RBUTTONDOWN && pMsg->message != WM_MOUSEWHEEL))
		return;

	const int iActivePage = GetActiveIndex();
	CPropertyPage* pPage = iActivePage >= 0 ? GetPage(iActivePage) : NULL;
	if (pPage == NULL || pPage->GetSafeHwnd() == NULL)
		return;

	CWnd* pTreeWnd = NULL;
	const UINT aTreeIds[] = {
		IDC_EXT_OPTS,
		IDC_MISC_IRC,
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
		IDC_DEBUG_OPTS,
#endif
	};
	for (size_t i = 0; i < _countof(aTreeIds); ++i) {
		CWnd* pCandidate = pPage->GetDlgItem(aTreeIds[i]);
		if (pCandidate != NULL) {
			pTreeWnd = pCandidate;
			break;
		}
	}
	if (pTreeWnd == NULL || pTreeWnd->GetSafeHwnd() == NULL)
		return;

	CString strToolTip;
	POINT ptScreen = {};
	if (::GetCursorPos(&ptScreen)) {
		CRect rcTree;
		pTreeWnd->GetWindowRect(&rcTree);
		if (rcTree.PtInRect(ptScreen)) {
			CTreeOptionsCtrl* pTree = DYNAMIC_DOWNCAST(CTreeOptionsCtrl, pTreeWnd);
			if (pTree == NULL)
				return;
			CPoint ptClient(ptScreen);
			pTree->ScreenToClient(&ptClient);
			UINT uFlags = 0;
			HTREEITEM hItem = pTree->HitTest(ptClient, &uFlags);
			if (hItem != NULL && (uFlags & TVHT_ONITEM)) {
				CTreeOptionsItemData* pItemData = reinterpret_cast<CTreeOptionsItemData*>(pTree->GetItemData(hItem));
				if (pItemData != NULL)
					strToolTip = pItemData->m_sInfo;
				if (!strToolTip.IsEmpty())
					strToolTip = FormatOptionToolTipText(strToolTip);
			}
		}
	}

	if (strToolTip != m_strActiveTreeOptionToolTip || m_hActiveTreeOptionToolTip != pTreeWnd->GetSafeHwnd()) {
		m_strActiveTreeOptionToolTip = strToolTip;
		m_hActiveTreeOptionToolTip = pTreeWnd->GetSafeHwnd();
		m_tooltipOptions.UpdateTipText(m_strActiveTreeOptionToolTip, pTreeWnd);
		if (m_strActiveTreeOptionToolTip.IsEmpty())
			m_tooltipOptions.Pop();
	}
}

void CPreferencesDlg::SetOptionsToolTipsEnabled(bool bEnabled)
{
	m_bShowOptionsToolTips = bEnabled;
	if (m_tooltipOptions.GetSafeHwnd() != NULL) {
		m_tooltipOptions.Activate(m_bShowOptionsToolTips);
		if (!m_bShowOptionsToolTips)
			m_tooltipOptions.Pop();
	}
}

void CPreferencesDlg::RefreshActivePageToolTips()
{
	RegisterActivePageToolTips(true);
}

BOOL CPreferencesDlg::PreTranslateMessage(MSG* pMsg)
{
	const int iActivePage = GetActiveIndex();
	CPropertyPage* pPage = iActivePage >= 0 ? GetPage(iActivePage) : NULL;
	if (pPage != NULL && pPage->GetSafeHwnd() != NULL && pPage->GetSafeHwnd() != m_hRegisteredOptionPage)
		RegisterActivePageToolTips(true);
	UpdateActiveTreeOptionToolTip(pMsg);
	if (m_bShowOptionsToolTips && m_tooltipOptions.GetSafeHwnd() != NULL && pMsg != NULL)
		m_tooltipOptions.RelayEvent(pMsg);
	return CTreePropSheet::PreTranslateMessage(pMsg);
}

bool AreOptionsToolTipsEnabled(const CWnd* pWnd)
{
	const CWnd* pCurrent = pWnd;
	while (pCurrent != NULL) {
		CPreferencesDlg* pPreferencesDlg = DYNAMIC_DOWNCAST(CPreferencesDlg, const_cast<CWnd*>(pCurrent));
		if (pPreferencesDlg != NULL)
			return pPreferencesDlg->GetOptionsToolTipsEnabled();
		pCurrent = pCurrent->GetParent();
	}
	return thePrefs.GetShowOptionsToolTips();
}

void CPreferencesDlg::OnDestroy()
{
	const int iActivePage = GetActiveIndex();
	if (iActivePage >= 0 && iActivePage < GetPageCount())
		m_uPshStartPageId = GetPageDialogId(GetPage(iActivePage));

	if (m_tooltipOptions.GetSafeHwnd() != NULL)
		m_tooltipOptions.CleanupWindow();
	m_aOptionToolTipWindows.RemoveAll();
	m_hRegisteredOptionPage = NULL;
	m_hActiveTreeOptionToolTip = NULL;
	m_strActiveTreeOptionToolTip.Empty();

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
}

BOOL CPreferencesDlg::OnInitDialog()
{
	ASSERT(!m_bSaveIniFile);
	BOOL bResult = CTreePropSheet::OnInitDialog();
	ScalePreferencesButtons();
	CreateResetButton();
	InitWindowStyles(this);
	if (GetPageTreeControl() != NULL)
		GetPageTreeControl()->ModifyStyle(TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS, TVS_FULLROWSELECT);
	InitSideBanner();

	for (int i = (int)m_pages.GetCount(); --i >= 0;)
		if (GetPageDialogId(GetPage(i)) == m_uPshStartPageId) {
			SetActivePage(i);
			break;
		}

	Localize();

	if (m_tooltipOptions.Create(this)) {
		m_tooltipOptions.SetMaxTipWidth(ScaleOptionsValue(420));
		m_tooltipOptions.SetAutoTabHeaderIcon(false);
		m_tooltipOptions.Activate(m_bShowOptionsToolTips);
		RegisterActivePageToolTips();
	}

	if (GetPageTreeControl() != NULL) {
		const int iModPage = GetPageIndex(&m_wndMod);
		const HTREEITEM hModItem = iModPage >= 0 ? GetPageTreeItem(iModPage) : NULL;
		if (hModItem != NULL)
			GetPageTreeControl()->Expand(hModItem, TVE_EXPAND);
	}
	return bResult;
}

bool CPreferencesDlg::ScalePreferencesButtons()
{
	CWnd* pOK = GetDlgItem(IDOK);
	CWnd* pCancel = GetDlgItem(IDCANCEL);
	CWnd* pApply = GetDlgItem(ID_APPLY_NOW);
	CWnd* pHelp = GetDlgItem(ID_HELP);
	if (pHelp == NULL)
		pHelp = GetDlgItem(IDHELP);
	CTabCtrl* pTab = GetTabControl();
	if (pOK == NULL || pCancel == NULL || pApply == NULL || pHelp == NULL || pTab == NULL || !::IsWindow(pTab->GetSafeHwnd()))
		return false;

	CRect rectApply;
	CRect rectHelp;
	CRect rectTab;
	CRect rectClient;
	CRect rectWindow;
	pApply->GetWindowRect(&rectApply);
	pHelp->GetWindowRect(&rectHelp);
	pTab->GetWindowRect(&rectTab);
	GetWindowRect(&rectWindow);
	ScreenToClient(&rectApply);
	ScreenToClient(&rectHelp);
	ScreenToClient(&rectTab);
	GetClientRect(&rectClient);

	const int iButtonWidth = ScaleOptionsMetric(rectApply.Width());
	const int iButtonHeight = ScaleOptionsMetric(rectApply.Height());
	const int iButtonGap = ScaleOptionsMetric(max(0, rectHelp.left - rectApply.right));
	const int iRightMargin = ScaleOptionsMetric(max(0, rectClient.right - rectHelp.right));
	const int iBottomMargin = ScaleOptionsMetric(max(0, rectClient.bottom - rectHelp.bottom));
	const int iPageGap = ScaleOptionsMetric(max(0, rectApply.top - rectTab.bottom));
	const int iButtonTop = rectTab.bottom + iPageGap;
	const int iButtonStep = iButtonWidth + iButtonGap;
	const int iHelpLeft = rectClient.right - iRightMargin - iButtonWidth;
	const CRect rectScaledHelp(iHelpLeft, iButtonTop, iHelpLeft + iButtonWidth, iButtonTop + iButtonHeight);
	const CRect rectScaledApply(rectScaledHelp.left - iButtonStep, iButtonTop, rectScaledHelp.left - iButtonGap, iButtonTop + iButtonHeight);
	const CRect rectScaledCancel(rectScaledApply.left - iButtonStep, iButtonTop, rectScaledApply.left - iButtonGap, iButtonTop + iButtonHeight);
	const CRect rectScaledOK(rectScaledCancel.left - iButtonStep, iButtonTop, rectScaledCancel.left - iButtonGap, iButtonTop + iButtonHeight);
	if (rectScaledOK.left < 0)
		return false;

	const int iRequiredClientHeight = iButtonTop + iButtonHeight + iBottomMargin;
	const int iHeightChange = iRequiredClientHeight - rectClient.Height();
	if (iHeightChange != 0 && !SetWindowPos(NULL, 0, 0, rectWindow.Width(), rectWindow.Height() + iHeightChange, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
		return false;

	const UINT uFlags = SWP_NOZORDER | SWP_NOACTIVATE;
	HDWP hDeferPos = ::BeginDeferWindowPos(4);
	if (hDeferPos != NULL)
		hDeferPos = ::DeferWindowPos(hDeferPos, pOK->GetSafeHwnd(), NULL, rectScaledOK.left, rectScaledOK.top, rectScaledOK.Width(), rectScaledOK.Height(), uFlags);
	if (hDeferPos != NULL)
		hDeferPos = ::DeferWindowPos(hDeferPos, pCancel->GetSafeHwnd(), NULL, rectScaledCancel.left, rectScaledCancel.top, rectScaledCancel.Width(), rectScaledCancel.Height(), uFlags);
	if (hDeferPos != NULL)
		hDeferPos = ::DeferWindowPos(hDeferPos, pApply->GetSafeHwnd(), NULL, rectScaledApply.left, rectScaledApply.top, rectScaledApply.Width(), rectScaledApply.Height(), uFlags);
	if (hDeferPos != NULL)
		hDeferPos = ::DeferWindowPos(hDeferPos, pHelp->GetSafeHwnd(), NULL, rectScaledHelp.left, rectScaledHelp.top, rectScaledHelp.Width(), rectScaledHelp.Height(), uFlags);
	const bool bButtonsMoved = hDeferPos != NULL && ::EndDeferWindowPos(hDeferPos) != FALSE;
	if (!bButtonsMoved) {
		if (iHeightChange != 0)
			SetWindowPos(NULL, 0, 0, rectWindow.Width(), rectWindow.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		return false;
	}

	CFont* pButtonFont = pApply->GetFont();
	if (pButtonFont != NULL) {
		pOK->SetFont(pButtonFont);
		pCancel->SetFont(pButtonFont);
		pApply->SetFont(pButtonFont);
		pHelp->SetFont(pButtonFont);
	}

	CenterWindow();
	return true;
}

bool CPreferencesDlg::CreateResetButton()
{
	CWnd* pOK = GetDlgItem(IDOK);
	CWnd* pCancel = GetDlgItem(IDCANCEL);
	CWnd* pApply = GetDlgItem(ID_APPLY_NOW);
	CWnd* pHelp = GetDlgItem(ID_HELP);
	if (pHelp == NULL)
		pHelp = GetDlgItem(IDHELP);
	if (pOK == NULL || pCancel == NULL || pApply == NULL || pHelp == NULL)
		return false;

	CRect rectOK;
	CRect rectCancel;
	CRect rectApply;
	CRect rectHelp;
	pOK->GetWindowRect(&rectOK);
	pCancel->GetWindowRect(&rectCancel);
	pApply->GetWindowRect(&rectApply);
	pHelp->GetWindowRect(&rectHelp);
	ScreenToClient(&rectOK);
	ScreenToClient(&rectCancel);
	ScreenToClient(&rectApply);
	ScreenToClient(&rectHelp);

	const int iButtonStep = rectHelp.left - rectApply.left;
	if (iButtonStep <= 0 || rectOK.left < iButtonStep)
		return false;

	CRect rectMovedOK(rectOK);
	CRect rectMovedCancel(rectCancel);
	CRect rectMovedApply(rectApply);
	rectMovedOK.OffsetRect(-iButtonStep, 0);
	rectMovedCancel.OffsetRect(-iButtonStep, 0);
	rectMovedApply.OffsetRect(-iButtonStep, 0);

	if (!m_btnReset.Create(_T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, rectApply, this, IDC_OPTIONS_RESET))
		return false;
	m_btnReset.SetFont(pApply->GetFont());

	const bool bOKMoved = pOK->SetWindowPos(NULL, rectMovedOK.left, rectMovedOK.top, rectMovedOK.Width(), rectMovedOK.Height(), SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
	const bool bCancelMoved = bOKMoved && pCancel->SetWindowPos(NULL, rectMovedCancel.left, rectMovedCancel.top, rectMovedCancel.Width(), rectMovedCancel.Height(), SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
	const bool bApplyMoved = bCancelMoved && pApply->SetWindowPos(NULL, rectMovedApply.left, rectMovedApply.top, rectMovedApply.Width(), rectMovedApply.Height(), SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
	if (!bApplyMoved) {
		pOK->SetWindowPos(NULL, rectOK.left, rectOK.top, rectOK.Width(), rectOK.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		pCancel->SetWindowPos(NULL, rectCancel.left, rectCancel.top, rectCancel.Width(), rectCancel.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		pApply->SetWindowPos(NULL, rectApply.left, rectApply.top, rectApply.Width(), rectApply.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		m_btnReset.DestroyWindow();
		return false;
	}

	m_btnReset.SetWindowPos(pApply, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	pHelp->SetWindowPos(&m_btnReset, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	return true;
}

void CPreferencesDlg::ResetActivePageToDefaults()
{
	CPropertyPage* pPage = GetActivePage();
	if (pPage == NULL)
		return;

	if (CDarkMode::MessageBox(GetResString(_T("OPTIONS_RESET_CONFIRM")), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	if (pPage == &m_wndGeneral)
		m_wndGeneral.ResetToDefaults();
	else if (pPage == &m_wndDisplay)
		m_wndDisplay.ResetToDefaults();
	else if (pPage == &m_wndConnection)
		m_wndConnection.ResetToDefaults();
	else if (pPage == &m_wndNetworkInterface)
		m_wndNetworkInterface.ResetToDefaults();
	else if (pPage == &m_wndProxy)
		m_wndProxy.ResetToDefaults();
	else if (pPage == &m_wndServer)
		m_wndServer.ResetToDefaults();
	else if (pPage == &m_wndDirectories)
		m_wndDirectories.ResetToDefaults();
	else if (pPage == &m_wndFiles)
		m_wndFiles.ResetToDefaults();
	else if (pPage == &m_wndNotify)
		m_wndNotify.ResetToDefaults();
	else if (pPage == &m_wndStats)
		m_wndStats.ResetToDefaults();
	else if (pPage == &m_wndIRC)
		m_wndIRC.ResetToDefaults();
	else if (pPage == &m_wndMessages)
		m_wndMessages.ResetToDefaults();
	else if (pPage == &m_wndSecurity)
		m_wndSecurity.ResetToDefaults();
	else if (pPage == &m_wndScheduler)
		m_wndScheduler.ResetToDefaults();
	else if (pPage == &m_wndWebServer)
		m_wndWebServer.ResetToDefaults();
	else if (pPage == &m_wndTweaks)
		m_wndTweaks.ResetToDefaults();
	else if (pPage == &m_wndMod)
		m_wndMod.ResetToDefaults();
	else if (pPage == &m_wndDownloadValidator)
		m_wndDownloadValidator.ResetToDefaults();
	else if (pPage == &m_wndProtectionPanel)
		m_wndProtectionPanel.ResetToDefaults();
	else if (pPage == &m_wndBlacklistPanel)
		m_wndBlacklistPanel.ResetToDefaults();
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	else if (pPage == &m_wndDebug)
		m_wndDebug.ResetToDefaults();
#endif
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
	const int nBannerMargin = ScaleOptionsMetric(PREFS_BANNER_MARGIN);
	const int nBannerMinWidth = ScaleOptionsMetric(PREFS_BANNER_MIN_WIDTH);
	const int nBannerMaxWidth = ScaleOptionsMetric(PREFS_BANNER_MAX_WIDTH);
	const int nTargetBannerHeight = max(1, rectClient.Height() - (nBannerMargin * 2));
	const int nSuggestedWidth = pBannerWnd->GetSuggestedWidth(nTargetBannerHeight);
	const int nLayoutLimit = max(nBannerMinWidth, rectClient.Width() / PREFS_BANNER_LAYOUT_DIVISOR);
	const int nBaseBannerWidth = min(min(nBannerMaxWidth, nLayoutLimit), max(nBannerMinWidth, nSuggestedWidth));
	m_nBannerWidth = max(1, static_cast<int>(nBaseBannerWidth * PREFS_BANNER_FRAME_WIDTH_SCALE + 0.5));

	const int nDialogGrowWidth = m_nBannerWidth + nBannerMargin + nBannerMargin;
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
	const int nBannerMargin = ScaleOptionsMetric(PREFS_BANNER_MARGIN);
	const CRect rectBanner(rectClient.right - nBannerMargin - m_nBannerWidth, nBannerMargin, rectClient.right - nBannerMargin, rectClient.bottom - nBannerMargin);
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
	SetTitle(GetResString(_T("OPTIONS")));

	if (m_hWnd != NULL) {
		if (GetDlgItem(IDOK) != NULL)
			SetDlgItemText(IDOK, GetResString(_T("MB_OK")));
		if (GetDlgItem(IDCANCEL) != NULL)
			SetDlgItemText(IDCANCEL, GetResString(_T("CANCEL")));
		if (GetDlgItem(ID_APPLY_NOW) != NULL)
			SetDlgItemText(ID_APPLY_NOW, GetResString(_T("PW_APPLY")));
		if (GetDlgItem(IDC_OPTIONS_RESET) != NULL)
			SetDlgItemText(IDC_OPTIONS_RESET, GetResString(_T("PW_RESET")));
		if (GetDlgItem(ID_HELP) != NULL)
			SetDlgItemText(ID_HELP, GetResNoAmp(_T("EM_HELP")));
		if (GetDlgItem(IDHELP) != NULL)
			SetDlgItemText(IDHELP, GetResNoAmp(_T("EM_HELP")));
	}

	m_wndGeneral.Localize();
	m_wndDisplay.Localize();
	m_wndConnection.Localize();
	m_wndNetworkInterface.Localize();
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
	m_wndDownloadValidator.Localize();
	m_wndProtectionPanel.Localize();
	m_wndBlacklistPanel.Localize();
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	m_wndDebug.Localize();
#endif

	if (GetPageTreeControl()) {
		static const LPCTSTR uids[20] =
		{
			_T("CD_GENERAL"), _T("PW_DISPLAY"), _T("CONNECTION"), _T("NETBIND_NETWORK_INTERFACE"), _T("PW_PROXY"),
			_T("SERVER"), _T("PW_DIR"), _T("FILES"), _T("PW_EKDEV_OPTIONS"), _T("SF_STATISTICS"),
			_T("IRC"), _T("MESSAGESCOMMENTS"), _T("SECURITY"), _T("SCHEDULER"), _T("PW_WS"),
			_T("PW_TWEAK"), _T("PW_MOD"), _T("DOWNLOAD_VALIDATOR"), _T("PW_PROTECTION_PANEL"), _T("PW_BLACKLIST_PANEL")
		};

		int c;
		for (c = 0; c < _countof(uids); ++c)
			LocalizeItemText(c, uids[c]);
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
		GetPageTreeControl()->SetItemText(GetPageTreeItem(c), _T("Debug"));
#endif
	}

	RegisterActivePageToolTips(true);
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

void CPreferencesDlg::ClearModalReopenRequest()
{
	m_uReopenPageId = 0;
	m_bModalReopenClosePosted = false;
	m_bClosingForModalReopen = false;
}

void CPreferencesDlg::RequestModalReopen(UINT uStartPageID)
{
	m_uReopenPageId = uStartPageID;
	if (!::IsWindow(m_hWnd) || m_bModalReopenClosePosted || m_bClosingForModalReopen)
		return;

	m_bModalReopenClosePosted = PostMessage(UM_OPTIONS_CLOSE_FOR_REOPEN) != FALSE;
}

bool CPreferencesDlg::ConsumeModalReopenRequest(UINT& uStartPageID)
{
	if (!m_bClosingForModalReopen || m_uReopenPageId == 0) {
		ClearModalReopenRequest();
		return false;
	}

	uStartPageID = m_uReopenPageId;
	ClearModalReopenRequest();
	return true;
}

BOOL CPreferencesDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	const UINT uCommandId = LOWORD(wParam);
	switch (uCommandId) {
	case ID_HELP:
	case IDHELP:
		OpenPreferencesHelpPage();
		return TRUE;
	case IDC_OPTIONS_RESET:
		ResetActivePageToDefaults();
		return TRUE;
	case IDOK:
		if (!m_bClosingForModalReopen)
			ClearModalReopenRequest();
		m_bApplyButtonClicked = false;
		m_bSaveIniFile = true;
		break;
	case IDCANCEL:
		if (!m_bClosingForModalReopen)
			ClearModalReopenRequest();
		break;
	case ID_APPLY_NOW:
		m_bApplyButtonClicked = true;
		m_bSaveIniFile = true;
		if (SendMessage(PSM_APPLY) == FALSE) {
			m_bApplyButtonClicked = false;
			return TRUE;
		}
		if (m_bOptionsWindowScaleRefreshPending) {
			m_bOptionsWindowScaleRefreshPending = false;
			RequestModalReopen(IDD_PPG_DISPLAY);
		}
		return TRUE;
	}

	return __super::OnCommand(wParam, lParam);
}

void CPreferencesDlg::OnClose()
{
	if (!m_bClosingForModalReopen)
		ClearModalReopenRequest();
	__super::OnClose();
}

LRESULT CPreferencesDlg::OnCloseForModalReopen(WPARAM, LPARAM)
{
	m_bModalReopenClosePosted = false;
	if (m_uReopenPageId == 0 || !::IsWindow(m_hWnd))
		return 0;

	m_bClosingForModalReopen = true;
	PressButton(PSBTN_CANCEL);
	return 0;
}

BOOL CPreferencesDlg::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}
