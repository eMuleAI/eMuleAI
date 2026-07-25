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
#include <unordered_map>
#include "SearchListCtrl.h"
#include "emule.h"
#include "ResizableLib/ResizableSheet.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "emuledlg.h"
#include "MetaDataDlg.h"
#include "CommentDialogLst.h"
#include "SearchDlg.h"
#include "SearchParams.h"
#include "ClosableTabCtrl.h"
#include "PreviewDlg.h"
#include "Preview.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "MemDC.h"
#include "SharedFileList.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "KnownFileList.h"
#include "OtherFunctions.h"
#include "Ini2.h"
#include "StringConversion.h"
#include "MenuCmds.h"
#include "Opcodes.h"
#include "Packets.h"
#include "WebServices.h"
#include "Log.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "ServerConnect.h"
#include "server.h"
#include "MediaInfo.h"
#include "MuleStatusBarCtrl.h"
#include "TransferDlg.h"
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/DownloadValidator.h"
#include "MuleListCtrl.h"
#include "ListViewSearchDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define COLLAPSE_ONLY	0
#define EXPAND_ONLY		1
#define EXPAND_COLLAPSE	2

#define	TREE_WIDTH		10

LPARAM CSearchListCtrl::m_pSortParam = NULL;

namespace
{
	enum ESearchListColumn
	{
		colSearchFileName = 0,
		colSearchSize,
		colSearchType,
		colSearchLength,
		colSearchAvailability,
		colSearchCompleteSources,
		colSearchSimilarity,
		colSearchKnown,
		colSearchBitrate,
		colSearchCodec,
		colSearchFileId,
		colSearchFolder,
		colSearchAlbum,
		colSearchTitle,
		colSearchArtist,
		colSearchAichHash,
		colSearchSpamRating,
		colSearchCount
	};

	const EListStateField kSearchListViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kSearchListSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;
	const int kSearchListColumnLayoutVersion = 1;
	const int kOldToNewSearchListColumn[colSearchCount] =
	{
		colSearchFileName, colSearchSize, colSearchAvailability, colSearchCompleteSources, colSearchType, colSearchFileId,
		colSearchArtist, colSearchAlbum, colSearchTitle, colSearchLength, colSearchBitrate, colSearchCodec, colSearchFolder,
		colSearchKnown, colSearchAichHash, colSearchSpamRating, colSearchSimilarity
	};
	const int kOldSearchListColumnWidths[colSearchCount] = { 848, 84, 137, 104, 59, 228, 213, 100, 161, 66, 79, 114, 587, 116, 220, 65, 75 };
	const int kOldSearchListColumnHidden[colSearchCount] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0 };
	const int kOldSearchListColumnOrder[colSearchCount] = { 0, 1, 9, 2, 3, 10, 11, 13, 4, 5, 12, 7, 8, 6, 14, 15, 16 };
	const uint8 kSearchBottomGroupManualBlacklist = 0x01;
	const uint8 kSearchBottomGroupAutomaticBlacklist = 0x02;
	const uint8 kSearchBottomGroupSpam = 0x04;
	const uint8 kSearchBottomGroupDownloading = 0x08;

	uint64 BuildPossibleKnownRowIdentityKey(const SSearchListRow& row)
	{
		uint64 uKey = 1469598103934665603ui64;
		for (size_t i = 0; i < MDX_DIGEST_SIZE; ++i) {
			uKey ^= row.ucHash[i];
			uKey *= 1099511628211ui64;
		}
		const uint64 uFileSize = static_cast<uint64>(row.uSize);
		for (size_t i = 0; i < sizeof(uFileSize); ++i) {
			uKey ^= static_cast<uint8>((uFileSize >> (i * 8)) & 0xFF);
			uKey *= 1099511628211ui64;
		}
		for (int i = 0; i < row.strName.GetLength(); ++i) {
			uKey ^= static_cast<uint16>(row.strName[i]);
			uKey *= 1099511628211ui64;
		}
		return uKey;
	}

	bool IsPossibleKnownRowBetter(const SSearchListRow& first, const SSearchListRow& second)
	{
		if (first.uSimilarityScore != second.uSimilarityScore)
			return first.uSimilarityScore > second.uSimilarityScore;
		if (first.uSourceFlags != second.uSourceFlags)
			return first.uSourceFlags > second.uSourceFlags;
		return !first.strFolder.IsEmpty() && second.strFolder.IsEmpty();
	}

	bool ParseSearchListIntegerList(const CString& strData, std::vector<int>& values)
	{
		values.clear();
		int nOffset = 0;
		while (nOffset < strData.GetLength()) {
			CString strValue;
			const int nNextOffset = CIni::Parse(strData, nOffset, strValue);
			if (strValue.IsEmpty() || nNextOffset <= nOffset)
				return false;
			values.push_back(_tstoi(strValue));
			nOffset = nNextOffset;
		}
		return !values.empty();
	}

	CString FormatSearchListIntegerList(const std::vector<int>& values)
	{
		CString strData;
		for (size_t i = 0; i < values.size(); ++i) {
			if (i != 0)
				strData.AppendChar(_T(','));
			strData.AppendFormat(_T("%d"), values[i]);
		}
		return strData;
	}

	int RemapSearchListSortValue(int iSortValue)
	{
		const UINT uColumn = LOWORD(iSortValue);
		if (uColumn >= static_cast<UINT>(colSearchCount))
			return iSortValue;
		return MAKELONG(kOldToNewSearchListColumn[uColumn], HIWORD(iSortValue));
	}

	void MigrateSearchListIndexedSetting(CIni& ini, LPCTSTR pszKey, const int* piOldDefaults)
	{
		const CString strValue(ini.GetString(pszKey));
		if (strValue.IsEmpty())
			return;

		std::vector<int> values;
		if (!ParseSearchListIntegerList(strValue, values) || values.size() > static_cast<size_t>(colSearchCount)) {
			ini.DeleteKey(pszKey);
			return;
		}

		int aiOldValues[colSearchCount];
		memcpy(aiOldValues, piOldDefaults, sizeof aiOldValues);
		for (size_t i = 0; i < values.size(); ++i)
			aiOldValues[i] = values[i];

		std::vector<int> remappedValues(static_cast<size_t>(colSearchCount));
		for (int i = 0; i < colSearchCount; ++i)
			remappedValues[kOldToNewSearchListColumn[i]] = aiOldValues[i];
		ini.WriteString(pszKey, FormatSearchListIntegerList(remappedValues));
	}

	void MigrateSearchListColumnOrder(CIni& ini)
	{
		const CString strValue(ini.GetString(_T("SearchListCtrlColumnOrders")));
		if (strValue.IsEmpty())
			return;

		std::vector<int> oldOrder;
		if (!ParseSearchListIntegerList(strValue, oldOrder) || oldOrder.size() > static_cast<size_t>(colSearchCount)) {
			ini.DeleteKey(_T("SearchListCtrlColumnOrders"));
			return;
		}

		const size_t uStoredCount = oldOrder.size();
		bool abUsed[colSearchCount] = {};
		for (size_t i = 0; i < oldOrder.size(); ++i) {
			const int iColumn = oldOrder[i];
			if (iColumn < 0 || iColumn >= colSearchCount || abUsed[iColumn]) {
				ini.DeleteKey(_T("SearchListCtrlColumnOrders"));
				return;
			}
			abUsed[iColumn] = true;
		}
		for (int i = 0; i < colSearchCount; ++i) {
			if (!abUsed[i])
				oldOrder.push_back(i);
		}

		bool bOldDefaultOrder = uStoredCount >= 16;
		for (size_t i = 0; bOldDefaultOrder && i < uStoredCount; ++i)
			bOldDefaultOrder = oldOrder[i] == kOldSearchListColumnOrder[i];

		std::vector<int> newOrder(static_cast<size_t>(colSearchCount));
		if (bOldDefaultOrder) {
			for (int i = 0; i < colSearchCount; ++i)
				newOrder[i] = i;
		} else {
			for (int i = 0; i < colSearchCount; ++i)
				newOrder[i] = kOldToNewSearchListColumn[oldOrder[i]];
		}
		ini.WriteString(_T("SearchListCtrlColumnOrders"), FormatSearchListIntegerList(newOrder));
	}

	void MigrateSearchListColumnSettings()
	{
		CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));
		if (ini.GetInt(_T("SearchListCtrlColumnLayoutVersion"), 0) >= kSearchListColumnLayoutVersion)
			return;

		MigrateSearchListIndexedSetting(ini, _T("SearchListCtrlColumnWidths"), kOldSearchListColumnWidths);
		MigrateSearchListIndexedSetting(ini, _T("SearchListCtrlColumnHidden"), kOldSearchListColumnHidden);
		MigrateSearchListColumnOrder(ini);

		const CString strSortItem(ini.GetString(_T("SearchListCtrlTableSortItem")));
		if (!strSortItem.IsEmpty())
			ini.WriteInt(_T("SearchListCtrlTableSortItem"), RemapSearchListSortValue(_tstoi(strSortItem)));

		const CString strSortHistory(ini.GetString(_T("SearchListCtrlSortHistory")));
		if (!strSortHistory.IsEmpty()) {
			std::vector<int> sortHistory;
			if (ParseSearchListIntegerList(strSortHistory, sortHistory)) {
				for (size_t i = 0; i < sortHistory.size(); ++i)
					sortHistory[i] = RemapSearchListSortValue(sortHistory[i]);
				ini.WriteString(_T("SearchListCtrlSortHistory"), FormatSearchListIntegerList(sortHistory));
			} else
				ini.DeleteKey(_T("SearchListCtrlSortHistory"));
		}

		ini.WriteInt(_T("SearchListCtrlColumnLayoutVersion"), kSearchListColumnLayoutVersion);
	}

	int GetSearchCompleteState(bool bKademlia, bool bHasDirectory, UINT uSources, UINT uCompleteSources)
	{
		if (bKademlia)
			return -1;
		if (bHasDirectory && uSources == 1 && uCompleteSources == 0)
			return -1;
		if (uSources > 0 && uCompleteSources > 0)
			return 1;
		return 0;
	}

	CString BuildSearchRemoveHashKey(uint32 nSearchID, const uchar *pFileHash)
	{
		CString strKey;
		if (nSearchID != 0 && pFileHash != NULL)
			strKey.Format(_T("%u:%s"), nSearchID, (LPCTSTR)md4str(pFileHash));
		return strKey;
	}

	void UpdateSearchListItemCount(CListCtrl& listCtrl, const size_t itemCount)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), kSearchListSetItemCountFlags);
	}

	void FillSearchFallbackOwnerDataRow(CListCtrl& listCtrl, LPDRAWITEMSTRUCT lpDrawItemStruct)
	{
		if (lpDrawItemStruct == NULL || lpDrawItemStruct->hDC == NULL)
			return;
		CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
		if (pDC == NULL)
			return;
		CRect rcItem(lpDrawItemStruct->rcItem);
		CRect rcClient;
		listCtrl.GetClientRect(&rcClient);
		rcItem.left = rcClient.left;
		rcItem.right = rcClient.right;
		pDC->FillSolidRect(rcItem, GetCustomSysColor(COLOR_WINDOW));
	}

	bool ShouldApplySearchSpamOrBlacklistBottomGrouping()
	{
		return thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistManual() || thePrefs.GetBlacklistAutomatic();
	}

	bool IsSearchFileSpamOrBlacklistedBottomGroup(const CSearchFile* item)
	{
		return item != NULL && ShouldApplySearchSpamOrBlacklistBottomGrouping() && item->IsConsideredSpam(true);
	}

	uint8 GetSearchFileBottomGroupStatusFlags(const CSearchFile* item)
	{
		if (item == NULL)
			return 0;

		uint8 uFlags = 0;
		if (item->GetManualBlacklisted())
			uFlags |= kSearchBottomGroupManualBlacklist;
		if (item->GetAutomaticBlacklisted())
			uFlags |= kSearchBottomGroupAutomaticBlacklist;
		if (item->IsConsideredSpam(false))
			uFlags |= kSearchBottomGroupSpam;
		if (item->GetKnownType() == CSearchFile::Downloading)
			uFlags |= kSearchBottomGroupDownloading;
		return uFlags;
	}

	int GetSearchFileBottomGroupRank(const CSearchFile* item)
	{
		if (!thePrefs.GetGroupKnownAtTheBottom() || item == NULL)
			return 0;
		if (IsSearchFileSpamOrBlacklistedBottomGroup(item))
			return 2;
		return item->GetKnownType() != CSearchFile::NotDetermined ? 1 : 0;
	}

	int GetSearchKnownTieRank(uint8 uKnownType, bool bManualBlacklisted, bool bAutomaticBlacklisted, bool bConsideredSpam)
	{
		if (!thePrefs.GetGroupKnownAtTheBottom())
			return 0;
		if (bManualBlacklisted)
			return 5;
		if (bAutomaticBlacklisted || bConsideredSpam)
			return 6;

		switch (uKnownType) {
		case CSearchFile::Downloading:
			return 1;
		case CSearchFile::Shared:
			return 2;
		case CSearchFile::Downloaded:
			return 3;
		case CSearchFile::Cancelled:
			return 4;
		default:
			return 0;
		}
	}

	int GetSearchFileKnownTieRank(const CSearchFile* item)
	{
		if (item == NULL)
			return 0;
		return GetSearchKnownTieRank(static_cast<uint8>(item->GetKnownType()), item->GetManualBlacklisted(), item->GetAutomaticBlacklisted(), item->IsConsideredSpam(false));
	}

	int CompareSearchFixedGroupRank(int iRank1, int iRank2, bool bSortAscending)
	{
		const int iResult = CompareUnsigned(static_cast<uint32>(iRank1), static_cast<uint32>(iRank2));
		return bSortAscending ? iResult : -iResult;
	}

	CString GetSearchKnownTypeDisplayString(uint8 uKnownType, bool bManualBlacklisted, bool bAutomaticBlacklisted, bool bConsideredSpam)
	{
		LPCTSTR uid = EMPTY;
		switch (uKnownType) {
		case CSearchFile::Shared:
			uid = _T("SHARED");
			break;
		case CSearchFile::Downloading:
			uid = _T("DOWNLOADING");
			break;
		case CSearchFile::Downloaded:
			uid = _T("DOWNLOADED");
			break;
		case CSearchFile::Cancelled:
			uid = _T("CANCELLED");
			break;
		default:
			if (thePrefs.GetBlacklistManual() && bManualBlacklisted)
				uid = _T("MANUAL_BLACKLISTED");
			else if (thePrefs.GetBlacklistAutomatic() && bAutomaticBlacklisted)
				uid = _T("AUTOMATIC_BLACKLISTED");
			else if (thePrefs.IsSearchSpamFilterEnabled() && bConsideredSpam)
				uid = _T("SPAM");
		}
		return uid != NULL && uid[0] != _T('\0') ? GetResString(uid) : CString();
	}

	bool ShouldApplySearchBottomGrouping()
	{
		return thePrefs.GetGroupKnownAtTheBottom();
	}

	const CSearchFile* GetSearchFileSortGroupFile(const CSearchFile *item)
	{
		return item != NULL && item->GetListParent() != NULL ? item->GetListParent() : item;
	}

	bool IsSearchFileKnown(const CSearchFile *item)
	{
		if (item == NULL)
			return false;

		switch (item->GetKnownType()) {
		case CSearchFile::Shared:
		case CSearchFile::Downloading:
		case CSearchFile::Downloaded:
		case CSearchFile::Cancelled:
			return true;
		default:
			return false;
		}
	}

	bool IsSearchKnownColumnEmpty(const CSearchFile *item)
	{
		return item == NULL || (!IsSearchFileKnown(item)
			&& !(thePrefs.GetBlacklistManual() && item->GetManualBlacklisted())
			&& !(thePrefs.GetBlacklistAutomatic() && item->GetAutomaticBlacklisted())
			&& !(thePrefs.IsSearchSpamFilterEnabled() && item->IsConsideredSpam(false)));
	}

	bool GroupSearchItemsByBottomCandidates(std::vector<CSearchFile*> &items)
	{
		if (!ShouldApplySearchBottomGrouping() || items.size() < 2)
			return false;

		std::vector<CSearchFile*> normalItems;
		std::vector<CSearchFile*> knownItems;
		std::vector<CSearchFile*> blockedItems;
		normalItems.reserve(items.size());
		knownItems.reserve(items.size());
		blockedItems.reserve(items.size());

		for (size_t i = 0; i < items.size(); ++i) {
			CSearchFile *pFile = items[i];
			if (pFile == NULL)
				continue;
			const CSearchFile *pSortFile = GetSearchFileSortGroupFile(pFile);
			const int iRank = GetSearchFileBottomGroupRank(pSortFile);
			if (iRank >= 2)
				blockedItems.push_back(pFile);
			else if (iRank == 1 && thePrefs.GetGroupKnownAtTheBottom())
				knownItems.push_back(pFile);
			else if (iRank == 1)
				blockedItems.push_back(pFile);
			else
				normalItems.push_back(pFile);
		}

		const int iNonEmptyBucketCount = (normalItems.empty() ? 0 : 1) + (knownItems.empty() ? 0 : 1) + (blockedItems.empty() ? 0 : 1);
		if (iNonEmptyBucketCount < 2)
			return false;

		items.clear();
		items.reserve(normalItems.size() + knownItems.size() + blockedItems.size());
		items.insert(items.end(), normalItems.begin(), normalItems.end());
		items.insert(items.end(), knownItems.begin(), knownItems.end());
		items.insert(items.end(), blockedItems.begin(), blockedItems.end());
		return true;
	}

	void RebuildPreviewMenu(CMenuXP& menu, const CPartFile* file, bool bEnablePreview, bool bEnablePauseOnPreview, bool bPauseOnPreviewChecked, bool bEnablePreviewParts, bool bPreviewPartsChecked, LPCTSTR pszCompleteFilePath)
	{
		while (menu.GetMenuItemCount() > 0)
			menu.RemoveMenu(0, MF_BYPOSITION);

		CString strPrimaryCommand = thePrefs.GetVideoPlayer();
		if (file != NULL) {
			const int iPreviewApp = thePreviewApps.GetPreviewApp(file);
			if (iPreviewApp >= 0)
				strPrimaryCommand = thePreviewApps.GetPreviewAppCmd(iPreviewApp);
		}

		const CString strPrimaryLabel = thePreviewApps.GetPreviewAppDisplayNameByCommand(strPrimaryCommand);
		menu.AppendODMenu(MF_STRING | (bEnablePreview ? MF_ENABLED : MF_GRAYED), MP_PREVIEW, new CMenuXPText(MP_PREVIEW, strPrimaryLabel.IsEmpty() ? GetResStringWithAccel(_T("PREVIEW_AVAILABLE"), _T('v')) : strPrimaryLabel, thePreviewApps.GetPreviewCommandIcon(strPrimaryCommand)));
		if (file != NULL || pszCompleteFilePath == NULL || *pszCompleteFilePath == _T('\0'))
			thePreviewApps.GetAllMenuEntries(menu, file, strPrimaryCommand);
		else
			thePreviewApps.GetAllMenuEntriesForFilePath(menu, pszCompleteFilePath, strPrimaryCommand);
		menu.AppendMenu(MF_SEPARATOR);
		if (!thePrefs.GetPreviewPrio()) {
			menu.AppendMenu(MF_STRING | (bEnablePreviewParts ? MF_ENABLED : MF_GRAYED), MP_TRY_TO_GET_PREVIEW_PARTS, GetResString(_T("DL_TRY_TO_GET_PREVIEW_PARTS")));
			menu.CheckMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, bPreviewPartsChecked ? MF_CHECKED : MF_UNCHECKED);
		}
		menu.AppendMenu(MF_STRING | (bEnablePauseOnPreview ? MF_ENABLED : MF_GRAYED), MP_PAUSEONPREVIEW, GetResString(_T("PAUSEONPREVIEW")));
		menu.CheckMenuItem(MP_PAUSEONPREVIEW, bPauseOnPreviewChecked ? MF_CHECKED : MF_UNCHECKED);
	}
}

//////////////////////////////////////////////////////////////////////////////
// CSearchResultFileDetailSheet

class CSearchResultFileDetailSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CSearchResultFileDetailSheet)

	void Localize();
public:
	CSearchResultFileDetailSheet(CTypedPtrList<CPtrList, CSearchFile*> &paFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

protected:
	CMetaDataDlg m_wndMetaData;
	CCommentDialogLst m_wndComments;
	CClosableTabCtrl			m_tabDark;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CSearchResultFileDetailSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CSearchResultFileDetailSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CSearchResultFileDetailSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

void CSearchResultFileDetailSheet::Localize()
{
	m_wndMetaData.Localize();
	SetTabTitle(_T("META_DATA"), &m_wndMetaData, this);
	m_wndComments.Localize();
	SetTabTitle(_T("COMMENT"), &m_wndComments, this);
}

CSearchResultFileDetailSheet::CSearchResultFileDetailSheet(CTypedPtrList<CPtrList, CSearchFile*> &paFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = paFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(paFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_NOAPPLYNOW;

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (thePrefs.IsExtControlsEnabled() && m_aItems.GetSize() == 1) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	m_wndComments.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndComments.m_psp.dwFlags |= PSP_USEICONID;
	m_wndComments.m_psp.pszIcon = _T("FileComments");
	m_wndComments.SetFiles(&m_aItems);
	AddPage(&m_wndComments);

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetSize(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CSearchResultFileDetailSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CSearchResultFileDetailSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("SearchResultFileDetailsSheet")); // call this after(!) OnInitDialog
	Localize();
	UpdateTitle();

	m_tabDark.m_bClosable = false;

	if (IsDarkModeEnabled()) {
		HWND hTab = PropSheet_GetTabControl(m_hWnd);
		if (hTab != NULL) {
			::SetWindowTheme(hTab, _T(""), _T(""));
			m_tabDark.SubclassWindow(hTab);
		}
	}

	return bResult;
}

LRESULT CSearchResultFileDetailSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	return 1;
}

void CSearchResultFileDetailSheet::UpdateTitle()
{
	CString sTitle(GetResString(_T("DETAILS")));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CSearchFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}


//////////////////////////////////////////////////////////////////////////////
// CSearchListCtrl

IMPLEMENT_DYNAMIC(CSearchListCtrl, CMuleListCtrl)

namespace
{
	const UINT_PTR kTimerChunkedSearchRemove = 0x5E71;
	const UINT_PTR kTimerDeferredSearchReload = 0x5E72;
	const UINT_PTR kTimerPossibleKnownAvailability = 0x5E73;
	const UINT kDeferredSearchReloadDelayMs = 250;
	const UINT kPossibleKnownAvailabilityDelayMs = 10;
	const UINT kPossibleKnownAvailabilityWaitForRemoveMs = 50;
	const DWORD kPossibleKnownAvailabilitySliceMs = 5;
	const UINT kPossibleKnownAvailabilityItemsPerSlice = 16;
	const size_t kClientSharedFilesAutoSortThreshold = 1000;
}


SSearchListRow::SSearchListRow()
	: eType(SearchListRowSearchFile)
	, pSearchFile(NULL)
	, pParentSearchFile(NULL)
	, nSearchID(0)
	, uSize(static_cast<uint64>(0))
	, uMediaLengthSec(0)
	, uMediaBitrateKbps(0)
	, uSimilarityScore(0)
	, uFileType(static_cast<uint8>(ED2KFT_ANY))
	, uSourceFlags(CDownloadValidator::FuzzyFileSourceUnknown)
	, uBottomGroupStatusFlags(0)
{
	md4clr(ucHash);
}

BEGIN_MESSAGE_MAP(CSearchListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_DELETEALLITEMS, OnLvnDeleteAllItems)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(LVN_KEYDOWN, OnLvnKeyDown)
	ON_NOTIFY_REFLECT(NM_CLICK, OnNmClick)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
	ON_WM_KEYDOWN()
	ON_WM_TIMER()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_SHOWWINDOW()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CSearchListCtrl::CSearchListCtrl()
	: CListCtrlItemWalk(this)
	, searchlist()
	, m_crSearchResultDownloading()
	, m_crSearchResultDownloadStopped()
	, m_crSearchResultKnown()
	, m_crSearchResultSharing()
	, m_crSearchResultCancelled()
	, m_crPossibleKnownHeader()
	, m_crShades()
	, m_nResultsID()
	, m_iDataSize(-1)
	, m_iNextChunkedSearchRemoveItem(0)
	, m_uChunkedSearchRemoveProcessed(0)
	, m_uChunkedSearchRemoveStale(0)
	, m_uChunkedSearchRemoveFailed(0)
	, m_dwChunkedSearchRemoveStartedTick(0)
	, m_dwChunkedSearchRemoveLastProgressTick(0)
	, m_bChunkedSearchRemoveActive(false)
	, m_bDeferredSearchReloadPending(false)
	, m_bDeferredSearchReloadSort(false)
	, m_bDeferredSearchReloadKeepPendingWhileInactive(false)
	, m_eDeferredSearchReloadState(kSearchListViewState)
	, m_lListedItemsModelSequence(0)
	, m_uPossibleKnownRevision(0)
	, m_uPossibleKnownCandidateDataRevision(0)
{
	SetGeneralPurposeFind(true);
	m_eFileSizeFormat = (EFileSizeFormat)theApp.GetProfileInt(_T("eMule"), _T("SearchResultsFileSizeFormat"), fsizeDefault);
	SetSkinKey(_T("SearchResultsLv"));
}

void CSearchListCtrl::OnDestroy()
{
	ClearChunkedSearchRemoveItems(false);
	ClearPossibleKnownAvailabilityQueue();
	KillTimer(kTimerDeferredSearchReload);
	m_bDeferredSearchReloadPending = false;
	m_bDeferredSearchReloadKeepPendingWhileInactive = false;
	theApp.WriteProfileInt(_T("eMule"), _T("SearchResultsFileSizeFormat"), m_eFileSizeFormat);
	__super::OnDestroy();
}


void CSearchListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kTimerDeferredSearchReload) {
		KillTimer(kTimerDeferredSearchReload);
		if (m_bDeferredSearchReloadPending) {
			if (theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible()) {
				SetTimer(kTimerDeferredSearchReload, 500, NULL);
				return;
			}
			if (theApp.emuledlg != NULL && theApp.emuledlg->activewnd == theApp.emuledlg->searchwnd && !IsWindowVisible()) {
				SetTimer(kTimerDeferredSearchReload, 200, NULL);
				return;
			}
			if (theApp.emuledlg != NULL && theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd) {
				if (m_bDeferredSearchReloadKeepPendingWhileInactive && SetTimer(kTimerDeferredSearchReload, 500, NULL) != 0)
					return;
				m_bDeferredSearchReloadPending = false;
				m_bDeferredSearchReloadSort = false;
				m_bDeferredSearchReloadKeepPendingWhileInactive = false;
				return;
			}
			const bool bSortCurrentList = m_bDeferredSearchReloadSort;
			const EListStateField eState = m_eDeferredSearchReloadState;
			m_bDeferredSearchReloadPending = false;
			m_bDeferredSearchReloadSort = false;
			m_bDeferredSearchReloadKeepPendingWhileInactive = false;
			ReloadList(bSortCurrentList, eState);
		}
		return;
	}


	if (nIDEvent == kTimerPossibleKnownAvailability) {
		KillTimer(kTimerPossibleKnownAvailability);
		ProcessPossibleKnownAvailability();
		return;
	}

	if (nIDEvent == kTimerChunkedSearchRemove) {
		ProcessChunkedSearchRemoveItems();
		if (m_bChunkedSearchRemoveActive && ::IsWindow(m_hWnd) && SetTimer(kTimerChunkedSearchRemove, 1, NULL) == 0) {
			AddDebugLogLine(DLP_HIGH, false, _T("Chunked search result remove aborted because the continuation timer could not be restarted. processed=%u remaining=%d\n"), m_uChunkedSearchRemoveProcessed, static_cast<int>(m_vecChunkedSearchRemoveItems.size() - static_cast<size_t>(m_iNextChunkedSearchRemoveItem)));
			FinishChunkedSearchRemoveItems(true);
		}
		return;
	}

	__super::OnTimer(nIDEvent);
}

void CSearchListCtrl::SetStyle()
{
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
}

void CSearchListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("EMPTY"))); //0
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated"))); //1
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake"))); //2
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor"))); //3
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair"))); //4
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good"))); //5
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent"))); //6
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //7 rating for comments are searched on kad
	m_ImageList.Add(CTempIconLoader(_T("SPAM"))); //8 spam indicator
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("FileCommentsOvl"))), 1);
	m_ImageList.Add(CTempIconLoader(_T("SPAM_PINK"))); //10 pink indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_PURPLE"))); //11 purple indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_YELLOW"))); //12 yellow indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_GREEN"))); //13 green indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_DARK_GREEN"))); //14 blue indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_ORANGE"))); //15 blue indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_BLUE"))); //16 blue indicator
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	VERIFY(ApplyImageList(m_ImageList) == NULL);

	// NOTE: There is another image list applied to this particular listview control!
	// See also the 'Init' function.
}

void CSearchListCtrl::Init(CSearchList *in_searchlist)
{
	SetPrefsKey(_T("SearchListCtrl"));
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);
	SetStyle();

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SetFileIconToolTip(true);
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
	}
	searchlist = in_searchlist;

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(colSearchFileName,			EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);			//DL_FILENAME
	InsertColumn(colSearchSize,				EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);				//DL_SIZE
	InsertColumn(colSearchType,				EMPTY,	LVCFMT_LEFT,	DFLT_FILETYPE_COL_WIDTH);			//TYPE
	InsertColumn(colSearchLength,			EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);				//LENGTH
	InsertColumn(colSearchAvailability,		EMPTY,	LVCFMT_RIGHT,	60);								//SEARCHAVAIL
	InsertColumn(colSearchCompleteSources,	EMPTY,	LVCFMT_RIGHT,	70);								//COMPLSOURCES
	InsertColumn(colSearchSimilarity,		EMPTY,	LVCFMT_RIGHT,	75);								//SIMILARITY
	InsertColumn(colSearchKnown,				EMPTY,	LVCFMT_LEFT,	50);								//KNOWN
	InsertColumn(colSearchBitrate,			EMPTY,	LVCFMT_RIGHT,	DFLT_BITRATE_COL_WIDTH);			//BITRATE
	InsertColumn(colSearchCodec,				EMPTY,	LVCFMT_LEFT,	DFLT_CODEC_COL_WIDTH);				//CODEC
	InsertColumn(colSearchFileId,			EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//FILEID
	InsertColumn(colSearchFolder,			EMPTY,	LVCFMT_LEFT,	DFLT_FOLDER_COL_WIDTH, -1, true);	//FOLDER
	InsertColumn(colSearchAlbum,				EMPTY,	LVCFMT_LEFT,	DFLT_ALBUM_COL_WIDTH);				//ALBUM
	InsertColumn(colSearchTitle,				EMPTY,	LVCFMT_LEFT,	DFLT_TITLE_COL_WIDTH);				//TITLE
	InsertColumn(colSearchArtist,			EMPTY,	LVCFMT_LEFT,	DFLT_ARTIST_COL_WIDTH);				//ARTIST
	InsertColumn(colSearchAichHash,			EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//AICHHASH
	InsertColumn(colSearchSpamRating,		EMPTY,	LVCFMT_RIGHT,	65, -1, true);						//SPAM_RATING

	SetAllIcons();

	// This states image list with that particular width is only there to let the listview control
	// auto-size the column width properly (double clicking on header divider). The items in the
	// list view contain a file type icon and optionally also a 'tree' icon (in case there are
	// more search entries related to one file hash). The width of that 'tree' icon (even if it is
	// not drawn) has to be known by the default list view control code to determine the total width
	// needed to show a particular item. The image list itself can be even empty, it is used by
	// the listview control just for querying the width of on image in the list, even if that image
	// was never added.
	CImageList imlDummyStates;
	imlDummyStates.Create(TREE_WIDTH, 16, ILC_COLOR, 0, 0);
	CImageList *pOldStates = SetImageList(&imlDummyStates, LVSIL_STATE);
	imlDummyStates.Detach();
	if (pOldStates)
		pOldStates->DeleteImageList();

	CreateMenus();

	MigrateSearchListColumnSettings();
	LoadSettings();
	SetHighlightColors();

	SetSortArrow();
	m_pSortParam = MAKELONG(GetSortItem(), !GetSortAscending());
	UpdateSortHistory(m_pSortParam); // This will save sort parameter history in m_liSortHistory which will be used when we call GetNextSortOrder.
	
}


CSearchListCtrl::~CSearchListCtrl()
{
	ClearListedItems(true);
}
CSearchListCtrl::SChunkedSearchRemoveItem::SChunkedSearchRemoveItem()
	: nSearchID(0)
	, bChild(false)
{
	md4clr(abyFileHash);
}

bool CSearchListCtrl::BuildChunkedSearchRemoveItem(const CSearchFile *pFile, SChunkedSearchRemoveItem &item) const
{
	item = SChunkedSearchRemoveItem();
	if (pFile == NULL)
		return false;
	item.nSearchID = pFile->GetSearchID();
	md4cpy(item.abyFileHash, pFile->GetFileHash());
	item.bChild = pFile->GetListParent() != NULL;
	item.strFileName = pFile->GetFileName();
	return item.nSearchID != 0 && !isnulmd4(item.abyFileHash);
}

bool CSearchListCtrl::ResolveChunkedSearchRemoveItem(const SChunkedSearchRemoveItem &item, SSearchResultId &id, CSearchFile *&pFile) const
{
	pFile = NULL;
	id.Clear();
	if (searchlist == NULL || item.nSearchID == 0 || isnulmd4(item.abyFileHash))
		return false;
	id.Set(item.nSearchID, item.abyFileHash, item.bChild, item.strFileName);
	if (!id.IsValid())
		return false;
	pFile = searchlist->GetSearchFileByResultRow(id, item.bChild, item.strFileName);
	return pFile != NULL && id.EqualsRow(pFile->GetSearchID(), pFile->GetFileHash(), pFile->GetListParent() != NULL, pFile->GetFileName());
}

void CSearchListCtrl::ClearChunkedSearchRemoveItems(bool bReloadVisibleList)
{
	if (::IsWindow(m_hWnd))
		KillTimer(kTimerChunkedSearchRemove);
	m_vecChunkedSearchRemoveItems.clear();
	m_iNextChunkedSearchRemoveItem = 0;
	m_bChunkedSearchRemoveActive = false;
	m_uChunkedSearchRemoveProcessed = 0;
	m_uChunkedSearchRemoveStale = 0;
	m_uChunkedSearchRemoveFailed = 0;
	m_dwChunkedSearchRemoveStartedTick = 0;
	m_dwChunkedSearchRemoveLastProgressTick = 0;
	if (bReloadVisibleList && ::IsWindow(m_hWnd) && !theApp.IsClosing())
		ReloadList(false, kSearchListViewState);
}

void CSearchListCtrl::StartChunkedRemoveSelectedSearchResults(CTypedPtrList<CPtrList, CSearchFile*> &selectedList)
{
	ClearChunkedSearchRemoveItems(false);
	CMapPtrToPtr mapSelectedRows;
	CMapStringToPtr mapSelectedParentHashes;
	for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
		CSearchFile *pFile = selectedList.GetNext(pos);
		if (pFile == NULL)
			continue;
		SChunkedSearchRemoveItem item;
		if (BuildChunkedSearchRemoveItem(pFile, item)) {
			m_vecChunkedSearchRemoveItems.push_back(item);
			mapSelectedRows.SetAt(pFile, reinterpret_cast<void*>(1));
			if (pFile->GetListParent() == NULL) {
				const CString strParentHashKey(BuildSearchRemoveHashKey(pFile->GetSearchID(), pFile->GetFileHash()));
				if (!strParentHashKey.IsEmpty())
					mapSelectedParentHashes.SetAt(strParentHashKey, reinterpret_cast<void*>(1));
			}
		}
	}

	if (m_vecChunkedSearchRemoveItems.empty())
		return;

	SaveListState(m_nResultsID, kSearchListViewState);
	SetRedraw(false);
	ClearPossibleKnownRows();
	std::vector<SSearchListRow*> keptItems;
	keptItems.reserve(m_ListedItemsVector.size());
	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		SSearchListRow* pRow = m_ListedItemsVector[i];
		CSearchFile* pListedFile = pRow != NULL && pRow->eType == SearchListRowSearchFile ? pRow->pSearchFile : NULL;
		if (pListedFile == NULL)
			continue;
		void *pDummy = NULL;
		if (mapSelectedRows.Lookup(pListedFile, pDummy))
			continue;
		const CString strListedHashKey(BuildSearchRemoveHashKey(pListedFile->GetSearchID(), pListedFile->GetFileHash()));
		if (!strListedHashKey.IsEmpty() && mapSelectedParentHashes.Lookup(strListedHashKey, pDummy))
			continue;
		keptItems.push_back(pRow);
	}
	m_ListedItemsVector.swap(keptItems);
	RebuildListedItemsMap();
	UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
	RestoreListState(m_nResultsID, kSearchListViewState, false);
	SetRedraw(true);
	Invalidate(FALSE);

	m_bChunkedSearchRemoveActive = true;
	m_iNextChunkedSearchRemoveItem = 0;
	m_uChunkedSearchRemoveProcessed = 0;
	m_uChunkedSearchRemoveStale = 0;
	m_uChunkedSearchRemoveFailed = 0;
	m_dwChunkedSearchRemoveStartedTick = ::GetTickCount();
	m_dwChunkedSearchRemoveLastProgressTick = m_dwChunkedSearchRemoveStartedTick;
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();

	if (SetTimer(kTimerChunkedSearchRemove, 1, NULL) == 0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked search result remove aborted because the continuation timer could not be created. total=%u\n"), static_cast<UINT>(m_vecChunkedSearchRemoveItems.size()));
		FinishChunkedSearchRemoveItems(true);
	}
}

void CSearchListCtrl::ProcessChunkedSearchRemoveItems()
{
	if (!m_bChunkedSearchRemoveActive)
		return;
	if (theApp.IsClosing() || searchlist == NULL) {
		FinishChunkedSearchRemoveItems(true);
		return;
	}

	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	while (m_iNextChunkedSearchRemoveItem < static_cast<INT_PTR>(m_vecChunkedSearchRemoveItems.size())) {
		const SChunkedSearchRemoveItem &item = m_vecChunkedSearchRemoveItems[static_cast<size_t>(m_iNextChunkedSearchRemoveItem++)];
		SSearchResultId id;
		CSearchFile *pFile = NULL;
		if (ResolveChunkedSearchRemoveItem(item, id, pFile)) {
			RemoveCachedSearchRowsForFile(pFile);
			searchlist->RemoveResult(pFile);
			++m_uChunkedSearchRemoveProcessed;
		} else
			++m_uChunkedSearchRemoveStale;
		++uProcessedInSlice;

		const DWORD dwNow = ::GetTickCount();
		if (static_cast<DWORD>(dwNow - m_dwChunkedSearchRemoveLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchResultRemove)) {
			m_dwChunkedSearchRemoveLastProgressTick = dwNow;
			AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked search result remove progress. processed=%u stale=%u failed=%u remaining=%d\n"), m_uChunkedSearchRemoveProcessed, m_uChunkedSearchRemoveStale, m_uChunkedSearchRemoveFailed, static_cast<int>(m_vecChunkedSearchRemoveItems.size() - static_cast<size_t>(m_iNextChunkedSearchRemoveItem)));
		}

		if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchResultRemove))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchResultRemove, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchResultRemove, _T("ProcessChunkedSearchRemoveItems"), dwSliceElapsed, uProcessedInSlice, static_cast<INT_PTR>(m_vecChunkedSearchRemoveItems.size()) - m_iNextChunkedSearchRemoveItem);

	if (m_iNextChunkedSearchRemoveItem >= static_cast<INT_PTR>(m_vecChunkedSearchRemoveItems.size()))
		FinishChunkedSearchRemoveItems(false);
	else if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSearchListCtrl::FinishChunkedSearchRemoveItems(bool bAborted)
{
	if (::IsWindow(m_hWnd))
		KillTimer(kTimerChunkedSearchRemove);
	const UINT uTotal = static_cast<UINT>(m_vecChunkedSearchRemoveItems.size());
	const DWORD dwElapsed = m_dwChunkedSearchRemoveStartedTick != 0 ? static_cast<DWORD>(::GetTickCount() - m_dwChunkedSearchRemoveStartedTick) : 0;
	AddDebugLogLine(DLP_LOW, false, _T("Chunked search result remove %s. processed=%u stale=%u failed=%u total=%u elapsed=%u\n"), bAborted ? _T("aborted") : _T("completed"), m_uChunkedSearchRemoveProcessed, m_uChunkedSearchRemoveStale, m_uChunkedSearchRemoveFailed, uTotal, dwElapsed);
	m_vecChunkedSearchRemoveItems.clear();
	m_iNextChunkedSearchRemoveItem = 0;
	m_bChunkedSearchRemoveActive = false;
	m_uChunkedSearchRemoveProcessed = 0;
	m_uChunkedSearchRemoveStale = 0;
	m_uChunkedSearchRemoveFailed = 0;
	m_dwChunkedSearchRemoveStartedTick = 0;
	m_dwChunkedSearchRemoveLastProgressTick = 0;
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	if (!theApp.IsClosing() && ::IsWindow(m_hWnd)) {
		ReloadList(false, kSearchListViewState);
		if (!bAborted)
			AutoSelectItem();
	}
}


void CSearchListCtrl::CancelActiveChunkedSearchOperation()
{
	ClearChunkedSearchRemoveItems(true);
	HideOperationOverlay();
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}


void CSearchListCtrl::OnOperationOverlayCancel()
{
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->CancelActiveBulkOperations();
}

void CSearchListCtrl::Localize()
{
	static const LPCTSTR uids[colSearchCount] =
	{
		_T("DL_FILENAME"), _T("DL_SIZE"), _T("TYPE"), _T("LENGTH"), 0/*SEARCHAVAIL*/
		, _T("COMPLSOURCES"), _T("DOWNLOAD_VALIDATOR_SIMILARITY"), _T("KNOWN"), _T("BITRATE")
		, _T("CODEC"), _T("FILEID"), _T("FOLDER"), _T("ALBUM"), _T("TITLE"), _T("ARTIST")
		, _T("AICHHASH"), _T("SPAM_RATING")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	HDITEM hdi;
	hdi.mask = HDI_TEXT;
	CString strRes(GetResString(_T("SEARCHAVAIL")));
	if (thePrefs.IsExtControlsEnabled())
		strRes.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("DL_SOURCES"))); //modify "availability" header
	hdi.pszText = (LPTSTR)(LPCTSTR)strRes;
	GetHeaderCtrl()->SetItem(colSearchAvailability, &hdi);

	CreateMenus();
}

void CSearchListCtrl::AddResult(CSearchFile* toshow)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	// Ignore hidden children of collapsed parents
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toshow == NULL
		|| toshow->GetSearchID() != m_nResultsID || !ShouldShowSearchItemInList(toshow) || (m_SearchItemsMap.Lookup(toshow, m_iIndex) && m_iIndex >= 0))
		return;

	SaveListState(m_nResultsID, kSearchListViewState); // Save selections and scroll state
	if (theApp.searchlist == NULL)
		return;

	SetRedraw(false); // Suspend painting

	// Determine insert position
	int insertPos = (int)m_ListedItemsVector.size();
	if (toshow->GetListParent()) {
		int parentIdx;
		if (m_SearchItemsMap.Lookup(toshow->GetListParent(), parentIdx))
			insertPos = parentIdx + 1;
	}
	m_ListedItemsVector.insert(m_ListedItemsVector.begin() + insertPos, GetOrCreateSearchRow(toshow));
	if (toshow->GetListParent() == NULL && IsPossibleKnownFeatureEnabled())
		QueuePossibleKnownAvailability(toshow);

	SortListedItemsRaw();
	GroupListedItemsByBottomCandidates();
	RebuildListedItemsMap(); // Rebuild the map after sorting.
	UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(m_nResultsID, kSearchListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	Invalidate(FALSE);
}

void CSearchListCtrl::RemoveResult(CSearchFile* toremove, bool bUpdateTabCount)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toremove == NULL)
		return;
	if (toremove->GetSearchID() != m_nResultsID || !m_SearchItemsMap.Lookup(toremove, m_iIndex) || m_iIndex < 0)
		return;

	SaveListState(m_nResultsID, kSearchListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	RemoveCachedSearchRowsForFile(toremove);
	GroupListedItemsByBottomCandidates();
	RebuildListedItemsMap();

	UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(m_nResultsID, kSearchListViewState, false); // Restore selections and scroll state
	SetRedraw(true);// resume painting
	Invalidate(FALSE);

	if (bUpdateTabCount)
		UpdateTabHeader(m_nResultsID, EMPTY, false);
}

void CSearchListCtrl::UpdateSources(CSearchFile* toupdate, const bool bSort)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	// Ignore hidden children of collapsed parents
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toupdate == NULL
		|| toupdate->GetSearchID() != m_nResultsID || (toupdate->GetListParent() && !toupdate->GetListParent()->IsListExpanded())
		|| !m_SearchItemsMap.Lookup(toupdate, m_iIndex) || m_iIndex < 0)
		return;

	if (toupdate->GetListParent() == NULL && IsPossibleKnownFeatureEnabled() && searchlist != NULL) {
		std::vector<CString> astrFileNames;
		uint32 uCurrentAliasFingerprint = 0;
		if (searchlist->BuildPossibleKnownAliasNames(toupdate, astrFileNames, uCurrentAliasFingerprint)) {
			std::map<CSearchFile*, SPossibleKnownCacheEntry>::const_iterator itCache = m_PossibleKnownCache.find(toupdate);
			if (itCache != m_PossibleKnownCache.end() && itCache->second.uAliasFingerprint != uCurrentAliasFingerprint) {
				const bool bLoadRows = itCache->second.bRowsLoaded || !itCache->second.rows.empty() || toupdate->IsListExpanded();
				QueuePossibleKnownAvailability(toupdate, bLoadRows, true);
			}
		}
	}

	int iRedrawFirst = m_iIndex;
	int iRedrawLast = m_iIndex;

	// Update child items
	if (toupdate->IsListExpanded()) {
		const SearchChildList* pChildren = theApp.searchlist->GetSearchChildrenForParent(toupdate);
		if (pChildren != NULL) {
			for (POSITION pos = pChildren->GetHeadPosition(); pos != NULL;) {
				CSearchFile* cur = pChildren->GetNext(pos);
				int childIdx;
				if (cur != NULL && m_SearchItemsMap.Lookup(cur, childIdx)) {
					iRedrawFirst = min(iRedrawFirst, childIdx);
					iRedrawLast = max(iRedrawLast, childIdx);
				}
			}
		}
	}

	if (bSort) {
		SaveListState(m_nResultsID, kSearchListViewState);
		SetRedraw(false);
		SortListedItemsRaw();
		GroupListedItemsByBottomCandidates();
		RebuildListedItemsMap();
		UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
		RestoreListState(m_nResultsID, kSearchListViewState, false);
		SetRedraw(true);
		RequestFullRedrawAsync();
	} else {
		RequestRowRedrawAsync(iRedrawFirst, iRedrawLast);
		MarkListedModelCurrent();
	}
}

void CSearchListCtrl::MarkListedModelCurrent()
{
	m_lListedItemsModelSequence = searchlist != NULL ? searchlist->GetSearchModelSequence() : 0;
}

bool CSearchListCtrl::IsListedModelCurrent(uint32 nSearchID) const
{
	return searchlist != NULL && m_nResultsID == nSearchID && m_lListedItemsModelSequence == searchlist->GetSearchModelSequence();
}

void CSearchListCtrl::UpdateSearch(CSearchFile* toupdate)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	// Ignore hidden children of collapsed parents
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toupdate == NULL || toupdate->GetSearchID() != m_nResultsID || !m_SearchItemsMap.Lookup(toupdate, m_iIndex) || m_iIndex < 0)
		return;

	RequestRowRedrawAsync(m_iIndex, m_iIndex);
	MarkListedModelCurrent();
}

bool CSearchListCtrl::ShouldShowSearchItemInList(const CSearchFile *pSearchFile) const
{
	return pSearchFile != NULL && !IsFilteredOut(pSearchFile) && (pSearchFile->GetListParent() == NULL || pSearchFile->GetListParent()->IsListExpanded());
}

void CSearchListCtrl::BuildVisibleSearchItems(const CTypedPtrList<CPtrList, CSearchFile*> &sourceList, std::vector<SSearchListRow*> &visibleItems)
{
	ClearPossibleKnownRows();
	ClearPossibleKnownAvailabilityQueue();
	visibleItems.clear();
	visibleItems.reserve(static_cast<size_t>(sourceList.GetCount()));

	std::map<CSearchFile*, bool> activeSearchRows;
	for (POSITION pos = sourceList.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pCurFile = sourceList.GetNext(pos);
		if (pCurFile != NULL)
			activeSearchRows[pCurFile] = true;
	}
	std::vector<SSearchListRow*> staleRows;
	for (std::map<CSearchFile*, SSearchListRow*>::const_iterator it = m_SearchRows.begin(); it != m_SearchRows.end(); ++it) {
		SSearchListRow* pRow = it->second;
		if (pRow != NULL && pRow->nSearchID == m_nResultsID && activeSearchRows.find(it->first) == activeSearchRows.end())
			staleRows.push_back(pRow);
	}
	RemoveRowsFromSavedStates(staleRows);
	for (std::map<CSearchFile*, SSearchListRow*>::iterator it = m_SearchRows.begin(); it != m_SearchRows.end();) {
		SSearchListRow* pRow = it->second;
		if (pRow != NULL && pRow->nSearchID == m_nResultsID && activeSearchRows.find(it->first) == activeSearchRows.end()) {
			m_ListedItemsMap.RemoveKey(pRow);
			m_PossibleKnownCache.erase(it->first);
			delete pRow;
			it = m_SearchRows.erase(it);
		} else
			++it;
	}

	for (POSITION pos = sourceList.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pCurFile = sourceList.GetNext(pos);
		if (!ShouldShowSearchItemInList(pCurFile))
			continue;
		visibleItems.push_back(GetOrCreateSearchRow(pCurFile));
		if (pCurFile->GetListParent() == NULL && IsPossibleKnownFeatureEnabled())
			QueuePossibleKnownAvailability(pCurFile);
	}
}

void CSearchListCtrl::CollectSearchDownloadItems(uint32 nSearchID, bool bOnlyUnknown, CTypedPtrList<CPtrList, CSearchFile*> &downloadItems) const
{
	downloadItems.RemoveAll();
	if (nSearchID == 0 || searchlist == NULL)
		return;

	std::vector<CSearchFile*> items;
	{
		CSingleLock searchModelLock(searchlist->GetSearchModelLock(), TRUE);
		const SearchList* list = searchlist->GetSearchListForID(nSearchID);
		if (list == NULL)
			return;

		items.reserve(static_cast<size_t>(list->GetCount()));
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile *pFile = list->GetNext(pos);
			if (pFile == NULL || (pFile->GetListParent() != NULL && !pFile->GetListParent()->IsListExpanded()))
				continue;
			if (bOnlyUnknown) {
				if (!IsSearchKnownColumnEmpty(pFile))
					continue;
			} else if (IsFilteredOut(pFile))
				continue;
			items.push_back(pFile);
		}

		if (items.size() > 1) {
			CombinedSort(items.begin(), items.end(), [this](const CSearchFile* left, const CSearchFile* right) -> bool
			{
				return CompareSearchFilesRaw(left, right, m_pSortParam) < 0;
			});
			GroupSearchItemsByBottomCandidates(items);

			CMapStringToPtr queuedHashes;
			queuedHashes.InitHashTable(static_cast<UINT>(items.size() * 2 + 1));
			std::vector<CSearchFile*> uniqueItems;
			uniqueItems.reserve(items.size());
			for (size_t i = 0; i < items.size(); ++i) {
				CSearchFile *pFile = items[i];
				if (pFile == NULL)
					continue;
				const CString strHash(md4str(pFile->GetFileHash()));
				void *pDummy = NULL;
				if (queuedHashes.Lookup(strHash, pDummy))
					continue;
				queuedHashes.SetAt(strHash, reinterpret_cast<void*>(1));
				uniqueItems.push_back(pFile);
			}
			items.swap(uniqueItems);
		}
	}

	for (size_t i = 0; i < items.size(); ++i)
		downloadItems.AddTail(items[i]);
}

CString CSearchListCtrl::GetListedItemDisplayText(int iItem, int iSubItem) const
{
	const SSearchListRow* pRow = ResolveRowByIndex(iItem);
	if (pRow == NULL)
		return EMPTY;
	if (pRow->eType == SearchListRowSearchFile)
		return pRow->pSearchFile != NULL ? GetItemDisplayText(pRow->pSearchFile, iSubItem) : EMPTY;
	return GetPossibleKnownDisplayText(pRow, iSubItem);
}

bool CSearchListCtrl::BuildSearchInfoTipText(int iItem, CString& strText) const
{
	strText.Empty();
	const CSearchFile* file = ResolveSearchFileByRowIndex(iItem);
	if (file == NULL)
		return false;

	CString strHead(file->GetFileName());
	strHead.AppendFormat(_T("\n") _T("%s %s\n") _T("%s %s\n<br_head>\n"), (LPCTSTR)GetResStringWithColon(_T("CD_UHASH2")), (LPCTSTR)md4str(file->GetFileHash()), (LPCTSTR)GetResStringWithColon(_T("DL_SIZE")), (LPCTSTR)CastItoXBytes((uint64)file->GetFileSize()));
	strText = strHead;

	const CArray<CTag*, CTag*>& tags = file->GetTags();
	for (INT_PTR i = 0; i < tags.GetCount(); ++i) {
		const CTag* tag = tags[i];
		if (tag == NULL)
			continue;

		CString strTag;
		switch (tag->GetNameID()) {
		case FT_FILETYPE:
			strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("TYPE")), (LPCTSTR)tag->GetStr());
			break;
		case FT_FILEFORMAT:
			strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SEARCHEXTENTION")), (LPCTSTR)tag->GetStr());
			break;
		case FT_SOURCES:
			strTag.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SEARCHAVAIL")), file->GetSourceCount());
			break;
		default:
			if (tag->GetNameID() == FT_FILENAME || tag->GetNameID() == FT_FILESIZE)
				break;
			if (tag->HasName()) {
				strTag.Format(_T("%hs: "), tag->GetName());
				strTag.SetAt(0, _totupper(strTag[0]));
			} else {
				extern CString GetName(const CTag *pTag);
				const CString& strTagName(GetName(tag));
				if (!strTagName.IsEmpty())
					strTag.Format(_T("%s: "), (LPCTSTR)strTagName);
			}
			if (!strTag.IsEmpty()) {
				if (tag->IsStr())
					strTag += tag->GetStr();
				else if (tag->IsInt())
					strTag.AppendFormat(_T("%u"), tag->GetInt());
				else if (tag->IsFloat())
					strTag.AppendFormat(_T("%f"), tag->GetFloat());
			}
			break;
		}

		if (!strTag.IsEmpty())
			strText.AppendFormat(_T("%s\n"), (LPCTSTR)strTag);
	}

	strText.AppendChar(TOOLTIP_AUTOFORMAT_SUFFIX_CH);
	return true;
}

SSearchListRow* CSearchListCtrl::ResolveRowByIndex(int iItem) const
{
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return NULL;
	return m_ListedItemsVector[static_cast<size_t>(iItem)];
}

CSearchFile* CSearchListCtrl::ResolveSearchFileByRowIndex(int iItem) const
{
	SSearchListRow* pRow = ResolveRowByIndex(iItem);
	return pRow != NULL && pRow->eType == SearchListRowSearchFile ? pRow->pSearchFile : NULL;
}

bool CSearchListCtrl::IsPassiveRowIndex(int iItem) const
{
	const SSearchListRow* pRow = ResolveRowByIndex(iItem);
	return pRow != NULL && pRow->eType != SearchListRowSearchFile;
}


void CSearchListCtrl::CollectSelectedSearchFiles(CTypedPtrList<CPtrList, CSearchFile*> &selectedList) const
{
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(GetNextSelectedItem(pos));
		if (pSearchFile != NULL)
			selectedList.AddTail(pSearchFile);
	}
}

SSearchListRow* CSearchListCtrl::GetOrCreateSearchRow(CSearchFile* pSearchFile)
{
	if (pSearchFile == NULL)
		return NULL;
	std::map<CSearchFile*, SSearchListRow*>::iterator it = m_SearchRows.find(pSearchFile);
	if (it != m_SearchRows.end())
		return it->second;
	SSearchListRow* pRow = new SSearchListRow;
	pRow->eType = SearchListRowSearchFile;
	pRow->pSearchFile = pSearchFile;
	pRow->nSearchID = pSearchFile->GetSearchID();
	m_SearchRows[pSearchFile] = pRow;
	return pRow;
}

void CSearchListCtrl::RemoveRowsFromSavedStates(const std::vector<SSearchListRow*>& rows)
{
	if (rows.empty())
		return;

	std::map<SSearchListRow*, bool> rowsToRemove;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (rows[i] != NULL)
			rowsToRemove[rows[i]] = true;
	}
	if (rowsToRemove.empty())
		return;

	for (POSITION pos = m_mapListStates.GetStartPosition(); pos != NULL;) {
		int nListID = 0;
		CListState<SSearchListRow>* pState = NULL;
		m_mapListStates.GetNextAssoc(pos, nListID, pState);
		UNREFERENCED_PARAMETER(nListID);
		if (pState == NULL)
			continue;

		bool bRemovedSelectedItem = false;
		for (INT_PTR i = pState->m_aSelectedItems.GetCount(); i > 0; --i) {
			if (rowsToRemove.find(pState->m_aSelectedItems[i - 1]) != rowsToRemove.end()) {
				pState->m_aSelectedItems.RemoveAt(i - 1);
				bRemovedSelectedItem = true;
			}
		}
		for (INT_PTR i = pState->m_aVisibleItems.GetCount(); i > 0; --i) {
			if (rowsToRemove.find(pState->m_aVisibleItems[i - 1]) != rowsToRemove.end())
				pState->m_aVisibleItems.RemoveAt(i - 1);
		}
		if (rowsToRemove.find(pState->m_pFocusedItem) != rowsToRemove.end())
			pState->m_pFocusedItem = NULL;
		if (rowsToRemove.find(pState->m_pSelectionMarkItem) != rowsToRemove.end())
			pState->m_pSelectionMarkItem = NULL;
		if (bRemovedSelectedItem)
			pState->m_bAllItemsSelected = false;
	}
}

void CSearchListCtrl::RemovePossibleKnownRowsFromSavedStates()
{
	RemoveRowsFromSavedStates(m_PossibleKnownRows);
}

void CSearchListCtrl::ClearPossibleKnownRows()
{
	if (m_PossibleKnownRows.empty())
		return;
	RemovePossibleKnownRowsFromSavedStates();
	for (size_t i = 0; i < m_PossibleKnownRows.size(); ++i)
		m_ListedItemsMap.RemoveKey(m_PossibleKnownRows[i]);
	m_ListedItemsVector.erase(std::remove_if(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), [](const SSearchListRow* pRow) {
		return pRow != NULL && pRow->eType != SearchListRowSearchFile;
	}), m_ListedItemsVector.end());
	for (size_t i = 0; i < m_PossibleKnownRows.size(); ++i)
		delete m_PossibleKnownRows[i];
	m_PossibleKnownRows.clear();
}

void CSearchListCtrl::RemoveCachedSearchRowsForFile(const CSearchFile* pFile)
{
	if (pFile == NULL)
		return;

	ClearPossibleKnownRows();
	const bool bRemoveHashGroup = pFile->GetListParent() == NULL;
	const uint32 nSearchID = pFile->GetSearchID();
	const uchar* pucHash = pFile->GetFileHash();
	m_ListedItemsVector.erase(std::remove_if(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), [pFile, bRemoveHashGroup, nSearchID, pucHash](const SSearchListRow* pRow) {
		if (pRow == NULL || pRow->eType != SearchListRowSearchFile || pRow->pSearchFile == NULL)
			return false;
		if (pRow->pSearchFile == pFile)
			return true;
		return bRemoveHashGroup && pRow->pSearchFile->GetSearchID() == nSearchID && md4equ(pRow->pSearchFile->GetFileHash(), pucHash);
	}), m_ListedItemsVector.end());

	std::vector<SSearchListRow*> rowsToRemove;
	for (std::map<CSearchFile*, SSearchListRow*>::const_iterator it = m_SearchRows.begin(); it != m_SearchRows.end(); ++it) {
		CSearchFile* pCachedFile = it->first;
		if (pCachedFile == pFile || (bRemoveHashGroup && pCachedFile != NULL && pCachedFile->GetSearchID() == nSearchID && md4equ(pCachedFile->GetFileHash(), pucHash)))
			rowsToRemove.push_back(it->second);
	}
	RemoveRowsFromSavedStates(rowsToRemove);
	for (std::map<CSearchFile*, SSearchListRow*>::iterator it = m_SearchRows.begin(); it != m_SearchRows.end();) {
		CSearchFile* pCachedFile = it->first;
		const bool bRemove = pCachedFile == pFile || (bRemoveHashGroup && pCachedFile != NULL && pCachedFile->GetSearchID() == nSearchID && md4equ(pCachedFile->GetFileHash(), pucHash));
		if (bRemove) {
			m_ListedItemsMap.RemoveKey(it->second);
			delete it->second;
			m_PossibleKnownCache.erase(pCachedFile);
			it = m_SearchRows.erase(it);
		} else
			++it;
	}
}

void CSearchListCtrl::ClearListedItems(bool bClearSearchRows)
{
	ClearPossibleKnownRows();
	ClearPossibleKnownAvailabilityQueue();
	m_ListedItemsVector.clear();
	m_ListedItemsMap.RemoveAll();
	m_SearchItemsMap.RemoveAll();
	if (bClearSearchRows)
		m_PossibleKnownCache.clear();
	if (bClearSearchRows) {
		std::vector<SSearchListRow*> rowsToRemove;
		rowsToRemove.reserve(m_SearchRows.size());
		for (std::map<CSearchFile*, SSearchListRow*>::const_iterator it = m_SearchRows.begin(); it != m_SearchRows.end(); ++it)
			rowsToRemove.push_back(it->second);
		RemoveRowsFromSavedStates(rowsToRemove);
		for (std::map<CSearchFile*, SSearchListRow*>::iterator it = m_SearchRows.begin(); it != m_SearchRows.end(); ++it)
			delete it->second;
		m_SearchRows.clear();
	}
}

void CSearchListCtrl::RemoveCachedSearchRows(uint32 nSearchID)
{
	std::vector<SSearchListRow*> rowsToRemove;
	for (std::map<CSearchFile*, SSearchListRow*>::const_iterator it = m_SearchRows.begin(); it != m_SearchRows.end(); ++it) {
		SSearchListRow* pRow = it->second;
		if (pRow != NULL && pRow->nSearchID == nSearchID)
			rowsToRemove.push_back(pRow);
	}
	RemoveRowsFromSavedStates(rowsToRemove);
	for (std::map<CSearchFile*, SSearchListRow*>::iterator it = m_SearchRows.begin(); it != m_SearchRows.end();) {
		CSearchFile* pFile = it->first;
		SSearchListRow* pRow = it->second;
		if (pRow != NULL && pRow->nSearchID == nSearchID) {
			m_ListedItemsMap.RemoveKey(pRow);
			delete pRow;
			m_PossibleKnownCache.erase(pFile);
			it = m_SearchRows.erase(it);
		} else
			++it;
	}
}

bool CSearchListCtrl::IsPossibleKnownFeatureEnabled() const
{
	return thePrefs.GetDownloadValidator() != 0 && theApp.DownloadValidator != NULL;
}

bool CSearchListCtrl::IsPossibleKnownFeatureActive() const
{
	return IsPossibleKnownFeatureEnabled() && theApp.DownloadValidator->IsPossibleKnownSearchReady();
}

bool CSearchListCtrl::ImportPossibleKnownCache(CSearchFile* pParent, SPossibleKnownCacheEntry& cacheEntry, bool bForce)
{
	if (pParent == NULL || pParent->GetListParent() != NULL || theApp.DownloadValidator == NULL || searchlist == NULL)
		return false;

	std::vector<CString> astrFileNames;
	uint32 uCurrentAliasFingerprint = 0;
	if (!searchlist->BuildPossibleKnownAliasNames(pParent, astrFileNames, uCurrentAliasFingerprint))
		return false;

	const uint32 uRevision = theApp.DownloadValidator->GetPossibleKnownRevision();
	const uint32 uCandidateDataRevision = theApp.DownloadValidator->GetCandidateDataRevision();
	const uint32 uSourceMediaLengthSec = pParent->GetIntTagValue(FT_MEDIA_LENGTH);
	const SSearchFilePossibleKnownCache& modelCache = pParent->GetPossibleKnownCache();
	const bool bCurrentModelCache = pParent->HasPossibleKnownCache(uRevision, uCandidateDataRevision, uSourceMediaLengthSec, uCurrentAliasFingerprint);
	const bool bUsableSnapshot = bForce && modelCache.bAvailabilityKnown && modelCache.uRevision == uRevision
		&& modelCache.uSourceMediaLengthSec == uSourceMediaLengthSec && modelCache.uAliasFingerprint == uCurrentAliasFingerprint;
	if (!bCurrentModelCache && !bUsableSnapshot)
		return false;

	if (!bForce && cacheEntry.uRevision == uRevision && cacheEntry.uCandidateDataRevision == uCandidateDataRevision
		&& cacheEntry.uSourceMediaLengthSec == uSourceMediaLengthSec && cacheEntry.uAliasFingerprint == uCurrentAliasFingerprint
		&& cacheEntry.bAvailabilityKnown && (!modelCache.bRowsLoaded || cacheEntry.bRowsLoaded))
		return true;

	cacheEntry = SPossibleKnownCacheEntry();
	cacheEntry.uRevision = modelCache.uRevision;
	cacheEntry.uCandidateDataRevision = modelCache.uCandidateDataRevision;
	cacheEntry.uSourceMediaLengthSec = modelCache.uSourceMediaLengthSec;
	cacheEntry.uAliasFingerprint = modelCache.uAliasFingerprint;
	cacheEntry.bAvailabilityKnown = modelCache.bAvailabilityKnown;
	cacheEntry.bHasMatches = modelCache.bHasMatches;
	cacheEntry.bRowsLoaded = modelCache.bRowsLoaded;
	if (modelCache.bRowsLoaded) {
		cacheEntry.rows.reserve(modelCache.rows.size() + 1);
		for (std::vector<SSearchFilePossibleKnownRow>::const_iterator it = modelCache.rows.begin(); it != modelCache.rows.end(); ++it) {
			SSearchListRow row;
			row.eType = SearchListRowPossibleKnownFile;
			row.strName = it->strName;
			row.strFolder = it->strFolder;
			row.strMediaArtist = it->strMediaArtist;
			row.strMediaAlbum = it->strMediaAlbum;
			row.strMediaTitle = it->strMediaTitle;
			row.strMediaCodec = it->strMediaCodec;
			row.strAICHHash = it->strAICHHash;
			row.uSize = it->uSize;
			row.uMediaLengthSec = it->uMediaLengthSec;
			row.uMediaBitrateKbps = it->uMediaBitrateKbps;
			row.uSimilarityScore = it->uSimilarityScore;
			row.uFileType = it->uFileType;
			row.uSourceFlags = it->uSourceFlags;
			md4cpy(row.ucHash, it->ucHash);
			cacheEntry.rows.push_back(row);
		}
	}
	if (AppendSameHashPossibleKnownRow(pParent, cacheEntry.rows)) {
		cacheEntry.bAvailabilityKnown = true;
		cacheEntry.bHasMatches = true;
		cacheEntry.bRowsLoaded = true;
	}
	if (cacheEntry.bRowsLoaded) {
		std::sort(cacheEntry.rows.begin(), cacheEntry.rows.end(), [](const SSearchListRow& first, const SSearchListRow& second) {
			if (first.uSimilarityScore != second.uSimilarityScore)
				return first.uSimilarityScore > second.uSimilarityScore;
			const int iNameCompare = first.strName.CompareNoCase(second.strName);
			return iNameCompare != 0 ? iNameCompare < 0 : first.uSize > second.uSize;
		});
	}
	return true;
}

void CSearchListCtrl::StorePossibleKnownCache(CSearchFile* pParent, const SPossibleKnownCacheEntry& cacheEntry)
{
	if (pParent == NULL || pParent->GetListParent() != NULL || !cacheEntry.bAvailabilityKnown)
		return;

	SSearchFilePossibleKnownCache modelCache;
	modelCache.uRevision = cacheEntry.uRevision;
	modelCache.uCandidateDataRevision = cacheEntry.uCandidateDataRevision;
	modelCache.uSourceMediaLengthSec = pParent->GetIntTagValue(FT_MEDIA_LENGTH);
	modelCache.uAliasFingerprint = cacheEntry.uAliasFingerprint;
	modelCache.bAvailabilityKnown = cacheEntry.bAvailabilityKnown;
	modelCache.bHasMatches = cacheEntry.bHasMatches;
	modelCache.bRowsLoaded = cacheEntry.bRowsLoaded;
	if (cacheEntry.bRowsLoaded) {
		modelCache.rows.reserve(cacheEntry.rows.size());
		for (std::vector<SSearchListRow>::const_iterator it = cacheEntry.rows.begin(); it != cacheEntry.rows.end(); ++it) {
			if (it->eType != SearchListRowPossibleKnownFile)
				continue;
			SSearchFilePossibleKnownRow row;
			row.strName = it->strName;
			row.strFolder = it->strFolder;
			row.strMediaArtist = it->strMediaArtist;
			row.strMediaAlbum = it->strMediaAlbum;
			row.strMediaTitle = it->strMediaTitle;
			row.strMediaCodec = it->strMediaCodec;
			row.strAICHHash = it->strAICHHash;
			row.uSize = it->uSize;
			row.uMediaLengthSec = it->uMediaLengthSec;
			row.uMediaBitrateKbps = it->uMediaBitrateKbps;
			row.uSimilarityScore = it->uSimilarityScore;
			row.uFileType = it->uFileType;
			row.uSourceFlags = it->uSourceFlags;
			md4cpy(row.ucHash, it->ucHash);
			modelCache.rows.push_back(row);
		}
	}
	pParent->SetPossibleKnownCache(modelCache);
}

bool CSearchListCtrl::HasPossibleKnownMatches(CSearchFile* pParent)
{
	if (pParent == NULL || pParent->GetListParent() != NULL || !IsPossibleKnownFeatureActive() || searchlist == NULL)
		return false;

	std::vector<CString> astrFileNames;
	uint32 uCurrentAliasFingerprint = 0;
	if (!searchlist->BuildPossibleKnownAliasNames(pParent, astrFileNames, uCurrentAliasFingerprint))
		return false;

	const uint32 uRevision = theApp.DownloadValidator->GetPossibleKnownRevision();
	SPossibleKnownCacheEntry& modelEntry = m_PossibleKnownCache[pParent];
	ImportPossibleKnownCache(pParent, modelEntry);
	std::map<CSearchFile*, SPossibleKnownCacheEntry>::iterator it = m_PossibleKnownCache.find(pParent);
	if (it != m_PossibleKnownCache.end() && (it->second.uRevision != uRevision || it->second.uAliasFingerprint != uCurrentAliasFingerprint)) {
		m_PossibleKnownCache.erase(it);
		it = m_PossibleKnownCache.end();
	}
	if (it != m_PossibleKnownCache.end() && it->second.uCandidateDataRevision != theApp.DownloadValidator->GetCandidateDataRevision())
		QueuePossibleKnownAvailability(pParent, it->second.bRowsLoaded || !it->second.rows.empty() || pParent->IsListExpanded(), true);
	if (it == m_PossibleKnownCache.end() || !it->second.bAvailabilityKnown)
		QueuePossibleKnownAvailability(pParent);
	it = m_PossibleKnownCache.find(pParent);
	return it != m_PossibleKnownCache.end() && it->second.bAvailabilityKnown && it->second.bHasMatches;
}

bool CSearchListCtrl::HasCachedPossibleKnownMatches(const CSearchFile* pParent) const
{
	std::map<CSearchFile*, SPossibleKnownCacheEntry>::const_iterator it = m_PossibleKnownCache.find(const_cast<CSearchFile*>(pParent));
	return it != m_PossibleKnownCache.end() && it->second.bHasMatches;
}

bool CSearchListCtrl::CanExpandSearchParent(CSearchFile* pParent)
{
	return pParent != NULL && pParent->GetListParent() == NULL && (pParent->GetListChildCount() > 1 || HasPossibleKnownMatches(pParent));
}

void CSearchListCtrl::AppendPossibleKnownRows(CSearchFile* pParent, std::vector<SSearchListRow*>& rows)
{
	if (pParent == NULL || !pParent->IsListExpanded() || !HasPossibleKnownMatches(pParent))
		return;

	SPossibleKnownCacheEntry& cacheEntry = m_PossibleKnownCache[pParent];
	if (!cacheEntry.bRowsLoaded && !cacheEntry.bRowsPending)
		QueuePossibleKnownAvailability(pParent, true);
	if (cacheEntry.rows.empty())
		return;

	SSearchListRow* pHeader = new SSearchListRow;
	pHeader->eType = SearchListRowPossibleKnownHeader;
	pHeader->pParentSearchFile = pParent;
	pHeader->nSearchID = pParent->GetSearchID();
	pHeader->strName = GetResString(_T("DOWNLOAD_VALIDATOR_POSSIBLE_KNOWN_MATCHES"));
	m_PossibleKnownRows.push_back(pHeader);
	rows.push_back(pHeader);

	const bool bReverse = GetSortItem() == colSearchSimilarity && GetSortAscending();
	for (size_t i = 0; i < cacheEntry.rows.size(); ++i) {
		const size_t uRowIndex = bReverse ? cacheEntry.rows.size() - i - 1 : i;
		SSearchListRow* pRow = new SSearchListRow(cacheEntry.rows[uRowIndex]);
		pRow->pParentSearchFile = pParent;
		pRow->nSearchID = pParent->GetSearchID();
		m_PossibleKnownRows.push_back(pRow);
		rows.push_back(pRow);
	}
}

void CSearchListCtrl::RebuildPossibleKnownRows()
{
	ClearPossibleKnownRows();
	if (!IsPossibleKnownFeatureActive() || m_ListedItemsVector.empty())
		return;
	std::vector<SSearchListRow*> rows;
	rows.reserve(m_ListedItemsVector.size());
	for (size_t i = 0; i < m_ListedItemsVector.size();) {
		SSearchListRow* pRow = m_ListedItemsVector[i++];
		rows.push_back(pRow);
		CSearchFile* pParent = pRow != NULL && pRow->eType == SearchListRowSearchFile ? pRow->pSearchFile : NULL;
		if (pParent == NULL || pParent->GetListParent() != NULL)
			continue;
		while (i < m_ListedItemsVector.size()) {
			SSearchListRow* pChildRow = m_ListedItemsVector[i];
			CSearchFile* pChild = pChildRow != NULL && pChildRow->eType == SearchListRowSearchFile ? pChildRow->pSearchFile : NULL;
			if (pChild == NULL || pChild->GetListParent() != pParent)
				break;
			rows.push_back(pChildRow);
			++i;
		}
		AppendPossibleKnownRows(pParent, rows);
	}
	m_ListedItemsVector.swap(rows);
}

bool CSearchListCtrl::IsRowDescendantOfParent(const SSearchListRow* pRow, const CSearchFile* pParent) const
{
	if (pRow == NULL || pParent == NULL)
		return false;
	if (pRow->eType == SearchListRowSearchFile)
		return pRow->pSearchFile != NULL && pRow->pSearchFile->GetListParent() == pParent;
	return pRow->pParentSearchFile == pParent;
}

bool CSearchListCtrl::HasSelectedPassiveRows() const
{
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		if (IsPassiveRowIndex(GetNextSelectedItem(pos)))
			return true;
	}
	return false;
}

bool CSearchListCtrl::CollectSelectedPossibleKnownRows(std::vector<const SSearchListRow*>& rows) const
{
	rows.clear();
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const SSearchListRow* pRow = ResolveRowByIndex(GetNextSelectedItem(pos));
		if (pRow == NULL || pRow->eType != SearchListRowPossibleKnownFile) {
			rows.clear();
			return false;
		}
		rows.push_back(pRow);
	}
	return !rows.empty();
}

bool CSearchListCtrl::ExecutePossibleKnownCancelCommand(UINT uCommand)
{
	if (uCommand != MP_CANCEL && uCommand != MP_CANCEL_FORGET)
		return false;

	std::vector<const SSearchListRow*> rows;
	if (!CollectSelectedPossibleKnownRows(rows))
		return false;
	if (theApp.downloadqueue == NULL)
		return true;

	CTypedPtrList<CPtrList, CPartFile*> selectedDownloads;
	for (size_t i = 0; i < rows.size(); ++i) {
		CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(rows[i]->ucHash);
		if (pPartFile == NULL || pPartFile->GetFileSize() != rows[i]->uSize)
			return true;
		if (selectedDownloads.Find(pPartFile) == NULL)
			selectedDownloads.AddTail(pPartFile);
	}

	CString fileList(GetResString(selectedDownloads.GetCount() == 1 ? _T("Q_CANCELDL2") : _T("Q_CANCELDL")));
	CStringArray removableHashes;
	removableHashes.SetSize(0, selectedDownloads.GetCount() > 16 ? selectedDownloads.GetCount() : 16);
	bool bValidDelete = false;
	int iDisplayFiles = 0;
	const int iMaxDisplayFiles = 10;
	for (POSITION pos = selectedDownloads.GetHeadPosition(); pos != NULL;) {
		const CPartFile* pPartFile = selectedDownloads.GetNext(pos);
		if (pPartFile == NULL || pPartFile->GetStatus() == PS_COMPLETING)
			continue;

		bValidDelete = true;
		removableHashes.Add(md4str(pPartFile->GetFileHash()));
		if (++iDisplayFiles < iMaxDisplayFiles)
			fileList.AppendFormat(_T("\n%s"), (LPCTSTR)pPartFile->GetFileName());
		else if (iDisplayFiles == iMaxDisplayFiles && pos != NULL)
			fileList += _T("\n...");
	}

	if (bValidDelete && removableHashes.GetSize() > 0 && CDarkMode::MessageBox(fileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES)
		theApp.ExecuteDownloadListRemoveCommand(removableHashes, uCommand != MP_CANCEL_FORGET, true);
	return true;
}

bool CSearchListCtrl::ExecutePossibleKnownCopyCommand(UINT uCommand, const std::vector<const SSearchListRow*>& rows)
{
	if (uCommand != MP_CUT && uCommand != MP_GETED2KLINK && uCommand != MP_GETHTMLED2KLINK)
		return false;

	CString strClipboard;
	for (size_t i = 0; i < rows.size(); ++i) {
		const SSearchListRow* pRow = rows[i];
		if (pRow == NULL)
			continue;

		CString strValue;
		if (uCommand == MP_CUT)
			strValue = pRow->strName;
		else {
			CString strLink;
			strLink.Format(_T("ed2k://|file|%s|%I64u|%s|"), (LPCTSTR)EncodeUrlUtf8(StripInvalidFilenameChars(pRow->strName)), static_cast<uint64>(pRow->uSize), (LPCTSTR)md4str(pRow->ucHash));
			if (!pRow->strAICHHash.IsEmpty())
				strLink.AppendFormat(_T("h=%s|"), (LPCTSTR)pRow->strAICHHash);
			strLink += _T('/');
			if (uCommand == MP_GETHTMLED2KLINK)
				strValue.Format(_T("<a href=\"%s\">%s</a>"), (LPCTSTR)strLink, (LPCTSTR)StripInvalidFilenameChars(pRow->strName));
			else
				strValue = strLink;
		}

		if (strValue.IsEmpty())
			continue;
		if (!strClipboard.IsEmpty())
			strClipboard += uCommand == MP_GETHTMLED2KLINK ? _T("<br>\r\n") : _T("\r\n");
		strClipboard += strValue;
	}

	if (!strClipboard.IsEmpty()) {
		theApp.CopyTextToClipboard(strClipboard);
		theApp.emuledlg->statusbar->SetText(GetResString(uCommand == MP_CUT ? _T("FILE_NAME_COPIED_TO_CLIPBOARD") : _T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
	}
	return true;
}

bool CSearchListCtrl::ExecutePossibleKnownSearchRelatedCommand(const std::vector<const SSearchListRow*>& rows)
{
	if (rows.empty() || theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL || !theApp.emuledlg->searchwnd->CanSearchRelatedFiles())
		return true;

	std::vector<CString> hashes;
	std::vector<CString> names;
	for (size_t i = 0; i < rows.size(); ++i) {
		const SSearchListRow* pRow = rows[i];
		if (pRow == NULL || isnulmd4(pRow->ucHash))
			continue;
		const CString strHash(md4str(pRow->ucHash));
		if (std::find(hashes.begin(), hashes.end(), strHash) != hashes.end())
			continue;
		hashes.push_back(strHash);
		names.push_back(pRow->strName);
	}
	if (!hashes.empty())
		theApp.emuledlg->searchwnd->SearchRelatedFiles(hashes, names);
	return true;
}

bool CSearchListCtrl::ExecutePossibleKnownWebServiceCommand(UINT uCommand, const std::vector<const SSearchListRow*>& rows)
{
	if (uCommand < MP_WEBURL || uCommand > MP_WEBURL + 256)
		return false;
	for (size_t i = 0; i < rows.size(); ++i) {
		const SSearchListRow* pRow = rows[i];
		if (pRow != NULL)
			theWebServices.RunURL(pRow->ucHash, static_cast<uint64>(pRow->uSize), pRow->strName, uCommand);
	}
	return true;
}

bool CSearchListCtrl::ResolvePossibleKnownSharedFilePath(const SSearchListRow* pRow, CString& strFilePath) const
{
	strFilePath.Empty();
	if (pRow == NULL || theApp.sharedfiles == NULL || isnulmd4(pRow->ucHash))
		return false;

	CKnownFile* pSharedFile = theApp.sharedfiles->GetLiveFileByID(pRow->ucHash);
	if (pSharedFile != NULL && !pSharedFile->IsPartFile() && pSharedFile->GetFileSize() == pRow->uSize
		&& pSharedFile->GetFileName().CompareNoCase(pRow->strName) == 0 && !pSharedFile->GetFilePath().IsEmpty()) {
		strFilePath = pSharedFile->GetFilePath();
		return true;
	}

	if (theApp.knownfiles == NULL)
		return false;

	std::vector<CString> duplicateFilePaths;
	theApp.knownfiles->CollectDuplicateFilePathsByIdentity(pRow->ucHash, pRow->strName, static_cast<uint64>(pRow->uSize), duplicateFilePaths);
	for (std::vector<CString>::const_iterator it = duplicateFilePaths.begin(); it != duplicateFilePaths.end(); ++it) {
		const int iSeparator = it->ReverseFind(_T('\\'));
		const CString strDirectory = iSeparator >= 0 ? it->Left(iSeparator) : CString();
		if (strDirectory.IsEmpty() || !theApp.sharedfiles->ShouldBeShared(strDirectory, *it, false))
			continue;
		strFilePath = *it;
		return true;
	}
	return false;
}

bool CSearchListCtrl::ExecutePossibleKnownPreviewCommand(UINT uCommand, const std::vector<const SSearchListRow*>& rows)
{
	if (rows.size() != 1)
		return true;
	const SSearchListRow* pRow = rows[0];
	if (pRow == NULL)
		return true;

	CPartFile* pPartFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pRow->ucHash) : NULL;
	if (pPartFile != NULL && pPartFile->GetFileSize() == pRow->uSize) {
		if (uCommand == MP_PREVIEW)
			pPartFile->PreviewFile();
		else if (uCommand == MP_TRY_TO_GET_PREVIEW_PARTS)
			pPartFile->SetPreviewPrio(!pPartFile->GetPreviewPrio());
		else if (uCommand == MP_PAUSEONPREVIEW && pPartFile->IsPreviewableFileType() && !pPartFile->IsReadyForPreview())
			pPartFile->SetPauseOnPreview(!pPartFile->IsPausingOnPreview());
		else if (uCommand >= MP_PREVIEW_APP_MIN && uCommand <= MP_PREVIEW_APP_MAX)
			thePreviewApps.RunApp(pPartFile, uCommand);
		return true;
	}

	CString strSharedFilePath;
	if (ResolvePossibleKnownSharedFilePath(pRow, strSharedFilePath)) {
		if (uCommand == MP_PREVIEW)
			thePreviewApps.RunCommandForFilePath(strSharedFilePath, thePrefs.GetVideoPlayer(), thePrefs.GetVideoPlayerArgs());
		else if (uCommand >= MP_PREVIEW_APP_MIN && uCommand <= MP_PREVIEW_APP_MAX)
			thePreviewApps.RunAppForFilePath(strSharedFilePath, uCommand);
	}
	return true;
}

void CSearchListCtrl::ClearPossibleKnownAvailabilityQueue()
{
	const bool bHadPending = m_uNextPossibleKnownAvailability < m_PossibleKnownAvailabilityQueue.size();
	const uint32 nPendingSearchID = m_nResultsID;
	if (::IsWindow(m_hWnd))
		KillTimer(kTimerPossibleKnownAvailability);
	for (size_t i = m_uNextPossibleKnownAvailability; i < m_PossibleKnownAvailabilityQueue.size(); ++i) {
		const SPossibleKnownAvailabilityItem& item = m_PossibleKnownAvailabilityQueue[i];
		std::map<CSearchFile*, SPossibleKnownCacheEntry>::iterator it = m_PossibleKnownCache.find(item.pParent);
		if (it == m_PossibleKnownCache.end() || it->second.uRevision != item.uRevision)
			continue;
		if (item.bReplaceRows) {
			if (it->second.uPendingCandidateDataRevision == item.uCandidateDataRevision) {
				it->second.bReplaceRowsPending = false;
				it->second.bPendingHasMatches = false;
				it->second.pendingRows.clear();
			}
		} else if (item.bLoadRows)
			it->second.bRowsPending = false;
		else
			it->second.bAvailabilityPending = false;
	}
	m_PossibleKnownAvailabilityQueue.clear();
	m_uNextPossibleKnownAvailability = 0;
	if (bHadPending && nPendingSearchID != 0)
		theApp.QueueSearchActivityChangedEvent(nPendingSearchID);
}

bool CSearchListCtrl::AppendSameHashPossibleKnownRow(CSearchFile* pParent, std::vector<SSearchListRow>& rows) const
{
	if (pParent == NULL || !thePrefs.GetDownloadValidatorRejectSameHash() || theApp.knownfiles == NULL)
		return false;

	CKnownFile* pKnownFile = theApp.knownfiles->FindKnownFileByID(pParent->GetFileHash());
	if (pKnownFile == NULL || pKnownFile->GetFileSize() != pParent->GetFileSize() || pKnownFile->IsPartFile())
		return false;

	const uint32 uSourceMediaLengthSec = pParent->GetIntTagValue(FT_MEDIA_LENGTH);
	const uint32 uCandidateMediaLengthSec = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
	if (thePrefs.GetDownloadValidatorMediaLengthMatching() && uSourceMediaLengthSec != 0 && uCandidateMediaLengthSec != 0) {
		const uint32 uDifference = uSourceMediaLengthSec > uCandidateMediaLengthSec ? uSourceMediaLengthSec - uCandidateMediaLengthSec : uCandidateMediaLengthSec - uSourceMediaLengthSec;
		if (uDifference > thePrefs.GetDownloadValidatorMediaLengthToleranceSec())
			return false;
	}

	for (size_t i = 0; i < rows.size(); ++i) {
		if (rows[i].uSize == pKnownFile->GetFileSize() && rows[i].strName == pKnownFile->GetFileName() && md4equ(rows[i].ucHash, pKnownFile->GetFileHash()))
			return true;
	}

	SSearchListRow row;
	row.eType = SearchListRowPossibleKnownFile;
	row.strName = pKnownFile->GetFileName();
	row.strFolder = pKnownFile->GetPath();
	row.strMediaArtist = pKnownFile->GetStrTagValue(FT_MEDIA_ARTIST);
	row.strMediaAlbum = pKnownFile->GetStrTagValue(FT_MEDIA_ALBUM);
	row.strMediaTitle = pKnownFile->GetStrTagValue(FT_MEDIA_TITLE);
	row.strMediaCodec = pKnownFile->GetStrTagValue(FT_MEDIA_CODEC);
	row.uSize = pKnownFile->GetFileSize();
	row.uMediaLengthSec = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
	row.uMediaBitrateKbps = pKnownFile->GetIntTagValue(FT_MEDIA_BITRATE);
	row.uSimilarityScore = 100;
	row.uFileType = static_cast<uint8>(GetED2KFileTypeID(pKnownFile->GetFileName()));
	row.uSourceFlags = CDownloadValidator::FuzzyFileSourceKnown;
	md4cpy(row.ucHash, pKnownFile->GetFileHash());
	if (pKnownFile->GetFileIdentifierC().HasAICHHash())
		row.strAICHHash = pKnownFile->GetFileIdentifierC().GetAICHHash().GetString();
	rows.push_back(row);
	return true;
}

void CSearchListCtrl::QueuePossibleKnownAvailability(CSearchFile* pParent, bool bLoadRows, bool bReplaceRows)
{
	if (pParent == NULL || pParent->GetListParent() != NULL || !IsPossibleKnownFeatureEnabled() || searchlist == NULL)
		return;
	if (!theApp.DownloadValidator->IsPossibleKnownSearchReady())
		return;
	if (searchlist->HasPendingPossibleKnownPreparation(pParent->GetSearchID()))
		return;

	std::vector<CString> astrFileNames;
	uint32 uAliasFingerprint = 0;
	if (!searchlist->BuildPossibleKnownAliasNames(pParent, astrFileNames, uAliasFingerprint))
		return;

	const uint32 uRevision = theApp.DownloadValidator->GetPossibleKnownRevision();
	const uint32 uCandidateDataRevision = theApp.DownloadValidator->GetCandidateDataRevision();
	const uint32 uSourceMediaLengthSec = pParent->GetIntTagValue(FT_MEDIA_LENGTH);
	SPossibleKnownCacheEntry& cacheEntry = m_PossibleKnownCache[pParent];
	ImportPossibleKnownCache(pParent, cacheEntry);
	if (cacheEntry.uRevision != uRevision || cacheEntry.uSourceMediaLengthSec != uSourceMediaLengthSec || cacheEntry.uAliasFingerprint != uAliasFingerprint) {
		cacheEntry = SPossibleKnownCacheEntry();
		cacheEntry.uRevision = uRevision;
		cacheEntry.uSourceMediaLengthSec = uSourceMediaLengthSec;
		cacheEntry.uAliasFingerprint = uAliasFingerprint;
	}
	if (cacheEntry.uCandidateDataRevision == uCandidateDataRevision && cacheEntry.bAvailabilityKnown
		&& (!bLoadRows || cacheEntry.bRowsLoaded) && !bReplaceRows)
		return;

	if (!cacheEntry.bAvailabilityKnown && AppendSameHashPossibleKnownRow(pParent, cacheEntry.rows)) {
		cacheEntry.bAvailabilityKnown = true;
		cacheEntry.bHasMatches = true;
	}

	if (bReplaceRows) {
		if (cacheEntry.bReplaceRowsPending && cacheEntry.uPendingCandidateDataRevision == uCandidateDataRevision)
			return;
		cacheEntry.bReplaceRowsPending = true;
		cacheEntry.bPendingHasMatches = false;
		cacheEntry.uPendingCandidateDataRevision = uCandidateDataRevision;
		cacheEntry.pendingRows.clear();
		if (bLoadRows && AppendSameHashPossibleKnownRow(pParent, cacheEntry.pendingRows))
			cacheEntry.bPendingHasMatches = true;
	} else if (bLoadRows) {
		if (cacheEntry.bRowsLoaded || cacheEntry.bRowsPending)
			return;
		cacheEntry.bRowsPending = true;
	} else {
		if (cacheEntry.bAvailabilityKnown || cacheEntry.bAvailabilityPending)
			return;
		cacheEntry.bAvailabilityPending = true;
	}

	const bool bStartTimer = m_PossibleKnownAvailabilityQueue.empty();
	SPossibleKnownAvailabilityItem item;
	item.pParent = pParent;
	item.nSearchID = pParent->GetSearchID();
	item.strFileName = pParent->GetFileName();
	item.astrFileNames.swap(astrFileNames);
	item.uFileSize = pParent->GetFileSize();
	item.uMediaLengthSec = uSourceMediaLengthSec;
	item.uAliasFingerprint = uAliasFingerprint;
	md4cpy(item.ucHash, pParent->GetFileHash());
	item.bLoadRows = bLoadRows;
	item.bReplaceRows = bReplaceRows;
	item.uRevision = uRevision;
	item.uCandidateDataRevision = uCandidateDataRevision;
	m_PossibleKnownAvailabilityQueue.push_back(item);
	theApp.QueueSearchActivityChangedEvent(item.nSearchID);
	if (bStartTimer && ::IsWindow(m_hWnd) && SetTimer(kTimerPossibleKnownAvailability, kPossibleKnownAvailabilityDelayMs, NULL) == 0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Possible known file availability timer could not be started. Falling back to an immediate slice. pending=%u\n"), static_cast<UINT>(m_PossibleKnownAvailabilityQueue.size() - m_uNextPossibleKnownAvailability));
		ProcessPossibleKnownAvailability();
	}
}

void CSearchListCtrl::ProcessPossibleKnownAvailability()
{
	if (!IsPossibleKnownFeatureEnabled() || theApp.IsClosing() || !::IsWindow(m_hWnd) || searchlist == NULL) {
		ClearPossibleKnownAvailabilityQueue();
		return;
	}
	if (!theApp.DownloadValidator->IsPossibleKnownSearchReady()) {
		ClearPossibleKnownAvailabilityQueue();
		return;
	}
	if (m_bChunkedSearchRemoveActive) {
		if (SetTimer(kTimerPossibleKnownAvailability, kPossibleKnownAvailabilityWaitForRemoveMs, NULL) == 0) {
			AddDebugLogLine(DLP_HIGH, false, _T("Possible known file availability timer could not be restarted during search result removal. pending=%u\n"), static_cast<UINT>(m_PossibleKnownAvailabilityQueue.size() - m_uNextPossibleKnownAvailability));
			ClearPossibleKnownAvailabilityQueue();
		}
		return;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;
	bool bQueueBackpressure = false;
	while (m_uNextPossibleKnownAvailability < m_PossibleKnownAvailabilityQueue.size()) {
		const SPossibleKnownAvailabilityItem item = m_PossibleKnownAvailabilityQueue[m_uNextPossibleKnownAvailability];
		std::map<CSearchFile*, SSearchListRow*>::const_iterator itRow = m_SearchRows.find(item.pParent);
		CSearchFile* pCurrentParent = itRow != m_SearchRows.end() && itRow->second != NULL ? itRow->second->pSearchFile : NULL;
		bool bQueued = false;
		bool bAliasFingerprintChanged = false;
		if (item.nSearchID == m_nResultsID && pCurrentParent != NULL && itRow->second->nSearchID == item.nSearchID
			&& pCurrentParent->GetFileName() == item.strFileName && pCurrentParent->GetFileSize() == item.uFileSize
			&& pCurrentParent->GetIntTagValue(FT_MEDIA_LENGTH) == item.uMediaLengthSec && md4equ(pCurrentParent->GetFileHash(), item.ucHash)
			&& item.uRevision == theApp.DownloadValidator->GetPossibleKnownRevision()
			&& item.uCandidateDataRevision == theApp.DownloadValidator->GetCandidateDataRevision()) {
			std::vector<CString> astrCurrentFileNames;
			uint32 uCurrentAliasFingerprint = 0;
			if (searchlist->BuildPossibleKnownAliasNames(pCurrentParent, astrCurrentFileNames, uCurrentAliasFingerprint)) {
				bAliasFingerprintChanged = uCurrentAliasFingerprint != item.uAliasFingerprint;
				if (!bAliasFingerprintChanged && !searchlist->QueuePossibleKnownQuery(reinterpret_cast<UINT_PTR>(item.pParent), item.nSearchID, item.ucHash, item.strFileName, item.astrFileNames, item.uFileSize,
					item.uMediaLengthSec, item.uAliasFingerprint, item.bLoadRows, item.uRevision, item.uCandidateDataRevision, item.bReplaceRows, &pCurrentParent->GetDownloadValidatorFuzzyQueryData())) {
					bQueueBackpressure = true;
					break;
				}
				if (!bAliasFingerprintChanged)
					bQueued = true;
			}
		}

		if (!bQueued) {
			std::map<CSearchFile*, SPossibleKnownCacheEntry>::iterator itCache = m_PossibleKnownCache.find(item.pParent);
			if (itCache != m_PossibleKnownCache.end() && itCache->second.uRevision == item.uRevision) {
				if (item.bReplaceRows) {
					if (itCache->second.uPendingCandidateDataRevision == item.uCandidateDataRevision) {
						itCache->second.bReplaceRowsPending = false;
						itCache->second.bPendingHasMatches = false;
						itCache->second.pendingRows.clear();
					}
				} else if (item.bLoadRows)
					itCache->second.bRowsPending = false;
				else
					itCache->second.bAvailabilityPending = false;
			}
			if (bAliasFingerprintChanged && pCurrentParent != NULL)
				QueuePossibleKnownAvailability(pCurrentParent, item.bLoadRows, item.bReplaceRows);
		}

		++m_uNextPossibleKnownAvailability;
		++uProcessed;
		if (uProcessed >= kPossibleKnownAvailabilityItemsPerSlice || static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= kPossibleKnownAvailabilitySliceMs)
			break;
	}

	if (m_uNextPossibleKnownAvailability >= m_PossibleKnownAvailabilityQueue.size())
		ClearPossibleKnownAvailabilityQueue();
	else if (SetTimer(kTimerPossibleKnownAvailability, bQueueBackpressure ? 50 : kPossibleKnownAvailabilityDelayMs, NULL) == 0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Possible known file availability continuation timer could not be started. pending=%u\n"), static_cast<UINT>(m_PossibleKnownAvailabilityQueue.size() - m_uNextPossibleKnownAvailability));
		ClearPossibleKnownAvailabilityQueue();
	}
}

void CSearchListCtrl::ApplyPossibleKnownQueryResult(UINT_PTR uParentToken, uint32 nSearchID, const uchar* pHash, const CString& strFileName, EMFileSize uFileSize,
	uint32 uMediaLengthSec, uint32 uAliasFingerprint, uint32 uRevision, uint32 uCandidateDataRevision, bool bReplaceRows, bool bRowsRequested, bool bHasMatches, bool bFinalResult, const SDownloadValidatorFuzzyQueryData& queryData, const std::vector<SSearchListRow>& rows)
{
	if (uParentToken == 0 || pHash == NULL || nSearchID == 0 || !IsPossibleKnownFeatureEnabled())
		return;
	if (uRevision != theApp.DownloadValidator->GetPossibleKnownRevision() || uCandidateDataRevision != theApp.DownloadValidator->GetCandidateDataRevision())
		return;

	CSearchFile* pParent = reinterpret_cast<CSearchFile*>(uParentToken);
	std::map<CSearchFile*, SSearchListRow*>::const_iterator itRow = m_SearchRows.find(pParent);
	if (itRow == m_SearchRows.end() || itRow->second == NULL || itRow->second->pSearchFile == NULL)
		return;
	CSearchFile* pCurrentParent = itRow->second->pSearchFile;
	if (pCurrentParent->GetSearchID() != nSearchID || pCurrentParent->GetFileName() != strFileName || pCurrentParent->GetFileSize() != uFileSize || !md4equ(pCurrentParent->GetFileHash(), pHash))
		return;
	const uint32 uCurrentMediaLengthSec = pCurrentParent->GetIntTagValue(FT_MEDIA_LENGTH);
	std::vector<CString> astrCurrentFileNames;
	uint32 uCurrentAliasFingerprint = 0;
	const bool bCurrentAliasesValid = searchlist != NULL && searchlist->BuildPossibleKnownAliasNames(pCurrentParent, astrCurrentFileNames, uCurrentAliasFingerprint);
	if (uCurrentMediaLengthSec != uMediaLengthSec || !bCurrentAliasesValid || uCurrentAliasFingerprint != uAliasFingerprint) {
		SPossibleKnownCacheEntry& staleCacheEntry = m_PossibleKnownCache[pParent];
		staleCacheEntry = SPossibleKnownCacheEntry();
		staleCacheEntry.uRevision = uRevision;
		staleCacheEntry.uSourceMediaLengthSec = uCurrentMediaLengthSec;
		staleCacheEntry.uAliasFingerprint = uCurrentAliasFingerprint;
		QueuePossibleKnownAvailability(pCurrentParent, bRowsRequested, bReplaceRows);
		return;
	}

	SPossibleKnownCacheEntry& cacheEntry = m_PossibleKnownCache[pParent];
	if (cacheEntry.uRevision != uRevision || cacheEntry.uSourceMediaLengthSec != uMediaLengthSec || cacheEntry.uAliasFingerprint != uAliasFingerprint) {
		cacheEntry = SPossibleKnownCacheEntry();
		cacheEntry.uRevision = uRevision;
		cacheEntry.uSourceMediaLengthSec = uMediaLengthSec;
		cacheEntry.uAliasFingerprint = uAliasFingerprint;
	}
	if (queryData.bPrepared)
		pCurrentParent->GetDownloadValidatorFuzzyQueryData() = queryData;
	std::vector<SSearchListRow>& targetRows = bReplaceRows ? cacheEntry.pendingRows : cacheEntry.rows;
	if (bReplaceRows) {
		if (!cacheEntry.bReplaceRowsPending || cacheEntry.uPendingCandidateDataRevision != uCandidateDataRevision) {
			cacheEntry.bReplaceRowsPending = true;
			cacheEntry.bPendingHasMatches = false;
			cacheEntry.uPendingCandidateDataRevision = uCandidateDataRevision;
			cacheEntry.pendingRows.clear();
			if (bRowsRequested && AppendSameHashPossibleKnownRow(pCurrentParent, cacheEntry.pendingRows))
				cacheEntry.bPendingHasMatches = true;
		}
		cacheEntry.bPendingHasMatches = cacheEntry.bPendingHasMatches || bHasMatches;
	} else if (bRowsRequested)
		cacheEntry.bAvailabilityPending = false;

	if (bRowsRequested) {
		std::unordered_multimap<uint64, size_t> rowIdentities;
		rowIdentities.reserve(targetRows.size() + rows.size());
		for (size_t i = 0; i < targetRows.size(); ++i)
			rowIdentities.emplace(BuildPossibleKnownRowIdentityKey(targetRows[i]), i);
		for (size_t i = 0; i < rows.size(); ++i) {
			const uint64 uIdentity = BuildPossibleKnownRowIdentityKey(rows[i]);
			bool bDuplicate = false;
			const std::pair<std::unordered_multimap<uint64, size_t>::iterator, std::unordered_multimap<uint64, size_t>::iterator> range = rowIdentities.equal_range(uIdentity);
			for (std::unordered_multimap<uint64, size_t>::iterator it = range.first; it != range.second; ++it) {
				if (it->second >= targetRows.size())
					continue;
				SSearchListRow& existing = targetRows[it->second];
				if (existing.uSize == rows[i].uSize && existing.strName == rows[i].strName && md4equ(existing.ucHash, rows[i].ucHash)) {
					if (IsPossibleKnownRowBetter(rows[i], existing))
						existing = rows[i];
					bDuplicate = true;
					break;
				}
			}
			if (!bDuplicate) {
				rowIdentities.emplace(uIdentity, targetRows.size());
				targetRows.push_back(rows[i]);
			}
		}
	}

	if (bReplaceRows && bFinalResult) {
		if (bRowsRequested) {
			cacheEntry.rows.swap(cacheEntry.pendingRows);
			cacheEntry.bRowsLoaded = true;
		}
		cacheEntry.pendingRows.clear();
		cacheEntry.bReplaceRowsPending = false;
		cacheEntry.bAvailabilityKnown = true;
		cacheEntry.bHasMatches = cacheEntry.bPendingHasMatches || !cacheEntry.rows.empty();
		cacheEntry.bPendingHasMatches = false;
		cacheEntry.uCandidateDataRevision = uCandidateDataRevision;
	} else if (!bReplaceRows && bRowsRequested) {
		if (bFinalResult) {
			cacheEntry.bRowsPending = false;
			cacheEntry.bRowsLoaded = true;
		}
		cacheEntry.bAvailabilityKnown = true;
		cacheEntry.bHasMatches = bHasMatches || !cacheEntry.rows.empty();
		cacheEntry.uCandidateDataRevision = uCandidateDataRevision;
	} else if (!bReplaceRows && bFinalResult) {
		cacheEntry.bAvailabilityPending = false;
		cacheEntry.bAvailabilityKnown = true;
		cacheEntry.bHasMatches = cacheEntry.bHasMatches || bHasMatches;
		cacheEntry.uCandidateDataRevision = uCandidateDataRevision;
	}

	if ((bReplaceRows && bFinalResult) || (!bReplaceRows && bRowsRequested)) {
		std::sort(cacheEntry.rows.begin(), cacheEntry.rows.end(), [](const SSearchListRow& first, const SSearchListRow& second) {
			if (first.uSimilarityScore != second.uSimilarityScore)
				return first.uSimilarityScore > second.uSimilarityScore;
			const int iNameCompare = first.strName.CompareNoCase(second.strName);
			return iNameCompare != 0 ? iNameCompare < 0 : first.uSize > second.uSize;
		});
	}

	if (bFinalResult)
		StorePossibleKnownCache(pCurrentParent, cacheEntry);

	const bool bCurrentSearch = nSearchID == m_nResultsID;
	if (bCurrentSearch && pCurrentParent->IsListExpanded() && cacheEntry.bHasMatches && !cacheEntry.bRowsLoaded && !cacheEntry.bRowsPending)
		QueuePossibleKnownAvailability(pCurrentParent, true);

	if (bCurrentSearch && pCurrentParent->IsListExpanded() && (!bReplaceRows || bFinalResult) && (cacheEntry.bRowsLoaded || !cacheEntry.rows.empty())) {
		if (bReplaceRows)
			QueueDeferredReload(false, kSearchListViewState, 50, true);
		else {
			SaveListState(m_nResultsID, kSearchListViewState);
			SetRedraw(false);
			RebuildPossibleKnownRows();
			RebuildListedItemsMap();
			UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
			RestoreListState(m_nResultsID, kSearchListViewState, false);
			SetRedraw(true);
			Invalidate(FALSE);
		}
	} else if (bCurrentSearch) {
		int iParent = -1;
		if (m_SearchItemsMap.Lookup(pParent, iParent) && iParent >= 0)
			RequestRowRedrawAsync(iParent, iParent);
	}
}

void CSearchListCtrl::CancelPendingPossibleKnownProcessing(uint32 nSearchID)
{
	if (nSearchID == 0 || nSearchID != m_nResultsID)
		return;
	ClearPossibleKnownAvailabilityQueue();
}

bool CSearchListCtrl::ApplyPreparedPossibleKnownCaches(uint32 nSearchID)
{
	if (nSearchID == 0 || nSearchID != m_nResultsID || !IsPossibleKnownFeatureActive() || theApp.IsClosing() || !::IsWindow(m_hWnd) || m_ListedItemsVector.empty())
		return false;

	bool bImported = false;
	for (std::map<CSearchFile*, SSearchListRow*>::const_iterator it = m_SearchRows.begin(); it != m_SearchRows.end(); ++it) {
		CSearchFile* pParent = it->first;
		if (pParent == NULL || pParent->GetListParent() != NULL || pParent->GetSearchID() != nSearchID)
			continue;
		SPossibleKnownCacheEntry& cacheEntry = m_PossibleKnownCache[pParent];
		if (ImportPossibleKnownCache(pParent, cacheEntry, true))
			bImported = true;
	}
	if (!bImported)
		return false;

	SaveListState(m_nResultsID, kSearchListViewState);
	SetRedraw(false);
	RebuildPossibleKnownRows();
	RebuildListedItemsMap();
	UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
	RestoreListState(m_nResultsID, kSearchListViewState, false);
	m_uPossibleKnownRevision = theApp.DownloadValidator->GetPossibleKnownRevision();
	m_uPossibleKnownCandidateDataRevision = theApp.DownloadValidator->GetCandidateDataRevision();
	SetRedraw(true);
	Invalidate(FALSE);
	return true;
}

bool CSearchListCtrl::HasPendingPossibleKnownProcessing(uint32 nSearchID) const
{
	return nSearchID != 0 && nSearchID == m_nResultsID
		&& (m_uNextPossibleKnownAvailability < m_PossibleKnownAvailabilityQueue.size() || m_bDeferredSearchReloadPending);
}

void CSearchListCtrl::QueuePossibleKnownRefresh(UINT uDelayMs)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	ClearPossibleKnownAvailabilityQueue();
	m_PossibleKnownCache.clear();
	QueueDeferredReload(false, LSF_SELECTION, uDelayMs, true);
}

void CSearchListCtrl::QueuePossibleKnownSoftRefresh()
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || theApp.DownloadValidator == NULL || !IsPossibleKnownFeatureEnabled())
		return;

	const uint32 uCandidateDataRevision = theApp.DownloadValidator->GetCandidateDataRevision();
	m_uPossibleKnownCandidateDataRevision = uCandidateDataRevision;
	for (std::map<CSearchFile*, SPossibleKnownCacheEntry>::iterator it = m_PossibleKnownCache.begin(); it != m_PossibleKnownCache.end(); ++it) {
		CSearchFile* pParent = it->first;
		SPossibleKnownCacheEntry& cacheEntry = it->second;
		if (pParent == NULL || pParent->GetListParent() != NULL || pParent->GetSearchID() != m_nResultsID || cacheEntry.uRevision != theApp.DownloadValidator->GetPossibleKnownRevision())
			continue;
		std::vector<CString> astrFileNames;
		uint32 uCurrentAliasFingerprint = 0;
		const bool bAliasCurrent = searchlist != NULL && searchlist->BuildPossibleKnownAliasNames(pParent, astrFileNames, uCurrentAliasFingerprint)
			&& cacheEntry.uAliasFingerprint == uCurrentAliasFingerprint;
		if (bAliasCurrent && (cacheEntry.uCandidateDataRevision == uCandidateDataRevision || (cacheEntry.bReplaceRowsPending && cacheEntry.uPendingCandidateDataRevision == uCandidateDataRevision)))
			continue;
		const bool bLoadRows = cacheEntry.bRowsLoaded || !cacheEntry.rows.empty() || pParent->IsListExpanded();
		QueuePossibleKnownAvailability(pParent, bLoadRows, true);
	}
}

void CSearchListCtrl::QueueDeferredReload(const bool bSortCurrentList, const EListStateField LsfFlag, UINT uDelayMs, bool bKeepPendingWhileInactive)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	m_bDeferredSearchReloadPending = true;
	m_bDeferredSearchReloadSort = m_bDeferredSearchReloadSort || bSortCurrentList;
	m_bDeferredSearchReloadKeepPendingWhileInactive = m_bDeferredSearchReloadKeepPendingWhileInactive || bKeepPendingWhileInactive;
	m_eDeferredSearchReloadState = LsfFlag;
	if (uDelayMs == 0)
		uDelayMs = kDeferredSearchReloadDelayMs;
	if (SetTimer(kTimerDeferredSearchReload, uDelayMs, NULL) == 0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Deferred search reload timer could not be started. Falling back to immediate reload. sort=%u\n"), bSortCurrentList ? 1U : 0U);
		KillTimer(kTimerDeferredSearchReload);
		m_bDeferredSearchReloadPending = false;
		m_bDeferredSearchReloadSort = false;
		m_bDeferredSearchReloadKeepPendingWhileInactive = false;
		ReloadList(bSortCurrentList, LsfFlag);
	}
}

void CSearchListCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible())
		return;

	const DWORD dwReloadStartTick = ::GetTickCount();
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList

	// Initializing the vector and map
	if (bInitializing) {
		m_iDataSize = 10007; // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
		m_SearchItemsMap.InitHashTable(m_iDataSize);
	} else
		SaveListState(m_nResultsID, LsfFlag); // Save selections, sort and scroll values for the previous m_nResultsID if this is not the first call.

	// ReloadList should always use search ID of the active search tab.
	bool bCurrentClientSharedFiles = false;
	int cur_sel = theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetCurSel();
	if (cur_sel >= 0) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetItem(cur_sel, &item) && item.lParam != NULL) {
			SSearchParams *pSearchParams = reinterpret_cast<SSearchParams*>(item.lParam);
			m_nResultsID = pSearchParams->dwSearchID;
			bCurrentClientSharedFiles = pSearchParams->bClientSharedFiles;
		}
	}

	if (theApp.searchlist == NULL)
		return;

	SetRedraw(false); // Suspend painting
	if (!bInitializing && (LsfFlag & LSF_SELECTION) != 0) {
		SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(-1);
	}
	{
		CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
		const SearchList* list = theApp.searchlist->GetSearchListForID(m_nResultsID);
		if (!list) {
			m_lListedItemsModelSequence = 0;
			SetRedraw(true);
			return;
		}

		if (!bSortCurrentList)
			BuildVisibleSearchItems(*list, m_ListedItemsVector);

		SortListedItemsRaw();
	}
	GroupListedItemsByBottomCandidates();
	RebuildListedItemsMap();
	if (theApp.DownloadValidator != NULL) {
		m_uPossibleKnownRevision = theApp.DownloadValidator->GetPossibleKnownRevision();
		m_uPossibleKnownCandidateDataRevision = theApp.DownloadValidator->GetCandidateDataRevision();
	}

	UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.

	if (!bInitializing)
		RestoreListState(m_nResultsID, LsfFlag, false); // Restore selections, sort and scroll values if this is not the first call.

	UpdateTabHeader(m_nResultsID, EMPTY, false);
	SetRedraw(true); // Resume painting
	Invalidate(); //Force redraw
	DWORD dwReloadElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwReloadStartTick, CemuleApp::TimeBudgetSearchRedraw, &dwReloadElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchRedraw, _T("SearchListCtrl::ReloadList"), dwReloadElapsed, static_cast<UINT>(m_ListedItemsVector.size()), 0);
}

// Index map after vector changes
void CSearchListCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	m_SearchItemsMap.RemoveAll();
	if (!m_ListedItemsVector.empty()) {
		const UINT uHashSize = static_cast<UINT>(m_ListedItemsVector.size() * 2 + 1);
		m_ListedItemsMap.InitHashTable(uHashSize);
		m_SearchItemsMap.InitHashTable(uHashSize);
	}
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i) {
		SSearchListRow* pRow = m_ListedItemsVector[static_cast<size_t>(i)];
		if (pRow == NULL)
			continue;
		m_ListedItemsMap[pRow] = i;
		if (pRow->eType == SearchListRowSearchFile && pRow->pSearchFile != NULL)
			m_SearchItemsMap[pRow->pSearchFile] = i;
	}
	MarkListedModelCurrent();
}

const bool CSearchListCtrl::SortFunc(const CSearchFile* first, const CSearchFile* second)
{
	return SortProc((LPARAM)first, (LPARAM)second, m_pSortParam) < 0; // If the first one has a smaller value returns true, otherwise returns false.
}

CObject* CSearchListCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || static_cast<size_t>(iIndex) >= m_ListedItemsVector.size())
		return nullptr;
	return ResolveSearchFileByRowIndex(iIndex);
}

void CSearchListCtrl::UpdateTabHeader(uint32 nResultsID, CString strClientHash, bool bUpdateAllSharedListTabs)
{
	CClosableTabCtrl &searchselect = theApp.emuledlg->searchwnd->GetSearchSelector();
	TCITEM ti;
	ti.mask = TCIF_PARAM;

	for (int iTabIndex = searchselect.GetItemCount(); --iTabIndex >= 0;)
		if (searchselect.GetItem(iTabIndex, &ti) && ti.lParam != NULL) {
			const SSearchParams* pSearchParams = reinterpret_cast<SSearchParams*>(ti.lParam);
			// Update tab header for the specified search ID or client hash, or all shared file tabs
			if ((pSearchParams->dwSearchID == nResultsID && strClientHash.IsEmpty() && !bUpdateAllSharedListTabs) // A specific nResultsID is given
				|| (!strClientHash.IsEmpty() && strClientHash == pSearchParams->m_strClientHash) || (bUpdateAllSharedListTabs && pSearchParams->bClientSharedFiles)) { // A specific m_strClientHash is given or bUpdateAllSharedListTabs is true
				CString strTabLabel(pSearchParams->strSearchTitle);

				if (pSearchParams->bClientSharedFiles && (thePrefs.GetRemoteSharedFilesUserHash() || thePrefs.GetRemoteSharedFilesClientNote()) && !pSearchParams->m_strClientHash.IsEmpty()) {
					CString m_strClientHash = pSearchParams->m_strClientHash;
					uchar m_uchClientHash[MDX_DIGEST_SIZE];
					if (strmd4(m_strClientHash, m_uchClientHash)) {
						CUpDownClient* pTabClient = theApp.clientlist != NULL
							? theApp.clientlist->AcquireTrackedClientByUserHash(m_uchClientHash, thePrefs.GetClientHistory())
							: NULL;
						if (pTabClient != NULL) {
							if (thePrefs.GetRemoteSharedFilesUserHash() && !isnulmd4(pTabClient->GetUserHash()))
								strTabLabel = md4str(pTabClient->GetUserHash()); // Replace search title with client hash

							if (thePrefs.GetRemoteSharedFilesClientNote() && !pTabClient->m_strClientNote.IsEmpty())
								strTabLabel.AppendFormat(_T(" [%s]"), (LPCTSTR)pTabClient->m_strClientNote); // Append client note

							pTabClient->ReleaseRuntimeReference();
						}
					}
				}

				const uint32 m_uSearchID = pSearchParams->dwSearchID;
				const uint32 m_uOriginalResultCount = theApp.searchlist->GetOriginalFoundFiles(m_uSearchID);
				const uint32 m_uParentItemsCount = theApp.searchlist->GetParentItemCount(m_uSearchID);
				const bool m_bHasMergeHistory = theApp.searchlist->HasMergedSearchHistory(m_uSearchID);

				if (m_bHasMergeHistory || m_uParentItemsCount != m_uOriginalResultCount) {
					strTabLabel.AppendFormat(_T(" (%u/%u)"), m_uParentItemsCount, m_uOriginalResultCount);
				} else if (m_uParentItemsCount > 0) {
					strTabLabel.AppendFormat(_T(" (%u)"), m_uParentItemsCount);
				}

				DupAmpersand(strTabLabel);
				ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTabLabel);
				ti.mask = TCIF_TEXT;
				searchselect.SetItem(iTabIndex, &ti);
				theApp.emuledlg->searchwnd->m_pwndResults->searchselect.UpdateTabToolTips(iTabIndex);
				if (searchselect.GetCurSel() != iTabIndex)
					searchselect.HighlightItem(iTabIndex);
				break;
			}
		}
}

bool CSearchListCtrl::IsComplete(const CSearchFile *pFile, UINT uSources) const
{

	// '< 0' ... unknown; treat 'unknown' as complete
	// '> 0' ... complete

	// '= 0' ... not complete
	return pFile->IsComplete(uSources, pFile->GetCompleteSourceCount()) != 0;

}

CString CSearchListCtrl::GetCompleteSourcesDisplayString(const CSearchFile *pFile, UINT uSources, bool *pbComplete) const
{
	UINT uCompleteSources = pFile->GetCompleteSourceCount();
	int iComplete = pFile->IsComplete(uSources, uCompleteSources);

	// If we have no 'Complete' info at all but the file size is <= PARTSIZE,
	// though we know that the file is complete (otherwise it would not be shared).
	if (iComplete < 0 && (uint64)pFile->GetFileSize() <= PARTSIZE) {
		iComplete = 1;
		// If this search result is from a remote client's shared file list, we know the 'complete' count.
		if (pFile->GetDirectory() != NULL)
			uCompleteSources = 1;
	}

	CString str;
	if (iComplete < 0) {		// '< 0' ... unknown
		str += _T('?');
		if (pbComplete)
			*pbComplete = true;	// treat 'unknown' as complete
	} else if (iComplete > 0) {	// '> 0' ... we know it's complete
		if (uSources && uCompleteSources) {
			str.Format(_T("%u%%"), (uCompleteSources * 100) / uSources);
			if (thePrefs.IsExtControlsEnabled())
				str.AppendFormat(_T(" (%u)"), uCompleteSources);
		} else {
			// we know it's complete, but we don't know the degree. (for files <= PARTSIZE in Kad searches)
			str = GetResString(_T("YES"));
		}
		if (pbComplete)
			*pbComplete = true;
	} else {					// '= 0' ... we know it's not complete
		str = _T("0%");
		if (thePrefs.IsExtControlsEnabled())
			str.AppendFormat(_T(" (0)"));
		if (pbComplete)
			*pbComplete = false;
	}
	return str;
}

int CSearchListCtrl::CompareSearchFilesRaw(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort) const
{
	if (item1 == NULL || item2 == NULL)
		return item1 == item2 ? 0 : (item1 == NULL ? 1 : -1);
	const bool bDirect = !HIWORD(lParamSort);
	int iResult = 0;
	if (item1->GetListParent() == NULL && item2->GetListParent() != NULL) {
		if (item1 == item2->GetListParent())
			return -1;
		iResult = Compare(item1, item2->m_list_parent, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item2->GetListParent() == NULL && item1->GetListParent() != NULL) {
		if (item1->m_list_parent == item2)
			return 1;
		iResult = Compare(item1->GetListParent(), item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item1->GetListParent() == NULL) {
		iResult = Compare(item1, item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else {
		iResult = Compare(item1->GetListParent(), item2->GetListParent(), lParamSort, bDirect);
		if (iResult != 0)
			return bDirect ? iResult : -iResult;

		if ((item1->GetListParent() == NULL && item2->GetListParent() != NULL) || (item2->GetListParent() == NULL && item1->GetListParent() != NULL))
			return item1->GetListParent() ? 1 : -1;
		iResult = CompareChild(item1, item2, lParamSort);
	}

	if (iResult == 0 && thePrefs.GetGroupKnownAtTheBottom()) {
		const CSearchFile *pSortItem1 = GetSearchFileSortGroupFile(item1);
		const CSearchFile *pSortItem2 = GetSearchFileSortGroupFile(item2);
		iResult = CompareUnsigned(static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem1)), static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem2)));
	}

	if (iResult == 0) {
		LPARAM iNextSort = GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = CompareSearchFilesRaw(item1, item2, iNextSort);
	}

	return iResult;
}

void CSearchListCtrl::SortListedItemsRaw()
{
	ClearPossibleKnownRows();
	if (theApp.searchlist == NULL)
		return;

	std::vector<CSearchFile*> searchFiles;
	searchFiles.reserve(m_ListedItemsVector.size());
	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		SSearchListRow* pRow = m_ListedItemsVector[i];
		if (pRow != NULL && pRow->eType == SearchListRowSearchFile && pRow->pSearchFile != NULL)
			searchFiles.push_back(pRow->pSearchFile);
	}
	if (searchFiles.size() > 1) {
		CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
		CombinedSort(searchFiles.begin(), searchFiles.end(), [this](const CSearchFile* left, const CSearchFile* right) -> bool
		{
			return CompareSearchFilesRaw(left, right, m_pSortParam) < 0;
		});
		GroupSearchItemsByBottomCandidates(searchFiles);
	}
	m_ListedItemsVector.clear();
	m_ListedItemsVector.reserve(searchFiles.size());
	for (size_t i = 0; i < searchFiles.size(); ++i)
		m_ListedItemsVector.push_back(GetOrCreateSearchRow(searchFiles[i]));
}

bool CSearchListCtrl::GroupListedItemsByBottomCandidates()
{
	std::map<CSearchFile*, bool> collapsedParents;
	std::vector<SSearchListRow*> hiddenRows;
	if (theApp.searchlist != NULL) {
		CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
		for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
			SSearchListRow* pRow = m_ListedItemsVector[i];
			CSearchFile* pFile = pRow != NULL && pRow->eType == SearchListRowSearchFile ? pRow->pSearchFile : NULL;
			if (pFile == NULL || pFile->GetListParent() != NULL)
				continue;

			const uint8 uCurrentStatusFlags = GetSearchFileBottomGroupStatusFlags(pFile);
			const bool bNewBottomGroupStatus = (uCurrentStatusFlags & ~pRow->uBottomGroupStatusFlags) != 0;
			pRow->uBottomGroupStatusFlags = uCurrentStatusFlags;
			const bool bIsBottomGroupItem = IsSearchFileSpamOrBlacklistedBottomGroup(pFile) || pFile->GetKnownType() == CSearchFile::Downloading;
			if (thePrefs.GetGroupKnownAtTheBottom() && bNewBottomGroupStatus && bIsBottomGroupItem && pFile->IsListExpanded()) {
				pFile->SetListExpanded(false);
				collapsedParents[pFile] = true;
			}
		}

		if (!collapsedParents.empty()) {
			for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
				SSearchListRow* pRow = m_ListedItemsVector[i];
				CSearchFile* pFile = pRow != NULL && pRow->eType == SearchListRowSearchFile ? pRow->pSearchFile : NULL;
				if (pFile != NULL && pFile->GetListParent() != NULL && collapsedParents.find(pFile->GetListParent()) != collapsedParents.end())
					hiddenRows.push_back(pRow);
			}
		}
	}

	if (!hiddenRows.empty()) {
		RemoveRowsFromSavedStates(hiddenRows);
		std::map<const SSearchListRow*, bool> hiddenRowMap;
		for (size_t i = 0; i < hiddenRows.size(); ++i)
			hiddenRowMap[hiddenRows[i]] = true;
		m_ListedItemsVector.erase(std::remove_if(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), [&hiddenRowMap](const SSearchListRow* pRow) {
			return hiddenRowMap.find(pRow) != hiddenRowMap.end();
		}), m_ListedItemsVector.end());
	}

	RebuildPossibleKnownRows();
	return true;
}

void CSearchListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case colSearchAvailability:
		case colSearchCompleteSources:
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	// Although SortItems will not sort anything since this is a virtual list, it will save sort parameter
	// history in m_liSortHistory which will be used when we call GetNextSortOrder.
	m_pSortParam = MAKELONG(pNMLV->iSubItem, !sortAscending);
	SortItems(SortProc, m_pSortParam);
	ReloadList(true, kSearchListViewState);
	*pResult = 0;
}

int CALLBACK CSearchListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const CSearchFile *item1 = reinterpret_cast<CSearchFile*>(lParam1);
	const CSearchFile *item2 = reinterpret_cast<CSearchFile*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return item1 == item2 ? 0 : (item1 == NULL ? 1 : -1);
	if (theApp.searchlist == NULL)
		return 0;
	CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
	bool bDirect = !HIWORD(lParamSort);

	int iResult;
	if (item1->GetListParent() == NULL && item2->GetListParent() != NULL) {
		if (item1 == item2->GetListParent())
			return -1;
		iResult = Compare(item1, item2->m_list_parent, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item2->GetListParent() == NULL && item1->GetListParent() != NULL) {
		if (item1->m_list_parent == item2)
			return 1;
		iResult = Compare(item1->GetListParent(), item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item1->GetListParent() == NULL) {
		iResult = Compare(item1, item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else {
		iResult = Compare(item1->GetListParent(), item2->GetListParent(), lParamSort, bDirect);
		if (iResult != 0)
			return bDirect ? iResult : -iResult;

		if ((item1->GetListParent() == NULL && item2->GetListParent() != NULL) || (item2->GetListParent() == NULL && item1->GetListParent() != NULL))
			return item1->GetListParent() ? 1 : -1;
		iResult = CompareChild(item1, item2, lParamSort);
	}

	if (iResult == 0 && thePrefs.GetGroupKnownAtTheBottom()) {
		const CSearchFile *pSortItem1 = GetSearchFileSortGroupFile(item1);
		const CSearchFile *pSortItem2 = GetSearchFileSortGroupFile(item2);
		iResult = CompareUnsigned(static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem1)), static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem2)));
	}

	// Call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = SortProc(lParam1, lParam2, iNextSort);
	}

	return iResult;
}

int CSearchListCtrl::CompareChild(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort)
{
	int iResult;
	switch (LOWORD(lParamSort)) {
	case colSearchFileName:
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case colSearchAichHash:
		iResult = CompareAICHHash(item1->GetFileIdentifierC(), item2->GetFileIdentifierC(), true);
		break;
	default: // always sort by descending availability
		iResult = -CompareUnsigned(item1->GetSourceCount(), item2->GetSourceCount());
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

int CSearchListCtrl::Compare(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort, bool bSortAscending)
{
	const CSearchFile *pSortItem1 = GetSearchFileSortGroupFile(item1);
	const CSearchFile *pSortItem2 = GetSearchFileSortGroupFile(item2);
	const int iBottomGroupResult = CompareSearchFixedGroupRank(GetSearchFileBottomGroupRank(pSortItem1), GetSearchFileBottomGroupRank(pSortItem2), bSortAscending);
	if (iBottomGroupResult != 0)
		return iBottomGroupResult;

	switch (LOWORD(lParamSort)) {
	case colSearchFileName:
		return CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
	case colSearchSize:
		return CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
	case colSearchType:
		{
			int iResult = item1->GetFileTypeDisplayStr().Compare(item2->GetFileTypeDisplayStr());
			if (iResult)
				return iResult;
			// the types are equal, sub-sort by extension
			LPCTSTR pszExt1 = ::PathFindExtension(item1->GetFileName());
			LPCTSTR pszExt2 = ::PathFindExtension(item2->GetFileName());
			if (!*pszExt1 ^ !*pszExt2)
				return *pszExt1 ? -1 : 1;
			return *pszExt1 ? _tcsicmp(pszExt1, pszExt2) : 0;
		}
	case colSearchLength:
		return CompareUnsignedUndefinedAtBottom(item1->GetIntTagValue(FT_MEDIA_LENGTH), item2->GetIntTagValue(FT_MEDIA_LENGTH), bSortAscending);
	case colSearchAvailability:
		return CompareUnsigned(item1->GetSourceCount(), item2->GetSourceCount());
	case colSearchCompleteSources:
		if (item1->GetSourceCount() == 0 || item2->GetSourceCount() == 0 || item1->IsKademlia() || item2->IsKademlia())
			return 0; // should never happen, just a sanity check
		return CompareUnsigned((item1->GetCompleteSourceCount() * 100) / item1->GetSourceCount(), (item2->GetCompleteSourceCount() * 100) / item2->GetSourceCount());
	case colSearchKnown:
		if (thePrefs.GetGroupKnownAtTheBottom())
			return CompareSearchFixedGroupRank(GetSearchFileKnownTieRank(pSortItem1), GetSearchFileKnownTieRank(pSortItem2), bSortAscending);
		return CompareOptLocaleStringNoCase(GetKnownTypeStr(item1), GetKnownTypeStr(item2));
	case colSearchBitrate:
		return CompareUnsignedUndefinedAtBottom(item1->GetIntTagValue(FT_MEDIA_BITRATE), item2->GetIntTagValue(FT_MEDIA_BITRATE), bSortAscending);
	case colSearchCodec:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(GetCodecDisplayName(item1->GetStrTagValue(FT_MEDIA_CODEC)), GetCodecDisplayName(item2->GetStrTagValue(FT_MEDIA_CODEC)), bSortAscending);
	case colSearchFileId:
		return memcmp(item1->GetFileHash(), item2->GetFileHash(), 16);
	case colSearchFolder:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetDirectory(), item2->GetDirectory(), bSortAscending);
	case colSearchAlbum:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_ALBUM), item2->GetStrTagValue(FT_MEDIA_ALBUM), bSortAscending);
	case colSearchTitle:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_TITLE), item2->GetStrTagValue(FT_MEDIA_TITLE), bSortAscending);
	case colSearchArtist:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_ARTIST), item2->GetStrTagValue(FT_MEDIA_ARTIST), bSortAscending);
	case colSearchAichHash:
		return CompareAICHHash(item1->GetFileIdentifierC(), item2->GetFileIdentifierC(), bSortAscending);
	case colSearchSpamRating:
		return CompareUnsigned(item1->GetSpamRating(), item2->GetSpamRating());
	}
	return 0;
}

void CSearchListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iContextItem = GetNextItem(-1, LVIS_FOCUSED);
	if (point.x != -1 && point.y != -1) {
		CPoint clientPoint(point);
		ScreenToClient(&clientPoint);
		iContextItem = HitTest(clientPoint);
	}

	const SSearchListRow* pContextRow = ResolveRowByIndex(iContextItem);
	if (pContextRow != NULL && pContextRow->eType == SearchListRowPossibleKnownHeader)
		return;

	bool bPossibleKnownContext = pContextRow != NULL && pContextRow->eType == SearchListRowPossibleKnownFile;
	if (bPossibleKnownContext && (GetItemState(iContextItem, LVIS_SELECTED) & LVIS_SELECTED) == 0) {
		SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iContextItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iContextItem);
	}

	std::vector<const SSearchListRow*> selectedPossibleKnownRows;
	if (bPossibleKnownContext)
		bPossibleKnownContext = CollectSelectedPossibleKnownRows(selectedPossibleKnownRows);
	if (!bPossibleKnownContext && (HasSelectedPassiveRows() || IsPassiveRowIndex(GetNextItem(-1, LVIS_FOCUSED))))
		return;

	int iSelected = 0;
	int iToDownload = 0;
	int iToPreview = 0;
	int iDownloadListMatches = 0;
	int iFilesToPreview = 0;
	int iFilesCanPauseOnPreview = 0;
	int iFilesDoPauseOnPreview = 0;
	int iFilesPreviewType = 0;
	int iFilesGetPreviewParts = 0;
	const CPartFile* pSingleDownloadFile = NULL;
	bool bContainsNotSpamFile = false;
	bool m_bContainsNotManualBlacklistedFile = false;
	bool m_bAllInDownloadList = true;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(GetNextSelectedItem(pos));
		if (pSearchFile == NULL)
			continue;

		++iSelected;
		iToPreview += static_cast<int>(pSearchFile->IsPreviewPossible());
		iToDownload += static_cast<int>(theApp.downloadqueue == NULL || !theApp.downloadqueue->IsFileExisting(pSearchFile->GetFileHash(), false));
		const CPartFile* pDownloadFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pSearchFile->GetFileHash()) : NULL;
		if (pDownloadFile == NULL)
			m_bAllInDownloadList = false;
		else {
			++iDownloadListMatches;
			iFilesPreviewType += static_cast<int>(pDownloadFile->IsPreviewableFileType());
			iFilesToPreview += static_cast<int>(pDownloadFile->IsReadyForPreview());
			iFilesCanPauseOnPreview += static_cast<int>(pDownloadFile->IsPreviewableFileType() && !pDownloadFile->IsReadyForPreview() && pDownloadFile->CanPauseFile());
			iFilesDoPauseOnPreview += static_cast<int>(pDownloadFile->IsPausingOnPreview());
			iFilesGetPreviewParts += static_cast<int>(pDownloadFile->GetPreviewPrio());
			if (iSelected == 1)
				pSingleDownloadFile = pDownloadFile;
		}
		if (!pSearchFile->GetManualBlacklisted())
			m_bContainsNotManualBlacklistedFile = true;
		if (!pSearchFile->IsConsideredSpam(false))
			bContainsNotSpamFile = true;
	}

	bool bAllPossibleKnownInDownloadList = bPossibleKnownContext && !selectedPossibleKnownRows.empty() && theApp.downloadqueue != NULL;
	bool bHasCancelablePossibleKnownDownload = false;
	CPartFile* pPossibleKnownPreviewPartFile = NULL;
	CString strPossibleKnownPreviewSharedFilePath;
	if (bAllPossibleKnownInDownloadList) {
		for (size_t i = 0; i < selectedPossibleKnownRows.size(); ++i) {
			CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(selectedPossibleKnownRows[i]->ucHash);
			if (pPartFile == NULL || pPartFile->GetFileSize() != selectedPossibleKnownRows[i]->uSize) {
				bAllPossibleKnownInDownloadList = false;
				break;
			}
			if (pPartFile->GetStatus() != PS_COMPLETING)
				bHasCancelablePossibleKnownDownload = true;
		}
	}
	if (bPossibleKnownContext && selectedPossibleKnownRows.size() == 1) {
		pPossibleKnownPreviewPartFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(selectedPossibleKnownRows[0]->ucHash) : NULL;
		if (pPossibleKnownPreviewPartFile != NULL && pPossibleKnownPreviewPartFile->GetFileSize() != selectedPossibleKnownRows[0]->uSize)
			pPossibleKnownPreviewPartFile = NULL;
		if (pPossibleKnownPreviewPartFile == NULL)
			ResolvePossibleKnownSharedFilePath(selectedPossibleKnownRows[0], strPossibleKnownPreviewSharedFilePath);
	}

	m_SearchFileMenu.EnableMenuItem(MP_RESUME, !bPossibleKnownContext && iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
	if (thePrefs.IsExtControlsEnabled()) {
		m_SearchFileMenu.EnableMenuItem(MP_RESUMEPAUSED, !bPossibleKnownContext && iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
		m_SearchFileMenu.EnableMenuItem(MP_DETAIL, iSelected == 1 ? MF_ENABLED : MF_GRAYED);
	}

	m_SearchFileMenu.EnableMenuItem(MP_BYPASSDOWNLOADVALIDATOR, !bPossibleKnownContext && iSelected > 0 && iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_BYPASSDOWNLOADVALIDATORPAUSED, !bPossibleKnownContext && iSelected > 0 && iToDownload > 0 ? MF_ENABLED : MF_GRAYED);

	const bool bEnableCancel = bPossibleKnownContext ? (bAllPossibleKnownInDownloadList && bHasCancelablePossibleKnownDownload) : (iSelected > 0 && m_bAllInDownloadList);
	m_SearchFileMenu.EnableMenuItem(MP_CANCEL, bEnableCancel ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_CANCEL_FORGET, bEnableCancel ? MF_ENABLED : MF_GRAYED);
	const bool bEnableDownloadOnlyMenu = (iSelected > 0 && m_bAllInDownloadList && iDownloadListMatches == iSelected);
	if (bPossibleKnownContext) {
		if (pPossibleKnownPreviewPartFile != NULL) {
			const bool bPreviewReady = pPossibleKnownPreviewPartFile->IsReadyForPreview();
			const bool bPreviewType = pPossibleKnownPreviewPartFile->IsPreviewableFileType();
			RebuildPreviewMenu(m_PreviewMenu, pPossibleKnownPreviewPartFile, bPreviewReady,
				bPreviewType && !bPreviewReady && pPossibleKnownPreviewPartFile->CanPauseFile(), pPossibleKnownPreviewPartFile->IsPausingOnPreview(),
				bPreviewType && !bPreviewReady, pPossibleKnownPreviewPartFile->GetPreviewPrio(), NULL);
		} else if (!strPossibleKnownPreviewSharedFilePath.IsEmpty())
			RebuildPreviewMenu(m_PreviewMenu, NULL, true, false, false, false, false, strPossibleKnownPreviewSharedFilePath);
		else
			RebuildPreviewMenu(m_PreviewMenu, NULL, false, false, false, false, false, NULL);
	} else
		RebuildPreviewMenu(m_PreviewMenu, (bEnableDownloadOnlyMenu && iSelected == 1) ? pSingleDownloadFile : NULL,
			bEnableDownloadOnlyMenu && iSelected == 1 && iFilesToPreview == 1, bEnableDownloadOnlyMenu && iFilesCanPauseOnPreview > 0,
			bEnableDownloadOnlyMenu && iSelected > 0 && iFilesDoPauseOnPreview == iSelected,
			bEnableDownloadOnlyMenu && iSelected == 1 && iFilesPreviewType == 1 && iFilesToPreview == 0 && iDownloadListMatches == 1,
			bEnableDownloadOnlyMenu && iSelected == 1 && iFilesGetPreviewParts == 1, NULL);
	const bool bEnablePreviewMenu = bPossibleKnownContext ? (pPossibleKnownPreviewPartFile != NULL || !strPossibleKnownPreviewSharedFilePath.IsEmpty()) : bEnableDownloadOnlyMenu;
	m_SearchFileMenu.EnableMenuItem((UINT)m_PreviewMenu.m_hMenu, bEnablePreviewMenu && m_PreviewMenu.HasEnabledItems() ? MF_ENABLED : MF_GRAYED);

	m_SearchFileMenu.EnableMenuItem(MP_CMT, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	const bool bEnableCopyCommands = bPossibleKnownContext ? !selectedPossibleKnownRows.empty() : iSelected > 0;
	m_SearchFileMenu.EnableMenuItem(MP_CUT, bEnableCopyCommands ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_GETED2KLINK, bEnableCopyCommands ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_GETHTMLED2KLINK, bEnableCopyCommands ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVESELECTED, !bPossibleKnownContext && iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVE, theApp.emuledlg->searchwnd->CanDeleteSearches() ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVEALL, theApp.emuledlg->searchwnd->CanDeleteSearches() ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_SEARCHRELATED, (bPossibleKnownContext ? !selectedPossibleKnownRows.empty() : iSelected > 0) && theApp.emuledlg->searchwnd->CanSearchRelatedFiles() ? MF_ENABLED : MF_GRAYED);
	UINT uInsertedMenuItem = 0;
	if (iToPreview == 1 && !(iSelected == 1 && m_bAllInDownloadList)) {
		if (m_SearchFileMenu.InsertMenu(MP_FIND, MF_STRING | MF_ENABLED, MP_PREVIEW, GetResStringWithAccel(_T("PREVIEW_AVAILABLE"), _T('v')), _T("PREVIEW")))
			uInsertedMenuItem = MP_PREVIEW;
	}
	m_SearchFileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);

	UINT uInsertedMenuItem3 = 0;
	if (thePrefs.GetBlacklistManual() && m_SearchFileMenu.InsertMenu(MP_REMOVESELECTED, MF_STRING | MF_ENABLED, MP_MARKASBLACKLISTED, (m_bContainsNotManualBlacklistedFile || iSelected == 0) ? GetResString(_T("MARK_AS_BLACKLISTED")) : GetResString(_T("MARK_AS_NOT_BLACKLISTED")), _T("SPAM_PURPLE"))) {
		uInsertedMenuItem3 = MP_MARKASBLACKLISTED;
		m_SearchFileMenu.EnableMenuItem(MP_MARKASBLACKLISTED, !bPossibleKnownContext && iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	}

	UINT uInsertedMenuItem2 = 0;
	if (thePrefs.IsSearchSpamFilterEnabled() && m_SearchFileMenu.InsertMenu(MP_REMOVESELECTED, MF_STRING | MF_ENABLED, MP_MARKASSPAM, (bContainsNotSpamFile || iSelected == 0) ? GetResString(_T("MARKSPAM")) : GetResString(_T("MARKNOTSPAM")), _T("SPAM"))) {
		uInsertedMenuItem2 = MP_MARKASSPAM;
		m_SearchFileMenu.EnableMenuItem(MP_MARKASSPAM, !bPossibleKnownContext && iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	}

	CMenuXP WebMenu;
	WebMenu.CreateMenu();
	int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
	UINT flag2 = (iWebMenuEntries == 0 || (bPossibleKnownContext ? selectedPossibleKnownRows.empty() : iSelected == 0)) ? MF_GRAYED : MF_STRING;
	m_SearchFileMenu.AppendMenu(MF_POPUP | flag2, (UINT_PTR)WebMenu.m_hMenu, GetResString(_T("WEBSERVICES")), _T("WEB"));

	if (iToDownload > 0)
		m_SearchFileMenu.SetDefaultItem((!thePrefs.AddNewFilesPaused() || !thePrefs.IsExtControlsEnabled()) ? MP_RESUME : MP_RESUMEPAUSED);
	else
		m_SearchFileMenu.SetDefaultItem(UINT_MAX);

	GetPopupMenuPos(*this, point);
	m_SearchFileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	if (uInsertedMenuItem)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem, MF_BYCOMMAND));
	if (uInsertedMenuItem2)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem2, MF_BYCOMMAND));
	if (uInsertedMenuItem3)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem3, MF_BYCOMMAND));
	m_SearchFileMenu.RemoveMenu(m_SearchFileMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	VERIFY(WebMenu.DestroyMenu());
}

BOOL CSearchListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	if (wParam == MP_FIND) {
		OnFindStart();
		return TRUE;
	}

	std::vector<const SSearchListRow*> selectedPossibleKnownRows;
	const bool bPossibleKnownSelection = CollectSelectedPossibleKnownRows(selectedPossibleKnownRows);
	if (bPossibleKnownSelection) {
		if (wParam == MP_CANCEL || wParam == MP_CANCEL_FORGET) {
			ExecutePossibleKnownCancelCommand(static_cast<UINT>(wParam));
			return TRUE;
		}
		if (wParam == MP_CUT || wParam == MP_GETED2KLINK || wParam == MP_GETHTMLED2KLINK) {
			ExecutePossibleKnownCopyCommand(static_cast<UINT>(wParam), selectedPossibleKnownRows);
			return TRUE;
		}
		if (wParam == MP_SEARCHRELATED) {
			ExecutePossibleKnownSearchRelatedCommand(selectedPossibleKnownRows);
			return TRUE;
		}
		if (wParam == MP_PREVIEW || wParam == MP_TRY_TO_GET_PREVIEW_PARTS || wParam == MP_PAUSEONPREVIEW || (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX)) {
			ExecutePossibleKnownPreviewCommand(static_cast<UINT>(wParam), selectedPossibleKnownRows);
			return TRUE;
		}
		if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256) {
			ExecutePossibleKnownWebServiceCommand(static_cast<UINT>(wParam), selectedPossibleKnownRows);
			return TRUE;
		}
		if (wParam != MP_REMOVE && wParam != MP_REMOVEALL)
			return TRUE;
	}

	CTypedPtrList<CPtrList, CSearchFile*> selectedList;
	CTypedPtrList<CPtrList, CPartFile*> selectedDownloadList;
	CPartFile* pSingleDownloadFile = NULL;
	CollectSelectedSearchFiles(selectedList);
	if (!bPossibleKnownSelection && (HasSelectedPassiveRows() || IsPassiveRowIndex(GetNextItem(-1, LVIS_FOCUSED))))
		return TRUE;
	for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pSearchFile = selectedList.GetNext(pos);
		CPartFile* pDownloadFile = pSearchFile != NULL ? theApp.downloadqueue->GetFileByID(pSearchFile->GetFileHash()) : NULL;
		if (pDownloadFile != NULL) {
			selectedDownloadList.AddTail(pDownloadFile);
			if (selectedDownloadList.GetCount() == 1)
				pSingleDownloadFile = pDownloadFile;
		}
	}

	if (!selectedList.IsEmpty()) {
		CSearchFile *file = selectedList.GetHead();
		bool m_bAddToCanceledMet = true;

		switch (wParam) {
		case MP_CUT:
			{
				CString m_strFileNames;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!m_strFileNames.IsEmpty())
							m_strFileNames += _T("\r\n");
						m_strFileNames += file->GetFileName();
					}
				}

				if (!m_strFileNames.IsEmpty()) {
					theApp.CopyTextToClipboard(m_strFileNames);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("FILE_NAME_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			break;
		case MP_COPYSELECTED:
		case MP_GETED2KLINK:
		{
				CString clpbrd;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!clpbrd.IsEmpty())
							clpbrd += _T("\r\n");
						clpbrd += file->GetED2kLink();
					}
				}

				if (!clpbrd.IsEmpty()) {
					theApp.CopyTextToClipboard(clpbrd);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			return TRUE;
		case MP_GETHTMLED2KLINK:
			{
				CString clpbrd;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!clpbrd.IsEmpty())
							clpbrd += _T("<br>\r\n");
						clpbrd += file->GetED2kLink(false, true);
					}
				}

				if (!clpbrd.IsEmpty()) {
					theApp.CopyTextToClipboard(clpbrd);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			return TRUE;
		case MP_RESUME:
			if (thePrefs.IsExtControlsEnabled())
				theApp.emuledlg->searchwnd->DownloadSelected(false);
			else
				theApp.emuledlg->searchwnd->DownloadSelected();
			return TRUE;
		case MP_RESUMEPAUSED:
			theApp.emuledlg->searchwnd->DownloadSelected(true);
			return TRUE;
		case MP_BYPASSDOWNLOADVALIDATOR:
			theApp.emuledlg->searchwnd->DownloadSelected(false, true);
			return TRUE;
		case MP_BYPASSDOWNLOADVALIDATORPAUSED:
			theApp.emuledlg->searchwnd->DownloadSelected(true, true);
			return TRUE;
		case MP_CANCEL_FORGET:
			m_bAddToCanceledMet = false;
		case MP_CANCEL:
			{
				CWaitCursor curWait;
				if (selectedList.GetCount() > 0) {
					CString fileList(GetResString(selectedList.GetCount() == 1 ? _T("Q_CANCELDL2") : _T("Q_CANCELDL")));
					bool validdelete = false;
					bool removecompl = false;
					int cFiles = 0;
					const int iMaxDisplayFiles = 10;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						file = selectedList.GetNext(pos);
						const CPartFile* cur_file = theApp.downloadqueue->GetFileByID(file->GetFileHash());
						if (cur_file == NULL)
							continue;
						if (cur_file->GetStatus() != PS_COMPLETING && cur_file->GetStatus() != PS_COMPLETE) {
							validdelete = true;
							if (++cFiles < iMaxDisplayFiles)
								fileList.AppendFormat(_T("\n%s"), (LPCTSTR)cur_file->GetFileName());
							else if (cFiles == iMaxDisplayFiles && pos != NULL)
								fileList += _T("\n...");
						} else if (cur_file->GetStatus() == PS_COMPLETE)
							removecompl = true;
					}

					if ((removecompl && !validdelete) || (validdelete && CDarkMode::MessageBox(fileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES)) {
						bool bRemovedItems = false;
						for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
							file = selectedList.GetNext(pos);
							CPartFile* partfile = theApp.downloadqueue->GetFileByID(file->GetFileHash());
							if (partfile == NULL)
								continue;
							theApp.emuledlg->transferwnd->GetDownloadList()->HideSources(partfile);
							switch (partfile->GetStatus()) {
							case PS_WAITINGFORHASH:
							case PS_HASHING:
							case PS_COMPLETING:
								break;
							case PS_COMPLETE:
								{
									bool delsucc = ShellDeleteFile(partfile->GetFilePath());
									if (delsucc)
										theApp.sharedfiles->RemoveFile(partfile, true);
									else {
										CString strError;
										strError.Format(GetResString(_T("ERR_DELFILE")) + _T("\r\n\r\n%s"), (LPCTSTR)partfile->GetFilePath(), (LPCTSTR)GetErrorMessage(::GetLastError()));
										CDarkMode::MessageBox(strError);
									}

									theApp.emuledlg->transferwnd->GetDownloadList()->RemoveFile(partfile);
									theApp.searchlist->SetSearchItemKnownType(file);
									UpdateSearch(file);
								}
								break;
							default:
								if (partfile->GetCategory())
									theApp.downloadqueue->StartNextFileIfPrefs(partfile->GetCategory());
							case PS_PAUSED:
								partfile->DeletePartFile(m_bAddToCanceledMet);
								theApp.searchlist->SetSearchItemKnownType(file);
								UpdateSearch(file);
							}
						}
						if (bRemovedItems) {
							AutoSelectItem();
							theApp.emuledlg->transferwnd->UpdateCatTabTitles();
						}
					}
				}
			}
			return TRUE;
		case IDA_ENTER:
			theApp.emuledlg->searchwnd->DownloadSelected();
			return TRUE;
		case MP_REMOVESELECTED:
		case MPG_DELETE:
			StartChunkedRemoveSelectedSearchResults(selectedList);
			return TRUE;
		case MP_DETAIL:
		case MPG_ALTENTER:
		case MP_CMT:
			{
				CSearchResultFileDetailSheet sheet(selectedList, (wParam == MP_CMT ? IDD_COMMENTLST : 0), this);
				sheet.DoModal();
			}
			return TRUE;
		case MP_TRY_TO_GET_PREVIEW_PARTS:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->SetPreviewPrio(!pSingleDownloadFile->GetPreviewPrio());
			return TRUE;
		case MP_PREVIEW:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL) {
				pSingleDownloadFile->PreviewFile();
				return TRUE;
			}
			if (file) {
				if (file->GetPreviews().GetSize() > 0) {
					// already have previews
					(new PreviewDlg())->SetFile(file);
				} else {
					CUpDownClient *newclient = new CUpDownClient(NULL, file->GetClientPort(), file->GetClientID(), file->GetClientServerIP(), file->GetClientServerPort(), true); // IPv6-TODO: Check this
					if (!theApp.clientlist->AttachToAlreadyKnown(&newclient, NULL))
						theApp.clientlist->AddClient(newclient);

					newclient->SendPreviewRequest(*file);
					// add to res - later
						AddLogLine(true, GetResString(_T("PREVIEW_REQUESTED_WAIT")));
				}
			}
			return TRUE;
		case MP_PREVIEW1:
		case MP_PREVIEW2:
		case MP_PREVIEW3:
		case MP_PREVIEW4:
		case MP_PREVIEW5:
		case MP_PREVIEW6:
		case MP_PREVIEW7:
		case MP_PREVIEW8:
		case MP_PREVIEW9:
		case MP_PREVIEW10:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->PreviewFile((UINT)wParam - MP_PREVIEW1);
			return TRUE;
		case MP_PAUSEONPREVIEW:
			{
				bool bAllPausedOnPreview = true;
				for (POSITION pos = selectedDownloadList.GetHeadPosition(); pos != NULL && bAllPausedOnPreview;)
					bAllPausedOnPreview = selectedDownloadList.GetNext(pos)->IsPausingOnPreview();
				while (!selectedDownloadList.IsEmpty()) {
					CPartFile* pPartFile = selectedDownloadList.RemoveHead();
					if (pPartFile->IsPreviewableFileType() && !pPartFile->IsReadyForPreview())
						pPartFile->SetPauseOnPreview(!bAllPausedOnPreview);
				}
			}
			return TRUE;
		case MP_SEARCHRELATED:
			// just a shortcut for the user typing into the search field "related::[filehash]"
			theApp.emuledlg->searchwnd->SearchRelatedFiles(selectedList);
			return TRUE;
		case MP_MARKASSPAM:
			{
				CWaitCursor curWait;
				SetRedraw(false);
				bool bContainsNotSpamFile = false;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (!file->IsConsideredSpam(false)) {
						bContainsNotSpamFile = true;
						break;
					}
				}
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file->IsConsideredSpam(false)) {
						if (!bContainsNotSpamFile)
							theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsNotSpam, true, 0);
					} else if (bContainsNotSpamFile)
						theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsSpam, true, 0);
				}
				SetRedraw(true);
			}
			return TRUE;
		case MP_MARKASBLACKLISTED:
			{
				CWaitCursor curWait;
				SetRedraw(false);
				bool bContainsNotBlacklistedFile = false;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (!file->GetManualBlacklisted()) {
						bContainsNotBlacklistedFile = true;
						break;
					}
				}
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file->GetManualBlacklisted()) {
						if (!bContainsNotBlacklistedFile)
							theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsNotBlacklisted, true, 0);
					} else if (bContainsNotBlacklistedFile)
						theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsBlacklisted, true, 0);
				}
				SetRedraw(true);
			}
			return TRUE;
		default:
			if (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX) {
				if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
					thePreviewApps.RunApp(pSingleDownloadFile, (UINT)wParam);
				return TRUE;
			}
			if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256) {
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) 
						theWebServices.RunURL(file, (UINT)wParam);
				}
				return TRUE;
			}
		}
	}
	switch (wParam) {
	case MP_REMOVEALL:
		{
			CWaitCursor curWait;
			theApp.emuledlg->searchwnd->DeleteAllSearches();
		}
		break;
	case MP_REMOVE:
		{
			CWaitCursor curWait;
			theApp.emuledlg->searchwnd->DeleteSearch(m_nResultsID);
		}
	}

	return FALSE;
}

void CSearchListCtrl::OnLvnDeleteAllItems(LPNMHDR, LRESULT *pResult)
{
	// To suppress subsequent LVN_DELETEITEM notification messages, return TRUE.
	*pResult = TRUE;
}

void CSearchListCtrl::CreateMenus()
{
	if (m_SearchFileMenu.m_hMenu != NULL) {
		if (::IsMenu(m_SearchFileMenu.m_hMenu))
			VERIFY(m_SearchFileMenu.DestroyMenu());
		else
			m_SearchFileMenu.m_hMenu = NULL;
	}
	if (m_PreviewMenu.m_hMenu != NULL) {
		if (::IsMenu(m_PreviewMenu.m_hMenu))
			VERIFY(m_PreviewMenu.DestroyMenu());
		else
			m_PreviewMenu.m_hMenu = NULL;
	}

	m_SearchFileMenu.CreatePopupMenu();
	m_SearchFileMenu.AddMenuSidebar(GetResString(_T("FILE")));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_RESUME, GetResString(_T("DOWNLOAD")), _T("RESUME"));
	if (thePrefs.IsExtControlsEnabled()) {
		CString sResumePaused(GetResString(_T("DOWNLOAD")));
		sResumePaused.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("PAUSED")));
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_RESUMEPAUSED, sResumePaused, _T("RESUME"));
	}

	if (thePrefs.GetDownloadValidator() > 0) {
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_BYPASSDOWNLOADVALIDATOR, GetResString(_T("DOWNLOAD_BYPASS_DOWNLOAD_VALIDATOR")), _T("RESUME"));
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_BYPASSDOWNLOADVALIDATORPAUSED, GetResString(_T("DOWNLOAD_BYPASS_DOWNLOAD_VALIDATOR_PAUSED")), _T("RESUME"));
	}

	if (thePrefs.IsExtControlsEnabled())
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("DL_INFO")), _T("FILEINFO"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CMT, GetResStringWithAccelAndEllipsis(_T("COMMENT"), _T('e')), _T("FILECOMMENTS"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CANCEL, GetResString(_T("CANCEL_DOWNLOAD")), _T("DELETE"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CANCEL_FORGET, GetResString(_T("CANCEL_FORGET_DOWNLOAD")), _T("DELETE_FORGET"));
	m_PreviewMenu.CreateMenu();
	RebuildPreviewMenu(m_PreviewMenu, NULL, false, false, false, false, false, NULL);
	m_SearchFileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PreviewMenu.m_hMenu, GetResString(_T("PREVIEWWITH")), _T("PREVIEW"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_GETHTMLED2KLINK, GetResString(_T("DL_LINK2")), _T("ED2KLINK"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVESELECTED, GetResString(_T("REMOVE")), _T("DELETESELECTED"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("REMOVESEARCHSTRING")), _T("DELETE"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVEALL, GetResString(_T("REMOVEALLSEARCH")), _T("CLEARCOMPLETE"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_FIND, GetResString(_T("FIND")), _T("SEARCH"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_SEARCHRELATED, GetResString(_T("SEARCHRELATED")), _T("KADFILESEARCH"));
}

bool CSearchListCtrl::ShouldShowPersistentInfoTip(const SPersistentInfoTipContext& context)
{
	if (!CMuleListCtrl::ShouldShowPersistentInfoTip(context))
		return false;

	bool bShowInfoTip = (GetSelectedCount() > 1 || GetKeyState(VK_CONTROL) < 0);
	if (bShowInfoTip && GetSelectedCount() > 1) {
		bool bInfoTipItemIsPartOfMultiSelection = false;
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			if (GetNextSelectedItem(pos) == context.iItem) {
				bInfoTipItemIsPartOfMultiSelection = true;
				break;
			}
		}
		if (!bInfoTipItemIsPartOfMultiSelection)
			bShowInfoTip = false;
	}

	return bShowInfoTip;
}

bool CSearchListCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	if (GetSelectedCount() <= 1)
		return BuildSearchInfoTipText(context.iItem, strText);

	int iSelected = 0;
	uint64 uTotalSize = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(GetNextSelectedItem(pos));
		if (pSearchFile != NULL) {
			++iSelected;
			uTotalSize += (uint64)pSearchFile->GetFileSize();
		}
	}

	if (iSelected <= 0)
		return false;

	strText.Format(_T("%s: %i\r\n%s: %s%c"), (LPCTSTR)GetResString(_T("FILES")), iSelected, (LPCTSTR)GetResString(_T("DL_SIZE")), (LPCTSTR)FormatFileSize(static_cast<ULONGLONG>(uTotalSize)), TOOLTIP_AUTOFORMAT_SUFFIX_CH);
	return true;
}


void CSearchListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CMuleListCtrl::OnLvnGetInfoTip(pNMHDR, pResult);
}


// virtual-list compliant expand / collapse
void CSearchListCtrl::ExpandCollapseItem(int iItem, int iAction)
{
	CSearchFile* pSel = ResolveSearchFileByRowIndex(iItem);
	if (pSel == NULL)
		return;
	CSearchFile* pParent = pSel->GetListParent() != NULL ? pSel->GetListParent() : pSel;
	if (pParent == NULL)
		return;

	if (!pParent->IsListExpanded()) {
		if (iAction == COLLAPSE_ONLY || !CanExpandSearchParent(pParent))
			return;
		pParent->SetListExpanded(true);
	} else {
		if (iAction == EXPAND_ONLY)
			return;
		int iParentIndex = iItem;
		if (m_SearchItemsMap.Lookup(pParent, iParentIndex)) {
			SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			SetItemState(iParentIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			SetSelectionMark(iParentIndex);
		}
		pParent->SetListExpanded(false);
	}
	ReloadList(false, kSearchListViewState);
}

void CSearchListCtrl::HideSources(CSearchFile* pParent)
{
	if (pParent == NULL || pParent->GetSearchID() != m_nResultsID || !pParent->IsListExpanded())
		return;
	pParent->SetListExpanded(false);
	ReloadList(false, kSearchListViewState);
}

void CSearchListCtrl::OnNmClick(LPNMHDR pNMHDR, LRESULT*)
{
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (pt.x < TREE_WIDTH) {
		LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
		if (!IsPassiveRowIndex(pNMIA->iItem))
			ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
	}
}

void CSearchListCtrl::OnNmDblClk(LPNMHDR pNMHDR, LRESULT*)
{
	const LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	if (pNMIA == NULL || IsPassiveRowIndex(pNMIA->iItem))
		return;
	POINT point;
	::GetCursorPos(&point);
	ScreenToClient(&point);
	if (point.x > TREE_WIDTH) {
		if (GetKeyState(VK_MENU) & 0x8000) {
			int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
			if (iSel >= 0) {
				CSearchFile* file = ResolveSearchFileByRowIndex(iSel);
				if (file) {
					CTypedPtrList<CPtrList, CSearchFile*> aFiles;
					aFiles.AddTail(file);
					CSearchResultFileDetailSheet sheet(aFiles, 0, this);
					sheet.DoModal();
				}
			}
		} else
			theApp.emuledlg->searchwnd->DownloadSelected();
	}
}

void CSearchListCtrl::DrawPossibleKnownRow(CDC& dc, LPDRAWITEMSTRUCT lpDrawItemStruct, const SSearchListRow* pRow, BOOL bCtrlFocused)
{
	if (pRow == NULL)
		return;
	const bool bSelected = (lpDrawItemStruct->itemState & ODS_SELECTED) != 0;
	dc.SetTextColor(bSelected ? m_crHighlightText : (pRow->eType == SearchListRowPossibleKnownHeader ? m_crPossibleKnownHeader : GetPossibleKnownItemColor(pRow)));

	CRect rcItem(lpDrawItemStruct->rcItem);
	RECT rcClient;
	GetClientRect(&rcClient);
	const CHeaderCtrl* pHeaderCtrl = GetHeaderCtrl();
	const int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		const int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;
		UINT uDrawTextAlignment;
		const int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		CRect rcColumn(itemLeft + sm_iLabelOffset, rcItem.top, itemLeft + iColumnWidth - sm_iLabelOffset, rcItem.bottom);
		if (iColumn == colSearchFileName) {
			const int iTreeIndent = pRow->eType == SearchListRowPossibleKnownHeader ? TREE_WIDTH + 10 : TREE_WIDTH * 2 + 10;
			rcColumn.left += iTreeIndent;
			if (pRow->eType == SearchListRowPossibleKnownFile) {
				const int iImage = theApp.GetFileTypeSystemImageIdx(pRow->strName);
				if (iImage >= 0 && theApp.GetSystemImageList() != NULL) {
					const int iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
					::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc, rcColumn.left, rcItem.top + iIconY, ILD_TRANSPARENT);
					rcColumn.left += theApp.GetSmallSytemIconSize().cx + 4;
				}
			}
		}
		if (rcColumn.left < rcColumn.right && HaveIntersection(rcClient, rcColumn)) {
			const CString strText(GetPossibleKnownDisplayText(pRow, iColumn));
			dc.DrawText(strText, -1, &rcColumn, MLC_DT_TEXT | uDrawTextAlignment);
		}
		itemLeft += iColumnWidth;
	}

	const int iMiddle = (rcItem.top + rcItem.bottom + 1) / 2;
	const int iRootX = lpDrawItemStruct->rcItem.left + 4;
	const int iChildX = iRootX + TREE_WIDTH;
	const int iGrandchildX = iChildX + TREE_WIDTH;
	const SSearchListRow* pNextRow = ResolveRowByIndex(static_cast<int>(lpDrawItemStruct->itemID + 1));
	const bool bNextSameParent = IsRowDescendantOfParent(pNextRow, pRow->pParentSearchFile);
	const bool bNextKnownCandidate = bNextSameParent && pNextRow != NULL && pNextRow->eType == SearchListRowPossibleKnownFile;
	const COLORREF crLine = bSelected ? m_crHighlightText : RGB(128, 128, 128);
	CPen pen(PS_SOLID, 1, crLine);
	CPen* pOldPen = dc.SelectObject(&pen);
	dc.MoveTo(iRootX, rcItem.top - 1);
	dc.LineTo(iRootX, bNextSameParent ? rcItem.bottom + 1 : iMiddle);
	if (pRow->eType == SearchListRowPossibleKnownHeader) {
		dc.MoveTo(iRootX, iMiddle);
		dc.LineTo(iChildX, iMiddle);
		if (bNextKnownCandidate) {
			dc.MoveTo(iChildX, iMiddle);
			dc.LineTo(iChildX, rcItem.bottom + 1);
		}
	} else {
		dc.MoveTo(iChildX, rcItem.top - 1);
		dc.LineTo(iChildX, bNextKnownCandidate ? rcItem.bottom + 1 : iMiddle);
		dc.MoveTo(iChildX, iMiddle);
		dc.LineTo(iGrandchildX, iMiddle);
	}
	dc.SelectObject(pOldPen);
	DrawFocusRect(&dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, bSelected);
}

void CSearchListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	int index = static_cast<int>(lpDrawItemStruct->itemID);
	SSearchListRow* pRow = ResolveRowByIndex(index);
	if (pRow == NULL) {
		FillSearchFallbackOwnerDataRow(*this, lpDrawItemStruct);
		return;
	}

	CRect rcItem(lpDrawItemStruct->rcItem);
	CRect rcClientFullRow;
	GetClientRect(&rcClientFullRow);
	CRect rcPaint(rcClientFullRow.left, rcItem.top, rcClientFullRow.right, rcItem.bottom);
	CDC* pBaseDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CMemoryDC dc(pBaseDC, rcPaint);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	COLORREF clrBk = (lpDrawItemStruct->itemState & ODS_SELECTED) ? GetCustomSysColor(COLOR_HIGHLIGHT) : GetCustomSysColor(COLOR_WINDOW);
	dc.FillSolidRect(rcPaint, clrBk);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(clrBk);
	if (pRow->eType != SearchListRowSearchFile) {
		DrawPossibleKnownRow(dc, lpDrawItemStruct, pRow, bCtrlFocused);
		return;
	}
	CSearchFile* content = pRow->pSearchFile;
	if (content == NULL)
		return;

	RECT rcClient;
	GetClientRect(&rcClient);
	if (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0)
		dc.SetTextColor(GetSearchItemColor(content));

	bool isChild = (content->GetListParent() != NULL);
	bool notLast = (lpDrawItemStruct->itemID + 1 != (UINT)m_ListedItemsVector.size());
	bool notFirst = (lpDrawItemStruct->itemID != 0);
	int tree_start = 0;
	int tree_end = 0;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	rcItem.right = rcItem.left - sm_iLabelOffset;
	rcItem.left += sm_iIconOffset;

	LONG iIndent = isChild ? 8 : 0;
	LONG iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
	const POINT point = { itemLeft + iIndent + TREE_WIDTH + 18, rcItem.top + iIconY };
	int iImage;
	if (content->GetKnownType() == CSearchFile::Shared)
		iImage = 14;
	else if (content->GetKnownType() == CSearchFile::Downloaded)
		iImage = 13;
	else if (content->GetKnownType() == CSearchFile::Downloading)
		iImage = 12;
	else if (content->GetKnownType() == CSearchFile::Cancelled)
		iImage = 15;
	else if (thePrefs.GetBlacklistManual() && content->GetManualBlacklisted())
		iImage = 11;
	else if (thePrefs.GetBlacklistAutomatic() && content->GetAutomaticBlacklisted())
		iImage = 10;
	else if (thePrefs.IsSearchSpamFilterEnabled() && content->IsConsideredSpam())
		iImage = 8;
	else if (thePrefs.ShowRatingIndicator() && (content->HasComment() || content->HasRating() || content->IsKadCommentSearchRunning()))
		iImage = content->UserRating(true) + 1;
	else
		iImage = 0;
	if (iImage)
		SafeImageListDraw(&m_ImageList, dc, iImage, point, ILD_NORMAL);

	iImage = theApp.GetFileTypeSystemImageIdx(content->GetFileName());
	if (iImage >= 0 && theApp.GetSystemImageList() != NULL)
		::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc, point.x - 18, point.y, ILD_TRANSPARENT);

	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iLabelOffset;
		switch (iColumn) {
		case colSearchFileName:
			tree_start = rcItem.left + 1;
			rcItem.left += min(8, iColumnWidth);
			tree_end = rcItem.left;
		default:
			rcItem.left += sm_iLabelOffset;
			if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem))
				if (isChild)
					DrawSourceChild(dc, iColumn, &rcItem, uDrawTextAlignment, content);
				else
					DrawSourceParent(dc, iColumn, &rcItem, uDrawTextAlignment, content);
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(&dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, (lpDrawItemStruct->itemState & ODS_SELECTED) != 0);

	if (tree_start < tree_end) {
		RECT tree_rect = { tree_start, lpDrawItemStruct->rcItem.top, tree_end, lpDrawItemStruct->rcItem.bottom };
		dc.SetBoundsRect(&tree_rect, DCB_DISABLE);

		const SSearchListRow* pNextRow = notLast ? ResolveRowByIndex(static_cast<int>(lpDrawItemStruct->itemID + 1)) : NULL;
		const CSearchFile* pTreeParent = isChild ? content->GetListParent() : content;
		bool hasNext = IsRowDescendantOfParent(pNextRow, pTreeParent);
		bool isOpenRoot = hasNext && !isChild;

		int treeCenter = tree_start + 4;
		int middle = (rcItem.top + rcItem.bottom + 1) / 2;

		COLORREF crLine = (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) ? RGB(128, 128, 128) : m_crHighlightText;
		CPen pn;
		pn.CreatePen(PS_SOLID, 1, crLine);
		CPen *oldpn = dc.SelectObject(&pn);

		if (isChild) {
			dc.MoveTo(tree_end + 10, middle);
			dc.LineTo(tree_start + 4, middle);
			if (hasNext) {
				dc.MoveTo(treeCenter, middle);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		} else if (isOpenRoot || content->GetListChildCount() > 1 || HasCachedPossibleKnownMatches(content)) {
			const RECT circle_rec = { treeCenter - 4, middle - 5, treeCenter + 5, middle + 4 };
			CBrush brush(crLine);
			dc.FrameRect(&circle_rec, &brush);
			CPen penBlack;
			penBlack.CreatePen(PS_SOLID, 1, (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) ? m_crWindowText : m_crHighlightText);
			CPen *pOldPen2 = dc.SelectObject(&penBlack);
			dc.MoveTo(treeCenter - 2, middle - 1);
			dc.LineTo(treeCenter + 3, middle - 1);
			if (!content->IsListExpanded()) {
				dc.MoveTo(treeCenter, middle - 3);
				dc.LineTo(treeCenter, middle + 2);
			}
			dc.SelectObject(pOldPen2);
			if (hasNext) {
				dc.MoveTo(treeCenter, middle + 4);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		}

		if (notFirst && isChild) {
			dc.MoveTo(treeCenter, middle);
			dc.LineTo(treeCenter, rcItem.top - 1);
		}

		dc.SelectObject(oldpn);
		pn.DeleteObject();
	}
}


COLORREF CSearchListCtrl::GetSearchItemColor(const CSearchFile* src) const
{
	if (theApp.searchlist == NULL)
		return { RGB(0, 0, 0) };
	CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
	switch (src->GetKnownType()) {
	case CSearchFile::Shared:
		return { m_crSearchResultSharing};
	case CSearchFile::Downloading:
		{
			const CKnownFile* pFile = theApp.downloadqueue->GetFileByID(src->GetFileHash());
			if (pFile && pFile->IsPartFile() && static_cast<const CPartFile*>(pFile)->GetStatus() == PS_PAUSED)
				return { m_crSearchResultDownloadStopped};
			return { m_crSearchResultDownloading };
		}
	case CSearchFile::Downloaded:
		return { m_crSearchResultKnown };
	case CSearchFile::Cancelled:
		return { m_crSearchResultCancelled };
	}

	// Spam check
	if (thePrefs.GetBlacklistManual() && src->GetManualBlacklisted())
		return { GetCustomSysColor(COLOR_MAN_BLACKLIST) }; // Purple
	else if (thePrefs.GetBlacklistAutomatic() && src->GetAutomaticBlacklisted())
		return { GetCustomSysColor(COLOR_AUTO_BLACKLIST) }; // Pink
	else if (thePrefs.IsSearchSpamFilterEnabled() && src->IsConsideredSpam(false))
		return { GetCustomSysColor(COLOR_SPAM) }; // Red

	// unknown file -> show shades of a color
	uint32 srccnt = src->GetSourceCount();
	srccnt -= static_cast<uint32>(srccnt > 0);
	return { m_crShades[min(srccnt, AVBLYSHADECOUNT - 1)] };
}

COLORREF CSearchListCtrl::GetPossibleKnownItemColor(const SSearchListRow* pRow) const
{
	if (pRow == NULL)
		return m_crWindowText;

	uint8 uSourceFlags = pRow->uSourceFlags;
	if (uSourceFlags == CDownloadValidator::FuzzyFileSourceUnknown && !isnulmd4(pRow->ucHash)) {
		if (theApp.downloadqueue != NULL && theApp.downloadqueue->GetFileByID(pRow->ucHash) != NULL)
			uSourceFlags = static_cast<uint8>(uSourceFlags | CDownloadValidator::FuzzyFileSourceDownloading);
		if (theApp.knownfiles != NULL && theApp.knownfiles->FindKnownFileByID(pRow->ucHash) != NULL)
			uSourceFlags = static_cast<uint8>(uSourceFlags | CDownloadValidator::FuzzyFileSourceKnown);
	}

	if ((uSourceFlags & CDownloadValidator::FuzzyFileSourceDownloading) != 0) {
		const CKnownFile* pFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pRow->ucHash) : NULL;
		if (pFile != NULL && pFile->IsPartFile() && static_cast<const CPartFile*>(pFile)->GetStatus() == PS_PAUSED)
			return m_crSearchResultDownloadStopped;
		return m_crSearchResultDownloading;
	}
	if ((uSourceFlags & CDownloadValidator::FuzzyFileSourceKnown) != 0)
		return m_crSearchResultKnown;
	return m_crWindowText;
}


void CSearchListCtrl::DrawSourceChild(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src)
{
	const CString sItem(GetItemDisplayText(src, nColumn));
	switch (nColumn) {
	case colSearchFileName:
		lpRect->left += 8 + 8 + theApp.GetSmallSytemIconSize().cy;
		if ((thePrefs.ShowRatingIndicator() && (src->HasComment() || src->HasRating() || src->IsKadCommentSearchRunning()))
			|| ((thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistAutomatic() || thePrefs.GetBlacklistManual()) && src->IsConsideredSpam())
			|| (src->GetKnownType() == CSearchFile::Shared || src->GetKnownType() == CSearchFile::Downloaded || src->GetKnownType() == CSearchFile::Downloading || src->GetKnownType() == CSearchFile::Cancelled))
			lpRect->left += 16;
	default:
		dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
	case colSearchType:
	case colSearchFileId:
		break;
	}
}

void CSearchListCtrl::DrawSourceParent(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src)
{
	const CString sItem(GetItemDisplayText(src, nColumn));
	switch (nColumn) {
	case colSearchFileName:
		lpRect->left += 8 + theApp.GetSmallSytemIconSize().cx;
		if ((thePrefs.ShowRatingIndicator() && (src->HasComment() || src->HasRating() || src->IsKadCommentSearchRunning()))
			|| ((thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistAutomatic() || thePrefs.GetBlacklistManual()) && src->IsConsideredSpam())
			|| (src->GetKnownType() == CSearchFile::Shared || src->GetKnownType() == CSearchFile::Downloaded || src->GetKnownType() == CSearchFile::Downloading || src->GetKnownType() == CSearchFile::Cancelled))
			lpRect->left += 16;
	default:
		dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
		break;
	case colSearchCompleteSources:
		{
			bool bComplete = IsComplete(src, src->GetSourceCount());
			COLORREF crOldTextColor = (bComplete ? 0 : dc->SetTextColor(RGB(255, 0, 0)));
			dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
			if (!bComplete)
				dc->SetTextColor(crOldTextColor);
		}
	}
}


void CSearchListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_F5) {
		theApp.searchlist->RecalculateSpamRatings(m_nResultsID, false, false, true);
		return;
	}


	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSearchListCtrl::SetHighlightColors()
{
	// Default colors
	// --------------
	//	Blue:	User does not know that file; shades of blue are used to indicate availability of file
	//  Red:	User already has the file; it is currently downloading or it is currently shared
	//			-> 'Red' means: User can not add this file
	//	Green:	User 'knows' the file (it was already download once, but is currently not in share)
	COLORREF crSearchResultAvblyBase = GetCustomSysColor(COLOR_SHADEBASE);
	m_crSearchResultDownloading = GetCustomSysColor(COLOR_SEARCH_DOWNLOADING); // Olive green
	m_crSearchResultDownloadStopped = GetCustomSysColor(COLOR_SEARCH_STOPPED); // Olive green
	m_crSearchResultSharing = GetCustomSysColor(COLOR_SEARCH_SHARING); // Dark green
	m_crSearchResultKnown = GetCustomSysColor(COLOR_SEARCH_KNOWN); // Medium green
	m_crSearchResultCancelled = GetCustomSysColor(COLOR_SEARCH_CANCELED); // Orange
	m_crPossibleKnownHeader = GetCustomSysColor(COLOR_SEARCH_POSSIBLE_KNOWN_HEADER);

	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Downloading"), m_crSearchResultDownloading);
	if (!theApp.LoadSkinColor(_T("Fg_DownloadStopped"), m_crSearchResultDownloadStopped))
		m_crSearchResultDownloadStopped = m_crSearchResultDownloading;
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Sharing"), m_crSearchResultSharing);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Known"), m_crSearchResultKnown);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_AvblyBase"), crSearchResultAvblyBase);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_PossibleKnownHeader"), m_crPossibleKnownHeader);

	// precalculate sources shades
	COLORREF normFGC = GetTextColor();
	// precalculate sources shades
	COLORREF darkFGC = GetCustomSysColor(COLOR_WINDOWTEXT); // Dark mode foreground color


	float rdelta = (GetRValue(crSearchResultAvblyBase) - GetRValue(normFGC)) / (float)AVBLYSHADECOUNT;
	float gdelta = (GetGValue(crSearchResultAvblyBase) - GetGValue(normFGC)) / (float)AVBLYSHADECOUNT;
	float bdelta = (GetBValue(crSearchResultAvblyBase) - GetBValue(normFGC)) / (float)AVBLYSHADECOUNT;

	for (int shades = 0; shades < AVBLYSHADECOUNT; ++shades)
	{
		if (IsDarkModeEnabled()) {
			// Adjust shades for dark mode
			m_crShades[shades] = RGB(GetRValue(darkFGC) + (rdelta * shades),
				GetGValue(darkFGC) + (gdelta * shades),
				GetBValue(darkFGC) + (bdelta * shades));
		} else {
			m_crShades[shades] = RGB(GetRValue(normFGC) + (rdelta * shades),
				GetGValue(normFGC) + (gdelta * shades),
				GetBValue(normFGC) + (bdelta * shades));
		}
	}
}

void CSearchListCtrl::RefreshThemeColors()
{
	CMuleListCtrl::RefreshThemeColors();
	SetHighlightColors();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void CSearchListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
}

void CSearchListCtrl::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CMuleListCtrl::OnShowWindow(bShow, nStatus);
	if (bShow && ::IsWindow(m_hWnd)) {
		RefreshThemeColors();
		if (IsPossibleKnownFeatureEnabled() && theApp.DownloadValidator->GetPossibleKnownRevision() != m_uPossibleKnownRevision)
			QueuePossibleKnownRefresh(1);
		else if (IsPossibleKnownFeatureEnabled() && theApp.DownloadValidator->GetCandidateDataRevision() != m_uPossibleKnownCandidateDataRevision)
			QueuePossibleKnownSoftRefresh();
	}
}

BOOL CSearchListCtrl::OnEraseBkgnd(CDC* pDC)
{
	return CMuleListCtrl::OnEraseBkgnd(pDC);
}

void CSearchListCtrl::OnLvnKeyDown(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);

	bool bAltKey = GetKeyState(VK_MENU) < 0;
	int iAction;
	if (pLVKeyDown->wVKey == VK_ADD || (bAltKey && pLVKeyDown->wVKey == VK_RIGHT))
		iAction = EXPAND_ONLY;
	else if (pLVKeyDown->wVKey == VK_SUBTRACT || (bAltKey && pLVKeyDown->wVKey == VK_LEFT))
		iAction = COLLAPSE_ONLY;
	else
		iAction = EXPAND_COLLAPSE;
	if (iAction < EXPAND_COLLAPSE)
		ExpandCollapseItem(GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED), iAction);
	*pResult = 0;
}

CString CSearchListCtrl::GetItemDisplayText(const CSearchFile *src, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case colSearchFileName:
		sText = src->GetFileName();
		break;
	case colSearchSize:
		if (src->GetListParent() == NULL
			|| (thePrefs.GetDebugSearchResultDetailLevel() >= 1 && src->GetFileSize() != src->GetListParent()->GetFileSize()))
		{
			sText = FormatFileSize(src->GetFileSize());
		}
		break;
	case colSearchType:
		if (src->GetListParent() == NULL)
			sText = src->GetFileTypeDisplayStr();
		break;
	case colSearchLength:
		{
			uint32 nMediaLength = src->GetIntTagValue(FT_MEDIA_LENGTH);
			if (nMediaLength)
				sText = SecToTimeLength(nMediaLength);
		}
		break;
	case colSearchAvailability:
		if (src->GetListParent() == NULL) {
			sText.Format(_T("%u"), src->GetSourceCount());
			if (thePrefs.IsExtControlsEnabled()) {
				if (src->IsKademlia()) {
					uint32 nKnownPublisher = (src->GetKadPublishInfo() >> 16) & 0xffu;
					if (nKnownPublisher > 0)
						sText.AppendFormat(_T(" (%u)"), nKnownPublisher);
				} else {
					int iClients = src->GetClientsCount();
					if (iClients > 0)
						sText.AppendFormat(_T(" (%i)"), iClients);
				}
			}
#ifdef _DEBUG
			if (src->GetKadPublishInfo() == 0)
				sText += _T(" | -");
			else
				sText.AppendFormat(_T("%sNames:%u, Pubs:%u, Trust:%0.2f"), sText.IsEmpty() ? _T("") : _T(" | "), (src->GetKadPublishInfo() >> 24) & 0xffu, (src->GetKadPublishInfo() >> 16) & 0xffu, (src->GetKadPublishInfo() & 0xffffu) / 100.0f);
#endif
		} else
			sText.Format(_T("%u"), src->GetListChildCount());
		break;
	case colSearchCompleteSources:
		if (src->GetListParent() == NULL
			|| (thePrefs.IsExtControlsEnabled() && thePrefs.GetDebugSearchResultDetailLevel() >= 1))
		{
			sText = GetCompleteSourcesDisplayString(src, src->GetSourceCount());
		}
		break;
	case colSearchSimilarity:
		break;
	case colSearchKnown:
		{
			sText = GetKnownTypeStr(src);
#ifdef _DEBUG
			sText.AppendFormat(_T("%sSR: %u%%"), sText.IsEmpty() ? _T("") : _T(" "), src->GetSpamRating());
#endif
		}
		break;
	case colSearchBitrate:
		{
			uint32 nBitrate = src->GetIntTagValue(FT_MEDIA_BITRATE);
			if (nBitrate)
				sText.Format(_T("%u %s"), nBitrate, (LPCTSTR)GetResString(_T("KBITSSEC")));
		}
		break;
	case colSearchCodec:
		sText = GetCodecDisplayName(src->GetStrTagValue(FT_MEDIA_CODEC));
		break;
	case colSearchFileId:
		if (src->GetListParent() == NULL)
			sText = md4str(src->GetFileHash());
		break;
	case colSearchFolder:
		if (src->GetDirectory())
			sText = src->GetDirectory();
		break;
	case colSearchAlbum:
		sText = src->GetStrTagValue(FT_MEDIA_ALBUM);
		break;
	case colSearchTitle:
		sText = src->GetStrTagValue(FT_MEDIA_TITLE);
		break;
	case colSearchArtist:
		sText = src->GetStrTagValue(FT_MEDIA_ARTIST);
		break;
	case colSearchAichHash:
		if (src->GetFileIdentifierC().HasAICHHash())
			sText = src->GetFileIdentifierC().GetAICHHash().GetString();
		break;
	case colSearchSpamRating:
		sText.Format(_T("%u"), src->GetSpamRating());
		break;
	}
	return sText;
}

CString CSearchListCtrl::GetPossibleKnownDisplayText(const SSearchListRow* pRow, int iSubItem) const
{
	if (pRow == NULL)
		return EMPTY;
	if (pRow->eType == SearchListRowPossibleKnownHeader)
		return iSubItem == colSearchFileName ? pRow->strName : EMPTY;

	CString strText;
	switch (iSubItem) {
	case colSearchFileName:
		strText = pRow->strName;
		break;
	case colSearchSize:
		if (static_cast<uint64>(pRow->uSize) != 0)
			strText = FormatFileSize(pRow->uSize);
		break;
	case colSearchType:
		if (pRow->uFileType != static_cast<uint8>(ED2KFT_ANY)) {
			LPCTSTR pszFileType = GetED2KFileTypeSearchTerm(static_cast<EED2KFileType>(pRow->uFileType), false);
			if (pszFileType != NULL)
				strText = GetFileTypeDisplayStrFromED2KFileType(pszFileType);
		}
		break;
	case colSearchLength:
		if (pRow->uMediaLengthSec != 0)
			strText = SecToTimeLength(pRow->uMediaLengthSec);
		break;
	case colSearchSimilarity:
		strText.Format(_T("%u%%"), pRow->uSimilarityScore);
		break;
	case colSearchKnown:
		{
			uint8 uSourceFlags = pRow->uSourceFlags;
			if (uSourceFlags == CDownloadValidator::FuzzyFileSourceUnknown && !isnulmd4(pRow->ucHash)) {
				if (theApp.downloadqueue != NULL && theApp.downloadqueue->GetFileByID(pRow->ucHash) != NULL)
					uSourceFlags = static_cast<uint8>(uSourceFlags | CDownloadValidator::FuzzyFileSourceDownloading);
				if (theApp.knownfiles != NULL && theApp.knownfiles->FindKnownFileByID(pRow->ucHash) != NULL)
					uSourceFlags = static_cast<uint8>(uSourceFlags | CDownloadValidator::FuzzyFileSourceKnown);
			}
			if ((uSourceFlags & CDownloadValidator::FuzzyFileSourceDownloading) != 0)
				strText = GetResString(_T("DOWNLOADING"));
			else if ((uSourceFlags & CDownloadValidator::FuzzyFileSourceKnown) != 0)
				strText = GetResString(_T("DOWNLOADED"));
		}
		break;
	case colSearchBitrate:
		if (pRow->uMediaBitrateKbps != 0)
			strText.Format(_T("%u %s"), pRow->uMediaBitrateKbps, (LPCTSTR)GetResString(_T("KBITSSEC")));
		break;
	case colSearchCodec:
		strText = GetCodecDisplayName(pRow->strMediaCodec);
		break;
	case colSearchFileId:
		if (!isnulmd4(pRow->ucHash))
			strText = md4str(pRow->ucHash);
		break;
	case colSearchFolder:
		strText = pRow->strFolder;
		break;
	case colSearchAlbum:
		strText = pRow->strMediaAlbum;
		break;
	case colSearchTitle:
		strText = pRow->strMediaTitle;
		break;
	case colSearchArtist:
		strText = pRow->strMediaArtist;
		break;
	case colSearchAichHash:
		strText = pRow->strAICHHash;
		break;
	}
	return strText;
}

void CSearchListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the
		// listview, to be capable of quick searching those items via the keyboard. Because our listview
		// items may change their contents, we do this via a text callback function. The listview control
		// will send us the LVN_DISPINFO notification if it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item.
		// At least this notification is only sent for the visible items and not for all items in the list.
		// Though, because this function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!)-items.
		//

		// This isn't an owner drawn list anymore, instead this is implemented as a virtual list. So above description is now obsolete!
		LVITEMW& rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if (rItem.mask & LVIF_TEXT) {
			_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetListedItemDisplayText(rItem.iItem, rItem.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}

CString CSearchListCtrl::FormatFileSize(ULONGLONG ullFileSize) const
{
	if (m_eFileSizeFormat == fsizeKByte)
		// Always round up to next KiB (this is same as Windows Explorer is doing)
		return GetFormatedUInt64((ullFileSize + 1024 - 1) / 1024) + _T(' ') + GetResString(_T("KBYTES"));

	if (m_eFileSizeFormat == fsizeMByte) {
		double fFileSize = ullFileSize / (1024.0 * 1024.0);
		if (fFileSize < 0.01)
			fFileSize = 0.01;

		static NUMBERFMT nf;
		static TCHAR szDecimalSep[] = _T(".");
		static TCHAR szThousandSep[] = _T(",");
		if (nf.Grouping == 0) {
			nf.NumDigits = 2;
			nf.LeadingZero = 1;
			nf.Grouping = 3;
			// we are hardcoding the following two format chars by intention because the C-RTL also has the decimal sep hardcoded to '.'
			nf.lpDecimalSep = szDecimalSep;
			nf.lpThousandSep = szThousandSep;
			nf.NegativeOrder = 0;
		}
		CString sVal, strVal;
		sVal.Format(_T("%.2f"), fFileSize);
		int iResult = GetNumberFormat(LOCALE_SYSTEM_DEFAULT, 0, sVal, &nf, strVal.GetBuffer(80), 80);
		strVal.ReleaseBuffer();
		return (iResult ? strVal : sVal) + _T(' ') + GetResString(_T("MBYTES"));
	}

	return CastItoXBytes(ullFileSize);
}

void CSearchListCtrl::SetFileSizeFormat(EFileSizeFormat eFormat)
{
	m_eFileSizeFormat = eFormat;
	Invalidate(FALSE);
}

const bool CSearchListCtrl::IsFilteredOut(const CSearchFile *pSearchFile) const
{

	if (!pSearchFile)
		return true;

	if ((thePrefs.m_uCompleteCheckState == BST_CHECKED && !pSearchFile->GetCompleteSourceCount()) ||
		(thePrefs.m_uCompleteCheckState == BST_INDETERMINATE && pSearchFile->GetCompleteSourceCount()))
		return true;

	if ((thePrefs.m_uSearchKnownCheckState == BST_CHECKED && !IsSearchFileKnown(pSearchFile)) ||
		(thePrefs.m_uSearchKnownCheckState == BST_INDETERMINATE && !IsSearchKnownColumnEmpty(pSearchFile)))
		return true;

	const CStringArray &rastrFilter = theApp.emuledlg->searchwnd->m_pwndResults->m_astrFilter;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString &szFilterTarget(GetItemDisplayText(pSearchFile, theApp.emuledlg->searchwnd->m_pwndResults->GetFilterColumn()));

		for (INT_PTR i = rastrFilter.GetCount(); --i >= 0;) {
			LPCTSTR pszText = (LPCTSTR)rastrFilter[i];
			bool bAnd = (*pszText != _T('-'));
			if (!bAnd)
				++pszText;

			bool bFound = (stristr(szFilterTarget, pszText) != NULL);
			if (bAnd != bFound)
				return true;
		}
	}
	return false;
}

CString CSearchListCtrl::GetKnownTypeStr(const CSearchFile* src)
{
	LPCTSTR uid;
	switch (src->m_eKnown) {
	case CSearchFile::Shared:
		uid = _T("SHARED");
		break;
	case CSearchFile::Downloading:
		uid = _T("DOWNLOADING");
		break;
	case CSearchFile::Downloaded:
		uid = _T("DOWNLOADED");
		break;
	case CSearchFile::Cancelled:
		uid = _T("CANCELLED");
		break;
	default:
		if (thePrefs.GetBlacklistManual() && src->GetManualBlacklisted())
			uid = _T("MANUAL_BLACKLISTED");
		else if (thePrefs.GetBlacklistAutomatic() && src->GetAutomaticBlacklisted())
			uid = _T("AUTOMATIC_BLACKLISTED");
		else if (thePrefs.IsSearchSpamFilterEnabled() && src->IsConsideredSpam(false))
			uid = _T("SPAM");
		else
			uid = EMPTY;
	}
	if (uid)
		return GetResString(uid);
	else
		return NULL;
}
