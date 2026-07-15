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
#include "MenuCmds.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "SearchParamsWnd.h"
#include "SearchParams.h"
#include "Packets.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "ServerConnect.h"
#include "ServerList.h"
#include "Server.h"
#include "SafeFile.h"
#include "DownloadQueue.h"
#include "Statistics.h"
#include "emuledlg.h"
#include "opcodes.h"
#include "ED2KLink.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/search.h"
#include "SearchExpr.h"
#include "OtherFunctions.h"
#define USE_FLEX
#include "Parser.hpp"
#include "Scanner.h"
#include "HelpIDs.h"
#include "Exceptions.h"
#include "StringConversion.h"
#include "UserMsgs.h"
#include "ClientListCtrl.h"
#include "Log.h"
#include "UpDownClient.h" 
#include "ClientList.h"
#include "ChatWnd.h"
#include "TransferDlg.h"
#include "FriendList.h"
#include "Friend.h"
#include "InputBox.h"
#include "ClientDetailDialog.h"
#include "eMuleAI/DarkMode.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace
{
	const int kSearchResultsFilterDefaultLeft = -18;
	const int kSearchResultsFilterControlGap = 4;
	const int kSearchResultsFilterRightMargin = 4;
	const int kSearchResultsToolbarControlGap = 4;
	const UINT kLargeChunkedSearchDownloadSourceDeferCount = 1000;

	int GetCheckboxIdealWidth(CWnd* pCheckBox)
	{
		CString strText;
		pCheckBox->GetWindowText(strText);

		CClientDC dc(pCheckBox);
		CFont* pOldFont = NULL;
		if (pCheckBox->GetFont() != NULL)
			pOldFont = dc.SelectObject(pCheckBox->GetFont());
		const CSize sizeText = dc.GetTextExtent(strText);
		TEXTMETRIC textMetric = {};
		dc.GetTextMetrics(&textMetric);
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);

		return sizeText.cx + max(::GetSystemMetrics(SM_CXMENUCHECK), textMetric.tmHeight) + max(4, textMetric.tmAveCharWidth);
	}

	bool TryAddSearchPacketPayloadSizes(uint32 uLeftPayloadSize, ULONGLONG ullRightPayloadSize, uint32& ruCombinedPayloadSize)
	{
		if (ullRightPayloadSize > 0xFFFFFFFFui64)
			return false;
		const uint32 uRightPayloadSize = static_cast<uint32>(ullRightPayloadSize);
		if (uLeftPayloadSize > 0xFFFFFFFFu - uRightPayloadSize)
			return false;
		ruCombinedPayloadSize = uLeftPayloadSize + uRightPayloadSize;
		return true;
	}

	DWORD GetRecentSearchDownloadInputAgeMs(DWORD dwNow)
	{
		LASTINPUTINFO lastInput;
		memset(&lastInput, 0, sizeof(lastInput));
		lastInput.cbSize = sizeof(lastInput);
		return ::GetLastInputInfo(&lastInput) ? static_cast<DWORD>(dwNow - lastInput.dwTime) : static_cast<DWORD>(-1);
	}

	void GetChunkedSearchDownloadSliceLimits(DWORD &dwSliceBudgetMs, UINT &uMaxItemsPerSlice)
	{
		const DWORD dwNow = ::GetTickCount();
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER | QS_POSTMESSAGE));
		const bool bInputPending = (uQueueStatus & (QS_KEY | QS_MOUSE)) != 0;
		const bool bPaintPending = (uQueueStatus & QS_PAINT) != 0;
		const bool bDispatchPending = (uQueueStatus & (QS_TIMER | QS_POSTMESSAGE)) != 0;
		const DWORD dwInputAge = GetRecentSearchDownloadInputAgeMs(dwNow);

		if (bInputPending || dwInputAge < 250) {
			dwSliceBudgetMs = 3;
			uMaxItemsPerSlice = 64;
			return;
		}
		if (bPaintPending || bDispatchPending) {
			dwSliceBudgetMs = 5;
			uMaxItemsPerSlice = 192;
			return;
		}
		if (dwInputAge < 1000) {
			dwSliceBudgetMs = 8;
			uMaxItemsPerSlice = 512;
			return;
		}

		dwSliceBudgetMs = 16;
		uMaxItemsPerSlice = 2048;
	}

	bool IsSearchKnownTypeAlreadyOwned(CSearchFile::EKnownType eKnownType)
	{
		return eKnownType == CSearchFile::Shared || eKnownType == CSearchFile::Downloading;
	}

	enum
	{
		WM_SEARCHRESULTSWND_DEFERRED_REFRESH = WM_APP + 4061,
		WM_SEARCHRESULTSWND_CHUNKED_DOWNLOAD = WM_APP + 4062,
		WM_SEARCHRESULTSWND_CHUNKED_CLEANUP = WM_APP + 4063
	};
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern int yyparse();
extern int yyerror(LPCTSTR errstr);
extern LPCTSTR g_aszInvKadKeywordChars;

enum ESearchTimerID
{
	TimerServerTimeout = 1,
	TimerGlobalSearch,
	TimerChunkedSearchDownload,
	TimerChunkedSearchCleanup,
	TimerSearchTabActivity
};

enum ESearchResultImage
{
	sriServerActive,
	sriGlobalActive,
	sriKadActice,
	sriClient,
	sriServer,
	sriGlobal,
	sriKad
};

namespace
{

	const UINT kSearchTabActivityIntervalMs = 130;
	const UINT kSearchTabActivityFrameCount = 10;
	const int kSearchTabActivityIconSize = 16;

	Gdiplus::Color BlendSearchTabActivityColor(const Gdiplus::Color& crBase, const Gdiplus::Color& crAccent, BYTE uAccentWeight)
	{
		const BYTE uBaseWeight = static_cast<BYTE>(255 - uAccentWeight);
		return Gdiplus::Color(
			static_cast<BYTE>((static_cast<UINT>(crBase.GetA()) * uBaseWeight + static_cast<UINT>(crAccent.GetA()) * uAccentWeight) / 255),
			static_cast<BYTE>((static_cast<UINT>(crBase.GetR()) * uBaseWeight + static_cast<UINT>(crAccent.GetR()) * uAccentWeight) / 255),
			static_cast<BYTE>((static_cast<UINT>(crBase.GetG()) * uBaseWeight + static_cast<UINT>(crAccent.GetG()) * uAccentWeight) / 255),
			static_cast<BYTE>((static_cast<UINT>(crBase.GetB()) * uBaseWeight + static_cast<UINT>(crAccent.GetB()) * uAccentWeight) / 255));
	}

	Gdiplus::Color GetSearchTabActivityAccentColor(UINT uFrame)
	{
		static const Gdiplus::Color s_acrAccentPalette[kSearchTabActivityFrameCount] =
		{
			Gdiplus::Color(255, 255, 70, 96),
			Gdiplus::Color(255, 255, 126, 54),
			Gdiplus::Color(255, 255, 214, 58),
			Gdiplus::Color(255, 170, 230, 62),
			Gdiplus::Color(255, 76, 222, 118),
			Gdiplus::Color(255, 62, 222, 214),
			Gdiplus::Color(255, 64, 158, 255),
			Gdiplus::Color(255, 116, 106, 255),
			Gdiplus::Color(255, 196, 82, 255),
			Gdiplus::Color(255, 255, 78, 184)
		};
		return s_acrAccentPalette[uFrame % kSearchTabActivityFrameCount];
	}

	void DrawSearchTabActivityFrame(Gdiplus::Graphics& graphics, UINT uFrame)
	{
		const Gdiplus::Color crOutlineBase(255, 30, 32, 44);
		const Gdiplus::RectF rcArc(2.15f, 2.15f, 11.7f, 11.7f);
		const Gdiplus::REAL fSegmentAngle = 360.0f / static_cast<Gdiplus::REAL>(kSearchTabActivityFrameCount);
		const Gdiplus::REAL fSweepAngle = fSegmentAngle - 8.0f;
		const UINT uHeadSegment = uFrame % kSearchTabActivityFrameCount;

		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

		for (UINT i = 0; i < kSearchTabActivityFrameCount; ++i) {
			const UINT uPaletteIndex = (i + uFrame) % kSearchTabActivityFrameCount;
			const UINT uDelta = (i + kSearchTabActivityFrameCount - uHeadSegment) % kSearchTabActivityFrameCount;
			const Gdiplus::Color crPalette = GetSearchTabActivityAccentColor(uPaletteIndex);
			const Gdiplus::Color crSegment = BlendSearchTabActivityColor(Gdiplus::Color(255, 82, 86, 104), crPalette, uDelta == 0 ? 255 : 232);
			const Gdiplus::Color crOutline = BlendSearchTabActivityColor(crOutlineBase, crPalette, uDelta == 0 ? 172 : 132);
			const Gdiplus::REAL fStartAngle = -90.0f + (static_cast<Gdiplus::REAL>(i) * fSegmentAngle);

			Gdiplus::Pen outlinePen(crOutline, 3.45f);
			outlinePen.SetStartCap(Gdiplus::LineCapRound);
			outlinePen.SetEndCap(Gdiplus::LineCapRound);
			graphics.DrawArc(&outlinePen, rcArc, fStartAngle, fSweepAngle);

			Gdiplus::Pen fillPen(crSegment, uDelta == 0 ? 2.95f : 2.6f);
			fillPen.SetStartCap(Gdiplus::LineCapRound);
			fillPen.SetEndCap(Gdiplus::LineCapRound);
			graphics.DrawArc(&fillPen, rcArc, fStartAngle, fSweepAngle);
		}

		const Gdiplus::Color crCenter = BlendSearchTabActivityColor(Gdiplus::Color(180, 42, 36, 72), GetSearchTabActivityAccentColor(uFrame), 96);
		Gdiplus::SolidBrush centerBrush(crCenter);
		graphics.FillEllipse(&centerBrush, 5.45f, 5.45f, 5.1f, 5.1f);
		Gdiplus::SolidBrush centerHighlight(BlendSearchTabActivityColor(crCenter, Gdiplus::Color(230, 255, 255, 255), 50));
		graphics.FillEllipse(&centerHighlight, 6.25f, 6.05f, 1.9f, 1.9f);
	}

	HICON CreateSearchTabActivityFrameIcon(UINT uFrame)
	{
		BITMAPV5HEADER bi = {};
		bi.bV5Size = sizeof(bi);
		bi.bV5Width = kSearchTabActivityIconSize;
		bi.bV5Height = -kSearchTabActivityIconSize;
		bi.bV5Planes = 1;
		bi.bV5BitCount = 32;
		bi.bV5Compression = BI_BITFIELDS;
		bi.bV5RedMask = 0x00FF0000;
		bi.bV5GreenMask = 0x0000FF00;
		bi.bV5BlueMask = 0x000000FF;
		bi.bV5AlphaMask = 0xFF000000;

		void *pBits = NULL;
		HDC hScreenDC = ::GetDC(NULL);
		HBITMAP hColorBitmap = ::CreateDIBSection(hScreenDC, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &pBits, NULL, 0);
		::ReleaseDC(NULL, hScreenDC);
		if (hColorBitmap == NULL || pBits == NULL) {
			if (hColorBitmap != NULL)
				::DeleteObject(hColorBitmap);
			return NULL;
		}

		::ZeroMemory(pBits, static_cast<size_t>(kSearchTabActivityIconSize * kSearchTabActivityIconSize * 4));
		{
			Gdiplus::Bitmap bitmap(kSearchTabActivityIconSize, kSearchTabActivityIconSize, kSearchTabActivityIconSize * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pBits));
			Gdiplus::Graphics graphics(&bitmap);
			DrawSearchTabActivityFrame(graphics, uFrame);
		}

		HBITMAP hMaskBitmap = ::CreateBitmap(kSearchTabActivityIconSize, kSearchTabActivityIconSize, 1, 1, NULL);
		ICONINFO ii = {};
		ii.fIcon = TRUE;
		ii.hbmColor = hColorBitmap;
		ii.hbmMask = hMaskBitmap;
		HICON hIcon = ::CreateIconIndirect(&ii);
		::DeleteObject(hMaskBitmap);
		::DeleteObject(hColorBitmap);
		return hIcon;
	}

	int GetSearchTabStaticImage(const SSearchParams *pParams)
	{
		if (pParams == NULL)
			return sriServer;
		if (pParams->bClientSharedFiles)
			return sriClient;
		if (pParams->eType == SearchTypeKademlia)
			return sriKad;
		if (pParams->eType == SearchTypeEd2kGlobal)
			return sriGlobal;
		return sriServer;
	}

	int AddSearchTabActivityImages(CImageList& imageList)
	{
		const int iBaseImage = imageList.GetImageCount();
		ULONG_PTR gdiplusToken = 0;
		Gdiplus::GdiplusStartupInput startupInput;
		if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, NULL) != Gdiplus::Ok)
			return -1;

		for (UINT uFrame = 0; uFrame < kSearchTabActivityFrameCount; ++uFrame) {
			HICON hIcon = CreateSearchTabActivityFrameIcon(uFrame);
			if (hIcon == NULL || imageList.Add(hIcon) < 0) {
				if (hIcon != NULL)
					::DestroyIcon(hIcon);
				Gdiplus::GdiplusShutdown(gdiplusToken);
				return -1;
			}
			::DestroyIcon(hIcon);
		}

		Gdiplus::GdiplusShutdown(gdiplusToken);
		return iBaseImage;
	}

	class CScopedSearchClientRef
	{
	public:
		explicit CScopedSearchClientRef(ClientRuntimeID uRuntimeID)
			: m_pClient(uRuntimeID != 0 && theApp.clientlist != NULL ? theApp.clientlist->AcquireTrackedClientByRuntimeID(uRuntimeID) : NULL)
		{
		}

		~CScopedSearchClientRef()
		{
			Release();
		}

		CUpDownClient* Get() const
		{
			return m_pClient;
		}

		void Release()
		{
			if (m_pClient != NULL) {
				m_pClient->ReleaseRuntimeReference();
				m_pClient = NULL;
			}
		}

	private:
		CUpDownClient* m_pClient;
	};

	UINT_PTR GetSearchSelectorToolId()
	{
		return 1;
	}

	CString FormatTooltipTimeValue(time_t tValue)
	{
		if (tValue <= 0)
			return _T("?");

		const CTime absoluteTime(tValue);
		const time_t tNow = time(NULL);
		const time_t tDiff = (tNow > tValue) ? (tNow - tValue) : 0;

		CString strTime(absoluteTime.Format(_T("%x %X")));
		strTime.AppendFormat(_T(" (%s)"), (LPCTSTR)CastSecondsToHM(tDiff));
		return strTime;
	}

	CString BuildFormattedTooltip(const CString& strTitle, const CString& strDetails)
	{
		CString strTooltip;
		if (!strTitle.IsEmpty())
			strTooltip = strTitle;

		if (!strDetails.IsEmpty()) {
			if (!strTooltip.IsEmpty())
				strTooltip += _T("\n<br_head>\n");
			strTooltip += strDetails;
		}

		if (!strTooltip.IsEmpty())
			strTooltip += TOOLTIP_AUTOFORMAT_SUFFIX_CH;
		return strTooltip;
	}

	void AppendTooltipLine(CString& strTooltip, LPCTSTR pszLabel, const CString& strValue)
	{
		if (strValue.IsEmpty())
			return;
		CString strLabel(pszLabel);
		strLabel.TrimRight();
		while (!strLabel.IsEmpty() && strLabel[strLabel.GetLength() - 1] == _T(':')) {
			strLabel.Truncate(strLabel.GetLength() - 1);
			strLabel.TrimRight();
		}
		if (!strTooltip.IsEmpty())
			strTooltip += _T('\n');
		strTooltip.AppendFormat(_T("%s: %s"), (LPCTSTR)strLabel, (LPCTSTR)strValue);
	}

	void AppendTooltipRawLine(CString& strTooltip, const CString& strLine)
	{
		if (strLine.IsEmpty())
			return;
		if (!strTooltip.IsEmpty())
			strTooltip += _T('\n');
		strTooltip += strLine;
	}

	CString GetSearchMethodText(const SSearchParams* pParams)
	{
		if (pParams == NULL)
			return CString();

		if (pParams->bClientSharedFiles)
			return GetResString(_T("SHAREDFILES"));

		switch (pParams->eType) {
		case SearchTypeEd2kServer:
			return GetResString(_T("ED2KSERVER"));
		case SearchTypeEd2kGlobal:
			return GetResString(_T("GLOBALSEARCH"));
		case SearchTypeKademlia:
			return GetResString(_T("KADEMLIA"));
		default:
			return CString();
		}
	}
}


// CSearchResultsWnd dialog

IMPLEMENT_DYNCREATE(CSearchResultsWnd, CResizableFormView)

BEGIN_MESSAGE_MAP(CSearchResultsWnd, CResizableFormView)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_SDOWNLOAD, OnBnClickedDownloadSelected)
	ON_BN_CLICKED(IDC_CLEARALL, OnBnClickedClearAll)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB1, OnSelChangeTab)
	ON_NOTIFY(TCN_SELCHANGING, IDC_TAB1, OnSelChangingTab)
	ON_MESSAGE(UM_CLOSETAB, OnCloseTab)
	ON_MESSAGE(UM_DBLCLICKTAB, OnDblClickTab)
	ON_WM_DESTROY()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_WM_CLOSE()
	ON_WM_CREATE()
	ON_WM_HELPINFO()
	ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
	ON_BN_CLICKED(IDC_OPEN_PARAMS_WND, OnBnClickedOpenParamsWnd)
	ON_WM_SYSCOMMAND()
	ON_MESSAGE(UM_DELAYED_EVALUATE, OnChangeFilter)
	ON_NOTIFY(TBN_DROPDOWN, IDC_SEARCHLST_ICO, OnSearchListMenuBtnDropDown)
	ON_NOTIFY(UM_TABMOVED, IDC_TAB1, OnTabMovement)
	ON_BN_CLICKED(IDC_CHECK_COMPLETE, OnBnClickedComplete)
	ON_BN_CLICKED(IDC_CHECK_KNOWN, OnBnClickedKnown)
	ON_WM_SIZE()
	ON_MESSAGE(WM_SEARCHRESULTSWND_DEFERRED_REFRESH, OnDeferredSearchListRefresh)
	ON_MESSAGE(WM_SEARCHRESULTSWND_CHUNKED_DOWNLOAD, OnProcessChunkedSearchDownload)
	ON_MESSAGE(WM_SEARCHRESULTSWND_CHUNKED_CLEANUP, OnProcessChunkedSearchCleanup)
END_MESSAGE_MAP()

CSearchResultsWnd::CSearchResultsWnd(CWnd* /*pParent*/)
	: CResizableFormView(CSearchResultsWnd::IDD)
	, m_pwndParams()
	, m_searchpacket()
	, global_search_timer()
	, m_uTimerLocalServer()
	, m_nEd2kSearchID(0x80000000u)
	, m_nFilterColumn()
	, m_servercount()
	, m_iSentMoreReq()
	, m_bEd2kMoreResultsAvailable(false)
	, m_b64BitSearchPacket()
	, m_globsearch()
	, m_cancelled()
	, m_uMergeFromSearchID()
	, m_bMergeFromSearchIDHasBeenSet()
	, m_astrFilterTemp()
	, m_bColumnDiff(false)
	, m_bDeferredSearchListRefreshPending(false)
	, m_chunkedSearchDownloadItems()
	, m_bChunkedSearchDownloadPending(false)
	, m_bChunkedSearchDownloadPaused(false)
	, m_bChunkedSearchDownloadBypassValidator(false)
	, m_iChunkedSearchDownloadCat(0)
	, m_bChunkedSearchDownloadNeedsRefresh(false)
	, m_bChunkedSearchDownloadBulkAddActive(false)
	, m_uChunkedSearchDownloadTotal(0)
	, m_vecChunkedSearchCleanupIDs()
	, m_iNextChunkedSearchCleanupID(0)
	, m_uChunkedSearchCleanupDeleted(0)
	, m_bChunkedSearchCleanupPending(false)
	, m_bChunkedSearchCleanupActive(false)
	, m_strFullFilterExpr()
	, m_nFilterColumnLastApplied()
	, m_uTimerSearchTabActivity(0)
	, m_uSearchTabActivityFrame(0)
	, m_iSearchTabActivityImageBase(-1)
{
}

CSearchResultsWnd::~CSearchResultsWnd()
{
	ClearChunkedSearchDownloadItems();
	ClearChunkedSearchCleanup();
	StopSearchTabActivityTimer();
	m_ctlSearchListHeader.Detach();
	delete m_searchpacket;
	if (m_uTimerLocalServer)
		VERIFY(KillTimer(m_uTimerLocalServer));
}

CSearchResultsWnd::SChunkedSearchDownloadItem::SChunkedSearchDownloadItem()
	: m_bSnapshotBuilt(false)
	, m_nSearchID(0)
	, m_nServerIP(0)
	, m_nServerPort(0)
	, m_uServerAvail(0)
	, m_uKadPublishInfo(0)
	, m_bKademlia(false)
	, m_bServerUDPAnswer(false)
	, m_bPreviewPossible(false)
	, m_bMultipleAICHFound(false)
{
}

void CSearchResultsWnd::ClearChunkedSearchDownloadItems()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TimerChunkedSearchDownload);
	m_bChunkedSearchDownloadPending = false;
	m_bChunkedSearchDownloadNeedsRefresh = false;
	m_uChunkedSearchDownloadTotal = 0;
	UpdateChunkedSearchDownloadOverlay();
	if (m_bChunkedSearchDownloadBulkAddActive) {
		m_bChunkedSearchDownloadBulkAddActive = false;
		if (theApp.downloadqueue != NULL)
			theApp.downloadqueue->EndBulkAddDownloads();
	}
	while (!m_chunkedSearchDownloadItems.IsEmpty())
		delete m_chunkedSearchDownloadItems.RemoveHead();
}

void CSearchResultsWnd::ClearChunkedSearchCleanup()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TimerChunkedSearchCleanup);
	m_vecChunkedSearchCleanupIDs.clear();
	m_iNextChunkedSearchCleanupID = 0;
	m_uChunkedSearchCleanupDeleted = 0;
	m_bChunkedSearchCleanupPending = false;
	m_bChunkedSearchCleanupActive = false;
	if (::IsWindow(m_hWnd) && ::IsWindow(searchselect.GetSafeHwnd()))
		UpdateSearchTabActivityAnimation();
}

bool CSearchResultsWnd::QueueChunkedSearchCleanupTab(int iTab)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (!searchselect.GetItem(iTab, &ti) || ti.lParam == NULL)
		return false;

	const uint32 uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
	if (uSearchID == 0)
		return false;

	for (size_t i = 0; i < m_vecChunkedSearchCleanupIDs.size(); ++i)
		if (m_vecChunkedSearchCleanupIDs[i] == uSearchID)
			return true;

	m_vecChunkedSearchCleanupIDs.push_back(uSearchID);
	return true;
}

bool CSearchResultsWnd::ScheduleChunkedSearchCleanup()
{
	if (m_bChunkedSearchCleanupPending || !m_bChunkedSearchCleanupActive || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return false;

	m_bChunkedSearchCleanupPending = SetTimer(TimerChunkedSearchCleanup, 1, NULL) != 0;
	if (!m_bChunkedSearchCleanupPending)
		m_bChunkedSearchCleanupPending = PostMessage(WM_SEARCHRESULTSWND_CHUNKED_CLEANUP, 0, 0) != FALSE;
	return m_bChunkedSearchCleanupPending;
}

bool CSearchResultsWnd::StartChunkedCleanUpSearchResults(int iTab)
{
	if (m_bChunkedSearchCleanupActive)
		return true;

	ClearChunkedSearchCleanup();
	if (!QueueChunkedSearchCleanupTab(iTab) || m_vecChunkedSearchCleanupIDs.empty())
		return false;

	m_bChunkedSearchCleanupActive = true;
	EnsureSearchTabActivityTimer();
	if (!ScheduleChunkedSearchCleanup()) {
		FinishChunkedSearchCleanup();
		return false;
	}
	return true;
}

bool CSearchResultsWnd::StartChunkedCleanUpAllSearchResults()
{
	if (m_bChunkedSearchCleanupActive)
		return true;

	ClearChunkedSearchCleanup();
	for (int iTab = 0; iTab < searchselect.GetItemCount(); ++iTab)
		QueueChunkedSearchCleanupTab(iTab);

	if (m_vecChunkedSearchCleanupIDs.empty())
		return false;

	m_bChunkedSearchCleanupActive = true;
	EnsureSearchTabActivityTimer();
	if (!ScheduleChunkedSearchCleanup()) {
		FinishChunkedSearchCleanup();
		return false;
	}
	return true;
}

void CSearchResultsWnd::FinishChunkedSearchCleanup()
{
	const uint32 uDeletedCount = m_uChunkedSearchCleanupDeleted;
	ClearChunkedSearchCleanup();
	EnsureSearchTabActivityTimer();
	uDeletedCount ? AddLogLine(true, GetResString(_T("CLEAN_UP_RESULTS_REMOVED")), uDeletedCount) : AddLogLine(true, GetResString(_T("CLEAN_UP_NO_RESULTS_REMOVED")));
}

namespace
{
	void AddChunkedSearchDownloadClientSnapshot(std::vector<CSearchFile::SClient>& clients, const CSearchFile::SClient& client)
	{
		if (!IsValidSearchResultClientIPPort(client.m_nIP, client.m_nPort))
			return;
		for (size_t i = 0; i < clients.size(); ++i) {
			if (clients[i] == client)
				return;
		}
		clients.push_back(client);
	}

	void AddChunkedSearchDownloadClientSnapshots(std::vector<CSearchFile::SClient>& clients, const CSearchFile* pFile)
	{
		if (pFile == NULL)
			return;
		if (IsValidSearchResultClientIPPort(pFile->GetClientID(), pFile->GetClientPort()))
			AddChunkedSearchDownloadClientSnapshot(clients, CSearchFile::SClient(pFile->GetClientID(), pFile->GetClientPort(), pFile->GetClientServerIP(), pFile->GetClientServerPort()));
		const CSimpleArray<CSearchFile::SClient>& fileClients = pFile->GetClients();
		for (int i = 0; i < fileClients.GetSize(); ++i)
			AddChunkedSearchDownloadClientSnapshot(clients, fileClients[i]);
	}
}

bool CSearchResultsWnd::BuildChunkedSearchDownloadItem(CSearchFile *pSelectedFile, SChunkedSearchDownloadItem &item) const
{
	if (pSelectedFile == NULL || theApp.searchlist == NULL)
		return false;

	CSearchFile *pParent = pSelectedFile->GetListParent();
	if (pParent == NULL)
		pParent = pSelectedFile;
	if (pParent == NULL || pParent->GetSearchID() == 0)
		return false;

	SSearchResultId resultId;
	if (!theApp.searchlist->GetSearchResultId(pSelectedFile, resultId))
		return false;

	CSafeMemFile data;
	pParent->StoreToFile(data);
	const ULONGLONG uLength = data.GetLength();
	if (uLength == 0 || uLength > static_cast<ULONGLONG>(UINT_MAX))
		return false;

	item = SChunkedSearchDownloadItem();
	item.m_resultId = resultId;
	item.m_data.resize(static_cast<size_t>(uLength));
	memcpy(&item.m_data[0], data.GetBuffer(), static_cast<size_t>(uLength));
	item.m_strSelectedFileName = pSelectedFile->GetFileName();
	item.m_nSearchID = pParent->GetSearchID();
	item.m_nServerIP = pParent->GetClientServerIP();
	item.m_nServerPort = pParent->GetClientServerPort();
	item.m_uServerAvail = pParent->GetIntTagValue(FT_SOURCES);
	const CSimpleArray<CSearchFile::SServer> &servers = pParent->GetServers();
	for (int i = 0; i < servers.GetSize(); ++i) {
		if (servers[i].m_nIP == item.m_nServerIP && servers[i].m_nPort == item.m_nServerPort) {
			item.m_uServerAvail = servers[i].m_uAvail;
			break;
		}
	}
	item.m_uKadPublishInfo = pSelectedFile->IsKademlia() ? pSelectedFile->GetKadPublishInfo() : pParent->GetKadPublishInfo();
	AddChunkedSearchDownloadClientSnapshots(item.m_clients, pParent);
	if (pSelectedFile != pParent)
		AddChunkedSearchDownloadClientSnapshots(item.m_clients, pSelectedFile);
	// Preserve Kad origin from either the selected child or its parent so adding a merged Kad result starts source lookup.
	item.m_bKademlia = pParent->IsKademlia() || pSelectedFile->IsKademlia();
	item.m_bServerUDPAnswer = pParent->IsServerUDPAnswer();
	item.m_bPreviewPossible = pParent->IsPreviewPossible();
	item.m_bMultipleAICHFound = pParent->HasFoundMultipleAICH();
	theApp.searchlist->GetSearchResultId(pParent, item.m_originalParentId);
	item.m_bSnapshotBuilt = true;
	return true;
}

bool CSearchResultsWnd::EnsureChunkedSearchDownloadSnapshot(SChunkedSearchDownloadItem &item) const
{
	if (item.m_bSnapshotBuilt)
		return true;

	CSearchFile *pFile = GetListedSearchFileById(item.m_resultId);
	return pFile != NULL && BuildChunkedSearchDownloadItem(pFile, item);
}

CSearchFile* CSearchResultsWnd::CreateChunkedSearchDownloadFile(const SChunkedSearchDownloadItem &item) const
{
	if (item.m_data.empty() || item.m_nSearchID == 0)
		return NULL;

	CSafeMemFile data(&item.m_data[0], static_cast<UINT>(item.m_data.size()));
	CSearchFile *pFile = NULL;
	try {
		pFile = new CSearchFile(data, true, item.m_nSearchID, 0, 0, NULL, item.m_bKademlia, item.m_bServerUDPAnswer);
		pFile->SetClientServerIP(item.m_nServerIP);
		pFile->SetClientServerPort(item.m_nServerPort);
		if (item.m_nServerIP != 0 && item.m_nServerPort != 0) {
			CSearchFile::SServer server(item.m_nServerIP, item.m_nServerPort, item.m_bServerUDPAnswer);
			server.m_uAvail = item.m_uServerAvail;
			pFile->AddServer(server);
		}
		pFile->SetKadPublishInfo(item.m_uKadPublishInfo);
		for (size_t i = 0; i < item.m_clients.size(); ++i)
			pFile->AddClient(item.m_clients[i]);
		if (item.m_bMultipleAICHFound)
			pFile->SetFoundMultipleAICH();
		pFile->SetPreviewPossible(item.m_bPreviewPossible);
		pFile->SetAFileName(item.m_strSelectedFileName);
		pFile->SetStrTagValue(FT_FILENAME, item.m_strSelectedFileName);
		return pFile;
	} catch (CException *ex) {
		delete pFile;
		ex->Delete();
	} catch (...) {
		delete pFile;
		throw;
	}
	return NULL;
}

bool CSearchResultsWnd::QueueChunkedSearchDownloadItem(CSearchFile *pSelectedFile)
{
	if (pSelectedFile == NULL || theApp.searchlist == NULL)
		return false;

	SChunkedSearchDownloadItem *pItem = new SChunkedSearchDownloadItem();
	if (!theApp.searchlist->GetSearchResultId(pSelectedFile, pItem->m_resultId)) {
		delete pItem;
		return false;
	}

	m_chunkedSearchDownloadItems.AddTail(pItem);
	return true;
}

CSearchFile* CSearchResultsWnd::GetListedSearchFileById(const SSearchResultId &id) const
{
	return theApp.searchlist != NULL ? theApp.searchlist->GetSearchFileByResultId(id) : NULL;
}

bool CSearchResultsWnd::ScheduleChunkedSearchDownload()
{
	if (m_bChunkedSearchDownloadPending || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return false;

	m_bChunkedSearchDownloadPending = SetTimer(TimerChunkedSearchDownload, 1, NULL) != 0;
	if (!m_bChunkedSearchDownloadPending)
		m_bChunkedSearchDownloadPending = PostMessage(WM_SEARCHRESULTSWND_CHUNKED_DOWNLOAD, 0, 0) != FALSE;
	return m_bChunkedSearchDownloadPending;
}

void CSearchResultsWnd::RequestDeferredSearchListRefresh()
{
	if (m_bDeferredSearchListRefreshPending)
		return;

	m_bDeferredSearchListRefreshPending = true;
	if (!PostMessage(WM_SEARCHRESULTSWND_DEFERRED_REFRESH, 0, 0)) {
		m_bDeferredSearchListRefreshPending = false;
		searchlistctrl.ReloadList(true, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
	}
}

bool CSearchResultsWnd::HasActiveChunkedSearchDownload() const
{
	return m_uChunkedSearchDownloadTotal > 0 && (!m_chunkedSearchDownloadItems.IsEmpty() || m_bChunkedSearchDownloadPending || m_bChunkedSearchDownloadBulkAddActive);
}

void CSearchResultsWnd::CancelActiveChunkedSearchDownload()
{
	ClearChunkedSearchDownloadItems();
}

bool CSearchResultsWnd::GetActiveChunkedSearchDownloadProgress(CString& strTitle, CString& strBody, CString& strCancelAndExit, CString& strWaitAndExit, UINT& uDone, UINT& uTotal) const
{
	if (!HasActiveChunkedSearchDownload() || m_uChunkedSearchDownloadTotal < BULK_OPERATION_MIN_ITEMS)
		return false;

	uTotal = m_uChunkedSearchDownloadTotal;
	const UINT uRemaining = static_cast<UINT>(m_chunkedSearchDownloadItems.GetCount());
	uDone = (uTotal >= uRemaining) ? (uTotal - uRemaining) : 0;
	strTitle = GetResString(_T("BULKOP_EXIT_TITLE"));
	strBody.Format(GetResString(_T("BULKOP_EXIT_ADD_BODY")), uTotal, uDone, uTotal - uDone);
	strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_ADD_AND_EXIT"));
	strWaitAndExit = GetResString(_T("BULKOP_EXIT_FINISH_AND_EXIT"));
	return true;
}

void CSearchResultsWnd::UpdateChunkedSearchDownloadOverlay()
{
	CDownloadListCtrl *pDownloadList = NULL;
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		pDownloadList = theApp.emuledlg->transferwnd->GetDownloadList();

	if (!HasActiveChunkedSearchDownload() || m_uChunkedSearchDownloadTotal < BULK_OPERATION_MIN_ITEMS) {
		searchlistctrl.HideOperationOverlay();
		if (pDownloadList != NULL)
			pDownloadList->HideMirroredSearchDownloadOverlay();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	const UINT uRemaining = static_cast<UINT>(m_chunkedSearchDownloadItems.GetCount());
	const UINT uDone = (m_uChunkedSearchDownloadTotal >= uRemaining) ? (m_uChunkedSearchDownloadTotal - uRemaining) : 0;
	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_FINAL_RELOAD_DETAIL")), uDone, m_uChunkedSearchDownloadTotal);
	const CString strTitle = GetResString(_T("BULKOP_ADD_DOWNLOADS_TITLE"));
	searchlistctrl.UpdateOperationOverlay(strTitle, strDetail, uDone, m_uChunkedSearchDownloadTotal, true);
	if (pDownloadList != NULL)
		pDownloadList->UpdateMirroredSearchDownloadOverlay(strTitle, strDetail, uDone, m_uChunkedSearchDownloadTotal);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSearchResultsWnd::OnInitialUpdate()
{
	CResizableFormView::OnInitialUpdate();
	theApp.searchlist->SetOutputWnd(&searchlistctrl);
	m_ctlSearchListHeader.Attach(searchlistctrl.GetHeaderCtrl()->Detach());
	searchlistctrl.Init(theApp.searchlist);
	searchlistctrl.SetPrefsKey(_T("SearchListCtrl"));
	m_btnSearchListMenu.Init(true, true);
	m_btnSearchListMenu.AddBtnStyle(IDC_SEARCHLST_ICO, TBSTYLE_AUTOSIZE);
	// Vista: Remove the TBSTYLE_TRANSPARENT to avoid flickering (can be done only after the toolbar was initially created with TBSTYLE_TRANSPARENT !?)
	m_btnSearchListMenu.SetExtendedStyle(m_btnSearchListMenu.GetExtendedStyle() & ~TBSTYLE_EX_MIXEDBUTTONS);
	m_btnSearchListMenu.RecalcLayout(true);

	m_ctlFilter.OnInit(&m_ctlSearchListHeader);

	SetAllIcons();
	searchprogress.SetStep(1);
	global_search_timer = 0;
	m_globsearch = false;

	CRect rectControl;
	m_ctlFilter.GetWindowRect(rectControl);
	m_ctlFilter.MoveWindow(-18, 0, 437, 23);
	GetDlgItem(IDC_CHECK_KNOWN)->MoveWindow(-90, 0, 65, 23);
	GetDlgItem(IDC_CHECK_COMPLETE)->MoveWindow(-162, 0, 65, 23);

	searchselect.GetWindowRect(rectControl);
	searchselect.MoveWindow(16, 24, 402, 26);
	searchselect.InitToolTips();

	ShowSearchSelector(false); //set anchors for IDC_SEARCHLIST

	AddOrReplaceAnchor(this, m_btnSearchListMenu, TOP_LEFT);
	AddOrReplaceAnchor(this, IDC_FILTER, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_CHECK_KNOWN, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_CHECK_COMPLETE, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_SDOWNLOAD, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_PROGRESS1, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_CLEARALL, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_OPEN_PARAMS_WND, TOP_RIGHT);
	AddOrReplaceAnchor(this, searchselect, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_STATIC_DLTOof, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_cattabs, BOTTOM_LEFT, BOTTOM_RIGHT);
	EnsureFilterControlLayout();

	if (theApp.m_fontSymbol.m_hObject) {
		GetDlgItem(IDC_STATIC_DLTOof)->SetFont(&theApp.m_fontSymbol);
		SetDlgItemText(IDC_STATIC_DLTOof, (GetExStyle() & WS_EX_LAYOUTRTL) ? _T("3") : _T("4")); // show a right-arrow
	}

	CheckDlgButton(IDC_CHECK_COMPLETE, thePrefs.m_uCompleteCheckState);
	CheckDlgButton(IDC_CHECK_KNOWN, thePrefs.m_uSearchKnownCheckState);

	InitWindowStyles(this); //Moved down
}

void CSearchResultsWnd::EnsureFilterControlLayout()
{
	if (!::IsWindow(m_hWnd) || !::IsWindow(m_ctlFilter.GetSafeHwnd()))
		return;

	CWnd* pCompleteCheck = GetDlgItem(IDC_CHECK_COMPLETE);
	CWnd* pKnownCheck = GetDlgItem(IDC_CHECK_KNOWN);
	if (pCompleteCheck == NULL || pKnownCheck == NULL || !::IsWindow(m_btnSearchListMenu.GetSafeHwnd()))
		return;

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcFilter;
	m_ctlFilter.GetWindowRect(&rcFilter);
	ScreenToClient(&rcFilter);

	CRect rcCompleteCheck;
	pCompleteCheck->GetWindowRect(&rcCompleteCheck);
	ScreenToClient(&rcCompleteCheck);
	CRect rcKnownCheck;
	pKnownCheck->GetWindowRect(&rcKnownCheck);
	ScreenToClient(&rcKnownCheck);
	CRect rcToolbar;
	m_btnSearchListMenu.GetWindowRect(&rcToolbar);
	ScreenToClient(&rcToolbar);

	const int iMaxRight = rcClient.right - kSearchResultsFilterRightMargin;
	const int iNewLeft = max(kSearchResultsFilterDefaultLeft, rcFilter.left);
	const int iNewWidth = iMaxRight - iNewLeft;
	if (iNewWidth <= 0)
		return;

	const int iKnownRight = iNewLeft - kSearchResultsFilterControlGap;
	const int iKnownWidth = GetCheckboxIdealWidth(pKnownCheck);
	const int iKnownLeft = iKnownRight - iKnownWidth;
	const int iCompleteRightWithKnown = iKnownLeft - kSearchResultsFilterControlGap;
	const int iCompleteWidth = GetCheckboxIdealWidth(pCompleteCheck);
	const int iCompleteLeftWithKnown = iCompleteRightWithKnown - iCompleteWidth;
	const bool bSearchSelectorVisible = (searchselect.GetStyle() & WS_VISIBLE) != 0;
	const bool bShowKnown = bSearchSelectorVisible && iCompleteLeftWithKnown >= rcToolbar.right + kSearchResultsToolbarControlGap;
	const int iCompleteRight = bShowKnown ? iCompleteRightWithKnown : iKnownRight;
	const int iCompleteLeft = iCompleteRight - iCompleteWidth;
	const bool bShowComplete = bSearchSelectorVisible && iCompleteLeft >= rcToolbar.right + kSearchResultsToolbarControlGap;

	if (rcCompleteCheck.left != iCompleteLeft || rcCompleteCheck.Width() != iCompleteWidth) {
		pCompleteCheck->SetWindowPos(NULL, iCompleteLeft, rcCompleteCheck.top, iCompleteWidth, rcCompleteCheck.Height(), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		AddOrReplaceAnchor(this, IDC_CHECK_COMPLETE, TOP_RIGHT);
	}
	if (rcKnownCheck.left != iKnownLeft || rcKnownCheck.Width() != iKnownWidth) {
		pKnownCheck->SetWindowPos(NULL, iKnownLeft, rcKnownCheck.top, iKnownWidth, rcKnownCheck.Height(), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		AddOrReplaceAnchor(this, IDC_CHECK_KNOWN, TOP_RIGHT);
	}
	if (((pCompleteCheck->GetStyle() & WS_VISIBLE) != 0) != bShowComplete)
		pCompleteCheck->ShowWindow(bShowComplete ? SW_SHOW : SW_HIDE);
	if (((pKnownCheck->GetStyle() & WS_VISIBLE) != 0) != bShowKnown)
		pKnownCheck->ShowWindow(bShowKnown ? SW_SHOW : SW_HIDE);
	if (rcFilter.left != iNewLeft || rcFilter.Width() != iNewWidth)
		m_ctlFilter.SetWindowPos(NULL, iNewLeft, rcFilter.top, iNewWidth, rcFilter.Height(), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CSearchResultsWnd::OnSize(UINT nType, int cx, int cy)
{
	CResizableFormView::OnSize(nType, cx, cy);
	UNREFERENCED_PARAMETER(cx);
	UNREFERENCED_PARAMETER(cy);

	if (nType == SIZE_MINIMIZED || !::IsWindow(m_hWnd) || !::IsWindow(m_ctlFilter.GetSafeHwnd()))
		return;

	EnsureFilterControlLayout();
}

BOOL CSearchResultsWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd)
		return FALSE;

	switch (pMsg->message) {
	case WM_KEYDOWN:
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
		if (pMsg->wParam == VK_ESCAPE)
			return FALSE;
		if (pMsg->wParam == VK_F5) {
			const int iTab = searchselect.GetCurSel();
			TCITEM ti;
			ti.mask = TCIF_PARAM;
			if (iTab >= 0 && searchselect.GetItem(iTab, &ti) && ti.lParam != NULL && theApp.searchlist != NULL) {
				const uint32 uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
				if (uSearchID != 0) {
					theApp.searchlist->RecalculateSpamRatings(uSearchID, false, false, true);
					RefreshSearchTabActivityAnimation();
					return TRUE;
				}
			}
		}
		if (pMsg->wParam == VK_RETURN && thePrefs.IsDisableFindAsYouType()) {
			CEditDelayed::SFilterParam* wParam = new CEditDelayed::SFilterParam;
			wParam->bForceApply = true; // We need to force OnChangeFilter to filter+reload listbox
			wParam->uColumnIndex = 0;
			OnChangeFilter(reinterpret_cast<WPARAM>(wParam), NULL); // We dont have lParam at this point, so send dummy null
		}
	break;
	case WM_MBUTTONUP:
		CPoint point;
		::GetCursorPos(&point);
		searchlistctrl.ScreenToClient(&point);
		int it = searchlistctrl.HitTest(point);
		if (it == -1)
			return FALSE;

		searchlistctrl.SetItemState(-1, 0, LVIS_SELECTED);
		searchlistctrl.SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		searchlistctrl.SetSelectionMark(it);   // display selection mark correctly!
		searchlistctrl.SendMessage(WM_COMMAND, MP_DETAIL);
		return TRUE;
	}

	return CResizableFormView::PreTranslateMessage(pMsg);
}

void CSearchResultsWnd::DoDataExchange(CDataExchange *pDX)
{
	CResizableFormView::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SEARCHLIST, searchlistctrl);
	DDX_Control(pDX, IDC_PROGRESS1, searchprogress);
	DDX_Control(pDX, IDC_TAB1, searchselect);
	DDX_Control(pDX, IDC_CATTAB2, m_cattabs);
	DDX_Control(pDX, IDC_FILTER, m_ctlFilter);
	DDX_Control(pDX, IDC_OPEN_PARAMS_WND, m_ctlOpenParamsWnd);
	DDX_Control(pDX, IDC_SEARCHLST_ICO, m_btnSearchListMenu);
}

void CSearchResultsWnd::StartSearch(SSearchParams *pParams)
{
	StartSearchFromCommand(pParams);
}

void CSearchResultsWnd::StartSearchFromCommand(SSearchParams *pParams)
{
	if (pParams == NULL)
		return;

	switch (pParams->eType) {
	case SearchTypeAutomatic:
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
	case SearchTypeKademlia:
		StartNewSearch(pParams);
		return;

	default:
		ASSERT(0);
		delete pParams;
	}
}

void CSearchResultsWnd::StartWebSearchFromCommand(SSearchParams *pParams)
{
	if (pParams == NULL)
		return;

	DeleteAllSearches();
	bool bStarted = false;
	try {
		switch (pParams->eType) {
		case SearchTypeEd2kServer:
		case SearchTypeEd2kGlobal:
			if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnected())
				bStarted = DoNewEd2kSearch(pParams);
			break;
		case SearchTypeKademlia:
			if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
				bStarted = DoNewKadSearch(pParams);
			break;
		case SearchTypeAutomatic:
			if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnected()) {
				pParams->eType = SearchTypeEd2kServer;
				bStarted = DoNewEd2kSearch(pParams);
			} else if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected()) {
				pParams->eType = SearchTypeKademlia;
				bStarted = DoNewKadSearch(pParams);
			}
			break;
		default:
			ASSERT(0);
		}
	} catch (CMsgBoxException *ex) {
		ex->Delete();
	} catch (...) {
		ASSERT(0);
	}

	if (bStarted) {
		SearchStarted();
		return;
	}

	delete pParams;
}

void CSearchResultsWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TimerSearchTabActivity) {
		++m_uSearchTabActivityFrame;
		const bool bHasActiveTab = UpdateSearchTabActivityAnimation();
		UpdateMoreButtonState(GetActiveSearchResultsParams());
		if (!bHasActiveTab)
			StopSearchTabActivityTimer();
		return;
	}

	if (nIDEvent == TimerChunkedSearchDownload) {
		VERIFY(KillTimer(TimerChunkedSearchDownload));
		OnProcessChunkedSearchDownload(0, 0);
		return;
	}

	if (nIDEvent == TimerChunkedSearchCleanup) {
		VERIFY(KillTimer(TimerChunkedSearchCleanup));
		OnProcessChunkedSearchCleanup(0, 0);
		return;
	}

	CResizableFormView::OnTimer(nIDEvent);

	if (m_uTimerLocalServer != 0 && nIDEvent == m_uTimerLocalServer) {
		if (thePrefs.GetDebugServerSearchesLevel() > 0)
			Debug(_T("Timeout waiting on search results of local server\n"));
		// the local server did not answer within the timeout
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;

		// start the global search
		if (m_globsearch) {
			if (global_search_timer == 0)
				VERIFY((global_search_timer = SetTimer(TimerGlobalSearch, 750, NULL)) != 0);
		} else
			CancelEd2kSearch();
	} else if (nIDEvent == global_search_timer) {
		if (theApp.serverconnect->IsConnected()) {
			CServer *pConnectedServer = theApp.serverconnect->GetCurrentServer();
			if (pConnectedServer)
				pConnectedServer = theApp.serverlist->GetServerByAddress(pConnectedServer->GetAddress(), pConnectedServer->GetPort());

			CServer *toask = NULL;
			while (++m_servercount < (unsigned)theApp.serverlist->GetServerCount()) {
				searchprogress.StepIt();
				toask = theApp.serverlist->GetNextSearchServer();
				if (toask == NULL || (toask != pConnectedServer && toask->GetFailedCount() < thePrefs.GetDeadServerRetries()))
					break;
				toask = NULL;
			}

			if (toask) {
				bool bRequestSent = false;
				if (toask->SupportsLargeFilesUDP() && (toask->GetUDPFlags() & SRV_UDPFLG_EXT_GETFILES)) {
					CSafeMemFile data(50);
					uint32 nTagCount = 1;
					data.WriteUInt32(nTagCount);
					CTag tagFlags(CT_SERVER_UDPSEARCH_FLAGS, SRVCAP_UDP_NEWTAGS_LARGEFILES);
					tagFlags.WriteNewEd2kTag(data);
					uint32 uExtSearchPacketSize = 0;
					if (TryAddSearchPacketPayloadSizes(m_searchpacket->size, data.GetLength(), uExtSearchPacketSize)) {
						const uint32 uExtensionSize = static_cast<uint32>(data.GetLength());
						Packet *pExtSearchPacket = new Packet(OP_GLOBSEARCHREQ3, uExtSearchPacketSize);
						data.SeekToBegin();
						data.Read(pExtSearchPacket->pBuffer, uExtensionSize);
						memcpy(pExtSearchPacket->pBuffer + uExtensionSize, m_searchpacket->pBuffer, m_searchpacket->size);
						theStats.AddUpDataOverheadServer(pExtSearchPacket->size);
						theApp.serverconnect->SendUDPPacket(pExtSearchPacket, toask, true);
						bRequestSent = true;
						if (thePrefs.GetDebugServerUDPLevel() > 0)
							Debug(_T(">>> Sending %s  to server %-21s (%3u of %3u)\n"), _T("OP_GlobSearchReq3"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
					} else if (thePrefs.GetDebugServerUDPLevel() > 0)
						Debug(_T(">>> Skipped UDP search extension packet for server %-21s (%3u of %3u): packet size overflow\n"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());

				} else if (toask->GetUDPFlags() & SRV_UDPFLG_EXT_GETFILES) {
					if (!m_b64BitSearchPacket || toask->SupportsLargeFilesUDP()) {
						m_searchpacket->opcode = OP_GLOBSEARCHREQ2;
						if (thePrefs.GetDebugServerUDPLevel() > 0)
							Debug(_T(">>> Sending %s  to server %-21s (%3u of %3u)\n"), _T("OP_GlobSearchReq2"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
						theStats.AddUpDataOverheadServer(m_searchpacket->size);
						theApp.serverconnect->SendUDPPacket(m_searchpacket, toask, false);
						bRequestSent = true;
					} else if (thePrefs.GetDebugServerUDPLevel() > 0)
						Debug(_T(">>> Skipped UDP search on server %-21s (%3u of %3u): No large file support\n"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
				} else {
					if (!m_b64BitSearchPacket || toask->SupportsLargeFilesUDP()) {
						m_searchpacket->opcode = OP_GLOBSEARCHREQ;
						if (thePrefs.GetDebugServerUDPLevel() > 0)
							Debug(_T(">>> Sending %s  to server %-21s (%3u of %3u)\n"), _T("OP_GlobSearchReq1"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
						theStats.AddUpDataOverheadServer(m_searchpacket->size);
						theApp.serverconnect->SendUDPPacket(m_searchpacket, toask, false);
						bRequestSent = true;
					} else if (thePrefs.GetDebugServerUDPLevel() > 0)
						Debug(_T(">>> Skipped UDP search on server %-21s (%3u of %3u): No large file support\n"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
				}
				if (bRequestSent)
					theApp.searchlist->SentUDPRequestNotification(m_nEd2kSearchID, toask->GetIP());
			} else
				CancelEd2kSearch();
		} else
			CancelEd2kSearch();
	} else
		ASSERT(0);
}

void CSearchResultsWnd::SetSearchResultsIcon(uint32 uSearchID, int iImage)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID) {
			ti.mask = TCIF_IMAGE;
			ti.iImage = iImage;
			searchselect.SetItem(i, &ti);
			break;
		}
}

void CSearchResultsWnd::SetActiveSearchResultsIcon(uint32 uSearchID)
{
	const SSearchParams *pParams = GetSearchResultsParams(uSearchID);
	if (pParams) {
		int iImage;
		if (pParams->eType == SearchTypeKademlia)
			iImage = sriKadActice;
		else if (pParams->eType == SearchTypeEd2kGlobal)
			iImage = sriGlobalActive;
		else
			iImage = sriServerActive;
		SetSearchResultsIcon(uSearchID, iImage);
	}
}

void CSearchResultsWnd::SetInactiveSearchResultsIcon(uint32 uSearchID)
{
	const SSearchParams *pParams = GetSearchResultsParams(uSearchID);
	if (pParams) {
		int iImage;
		if (pParams->eType == SearchTypeKademlia)
			iImage = sriKad;
		else if (pParams->eType == SearchTypeEd2kGlobal)
			iImage = sriGlobal;
		else
			iImage = sriServer;
		SetSearchResultsIcon(uSearchID, iImage);
	}
}

SSearchParams* CSearchResultsWnd::GetSearchResultsParams(uint32 uSearchID) const
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID)
			return reinterpret_cast<SSearchParams*>(ti.lParam);
	return NULL;
}

SSearchParams* CSearchResultsWnd::GetActiveSearchResultsParams() const
{
	const int iSel = searchselect.GetCurSel();
	if (iSel < 0)
		return NULL;

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	return searchselect.GetItem(iSel, &ti) && ti.lParam != NULL ? reinterpret_cast<SSearchParams*>(ti.lParam) : NULL;
}

void CSearchResultsWnd::UpdateMoreButtonState(const SSearchParams *pParams)
{
	if (m_pwndParams == NULL || !::IsWindow(m_pwndParams->m_ctlMore.GetSafeHwnd()))
		return;

	bool bEnable = false;
	if (pParams != NULL) {
		switch (pParams->eType) {
		case SearchTypeKademlia:
			break;
		case SearchTypeEd2kServer:
		case SearchTypeEd2kGlobal:
			{
				const int iMaxMoreRequests = thePrefs.GetEd2kSearchMaxMoreRequests();
				bEnable = pParams->dwSearchID == m_nEd2kSearchID && m_bEd2kMoreResultsAvailable
					&& (iMaxMoreRequests == 0 || m_iSentMoreReq < iMaxMoreRequests)
					&& theApp.serverconnect != NULL && theApp.serverconnect->IsConnected();
			}
			break;
		default:
			break;
		}
	}
	m_pwndParams->m_ctlMore.EnableWindow(bEnable);
}

void CSearchResultsWnd::CancelSearch(uint32 uSearchID)
{
	if (uSearchID == 0) {
		int iCurSel = searchselect.GetCurSel();
		if (iCurSel >= 0) {
			TCITEM ti;
			ti.mask = TCIF_PARAM;
			if (searchselect.GetItem(iCurSel, &ti) && ti.lParam != NULL)
				uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
		}
	}
	theApp.ExecuteSearchCancelCommand(uSearchID);
}

void CSearchResultsWnd::CancelSearchFromCommand(uint32 uSearchID)
{
	if (uSearchID == 0)
		return;

	const SSearchParams *pParams = GetSearchResultsParams(uSearchID);
	if (pParams == NULL)
		return;

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
		CancelEd2kSearch();
		break;
	case SearchTypeKademlia:
		Kademlia::CSearchManager::StopSearch(pParams->dwSearchID, false);
		CancelKadSearch(pParams->dwSearchID);
	}
}
void CSearchResultsWnd::CancelEd2kSearch()
{
	SetInactiveSearchResultsIcon(m_nEd2kSearchID);

	m_cancelled = true;

	// delete any global search timer
	if (global_search_timer) {
		VERIFY(KillTimer(global_search_timer));
		global_search_timer = 0;
		searchprogress.SetPos(0);
	}
	delete m_searchpacket;
	m_searchpacket = NULL;
	m_b64BitSearchPacket = false;
	m_globsearch = false;
	m_bEd2kMoreResultsAvailable = false;

	// delete local server timeout timer
	if (m_uTimerLocalServer) {
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;
	}

	SearchCancelled(m_nEd2kSearchID);
}

void CSearchResultsWnd::CancelKadSearch(uint32 uSearchID)
{
	SearchCancelled(uSearchID);
	if (theApp.searchlist->m_bKadReloadWaiting) {
		theApp.searchlist->m_dwKadLastReloadTick = 0;
		theApp.searchlist->m_bKadReloadWaiting = false;
		searchlistctrl.ReloadList(false, LSF_SELECTION);
	}
}

void CSearchResultsWnd::SearchStarted()
{
	const CWnd *pWndFocus = GetFocus();
	m_pwndParams->m_ctlStart.EnableWindow(FALSE);
	if (pWndFocus && pWndFocus->m_hWnd == m_pwndParams->m_ctlStart.m_hWnd)
		m_pwndParams->m_ctlName.SetFocus();
	m_pwndParams->m_ctlCancel.EnableWindow(TRUE);
	EnsureSearchTabActivityTimer();
}

void CSearchResultsWnd::SearchCancelled(uint32 uSearchID)
{
	SetInactiveSearchResultsIcon(uSearchID);

	int iSel = searchselect.GetCurSel();
	if (iSel >= 0) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (searchselect.GetItem(iSel, &ti) && ti.lParam != NULL && uSearchID == reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID) {
			const CWnd *pWndFocus = GetFocus();
			m_pwndParams->m_ctlCancel.EnableWindow(FALSE);
			if (pWndFocus && pWndFocus->m_hWnd == m_pwndParams->m_ctlCancel.m_hWnd)
				m_pwndParams->m_ctlName.SetFocus();
			m_pwndParams->m_ctlStart.EnableWindow(m_pwndParams->m_ctlName.GetWindowTextLength() > 0);
		}
	}
	UpdateMoreButtonState(GetActiveSearchResultsParams());
	EnsureSearchTabActivityTimer();
}

void CSearchResultsWnd::LocalEd2kSearchEnd(UINT count, bool bMoreResultsAvailable)
{
	// local server has answered, kill the timeout timer
	if (m_uTimerLocalServer) {
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;
	}

	AddEd2kSearchResults(count);
	if (!m_cancelled) {
		if (!m_globsearch)
			SearchCancelled(m_nEd2kSearchID);
		else if (!global_search_timer)
			VERIFY((global_search_timer = SetTimer(TimerGlobalSearch, 750, NULL)) != 0);
	}
	m_bEd2kMoreResultsAvailable = bMoreResultsAvailable;
	UpdateMoreButtonState(GetActiveSearchResultsParams());
}

void CSearchResultsWnd::AddEd2kSearchResults(UINT count)
{
	const int iMaxResults = thePrefs.GetEd2kSearchMaxResults();
	if (m_cancelled || iMaxResults == 0)
		return;

	UINT uTotalResults = count;
	if (theApp.searchlist != NULL)
		uTotalResults = max(uTotalResults, theApp.searchlist->GetResultCount(m_nEd2kSearchID));

	if (uTotalResults > static_cast<UINT>(iMaxResults))
		CancelEd2kSearch();
}

void CSearchResultsWnd::OnBnClickedDownloadSelected()
{
	//start download(s)
	DownloadSelected();
}

void CSearchResultsWnd::OnDblClkSearchList(LPNMHDR, LRESULT *pResult)
{
	OnBnClickedDownloadSelected();
	*pResult = 0;
}



void CSearchResultsWnd::DownloadSelected()
{
	DownloadSelected(thePrefs.AddNewFilesPaused());
}

void CSearchResultsWnd::DownloadSelected(bool bPaused, bool bBypassDownloadValidator)
{
	// Save selected list first. Because it'll be reordered each time when thePrefs.GetGroupKnownAtTheBottom() is active which changes indexes dynamically.
	CTypedPtrList<CPtrList, CSearchFile*> selectedList;
	for (POSITION pos = searchlistctrl.GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = searchlistctrl.GetNextSelectedItem(pos);
		if (index >= 0)
			selectedList.AddTail(reinterpret_cast<CSearchFile*>(searchlistctrl.m_ListedItemsVector[index]));
	}

	if (selectedList.IsEmpty())
		return;

	ExecuteSearchDownloadCommand(selectedList, bPaused, bBypassDownloadValidator);
}

void CSearchResultsWnd::DownloadAllSearchResults(int iTab, bool bOnlyUnknown)
{
	TCITEM item = {};
	item.mask = TCIF_PARAM;
	if (iTab < 0 || !searchselect.GetItem(iTab, &item) || item.lParam == NULL)
		return;

	const uint32 nSearchID = reinterpret_cast<SSearchParams*>(item.lParam)->dwSearchID;
	CTypedPtrList<CPtrList, CSearchFile*> downloadItems;
	searchlistctrl.CollectSearchDownloadItems(nSearchID, bOnlyUnknown, downloadItems);
	if (!downloadItems.IsEmpty())
		ExecuteSearchDownloadCommand(downloadItems, thePrefs.AddNewFilesPaused(), false);
}

void CSearchResultsWnd::ExecuteSearchDownloadCommand(CTypedPtrList<CPtrList, CSearchFile*> &selectedList, bool bPaused, bool bBypassDownloadValidator)
{
	ClearChunkedSearchDownloadItems();
	if (theApp.IsClosing())
		return;

	m_bChunkedSearchDownloadPaused = bPaused;
	m_bChunkedSearchDownloadBypassValidator = bBypassDownloadValidator;
	m_iChunkedSearchDownloadCat = GetSelectedCat();
	for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
		CSearchFile *sel_file = selectedList.GetNext(pos);
		if (sel_file == NULL)
			continue;

		CSearchFile* parent = sel_file->GetListParent();
		if (parent == NULL)
			parent = sel_file;

		if (theApp.searchlist != NULL)
			theApp.searchlist->SetSearchItemKnownType(parent);
		if (IsSearchKnownTypeAlreadyOwned(parent->GetKnownType())) {
			searchlistctrl.UpdateSearch(parent);
			if (sel_file != parent)
				searchlistctrl.UpdateSearch(sel_file);
			continue;
		}
		if (theApp.downloadqueue != NULL && theApp.downloadqueue->IsFileExisting(parent->GetFileHash(), false)) {
			if (theApp.searchlist != NULL)
				theApp.searchlist->SetSearchItemKnownType(parent);
			searchlistctrl.UpdateSearch(parent);
			if (sel_file != parent)
				searchlistctrl.UpdateSearch(sel_file);
			continue;
		}

		if (parent->IsComplete() == 0 && parent->GetSourceCount() >= 50) {
			CString strMsg;
			strMsg.Format(GetResString(_T("ASKDLINCOMPLETE")), (LPCTSTR)sel_file->GetFileName());
			if (!thePrefs.GetDownloadValidatorSkipIncompleteFileConfirmation() && CDarkMode::MessageBox(strMsg, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
				continue;
		}

		QueueChunkedSearchDownloadItem(sel_file);
	}

	if (m_chunkedSearchDownloadItems.IsEmpty())
		return;

	m_uChunkedSearchDownloadTotal = static_cast<UINT>(m_chunkedSearchDownloadItems.GetCount());
	UpdateChunkedSearchDownloadOverlay();
	if (theApp.downloadqueue != NULL) {
		theApp.downloadqueue->BeginBulkAddDownloads(m_uChunkedSearchDownloadTotal >= BULK_OPERATION_MIN_ITEMS);
		m_bChunkedSearchDownloadBulkAddActive = true;
	}
	if (!ScheduleChunkedSearchDownload()) {
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked search download aborted because the first continuation message could not be posted. remaining=%d\n"), static_cast<int>(m_chunkedSearchDownloadItems.GetCount()));
		ClearChunkedSearchDownloadItems();
	}
}

LRESULT CSearchResultsWnd::OnProcessChunkedSearchDownload(WPARAM, LPARAM)
{
	m_bChunkedSearchDownloadPending = false;
	if (theApp.IsClosing() || !::IsWindow(m_hWnd)) {
		ClearChunkedSearchDownloadItems();
		return 0;
	}

	const DWORD dwSliceStartTick = ::GetTickCount();
	DWORD dwSliceBudgetMs = 8;
	UINT uMaxItemsPerSlice = 512;
	GetChunkedSearchDownloadSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
	UINT uProcessed = 0;
	while (!m_chunkedSearchDownloadItems.IsEmpty()) {
		SChunkedSearchDownloadItem *pItem = m_chunkedSearchDownloadItems.RemoveHead();
		if (pItem != NULL) {
			CSearchFile *pDownloadFile = NULL;
			if (EnsureChunkedSearchDownloadSnapshot(*pItem))
				pDownloadFile = CreateChunkedSearchDownloadFile(*pItem);
			if (pDownloadFile != NULL) {
				CSearchFile *pOriginalParent = GetListedSearchFileById(pItem->m_originalParentId);
				if (pOriginalParent != NULL)
					pDownloadFile->SetListParent(pOriginalParent);
				const bool bDeferSearchSources = m_bChunkedSearchDownloadPaused && m_uChunkedSearchDownloadTotal >= kLargeChunkedSearchDownloadSourceDeferCount;
				theApp.downloadqueue->AddSearchToDownload(pDownloadFile, m_bChunkedSearchDownloadPaused, m_iChunkedSearchDownloadCat, m_bChunkedSearchDownloadBypassValidator, bDeferSearchSources);
				if (theApp.searchlist != NULL)
					theApp.searchlist->RefreshSearchResultKnownType(pItem->m_resultId);
				m_bChunkedSearchDownloadNeedsRefresh = true;
			}
			delete pDownloadFile;
			delete pItem;
		}
		++uProcessed;

		if ((uProcessed & 0x0F) == 0)
			GetChunkedSearchDownloadSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
		const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStartTick);
		if (uProcessed >= uMaxItemsPerSlice || (uProcessed != 0 && dwElapsed >= dwSliceBudgetMs))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchResultDownload, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchResultDownload, _T("OnProcessChunkedSearchDownload"), dwSliceElapsed, uProcessed, m_chunkedSearchDownloadItems.GetCount());

	if (!m_chunkedSearchDownloadItems.IsEmpty()) {
		UpdateChunkedSearchDownloadOverlay();
		if (!ScheduleChunkedSearchDownload()) {
			const bool bNeedsRefresh = m_bChunkedSearchDownloadNeedsRefresh;
			AddDebugLogLine(DLP_HIGH, false, _T("Chunked search download aborted because the continuation message could not be posted. processed=%u remaining=%d\n"), uProcessed, static_cast<int>(m_chunkedSearchDownloadItems.GetCount()));
			ClearChunkedSearchDownloadItems();
			if (bNeedsRefresh)
				RequestDeferredSearchListRefresh();
		}
	} else {
		if (m_bChunkedSearchDownloadBulkAddActive) {
			m_bChunkedSearchDownloadBulkAddActive = false;
			if (theApp.downloadqueue != NULL)
				theApp.downloadqueue->EndBulkAddDownloads();
		}
		if (m_bChunkedSearchDownloadNeedsRefresh) {
			m_bChunkedSearchDownloadNeedsRefresh = false;
			RequestDeferredSearchListRefresh();
		}
		m_uChunkedSearchDownloadTotal = 0;
		UpdateChunkedSearchDownloadOverlay();
	}
	return 0;
}

LRESULT CSearchResultsWnd::OnDeferredSearchListRefresh(WPARAM, LPARAM)
{
	m_bDeferredSearchListRefreshPending = false;
	if (::IsWindow(searchlistctrl.m_hWnd))
		searchlistctrl.ReloadList(true, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
	return 0;
}

void CSearchResultsWnd::OnSysColorChange()
{
	CResizableFormView::OnSysColorChange();
	SetAllIcons();
	searchlistctrl.CreateMenus();
	m_ctlFilter.ShowColumnText(true); // forces the placeholder text
}

void CSearchResultsWnd::SetAllIcons()
{
	m_btnSearchListMenu.SetIcon(_T("SearchResults"));

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("SearchMethod_ServerActive")));
	iml.Add(CTempIconLoader(_T("SearchMethod_GlobalActive")));
	iml.Add(CTempIconLoader(_T("SearchMethod_KademliaActive")));
	iml.Add(CTempIconLoader(_T("StatsClients")));
	iml.Add(CTempIconLoader(_T("SearchMethod_SERVER")));
	iml.Add(CTempIconLoader(_T("SearchMethod_GLOBAL")));
	iml.Add(CTempIconLoader(_T("SearchMethod_KADEMLIA")));
	m_iSearchTabActivityImageBase = AddSearchTabActivityImages(iml);
	searchselect.SetImageList(&iml);
	m_imlSearchResults.DeleteImageList();
	m_imlSearchResults.Attach(iml.Detach());
	searchselect.SetPadding(CSize(12, 3));
	UpdateSearchTabActivityAnimation();
}

void CSearchResultsWnd::RefreshSearchTabActivityAnimation()
{
	EnsureSearchTabActivityTimer();
}

void CSearchResultsWnd::EnsureSearchTabActivityTimer()
{
	if (!::IsWindow(m_hWnd))
		return;

	if (UpdateSearchTabActivityAnimation()) {
		if (m_uTimerSearchTabActivity == 0)
			m_uTimerSearchTabActivity = SetTimer(TimerSearchTabActivity, kSearchTabActivityIntervalMs, NULL);
	} else
		StopSearchTabActivityTimer();
}

void CSearchResultsWnd::StopSearchTabActivityTimer()
{
	if (m_uTimerSearchTabActivity != 0 && ::IsWindow(m_hWnd))
		VERIFY(KillTimer(m_uTimerSearchTabActivity));
	m_uTimerSearchTabActivity = 0;
	m_uSearchTabActivityFrame = 0;

	if (!::IsWindow(searchselect.GetSafeHwnd()))
		return;

	for (int iTab = 0; iTab < searchselect.GetItemCount(); ++iTab) {
		TCITEM ti = {};
		ti.mask = TCIF_PARAM | TCIF_IMAGE;
		if (searchselect.GetItem(iTab, &ti) && ti.lParam != NULL) {
			const int iTargetImage = GetSearchTabBaseImage(reinterpret_cast<const SSearchParams*>(ti.lParam));
			if (ti.iImage != iTargetImage) {
				ti.iImage = iTargetImage;
				searchselect.SetItem(iTab, &ti);
			}
		}
	}
}

bool CSearchResultsWnd::IsSearchTabActivityActive(const SSearchParams *pParams) const
{
	if (pParams == NULL)
		return false;
	if (IsSearchCleanupActiveForSearch(pParams->dwSearchID))
		return true;

	if (theApp.searchlist != NULL) {
		if (theApp.searchlist->IsStartupLoadActiveForSearch(pParams->dwSearchID))
			return true;
		if (theApp.searchlist->HasPendingSearchProcessing(pParams->dwSearchID))
			return true;
		if (searchlistctrl.m_nResultsID == pParams->dwSearchID && searchlistctrl.m_ListedItemsVector.empty() && theApp.searchlist->GetParentItemCount(pParams->dwSearchID) > 0)
			return true;
	}

	if (pParams->bClientSharedFiles)
		return false;

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
		return pParams->dwSearchID == m_nEd2kSearchID && IsLocalEd2kSearchRunning();
	case SearchTypeEd2kGlobal:
		return pParams->dwSearchID == m_nEd2kSearchID && (IsLocalEd2kSearchRunning() || IsGlobalEd2kSearchRunning());
	case SearchTypeKademlia:
		return Kademlia::CSearchManager::IsSearching(pParams->dwSearchID);
	default:
		return false;
	}
}

bool CSearchResultsWnd::IsSearchCleanupActiveForSearch(uint32 nSearchID) const
{
	if (nSearchID == 0 || !m_bChunkedSearchCleanupActive)
		return false;

	for (INT_PTR i = m_iNextChunkedSearchCleanupID; i < static_cast<INT_PTR>(m_vecChunkedSearchCleanupIDs.size()); ++i)
		if (m_vecChunkedSearchCleanupIDs[static_cast<size_t>(i)] == nSearchID)
			return true;

	return false;
}

int CSearchResultsWnd::GetSearchTabBaseImage(const SSearchParams *pParams) const
{
	if (pParams == NULL)
		return sriServer;
	if (pParams->bClientSharedFiles)
		return sriClient;
	if (pParams->eType == SearchTypeKademlia)
		return Kademlia::CSearchManager::IsSearching(pParams->dwSearchID) ? sriKadActice : sriKad;
	if (pParams->eType == SearchTypeEd2kGlobal)
		return (pParams->dwSearchID == m_nEd2kSearchID && (IsLocalEd2kSearchRunning() || IsGlobalEd2kSearchRunning())) ? sriGlobalActive : sriGlobal;
	return (pParams->dwSearchID == m_nEd2kSearchID && IsLocalEd2kSearchRunning()) ? sriServerActive : sriServer;
}

bool CSearchResultsWnd::UpdateSearchTabActivityAnimation()
{
	if (!::IsWindow(searchselect.GetSafeHwnd()))
		return false;

	bool bHasActiveTab = false;
	for (int iTab = 0; iTab < searchselect.GetItemCount(); ++iTab) {
		TCITEM ti = {};
		ti.mask = TCIF_PARAM | TCIF_IMAGE;
		if (!searchselect.GetItem(iTab, &ti) || ti.lParam == NULL)
			continue;

		const SSearchParams *pParams = reinterpret_cast<const SSearchParams*>(ti.lParam);
		int iTargetImage = GetSearchTabBaseImage(pParams);
		if (IsSearchTabActivityActive(pParams)) {
			bHasActiveTab = true;
			if (m_iSearchTabActivityImageBase >= 0)
				iTargetImage = m_iSearchTabActivityImageBase + static_cast<int>(m_uSearchTabActivityFrame % kSearchTabActivityFrameCount);
		}

		if (ti.iImage != iTargetImage) {
			ti.iImage = iTargetImage;
			searchselect.SetItem(iTab, &ti);
		}
	}

	return bHasActiveTab;
}

void CSearchResultsWnd::Localize()
{
	searchlistctrl.Localize();
	m_ctlFilter.ShowColumnText(true);
	UpdateCatTabs();

	SetDlgItemText(IDC_CHECK_COMPLETE, GetResString(_T("COMPLETE")));
	SetDlgItemText(IDC_CHECK_KNOWN, GetResString(_T("KNOWN")));
	SetDlgItemText(IDC_CLEARALL, GetResString(_T("REMOVEALLSEARCH")));
	m_btnSearchListMenu.SetWindowText(GetResString(_T("SW_RESULT")));
	SetDlgItemText(IDC_SDOWNLOAD, GetResString(_T("SW_DOWNLOAD")));
	m_ctlOpenParamsWnd.SetWindowText(GetResString(_T("SEARCHPARAMS")) + _T("..."));
	EnsureFilterControlLayout();
}

void CSearchResultsWnd::OnBnClickedClearAll()
{
	DeleteAllSearches();
}

CString DbgGetFileMetaTagName(UINT uMetaTagID)
{
	LPCTSTR p;
	switch (uMetaTagID) {
	case FT_FILENAME:
		p = _T("@Name");
		break;
	case FT_FILESIZE:
		p = _T("@Size");
		break;
	case FT_FILESIZE_HI:
		p = _T("@SizeHI");
		break;
	case FT_FILETYPE:
		p = _T("@Type");
		break;
	case FT_FILEFORMAT:
		p = _T("@Format");
		break;
	case FT_LASTSEENCOMPLETE:
		p = _T("@LastSeenComplete");
		break;
	case FT_SOURCES:
		p = _T("@Sources");
		break;
	case FT_COMPLETE_SOURCES:
		p = _T("@Complete");
		break;
	case FT_MEDIA_ARTIST:
		p = _T("@Artist");
		break;
	case FT_MEDIA_ALBUM:
		p = _T("@Album");
		break;
	case FT_MEDIA_TITLE:
		p = _T("@Title");
		break;
	case FT_MEDIA_LENGTH:
		p = _T("@Length");
		break;
	case FT_MEDIA_BITRATE:
		p = _T("@Bitrate");
		break;
	case FT_MEDIA_CODEC:
		p = _T("@Codec");
		break;
	case FT_FILECOMMENT:
		p = _T("@Comment");
		break;
	case FT_FILERATING:
		p = _T("@Rating");
		break;
	case FT_FILEHASH:
		p = _T("@Filehash");
		break;
	default:
		{
			CString buffer;
			buffer.Format(_T("Tag0x%02X"), uMetaTagID);
			return buffer;
		}
	}
	return CString(p);
}

CString DbgGetFileMetaTagName(LPCSTR pszMetaTagID)
{
	if (strlen(pszMetaTagID) == 1)
		return DbgGetFileMetaTagName(((BYTE*)pszMetaTagID)[0]);
	CString strName;
	strName.Format(_T("\"%hs\""), pszMetaTagID);
	return strName;
}

CString DbgGetSearchOperatorName(UINT uOperator)
{
	static LPCTSTR const _aszEd2kOps[] =
	{
		_T("="),
		_T(">"),
		_T("<"),
		_T(">="),
		_T("<="),
		_T("<>"),
	};

	if (uOperator >= _countof(_aszEd2kOps)) {
		ASSERT(0);
		return _T("*UnkOp*");
	}
	return _aszEd2kOps[uOperator];
}

static CStringA s_strCurKadKeywordA;
static CSearchExpr s_SearchExpr;
CStringArray g_astrParserErrors;

static TCHAR s_chLastChar = 0;
static CString s_strSearchTree;

bool DumpSearchTree(int &iExpr, const CSearchExpr &rSearchExpr, int iLevel, bool bFlat)
{
	if (iExpr >= rSearchExpr.m_aExpr.GetCount())
		return false;
	if (!bFlat)
		s_strSearchTree.AppendFormat(_T("\n%s"), (LPCTSTR)CString(_T(' '), iLevel));
	const CSearchAttr &rSearchAttr(rSearchExpr.m_aExpr[iExpr++]);
	CStringA strTok(rSearchAttr.m_str);
	if (bFlat && s_chLastChar != _T('(') && s_chLastChar != _T('\0'))
		s_strSearchTree += _T(' ');
	if (strTok == SEARCHOPTOK_AND || strTok == SEARCHOPTOK_OR || strTok == SEARCHOPTOK_NOT) {
		s_strSearchTree.AppendFormat(_T("(%hs "), CPTRA(strTok, 1));
		s_chLastChar = _T('(');
		DumpSearchTree(iExpr, rSearchExpr, iLevel + 4, bFlat);
		DumpSearchTree(iExpr, rSearchExpr, iLevel + 4, bFlat);
		s_strSearchTree += _T(')');
		s_chLastChar = _T(')');
	} else {
		s_strSearchTree += rSearchAttr.DbgGetAttr();
		s_chLastChar = _T('\1');
	}
	return true;
}

bool DumpSearchTree(const CSearchExpr &rSearchExpr, bool bFlat)
{
	s_chLastChar = _T('\0');
	int iExpr = 0;
	int iLevel = 0;
	return DumpSearchTree(iExpr, rSearchExpr, iLevel, bFlat);
}

void ParsedSearchExpression(const CSearchExpr &expr)
{
	int iOpAnd = 0;
	int iOpOr = 0;
	int iOpNot = 0;
	int iNonDefTags = 0;
	//CStringA strDbg;
	for (INT_PTR i = 0; i < expr.m_aExpr.GetCount(); ++i) {
		const CSearchAttr &rSearchAttr(expr.m_aExpr[i]);
		const CStringA &rstr(rSearchAttr.m_str);
		if (rstr == SEARCHOPTOK_AND) {
			++iOpAnd;
		} else if (rstr == SEARCHOPTOK_OR) {
			++iOpOr;
		} else if (rstr == SEARCHOPTOK_NOT) {
			++iOpNot;
		} else {
			if (rSearchAttr.m_iTag != FT_FILENAME)
				++iNonDefTags;
		}
	}

	// this limit (+ the additional operators which will be added later) has to match the limit in 'CreateSearchExpressionTree'
	//	+1 Type (Audio, Video)
	//	+1 MinSize
	//	+1 MaxSize
	//	+1 Avail
	//	+1 Extension
	//	+1 Complete sources
	//	+1 Codec
	//	+1 Bitrate
	//	+1 Length
	//	+1 Title
	//	+1 Album
	//	+1 Artist
	// ---------------
	//  12
	if (iOpAnd + iOpOr + iOpNot > 10)
		yyerror(GetResString(_T("SEARCH_TOOCOMPLEX")));

	// FIXME: When searching on Kad the keyword may not be included into the OR operator in anyway (or into the not part of NAND)
	// Currently we do not check this properly for all cases but only for the most common ones and more important we
	// do not try to rearrange keywords, which could make a search valid
	if (!s_strCurKadKeywordA.IsEmpty() && iOpOr > 0)
		if (iOpAnd + iOpNot > 0) {
			if (expr.m_aExpr.GetCount() > 2)
				if (expr.m_aExpr[0].m_str == SEARCHOPTOK_OR && expr.m_aExpr[1].m_str == s_strCurKadKeywordA)
					yyerror(GetResString(_T("SEARCH_BADOPERATORCOMBINATION")));
		} else // if we habe only OR its not going to work out for sure
			yyerror(GetResString(_T("SEARCH_BADOPERATORCOMBINATION")));

	s_SearchExpr.m_aExpr.RemoveAll();
	// optimize search expression, if no OR nor NOT specified
	if (iOpAnd > 0 && iOpOr == 0 && iOpNot == 0 && iNonDefTags == 0) {

		// figure out if we can use a better keyword than the one the user selected
		// for example most user will search like this "The oxymoronaccelerator 2", which would ask the node which indexes "the"
		// This causes higher traffic for such nodes and makes them a viable target to attackers, while the kad result should be
		// the same or even better if we ask the node which indexes the rare keyword "oxymoronaccelerator", so we try to rearrange
		// keywords and generally assume that the longer keywords are rarer
		if (thePrefs.GetRearrangeKadSearchKeywords() && !s_strCurKadKeywordA.IsEmpty()) {
			for (INT_PTR i = 0; i < expr.m_aExpr.GetCount(); ++i) {
				const CStringA &cs(expr.m_aExpr[i].m_str);
				if (   cs != SEARCHOPTOK_AND
					&& cs != s_strCurKadKeywordA
					&& cs.FindOneOf(g_aszInvKadKeywordCharsA) < 0
					&& cs[0] != '"' // no quoted expression as a keyword
					&& cs.GetLength() >= 3
					&& s_strCurKadKeywordA.GetLength() < cs.GetLength())
				{
					s_strCurKadKeywordA = cs;
				}
			}
		}

		CStringA strAndTerms;
		for (INT_PTR i = 0; i < expr.m_aExpr.GetCount(); ++i) {
			const CStringA &cs(expr.m_aExpr[i].m_str);
			if (cs != SEARCHOPTOK_AND) {
				ASSERT(expr.m_aExpr[i].m_iTag == FT_FILENAME);
				// Minor optimization: Because we added the Kad keyword to the boolean search expression,
				// we remove it here (and only here) again because we know that the entire search expression
				// does only contain (implicit) ANDed strings.
				if (cs != s_strCurKadKeywordA) {
					if (!strAndTerms.IsEmpty())
						strAndTerms += ' ';
					strAndTerms += cs;
				}
			}
		}
		ASSERT(s_SearchExpr.m_aExpr.IsEmpty());
		s_SearchExpr.m_aExpr.Add(CSearchAttr(strAndTerms));
	} else if (expr.m_aExpr.GetCount() != 1
			|| !(expr.m_aExpr[0].m_iTag == FT_FILENAME && expr.m_aExpr[0].m_str == s_strCurKadKeywordA))
	{
		s_SearchExpr.m_aExpr.Append(expr.m_aExpr);
	}
}

class CSearchExprTarget
{
public:
	CSearchExprTarget(CSafeMemFile &data, EUTF8str eStrEncode, bool bSupports64Bit, bool *pbPacketUsing64Bit)
		: m_data(data)
		, m_pbPacketUsing64Bit(pbPacketUsing64Bit)
		, m_eStrEncode(eStrEncode)
		, m_bSupports64Bit(bSupports64Bit)
	{
		if (m_pbPacketUsing64Bit)
			*m_pbPacketUsing64Bit = false;
	}

	const CString& GetDebugString() const
	{
		return m_strDbg;
	}

	void WriteBooleanAND()
	{
		m_data.WriteUInt8(0);						// boolean operator parameter type
		m_data.WriteUInt8(0x00);					// "AND"
		m_strDbg.AppendFormat(_T("AND "));
	}

	void WriteBooleanOR()
	{
		m_data.WriteUInt8(0);						// boolean operator parameter type
		m_data.WriteUInt8(0x01);					// "OR"
		m_strDbg.AppendFormat(_T("OR "));
	}

	void WriteBooleanNOT()
	{
		m_data.WriteUInt8(0);						// boolean operator parameter type
		m_data.WriteUInt8(0x02);					// "NOT"
		m_strDbg.AppendFormat(_T("NOT "));
	}

	void WriteMetaDataSearchParam(const CString &rstrValue)
	{
		m_data.WriteUInt8(1);						// string parameter type
		m_data.WriteString(rstrValue, m_eStrEncode); // string value
		m_strDbg.AppendFormat(_T("\"%s\" "), (LPCTSTR)rstrValue);
	}

	void WriteMetaDataSearchParam(UINT uMetaTagID, const CString &rstrValue)
	{
		m_data.WriteUInt8(2);						// string parameter type
		m_data.WriteString(rstrValue, m_eStrEncode); // string value
		m_data.WriteUInt16(sizeof(uint8));			// meta tag ID length
		m_data.WriteUInt8((uint8)uMetaTagID);		// meta tag ID name
		m_strDbg.AppendFormat(_T("%s=\"%s\" "), (LPCTSTR)DbgGetFileMetaTagName(uMetaTagID), (LPCTSTR)rstrValue);
	}

	void WriteMetaDataSearchParamA(UINT uMetaTagID, const CStringA &rstrValueA)
	{
		m_data.WriteUInt8(2);						// string parameter type
		m_data.WriteString(rstrValueA);			// string value
		m_data.WriteUInt16(sizeof(uint8));			// meta tag ID length
		m_data.WriteUInt8((uint8)uMetaTagID);		// meta tag ID name
		m_strDbg.AppendFormat(_T("%s=\"%hs\" "), (LPCTSTR)DbgGetFileMetaTagName(uMetaTagID), (LPCSTR)rstrValueA);
	}

	void WriteMetaDataSearchParam(LPCSTR pszMetaTagID, const CString &rstrValue)
	{
		m_data.WriteUInt8(2);						// string parameter type
		m_data.WriteString(rstrValue, m_eStrEncode); // string value
		m_data.WriteString(pszMetaTagID);			// meta tag ID
		m_strDbg.AppendFormat(_T("%s=\"%s\" "), (LPCTSTR)DbgGetFileMetaTagName(pszMetaTagID), (LPCTSTR)rstrValue);
	}

	void WriteMetaDataSearchParam(UINT uMetaTagID, UINT uOperator, uint64 ullValue)
	{
		bool b64BitValue = ullValue > 0xFFFFFFFFui64;
		if (b64BitValue && m_bSupports64Bit) {
			if (m_pbPacketUsing64Bit)
				*m_pbPacketUsing64Bit = true;
			m_data.WriteUInt8(8);					// numeric parameter type (int64)
			m_data.WriteUInt64(ullValue);			// numeric value
		} else {
			if (b64BitValue)
				ullValue = _UI32_MAX;
			m_data.WriteUInt8(3);					// numeric parameter type (int32)
			m_data.WriteUInt32((uint32)ullValue);	// numeric value
		}
		m_data.WriteUInt8((uint8)uOperator);		// comparison operator
		m_data.WriteUInt16(sizeof(uint8));			// meta tag ID length
		m_data.WriteUInt8((uint8)uMetaTagID);		// meta tag ID name
		m_strDbg.AppendFormat(_T("%s%s%I64u "), (LPCTSTR)DbgGetFileMetaTagName(uMetaTagID), (LPCTSTR)DbgGetSearchOperatorName(uOperator), ullValue);
	}

	void WriteMetaDataSearchParam(LPCSTR pszMetaTagID, UINT uOperator, uint64 ullValue)
	{
		bool b64BitValue = ullValue > _UI32_MAX;
		if (b64BitValue && m_bSupports64Bit) {
			if (m_pbPacketUsing64Bit)
				*m_pbPacketUsing64Bit = true;
			m_data.WriteUInt8(8);					// numeric parameter type (int64)
			m_data.WriteUInt64(ullValue);			// numeric value
		} else {
			if (b64BitValue)
				ullValue = _UI32_MAX;
			m_data.WriteUInt8(3);					// numeric parameter type (int32)
			m_data.WriteUInt32((uint32)ullValue);	// numeric value
		}
		m_data.WriteUInt8((uint8)uOperator);		// comparison operator
		m_data.WriteString(pszMetaTagID);			// meta tag ID
		m_strDbg.AppendFormat(_T("%s%s%I64u "), (LPCTSTR)DbgGetFileMetaTagName(pszMetaTagID), (LPCTSTR)DbgGetSearchOperatorName(uOperator), ullValue);
	}

protected:
	CSafeMemFile &m_data;
	CString m_strDbg;
	bool *m_pbPacketUsing64Bit;
	EUTF8str m_eStrEncode;
	bool m_bSupports64Bit;
};

static CSearchExpr s_SearchExpr2;

static void AddAndAttr(UINT uTag, const CString &rstr)
{
	s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(uTag, StrToUtf8(rstr)));
	if (s_SearchExpr2.m_aExpr.GetCount() > 1)
		s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(SEARCHOPTOK_AND));
}

static void AddAndAttr(UINT uTag, UINT uOpr, uint64 ullVal)
{
	s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(uTag, uOpr, ullVal));
	if (s_SearchExpr2.m_aExpr.GetCount() > 1)
		s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(SEARCHOPTOK_AND));
}

bool GetSearchPacket(CSafeMemFile &data, SSearchParams *pParams, bool bTargetSupports64Bit, bool *pbPacketUsing64Bit)
{
	LPCTSTR pFileType;
	if (pParams->strFileType == _T(ED2KFTSTR_ARCHIVE)) {
		// eDonkeyHybrid 0.48 uses type "Pro" for archives files
		// www.filedonkey.com used type "Pro" for archives files
		pFileType = _T(ED2KFTSTR_PROGRAM);
	} else if (pParams->strFileType == _T(ED2KFTSTR_CDIMAGE)) {
		// eDonkeyHybrid 0.48 uses *no* type for iso/nrg/cue/img files
		// www.filedonkey.com used type "Pro" for CD-image files
		pFileType = _T(ED2KFTSTR_PROGRAM);
	} else {
		//TODO: Support "Doc" types
		pFileType = pParams->strFileType;
	}
	const CString &strFileType(pFileType);

	s_strCurKadKeywordA.Empty();
	ASSERT(!pParams->strExpression.IsEmpty());
	if (pParams->eType == SearchTypeKademlia) {
		ASSERT(!pParams->strKeyword.IsEmpty());
		s_strCurKadKeywordA = StrToUtf8(pParams->strKeyword);
	}
	if (pParams->strBooleanExpr.IsEmpty())
		pParams->strBooleanExpr = pParams->strExpression;
	if (pParams->strBooleanExpr.IsEmpty())
		return false;

	g_astrParserErrors.RemoveAll();
	s_SearchExpr.m_aExpr.RemoveAll();
	if (!pParams->strBooleanExpr.IsEmpty()) {
		LexInit(pParams->strBooleanExpr, true);
		int iParseResult = yyparse();
		LexFree();
		if (!g_astrParserErrors.IsEmpty()) {
			s_SearchExpr.m_aExpr.RemoveAll();
			CString strError(GetResString(_T("SEARCH_EXPRERROR")));
			strError.AppendFormat(_T("\n\n%s"), (LPCTSTR)g_astrParserErrors[g_astrParserErrors.GetCount() - 1]);
			throw new CMsgBoxException(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
		}
		if (iParseResult != 0) {
			s_SearchExpr.m_aExpr.RemoveAll();
			CString strError(GetResString(_T("SEARCH_EXPRERROR")));
			strError.AppendFormat(_T("\n\n%s"), (LPCTSTR)GetResString(_T("SEARCH_GENERALERROR")));
			throw new CMsgBoxException(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
		}

		if (pParams->eType == SearchTypeKademlia && s_strCurKadKeywordA != StrToUtf8(pParams->strKeyword)) {
			DebugLog(_T("KadSearch: Keyword was rearranged, using %s instead of %s"), (LPCTSTR)EscPercent(OptUtf8ToStr(s_strCurKadKeywordA)), (LPCTSTR)EscPercent(pParams->strKeyword));
			pParams->strKeyword = OptUtf8ToStr(s_strCurKadKeywordA);
		}
	}

	// create ed2k search expression
	CSearchExprTarget target(data, UTF8strRaw, bTargetSupports64Bit, pbPacketUsing64Bit);

	s_SearchExpr2.m_aExpr.RemoveAll();

	if (!pParams->strExtension.IsEmpty())
		AddAndAttr(FT_FILEFORMAT, pParams->strExtension);

	if (pParams->uAvailability > 0)
		AddAndAttr(FT_SOURCES, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uAvailability);

	if (pParams->ullMaxSize > 0)
		AddAndAttr(FT_FILESIZE, ED2K_SEARCH_OP_LESS_EQUAL, pParams->ullMaxSize);

	if (pParams->ullMinSize > 0)
		AddAndAttr(FT_FILESIZE, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->ullMinSize);

	if (!strFileType.IsEmpty())
		AddAndAttr(FT_FILETYPE, strFileType);

	if (pParams->uComplete > 0)
		AddAndAttr(FT_COMPLETE_SOURCES, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uComplete);

	if (pParams->uiMinBitrate > 0)
		AddAndAttr(FT_MEDIA_BITRATE, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uiMinBitrate);

	if (pParams->uiMinLength > 0)
		AddAndAttr(FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uiMinLength);

	if (!pParams->strCodec.IsEmpty())
		AddAndAttr(FT_MEDIA_CODEC, pParams->strCodec);

	if (!pParams->strTitle.IsEmpty())
		AddAndAttr(FT_MEDIA_TITLE, pParams->strTitle);

	if (!pParams->strAlbum.IsEmpty())
		AddAndAttr(FT_MEDIA_ALBUM, pParams->strAlbum);

	if (!pParams->strArtist.IsEmpty())
		AddAndAttr(FT_MEDIA_ARTIST, pParams->strArtist);

	if (!s_SearchExpr2.m_aExpr.IsEmpty()) {
		if (!s_SearchExpr.m_aExpr.IsEmpty())
			s_SearchExpr.m_aExpr.InsertAt(0, CSearchAttr(SEARCHOPTOK_AND));
		s_SearchExpr.Add(&s_SearchExpr2);
	}

	if (thePrefs.GetVerbose()) {
		s_strSearchTree.Empty();
		DumpSearchTree(s_SearchExpr, true);
		DebugLog(_T("Search Expr: %s"), (LPCTSTR)EscPercent(s_strSearchTree));
	}

	for (INT_PTR i = 0; i < s_SearchExpr.m_aExpr.GetCount(); ++i) {
		const CSearchAttr &rSearchAttr(s_SearchExpr.m_aExpr[i]);
		const CStringA &rstrA(rSearchAttr.m_str);
		if (rstrA == SEARCHOPTOK_AND)
			target.WriteBooleanAND();
		else if (rstrA == SEARCHOPTOK_OR)
			target.WriteBooleanOR();
		else if (rstrA == SEARCHOPTOK_NOT)
			target.WriteBooleanNOT();
		else
			switch (rSearchAttr.m_iTag) {
			case FT_FILESIZE:
			case FT_SOURCES:
			case FT_COMPLETE_SOURCES:
			case FT_FILERATING:
			case FT_MEDIA_BITRATE:
			case FT_MEDIA_LENGTH:
				// 11-Sep-2005 []: Kad comparison operators where changed to match the ED2K operators. For backward
				// compatibility with old Kad nodes, we map ">=val" and "<=val" to ">val-1" and "<val+1".
				// This way, the older Kad nodes will perform a ">=val" and "<=val".
				//
				// TODO: This should be removed in couple of months!
				//else
				target.WriteMetaDataSearchParam(rSearchAttr.m_iTag, rSearchAttr.m_uIntegerOperator, rSearchAttr.m_nNum);
				break;
			case FT_FILETYPE:
			case FT_FILEFORMAT:
			case FT_MEDIA_CODEC:
			case FT_MEDIA_TITLE:
			case FT_MEDIA_ALBUM:
			case FT_MEDIA_ARTIST:
				ASSERT(rSearchAttr.m_uIntegerOperator == ED2K_SEARCH_OP_EQUAL);
				target.WriteMetaDataSearchParam(rSearchAttr.m_iTag, OptUtf8ToStr(rSearchAttr.m_str));
				break;
			default:
				ASSERT(rSearchAttr.m_iTag == FT_FILENAME);
				ASSERT(rSearchAttr.m_uIntegerOperator == ED2K_SEARCH_OP_EQUAL);
				target.WriteMetaDataSearchParam(OptUtf8ToStr(rstrA));
			}
	}

	if (thePrefs.GetDebugServerSearchesLevel() > 0)
		Debug(_T("Search Data: %s\n"), (LPCTSTR)target.GetDebugString());
	s_SearchExpr.m_aExpr.RemoveAll();
	s_SearchExpr2.m_aExpr.RemoveAll();
	return true;
}

bool CSearchResultsWnd::StartNewSearch(SSearchParams *pParams)
{

	if (pParams->eType == SearchTypeAutomatic) {
		// select between kad and server
		// its easy if we are connected to one network only
		if (!theApp.serverconnect->IsConnected() && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
			pParams->eType = SearchTypeKademlia;
		else if (theApp.serverconnect->IsConnected() && (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected()))
			pParams->eType = SearchTypeEd2kServer;
		else {
			if (!theApp.serverconnect->IsConnected() && (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected())) {
				LocMessageBox(_T("NOTCONNECTEDANY"), MB_ICONWARNING, 0);
				delete pParams;
				return false;
			}
			// connected to both
			// We choose Kad, except
			// - if we are connected to a static server
			// - or a server with more than 40k and less than 2mio users connected,
			//      more than 5 mio files and if our serverlist contains less than 40 servers
			//      (otherwise we have assume that its polluted with fake servers and we might
			//      just as well to be connected to one)
			// might be further optimized in the future
			const CServer *curserv = theApp.serverconnect->GetCurrentServer();
			pParams->eType = ( theApp.serverconnect->IsConnected() && curserv != NULL
				&& (curserv->IsStaticMember()
					|| (curserv->GetUsers() > 40000
						&& theApp.serverlist->GetServerCount() < 40
						&& curserv->GetUsers() < 2000000 //was 5M - copy & paste bug
						&& curserv->GetFiles() > 5000000))
				)
				? SearchTypeEd2kServer : SearchTypeKademlia;
		}
	}

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
		if (!theApp.serverconnect->IsConnected()) {
			LocMessageBox(_T("ERR_NOTCONNECTED"), MB_ICONWARNING, 0);
			break;
		}

		try {
			if (!DoNewEd2kSearch(pParams))
				break;
		} catch (CMsgBoxException *ex) {
			CDarkMode::MessageBox(ex->m_strMsg, ex->m_uType, ex->m_uHelpID);
			ex->Delete();
			break;
		}

		SearchStarted();
		return true;
	case SearchTypeKademlia:
		if (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected()) {
			LocMessageBox(_T("ERR_NOTCONNECTEDKAD"), MB_ICONWARNING, 0);
			break;
		}

		try {
			if (!DoNewKadSearch(pParams))
				break;
		} catch (CMsgBoxException *ex) {
			CDarkMode::MessageBox(ex->m_strMsg, ex->m_uType, ex->m_uHelpID);
			ex->Delete();
			break;
		}

		SearchStarted();
		return true;
	default:
		ASSERT(0);
	}

	delete pParams;
	return false;
}

bool CSearchResultsWnd::DoNewEd2kSearch(SSearchParams *pParams)
{
	if (!theApp.serverconnect->IsConnected())
		return false;

	delete m_searchpacket;
	m_searchpacket = NULL;
	bool bServerSupports64Bit = theApp.serverconnect->GetCurrentServer() != NULL
		&& (theApp.serverconnect->GetCurrentServer()->GetTCPFlags() & SRV_TCPFLG_LARGEFILES);
	bool bPacketUsing64Bit = false;
	CSafeMemFile data(100);
	if (!GetSearchPacket(data, pParams, bServerSupports64Bit, &bPacketUsing64Bit) || data.GetLength() == 0)
		return false;

	CancelEd2kSearch();

	CString strResultType(pParams->strFileType);
	if (strResultType == _T(ED2KFTSTR_PROGRAM))
		strResultType.Empty();

	if (const CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer()) {
		pParams->strSearchServerName = pCurrentServer->GetListName();
		pParams->dwSearchServerIP = pCurrentServer->GetIP();
		pParams->nSearchServerPort = pCurrentServer->GetPort();
	}

	pParams->dwSearchID = GetNextSearchID();
	theApp.searchlist->NewSearch(&searchlistctrl, strResultType, pParams);
	m_cancelled = false;

	if (m_uTimerLocalServer) {
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;
	}

	// sending a new search request invalidates any previously received 'More'
	const CWnd *pWndFocus = GetFocus();
	m_pwndParams->m_ctlMore.EnableWindow(FALSE);
	m_bEd2kMoreResultsAvailable = false;
	if (pWndFocus && pWndFocus->m_hWnd == m_pwndParams->m_ctlMore.m_hWnd)
		m_pwndParams->m_ctlCancel.SetFocus();
	m_iSentMoreReq = 0;

	Packet *packet = new Packet(data);
	packet->opcode = OP_SEARCHREQUEST;
	if (thePrefs.GetDebugServerTCPLevel() > 0)
		Debug(_T(">>> Sending OP_SearchRequest\n"));
	theStats.AddUpDataOverheadServer(packet->size);
	m_globsearch = pParams->eType == SearchTypeEd2kGlobal && theApp.serverconnect->IsUDPSocketAvailable();
	if (m_globsearch)
		m_searchpacket = new Packet(*packet);
	theApp.serverconnect->SendPacket(packet);

	if (m_globsearch) {
		// set timeout timer for local server
		m_uTimerLocalServer = SetTimer(TimerServerTimeout, SEC2MS(50), NULL);

		if (thePrefs.GetUseServerPriorities())
			theApp.serverlist->ResetSearchServerPos();

		m_searchpacket->opcode = OP_GLOBSEARCHREQ; // will be changed later when actually sending the packet!!
		m_b64BitSearchPacket = bPacketUsing64Bit;
		m_servercount = 0;
		searchprogress.SetRange32(0, (int)theApp.serverlist->GetServerCount() - 1);
	}
	CreateNewTab(pParams);
	return true;
}

bool CSearchResultsWnd::SearchMore()
{
	SSearchParams *pParams = GetActiveSearchResultsParams();
	if (pParams == NULL)
		return false;

	if ((pParams->eType != SearchTypeEd2kServer && pParams->eType != SearchTypeEd2kGlobal) || pParams->dwSearchID != m_nEd2kSearchID || theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected()) {
		UpdateMoreButtonState(pParams);
		return false;
	}

	const int iMaxMoreRequests = thePrefs.GetEd2kSearchMaxMoreRequests();
	if (iMaxMoreRequests != 0 && m_iSentMoreReq >= iMaxMoreRequests) {
		m_bEd2kMoreResultsAvailable = false;
		UpdateMoreButtonState(pParams);
		return false;
	}

	SetActiveSearchResultsIcon(m_nEd2kSearchID);
	m_cancelled = false;
	EnsureSearchTabActivityTimer();

	Packet *packet = new Packet();
	packet->opcode = OP_QUERY_MORE_RESULT;
	if (thePrefs.GetDebugServerTCPLevel() > 0)
		Debug(_T(">>> Sending OP_QueryMoreResults\n"));
	theStats.AddUpDataOverheadServer(packet->size);
	theApp.serverconnect->SendPacket(packet);
	++m_iSentMoreReq;
	m_bEd2kMoreResultsAvailable = false;
	UpdateMoreButtonState(pParams);
	return true;
}

bool CSearchResultsWnd::DoNewKadSearch(SSearchParams *pParams)
{
	if (!Kademlia::CKademlia::IsConnected())
		return false;

	int iPos = 0;
	pParams->strKeyword = pParams->strExpression.Tokenize(_T(" "), iPos);
	if (pParams->strKeyword[0] == _T('"')) {
		// remove leading and possibly trailing quotes, if they terminate properly (otherwise the keyword is later handled as invalid)
		// (quotes are still kept in search expr and matched against the result, so everything is fine)
		const int iLen = pParams->strKeyword.GetLength();
		if (iLen > 1 && pParams->strKeyword[iLen - 1] == _T('"'))
			pParams->strKeyword = pParams->strKeyword.Mid(1, iLen - 2);
		else if (pParams->strExpression.Find(_T('"'), 1) > iLen)
			pParams->strKeyword = pParams->strKeyword.Mid(1, iLen - 1);
	}
	pParams->strKeyword.Trim();

	CSafeMemFile data(100);
	if (!GetSearchPacket(data, pParams, true, NULL)/* || (!pParams->strBooleanExpr.IsEmpty() && data.GetLength() == 0)*/)
		return false;

	if (pParams->strKeyword.IsEmpty() || pParams->strKeyword.FindOneOf(g_aszInvKadKeywordChars) >= 0) {
		CString strError;
		strError.Format(GetResString(_T("KAD_SEARCH_KEYWORD_INVALID")), g_aszInvKadKeywordChars);
		throw new CMsgBoxException(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
	}

	LPBYTE pSearchTermsData = NULL;
	UINT uSearchTermsSize = (UINT)data.GetLength();
	if (uSearchTermsSize) {
		pSearchTermsData = new BYTE[uSearchTermsSize];
		data.SeekToBegin();
		data.Read(pSearchTermsData, uSearchTermsSize);
	}

	Kademlia::CSearch *pSearch = NULL;
	try {
		pSearch = Kademlia::CSearchManager::PrepareFindKeywords(pParams->strKeyword, uSearchTermsSize, pSearchTermsData);
		delete[] pSearchTermsData;
		pSearchTermsData = NULL;
		if (!pSearch) {
			ASSERT(0);
			return false;
		}
	} catch (const CString &strException) {
		delete[] pSearchTermsData;
		throw new CMsgBoxException(strException, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
	}
	pParams->dwSearchID = pSearch->GetSearchID();
	CString strResultType(pParams->strFileType);
	if (strResultType == ED2KFTSTR_PROGRAM)
		strResultType.Empty();
	theApp.searchlist->NewSearch(&searchlistctrl, strResultType, pParams);
	CreateNewTab(pParams);
	return true;
}

bool CSearchResultsWnd::CreateNewTab(SSearchParams *pParams, bool bActiveIcon, bool bShowResults)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == pParams->dwSearchID)
			return false;

	// add a new tab
	if (pParams->strExpression.IsEmpty())
		pParams->strExpression += _T('-');
	ti.mask = TCIF_PARAM | TCIF_TEXT | TCIF_IMAGE;
	ti.lParam = (LPARAM)pParams;
	pParams->strSearchTitle = (pParams->strSpecialTitle.IsEmpty() ? (pParams->strBooleanExpr.IsEmpty() ? pParams->strExpression : pParams->strBooleanExpr) : pParams->strSpecialTitle);
	CString strTcLabel(pParams->strSearchTitle);
	DupAmpersand(strTcLabel);
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTcLabel);
	ti.cchTextMax = 0;
	if (pParams->bClientSharedFiles)
		ti.iImage = sriClient;
	else if (pParams->eType == SearchTypeKademlia)
		ti.iImage = bActiveIcon ? sriKadActice : sriKad;
	else if (pParams->eType == SearchTypeEd2kGlobal)
		ti.iImage = bActiveIcon ? sriGlobalActive : sriGlobal;
	else {
		ASSERT(pParams->eType == SearchTypeEd2kServer);
		ti.iImage = bActiveIcon ? sriServerActive : sriServer;
	}
	searchselect.m_bShowCloseButton = thePrefs.GetShowCloseButtonOnSearchTabs();
	int itemnr = searchselect.InsertItem(INT_MAX, &ti);
	if (itemnr < 0)
		return false;
	if (!searchselect.IsWindowVisible())
		ShowSearchSelector(true);
	LRESULT lResult;
	OnSelChangingTab(NULL, &lResult);
	searchselect.SetCurSel(itemnr);
	searchselect.UpdateTabToolTips(itemnr);
	if (bShowResults)
		searchlistctrl.ReloadList(false, LSF_NONE);
	UpdateMoreButtonState(pParams);
	EnsureSearchTabActivityTimer();
	return true;
}

void CSearchResultsWnd::DeleteSelectedSearch()
{
	if (CanDeleteSearches()) {
		int iFocus = searchselect.GetCurFocus();
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (iFocus >= 0 && searchselect.GetItem(iFocus, &ti) && ti.lParam != NULL)
			DeleteSearch(reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID);
}

}

#pragma warning(push)
#pragma warning(disable:4701) // potentially uninitialized local variable 'item' used
void CSearchResultsWnd::DeleteSearch(uint32 uSearchID)
{
	ClearChunkedSearchCleanup();
	Kademlia::CSearchManager::StopSearch(uSearchID, false);

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	int i = searchselect.GetItemCount();
	while (--i >= 0 && !(searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID));
	if (i < 0)
		return;

	if (uSearchID == theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID) {
		// This is current tab, so we need to clear the results list
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_ListedItemsVector.clear();
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_ListedItemsMap.RemoveAll();
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.SetItemCountEx(static_cast<int>(0), LVSICF_NOINVALIDATEALL);
	}

	// delete search results
	if (uSearchID == m_nEd2kSearchID) {
		if (!m_cancelled)
			CancelEd2kSearch();
		m_pwndParams->m_ctlMore.EnableWindow(FALSE);
	}
	theApp.searchlist->RemoveResults(uSearchID);


	// delete search tab
	int iCurSel = searchselect.GetCurSel();
	searchselect.DeleteItem(i);
	searchselect.UpdateTabToolTips();
	delete reinterpret_cast<SSearchParams*>(ti.lParam);

	int iTabItems = searchselect.GetItemCount();
	if (iTabItems > 0) {
		// select next search tab
		if (iCurSel == CB_ERR)
			iCurSel = 0;
		else if (iCurSel >= iTabItems)
			iCurSel = iTabItems - 1;
		(void)searchselect.SetCurSel(iCurSel);	// returns CB_ERR if error or no prev. selection(!)
		iCurSel = searchselect.GetCurSel();		// get the real current selection
		if (iCurSel == CB_ERR)					// if still error
			iCurSel = searchselect.SetCurSel(0);
		if (iCurSel != CB_ERR) {
			ti.mask = TCIF_PARAM;
			if (searchselect.GetItem(iCurSel, &ti) && ti.lParam != NULL) {
				searchselect.HighlightItem(iCurSel, FALSE);
				ShowResults(reinterpret_cast<SSearchParams*>(ti.lParam));
			}
		}
	} else
		NoTabItems();
	EnsureSearchTabActivityTimer();
}
#pragma warning(pop)


void CSearchResultsWnd::DeleteAllSearches()
{
	ClearChunkedSearchCleanup();
	CancelEd2kSearch();

	CTypedPtrList<CPtrList, SSearchParams*> listSearchParamsToDelete;
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL) {
			SSearchParams *params = reinterpret_cast<SSearchParams*>(ti.lParam);
			Kademlia::CSearchManager::StopSearch(params->dwSearchID, false);
			listSearchParamsToDelete.AddTail(params);
		}
	Kademlia::CSearchManager::StopAllKeywordSearches();
	NoTabItems();

	while (!listSearchParamsToDelete.IsEmpty())
		delete listSearchParamsToDelete.RemoveHead();
	StopSearchTabActivityTimer();
}

void CSearchResultsWnd::NoTabItems()
{
	searchlistctrl.m_ListedItemsVector.clear();
	searchlistctrl.m_ListedItemsMap.RemoveAll();
	searchlistctrl.SetItemCountEx(static_cast<int>(0), LVSICF_NOINVALIDATEALL);

	theApp.searchlist->Clear();
	ShowSearchSelector(false);
	searchselect.DeleteAllItems();
	searchselect.UpdateTabToolTips();
	searchlistctrl.NoTabs();

	const CWnd *pWndFocus = GetFocus();
	m_pwndParams->m_ctlMore.EnableWindow(FALSE);
	m_pwndParams->m_ctlCancel.EnableWindow(FALSE);
	m_pwndParams->m_ctlStart.EnableWindow(m_pwndParams->m_ctlName.GetWindowTextLength() > 0);
	if (pWndFocus) {
		if (pWndFocus->m_hWnd == m_pwndParams->m_ctlMore.m_hWnd || pWndFocus->m_hWnd == m_pwndParams->m_ctlCancel.m_hWnd) {
			if (m_pwndParams->m_ctlStart.IsWindowEnabled())
				m_pwndParams->m_ctlStart.SetFocus();
			else
				m_pwndParams->m_ctlName.SetFocus();
		} else if (pWndFocus->m_hWnd == m_pwndParams->m_ctlStart.m_hWnd && !m_pwndParams->m_ctlStart.IsWindowEnabled())
			m_pwndParams->m_ctlName.SetFocus();
	}
	StopSearchTabActivityTimer();
}

void CSearchResultsWnd::EnsureActiveTabLoaded()
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || !::IsWindow(searchselect.GetSafeHwnd()) || !::IsWindow(searchlistctrl.GetSafeHwnd()))
		return;

	const int iCurSel = searchselect.GetCurSel();
	if (iCurSel < 0)
		return;

	TCITEM ti = {};
	ti.mask = TCIF_PARAM;
	if (!searchselect.GetItem(iCurSel, &ti) || ti.lParam == NULL)
		return;

	const SSearchParams *pParams = reinterpret_cast<const SSearchParams*>(ti.lParam);
	if (pParams == NULL || pParams->dwSearchID == 0)
		return;

	const int iSearchListItems = searchlistctrl.GetVirtualItemCount();
	const int iControlItems = searchlistctrl.GetItemCount();
	bool bReloadNeeded = searchlistctrl.m_nResultsID != pParams->dwSearchID || iControlItems != iSearchListItems;
	if (!bReloadNeeded && !searchlistctrl.IsListedModelCurrent(pParams->dwSearchID))
		bReloadNeeded = true;
	if (!bReloadNeeded && iSearchListItems == 0 && theApp.searchlist != NULL && theApp.searchlist->GetParentItemCount(pParams->dwSearchID) > 0)
		bReloadNeeded = true;
	if (!bReloadNeeded) {
		searchlistctrl.Invalidate(FALSE);
		return;
	}

	const EListStateField eReloadState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	if (theApp.emuledlg != NULL && theApp.emuledlg->activewnd == theApp.emuledlg->searchwnd && !theApp.emuledlg->IsStartupLoadingDialogVisible() && searchlistctrl.IsWindowVisible())
		searchlistctrl.ReloadList(false, eReloadState);
	else
		searchlistctrl.QueueDeferredReload(false, eReloadState, 1);
}

void CSearchResultsWnd::ShowResults(const SSearchParams *pParams)
{
	// restoring the params works and is nice during development/testing but pretty annoying in practice.
	// TODO: maybe it should be done explicitly via a context menu function or such.
	if (GetKeyState(VK_CONTROL) < 0)
		m_pwndParams->SetParameters(pParams);

	if (pParams->eType == SearchTypeEd2kServer)
		m_pwndParams->m_ctlCancel.EnableWindow(pParams->dwSearchID == m_nEd2kSearchID && IsLocalEd2kSearchRunning());
	else if (pParams->eType == SearchTypeEd2kGlobal)
		m_pwndParams->m_ctlCancel.EnableWindow(pParams->dwSearchID == m_nEd2kSearchID && (IsLocalEd2kSearchRunning() || IsGlobalEd2kSearchRunning()));
	else if (pParams->eType == SearchTypeKademlia)
		m_pwndParams->m_ctlCancel.EnableWindow(Kademlia::CSearchManager::IsSearching(pParams->dwSearchID));

	UpdateMoreButtonState(pParams);
	searchlistctrl.ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
}

void CSearchResultsWnd::OnSelChangeTab(LPNMHDR, LRESULT *pResult)
{
	int cur_sel = searchselect.GetCurSel();
	if (cur_sel >= 0) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (searchselect.GetItem(cur_sel, &ti) && ti.lParam != NULL) {
			searchselect.HighlightItem(cur_sel, FALSE);
			ShowResults(reinterpret_cast<SSearchParams*>(ti.lParam));
			searchselect.UpdateTabToolTips();
		}
	}
	*pResult = 0;
}

void CSearchResultsWnd::OnSelChangingTab(LPNMHDR, LRESULT *pResult)
{
	if (!m_astrFilter.IsEmpty()) {
		int cur_sel = searchselect.GetCurSel();
		if (cur_sel >= 0) {
			CString strTabLabel;
			TCITEM ti;
			ti.cchTextMax = 512;
			ti.pszText = strTabLabel.GetBuffer(ti.cchTextMax);
			ti.mask = TCIF_TEXT;
			bool b = searchselect.GetItem(cur_sel, &ti);
			strTabLabel.ReleaseBuffer();
			if (b) {
				int i = strTabLabel.ReverseFind(_T('/'));
				int j = strTabLabel.ReverseFind(_T('('));
				if (j >= 0 && i > j) {
					strTabLabel.Delete(j + 1, i - j);
					DupAmpersand(strTabLabel);
					ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTabLabel);
					searchselect.SetItem(cur_sel, &ti);
				}
			}
		}
	}
	*pResult = 0;
}

LRESULT CSearchResultsWnd::OnCloseTab(WPARAM wParam, LPARAM)
{
	if (searchselect.m_bDragging)
		return false;

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (searchselect.GetItem((int)wParam, &ti) && ti.lParam != NULL) {
		uint32 uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
		if (!m_cancelled && uSearchID == m_nEd2kSearchID)
			CancelEd2kSearch();
		DeleteSearch(uSearchID);
	}
	return TRUE;
}

LRESULT CSearchResultsWnd::OnDblClickTab(WPARAM wParam, LPARAM)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (searchselect.GetItem((int)wParam, &ti) && ti.lParam != NULL)
		m_pwndParams->SetParameters(reinterpret_cast<SSearchParams*>(ti.lParam));
	return TRUE;
}

void CSearchResultsWnd::OnTabMovement(LPNMHDR, LRESULT*)
{
	UINT from = searchselect.GetLastMovementSource();
	UINT to = searchselect.GetLastMovementDestionation();

	if (from == to - 1)
		return;
	
	searchselect.ReorderTab(from, to); // reorder control itself
	theApp.searchlist->ReorderSearches();

	if (to > from)
		--to;
	searchselect.SetCurSel(to);
	searchselect.UpdateTabToolTips();
}

void CSearchResultsWnd::OnBnClickedComplete()
{
	thePrefs.m_uCompleteCheckState = IsDlgButtonChecked(IDC_CHECK_COMPLETE);
	int iCurSel = searchselect.GetCurSel();
	if (iCurSel >= 0) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (searchselect.GetItem(iCurSel, &item) && item.lParam != NULL)
			ShowResults(reinterpret_cast<SSearchParams*>(item.lParam));
	}
}

void CSearchResultsWnd::OnBnClickedKnown()
{
	thePrefs.m_uSearchKnownCheckState = IsDlgButtonChecked(IDC_CHECK_KNOWN);
	int iCurSel = searchselect.GetCurSel();
	if (iCurSel >= 0) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (searchselect.GetItem(iCurSel, &item) && item.lParam != NULL)
			ShowResults(reinterpret_cast<SSearchParams*>(item.lParam));
	}
}

void CSearchResultsWnd::UpdateCatTabs()
{
	int oldsel = m_cattabs.GetCurSel();
	m_cattabs.DeleteAllItems();
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		CString label(thePrefs.GetCategoryDisplayTitle(i));
		DupAmpersand(label);
		m_cattabs.InsertItem((int)i, label);
	}
	if (oldsel >= m_cattabs.GetItemCount() || oldsel < 0)
		oldsel = 0;

	m_cattabs.SetCurSel(oldsel);
	int flag = (m_cattabs.GetItemCount() > 1) ? SW_SHOW : SW_HIDE;
	m_cattabs.ShowWindow(flag);
	GetDlgItem(IDC_STATIC_DLTOof)->ShowWindow(flag);
}

void CSearchResultsWnd::ShowSearchSelector(bool visible)
{
	WINDOWPLACEMENT wpTabSelect, wpList;
	searchselect.GetWindowPlacement(&wpTabSelect);
	searchlistctrl.GetWindowPlacement(&wpList);

	int nCmdShow;
	if (visible) {
		nCmdShow = SW_SHOW;
		wpList.rcNormalPosition.top = wpTabSelect.rcNormalPosition.bottom;
	} else {
		nCmdShow = SW_HIDE;
		wpList.rcNormalPosition.top = wpTabSelect.rcNormalPosition.top;
	}
	searchselect.ShowWindow(nCmdShow);
	RemoveAnchor(searchlistctrl);
	searchlistctrl.SetWindowPlacement(&wpList);
	AddOrReplaceAnchor(this, searchlistctrl, TOP_LEFT, BOTTOM_RIGHT);
	GetDlgItem(IDC_CLEARALL)->ShowWindow(nCmdShow);
	m_ctlFilter.ShowWindow(nCmdShow);
	GetDlgItem(IDC_CHECK_COMPLETE)->ShowWindow(nCmdShow);
	GetDlgItem(IDC_CHECK_KNOWN)->ShowWindow(SW_HIDE);
	if (visible)
		EnsureFilterControlLayout();
	searchselect.UpdateTabToolTips();
}

void CSearchResultsWnd::OnDestroy()
{
	StopSearchTabActivityTimer();
	ClearChunkedSearchDownloadItems();
	ClearChunkedSearchCleanup();

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (INT_PTR i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem((int)i, &ti))
			delete reinterpret_cast<SSearchParams*>(ti.lParam);

	CResizableFormView::OnDestroy();
}

void CSearchResultsWnd::OnClose()
{
	// Do not pass the WM_CLOSE to the base class. Since we have a rich edit control *and*
	// an attached auto complete control, the WM_CLOSE will get generated by the rich edit control
	// when user presses ESC while the auto complete is open.
}

BOOL CSearchResultsWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Search);
	return TRUE;
}

LRESULT CSearchResultsWnd::OnIdleUpdateCmdUI(WPARAM, LPARAM)
{
	BOOL bSearchParamsWndVisible = theApp.emuledlg->searchwnd->IsSearchParamsWndVisible();
	m_ctlOpenParamsWnd.ShowWindow(bSearchParamsWndVisible ? SW_HIDE : SW_SHOW);

	return 0;
}

void CSearchResultsWnd::OnBnClickedOpenParamsWnd()
{
	theApp.emuledlg->searchwnd->OpenParametersWnd();
}

void CSearchResultsWnd::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) != SC_KEYMENU)
		__super::OnSysCommand(nID, lParam);
	else if (lParam == EMULE_HOTMENU_ACCEL)
			theApp.emuledlg->SendMessage(WM_COMMAND, IDC_HOTMENU);
		else
			theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
}

bool CSearchResultsWnd::CanSearchRelatedFiles() const
{
	return theApp.serverconnect->IsConnected()
		&& theApp.serverconnect->GetCurrentServer() != NULL
		&& theApp.serverconnect->GetCurrentServer()->GetRelatedSearchSupport();
}

// https://forum.emule-project.net/index.php?showtopic=79371&view=findpost&p=564252 )
// Syntax: related::<file hash> or related:<file size>:<file hash>
//
// "the file you 'search' for must be shared by at least 5 clients."
// A client can give several hashes in the related search request since v17.14.
void CSearchResultsWnd::SearchRelatedFiles(CPtrList &listFiles)
{
	POSITION pos = listFiles.GetHeadPosition();
	if (pos == NULL) {
		ASSERT(0);
		return;
	}
	SSearchParams *pParams = new SSearchParams;
	pParams->strExpression = _T("related");

	CString strNames;
	while (pos != NULL) {
		CAbstractFile *pFile = static_cast<CAbstractFile*>(listFiles.GetNext(pos));
		if (pFile->IsKindOf(RUNTIME_CLASS(CAbstractFile))) {
			pParams->strExpression.AppendFormat(_T("::%s"), (LPCTSTR)md4str(pFile->GetFileHash()));
			if (!strNames.IsEmpty())
				strNames += _T(", ");
			strNames += pFile->GetFileName();
		} else
			ASSERT(0);
	}

	pParams->strSpecialTitle.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("RELATED")), (LPCTSTR)strNames);
	if (pParams->strSpecialTitle.GetLength() > 50)
		pParams->strSpecialTitle = pParams->strSpecialTitle.Left(47) + _T("...");
	StartSearch(pParams);
}


///////////////////////////////////////////////////////////////////////////////
// CSearchResultsSelector

BEGIN_MESSAGE_MAP(CSearchResultsSelector, CClosableTabCtrl)
	ON_WM_CONTEXTMENU()
	ON_WM_MOUSELEAVE()
	ON_WM_MOUSEMOVE()
	ON_WM_SIZE()
END_MESSAGE_MAP()

CSearchResultsSelector::CSearchResultsSelector()
	: m_uSelectedClientRuntimeID(0)
	, m_nTooltipTabIndex(-1)
{
}

void CSearchResultsSelector::InitToolTips()
{
	if (m_tooltipTabs.GetSafeHwnd() != NULL)
		return;

	m_tooltipTabs.Create(this, TTS_NOPREFIX);
	SetToolTips(&m_tooltipTabs);
	m_tooltipTabs.SetMargin(CRect(6, 6, 6, 6));
	m_tooltipTabs.SendMessage(TTM_SETMAXTIPWIDTH, 0, 500);
	m_tooltipTabs.SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
	m_tooltipTabs.SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	m_tooltipTabs.Activate(TRUE);
}

void CSearchResultsSelector::UpdateTabToolTips(int tab)
{
	if (m_tooltipTabs.GetSafeHwnd() == NULL)
		return;

	m_tooltipTabs.ClearHeaderIcon();
	m_tooltipTabs.DelTool(this, GetSearchSelectorToolId());

	int iTooltipTab = m_nTooltipTabIndex;
	if (iTooltipTab < 0)
		iTooltipTab = tab;
	if (iTooltipTab < 0 || iTooltipTab >= GetItemCount())
		return;

	CString strTip = BuildSharedFilesTooltip(iTooltipTab);
	if (strTip.IsEmpty())
		strTip = BuildSearchTooltip(iTooltipTab);
	if (strTip.IsEmpty())
		return;

	TCITEM ti = {};
	ti.mask = TCIF_PARAM;
	if (GetItem(iTooltipTab, &ti) && ti.lParam != NULL)
		m_tooltipTabs.SetHeaderIcon(GetImageList(), GetSearchTabStaticImage(reinterpret_cast<const SSearchParams*>(ti.lParam)));

	CRect rcItem;
	GetItemRect(iTooltipTab, &rcItem);
	VERIFY(m_tooltipTabs.AddTool(this, strTip, &rcItem, GetSearchSelectorToolId()));
}

void CSearchResultsSelector::OnMouseLeave()
{
	if (m_nTooltipTabIndex != -1 && m_tooltipTabs.GetSafeHwnd() != NULL)
		m_tooltipTabs.Pop();

	m_nTooltipTabIndex = -1;
	UpdateTabToolTips();
	CClosableTabCtrl::OnMouseLeave();
}

void CSearchResultsSelector::OnMouseMove(UINT nFlags, CPoint point)
{
	TCHITTESTINFO hitTestInfo = {};
	hitTestInfo.pt = point;
	const int nHoverTabIndex = HitTest(&hitTestInfo);

	if (nHoverTabIndex != m_nTooltipTabIndex) {
		if (m_tooltipTabs.GetSafeHwnd() != NULL)
			m_tooltipTabs.Pop();
		m_nTooltipTabIndex = nHoverTabIndex;
		UpdateTabToolTips(nHoverTabIndex);
	}

	CClosableTabCtrl::OnMouseMove(nFlags, point);
}

ClientRuntimeID CSearchResultsSelector::GetClientRuntimeIDForTab(int iTab) const
{
	TCITEM ti = {};
	ti.mask = TCIF_PARAM;
	if (!GetItem(iTab, &ti) || ti.lParam == NULL)
		return 0;

	const SSearchParams* pParams = reinterpret_cast<const SSearchParams*>(ti.lParam);
	if (!pParams->bClientSharedFiles || pParams->m_strClientHash.IsEmpty())
		return 0;

	uchar aucClientHash[MDX_DIGEST_SIZE];
	if (!strmd4(pParams->m_strClientHash, aucClientHash))
		return 0;

	if (theApp.clientlist == NULL)
		return 0;

	CUpDownClient* pClient = theApp.clientlist->AcquireTrackedClientByUserHash(aucClientHash, thePrefs.GetClientHistory());
	if (pClient == NULL)
		return 0;

	const ClientRuntimeID uRuntimeID = pClient->GetRuntimeID();
	pClient->ReleaseRuntimeReference();
	return uRuntimeID;
}

CString CSearchResultsSelector::BuildSearchTooltip(int iTab) const
{
	TCITEM ti = {};
	ti.mask = TCIF_PARAM;
	if (!GetItem(iTab, &ti) || ti.lParam == NULL)
		return CString();

	const SSearchParams* pParams = reinterpret_cast<const SSearchParams*>(ti.lParam);
	if (pParams->bClientSharedFiles)
		return CString();

	CString strDetails;
	const CString strQuery = !pParams->strSpecialTitle.IsEmpty()
		? pParams->strSpecialTitle
		: (pParams->strBooleanExpr.IsEmpty() ? pParams->strExpression : pParams->strBooleanExpr);
	AppendTooltipLine(strDetails, GetResString(_T("METHOD")), GetSearchMethodText(pParams));

	if (pParams->dwSearchServerIP != 0) {
		CString strServerAddress;
		strServerAddress.Format(_T("%s:%u"), (LPCTSTR)ipstr(pParams->dwSearchServerIP), pParams->nSearchServerPort);
		AppendTooltipLine(strDetails, GetResString(_T("SERVER")), strServerAddress);

		CString strServerName(pParams->strSearchServerName);
		if (strServerName.IsEmpty()) {
			if (const CServer* pServer = theApp.serverlist->GetServerByIPTCP(pParams->dwSearchServerIP, pParams->nSearchServerPort))
				strServerName = pServer->GetListName();
		}
		if (!strServerName.IsEmpty())
			AppendTooltipLine(strDetails, GetResString(_T("SL_SERVERNAME")), strServerName);
	}

	if (!pParams->strFileType.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("TYPE")), pParams->strFileType);

	CString strMinSize(pParams->strMinSize);
	if (strMinSize.IsEmpty() && pParams->ullMinSize > 0)
		strMinSize = CastItoXBytes(pParams->ullMinSize);
	if (!strMinSize.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("SEARCHMINSIZE")), strMinSize);
	CString strMaxSize(pParams->strMaxSize);
	if (strMaxSize.IsEmpty() && pParams->ullMaxSize > 0)
		strMaxSize = CastItoXBytes(pParams->ullMaxSize);
	if (!strMaxSize.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("SEARCHMAXSIZE")), strMaxSize);
	if (pParams->uAvailability > 0) {
		CString strAvailability;
		strAvailability.Format(_T("%u"), pParams->uAvailability);
		AppendTooltipLine(strDetails, GetResString(_T("SEARCHAVAIL")), strAvailability);
	}
	if (!pParams->strExtension.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("SEARCHEXTENTION")), pParams->strExtension);
	if (pParams->uComplete > 0) {
		CString strComplete;
		strComplete.Format(_T("%u"), pParams->uComplete);
		AppendTooltipLine(strDetails, GetResString(_T("COMPLETE")), strComplete);
	}
	if (!pParams->strCodec.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("CODEC")), pParams->strCodec);
	if (pParams->uiMinBitrate > 0) {
		CString strBitrate;
		strBitrate.Format(_T("%u"), pParams->uiMinBitrate);
		AppendTooltipLine(strDetails, GetResString(_T("BITRATE")), strBitrate);
	}
	if (pParams->uiMinLength > 0) {
		CString strLength;
		strLength.Format(_T("%u"), pParams->uiMinLength);
		AppendTooltipLine(strDetails, GetResString(_T("LENGTH")), strLength);
	}
	if (!pParams->strTitle.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("TITLE")), pParams->strTitle);
	if (!pParams->strAlbum.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("ALBUM")), pParams->strAlbum);
	CString strArtist(pParams->strArtist);
	if (!strArtist.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("ARTIST")), strArtist);

	return BuildFormattedTooltip(strQuery, strDetails);
}

CString CSearchResultsSelector::BuildSharedFilesTooltip(int iTab) const
{
	CScopedSearchClientRef clientRef(GetClientRuntimeIDForTab(iTab));
	CUpDownClient* pClient = clientRef.Get();
	if (pClient == NULL)
		return CString();

	const CString strUserName = (pClient->GetUserName() != NULL && pClient->GetUserName()[0] != _T('\0'))
		? CString(pClient->GetUserName())
		: md4str(pClient->GetUserHash());
	CString strDetails;
	CString strClientNote(pClient->m_strClientNote);

	AppendTooltipLine(strDetails, GetResString(_T("QL_USERNAME")), strUserName);
	AppendTooltipLine(strDetails, GetResString(_T("CD_UHASH2")), pClient->HasValidHash() ? md4str(pClient->GetUserHash()) : CString(_T("?")));

	CString strIpAddress;
	if (!pClient->GetConnectIP().IsNull() || !pClient->GetIP().IsNull())
		strIpAddress = ipstr(!pClient->GetIP().IsNull() ? pClient->GetIP() : pClient->GetConnectIP());
	strIpAddress.TrimLeft();
	if (strIpAddress.GetLength() > 1 && strIpAddress[0] == _T(':') && _istdigit(strIpAddress[1])) {
		strIpAddress.Delete(0, 1);
		strIpAddress.TrimLeft();
	}
	if (!strIpAddress.IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("CD_UIP")), strIpAddress);

	CString strCountry(pClient->m_structClientGeolocationData.Country);
	CString strCity(pClient->m_structClientGeolocationData.City);
	CString strCountryCity;
	if (!strCountry.IsEmpty()) {
		strCountryCity = strCountry;
		if (!strCity.IsEmpty())
			strCountryCity.AppendFormat(_T(", %s"), (LPCTSTR)strCity);
	} else {
		strCountryCity = pClient->GetGeolocationData(true);
	}
	if (!strCountryCity.IsEmpty()) {
		const CString strCountryLabel = GetResString(_T("GEOLOCATION"));
		if (theApp.ipgeolocation && theApp.ipgeolocation->ShowCountryFlag()) {
			CString strFlagLine;
			strFlagLine.Format(_T("%s: <flag=%u>%s"), (LPCTSTR)strCountryLabel, pClient->GetCountryFlagIndex(), (LPCTSTR)strCountryCity);
			AppendTooltipRawLine(strDetails, strFlagLine);
		} else {
			AppendTooltipLine(strDetails, strCountryLabel, strCountryCity);
		}
	}

	AppendTooltipLine(strDetails, GetResString(_T("CD_CSOFT")), pClient->DbgGetFullClientSoftVer());

	if (!pClient->GetClientModVer().IsEmpty())
		AppendTooltipLine(strDetails, GetResString(_T("CD_MOD")), pClient->GetClientModVer());

	AppendTooltipLine(strDetails, GetResString(_T("FIRST_SEEN")), FormatTooltipTimeValue(pClient->tFirstSeen));
	AppendTooltipLine(strDetails, GetResString(_T("LAST_SEEN")), FormatTooltipTimeValue(pClient->tLastSeen));

	CString strSharedCount;
	strSharedCount.Format(_T("%u"), pClient->m_uSharedFilesCount);
	AppendTooltipLine(strDetails, GetResString(_T("SHAREDFILESCOUNTCOLUMN")), strSharedCount);

	if (pClient->m_tSharedFilesLastQueriedTime > 0)
		AppendTooltipLine(strDetails, GetResString(_T("SHAREDFILESLASTQUERIED")), FormatTooltipTimeValue(pClient->m_tSharedFilesLastQueriedTime));

	if (pClient->GetServerIP() != 0) {
		CString strServer;
		strServer.Format(_T("%s:%u"), (LPCTSTR)ipstr(pClient->GetServerIP()), pClient->GetServerPort());
		AppendTooltipLine(strDetails, GetResString(_T("ED2KSERVER")), strServer);
	}

	const CString strKadState = GetResString(pClient->GetKadPort() ? _T("CONNECTED") : _T("DISCONNECTED"));
	CString strKad;
	strKad.Format(_T("%s (%u)"), (LPCTSTR)strKadState, pClient->GetKadPort());
	AppendTooltipLine(strDetails, GetResString(_T("KADEMLIA")), strKad);

	AppendTooltipLine(strDetails, GetResString(_T("FRIEND")), GetResString(pClient->IsFriend() ? _T("YES") : _T("NO")));
	AppendTooltipLine(strDetails, GetResString(_T("BAD_CLIENT_TYPE")), pClient->GetPunishmentReason());
	AppendTooltipLine(strDetails, GetResString(_T("PUNISHMENT")), pClient->GetPunishmentText());

	CString strTitle;
	strTitle.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("CLIENT_NOTE")), (LPCTSTR)strClientNote);
	return BuildFormattedTooltip(strTitle, strDetails);
}

BOOL CSearchResultsSelector::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case MP_RESTORESEARCHPARAMS:
	{
		int iTab = GetTabUnderContextMenu();
		if (iTab >= 0)
			GetParent()->SendMessage(UM_DBLCLICKTAB, (WPARAM)iTab);

		return TRUE;
	}
	case MP_RECHECK_SPAM_BLACKLIST:
	{
		int iTab = GetTabUnderContextMenu();
		if (iTab < 0)
			iTab = GetCurSel();

		TCITEM ti;
		ti.mask = TCIF_PARAM;
		CSearchResultsWnd* pResultsWnd = theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL ? theApp.emuledlg->searchwnd->m_pwndResults : NULL;
		if (pResultsWnd != NULL && iTab >= 0 && GetItem(iTab, &ti) && ti.lParam != NULL && theApp.searchlist != NULL) {
			const uint32 uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
			if (uSearchID != 0) {
				theApp.searchlist->RecalculateSpamRatings(uSearchID, false, false, true);
				pResultsWnd->RefreshSearchTabActivityAnimation();
			}
		}

		return TRUE;
	}
	case MP_MERGE_FROM:
	{
		int iTab = GetTabUnderContextMenu();
		if (iTab < 0)
			return TRUE;

		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetItem(iTab, &ti) && ti.lParam != NULL) {
			theApp.emuledlg->searchwnd->m_pwndResults->m_uMergeFromSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
			theApp.emuledlg->searchwnd->m_pwndResults->m_bMergeFromSearchIDHasBeenSet = true;
		}

		return TRUE;
	}
	case MP_MERGE_TO:
	{
		if (!theApp.emuledlg->searchwnd->m_pwndResults->m_bMergeFromSearchIDHasBeenSet) // We need true to continue.
			return TRUE;

		int iTab = GetTabUnderContextMenu();
		if (iTab < 0) 
			return TRUE;

		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (!theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetItem(iTab, &ti) || !ti.lParam)
			return FALSE; // No search tab selected or no parameters available.

		BOOL bResult = theApp.emuledlg->searchwnd->m_pwndResults->MergeSearchResults(theApp.emuledlg->searchwnd->m_pwndResults->m_uMergeFromSearchID, reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID);
		theApp.emuledlg->searchwnd->m_pwndResults->m_bMergeFromSearchIDHasBeenSet = false; // Reset the merge from search ID, so we can merge again later.
		return bResult; // Return the result of the merge operation.
	}
	case MP_CLEAN_UP_CURRENT_TAB:
	{
		int iTab = GetTabUnderContextMenu();
		if (iTab >= 0)
			theApp.emuledlg->searchwnd->m_pwndResults->StartChunkedCleanUpSearchResults(iTab);
		return TRUE;
	}
	case MP_DOWNLOAD_ALL_LISTED:
	case MP_DOWNLOAD_ALL_UNKNOWN:
	{
		int iTab = GetTabUnderContextMenu();
		if (iTab < 0)
			iTab = GetCurSel();
		if (iTab >= 0)
			theApp.emuledlg->searchwnd->m_pwndResults->DownloadAllSearchResults(iTab, wParam == MP_DOWNLOAD_ALL_UNKNOWN);
		return TRUE;
	}
	case MP_SHOWLIST:
	case MP_MESSAGE:
	case MP_ADDFRIEND:
	case MP_FRIENDSLOT:
	case MP_DETAIL:
	case MP_BOOT:
	case MP_SHOWLIST_AUTO_QUERY:
	case MP_ACTIVATE_AUTO_QUERY:
	case MP_DEACTIVATE_AUTO_QUERY:
	case MP_EDIT_NOTE:
	case MP_PUNISMENT_IPUSERHASHBAN:
	case MP_PUNISMENT_USERHASHBAN:
	case MP_PUNISMENT_UPLOADBAN:
	case MP_PUNISMENT_SCOREX01:
	case MP_PUNISMENT_SCOREX02:
	case MP_PUNISMENT_SCOREX03:
	case MP_PUNISMENT_SCOREX04:
	case MP_PUNISMENT_SCOREX05:
	case MP_PUNISMENT_SCOREX06:
	case MP_PUNISMENT_SCOREX07:
	case MP_PUNISMENT_SCOREX08:
	case MP_PUNISMENT_SCOREX09:
	case MP_PUNISMENT_NONE:
	{
		CScopedSearchClientRef clientRef(m_uSelectedClientRuntimeID);
		CUpDownClient* pSelectedClient = clientRef.Get();
		if (pSelectedClient == NULL)
			return TRUE;

		switch (wParam) {
		case MP_SHOWLIST:
		{
			CUpDownClient* pActiveClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(pSelectedClient);
			if (pActiveClient != NULL)
				pActiveClient->RequestSharedFileList();
			return TRUE;
		}
		case MP_MESSAGE:
		{
			CUpDownClient* pActiveClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(pSelectedClient);
			if (pActiveClient != NULL)
				theApp.emuledlg->chatwnd->StartSession(pActiveClient);
			return TRUE;
		}
		case MP_ADDFRIEND:
			theApp.friendlist->AddFriend(pSelectedClient);
			return TRUE;
		case MP_FRIENDSLOT:
			{
				CFriend *pFriend = pSelectedClient->GetFriend();
				if (pFriend != NULL) {
					pFriend->SetFriendSlot(!pFriend->GetFriendSlot());
					theApp.friendlist->SaveList();
				}
			}
			return TRUE;
		case MP_DETAIL:
		{
			CClientDetailDialog dialog(pSelectedClient, NULL);
			clientRef.Release();
			dialog.DoModal();
			return TRUE;
		}
		case MP_BOOT:
			if (pSelectedClient->GetKadPort() && pSelectedClient->GetKadVersion() >= KADEMLIA_VERSION2_47a)
				Kademlia::CKademlia::Bootstrap(pSelectedClient->GetIPv4().ToUInt32(true), pSelectedClient->GetKadPort());
			return TRUE;
		case MP_SHOWLIST_AUTO_QUERY:
		{
			pSelectedClient->SetAutoQuerySharedFiles(true);
			CUpDownClient* pActiveClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(pSelectedClient);
			if (pActiveClient != NULL && (pActiveClient == pSelectedClient || theApp.clientlist->IsValidClient(pActiveClient)))
				pActiveClient->RequestSharedFileList();
			return TRUE;
		}
		case MP_ACTIVATE_AUTO_QUERY:
			pSelectedClient->SetAutoQuerySharedFiles(true);
			return TRUE;
		case MP_DEACTIVATE_AUTO_QUERY:
			pSelectedClient->SetAutoQuerySharedFiles(false);
			return TRUE;
		case MP_EDIT_NOTE:
		{
			const CString strUserName = pSelectedClient->GetUserName() != NULL ? pSelectedClient->GetUserName() : _T("?");
			const CString strUserHash = md4str(pSelectedClient->GetUserHash());
			const CString strCurrentNote = pSelectedClient->m_strClientNote;
			clientRef.Release();

			InputBox inputbox;
			CString m_strLabel;
			m_strLabel.Format(_T("User: %s\nHash: %s"), (LPCTSTR)strUserName, (LPCTSTR)strUserHash);
			inputbox.SetLabels(GetResString(_T("EDIT_CLIENT_NOTE")), m_strLabel, strCurrentNote);
			inputbox.DoModal();
			if (inputbox.WasCancelled() || inputbox.GetInput().IsEmpty())
				return TRUE;

			CScopedSearchClientRef updateClientRef(m_uSelectedClientRuntimeID);
			CUpDownClient* pUpdateClient = updateClientRef.Get();
			if (pUpdateClient == NULL)
				return TRUE;

			pUpdateClient->m_strClientNote = inputbox.GetInput();
			theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(pUpdateClient, -1, CClientListCtrl::kSortImpactNote);
			theApp.QueueUploadClientRowsChanged(pUpdateClient, CemuleApp::UploadClientUiTargetUploadList | CemuleApp::UploadClientUiTargetQueueList | CemuleApp::UploadClientUiTargetDownloadClients);
			theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.UpdateTabHeader(0, md4str(pUpdateClient->GetUserHash()), false);
			return TRUE;
		}
		case MP_PUNISMENT_IPUSERHASHBAN:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
			return TRUE;
		case MP_PUNISMENT_USERHASHBAN:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
			return TRUE;
		case MP_PUNISMENT_UPLOADBAN:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
			return TRUE;
		case MP_PUNISMENT_SCOREX01:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
			return TRUE;
		case MP_PUNISMENT_SCOREX02:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
			return TRUE;
		case MP_PUNISMENT_SCOREX03:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
			return TRUE;
		case MP_PUNISMENT_SCOREX04:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
			return TRUE;
		case MP_PUNISMENT_SCOREX05:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
			return TRUE;
		case MP_PUNISMENT_SCOREX06:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
			return TRUE;
		case MP_PUNISMENT_SCOREX07:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
			return TRUE;
		case MP_PUNISMENT_SCOREX08:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
			return TRUE;
		case MP_PUNISMENT_SCOREX09:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
			return TRUE;
		case MP_PUNISMENT_NONE:
			theApp.shield->SetPunishment(pSelectedClient,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_NOTBADCLIENT, P_NOPUNISHMENT);
			return TRUE;
		}
		break;
	}
	}

	return CClosableTabCtrl::OnCommand(wParam, lParam);
}

void CSearchResultsSelector::OnContextMenu(CWnd*, CPoint point)
{
	if (point.x == -1 || point.y == -1) {
		if (!SetDefaultContextMenuPos())
			return;
		point = m_ptCtxMenu;
		ClientToScreen(&point);
	} else {
		m_ptCtxMenu = point;
		ScreenToClient(&m_ptCtxMenu);
	}

	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(GetResString(_T("SW_RESULT")));

	CMenuXP ClientMenu;
	ClientMenu.CreatePopupMenu(); // Ensure HMENU is created before adding sidebar; otherwise CMenu::AppendMenu asserts.
	ClientMenu.AddMenuSidebar(GetResString(_T("CLIENT")));

	int cur_sel = theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetCurSel();
	if (cur_sel >= 0) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetItem(cur_sel, &item) && item.lParam != NULL) {
			theApp.emuledlg->searchwnd->m_pwndResults->searchselect.HighlightItem(cur_sel, FALSE);
			uint32 m_uSearchID = reinterpret_cast<SSearchParams*>(item.lParam)->dwSearchID;
			const SSearchParams* pParams = theApp.emuledlg->searchwnd->m_pwndResults->GetSearchResultsParams(m_uSearchID);
			if (pParams && pParams->bClientSharedFiles) {
				m_uSelectedClientRuntimeID = GetClientRuntimeIDForTab(cur_sel);
				CScopedSearchClientRef selectedClientRef(m_uSelectedClientRuntimeID);
				CUpDownClient* pSelectedClient = selectedClientRef.Get();
				if (pSelectedClient != NULL) {
					const bool is_ed2k = pSelectedClient->IsEd2kClient();
					const CFriend *pFriend = pSelectedClient->GetFriend();
					ClientMenu.AppendMenu(MF_STRING | MF_ENABLED, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
					ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !pSelectedClient->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(_T("ADDFRIEND")), _T("ADDFRIEND"));
					ClientMenu.AppendMenu(MF_STRING | (pFriend != NULL ? MF_ENABLED : MF_GRAYED), MP_FRIENDSLOT, GetResString(_T("FRIENDSLOT")), _T("FRIENDSLOT"));
					ClientMenu.CheckMenuItem(MP_FRIENDSLOT, (pFriend != NULL && pFriend->GetFriendSlot()) ? MF_CHECKED : MF_UNCHECKED);
					ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(_T("SEND_MSG")), _T("SENDMESSAGE"));
					ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && pSelectedClient->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(_T("VIEWFILES")), _T("VIEWFILES"));

					ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && pSelectedClient->GetViewSharedFilesSupport() && (pSelectedClient->m_bIsArchived || !pSelectedClient->socket || !pSelectedClient->socket->IsConnected())) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST_AUTO_QUERY, GetResString(_T("VIEW_FILES_ACTIVATE_AUTO_QUERY")), _T("CLOCKGREEN"));
					if (m_uSelectedClientRuntimeID == 0)
						ClientMenu.AppendMenu(MF_STRING | MF_GRAYED, MP_ACTIVATE_AUTO_QUERY, GetResString(_T("ACTIVATE_AUTO_QUERY")), _T("CLOCKBLUE"));
					else if (pSelectedClient->m_bAutoQuerySharedFiles)
						ClientMenu.AppendMenu(MF_STRING | MF_ENABLED, MP_DEACTIVATE_AUTO_QUERY, GetResString(_T("DEACTIVATE_AUTO_QUERY")), _T("CLOCKRED"));
					else
						ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && pSelectedClient->GetViewSharedFilesSupport() && (pSelectedClient->m_bIsArchived || !pSelectedClient->socket || !pSelectedClient->socket->IsConnected())) ? MF_ENABLED : MF_GRAYED), MP_ACTIVATE_AUTO_QUERY, GetResString(_T("ACTIVATE_AUTO_QUERY")), _T("CLOCKBLUE"));

					ClientMenu.AppendMenu(MF_STRING | MF_ENABLED, MP_EDIT_NOTE, GetResString(_T("EDIT_CLIENT_NOTE")), _T("RENAME"));

					ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
					CMenuXP m_PunishmentMenu;
					m_PunishmentMenu.CreateMenu();
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_IPUSERHASHBAN, GetResString(_T("IP_USER_HASH_BAN")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_USERHASHBAN, GetResString(_T("USER_HASH_BAN")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_UPLOADBAN, GetResString(_T("UPLOAD_BAN")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX01, GetResString(_T("SCORE_01")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX02, GetResString(_T("SCORE_02")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX03, GetResString(_T("SCORE_03")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX04, GetResString(_T("SCORE_04")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX05, GetResString(_T("SCORE_05")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX06, GetResString(_T("SCORE_06")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX07, GetResString(_T("SCORE_07")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX08, GetResString(_T("SCORE_08")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX09, GetResString(_T("SCORE_09")));
					m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_NONE, GetResString(_T("NO_PUNISHMENT")));
					ClientMenu.EnableMenuItem((UINT)m_PunishmentMenu.m_hMenu, MF_ENABLED);
					int m_PunishmentMenuItem = MP_PUNISMENT_IPUSERHASHBAN + pSelectedClient->m_uPunishment;
					m_PunishmentMenu.CheckMenuRadioItem(MP_PUNISMENT_IPUSERHASHBAN, MP_PUNISMENT_NONE, m_PunishmentMenuItem, 0);
					ClientMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PunishmentMenu.m_hMenu, GetResString(_T("PUNISHMENT")), _T("PUNISHMENT"));
				}
				menu.AppendMenu(MF_STRING | MF_POPUP | (m_uSelectedClientRuntimeID != 0 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)ClientMenu.m_hMenu, GetResString(_T("CLIENT")), _T("StatsClients"));
				menu.AppendMenu(MF_STRING | MF_SEPARATOR);
			} else
				menu.AppendMenu(MF_STRING, MP_RESTORESEARCHPARAMS, GetResString(_T("RESTORESEARCHPARAMS")), _T("RELOAD"));
		}
	}

	menu.AppendMenu(MF_STRING, MP_RECHECK_SPAM_BLACKLIST, GetResString(_T("RECHECK_SPAM_BLACKLIST")), _T("SPAM_PURPLE"));
	menu.AppendMenu(MF_STRING | MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_MERGE_FROM, GetResString(_T("MERGE_FROM")), _T("MERGEFROM"));
	menu.AppendMenu(MF_STRING | (theApp.emuledlg->searchwnd->m_pwndResults->m_bMergeFromSearchIDHasBeenSet ? MF_ENABLED : MF_GRAYED), MP_MERGE_TO, GetResString(_T("MERGE_TO")), _T("MERGETO"));
	menu.AppendMenu(MF_STRING | MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_CLEAN_UP_CURRENT_TAB, GetResString(_T("CLEAN_UP_CURRENT_TAB")), _T("CLEAR"));
	menu.AppendMenu(MF_STRING | MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_DOWNLOAD_ALL_LISTED, GetResString(_T("DOWNLOAD_ALL_LISTED")), _T("RESUME"));
	menu.AppendMenu(MF_STRING, MP_DOWNLOAD_ALL_UNKNOWN, GetResString(_T("DOWNLOAD_ALL_UNKNOWN")), _T("RESUME"));
	menu.AppendMenu(MF_STRING | MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("FD_CLOSE")), _T("CLOSETAB"));

	menu.SetDefaultItem(MP_RESTORESEARCHPARAMS);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

void CSearchResultsSelector::OnSize(UINT nType, int cx, int cy)
{
	CClosableTabCtrl::OnSize(nType, cx, cy);
	UpdateTabToolTips();
}

LRESULT CSearchResultsWnd::OnChangeFilter(WPARAM wParam, LPARAM lParam)
{
	CEditDelayed::SFilterParam* pFilterParam = reinterpret_cast<CEditDelayed::SFilterParam*>(wParam);
	bool m_bForceApplyFilter = false;
	uint32 m_nFilterColumnTemp = 0;

	if (pFilterParam) {
		m_bForceApplyFilter = pFilterParam->bForceApply;
		m_nFilterColumnTemp = pFilterParam->uColumnIndex;
		delete pFilterParam;
		pFilterParam = nullptr;
	}

	if (!m_bForceApplyFilter) {
		//If not forced to apply filter, read parameters as usual.
		m_strFullFilterExpr = (LPCTSTR)lParam;
		m_nFilterColumn = m_nFilterColumnTemp;

		if (thePrefs.IsDisableFindAsYouType())
			return 0;
	} else if (thePrefs.IsDisableFindAsYouType())
		//If forced to apply filter, we need to read current entered text directly since CEditDelayed will delay lParam.
		m_ctlFilter.GetWindowText(m_strFullFilterExpr);

	m_astrFilterTemp.RemoveAll();
	for (int iPos = 0; iPos >= 0;) {
		const CString& strFilter(m_strFullFilterExpr.Tokenize(_T(" "), iPos));
		if (!strFilter.IsEmpty() && strFilter != _T("-"))
			m_astrFilterTemp.Add(strFilter);
	}

	m_bColumnDiff = (m_nFilterColumn != m_nFilterColumnLastApplied);
	m_nFilterColumnLastApplied = m_nFilterColumn;
	bool bFilterDiff = (m_astrFilterTemp.GetCount() != m_astrFilter.GetCount());

	if (!bFilterDiff)
		for (INT_PTR i = m_astrFilterTemp.GetCount(); --i >= 0;)
			if (m_astrFilterTemp[i] != m_astrFilter[i]) {
				bFilterDiff = true;
				break;
			}

	// Added m_bForceApplyFilter to force filtering with enter/return keys
	if (m_bColumnDiff || bFilterDiff || m_bForceApplyFilter) {
		m_astrFilter.RemoveAll();
		m_astrFilter.Append(m_astrFilterTemp);
		int iCurSel = searchselect.GetCurSel();
		if (iCurSel >= 0) {
			TCITEM ti;
			ti.mask = TCIF_PARAM;
			if (searchselect.GetItem(iCurSel, &ti) && ti.lParam != NULL)
				ShowResults(reinterpret_cast<SSearchParams*>(ti.lParam));
		}
	}

	return 0;
}

void CSearchResultsWnd::OnSearchListMenuBtnDropDown(LPNMHDR, LRESULT*)
{
	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(_T(" "));

	menu.AppendMenu(MF_STRING | (searchselect.GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEALL, GetResString(_T("REMOVEALLSEARCH")), _T("CloseTabSelected"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (searchselect.GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_CLEAN_UP_ALL_TABS, GetResString(_T("CLEAN_UP_ALL_TABS")), _T("CLEAR"));
	menu.AppendMenu(MF_STRING | (searchselect.GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_RECHECK_SPAM_BLACKLIST_FOR_ALL_TABS, GetResString(_T("RECHECK_ALL_TABS")), _T("SPAM_PURPLE"));
	menu.AppendMenu(MF_STRING | (searchselect.GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_MERGE_ALL_TABS, GetResString(_T("MERGE_ALL_TABS")), _T("MERGEFROM"));
	menu.AppendMenu(MF_SEPARATOR);
	CMenuXP menuFileSizeFormat;
	menuFileSizeFormat.CreatePopupMenu(); // Use popup menu for consistency; ensures valid HMENU for AppendMenu operations
	menuFileSizeFormat.AppendMenu(MF_STRING, MP_SHOW_FILESIZE_DFLT, GetResString(_T("DEFAULT")));
	menuFileSizeFormat.AppendMenu(MF_STRING, MP_SHOW_FILESIZE_KBYTE, GetResString(_T("KBYTES")));
	menuFileSizeFormat.AppendMenu(MF_STRING, MP_SHOW_FILESIZE_MBYTE, GetResString(_T("MBYTES")));
	menuFileSizeFormat.CheckMenuRadioItem(MP_SHOW_FILESIZE_DFLT, MP_SHOW_FILESIZE_MBYTE, MP_SHOW_FILESIZE_DFLT + searchlistctrl.GetFileSizeFormat(), 0);
	menu.AppendMenu(MF_POPUP, (UINT_PTR)menuFileSizeFormat.m_hMenu, GetResString(_T("DL_SIZE")));

	RECT rc;
	m_btnSearchListMenu.GetWindowRect(&rc);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom, this);
}

BOOL CSearchResultsWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case MP_REMOVEALL:
		DeleteAllSearches();
		return TRUE;
	case MP_CLEAN_UP_ALL_TABS:
	{
		StartChunkedCleanUpAllSearchResults();
		return TRUE;
	}
	case MP_RECHECK_SPAM_BLACKLIST_FOR_ALL_TABS:
	{
		uint32 m_uTabCount = RecheckAllSearchResults();
		m_uTabCount ? AddLogLine(true, GetResString(_T("RECHECK_ALL_TABS_SUCCESSFUL")), m_uTabCount) : AddLogLine(true, GetResString(_T("RECHECK_ALL_TABS_FOUND_NONE")));
		return TRUE;
	}
	case MP_MERGE_ALL_TABS:
	{
		const uint32 m_TotalTabCount = searchselect.GetItemCount();
		uint32 m_uSuccessCount = MergeAllSearchResults();
		uint32 m_uFailedCount = m_TotalTabCount - m_uSuccessCount;
		m_uSuccessCount > 1 ? m_uFailedCount ? AddLogLine(true, GetResString(_T("MERGE_ALL_TABS_SUCCESSFUL_PARTLY")), m_uSuccessCount, m_uFailedCount) : AddLogLine(true, GetResString(_T("MERGE_ALL_TABS_SUCCESSFUL")), m_uSuccessCount): AddLogLine(true, GetResString(_T("MERGE_ALL_TABS_FOUND_NONE")));
		return TRUE;
	}
	case MP_SHOW_FILESIZE_DFLT:
		searchlistctrl.SetFileSizeFormat(fsizeDefault);
		return TRUE;
	case MP_SHOW_FILESIZE_KBYTE:
		searchlistctrl.SetFileSizeFormat(fsizeKByte);
		return TRUE;
	case MP_SHOW_FILESIZE_MBYTE:
		searchlistctrl.SetFileSizeFormat(fsizeMByte);
		return TRUE;
	}
	return CResizableFormView::OnCommand(wParam, lParam);
}

HBRUSH CSearchResultsWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

uint32 CSearchResultsWnd::CleanUpSearchResults(int iTab)
{
	uint32 m_uDeletedCount = 0;
	TCITEM ti;
	ti.mask = TCIF_PARAM;

	if (searchselect.GetItem(iTab, &ti) && ti.lParam != NULL) {
		uint32 m_uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
		if (theApp.searchlist != NULL) {
			CWaitCursor curWait; // This may take a while, so show a wait cursor.
			bool bRemovedAny = false;
			m_uDeletedCount = theApp.searchlist->RemoveCleanUpSearchResults(m_uSearchID, &bRemovedAny);
			if (bRemovedAny && m_uSearchID == searchlistctrl.m_nResultsID) // If this is the current search results tab, update the search list control.
				searchlistctrl.ReloadList(false, LSF_SELECTION);
			else if (bRemovedAny) // Otherwise, just update the tab header.
				searchlistctrl.UpdateTabHeader(m_uSearchID, EMPTY, false); // Update the tab header to reflect the changes.
		}
	}

	return m_uDeletedCount;
}

uint32 CSearchResultsWnd::CleanUpAllSearchResults()
{
	uint32 m_uDeletedCount = 0;
	const int m_iTabs = searchselect.GetItemCount();

	for (int iTab = 0; iTab < m_iTabs; ++iTab)
		m_uDeletedCount += CleanUpSearchResults(iTab);

	return m_uDeletedCount;
}

uint32 CSearchResultsWnd::RecheckAllSearchResults()
{
	const int m_iTabs = searchselect.GetItemCount();
	for (int iTab = 0; iTab < m_iTabs; ++iTab) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (searchselect.GetItem(iTab, &ti) && ti.lParam != NULL) {
			int m_uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
			theApp.searchlist->RecalculateSpamRatings(m_uSearchID, false, false, true);
		}
	}

	return m_iTabs;
}

LRESULT CSearchResultsWnd::OnProcessChunkedSearchCleanup(WPARAM, LPARAM)
{
	m_bChunkedSearchCleanupPending = false;
	if (!m_bChunkedSearchCleanupActive || theApp.IsClosing() || theApp.searchlist == NULL) {
		ClearChunkedSearchCleanup();
		return 0;
	}

	if (m_iNextChunkedSearchCleanupID >= static_cast<INT_PTR>(m_vecChunkedSearchCleanupIDs.size())) {
		FinishChunkedSearchCleanup();
		return 0;
	}

	const uint32 uSearchID = m_vecChunkedSearchCleanupIDs[static_cast<size_t>(m_iNextChunkedSearchCleanupID++)];
	bool bRemovedAny = false;
	m_uChunkedSearchCleanupDeleted += theApp.searchlist->RemoveCleanUpSearchResults(uSearchID, &bRemovedAny);
	if (bRemovedAny) {
		if (uSearchID == searchlistctrl.m_nResultsID)
			searchlistctrl.ReloadList(false, LSF_SELECTION);
		else
			searchlistctrl.UpdateTabHeader(uSearchID, EMPTY, false);
	}

	EnsureSearchTabActivityTimer();
	if (m_iNextChunkedSearchCleanupID >= static_cast<INT_PTR>(m_vecChunkedSearchCleanupIDs.size()))
		FinishChunkedSearchCleanup();
	else if (!ScheduleChunkedSearchCleanup())
		FinishChunkedSearchCleanup();
	return 0;
}

BOOL CSearchResultsWnd::MergeSearchResults(uint32 uFromSearchID, uint32 uToSearchID)
{
	if (uFromSearchID == uToSearchID)
		return FALSE; // Source and target tabs should be different.

	SearchList* pFromList = theApp.searchlist->GetSearchListForID(uFromSearchID);
	SearchList* pToList = theApp.searchlist->GetSearchListForID(uToSearchID);

	if (!pFromList || !pToList || pFromList == pToList)
		return FALSE; // No valid source or target search list, or both lists are the same.

	CWaitCursor curWait; // This may take a while, so show a wait cursor.
	bool bHasSourceParentItems = false;
	bool bSourceTabRetainedParentItems = false;
	for (POSITION pos = pFromList->GetHeadPosition(); pos != NULL;) {
		POSITION posCur = pos;
		CSearchFile* pFile = pFromList->GetNext(pos);
		const bool bWasParentItem = (pFile->GetListParent() == NULL);
		if (bWasParentItem)
			bHasSourceParentItems = true;
		pFile->SetSearchID(uToSearchID); // Set search ID to the target search list.

		if (theApp.searchlist->AddToList(pFile, pFile->GetDirectory() != NULL, 0, false)) { // Add the file to the target search list, if it was not already there. GetDirectory() only filled for shared files listings.
			pFromList->RemoveAt(posCur); // Remove the file from the source search list if it was added to the target search list.
		} else {
			pFile->SetSearchID(uFromSearchID); // Restore search ID if the file was not added to the target search list.
			if (bWasParentItem)
				bSourceTabRetainedParentItems = true;
		}
	}

	if (bHasSourceParentItems)
		theApp.searchlist->MarkSearchAsMerged(uToSearchID);

	if (bSourceTabRetainedParentItems)
		theApp.searchlist->MarkSearchAsMerged(uFromSearchID);

	if (pFromList->GetCount()) { // If the source search list is not empty after the merge, refresh the results in the source tab.
		if (uFromSearchID == searchlistctrl.m_nResultsID) // If this is the current search tab, reload the results.
			searchlistctrl.ReloadList(false, LSF_SELECTION);
		else // Otherwise just update the tab header.
			searchlistctrl.UpdateTabHeader(uFromSearchID, EMPTY, false);
	} else // Close source tab after the merge if it is empty.
		theApp.emuledlg->searchwnd->DeleteSearch(uFromSearchID);

	// Refresh the results in the target tab.
	if (uToSearchID == searchlistctrl.m_nResultsID)  // If this is the current search tab, reload the results.
		searchlistctrl.ReloadList(false, LSF_SELECTION);
	else // Otherwise just update the tab header.
		searchlistctrl.UpdateTabHeader(uToSearchID, EMPTY, false);

	return TRUE;
}

uint32 CSearchResultsWnd::MergeAllSearchResults()
{
	uint32 m_uSuccessCount = 1;
	const uint32 m_iTabs = searchselect.GetItemCount();
	if (m_iTabs > 1) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;

		int m_nToSearchID = -1; // Initialize to an invalid search ID
		if (searchselect.GetItem(0, &ti) && ti.lParam != NULL)
			m_nToSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
		if (m_nToSearchID < 0)
			return 0; // No valid search ID, nothing to merge.

		for (uint32 iTab = m_iTabs - 1; iTab > 0; --iTab)
			if (searchselect.GetItem(iTab, &ti) && ti.lParam != NULL)
				if (theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetItem(iTab, &ti) && ti.lParam != NULL && MergeSearchResults(reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID, m_nToSearchID))
					m_uSuccessCount++; // Increment success count if the merge was successful.
	}

	return m_uSuccessCount;
}
