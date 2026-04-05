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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "ArchivePreviewDlg.h"
#include "KnownFile.h"
#include "partfile.h"
#include "preferences.h"
#include "UserMsgs.h"
#include "SplitterControl.h"
#include "MenuCmds.h"
#include "SharedFilesWnd.h"
#include "eMuleAI/MenuXP.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	void AdjustSharedFilesDetailsHost(CWnd *pPage)
	{
		if (pPage == NULL)
			return;

		CWnd *pSheet = pPage->GetParent();
		if (pSheet == NULL)
			return;

		CSharedFilesWnd *pSharedFilesWnd = DYNAMIC_DOWNCAST(CSharedFilesWnd, pSheet->GetParent());
		if (pSharedFilesWnd != NULL)
			pSharedFilesWnd->RequestDetailsPanelHeightAdjustment();
	}
}


// from free unRAR, Alexander L. Roshal
class EncodeFileName
{
private:
	//byte *EncName;
	int FlagBits;
	int FlagsPos;
	int DestSize;
	byte Flags;
public:
	EncodeFileName();
	void Decode(const char* const Name, byte *EncName, int EncSize, wchar_t *NameW, int MaxDecSize);
};

EncodeFileName::EncodeFileName()
	: FlagBits()
	, FlagsPos()
	, DestSize()
	, Flags()
{
}

void EncodeFileName::Decode(const char* const Name, byte *EncName, int EncSize, wchar_t *NameW, int MaxDecSize)
{
	int EncPos = 0, DecPos = 0;
	byte HighByte = EncName[EncPos++];
	while (EncPos < EncSize && DecPos < MaxDecSize) {
		if (FlagBits == 0) {
			Flags = EncName[EncPos++];
			FlagBits = 8;
		}
		switch (Flags >> 6) {
		case 0:
			NameW[DecPos++] = EncName[EncPos++];
			break;
		case 1:
			NameW[DecPos++] = EncName[EncPos++] + (HighByte << 8);
			break;
		case 2:
			NameW[DecPos++] = EncName[EncPos] + (EncName[EncPos + 1] << 8);
			EncPos += 2;
			break;
		case 3:
			{
				int Length = EncName[EncPos++];
				if (Length & 0x80) {
					byte Correction = EncName[EncPos++];
					for (Length = (Length & 0x7f) + 2; Length > 0 && DecPos < MaxDecSize; --Length, ++DecPos)
						NameW[DecPos] = ((Name[DecPos] + Correction) & 0xff) + (HighByte << 8);
				} else
					for (Length += 2; Length > 0 && DecPos < MaxDecSize; --Length, ++DecPos)
						NameW[DecPos] = Name[DecPos];
			}
		}
		Flags <<= 2;
		FlagBits -= 2;
	}
	NameW[DecPos < MaxDecSize ? DecPos : MaxDecSize - 1] = 0;
}
/////////////////////////



//////////////////////////////////////////////////////////////////////////////
// COLUMN_INIT -- List View Columns

enum EArchiveCols
{
	ARCHPREV_COL_NAME = 0,
	ARCHPREV_COL_SIZE,
	ARCHPREV_COL_CRC,
	ARCHPREV_COL_ATTR,
	ARCHPREV_COL_TIME,
	ARCHPREV_COL_CMT
};

// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
static LCX_COLUMN_INIT s_aColumns[] =
{
	{ ARCHPREV_COL_NAME, _T("Name"),  _T("DL_FILENAME"),  LVCFMT_LEFT,  -1, 0, ASCENDING, NONE, _T("LONG FILENAME.DAT") },
	{ ARCHPREV_COL_SIZE, _T("Size"),  _T("DL_SIZE"),      LVCFMT_RIGHT, -1, 1, ASCENDING, NONE, _T("9999 MByte") },
	{ ARCHPREV_COL_CRC,  _T("CRC"),	   EMPTY,				  LVCFMT_LEFT,  -1, 2, ASCENDING, NONE, _T("1234abcd")  },
	{ ARCHPREV_COL_ATTR, _T("Attr"),  _T("ATTRIBUTES"),   LVCFMT_LEFT,  -1, 3, ASCENDING, NONE, _T("mmm") },
	{ ARCHPREV_COL_TIME, _T("Time"),  _T("LASTMODIFIED"), LVCFMT_LEFT,  -1, 4, ASCENDING, NONE, _T("12.12.1990 00:00:00") },
	{ ARCHPREV_COL_CMT,  _T("Cmt"),   _T("COMMENT"),      LVCFMT_LEFT,  -1, 5, ASCENDING, NONE, _T("Long long long long comment") }
};

#define	PREF_INI_SECTION	_T("ArchivePreviewDlg")

IMPLEMENT_DYNAMIC(CArchivePreviewDlg, CResizablePage)

BEGIN_MESSAGE_MAP(CArchivePreviewDlg, CResizablePage)
	ON_BN_CLICKED(IDC_READARCH, OnBnClickedRead)
	ON_BN_CLICKED(IDC_RESTOREARCH, OnBnClickedCreateRestored)
	ON_BN_CLICKED(IDC_AP_EXPLAIN, OnBnExplain)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_MESSAGE(UM_ARCHIVESCANDONE, ShowScanResults)
	ON_WM_DESTROY()
	ON_NOTIFY(LVN_DELETEALLITEMS, IDC_FILELIST, OnLvnDeleteAllItemsArchiveEntries)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_FILELIST, OnNMCustomDrawArchiveEntries)
	ON_WM_CONTEXTMENU()
	ON_COMMAND(MP_UPDATE, OnBnClickedRead)
	ON_COMMAND(MP_HM_HELP, OnBnExplain)
END_MESSAGE_MAP()

CArchivePreviewDlg::CArchivePreviewDlg()
	: CResizablePage(CArchivePreviewDlg::IDD)
	, m_pFile()
	, m_paFiles()
	, m_activeTParams()
	, m_StoredColWidth2()
	, m_StoredColWidth5()
	, m_bDataChanged()
	, m_bReducedDlg()
{
	m_strCaption = GetResString(_T("CONTENT_INFO"));
	m_psp.pszTitle = m_strCaption;
	m_psp.dwFlags |= PSP_USETITLE;

	m_ContentList.m_pParent = this;
	m_ContentList.SetRegistryKey(PREF_INI_SECTION);
	m_ContentList.SetRegistryPrefix(_T("ContentList_"));
}

void CArchivePreviewDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizablePage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FILELIST, m_ContentList);
	DDX_Control(pDX, IDC_ARCHPROGRESS, m_progressbar);
}

BOOL CArchivePreviewDlg::OnInitDialog()
{
	CResizablePage::OnInitDialog();
	InitWindowStyles(this);

	m_StoredColWidth2 = 0;
	m_StoredColWidth5 = 0;
	if (!m_bReducedDlg) {
		AddOrReplaceAnchor(this, IDC_READARCH, BOTTOM_LEFT);
		AddOrReplaceAnchor(this, IDC_RESTOREARCH, BOTTOM_LEFT);
		AddOrReplaceAnchor(this, IDC_APV_FILEINFO, TOP_LEFT, TOP_RIGHT);
		AddOrReplaceAnchor(this, IDC_ARCP_ATTRIBS, TOP_CENTER);
		AddOrReplaceAnchor(this, IDC_INFO_ATTR, TOP_CENTER, TOP_RIGHT);
		AddOrReplaceAnchor(this, IDC_AP_EXPLAIN, BOTTOM_LEFT);
		AddOrReplaceAnchor(this, IDC_INFO_STATUS, TOP_LEFT, TOP_RIGHT);
	} else {
		CRect rc;
		GetDlgItem(IDC_APV_FILEINFO)->GetWindowRect(rc);
		int nDelta1 = rc.Height();
		GetDlgItem(IDC_RESTOREARCH)->GetWindowRect(rc);
		int nDelta2 = rc.Height();
		CSplitterControl::ChangePos(GetDlgItem(IDC_FILELIST), 0, -nDelta1);
		CSplitterControl::ChangeHeight(GetDlgItem(IDC_FILELIST), nDelta1 + nDelta2);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ARCHPROGRESS), 0, nDelta2);
		CSplitterControl::ChangePos(GetDlgItem(IDC_INFO_FILECOUNT), 0, nDelta2);
		GetDlgItem(IDC_READARCH)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_RESTOREARCH)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_APV_FILEINFO)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_ARCP_ATTRIBS)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_INFO_ATTR)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_AP_EXPLAIN)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_INFO_STATUS)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_INFO_TYPE)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_ARCP_TYPE)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_ARCP_STATUS)->ShowWindow(SW_HIDE);
	}
	AddOrReplaceAnchor(this, IDC_INFO_FILECOUNT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_ARCHPROGRESS, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_FILELIST, TOP_LEFT, BOTTOM_RIGHT);

	AddAllOtherAnchors();

	// Win98: Explicitly set to Unicode to receive Unicode notifications.
	m_ContentList.SendMessage(CCM_SETUNICODEFORMAT, TRUE);
	// To support full sorting of the archive entries list we'd need a separate list which
	// is holding the unified archive entries for all different supported archive formats so
	// that the ListView's sortproc can get valid 'lParam' values which are pointing to those
	// entries. This could be done, but for now we just let the default ListView's
	// functionality sort the archive entries by filename (the content of the first column).
	ASSERT(m_ContentList.GetStyle() & LVS_SORTASCENDING);
	ASSERT(m_ContentList.GetStyle() & LVS_SHAREIMAGELISTS);
	m_ContentList.SendMessage(LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)theApp.GetSystemImageList());
	m_ContentList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_GRIDLINES);
	m_ContentList.EnableHdrCtrlSortBitmaps();

	m_ContentList.ReadColumnStats(_countof(s_aColumns), s_aColumns);
	m_ContentList.CreateColumns(_countof(s_aColumns), s_aColumns);
	m_ContentList.InitColumnOrders(_countof(s_aColumns), s_aColumns);
	m_ContentList.UpdateSortColumn(_countof(s_aColumns), s_aColumns);

	CResizablePage::UpdateData(FALSE);
	Localize();

	m_progressbar.SetRange(0, 1000);
	m_progressbar.SetPos(0);

	return TRUE;
}

void CArchivePreviewDlg::OnDestroy()
{
	if (m_ContentList.GetColumnWidth(2) == 0)
		m_ContentList.SetColumnWidth(2, m_StoredColWidth2);
	if (m_ContentList.GetColumnWidth(5) == 0)
		m_ContentList.SetColumnWidth(5, m_StoredColWidth5);

	m_ContentList.WriteColumnStats(_countof(s_aColumns), s_aColumns);

	// stop running scanner-thread if present
	if (m_activeTParams != NULL) {
		m_activeTParams->m_bIsValid = false;
		m_activeTParams = NULL;
	}

	// This property sheet's window may get destroyed and re-created several times although
	// the corresponding C++ class is kept -> explicitly reset ResizableLib state
	RemoveAllAnchors();

	CResizablePage::OnDestroy();
}

BOOL CArchivePreviewDlg::OnSetActive()
{
	if (!CResizablePage::OnSetActive())
		return FALSE;
	AdjustSharedFilesDetailsHost(this);

	if (m_bDataChanged) {
		UpdateArchiveDisplay(thePrefs.m_bAutomaticArcPreviewStart);
		m_bDataChanged = false;
	}
	return TRUE;
}

LRESULT CArchivePreviewDlg::OnDataChanged(WPARAM, LPARAM)
{
	m_bDataChanged = true;
	return 1;
}

void CArchivePreviewDlg::Localize()
{
	if (!m_hWnd)
		return;
	SetTabTitle(_T("CONTENT_INFO"), this);

	if (!m_bReducedDlg) {
		SetDlgItemText(IDC_READARCH, GetResString(_T("SV_UPDATE")));
		SetDlgItemText(IDC_RESTOREARCH, GetResString(_T("AP_CREATEPREVCOPY")));
		SetDlgItemText(IDC_ARCP_TYPE, GetResString(_T("ARCHTYPE")) + _T(':'));
		SetDlgItemText(IDC_ARCP_STATUS, GetResString(_T("STATUS")) + _T(':'));
		SetDlgItemText(IDC_ARCP_ATTRIBS, GetResString(_T("INFO")) + _T(':'));
	}
}

void CArchivePreviewDlg::OnLvnDeleteAllItemsArchiveEntries(LPNMHDR, LRESULT *pResult)
{
	// To suppress subsequent LVN_DELETEITEM notification messages, return TRUE.
	*pResult = TRUE;
}

void CArchivePreviewDlg::OnNMCustomDrawArchiveEntries(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVCUSTOMDRAW pnmlvcd = (LPNMLVCUSTOMDRAW)pNMHDR;

	if (pnmlvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	}

	if (pnmlvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && pnmlvcd->nmcd.lItemlParam == 1)
		//Show unavailable or incomplete entries in grey
		pnmlvcd->clrText = RGB(128, 128, 128);

	*pResult = CDRF_DODEFAULT;
}

void CArchivePreviewDlg::OnBnExplain()
{
	LocMessageBox(_T("ARCHPREV_ATTRIBINFO"), MB_OK | MB_ICONINFORMATION, 0);
}

void CArchivePreviewDlg::OnBnClickedRead()
{
	UpdateArchiveDisplay(true);
}

void CArchivePreviewDlg::OnBnClickedCreateRestored()
{
	if (!static_cast<CShareableFile*>((*m_paFiles)[0])->IsPartFile())
		return;

	CPartFile *file = static_cast<CPartFile*>((*m_paFiles)[0]);

	if (!file->m_bRecoveringArchive && !file->m_bPreviewing)
		CArchiveRecovery::recover(file, true, thePrefs.GetPreviewCopiedArchives());
}

// ###########################################################################

/*****************************************
				A C E
 *****************************************/
int CArchivePreviewDlg::ShowAceResults(int succ, archiveScannerThreadParams_s *tp)
{

	if (succ == 0) {
		SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("ARCPREV_INSUFFDATA")));
		return -1;
	}

	// file contents into list
	bool statusEncrypted = false;
	UINT uArchiveFileEntries = 0;

	LPCTSTR uid = tp->ai->ACEdir->IsEmpty()
		? _T("ARCPREV_INSUFFDATA")
		: tp->file->IsPartFile() ? _T("ARCPREV_LISTMAYBEINCOMPL") : 0;
	CString temp;
	temp.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("ARCPARSED")), !IsEmpty(uid) ? (LPCTSTR)GetResString(uid) : EMPTY);
	SetDlgItemText(IDC_INFO_STATUS, temp);

	if (!tp->ai->ACEdir->IsEmpty()) {
		m_ContentList.SetRedraw(FALSE);
		for (POSITION pos = tp->ai->ACEdir->GetHeadPosition(); tp->m_bIsValid && pos != NULL;) {
			const ACE_BlockFile *block = tp->ai->ACEdir->GetNext(pos);
			int uSubId = 1;

			bool bCompleteEntry = !tp->file->IsPartFile()
				|| static_cast<CPartFile*>(tp->file)->IsCompleteBDSafe(block->data_offset, block->data_offset + block->PACK_SIZE);
			bool bIsDirectory = ((block->FILE_ATTRIBS & FILE_ATTRIBUTE_DIRECTORY) != 0);
			if (!bIsDirectory)
				++uArchiveFileEntries;

			// file/folder name
			temp = CString(block->FNAME);
			int iSystemImage = bIsDirectory ? theApp.GetFileTypeSystemImageIdx(_T("\\"), 1) : theApp.GetFileTypeSystemImageIdx(temp, temp.GetLength());
			int iItem = m_ContentList.InsertItem(LVIF_TEXT | LVIF_PARAM | (iSystemImage > 0 ? LVIF_IMAGE : 0)
				, INT_MAX, temp, 0, 0, iSystemImage, static_cast<LPARAM>(!bCompleteEntry));

			if (bIsDirectory)
				uSubId += 3;
			else {
				// size (uncompressed)
				UINT64 size = block->ORIG_SIZE;
				m_ContentList.SetItemText(iItem, uSubId++, (LPCTSTR)CastItoXBytes(size));

				// crc
				temp.Format(_T("%08X"), block->CRC32);
				m_ContentList.SetItemText(iItem, uSubId++, temp);
			}

			// attribs
			if (block->HEAD_FLAGS & 0x4000) {
				statusEncrypted = true;
				temp = _T("P");
			} else
				temp.Empty();

			if (bIsDirectory) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('D');
			}

			if (block->HEAD_FLAGS & 0x2000) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('>');
			}
			if (block->HEAD_FLAGS & 0x1000) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('<');
			}
			if (block->HEAD_FLAGS & 0x02) {
				// file comment - theoretically
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('C');
			}

			if (!bCompleteEntry) {
				// packed data not yet downloaded
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('M');
			}

			if (!bIsDirectory) {
				// compression level
				if (!temp.IsEmpty())
					temp += _T(", ");
				temp.AppendFormat(_T("L%i"), (BYTE)(block->TECHINFO >> 8));
				m_ContentList.SetItemText(iItem, uSubId++, temp);
			}

			// date time
			UINT16 fdate, ftime;
			ftime = (UINT16)block->FTIME;
			fdate = (UINT16)(block->FTIME >> 16);
			CTime lm = CTime((WORD)(((fdate >> 9) & 0x7f) + 1980)
				, (WORD)((fdate >> 5) & 0xf)
				, (WORD)(fdate & 0x1f)
				, (WORD)((ftime >> 11) & 0x1f)
				, (WORD)((ftime >> 5) & 0x3f)
				, (WORD)((ftime & 0x1f) * 2));
			m_ContentList.SetItemText(iItem, uSubId++, lm.Format(thePrefs.GetDateTimeFormat4Log()));

			// cleanup
			delete block;
		}
		m_ContentList.SetRedraw(TRUE);
	}

	temp.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("FILES")), uArchiveFileEntries);
	SetDlgItemText(IDC_INFO_FILECOUNT, temp);

	// general info / archive attribs
	CString status;
	if (statusEncrypted)
		status = GetResString(_T("PASSWPROT"));

	if (tp->ai->ACEhdr) {
		if (tp->ai->ACEhdr->HEAD_FLAGS & 0x8000) {
			if (!status.IsEmpty())
				status += _T(',');
			status += _T("Solid");
		}
		if (tp->ai->ACEhdr->HEAD_FLAGS & 0x4000) {
			if (!status.IsEmpty())
				status += _T(',');
			status += _T("Locked");
		}
		if (tp->ai->ACEhdr->HEAD_FLAGS & 0x2000) {
			if (!status.IsEmpty())
				status += _T(',');
			status += _T("RecoveryRec");
		}
		if (tp->ai->ACEhdr->COMMENT_SIZE > 0) {
			if (!status.IsEmpty())
				status += _T(',');
			status += GetResString(_T("COMMENT"));
		}
	}

	SetDlgItemText(IDC_INFO_ATTR, status);
	//... any other info?

	delete tp->ai->ACEdir;
	tp->ai->ACEdir = NULL;
	if (tp->ai->ACEhdr) {
		delete tp->ai->ACEhdr;
		tp->ai->ACEhdr = NULL;
	}

	return 0;
}

/*****************************************
				I S O
 *****************************************/
int CArchivePreviewDlg::ShowISOResults(int succ, archiveScannerThreadParams_s *tp)
{

	if (succ == 0) {
		SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("UNSUPPORTEDIMAGE")));
		delete tp->ai->ISOdir;
		return -1;
	}

	// file contents into list
	DWORD filecount = 0;

	LPCTSTR uid = tp->ai->ISOdir->IsEmpty()
		? _T("ARCPREV_INSUFFDATA")
		: tp->file->IsPartFile() ? _T("ARCPREV_LISTMAYBEINCOMPL") : 0;
	CString temp;
	temp.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("ARCPARSED")), !IsEmpty(uid) ? (LPCTSTR)GetResString(uid) : EMPTY);
	SetDlgItemText(IDC_INFO_STATUS, temp);

	m_ContentList.SetRedraw(FALSE);

	for (POSITION pos = tp->ai->ISOdir->GetHeadPosition(); tp->m_bIsValid && pos != NULL;) {
		ISO_FileFolderEntry *file = tp->ai->ISOdir->GetNext(pos);

		bool bCompleteEntry = !tp->file->IsPartFile()
			|| static_cast<CPartFile*>(tp->file)->IsCompleteBDSafe(
				LODWORD(file->sector1OfExtension) * (uint64)tp->ai->isoInfos.secSize
				, (LODWORD(file->sector1OfExtension) * (uint64)tp->ai->isoInfos.secSize) + file->dataSize);

		temp = CString(file->name);
		// remove separator extensions
		int sep = temp.ReverseFind(_T(';'));
		if (sep >= 0)
			temp.Delete(sep, temp.GetLength() - sep);

		int iSystemImage = theApp.GetFileTypeSystemImageIdx(temp, temp.GetLength());
		int iItem = m_ContentList.InsertItem(LVIF_TEXT | LVIF_PARAM | (iSystemImage > 0 ? LVIF_IMAGE : 0)
						, INT_MAX, temp, 0, 0, iSystemImage, static_cast<LPARAM>(!bCompleteEntry));

		// attribs

		//is directory?
		if (file->fileFlags & ISO_DIRECTORY)
			temp = _T("D");
		else {
			temp.Empty();
			++filecount;

			// size
			UINT32 size = LODWORD(file->dataSize);
			m_ContentList.SetItemText(iItem, 1, (LPCTSTR)CastItoXBytes(size));
		}

		// file attribs
		if (file->fileFlags & ISO_HIDDEN) {
			if (!temp.IsEmpty())
				temp += _T(',');
			temp += _T('H');
		}
		if (file->fileFlags & ISO_READONLY) {
			if (!temp.IsEmpty())
				temp += _T(',');
			temp += _T('R');
		}

		if (!bCompleteEntry) {
			// packed data not yet downloaded
			if (!temp.IsEmpty())
				temp += _T(',');
			temp += _T('M');
		}

		m_ContentList.SetItemText(iItem, 3, temp);

		// date time
		CTime lm = CTime(
			1900 + file->dateTime.year,
			file->dateTime.month,
			file->dateTime.day,
			file->dateTime.hour,
			file->dateTime.minute,
			file->dateTime.second
		);
		m_ContentList.SetItemText(iItem, 4, lm.Format(thePrefs.GetDateTimeFormat4Log()) + _T(" GMT"));

		// cleanup
		delete file;
	}
	m_ContentList.SetRedraw(TRUE);

	temp.Format(_T("%s: %lu"), (LPCTSTR)GetResString(_T("FILES")), filecount);
	SetDlgItemText(IDC_INFO_FILECOUNT, temp);

	if (tp->ai->isoInfos.bBootable)
		temp = GetResString(_T("BOOTABLE"));
	else
		temp.Empty();

	if (tp->ai->isoInfos.type & ISOtype_9660) {
		if (!temp.IsEmpty())
			temp += _T(',');
		temp += _T("ISO9660");
	}
	if (tp->ai->isoInfos.type & ISOtype_joliet) {
		if (!temp.IsEmpty())
			temp += _T(',');
		temp += _T("Joliet");
	}
	if (tp->ai->isoInfos.type & ISOtype_UDF_nsr02 || tp->ai->isoInfos.type & ISOtype_UDF_nsr03) {
		if (!temp.IsEmpty())
			temp += _T(',');
		temp.AppendFormat(_T("UDF (%s)"), (LPCTSTR)GetResString(_T("UNSUPPORTEDIMAGE")));
	}

	SetDlgItemText(IDC_INFO_ATTR, temp);

	delete tp->ai->ISOdir;

	return 0;
}

/*****************************************
				R A R
 *****************************************/
int CArchivePreviewDlg::ShowRarResults(int succ, archiveScannerThreadParams_s *tp)
{

	if (succ == 0) {
		SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("ARCPREV_INSUFFDATA")));
		return -1;
	}

	// file contents into list
	bool statusEncrypted;
	UINT uArchiveFileEntries = 0;

	CString temp;
	if (tp->ai->rarFlags & 0x0080) {
		statusEncrypted = true;
		SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("RARFULLENCR")));
	} else {
		statusEncrypted = false;
		LPCTSTR uid = tp->ai->RARdir->IsEmpty()
			? _T("ARCPREV_INSUFFDATA")
			: tp->file->IsPartFile() ? _T("ARCPREV_LISTMAYBEINCOMPL") : 0;
		temp.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("ARCPARSED")), !IsEmpty(uid) ? (LPCTSTR)GetResString(uid) : EMPTY);
		SetDlgItemText(IDC_INFO_STATUS, temp);
	}

	if (!tp->ai->RARdir->IsEmpty()) {
		char buf[MAX_PATH + MAX_PATH * 2];

		// This file could be a self-extracting RAR archive. Now that we eventually have read something
		// RAR-like from that file, we can set the 'verified' file type.
		tp->file->SetVerifiedFileType(ARCHIVE_RAR);

		m_ContentList.SetRedraw(FALSE);
		for (POSITION pos = tp->ai->RARdir->GetHeadPosition(); tp->m_bIsValid && pos != NULL;) {
			RAR_BlockFile *block = tp->ai->RARdir->GetNext(pos);
			int uSubId = 1;

			bool bCompleteEntry = !tp->file->IsPartFile()
				|| static_cast<CPartFile*>(tp->file)->IsCompleteBDSafe(block->offsetData, block->offsetData + block->dataLength);
			bool bIsDirectory = (block->HEAD_FLAGS & 0xE0) == 0xE0;
			uArchiveFileEntries += static_cast<UINT>(!bIsDirectory);

			// file/folder name
			memcpy(buf, block->FILE_NAME, block->NAME_SIZE);
			buf[block->NAME_SIZE] = 0;

			// read Unicode name from the name buffer and decode it
			if (block->HEAD_FLAGS & 0x0200) {
				unsigned asciilen = (unsigned)(strlen(buf) + 1);
				wchar_t nameuc[MAX_PATH];
				EncodeFileName enc;
				enc.Decode(buf, (byte*)buf + asciilen, block->NAME_SIZE - asciilen, nameuc, _countof(nameuc));
				temp = nameuc;
			} else
				temp = buf;

			int iSystemImage = bIsDirectory ? theApp.GetFileTypeSystemImageIdx(_T("\\"), 1) : theApp.GetFileTypeSystemImageIdx(temp, temp.GetLength());
			int iItem = m_ContentList.InsertItem(LVIF_TEXT | LVIF_PARAM | (iSystemImage > 0 ? LVIF_IMAGE : 0)
				, INT_MAX, temp, 0, 0, iSystemImage, static_cast<LPARAM>(!bCompleteEntry));

			if ((block->HEAD_FLAGS & 0xE0) != 0xE0) {

				// size (uncompressed)
				UINT64 size = block->UNP_SIZE;
				if (block->ATTR & 0x100)
					size += block->HIGH_UNP_SIZE * 0x100000000;
				m_ContentList.SetItemText(iItem, uSubId++, (LPCTSTR)CastItoXBytes(size));

				// crc
				temp.Format(_T("%08X"), block->FILE_CRC);
				m_ContentList.SetItemText(iItem, uSubId++, temp);

			} else
				uSubId += 2;

			// attribs
			if (block->HEAD_FLAGS & 0x04) {
				statusEncrypted = true;
				temp = _T("P");
			} else
				temp.Empty();

			//is directory?
			if (bIsDirectory) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('D');
			}

			if (block->HEAD_FLAGS & 0x01) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('<');
			}
			if (block->HEAD_FLAGS & 0x02) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('>');
			}
			if (block->HEAD_FLAGS & 0x08) {
				// file comment - theoretically
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('C');
			}

			if (!bCompleteEntry) {
				// packed data not yet downloaded
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('M');
			}

			if (!temp.IsEmpty())
				temp += _T(", ");
			temp.AppendFormat(_T("L%u"), block->METHOD - 0x30u);

			m_ContentList.SetItemText(iItem, uSubId++, temp);

			// date time
			UINT16 fdate, ftime;
			ftime = (UINT16)block->FTIME;
			fdate = (UINT16)(block->FTIME >> 16);
			CTime lm = CTime((WORD)(((fdate >> 9) & 0x7f) + 1980)
				, (WORD)((fdate >> 5) & 0xf)
				, (WORD)(fdate & 0x1f)
				, (WORD)((ftime >> 11) & 0x1f)
				, (WORD)((ftime >> 5) & 0x3f)
				, (WORD)((ftime & 0x1f) * 2));
			m_ContentList.SetItemText(iItem, uSubId++, lm.Format(thePrefs.GetDateTimeFormat4Log()));

			// cleanup
			delete[] block->FILE_NAME;
			delete block;
		}
		m_ContentList.SetRedraw(TRUE);
	}

	temp.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("FILES")), uArchiveFileEntries);
	SetDlgItemText(IDC_INFO_FILECOUNT, temp);

	// general info / archive attribs
	CString status;
	if (statusEncrypted)
		status = GetResString(_T("PASSWPROT"));

	if (tp->ai->rarFlags & 0x0008) {
		if (!status.IsEmpty())
			status += _T(',');
		status += _T("Solid");
	}
	if (tp->ai->rarFlags & 0x0004) {
		if (!status.IsEmpty())
			status += _T(',');
		status += _T("Locked");
	}
	if (tp->ai->rarFlags & 0x0040) {
		if (!status.IsEmpty())
			status += _T(',');
		status += _T("RecoveryRec");
	}
	if (tp->ai->rarFlags & 0x0002) {
		if (!status.IsEmpty())
			status += _T(',');
		status += GetResString(_T("COMMENT"));
	}

	SetDlgItemText(IDC_INFO_ATTR, status);
	//... any other info?

	delete tp->ai->RARdir;
	tp->ai->RARdir = NULL;

	return 0;
}

/*****************************************
				Z I P
 *****************************************/
int CArchivePreviewDlg::ShowZipResults(int succ, archiveScannerThreadParams_s *tp)
{

	if (succ == 0) {
		SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("ARCPREV_INSUFFDATA")));
		return 1;
	}

	// file contents into list
	CString temp;
	if (tp->ai->bZipCentralDir)
		temp = GetResString(_T("ARCPREV_DIRSUCCREAD"));
	else {
		temp.Format(_T("%s %s %s"), (LPCTSTR)GetResString(_T("ARCPREV_DIRNOTFOUND"))
			, (LPCTSTR)GetResString(_T("ARCPARSED"))
			, (LPCTSTR)GetResString(tp->ai->centralDirectoryEntries->IsEmpty() ? _T("ARCPREV_INSUFFDATA") : _T("ARCPREV_LISTMAYBEINCOMPL")));
	}
	SetDlgItemText(IDC_INFO_STATUS, temp);

	UINT uArchiveFileEntries = 0;
	bool statusEncrypted = false;
	if (!tp->ai->centralDirectoryEntries->IsEmpty()) {
		m_ContentList.SetRedraw(FALSE);
		for (POSITION pos = tp->ai->centralDirectoryEntries->GetHeadPosition(); tp->m_bIsValid && pos != NULL;) {
			ZIP_CentralDirectory *cdEntry = tp->ai->centralDirectoryEntries->GetNext(pos);
			int uSubId = 1;

			// compressed data not available yet?
			bool bCompleteEntry = true;
			if (tp->file->IsPartFile()) {
				const CPartFile *pf = static_cast<CPartFile*>(tp->file);
				uint64 dataoffset = cdEntry->relativeOffsetOfLocalHeader
					+ sizeof(ZIP_Entry) - (3 * sizeof(BYTE*))
					+ cdEntry->lenComment
					+ cdEntry->lenFilename
					+ cdEntry->lenExtraField;
				bCompleteEntry = pf->IsCompleteBDSafe(dataoffset, dataoffset + cdEntry->lenCompressed);
			}

			// Is directory?
			bool bIsDirectory = (cdEntry->externalFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				|| (cdEntry->crc32 == 0 && cdEntry->lenCompressed == 0 && cdEntry->lenUncompressed == 0 && cdEntry->lenFilename != 0);

			uArchiveFileEntries += static_cast<UINT>(!bIsDirectory);

			// file/folder name
			CStringA strBuffA((char*)cdEntry->filename, cdEntry->lenFilename);
			temp = (CString)strBuffA;
			int iSystemImage = bIsDirectory ? theApp.GetFileTypeSystemImageIdx(_T("\\"), 1) : theApp.GetFileTypeSystemImageIdx(temp, temp.GetLength());
			int iItem = m_ContentList.InsertItem(LVIF_TEXT | LVIF_PARAM | (iSystemImage > 0 ? LVIF_IMAGE : 0)
				, INT_MAX, temp, 0, 0, iSystemImage, static_cast<LPARAM>(!bCompleteEntry));

			// size (uncompressed)
			if (!bIsDirectory) {
				m_ContentList.SetItemText(iItem, uSubId++, (LPCTSTR)CastItoXBytes(cdEntry->lenUncompressed));

				// crc
				temp.Format(_T("%08X"), cdEntry->crc32);
				m_ContentList.SetItemText(iItem, uSubId++, temp);
			} else
				uSubId += 2;

			// attribs
			if (cdEntry->generalPurposeFlag & 0x01 || cdEntry->generalPurposeFlag & 0x40) {
				statusEncrypted = true;
				temp = _T("P");
			} else
				temp.Empty();

			if (bIsDirectory) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('D');
			}

			// compressed data not available yet?
			if (!bCompleteEntry) {
				if (!temp.IsEmpty())
					temp += _T(',');
				temp += _T('M');
			}

			m_ContentList.SetItemText(iItem, uSubId++, temp);

			// date time
			CTime lm = CTime((WORD)(((cdEntry->lastModFileDate >> 9) & 0x7f) + 1980)
				, (WORD)((cdEntry->lastModFileDate >> 5) & 0xf)
				, (WORD)(cdEntry->lastModFileDate & 0x1f)
				, (WORD)((cdEntry->lastModFileTime >> 11) & 0x1f)
				, (WORD)((cdEntry->lastModFileTime >> 5) & 0x3f)
				, (WORD)((cdEntry->lastModFileTime & 0x1f) * 2));
			m_ContentList.SetItemText(iItem, uSubId++, lm.Format(thePrefs.GetDateTimeFormat4Log()));

			// comment
			if (cdEntry->lenComment) {
				strBuffA.SetString((char*)cdEntry->comment, cdEntry->lenComment);
				temp = (CString)strBuffA;
			} else
				temp.Empty();
			m_ContentList.SetItemText(iItem, uSubId++, temp);


			// cleanup
			delete[] cdEntry->filename;
			if (cdEntry->lenExtraField > 0)
				delete[] cdEntry->extraField;
			if (cdEntry->lenComment > 0)
				delete[] cdEntry->comment;
			delete cdEntry;
		}
		m_ContentList.SetRedraw(TRUE);
	}

	temp.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("FILES")), uArchiveFileEntries);
	SetDlgItemText(IDC_INFO_FILECOUNT, temp);

	// general info / archive attribs

	SetDlgItemText(IDC_INFO_ATTR, (statusEncrypted ? (LPCTSTR)GetResString(_T("PASSWPROT")) : EMPTY));
	//... any other info?

	delete tp->ai->centralDirectoryEntries;
	tp->ai->centralDirectoryEntries = NULL;

	return 0;
}

// ##################################################################
static void FreeMemory(void *arg)
{
	archiveScannerThreadParams_s *tp = static_cast<archiveScannerThreadParams_s*>(arg);

	delete tp->filled;
	tp->filled = NULL;

	if (tp->ai->RARdir) {
		while (!tp->ai->RARdir->IsEmpty()) {
			RAR_BlockFile *block = tp->ai->RARdir->RemoveHead();
			delete[] block->FILE_NAME;
			delete block;
		}
		delete tp->ai->RARdir;
		tp->ai->RARdir = NULL;
	}

	if (tp->ai->centralDirectoryEntries) {
		while (!tp->ai->centralDirectoryEntries->IsEmpty()) {
			ZIP_CentralDirectory *cdEntry = tp->ai->centralDirectoryEntries->RemoveHead();
			// cleanup
			delete[] cdEntry->filename;
			if (cdEntry->lenExtraField > 0)
				delete[] cdEntry->extraField;
			if (cdEntry->lenComment > 0)
				delete[] cdEntry->comment;
			delete cdEntry;
		}
		delete tp->ai->centralDirectoryEntries;
		tp->ai->centralDirectoryEntries = NULL;
	}

	delete tp->ai->ACEhdr;
	tp->ai->ACEhdr = NULL;
	if (tp->ai->ACEdir) {
		while (!tp->ai->ACEdir->IsEmpty())
			delete tp->ai->ACEdir->RemoveHead();
		delete tp->ai->ACEdir;
		tp->ai->ACEdir = NULL;
	}

	delete tp->ai;
	tp->ai = NULL;

	delete tp;
}

void CArchivePreviewDlg::UpdateArchiveDisplay(bool doscan)
{
	if (m_paFiles == NULL || m_paFiles->GetSize() <= 0)
		m_pFile = NULL;
	else if (m_pFile == static_cast<CShareableFile*>((*m_paFiles)[0]))
		return;
	// invalidate previous scanning thread if still running
	if (m_activeTParams != NULL) {
		m_activeTParams->m_bIsValid = false;
		m_activeTParams = NULL; // thread may still run but is not our active one any more
	}
	m_progressbar.SetPos(0);

	m_ContentList.DeleteAllItems();
	m_ContentList.UpdateWindow();

	// set infos
	SetDlgItemText(IDC_APV_FILEINFO, EMPTY);
	SetDlgItemText(IDC_INFO_ATTR, EMPTY);
	SetDlgItemText(IDC_INFO_STATUS, EMPTY);
	SetDlgItemText(IDC_INFO_FILECOUNT, EMPTY);

	if (m_paFiles == NULL || m_paFiles->GetSize() <= 0)
		return;

	CShareableFile *pFile = static_cast<CShareableFile*>((*m_paFiles)[0]);

	GetDlgItem(IDC_RESTOREARCH)->EnableWindow(pFile->IsPartFile()
		&& static_cast<CPartFile*>(pFile)->IsArchive(true)
		&& static_cast<CPartFile*>(pFile)->IsReadyForPreview());

	EFileType type = GetFileTypeEx(pFile);
	LPCTSTR pType;
	switch (type) {
	case ARCHIVE_ZIP:
		pType = _T("ZIP");
		break;
	case ARCHIVE_RAR:
		pType = _T("RAR");
		break;
	case ARCHIVE_ACE:
		pType = _T("ACE");
		break;
	case IMAGE_ISO:
		pType = _T("ISO");
		break;
	default:
		SetDlgItemText(IDC_INFO_TYPE, GetResString(_T("ARCPREV_UNKNOWNFORMAT")));
		return;
	}

	SetDlgItemText(IDC_INFO_TYPE, pType);
	if (!doscan)
		return;

	// preparing archive processing
	archiveinfo_s *ai = new archiveinfo_s;

	// get filled area list
	CArray<Gap_Struct> *filled = new CArray<Gap_Struct>;
	if (pFile->IsPartFile()) {
		static_cast<CPartFile*>(pFile)->GetFilledArray(*filled);
		if (filled->IsEmpty()) {
			SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("ARCPREV_INSUFFDATA")));
			delete filled;
			delete ai;
			return;
		}
	} else
		filled->Add(Gap_Struct{ 0, (uint64)pFile->GetFileSize() });

	SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("ARCPREV_PLEASEWAIT")));

	switch (type) {
	case ARCHIVE_ZIP:
		ai->centralDirectoryEntries = new CTypedPtrList<CPtrList, ZIP_CentralDirectory*>;
		break;
	case ARCHIVE_RAR:
		ai->RARdir = new CTypedPtrList<CPtrList, RAR_BlockFile*>;
		break;
	case ARCHIVE_ACE:
		ai->ACEdir = new CTypedPtrList<CPtrList, ACE_BlockFile*>;
		break;
	case IMAGE_ISO:
		ai->ISOdir = new CTypedPtrList<CPtrList, ISO_FileFolderEntry*>;
	}

	// prepare threadparams
	archiveScannerThreadParams_s *tp = new archiveScannerThreadParams_s;
	m_activeTParams = tp;
	tp->ai = ai;
	tp->file = pFile;
	tp->filled = filled;
	tp->type = type;
	tp->ownerHwnd = m_hWnd;
	tp->progressHwnd = GetDlgItem(IDC_ARCHPROGRESS)->m_hWnd;
	tp->curProgress = 0;
	tp->m_bIsValid = true;

	// start scanning thread
	if (AfxBeginThread(RunArchiveScanner, (LPVOID)tp, THREAD_PRIORITY_LOWEST) == NULL) {
		FreeMemory(tp);
		SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("ERROR")));
	}
}

UINT AFX_CDECL CArchivePreviewDlg::RunArchiveScanner(LPVOID pParam)
{
	DbgSetThreadName("ArchiveScanner");

	archiveScannerThreadParams_s *tp = static_cast<archiveScannerThreadParams_s*>(pParam);

	int ret;
	CFile inFile;

	if (!inFile.Open(tp->file->GetFilePath(), CFile::modeRead | CFile::shareDenyNone))
		ret = -1;
	else {
		switch (tp->type) {
		case ARCHIVE_ZIP:
			ret = CArchiveRecovery::recoverZip(&inFile, NULL, tp, tp->filled, (inFile.GetLength() == tp->file->GetFileSize()));
			break;
		case ARCHIVE_RAR:
			ret = CArchiveRecovery::recoverRar(&inFile, NULL, tp, tp->filled);
			break;
		case ARCHIVE_ACE:
			ret = CArchiveRecovery::recoverAce(&inFile, NULL, tp, tp->filled);
			break;
		case IMAGE_ISO:
			ret = CArchiveRecovery::recoverISO(&inFile, NULL, tp, tp->filled);
			break;
		default:
			ret = 0;
		}

		inFile.Close();
	}

	if (!tp->m_bIsValid || !::IsWindow(tp->ownerHwnd) || !::SendMessage(tp->ownerHwnd, UM_ARCHIVESCANDONE, ret, (LPARAM)tp))
		FreeMemory(tp);
	return 0;
}

LRESULT CArchivePreviewDlg::ShowScanResults(WPARAM wParam, LPARAM lParam)
{
	CWaitCursor curHourglass;

	int ret = static_cast<int>(wParam);
	archiveScannerThreadParams_s *tp = reinterpret_cast<archiveScannerThreadParams_s*>(lParam);
	ASSERT(tp->ownerHwnd == m_hWnd);

	// We may receive 'stopped' archive thread results here, just ignore them (but free the memory)
	if (tp->m_bIsValid) {
		m_progressbar.SetPos(0);
		if (ret == -1)
			SetDlgItemText(IDC_INFO_STATUS, GetResString(_T("IMP_ERR_IO")));
		else { //if (tp->m_bIsValid) {

			// hide two unused columns for ISO and unhide for other types
			if (tp->type != IMAGE_ISO) {
				if (m_ContentList.GetColumnWidth(2) == 0)
					m_ContentList.SetColumnWidth(2, m_StoredColWidth2);
				if (m_ContentList.GetColumnWidth(5) == 0)
					m_ContentList.SetColumnWidth(5, m_StoredColWidth5);
			} else {
				int w = m_ContentList.GetColumnWidth(2);
				if (w > 0)
					m_StoredColWidth2 = w;
				w = m_ContentList.GetColumnWidth(5);
				if (w > 0)
					m_StoredColWidth5 = w;

				m_ContentList.SetColumnWidth(2, 0);
				m_ContentList.SetColumnWidth(5, 0);
			}

			switch (tp->type) {
			case ARCHIVE_ZIP:
				ShowZipResults(ret, tp);
				break;
			case ARCHIVE_RAR:
				ShowRarResults(ret, tp);
				break;
			case ARCHIVE_ACE:
				ShowAceResults(ret, tp);
				break;
			case IMAGE_ISO:
				ShowISOResults(ret, tp);
			}
		}
		ASSERT(tp == m_activeTParams);
		m_activeTParams = NULL;
	}

	FreeMemory(tp);
	return 1;
}

void CArchivePreviewDlg::OnContextMenu(CWnd *pWnd, CPoint point)
{
	if (m_bReducedDlg) {
		CMenuXP menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, MP_UPDATE, GetResString(_T("SV_UPDATE")));
		menu.AppendMenu(MF_STRING, MP_HM_HELP, GetResString(_T("EM_HELP")));
		menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	} else
		__super::OnContextMenu(pWnd, point);
}
