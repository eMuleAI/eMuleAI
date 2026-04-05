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
#include "emuledlg.h"
#include "toolbarwnd.h"
#include "HelpIDs.h"
#include "OtherFunctions.h"
#include "MenuCmds.h"
#include "DownloadListCtrl.h"
#include "TransferDlg.h"
#include "Preferences.h"
#include "Otherfunctions.h"
#include "Preview.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	enum EDownloadToolbarImageIndex
	{
		DownloadToolbarImagePriority = 0,
		DownloadToolbarImagePause,
		DownloadToolbarImageStop,
		DownloadToolbarImageResume,
		DownloadToolbarImageDelete,
		DownloadToolbarImageOpenFile,
		DownloadToolbarImageOpenFolder,
		DownloadToolbarImagePreview,
		DownloadToolbarImageFileInfo,
		DownloadToolbarImageFileComments,
		DownloadToolbarImageEd2kLink,
		DownloadToolbarImageCategory,
		DownloadToolbarImageClearComplete,
		DownloadToolbarImageSearchRelated,
		DownloadToolbarImageFind,
		DownloadToolbarImageSave,
		DownloadToolbarImageReload,
		DownloadToolbarImageBackup,
		DownloadToolbarImageInspect,
		DownloadToolbarImageCount
	};

	const UINT kMaxPreviewToolbarButtons = 10;

	int GetPreviewToolbarImageIndex(const UINT uPreviewAppIndex)
	{
		return DownloadToolbarImageCount + static_cast<int>(uPreviewAppIndex);
	}
}

IMPLEMENT_DYNAMIC(CToolbarWnd, CDialogBar);

BEGIN_MESSAGE_MAP(CToolbarWnd, CDialogBar)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_SYSCOLORCHANGE()
	ON_MESSAGE(WM_INITDIALOG, OnInitDialog)
	ON_WM_SETCURSOR()
	ON_WM_HELPINFO()
	ON_NOTIFY(TBN_DROPDOWN, IDC_DTOOLBAR, OnBtnDropDown)
	ON_WM_SYSCOMMAND()
	ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()


CToolbarWnd::CToolbarWnd()
	: m_hcurMove(::LoadCursor(NULL, IDC_SIZEALL)) // load default windows system cursor (a shared resource)
	, m_pCommandTargetWnd()
{
}

void CToolbarWnd::DoDataExchange(CDataExchange *pDX)
{
	CDialogBar::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DTOOLBAR, m_btnBar);
}

void CToolbarWnd::FillToolbar()
{
	m_btnBar.DeleteAllButtons();
	UpdateImageLists();

	unsigned int iPreviewAppCount = min(thePreviewApps.GetPreviewAppCount(), kMaxPreviewToolbarButtons);
	unsigned int iButtonCount = iPreviewAppCount + 24;
	int m_iDontShowPreviewButton = 0;

	// If default preview app is already defined in PreviewApps.dat, don't create a "Preview" button since corresponding "Preview with" will be created.
	for (int i = 0; i < iPreviewAppCount; i++) {
		if (thePrefs.GetVideoPlayer() == thePreviewApps.GetPreviewAppCmd(i) && thePrefs.GetVideoPlayerArgs() == thePreviewApps.GetPreviewAppCmdArgs(i)) {
			iButtonCount = iButtonCount - 1;
			m_iDontShowPreviewButton = 1;
			break;
		}
	}

	TBBUTTON* atb1 = new TBBUTTON[iButtonCount];

	atb1[0].iBitmap = DownloadToolbarImagePriority;
	atb1[0].idCommand = MP_PRIOLOW;
	atb1[0].fsState = TBSTATE_WRAP;
	atb1[0].fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE;
	CString sPrio(GetResString(_T("PRIORITY")));
	sPrio.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("DOWNLOAD")));
	atb1[0].iString = m_btnBar.AddString(sPrio);

	atb1[1].iBitmap = DownloadToolbarImagePause;
	atb1[1].idCommand = MP_PAUSE;
	atb1[1].fsState = TBSTATE_WRAP;
	atb1[1].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[1].iString = m_btnBar.AddString(GetResString(_T("DL_PAUSE")));

	atb1[2].iBitmap = DownloadToolbarImageStop;
	atb1[2].idCommand = MP_STOP;
	atb1[2].fsState = TBSTATE_WRAP;
	atb1[2].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[2].iString = m_btnBar.AddString(GetResString(_T("DL_STOP")));

	atb1[3].iBitmap = DownloadToolbarImageResume;
	atb1[3].idCommand = MP_RESUME;
	atb1[3].fsState = TBSTATE_WRAP;
	atb1[3].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[3].iString = m_btnBar.AddString(GetResString(_T("DL_RESUME")));

	atb1[4].iBitmap = DownloadToolbarImageDelete;
	atb1[4].idCommand = MP_CANCEL;
	atb1[4].fsState = TBSTATE_WRAP;
	atb1[4].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[4].iString = m_btnBar.AddString(GetResString(_T("MAIN_BTN_CANCEL")));
	/////////////
	atb1[5].iBitmap = -1;
	atb1[5].idCommand = 0;
	atb1[5].fsState = TBSTATE_WRAP;
	atb1[5].fsStyle = BTNS_SEP;
	atb1[5].iString = -1;

	if (m_iDontShowPreviewButton == 0) {
		atb1[6].iBitmap = DownloadToolbarImagePreview;
		atb1[6].idCommand = MP_PREVIEW;
		atb1[6].fsState = TBSTATE_WRAP;
		atb1[6].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[6].iString = m_btnBar.AddString(GetResString(_T("DL_PREVIEW")));
	}

	if (iPreviewAppCount >= 1) {
		atb1[7 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(0);
		atb1[7 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW1;
		atb1[7 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[7 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[7 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(0) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 2) {
		atb1[8 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(1);
		atb1[8 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW2;
		atb1[8 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[8 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[8 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(1) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 3) {
		atb1[9 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(2);
		atb1[9 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW3;
		atb1[9 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[9 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[9 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(2) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 4) {
		atb1[10 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(3);
		atb1[10 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW4;
		atb1[10 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[10 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[10 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(3) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 5) {
		atb1[11 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(4);
		atb1[11 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW5;
		atb1[11 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[11 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[11 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(4) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 6) {
		atb1[12 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(5);
		atb1[12 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW6;
		atb1[12 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[12 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[12 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(5) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 7) {
		atb1[13 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(6);
		atb1[13 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW7;
		atb1[13 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[13 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[13 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(6) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 8) {
		atb1[14 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(7);
		atb1[14 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW8;
		atb1[14 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[14 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[14 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(7) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 9) {
		atb1[15 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(8);
		atb1[15 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW9;
		atb1[15 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[15 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[15 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(8) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	if (iPreviewAppCount >= 10) {
		atb1[16 - m_iDontShowPreviewButton].iBitmap = GetPreviewToolbarImageIndex(9);
		atb1[16 - m_iDontShowPreviewButton].idCommand = MP_PREVIEW10;
		atb1[16 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
		atb1[16 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		atb1[16 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("PREVIEWWITH")) + _T(" ") + thePreviewApps.GetPreviewAppName(9) + GetResString(_T("LCLICK_RCLICK_EXPLANATION")));
	}

	/////////////
	atb1[iPreviewAppCount + 7 - m_iDontShowPreviewButton].iBitmap = -1;
	atb1[iPreviewAppCount + 7 - m_iDontShowPreviewButton].idCommand = 0;
	atb1[iPreviewAppCount + 7 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 7 - m_iDontShowPreviewButton].fsStyle = BTNS_SEP;
	atb1[iPreviewAppCount + 7 - m_iDontShowPreviewButton].iString = -1;

	atb1[iPreviewAppCount + 8 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageOpenFile;
	atb1[iPreviewAppCount + 8 - m_iDontShowPreviewButton].idCommand = MP_OPEN;
	atb1[iPreviewAppCount + 8 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 8 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 8 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("DL_OPEN")));

	atb1[iPreviewAppCount + 9 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageOpenFolder;
	atb1[iPreviewAppCount + 9 - m_iDontShowPreviewButton].idCommand = MP_OPENFOLDER;
	atb1[iPreviewAppCount + 9 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 9 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 9 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("OPENFOLDER")));

	atb1[iPreviewAppCount + 10 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageFileInfo;
	atb1[iPreviewAppCount + 10 - m_iDontShowPreviewButton].idCommand = MP_METINFO;
	atb1[iPreviewAppCount + 10 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 10 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 10 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("DL_INFO")));

	atb1[iPreviewAppCount + 11 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageFileComments;
	atb1[iPreviewAppCount + 11 - m_iDontShowPreviewButton].idCommand = MP_VIEWFILECOMMENTS;
	atb1[iPreviewAppCount + 11 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 11 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 11 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("CMT_SHOWALL")));

	atb1[iPreviewAppCount + 12 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageEd2kLink;
	atb1[iPreviewAppCount + 12 - m_iDontShowPreviewButton].idCommand = MP_SHOWED2KLINK;
	atb1[iPreviewAppCount + 12 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 12 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 12 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("DL_SHOWED2KLINK")));

	/////////////
	atb1[iPreviewAppCount + 13 - m_iDontShowPreviewButton].iBitmap = -1;
	atb1[iPreviewAppCount + 13 - m_iDontShowPreviewButton].idCommand = 0;
	atb1[iPreviewAppCount + 13 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 13 - m_iDontShowPreviewButton].fsStyle = BTNS_SEP;
	atb1[iPreviewAppCount + 13 - m_iDontShowPreviewButton].iString = -1;

	atb1[iPreviewAppCount + 14 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageSave;
	atb1[iPreviewAppCount + 14 - m_iDontShowPreviewButton].idCommand = MP_SAVEAPPSTATE;
	atb1[iPreviewAppCount + 14 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 14 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 14 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("SAVE_APP_STATE_TIP")));

	atb1[iPreviewAppCount + 15 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageReload;
	atb1[iPreviewAppCount + 15 - m_iDontShowPreviewButton].idCommand = MP_RELOADCONF;
	atb1[iPreviewAppCount + 15 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 15 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 15 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("RELOAD_CONF_TIP")));

	atb1[iPreviewAppCount + 16 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageBackup;
	atb1[iPreviewAppCount + 16 - m_iDontShowPreviewButton].idCommand = MP_BACKUP;
	atb1[iPreviewAppCount + 16 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 16 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 16 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("BACKUP_TIP")));

	atb1[iPreviewAppCount + 17 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageInspect;
	atb1[iPreviewAppCount + 17 - m_iDontShowPreviewButton].idCommand = MP_DOWNLOADINSPECTOR;
	atb1[iPreviewAppCount + 17 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 17 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 17 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("DOWNLOAD_INSPECTOR_NOW")));

	/////////////
	atb1[iPreviewAppCount + 18 - m_iDontShowPreviewButton].iBitmap = -1;
	atb1[iPreviewAppCount + 18 - m_iDontShowPreviewButton].idCommand = 0;
	atb1[iPreviewAppCount + 18 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 18 - m_iDontShowPreviewButton].fsStyle = BTNS_SEP;
	atb1[iPreviewAppCount + 18 - m_iDontShowPreviewButton].iString = -1;

	atb1[iPreviewAppCount + 19 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageCategory;
	atb1[iPreviewAppCount + 19 - m_iDontShowPreviewButton].idCommand = MP_NEWCAT;
	atb1[iPreviewAppCount + 19 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 19 - m_iDontShowPreviewButton].fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 19 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("TOCAT")));

	atb1[iPreviewAppCount + 20 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageClearComplete;
	atb1[iPreviewAppCount + 20 - m_iDontShowPreviewButton].idCommand = MP_CLEARCOMPLETED;
	atb1[iPreviewAppCount + 20 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 20 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 20 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("DL_CLEAR")));

	atb1[iPreviewAppCount + 21 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageSearchRelated;
	atb1[iPreviewAppCount + 21 - m_iDontShowPreviewButton].idCommand = MP_SEARCHRELATED;
	atb1[iPreviewAppCount + 21 - m_iDontShowPreviewButton].fsState = TBSTATE_WRAP;
	atb1[iPreviewAppCount + 21 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 21 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("SEARCHRELATED")));

	/////////////
	atb1[iPreviewAppCount + 22 - m_iDontShowPreviewButton].iBitmap = -1;
	atb1[iPreviewAppCount + 22 - m_iDontShowPreviewButton].idCommand = 0;
	atb1[iPreviewAppCount + 22 - m_iDontShowPreviewButton].fsState = TBSTATE_ENABLED | TBSTATE_WRAP;
	atb1[iPreviewAppCount + 22 - m_iDontShowPreviewButton].fsStyle = BTNS_SEP;
	atb1[iPreviewAppCount + 22 - m_iDontShowPreviewButton].iString = -1;

	atb1[iPreviewAppCount + 23 - m_iDontShowPreviewButton].iBitmap = DownloadToolbarImageFind;
	atb1[iPreviewAppCount + 23 - m_iDontShowPreviewButton].idCommand = MP_FIND;
	atb1[iPreviewAppCount + 23 - m_iDontShowPreviewButton].fsState = TBSTATE_ENABLED | TBSTATE_WRAP;
	atb1[iPreviewAppCount + 23 - m_iDontShowPreviewButton].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb1[iPreviewAppCount + 23 - m_iDontShowPreviewButton].iString = m_btnBar.AddString(GetResString(_T("FIND")));

	m_btnBar.AddButtons(iButtonCount, atb1);

	delete[] atb1;
}

void CToolbarWnd::UpdateImageLists()
{
	CImageList iml;
	int nFlags = theApp.m_iDfltImageListColorFlags;
	// older Windows versions image lists cannot create monochrome (disabled) icons which have alpha support
	// so we have to take care of this ourself
	const bool bNeedMonoIcons = thePrefs.GetWindowsVersion() < _WINVER_VISTA_ && nFlags != ILC_COLOR4;
	const UINT uPreviewAppCount = min(thePreviewApps.GetPreviewAppCount(), kMaxPreviewToolbarButtons);
	nFlags |= ILC_MASK;
	iml.Create(16, 16, nFlags, DownloadToolbarImageCount + uPreviewAppCount, 1);
	iml.Add(CTempIconLoader(_T("FILEPRIORITY")));
	iml.Add(CTempIconLoader(_T("PAUSE")));
	iml.Add(CTempIconLoader(_T("STOP")));
	iml.Add(CTempIconLoader(_T("RESUME")));
	iml.Add(CTempIconLoader(_T("DELETE")));
	iml.Add(CTempIconLoader(_T("OPENFILE")));
	iml.Add(CTempIconLoader(_T("OPENFOLDER")));
	iml.Add(CTempIconLoader(_T("PREVIEW")));
	iml.Add(CTempIconLoader(_T("FILEINFO")));
	iml.Add(CTempIconLoader(_T("FILECOMMENTS")));
	iml.Add(CTempIconLoader(_T("ED2KLINK")));
	iml.Add(CTempIconLoader(_T("CATEGORY")));
	iml.Add(CTempIconLoader(_T("CLEARCOMPLETE")));
	iml.Add(CTempIconLoader(_T("KadFileSearch")));
	iml.Add(CTempIconLoader(_T("Search")));
	iml.Add(CTempIconLoader(_T("SAVE")));
	iml.Add(CTempIconLoader(_T("RELOAD")));
	iml.Add(CTempIconLoader(_T("BACKUP")));
	iml.Add(CTempIconLoader(_T("INSPECT")));

	CImageList iml2;
	if (bNeedMonoIcons) {
		iml2.Create(16, 16, nFlags, DownloadToolbarImageCount + uPreviewAppCount, 1);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("FILEPRIORITY"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("PAUSE"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("STOP"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("RESUME"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("DELETE"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("OPENFILE"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("OPENFOLDER"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("PREVIEW"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("FILEINFO"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("FILECOMMENTS"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("ED2KLINK"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("CATEGORY"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("CLEARCOMPLETE"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("KadFileSearch"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("Search"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("SAVE"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("RELOAD"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("BACKUP"))) >= 0);
		VERIFY(AddIconGreyedToImageList(iml2, CTempIconLoader(_T("INSPECT"))) >= 0);
	}

	for (UINT i = 0; i < uPreviewAppCount; ++i) {
		HICON hPreviewIcon = thePreviewApps.GetPreviewAppIcon(i);
		if (hPreviewIcon == NULL)
			continue;

		VERIFY(iml.Add(hPreviewIcon) >= 0);
		if (bNeedMonoIcons)
			VERIFY(AddIconGreyedToImageList(iml2, hPreviewIcon) >= 0);
		VERIFY(::DestroyIcon(hPreviewIcon));
	}

	if (bNeedMonoIcons) {
		CImageList *pImlOld = m_btnBar.SetDisabledImageList(&iml2);
		iml2.Detach();
		if (pImlOld)
			pImlOld->DeleteImageList();
	}
	CImageList *pImlOld = m_btnBar.SetImageList(&iml);
	iml.Detach();
	if (pImlOld)
		pImlOld->DeleteImageList();
}

LRESULT CToolbarWnd::OnInitDialog(WPARAM, LPARAM)
{
	Default();
	InitWindowStyles(this);

	CRect sizeDefault;
	GetWindowRect(&sizeDefault);
	static const RECT rcBorders = { 4, 4, 4, 4 };
	SetBorders(&rcBorders);
	m_szFloat.cx = sizeDefault.Width() + rcBorders.left + rcBorders.right + ::GetSystemMetrics(SM_CXEDGE) * 2;
	m_szFloat.cy = sizeDefault.Height() + rcBorders.top + rcBorders.bottom + ::GetSystemMetrics(SM_CYEDGE) * 2;
	m_szMRU = m_szFloat;
	UpdateData(FALSE);

	m_btnBar.SetMaxTextRows(0);

	Localize();
	return TRUE;
}

#define	MIN_HORZ_WIDTH	200
#define	MIN_VERT_WIDTH	36

CSize CToolbarWnd::CalcDynamicLayout(int nLength, DWORD dwMode)
{
	CFrameWnd *pFrm = GetDockingFrame();

	// This function is typically called with

	CRect rcFrmClnt;
	pFrm->GetClientRect(&rcFrmClnt);
	CRect rcInside(rcFrmClnt);
	CalcInsideRect(rcInside, dwMode & LM_HORZDOCK);
	RECT rcBorders =
	{
		rcInside.left - rcFrmClnt.left,
		rcInside.top - rcFrmClnt.top,
		rcFrmClnt.right - rcInside.right,
		rcFrmClnt.bottom - rcInside.bottom
	};

	if (dwMode & (LM_HORZDOCK | LM_VERTDOCK)) {
		if (dwMode & LM_VERTDOCK) {
			CSize szFloat;
			szFloat.cx = MIN_VERT_WIDTH;
			szFloat.cy = rcFrmClnt.Height() + ::GetSystemMetrics(SM_CYEDGE) * 2;
			m_szFloat = szFloat;
			return szFloat;
		}
		if (dwMode & LM_HORZDOCK) {
			CSize szFloat;
			szFloat.cx = rcFrmClnt.Width() + ::GetSystemMetrics(SM_CXEDGE) * 2;
			szFloat.cy = m_sizeDefault.cy + rcBorders.top + rcBorders.bottom;
			m_szFloat = szFloat;
			return szFloat;
		}
		return CDialogBar::CalcDynamicLayout(nLength, dwMode);
	}

	if (dwMode & LM_MRUWIDTH)
		return m_szMRU;

	if (dwMode & LM_COMMIT) {
		m_szMRU = m_szFloat;
		return m_szFloat;
	}

	CSize szFloat;
	if ((dwMode & LM_LENGTHY) == 0) {
		szFloat.cx = nLength;
		if (nLength < m_sizeDefault.cx + rcBorders.left + rcBorders.right) {
			szFloat.cx = MIN_VERT_WIDTH;
			szFloat.cy = MIN_HORZ_WIDTH;
		} else
			szFloat.cy = m_sizeDefault.cy + rcBorders.top + rcBorders.bottom;
	} else {
		szFloat.cy = nLength;
		if (nLength < MIN_HORZ_WIDTH) {
			szFloat.cx = m_sizeDefault.cx + rcBorders.left + rcBorders.right;
			szFloat.cy = m_sizeDefault.cy + rcBorders.top + rcBorders.bottom;
		} else
			szFloat.cx = MIN_VERT_WIDTH;
	}

	m_szFloat = szFloat;
	return szFloat;
}

BOOL CToolbarWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (m_hcurMove && ((m_dwStyle & (CBRS_GRIPPER | CBRS_FLOATING)) == CBRS_GRIPPER) && pWnd->GetSafeHwnd() == m_hWnd) {
		CPoint ptCursor;
		if (::GetCursorPos(&ptCursor)) {
			ScreenToClient(&ptCursor);
			CRect rcClnt;
			GetClientRect(&rcClnt);
			if (rcClnt.PtInRect(ptCursor))
				if ((m_dwStyle & CBRS_ORIENT_HORZ ? ptCursor.x : ptCursor.y) <= 10) {
					::SetCursor(m_hcurMove); //mouse over the gripper
					return TRUE;
				}
		}
	}
	return CDialogBar::OnSetCursor(pWnd, nHitTest, message);
}

void CToolbarWnd::OnSize(UINT nType, int cx, int cy)
{
	CDialogBar::OnSize(nType, cx, cy);
	if (m_btnBar.m_hWnd == 0)
		return;

	if (cx >= MIN_HORZ_WIDTH) {
		CRect rcClient;
		GetClientRect(&rcClient);
		CalcInsideRect(rcClient, TRUE);
		m_btnBar.MoveWindow(rcClient.left + 1, rcClient.top, rcClient.Width() - 8, 22);
	} else if (cx < MIN_HORZ_WIDTH) {
		CRect rcClient;
		GetClientRect(&rcClient);
		CalcInsideRect(rcClient, FALSE);
		m_btnBar.MoveWindow(rcClient.left, rcClient.top + 1, 24, rcClient.Height() - 1);
	}
}

void CToolbarWnd::OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL /*bDisableIfNoHndler*/)
{
	if (m_pCommandTargetWnd != NULL && !theApp.IsClosing()) {
		CList<int> liCommands;
		if (m_pCommandTargetWnd->ReportAvailableCommands(liCommands))
			OnAvailableCommandsChanged(&liCommands);
	}
	// Disable MFC's command routing by not passing the message flow to the base class
}

void CToolbarWnd::OnDestroy()
{
	CDialogBar::OnDestroy();
}

void CToolbarWnd::OnSysColorChange()
{
	CDialogBar::OnSysColorChange();
}

void CToolbarWnd::Localize()
{
	SetWindowText(GetResString(_T("DOWNLOADCOMMANDS")));
	FillToolbar();
}

BOOL CToolbarWnd::PreTranslateMessage(MSG *pMsg)
{
	switch (pMsg->message) {
	case WM_KEYDOWN:
		if (pMsg->wParam == VK_ESCAPE)
			return false;
		break;
	case WM_RBUTTONUP:
		// We'll redirect right click event to left click event, while setting m_bRightClicked flag to true. This will help us to distinguish which action to take: 
		// a) Preview current selection (left click)
		// b) Preview next previewable selection (right click)
		theApp.emuledlg->transferwnd->GetDownloadList()->m_bRightClicked = true;
		::SendMessage(pMsg->hwnd, WM_LBUTTONDOWN, pMsg->wParam, pMsg->lParam);
		::SendMessage(pMsg->hwnd, WM_LBUTTONUP, pMsg->wParam, pMsg->lParam);
		break;
	}

	return CDialogBar::PreTranslateMessage(pMsg);
}

BOOL CToolbarWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Transfers);
	return TRUE;
}

void CToolbarWnd::OnAvailableCommandsChanged(CList<int> *liCommands)
{
	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_COMMAND | TBIF_BYINDEX | TBIF_STATE | TBIF_STYLE;

	for (int i = m_btnBar.GetButtonCount(); --i >= 0;)
		if (m_btnBar.GetButtonInfo(i, &tbbi) >= 0 && (tbbi.fsStyle & BTNS_SEP) == 0)
			m_btnBar.EnableButton(tbbi.idCommand, static_cast<BOOL>(liCommands->Find(tbbi.idCommand) != NULL));
}

BOOL CToolbarWnd::OnCommand(WPARAM wParam, LPARAM)
{
	if (LOWORD(wParam) == MP_TOGGLEDTOOLBAR) {
		theApp.emuledlg->transferwnd->ShowToolbar(false);
		thePrefs.SetDownloadToolbar(false);
	} else if (m_pCommandTargetWnd != 0)
		m_pCommandTargetWnd->SendMessage(WM_COMMAND, wParam, 0);
	return TRUE;
}

void CToolbarWnd::OnBtnDropDown(LPNMHDR pNMHDR, LRESULT *pResult)
{
	TBNOTIFY *tbn = (TBNOTIFY*)pNMHDR;
	if (tbn->iItem == MP_PRIOLOW) {
		RECT rc;
		m_btnBar.GetItemRect(m_btnBar.CommandToIndex(MP_PRIOLOW), &rc);
		m_btnBar.ClientToScreen(&rc);
		m_pCommandTargetWnd->GetPrioMenu()->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom, this);
	} else if (tbn->iItem == MP_NEWCAT) {
		RECT rc;
		m_btnBar.GetItemRect(m_btnBar.CommandToIndex(MP_NEWCAT), &rc);
		m_btnBar.ClientToScreen(&rc);
		CMenuXP menu;
		menu.CreatePopupMenu();
		m_pCommandTargetWnd->FillCatsMenu(menu);
		menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom, this);
	} else
		ASSERT(0);
	*pResult = TBDDRET_DEFAULT;
}


void CToolbarWnd::DelayShow(BOOL bShow)
{
	// Yes, it is somewhat ugly but still the best way (without partially rewriting 3 MFC classes)
	// to know if the user clicked on the Close-Button of our floating Bar
	if (!bShow && m_pDockSite != NULL && m_pDockBar != NULL) {
		if (m_pDockBar->m_bFloating) {
			CWnd *pDockFrame = m_pDockBar->GetParent();
			ASSERT(pDockFrame != NULL);
			if (pDockFrame != NULL) {
				CPoint point;
				::GetCursorPos(&point);
				LRESULT res = pDockFrame->SendMessage(WM_NCHITTEST, 0, MAKELONG(point.x, point.y));
				if (res == HTCLOSE)
					thePrefs.SetDownloadToolbar(false);
			}
		}
	}
	__super::DelayShow(bShow);
}

void CToolbarWnd::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) != SC_KEYMENU)
		__super::OnSysCommand(nID, lParam);
	else if (lParam == EMULE_HOTMENU_ACCEL)
		theApp.emuledlg->SendMessage(WM_COMMAND, IDC_HOTMENU);
	else
		theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
}

void CToolbarWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	CWnd* pChild = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT | CWP_SKIPDISABLED);
	if (pChild && pChild != this)
		CDialogBar::OnLButtonDown(nFlags, point); // allow clicks on child controls
	// ignore drag from empty area to block floating
}
