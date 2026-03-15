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
#include "eMule.h"
#include "FileInfoDialog.h"
#include "OtherFunctions.h"
#include "MediaInfo.h"
#include "eMuleAI/MediaInfoLib.h"
#include "PartFile.h"
#include "Preferences.h"
#include "UserMsgs.h"
#include "SplitterControl.h"
#include "id3/tag.h"
#include "id3/misc_support.h"
#include <locale.h>
#include "eMuleAI/DarkMode.h"

// MediaInfoLib enums are provided by eMuleAI/MediaInfoLib.h


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Use CMediaInfoLIB from eMuleAI instead of local wrapper

/////////////////////////////////////////////////////////////////////////////
// SMediaInfoThreadResult

struct SMediaInfoThreadResult
{
	~SMediaInfoThreadResult()
	{
		delete paMediaInfo;
	}
	CArray<SMediaInfo> *paMediaInfo = NULL;
	CStringA strInfo;
};

/////////////////////////////////////////////////////////////////////////////
// CGetMediaInfoThread

class CGetMediaInfoThread : public CWinThread
{
	DECLARE_DYNCREATE(CGetMediaInfoThread)

protected:
	CGetMediaInfoThread()
		: m_hWndOwner()
		, m_hFont()
		, m_handle()
	{
	}

public:
	virtual BOOL InitInstance();
	virtual int	Run();
	void SetValues(HWND hWnd, const CSimpleArray<CObject*> *paFiles, HFONT hFont)
	{
		m_hWndOwner = hWnd;
		m_aFiles.RemoveAll();
		for (int i = 0; i < paFiles->GetSize(); ++i) {
			const CShareableFile *pFile = static_cast<CShareableFile*>((*paFiles)[i]);
			if (pFile == NULL)
				continue;
			SFileContext ctx;
			ctx.strFilePath = pFile->GetFilePath();
			ctx.strFileName = pFile->GetFileName();
			ctx.uFileSize = pFile->GetFileSize();
			ctx.bIsPartFile = pFile->IsPartFile();
			ctx.bHasMimeData = true;
			ctx.bHasHeaderData = true;
			if (ctx.bIsPartFile) {
				const CPartFile *pPart = static_cast<const CPartFile*>(pFile);
				if (pPart != NULL) {
					ctx.bHasMimeData = pPart->IsCompleteBDSafe(0, 1024);
					ctx.bHasHeaderData = pPart->IsCompleteSafe(0, 16 * 1024);
				} else {
					ctx.bHasMimeData = false;
					ctx.bHasHeaderData = false;
				}
			}
			m_aFiles.Add(ctx);
		}
		m_hFont = hFont;
	}

private:
	struct SFileContext
	{
		CString strFilePath;
		CString strFileName;
		EMFileSize uFileSize;
		bool bIsPartFile;
		bool bHasMimeData;
		bool bHasHeaderData;
	};

	bool GetMediaInfo(HWND hWndOwner, const SFileContext &fileCtx, SMediaInfo *mi, bool bSingleFile);
	void WarnAboutWrongFileExtension(SMediaInfo *mi, LPCTSTR pszFileName, LPCTSTR pszExtensions);

	CString InfoGet(MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter)
	{
		return m_miLib.InfoGet(StreamKind, StreamNumber, Parameter);
	}
	CString InfoGetI(MediaInfo_stream_C StreamKind, int StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo)
	{
		return m_miLib.InfoGetI(StreamKind, StreamNumber, Parameter, KindOfInfo);
	}
	CSimpleArray<SFileContext> m_aFiles;
	HWND m_hWndOwner;
	HFONT m_hFont;
	HANDLE m_handle;
	CMediaInfoLIB m_miLib;
};


/////////////////////////////////////////////////////////////////////////////
// CFileInfoDialog dialog

IMPLEMENT_DYNAMIC(CFileInfoDialog, CResizablePage)

BEGIN_MESSAGE_MAP(CFileInfoDialog, CResizablePage)
	ON_MESSAGE(UM_MEDIA_INFO_RESULT, OnMediaInfoResult)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CFileInfoDialog::CFileInfoDialog()
	: CResizablePage(CFileInfoDialog::IDD)
	, m_paFiles()
	, m_bDataChanged()
	, m_bReducedDlg()
{
	m_strCaption = GetResString(_T("CONTENT_INFO"));
	m_psp.pszTitle = m_strCaption;
	m_psp.dwFlags |= PSP_USETITLE;
}

BOOL CFileInfoDialog::OnInitDialog()
{
	CWaitCursor curWait; // we may get quite busy here.
	ReplaceRichEditCtrl(GetDlgItem(IDC_FULL_FILE_INFO), this, GetDlgItem(IDC_FD_XI1)->GetFont());
	CResizablePage::OnInitDialog();
	InitWindowStyles(this);

	if (!m_bReducedDlg) {
		AddOrReplaceAnchor(this, IDC_FILESIZE, TOP_LEFT, TOP_RIGHT);
		AddOrReplaceAnchor(this, IDC_FULL_FILE_INFO, TOP_LEFT, BOTTOM_RIGHT);

		m_fi.LimitText(_I32_MAX);
		m_fi.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		m_fi.SetAutoURLDetect();
		m_fi.SetEventMask(m_fi.GetEventMask() | ENM_LINK);
	} else {
		GetDlgItem(IDC_FILESIZE)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_FULL_FILE_INFO)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_FD_XI1)->ShowWindow(SW_HIDE);

		CRect rc;
		GetDlgItem(IDC_FILESIZE)->GetWindowRect(rc);
		int nDelta = rc.Height();

		CSplitterControl::ChangeHeight(GetDlgItem(IDC_GENERAL), -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_LENGTH), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FORMAT), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI3), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VCODEC), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VBITRATE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VWIDTH), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VASPECT), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VFPS), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI6), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI8), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI10), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI12), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_STATIC_LANGUAGE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI4), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ACODEC), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ABITRATE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ACHANNEL), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ASAMPLERATE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ALANGUAGE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI5), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI9), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI7), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI14), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI13), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI2), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_STATICFI), 0, -nDelta);
	}

	// General Group
	AddOrReplaceAnchor(this, IDC_GENERAL, TOP_LEFT, TOP_RIGHT);

	// Video Group
	AddOrReplaceAnchor(this, IDC_FD_XI3, TOP_LEFT, TOP_CENTER);

	// Audio Group - Labels
	AddOrReplaceAnchor(this, IDC_FD_XI4, TOP_CENTER, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_FD_XI6, TOP_CENTER, TOP_CENTER);
	AddOrReplaceAnchor(this, IDC_FD_XI8, TOP_CENTER, TOP_CENTER);
	AddOrReplaceAnchor(this, IDC_FD_XI10, TOP_CENTER, TOP_CENTER);
	AddOrReplaceAnchor(this, IDC_FD_XI12, TOP_CENTER, TOP_CENTER);
	AddOrReplaceAnchor(this, IDC_STATIC_LANGUAGE, TOP_CENTER, TOP_CENTER);

	// Audio Group - Values
	AddOrReplaceAnchor(this, IDC_ACODEC, TOP_CENTER, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_ABITRATE, TOP_CENTER, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_ACHANNEL, TOP_CENTER, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_ASAMPLERATE, TOP_CENTER, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_ALANGUAGE, TOP_CENTER, TOP_RIGHT);

	AddAllOtherAnchors();

	CResizablePage::UpdateData(FALSE);
	Localize();
	return TRUE;
}

void CFileInfoDialog::OnDestroy()
{
	// This property sheet's window may get destroyed and re-created several times although
	// the corresponding C++ class is kept -> explicitly reset ResizableLib state
	RemoveAllAnchors();

	CResizablePage::OnDestroy();
}

BOOL CFileInfoDialog::OnSetActive()
{
	if (!CResizablePage::OnSetActive())
		return FALSE;
	if (m_bDataChanged) {
		InitDisplay(GetResString(_T("FSTAT_WAITING")));

		CGetMediaInfoThread *pThread = (CGetMediaInfoThread*)AfxBeginThread(RUNTIME_CLASS(CGetMediaInfoThread), THREAD_PRIORITY_LOWEST, 0, CREATE_SUSPENDED);
		if (pThread) {
			pThread->SetValues(m_hWnd, m_paFiles, (HFONT)GetDlgItem(IDC_FD_XI1)->GetFont()->m_hObject);
			pThread->ResumeThread();
		}
		m_pFiles.RemoveAll();
		for (int i = m_paFiles->GetSize(); --i >= 0;)
			m_pFiles.Add((*m_paFiles)[i]);
		m_bDataChanged = false;
	}
	return TRUE;
}

LRESULT CFileInfoDialog::OnDataChanged(WPARAM, LPARAM)
{
	m_bDataChanged = true;
	return 1;
}

IMPLEMENT_DYNCREATE(CGetMediaInfoThread, CWinThread)

BOOL CGetMediaInfoThread::InitInstance()
{
	DbgSetThreadName("GetMediaInfo");
	return TRUE;
}

int CGetMediaInfoThread::Run()
{
	(void)CoInitialize(NULL);

	HWND hwndRE = ::CreateWindow(RICHEDIT_CLASS, NULL, ES_MULTILINE | ES_READONLY | WS_DISABLED, 0, 0, 200, 200, NULL, NULL, NULL, NULL);
	ASSERT(hwndRE);
	if (hwndRE && m_hFont)
		::SendMessage(hwndRE, WM_SETFONT, (WPARAM)m_hFont, 0);

	CArray<SMediaInfo> *paMediaInfo = NULL;
	try {
		CRichEditStream re;
		re.Attach(hwndRE);
		re.LimitText(_I32_MAX);
		PARAFORMAT pf;
		pf.cbSize = (UINT)sizeof pf;
		if (re.GetParaFormat(pf)) {
			pf.dwMask |= PFM_TABSTOPS;
			pf.cTabCount = 1;
			pf.rgxTabs[0] = 3000;
			re.SetParaFormat(pf);
		}
		re.Detach();

		const int arcnt = m_aFiles.GetSize();
		paMediaInfo = new CArray<SMediaInfo>;
		for (int i = 0; i < arcnt; ++i) {
			SMediaInfo mi;
			mi.bOutputFileName = arcnt > 1;
			mi.strFileName = m_aFiles[i].strFileName;
			mi.strInfo.Attach(hwndRE);
			mi.strInfo.InitColors();
			if (!::IsWindow(m_hWndOwner) || !GetMediaInfo(m_hWndOwner, m_aFiles[i], &mi, (arcnt == 1))) {
				mi.strInfo.Detach();
				paMediaInfo->RemoveAll();
				delete paMediaInfo;
				paMediaInfo = NULL;
				break;
			}
			mi.strInfo.Detach();
			paMediaInfo->Add(mi);
		}
	} catch (...) {
		ASSERT(0);
		if (paMediaInfo != NULL) {
			paMediaInfo->RemoveAll();
			delete paMediaInfo;
			paMediaInfo = NULL;
		}
	}

	SMediaInfoThreadResult *pThreadRes = new SMediaInfoThreadResult;
	pThreadRes->paMediaInfo = paMediaInfo;
	CRichEditStream re;
	re.Attach(hwndRE);
	re.GetRTFText(pThreadRes->strInfo);
	re.Detach();
	VERIFY(DestroyWindow(hwndRE));

	// Usage of 'PostMessage': The idea is to post a message to the window in that other
	// thread and never deadlock (because of the post). This is safe, but leads to the problem
	// that we may create memory leaks in case the target window is currently in the process
	// of getting destroyed! E.g. if the target window gets destroyed after we put the message
	// into the queue, we have no chance of determining that and the memory wouldn't get freed.
	//	delete pThreadRes;

	// Usage of 'SendMessage': Using 'SendMessage' seems to be dangerous because of potential
	// deadlocks. Basically it depends on what the target thread/window is currently doing
	// whether there is a risk for a deadlock. However, even with extensive stress testing
	// there does not show any problem. The worse thing which can happen, is that we call
	// 'SendMessage', then the target window gets destroyed (while we are still waiting in
	// 'SendMessage') and would get blocked. Though, this does not happen, it seems that Windows
	// is catching that case internally and lets our 'SendMessage' call return (with a result
	// of '0'). If that happened, the 'IsWindow(m_hWndOwner)' returns FALSE, which positively
	// indicates that the target window was destroyed while we were waiting in 'SendMessage'.
	// So, everything should be fine (with that special scenario) with using 'SendMessage'.
	// Let's be brave. :)
	if (!::IsWindow(m_hWndOwner) || !::SendMessage(m_hWndOwner, UM_MEDIA_INFO_RESULT, 0, (LPARAM)pThreadRes))
		delete pThreadRes;

	CoUninitialize();
	return 0;
}

void CFileInfoDialog::InitDisplay(LPCTSTR pStr)
{
	SetDlgItemText(IDC_FORMAT, pStr);
	SetDlgItemText(IDC_FILESIZE, pStr);
	SetDlgItemText(IDC_LENGTH, pStr);
	SetDlgItemText(IDC_VCODEC, pStr);
	SetDlgItemText(IDC_VBITRATE, pStr);
	SetDlgItemText(IDC_VWIDTH, pStr);
	SetDlgItemText(IDC_VASPECT, pStr);
	SetDlgItemText(IDC_VFPS, pStr);
	SetDlgItemText(IDC_ACODEC, pStr);
	SetDlgItemText(IDC_ACHANNEL, pStr);
	SetDlgItemText(IDC_ASAMPLERATE, pStr);
	SetDlgItemText(IDC_ABITRATE, pStr);
	SetDlgItemText(IDC_ALANGUAGE, pStr);
	SetDlgItemText(IDC_FULL_FILE_INFO, EMPTY);

}

LRESULT CFileInfoDialog::OnMediaInfoResult(WPARAM, LPARAM lParam)
{
	SetDlgItemText(IDC_FD_XI3, GetResString(_T("VIDEO")));
	SetDlgItemText(IDC_FD_XI4, GetResString(_T("AUDIO")));
	InitDisplay(_T("-"));

	SMediaInfoThreadResult *pThreadRes = (SMediaInfoThreadResult*)lParam;
	if (pThreadRes == NULL)
		return 1;
	CArray<SMediaInfo> *paMediaInfo = pThreadRes->paMediaInfo;
	if (paMediaInfo == NULL) {
		delete pThreadRes;
		return 1;
	}

	if (paMediaInfo->GetSize() != m_paFiles->GetSize()) {
		InitDisplay(EMPTY);
		delete pThreadRes;
		return 1;
	}

	uint64 uTotalFileSize = 0;
	SMediaInfo ami;
	bool bDiffVideoStreamCount = false;
	bool bDiffVideoCompression = false;
	bool bDiffVideoWidth = false;
	bool bDiffVideoHeight = false;
	bool bDiffVideoFrameRate = false;
	bool bDiffVideoBitRate = false;
	bool bDiffVideoAspectRatio = false;
	bool bDiffAudioStreamCount = false;
	bool bDiffAudioCompression = false;
	bool bDiffAudioChannels = false;
	bool bDiffAudioSamplesPerSec = false;
	bool bDiffAudioAvgBytesPerSec = false;
	bool bDiffAudioLanguage = false;
	for (int i = 0; i < paMediaInfo->GetSize(); ++i) {
		SMediaInfo &mi = (*paMediaInfo)[i];

		mi.InitFileLength();
		uTotalFileSize += (uint64)mi.ulFileSize;
		if (i == 0)
			ami = mi;
		else {
			if (ami.strFileFormat != mi.strFileFormat)
				ami.strFileFormat.Empty();

			if (ami.strMimeType != mi.strMimeType)
				ami.strMimeType.Empty();

			ami.fFileLengthSec += mi.fFileLengthSec;
			ami.bFileLengthEstimated |= mi.bFileLengthEstimated;

			ami.fVideoLengthSec += mi.fVideoLengthSec;
			ami.bVideoLengthEstimated |= mi.bVideoLengthEstimated;
			if (ami.iVideoStreams == 0 && mi.iVideoStreams > 0 || ami.iVideoStreams > 0 && mi.iVideoStreams == 0) {
				if (ami.iVideoStreams == 0)
					ami.iVideoStreams = mi.iVideoStreams;
				bDiffVideoStreamCount = true;
				bDiffVideoCompression = true;
				bDiffVideoWidth = true;
				bDiffVideoHeight = true;
				bDiffVideoFrameRate = true;
				bDiffVideoBitRate = true;
				bDiffVideoAspectRatio = true;
			} else {
				bDiffVideoStreamCount |= (ami.iVideoStreams != mi.iVideoStreams);
				bDiffVideoCompression |= (ami.strVideoFormat != mi.strVideoFormat);
				bDiffVideoWidth |= (ami.video.bmiHeader.biWidth != mi.video.bmiHeader.biWidth);
				bDiffVideoHeight |= (ami.video.bmiHeader.biHeight != mi.video.bmiHeader.biHeight);
				bDiffVideoFrameRate |= (ami.fVideoFrameRate != mi.fVideoFrameRate);
				bDiffVideoBitRate |= (ami.video.dwBitRate != mi.video.dwBitRate);
				bDiffVideoAspectRatio |= (ami.fVideoAspectRatio != mi.fVideoAspectRatio);
			}

			ami.fAudioLengthSec += mi.fAudioLengthSec;
			ami.bAudioLengthEstimated |= mi.bAudioLengthEstimated;
			if (ami.iAudioStreams == 0 && mi.iAudioStreams > 0 || ami.iAudioStreams > 0 && mi.iAudioStreams == 0) {
				if (ami.iAudioStreams == 0)
					ami.iAudioStreams = mi.iAudioStreams;
				bDiffAudioStreamCount = true;
				bDiffAudioCompression = true;
				bDiffAudioChannels = true;
				bDiffAudioSamplesPerSec = true;
				bDiffAudioAvgBytesPerSec = true;
				bDiffAudioLanguage = true;
			} else {
				bDiffAudioStreamCount |= (ami.iAudioStreams != mi.iAudioStreams);
				bDiffAudioCompression |= (ami.strAudioFormat != mi.strAudioFormat);
				bDiffAudioChannels |= (ami.audio.nChannels != mi.audio.nChannels);
				bDiffAudioSamplesPerSec |= (ami.audio.nSamplesPerSec != mi.audio.nSamplesPerSec);
				bDiffAudioAvgBytesPerSec |= (ami.audio.nAvgBytesPerSec != mi.audio.nAvgBytesPerSec);
				bDiffAudioLanguage |= (ami.strAudioLanguage.CompareNoCase(mi.strAudioLanguage) != 0);
			}
		}
	}

	CString buffer(ami.strFileFormat);
	if (!ami.strMimeType.IsEmpty()) {
		if (!buffer.IsEmpty())
			buffer += _T("; MIME type=");
		buffer += ami.strMimeType;
	}
	SetDlgItemText(IDC_FORMAT, buffer);

	if (uTotalFileSize)
		SetDlgItemText(IDC_FILESIZE, CastItoXBytes(uTotalFileSize));

	if (ami.fFileLengthSec) {
		buffer = CastSecondsToHM((time_t)ami.fFileLengthSec);
		if (ami.bFileLengthEstimated)
			buffer.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("ESTIMATED")));
		SetDlgItemText(IDC_LENGTH, buffer);
	}

	if (ami.iVideoStreams) {
		if (!bDiffVideoStreamCount && ami.iVideoStreams > 1)
			SetDlgItemText(IDC_FD_XI3, GetResString(_T("VIDEO")) + _T(" #1"));

		SetDlgItemText(IDC_VCODEC, bDiffVideoCompression ? EMPTY : ami.strVideoFormat);

		if (!bDiffVideoBitRate && ami.video.dwBitRate) {
			if (ami.video.dwBitRate == _UI32_MAX)
				buffer = _T("Variable");
			else
				buffer.Format(_T("%lu %s"), (ami.video.dwBitRate + SEC2MS(1) / 2) / SEC2MS(1), (LPCTSTR)GetResString(_T("KBITSSEC")));
			SetDlgItemText(IDC_VBITRATE, buffer);
		} else
			SetDlgItemText(IDC_VBITRATE, EMPTY);

		if (!bDiffVideoWidth && ami.video.bmiHeader.biWidth && !bDiffVideoHeight && ami.video.bmiHeader.biHeight) {
			buffer.Format(_T("%i x %i"), abs(ami.video.bmiHeader.biWidth), abs(ami.video.bmiHeader.biHeight));
			SetDlgItemText(IDC_VWIDTH, buffer);
		} else
			SetDlgItemText(IDC_VWIDTH, EMPTY);

		if (!bDiffVideoAspectRatio && ami.fVideoAspectRatio) {
			buffer.Format(_T("%.3f"), ami.fVideoAspectRatio);
			const CString &strAR(GetKnownAspectRatioDisplayString((float)ami.fVideoAspectRatio));
			if (!strAR.IsEmpty())
				buffer.AppendFormat(_T("  (%s)"), (LPCTSTR)strAR);
			SetDlgItemText(IDC_VASPECT, buffer);
		} else
			SetDlgItemText(IDC_VASPECT, EMPTY);

		if (!bDiffVideoFrameRate && ami.fVideoFrameRate) {
			buffer.Format(_T("%.2f"), ami.fVideoFrameRate);
			SetDlgItemText(IDC_VFPS, buffer);
		} else
			SetDlgItemText(IDC_VFPS, EMPTY);
	}

	if (ami.iAudioStreams) {
		if (!bDiffAudioStreamCount && ami.iAudioStreams > 1)
			SetDlgItemText(IDC_FD_XI4, GetResString(_T("AUDIO")) + _T(" #1"));

		SetDlgItemText(IDC_ACODEC, bDiffAudioCompression ? EMPTY : ami.strAudioFormat);

		LPCTSTR pChan;
		if (!bDiffAudioChannels && ami.audio.nChannels) {
			switch (ami.audio.nChannels) {
			case 1:
				pChan = _T("1 (Mono)");
				break;
			case 2:
				pChan = _T("2 (Stereo)");
				break;
			case 5:
				pChan = _T("5.1 (Surround)");
				break;
			default:
				pChan = NULL;
				SetDlgItemInt(IDC_ACHANNEL, ami.audio.nChannels, FALSE);
			}
		} else
			pChan = EMPTY;
		if (pChan)
			SetDlgItemText(IDC_ACHANNEL, pChan);

		if (!bDiffAudioSamplesPerSec && ami.audio.nSamplesPerSec) {
			buffer.Format(_T("%.3f kHz"), ami.audio.nSamplesPerSec / 1000.0);
			SetDlgItemText(IDC_ASAMPLERATE, buffer);
		} else
			SetDlgItemText(IDC_ASAMPLERATE, EMPTY);

		if (!bDiffAudioAvgBytesPerSec && ami.audio.nAvgBytesPerSec) {
			if (ami.audio.nAvgBytesPerSec == _UI32_MAX)
				buffer = _T("Variable");
			else
				buffer.Format(_T("%u %s"), (UINT)((ami.audio.nAvgBytesPerSec * 16ull + SEC2MS(1)) / SEC2MS(2)), (LPCTSTR)GetResString(_T("KBITSSEC")));
			SetDlgItemText(IDC_ABITRATE, buffer);
		} else
			SetDlgItemText(IDC_ABITRATE, EMPTY);

		SetDlgItemText(IDC_ALANGUAGE, bDiffAudioLanguage ? EMPTY : ami.strAudioLanguage);
	}

	if (!m_bReducedDlg) {
		m_fi.SetRTFText(pThreadRes->strInfo);
		DisableAutoSelect(m_fi);
		m_fi.UpdateColors();
	}

	delete pThreadRes;
	return 1;
}

void CGetMediaInfoThread::WarnAboutWrongFileExtension(SMediaInfo *mi, LPCTSTR pszFileName, LPCTSTR pszExtensions)
{
	if (!mi->strInfo.IsEmpty())
		mi->strInfo << _T("\r\n");
	mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
	mi->strInfo.AppendFormat(GetResString(_T("WARNING_WRONGFILEEXTENTION")), pszFileName, pszExtensions);
	mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
}

//TODO Verify if next two functions are still broken in the latest id3lib codes

wchar_t* ID3_GetStringW(const ID3_Frame *frame, ID3_FieldID fldName)
{
	// ID3LIB BUG: Use the Unicode support of id3lib only if the tag is already
	// in Unicode format, thus avoiding couple of bugs with character conversion in
	// id3lib to get triggered.
	if (frame) {
		ID3_Field *fld = frame->GetField(fldName);
		if (fld	&& fld->GetEncoding() == ID3TE_UTF16) {
			unicode_t *text = NULL;
			size_t nText = fld->Size();
			text = new unicode_t[nText / sizeof(unicode_t) + 1];
			fld->Get(text, nText / sizeof(unicode_t));
			text[nText / sizeof(unicode_t)] = L'\0';
			for (unsigned int i = 0; i < nText / sizeof(unicode_t); ++i)
				text[i] = _byteswap_ushort(text[i]);
			return (wchar_t*)text;
		}
	}

	char *text = ID3_GetString(frame, fldName);
	CStringW wstr(text);
	delete[] text;
	wchar_t *pwsz = new wchar_t[wstr.GetLength() + 1];
	wcscpy(pwsz, wstr);
	return pwsz;
}

wchar_t* ID3_GetStringW(const ID3_Frame *frame, ID3_FieldID fldName, size_t nIndex)
{
	// Do not use 'ID3_FieldImpl::Get(unicode_t *buffer, size_t maxLength, size_t itemNum)'.
	// That function is broken in id3lib (the bug is in 'GetRawUnicodeTextItem')
	return (nIndex == 0) ? ID3_GetStringW(frame, fldName) : NULL;
}

bool CGetMediaInfoThread::GetMediaInfo(HWND hWndOwner, const SFileContext &fileCtx, SMediaInfo *mi, bool bSingleFile)
{
	const CString &strFilePath = fileCtx.strFilePath;
	if (strFilePath.IsEmpty())
		return false;
	ASSERT(!strFilePath.IsEmpty());

	const CString &strFileName = fileCtx.strFileName;
	const EMFileSize uFileSize = fileCtx.uFileSize;
	const bool bIsPartFile = fileCtx.bIsPartFile;

	bool bHasDRM = false;
	if (!bIsPartFile || fileCtx.bHasMimeData) {
		GetMimeType(strFilePath, mi->strMimeType);
		bHasDRM = GetDRM(strFilePath);
		if (bHasDRM) {
			if (!mi->strInfo.IsEmpty())
				mi->strInfo << _T("\r\n");
			mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
			mi->strInfo.AppendFormat(GetResString(_T("MEDIAINFO_DRMWARNING")), (LPCTSTR)strFileName);
			mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
		}
	}

	mi->ulFileSize = uFileSize;

	bool bFoundHeader = false;
	if (bIsPartFile) {
		// Do *not* pass a part file which does not have the beginning of file to the following code.
		//	- The MP3 reading code will skip all 0-bytes from the beginning of the file and may block
		//	  the main thread for a long time.
		//
		//	- The RIFF reading code will not work without the file header.
		//
		//	- Most (if not all) other code also will not work without the beginning of the file available.
		if (!fileCtx.bHasHeaderData)
			return bFoundHeader || !mi->strMimeType.IsEmpty();
	}

	CString szExt(::PathFindExtension(strFileName));
	szExt.MakeLower();

	////////////////////////////////////////////////////////////////////////////
	// Check for AVI file
	//
	bool bIsAVI = false;
	try {
		if (GetRIFFHeaders(strFilePath, mi, bIsAVI, false)) {
			if (bIsAVI && szExt != _T(".avi"))
				WarnAboutWrongFileExtension(mi, strFileName, _T("avi"));
			return true;
		}
	} catch (...) {
		ASSERT(0);
	}

	if (!::IsWindow(hWndOwner))
		return false;

	////////////////////////////////////////////////////////////////////////////
	// Check for RM file
	//
	try {
		bool bIsRM = false;
		if (GetRMHeaders(strFilePath, mi, bIsRM, true)) {
			if (bIsRM && szExt != _T(".rm") && szExt != _T(".rmvb") && szExt != _T(".ra"))
				WarnAboutWrongFileExtension(mi, strFileName, _T("rm rmvb ra"));
			return true;
		}
	} catch (...) {
		ASSERT(0);
	}

	if (!::IsWindow(hWndOwner))
		return false;

	////////////////////////////////////////////////////////////////////////////
	// Check for WM file
	//
#ifdef HAVE_WMSDK_H
	try {
		bool bIsWM = false;
		if (GetWMHeaders(strFilePath, mi, bIsWM, true)) {
			if (bIsWM
				&& szExt != _T(".asf")
				&& szExt != _T(".wm")
				&& szExt != _T(".wma")
				&& szExt != _T(".wmv")
				&& szExt != _T(".dvr-ms"))
			{
				WarnAboutWrongFileExtension(mi, strFileName, _T("asf wm wma wmv dvr-ms"));
			}
			return true;
		}
	} catch (...) {
		ASSERT(0);
	}

	if (!::IsWindow(hWndOwner))
		return false;
#endif//HAVE_WMSDK_H

	////////////////////////////////////////////////////////////////////////////
	// Check for MPEG Audio file
	//
	try {
		// ID3LIB BUG: If there are ID3v2 _and_ ID3v1 tags available, id3lib
		// destroys (actually corrupts) the Unicode strings from ID3v2 tags due to
		// converting Unicode to ASCII and then conversion back from ASCII to Unicode.
		// To prevent this, we force the reading of ID3v2 tags only, in case there are
		// also ID3v1 tags available.
	ID3_Tag myTag;
	CStringA strFilePathA(strFilePath);
		size_t id3Size = myTag.Link(strFilePathA, ID3TT_ID3V2);
		if (id3Size == 0) {
			myTag.Clear();
			myTag.Link(strFilePathA, ID3TT_ID3V1);
		}

		const Mp3_Headerinfo *mp3info;
		mp3info = myTag.GetMp3HeaderInfo();
		if (mp3info) {
			mi->strFileFormat = _T("MPEG audio");

			mi->OutputFileName();
			if (!bSingleFile) {
				mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
				mi->strInfo << _T("MP3 Header Info\n");
			}

			switch (mp3info->version) {
			case MPEGVERSION_2_5:
				mi->strAudioFormat = _T("MPEG-2.5,");
				mi->audio.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
				break;
			case MPEGVERSION_2:
				mi->strAudioFormat = _T("MPEG-2,");
				mi->audio.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
				break;
			case MPEGVERSION_1:
				mi->strAudioFormat = _T("MPEG-1,");
				mi->audio.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
			default:
				break;
			}
			mi->strAudioFormat += _T(' ');

			switch (mp3info->layer) {
			case MPEGLAYER_III:
				mi->strAudioFormat += _T("Layer 3");
				break;
			case MPEGLAYER_II:
				mi->strAudioFormat += _T("Layer 2");
				break;
			case MPEGLAYER_I:
				mi->strAudioFormat += _T("Layer 1");
			default:
				break;
			}
			if (!bSingleFile) {
				mi->strInfo << _T("   ") << GetResString(_T("CODEC")) << _T(":\t") << mi->strAudioFormat << _T("\n");
				//no vbr bit rate
				mi->strInfo << _T("   ") << GetResString(_T("BITRATE")) << _T(":\t") << (mp3info->bitrate + SEC2MS(1) / 2) / SEC2MS(1) << _T(" ") << GetResString(_T("KBITSSEC")) << _T("\n");
				mi->strInfo << _T("   ") << GetResString(_T("SAMPLERATE")) << _T(":\t") << mp3info->frequency / SEC2MS(1.0) << _T(" kHz\n");
			}

			++mi->iAudioStreams;
			//no vbr bit rate
			mi->audio.nAvgBytesPerSec = mp3info->bitrate / 8;
			mi->audio.nSamplesPerSec = mp3info->frequency;

			if (!bSingleFile)
				mi->strInfo << _T("   Mode:\t");
			switch (mp3info->channelmode) {
			case MP3CHANNELMODE_STEREO:
				if (!bSingleFile)
					mi->strInfo << _T("Stereo");
				mi->audio.nChannels = 2;
				break;
			case MP3CHANNELMODE_JOINT_STEREO:
				if (!bSingleFile)
					mi->strInfo << _T("Joint Stereo");
				mi->audio.nChannels = 2;
				break;
			case MP3CHANNELMODE_DUAL_CHANNEL:
				if (!bSingleFile)
					mi->strInfo << _T("Dual Channel");
				mi->audio.nChannels = 2;
				break;
			case MP3CHANNELMODE_SINGLE_CHANNEL:
				if (!bSingleFile)
					mi->strInfo << _T("Mono");
				mi->audio.nChannels = 1;
			}
			if (!bSingleFile)
				mi->strInfo << _T("\n");

			// length
			if (mp3info->time) {
				if (!bSingleFile) {
					mi->strInfo << _T("   ") << GetResString(_T("LENGTH")) << _T(":\t") << SecToTimeLength(mp3info->time);
				if (bIsPartFile) {
						mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
						mi->strInfo << _T(" (") << GetResString(_T("ESTIMATED")) << _T(")");
						mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
					}
					mi->strInfo << _T("\n");
				}
				mi->fAudioLengthSec = mp3info->time;
				mi->bAudioLengthEstimated |= bIsPartFile;
			}

			bFoundHeader = true;
			if (mi->strMimeType.IsEmpty())
				mi->strMimeType = _T("audio/mpeg");
		}

		int iTag = 0;
		ID3_Tag::Iterator *iter = myTag.CreateIterator();
		for (const ID3_Frame *frame; (frame = iter->GetNext()) != NULL;) {
			if (iTag == 0) {
				if (mp3info && !bSingleFile)
					mi->strInfo << _T("\n");
				mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
				mi->strInfo << _T("MP3 Tags\n");
			}
			++iTag;

			LPCSTR desc = frame->GetDescription();
			if (!desc)
				desc = frame->GetTextID();

			CStringStream strFidInfo;
			ID3_FrameID eFrameID = frame->GetID();
			switch (eFrameID) {
			case ID3FID_ALBUM:
			case ID3FID_COMPOSER:
			case ID3FID_CONTENTTYPE:
			case ID3FID_COPYRIGHT:
			case ID3FID_DATE:
			case ID3FID_PLAYLISTDELAY:
			case ID3FID_ENCODEDBY:
			case ID3FID_LYRICIST:
			case ID3FID_FILETYPE:
			case ID3FID_TIME:
			case ID3FID_CONTENTGROUP:
			case ID3FID_TITLE:
			case ID3FID_SUBTITLE:
			case ID3FID_INITIALKEY:
			case ID3FID_LANGUAGE:
			case ID3FID_MEDIATYPE:
			case ID3FID_ORIGALBUM:
			case ID3FID_ORIGFILENAME:
			case ID3FID_ORIGLYRICIST:
			case ID3FID_ORIGARTIST:
			case ID3FID_ORIGYEAR:
			case ID3FID_FILEOWNER:
			case ID3FID_LEADARTIST:
			case ID3FID_BAND:
			case ID3FID_CONDUCTOR:
			case ID3FID_MIXARTIST:
			case ID3FID_PARTINSET:
			case ID3FID_PUBLISHER:
			case ID3FID_TRACKNUM:
			case ID3FID_RECORDINGDATES:
			case ID3FID_NETRADIOSTATION:
			case ID3FID_NETRADIOOWNER:
			case ID3FID_SIZE:
			case ID3FID_ISRC:
			case ID3FID_ENCODERSETTINGS:
			case ID3FID_YEAR:
				{
					wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
					strFidInfo << CString(sText).Trim();
					delete[] sText;
				}
				break;
			case ID3FID_BPM:
				{
					wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
					long lLength = _wtol(sText);
					if (lLength > 0) // check for != "0"
						strFidInfo << sText;
					delete[] sText;
				}
				break;
			case ID3FID_SONGLEN:
				{
					wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
					long lLength = _wtol(sText) / 1000;
					if (lLength > 0)
						strFidInfo << SecToTimeLength(lLength);
					delete[] sText;
				}
				break;
			case ID3FID_USERTEXT:
				{
					wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
					CString strText(sText);
					delete[] sText;
					if (!strText.Trim().IsEmpty()) {
						wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
						CString strDesc(sDesc);
						delete[] sDesc;
						if (!strDesc.Trim().IsEmpty())
							strFidInfo << _T("(") << strDesc << _T("): ");

						strFidInfo << strText;
					}
				}
				break;
			case ID3FID_COMMENT:
			case ID3FID_UNSYNCEDLYRICS:
				{
					wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
					CString strText(sText);
					delete[] sText;
					if (!strText.Trim().IsEmpty()) {
						wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
						CString strDesc(sDesc);
						delete[] sDesc;
						if (strDesc.Trim() == _T("ID3v1 Comment"))
							strDesc.Empty();
						else if (!strDesc.IsEmpty())
							strFidInfo << _T("(") << strDesc << _T(")");

						wchar_t *sLang = ID3_GetStringW(frame, ID3FN_LANGUAGE);
						CString strLang(sLang);
						delete[] sLang;
						if (!strLang.Trim().IsEmpty())
							strFidInfo << _T("[") << strLang << _T("]");

						if (!strDesc.IsEmpty() || !strLang.IsEmpty())
							strFidInfo << _T(": ");
						strFidInfo << strText;
					}
				}
				break;
			case ID3FID_WWWAUDIOFILE:
			case ID3FID_WWWARTIST:
			case ID3FID_WWWAUDIOSOURCE:
			case ID3FID_WWWCOMMERCIALINFO:
			case ID3FID_WWWCOPYRIGHT:
			case ID3FID_WWWPUBLISHER:
			case ID3FID_WWWPAYMENT:
			case ID3FID_WWWRADIOPAGE:
				{
					wchar_t *sURL = ID3_GetStringW(frame, ID3FN_URL);
					strFidInfo << CString(sURL).Trim();
					delete[] sURL;
				}
				break;
			case ID3FID_WWWUSER:
				{
					wchar_t *sURL = ID3_GetStringW(frame, ID3FN_URL);
					CString strURL(sURL);
					delete[] sURL;
					if (!strURL.Trim().IsEmpty()) {
						wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
						CString strDesc(sDesc);
						delete[] sDesc;
						if (!strDesc.Trim().IsEmpty())
							strFidInfo << _T("(") << strDesc << _T("): ");

						strFidInfo << strURL;
					}
				}
				break;
			case ID3FID_INVOLVEDPEOPLE:
				{
					size_t nItems = frame->GetField(ID3FN_TEXT)->GetNumTextItems();
					for (size_t nIndex = 0; nIndex < nItems; ++nIndex) {
						wchar_t *sPeople = ID3_GetStringW(frame, ID3FN_TEXT, nIndex);
						strFidInfo << sPeople;
						delete[] sPeople;
						if (nIndex + 1 < nItems)
							strFidInfo << _T(", ");
					}
				}
				break;
			case ID3FID_PICTURE:
				{
					wchar_t *sMimeType = ID3_GetStringW(frame, ID3FN_MIMETYPE);
					wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
					wchar_t *sFormat = ID3_GetStringW(frame, ID3FN_IMAGEFORMAT);
					size_t nPicType = frame->GetField(ID3FN_PICTURETYPE)->Get();
					size_t nDataSize = frame->GetField(ID3FN_DATA)->Size();
					strFidInfo << _T("(") << sDesc << _T(")[") << sFormat << _T(", ")
						<< static_cast<UINT>(nPicType) << _T("]: ") << sMimeType << _T(", ")
						<< static_cast<UINT>(nDataSize) << _T(" bytes");
					delete[] sMimeType;
					delete[] sDesc;
					delete[] sFormat;
				}
				break;
			case ID3FID_GENERALOBJECT:
				{
					wchar_t *sMimeType = ID3_GetStringW(frame, ID3FN_MIMETYPE);
					wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
					wchar_t *sFileName = ID3_GetStringW(frame, ID3FN_FILENAME);
					size_t
						nDataSize = frame->GetField(ID3FN_DATA)->Size();
					strFidInfo << _T("(") << sDesc << _T(")[")
						<< sFileName << _T("]: ") << sMimeType << _T(", ") << static_cast<UINT>(nDataSize) << _T(" bytes");
					delete[] sMimeType;
					delete[] sDesc;
					delete[] sFileName;
					break;
				}
			case ID3FID_UNIQUEFILEID:
				{
					wchar_t *sOwner = ID3_GetStringW(frame, ID3FN_OWNER);
					size_t nDataSize = frame->GetField(ID3FN_DATA)->Size();
					strFidInfo << sOwner << _T(", ") << static_cast<UINT>(nDataSize) << _T(" bytes");
					delete[] sOwner;
				}
				break;
			case ID3FID_PLAYCOUNTER:
				{
					size_t nCounter = frame->GetField(ID3FN_COUNTER)->Get();
					strFidInfo << static_cast<UINT>(nCounter);
				}
				break;
			case ID3FID_POPULARIMETER:
				{
					wchar_t *sEmail = ID3_GetStringW(frame, ID3FN_EMAIL);
					size_t nCounter = frame->GetField(ID3FN_COUNTER)->Get();
					size_t nRating = frame->GetField(ID3FN_RATING)->Get();
					strFidInfo << sEmail << _T(", counter=") << static_cast<UINT>(nCounter) << _T(" rating=") << static_cast<UINT>(nRating);
					delete[] sEmail;
				}
				break;
			case ID3FID_CRYPTOREG:
			case ID3FID_GROUPINGREG:
				{
					wchar_t *sOwner = ID3_GetStringW(frame, ID3FN_OWNER);
					size_t
						nSymbol = frame->GetField(ID3FN_ID)->Get(),
						nDataSize = frame->GetField(ID3FN_DATA)->Size();
					strFidInfo << _T("(") << static_cast<UINT>(nSymbol) << _T("): ") << sOwner << _T(", ") << static_cast<UINT>(nDataSize) << _T(" bytes");
				}
				break;
			case ID3FID_SYNCEDLYRICS:
				{
					wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
					wchar_t *sLang = ID3_GetStringW(frame, ID3FN_LANGUAGE);
					size_t
						nRating = frame->GetField(ID3FN_CONTENTTYPE)->Get();
					strFidInfo << _T("(") << sDesc << _T(")[") << sLang << _T("]: ");
					switch (nRating) {
					case ID3CT_OTHER:
						strFidInfo << _T("Other");
						break;
					case ID3CT_LYRICS:
						strFidInfo << _T("Lyrics");
						break;
					case ID3CT_TEXTTRANSCRIPTION:
						strFidInfo << _T("Text transcription");
						break;
					case ID3CT_MOVEMENT:
						strFidInfo << _T("Movement/part name");
						break;
					case ID3CT_EVENTS:
						strFidInfo << _T("Events");
						break;
					case ID3CT_CHORD:
						strFidInfo << _T("Chord");
						break;
					case ID3CT_TRIVIA:
						strFidInfo << _T("Trivia/'pop up' information");
					}
					delete[] sDesc;
					delete[] sLang;
				}
				break;
			case ID3FID_AUDIOCRYPTO:
			case ID3FID_EQUALIZATION:
			case ID3FID_EVENTTIMING:
			case ID3FID_CDID:
			case ID3FID_MPEGLOOKUP:
			case ID3FID_OWNERSHIP:
			case ID3FID_PRIVATE:
			case ID3FID_POSITIONSYNC:
			case ID3FID_BUFFERSIZE:
			case ID3FID_VOLUMEADJ:
			case ID3FID_REVERB:
			case ID3FID_SYNCEDTEMPO:
			case ID3FID_METACRYPTO:
				break;
			default:
				break;
			}

			if (!strFidInfo.IsEmpty()) {
				mi->strInfo << _T("   ") << CString(desc) << _T(":");
				for (int iPos = 0; iPos >= 0;) {
					const CString &strFidInfoLine(strFidInfo.GetText().Tokenize(_T("\r\n"), iPos));
					if (!strFidInfoLine.IsEmpty())
						mi->strInfo << _T("\t") << strFidInfoLine << _T("\r\n");
				}
			}
		}
		delete iter;
	} catch (...) {
		ASSERT(0);
	}

	if (bFoundHeader) {
		mi->InitFileLength();
		return true;
	}

	if (!::IsWindow(hWndOwner))
		return false;

	/////////////////////////////////////////////////////////////////////////////
	// Try MediaInfo lib
	//
	// Use MediaInfo only for non AVI files. Reading potentially broken AVI files
	// with the VfW API (as MediaInfo is doing) is rather dangerous.
	//
	if (!bIsAVI) {
		try {
			m_handle = m_miLib.Open(strFilePath);
			if (m_handle) {
				auto _miGuard = MakeMediaInfoHandleGuard(m_miLib, m_handle); // RAII guard
				LPCTSTR pCodec = _T("Format");
				LPCTSTR pCodecInfo = _T("Format/Info");
				LPCTSTR	pCodecString = _T("Format/String");
				LPCTSTR	pLanguageInfo = _T("Language_More");

				mi->strFileFormat = InfoGet(MediaInfo_Stream_General, 0, _T("Format"));
				CString str(InfoGet(MediaInfo_Stream_General, 0, _T("Format/String")));
				if (!str.IsEmpty() && str != mi->strFileFormat)
					mi->strFileFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

				if (szExt[0] == _T('.') && szExt[1] != _T('\0')) {
					str = InfoGet(MediaInfo_Stream_General, 0, _T("Format/Extensions"));
					if (!str.IsEmpty()) {
						// minor bug in MediaInfo lib: some file extension lists have a ')' character in there.
						str.Remove(_T(')'));
						str.Remove(_T('('));
						str.MakeLower();

						bool bFoundExt = false;
						for (int iPos = 0; iPos >= 0;) {
							const CString &strFmtExt(str.Tokenize(_T(" "), iPos));
							if (!strFmtExt.IsEmpty() && strFmtExt == CPTR(szExt, 1)) {
								bFoundExt = true;
								break;
							}
						}
						if (!bFoundExt)
							WarnAboutWrongFileExtension(mi, strFileName, str);
					}
				}

				CString strTitle(InfoGet(MediaInfo_Stream_General, 0, _T("Title")));
				const CString &strTitleMore(InfoGet(MediaInfo_Stream_General, 0, _T("Title_More")));
				if (!strTitleMore.IsEmpty() && !strTitle.IsEmpty() && strTitleMore != strTitle)
					strTitle.AppendFormat(_T("; %s"), (LPCTSTR)strTitleMore);
				CString strAuthor(InfoGet(MediaInfo_Stream_General, 0, _T("Performer")));
				if (strAuthor.IsEmpty())
					strAuthor = InfoGet(MediaInfo_Stream_General, 0, _T("Author"));
				const CString &strCopyright(InfoGet(MediaInfo_Stream_General, 0, _T("Copyright")));
				CString strComments(InfoGet(MediaInfo_Stream_General, 0, _T("Comments")));
				if (strComments.IsEmpty())
					strComments = InfoGet(MediaInfo_Stream_General, 0, _T("Comment"));
				CString strDate(InfoGet(MediaInfo_Stream_General, 0, _T("Date")));
				if (strDate.IsEmpty())
					strDate = InfoGet(MediaInfo_Stream_General, 0, _T("Encoded_Date"));
				/*
				* Removed conversion of UTC time to local:
				* 1) _stscanf fails on strings such as "2000-09-08" (8 and 9 are not octal);
				* 2) the original time zone is unknown.
				*/
				if (!strTitle.IsEmpty() || !strAuthor.IsEmpty() || !strCopyright.IsEmpty() || !strComments.IsEmpty() || !strDate.IsEmpty()) {
					if (!mi->strInfo.IsEmpty())
						mi->strInfo << _T("\n");
					mi->OutputFileName();
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << GetResString(_T("FD_GENERAL")) << _T("\n");
					if (!strTitle.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("TITLE")) << _T(":\t") << strTitle << _T("\n");
					if (!strAuthor.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("AUTHOR")) << _T(":\t") << strAuthor << _T("\n");
					if (!strCopyright.IsEmpty())
						mi->strInfo << _T("   Copyright:\t") << strCopyright << _T("\n");
					if (!strComments.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("COMMENT")) << _T(":\t") << strComments << _T("\n");
					if (!strDate.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("DATE")) << _T(":\t") << strDate << _T("\n");
				}

				str = InfoGet(MediaInfo_Stream_General, 0, _T("Duration"));
				if (str.IsEmpty())
					str = InfoGet(MediaInfo_Stream_General, 0, _T("PlayTime")); //deprecated
				float fFileLengthSec = _tstoi(str) / SEC2MS(1.0f);
				UINT uAllBitrates = 0;

				str = InfoGet(MediaInfo_Stream_General, 0, _T("VideoCount"));
				int iVideoStreams = _tstoi(str);
				if (iVideoStreams > 0) {
					mi->iVideoStreams = iVideoStreams;
					mi->fVideoLengthSec = fFileLengthSec;

					str = InfoGet(MediaInfo_Stream_Video, 0, pCodec);
					mi->strVideoFormat = str;
					if (!str.IsEmpty()) {
						CStringA strCodecA(str);
						if (!strCodecA.IsEmpty())
							mi->video.bmiHeader.biCompression = *(LPDWORD)(LPCSTR)strCodecA;
					}
					str = InfoGet(MediaInfo_Stream_Video, 0, pCodecString);
					if (!str.IsEmpty() && str != mi->strVideoFormat)
						mi->strVideoFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

					str = InfoGet(MediaInfo_Stream_Video, 0, _T("Width"));
					mi->video.bmiHeader.biWidth = _tstoi(str);

					str = InfoGet(MediaInfo_Stream_Video, 0, _T("Height"));
					mi->video.bmiHeader.biHeight = _tstoi(str);

					str = InfoGet(MediaInfo_Stream_Video, 0, _T("FrameRate"));
					mi->fVideoFrameRate = _tstof(str);

					str = InfoGet(MediaInfo_Stream_Video, 0, _T("BitRate_Mode"));
					if (str.CompareNoCase(_T("VBR")) == 0) {
						mi->video.dwBitRate = _UI32_MAX;
						uAllBitrates = _UI32_MAX;
					} else {
						str = InfoGet(MediaInfo_Stream_Video, 0, _T("BitRate"));
						int iBitrate = _tstoi(str);
						mi->video.dwBitRate = iBitrate == -1 ? -1 : iBitrate;
						if (iBitrate == -1)
							uAllBitrates = _UI32_MAX;
						else //if (uAllBitrates != _UI32_MAX) always true
							uAllBitrates += iBitrate;
					}

					str = InfoGet(MediaInfo_Stream_Video, 0, _T("AspectRatio"));
					mi->fVideoAspectRatio = _tstof(str);

					for (int s = 1; s < iVideoStreams; ++s) {
						if (!mi->strInfo.IsEmpty())
							mi->strInfo << _T("\n");
						mi->OutputFileName();
						mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
						mi->strInfo << GetResString(_T("VIDEO")) << _T(" #") << s + 1 << _T("\n");

						CString strVideoFormat(InfoGet(MediaInfo_Stream_Video, s, pCodec));
						str = InfoGet(MediaInfo_Stream_Video, s, pCodecString);
						if (!str.IsEmpty() && str != strVideoFormat)
							strVideoFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);
						if (!strVideoFormat.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("CODEC")) << _T(":\t") << strVideoFormat << _T("\n");

						CString strBitrate;
						str = InfoGet(MediaInfo_Stream_Video, s, _T("BitRate_Mode"));
						if (str.CompareNoCase(_T("VBR")) == 0) {
							strBitrate = _T("Variable");
							uAllBitrates = _UI32_MAX;
						} else {
							str = InfoGet(MediaInfo_Stream_Video, s, _T("BitRate"));
							int iBitrate = _tstoi(str);
							if (iBitrate != 0) {
								if (iBitrate == -1) {
									strBitrate = _T("Variable");
									uAllBitrates = _UI32_MAX;
								} else {
									strBitrate.Format(_T("%u %s"), (iBitrate + SEC2MS(1u) / 2) / SEC2MS(1), (LPCTSTR)GetResString(_T("KBITSSEC")));
									if (uAllBitrates != _UI32_MAX)
										uAllBitrates += iBitrate;
								}
							}
						}
						if (!strBitrate.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("BITRATE")) << _T(":\t") << strBitrate << _T("\n");

						const CString &strWidth(InfoGet(MediaInfo_Stream_Video, s, _T("Width")));
						const CString &strHeight(InfoGet(MediaInfo_Stream_Video, s, _T("Height")));
						if (!strWidth.IsEmpty() && !strHeight.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("WIDTH")) << _T(" x ") << GetResString(_T("HEIGHT")) << _T(":\t") << strWidth << _T(" x ") << strHeight << _T("\n");

						str = InfoGet(MediaInfo_Stream_Video, s, _T("AspectRatio"));
						if (!str.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("ASPECTRATIO")) << _T(":\t") << str << _T("  (") << GetKnownAspectRatioDisplayString((float)_tstof(str)) << _T(")\n");

						str = InfoGet(MediaInfo_Stream_Video, s, _T("FrameRate"));
						if (!str.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("FPS")) << _T(":\t") << str << _T("\n");
					}

					bFoundHeader = true;
				}

				str = InfoGet(MediaInfo_Stream_General, 0, _T("AudioCount"));
				int iAudioStreams = _tstoi(str);
				if (iAudioStreams > 0) {
					mi->iAudioStreams = iAudioStreams;
					mi->fAudioLengthSec = fFileLengthSec;

					str = InfoGet(MediaInfo_Stream_Audio, 0, pCodec);
					if (_stscanf(str, _T("%hx"), &mi->audio.wFormatTag) != 1) {
						mi->strAudioFormat = str;
						str = InfoGet(MediaInfo_Stream_Audio, 0, pCodecString);
					} else {
						mi->strAudioFormat = InfoGet(MediaInfo_Stream_Audio, 0, pCodecString);
						str = InfoGet(MediaInfo_Stream_Audio, 0, pCodecInfo);
					}
					if (!str.IsEmpty() && str != mi->strAudioFormat)
						mi->strAudioFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

					str = InfoGet(MediaInfo_Stream_Audio, 0, _T("Channel(s)"));
					mi->audio.nChannels = (WORD)_tstoi(str);

					str = InfoGet(MediaInfo_Stream_Audio, 0, _T("SamplingRate"));
					mi->audio.nSamplesPerSec = _tstoi(str);

					str = InfoGet(MediaInfo_Stream_Audio, 0, _T("BitRate_Mode"));
					if (str.CompareNoCase(_T("VBR")) == 0) {
						mi->audio.nAvgBytesPerSec = _UI32_MAX;
						uAllBitrates = _UI32_MAX;
					} else {
						str = InfoGet(MediaInfo_Stream_Audio, 0, _T("BitRate"));
						int iBitrate = _tstoi(str);
						mi->audio.nAvgBytesPerSec = iBitrate == -1 ? -1 : iBitrate / 8;
						if (iBitrate == -1)
							uAllBitrates = _UI32_MAX;
						else if (uAllBitrates != _UI32_MAX)
							uAllBitrates += iBitrate;
					}

					mi->strAudioLanguage = InfoGet(MediaInfo_Stream_Audio, 0, _T("Language/String"));
					if (mi->strAudioLanguage.IsEmpty())
						mi->strAudioLanguage = InfoGet(MediaInfo_Stream_Audio, 0, _T("Language"));

					for (int s = 1; s < iAudioStreams; ++s) {
						if (!mi->strInfo.IsEmpty())
							mi->strInfo << _T("\n");
						mi->OutputFileName();
						mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
						mi->strInfo << GetResString(_T("AUDIO")) << _T(" #") << s + 1 << _T("\n");

						CString strAudioFormat(InfoGet(MediaInfo_Stream_Audio, s, pCodec));
						str = InfoGet(MediaInfo_Stream_Audio, s, pCodecString);
						WORD wFormatTag;
						if (_stscanf(str, _T("%hx"), &wFormatTag) == 1) {
							strAudioFormat = str;
							str = InfoGet(MediaInfo_Stream_Audio, s, pCodecInfo);
						}
						if (!str.IsEmpty() && str != strAudioFormat)
							strAudioFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);
						if (!strAudioFormat.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("CODEC")) << _T(":\t") << strAudioFormat << _T("\n");

						CString strBitrate;
						str = InfoGet(MediaInfo_Stream_Audio, s, _T("BitRate_Mode"));
						if (str.CompareNoCase(_T("VBR")) == 0) {
							strBitrate = _T("Variable");
							uAllBitrates = _UI32_MAX;
						} else {
							str = InfoGet(MediaInfo_Stream_Audio, s, _T("BitRate"));
							int iBitrate = _tstoi(str);
							if (iBitrate != 0) {
								if (iBitrate == -1) {
									strBitrate = _T("Variable");
									uAllBitrates = _UI32_MAX;
								} else {
									strBitrate.Format(_T("%u %s"), (iBitrate + SEC2MS(1u) / 2) / SEC2MS(1), (LPCTSTR)GetResString(_T("KBITSSEC")));
									if (uAllBitrates != _UI32_MAX)
										uAllBitrates += iBitrate;
								}
							}
						}
						if (!strBitrate.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("BITRATE")) << _T(":\t") << strBitrate << _T("\n");

						str = InfoGet(MediaInfo_Stream_Audio, s, _T("Channel(s)"));
						if (!str.IsEmpty()) {
							int iChannels = _tstoi(str);
							mi->strInfo << _T("   ") << GetResString(_T("CHANNELS")) << _T(":\t");
							if (iChannels == 1)
								mi->strInfo << _T("1 (Mono)");
							else if (iChannels == 2)
								mi->strInfo << _T("2 (Stereo)");
							else if (iChannels == 5)
								mi->strInfo << _T("5.1 (Surround)");
							else
								mi->strInfo << iChannels;
							mi->strInfo << _T("\n");
						}

						str = InfoGet(MediaInfo_Stream_Audio, s, _T("SamplingRate"));
						if (!str.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("SAMPLERATE")) << _T(":\t") << _tstoi(str) / 1000.0 << _T(" kHz\n");

						str = InfoGet(MediaInfo_Stream_Audio, s, _T("Language/String"));
						if (str.IsEmpty())
							str = InfoGet(MediaInfo_Stream_Audio, s, _T("Language"));
						if (!str.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("PW_LANG")) << _T(":\t") << str << _T("\n");

						str = InfoGet(MediaInfo_Stream_Audio, s, pLanguageInfo);
						if (!str.IsEmpty())
							mi->strInfo << _T("   ") << GetResString(_T("PW_LANG")) << _T(":\t") << str << _T("\n");
					}

					bFoundHeader = true;
				}

				str = InfoGet(MediaInfo_Stream_General, 0, _T("TextCount"));
				int iTextStreams = _tstoi(str);
				for (int s = 0; s < iTextStreams; ++s) {
					if (!mi->strInfo.IsEmpty())
						mi->strInfo << _T("\n");
					mi->OutputFileName();
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << _T("Subtitle") << _T(" #") << s + 1 << _T("\n");

					str = InfoGet(MediaInfo_Stream_Text, s, pCodec);
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("CODEC")) << _T(":\t") << str << _T("\n");

					str = InfoGet(MediaInfo_Stream_Text, s, _T("Language/String"));
					if (str.IsEmpty())
						str = InfoGet(MediaInfo_Stream_Text, s, _T("Language"));
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("PW_LANG")) << _T(":\t") << str << _T("\n");

					str = InfoGet(MediaInfo_Stream_Text, s, pLanguageInfo);
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(_T("PW_LANG")) << _T(":\t") << str << _T("\n");
				}

				str = InfoGet(MediaInfo_Stream_General, 0, _T("MenuCount"));
				int iMenuStreams = _tstoi(str);
				for (int m = 0; m < iMenuStreams; ++m) {
					if (!mi->strInfo.IsEmpty())
						mi->strInfo << _T("\n");
					mi->OutputFileName();
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << _T("Menu") << _T(" #") << m + 1 << _T("\n");

					str = InfoGet(MediaInfo_Stream_Menu, m, _T("Chapters_Pos_Begin"));
					int iBegin = _tstoi(str);
					str = InfoGet(MediaInfo_Stream_Menu, m, _T("Chapters_Pos_End"));
					int iEnd = _tstoi(str);
					for (int s = iBegin; s < iEnd; ++s) {
						if (!mi->strInfo.IsEmpty())
							mi->strInfo << _T("\n");
						mi->OutputFileName();

						mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
						mi->strInfo << _T("  Chapter") << _T(" #") << s - iBegin + 1 << _T("\n");

						str = InfoGetI(MediaInfo_Stream_Menu, m, s, MediaInfo_Info_Name);
						mi->strInfo << _T("   ") << str << _T("\t");
						str = InfoGetI(MediaInfo_Stream_Menu, m, s, MediaInfo_Info_Text);
						mi->strInfo << str << _T("\n");
					}
				}

				// MediaInfoLib does not handle MPEG files correctly in regards of
				// play length property -- even for completed files (applies also for
				// v0.7.2.1). So, we try to calculate the play length by using the
				// various bit rates. We could do this only for part files which are
				// still not having the final file length, but MediaInfoLib also
				// fails to determine play length for completed files (Hint: one can
				// not use GOPs to determine the play length (properly)).
				//
				//"MPEG 1"		v0.7.0.0
				//"MPEG-1 PS"	v0.7.2.1
				if (mi->strFileFormat.Find(_T("MPEG")) == 0) {	/* MPEG container? */
					if (uAllBitrates != 0				/* do we have any bit rates? */
						&& uAllBitrates != _UI32_MAX)	/* do we have CBR only? */
					{
						// Though, it's not that easy to calculate the real play length
						// without including the container's overhead. The value we
						// calculate with this simple formula is slightly too large!
						// But, its still better than using GOP-derived values which are
						// sometimes completely wrong.
						fFileLengthSec = (uint64)uFileSize * 8.0f / uAllBitrates;

						if (mi->iVideoStreams > 0) {
							// Try to compensate the error from above by estimating the overhead
							if (mi->fVideoFrameRate > 0) {
								ULONGLONG uFrames = (ULONGLONG)(fFileLengthSec * mi->fVideoFrameRate);
								fFileLengthSec = ((uint64)uFileSize - uFrames * 24) * 8.0f / uAllBitrates;
							}
							mi->fVideoLengthSec = fFileLengthSec;
						}
						if (mi->iAudioStreams > 0)
							mi->fAudioLengthSec = fFileLengthSec;
					}
					// set the 'estimated' flags in case of any VBR stream
					mi->bVideoLengthEstimated |= (mi->iVideoStreams > 0);
					mi->bAudioLengthEstimated |= (mi->iAudioStreams > 0);
				}

				if (bFoundHeader) {
					mi->InitFileLength();
					return true;
				}
			}
		} catch (...) {
			ASSERT(0);
		}
	}

	if (!::IsWindow(hWndOwner))
		return false;

	/////////////////////////////////////////////////////////////////////////////
	// Try MediaDet object
	//
#ifdef HAVE_QEDIT_H
	try {
		CComPtr<IMediaDet> pMediaDet;
		HRESULT hr = pMediaDet.CoCreateInstance(__uuidof(MediaDet));
		if (SUCCEEDED(hr)) {
			hr = pMediaDet->put_Filename(CComBSTR(strFilePath));
			if (SUCCEEDED(hr)) {
				long lStreams;
				if (SUCCEEDED(pMediaDet->get_OutputStreams(&lStreams))) {
					for (long i = 0; i < lStreams; ++i) {
						if (SUCCEEDED(pMediaDet->put_CurrentStream(i))) {
							GUID major_type;
							if (SUCCEEDED(pMediaDet->get_StreamType(&major_type))) {
								if (major_type == MEDIATYPE_Video) {
									++mi->iVideoStreams;

									if (mi->iVideoStreams > 1) {
										if (!mi->strInfo.IsEmpty())
											mi->strInfo << _T("\n");
										mi->OutputFileName();
										mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
										mi->strInfo << GetResString(_T("VIDEO")) << _T(" #") << mi->iVideoStreams << _T("\n");
									}

									AM_MEDIA_TYPE mt;
									if (SUCCEEDED(pMediaDet->get_StreamMediaType(&mt))) {
										if (mt.formattype == FORMAT_VideoInfo) {
											VIDEOINFOHEADER *pVIH = (VIDEOINFOHEADER*)mt.pbFormat;

											if (mi->iVideoStreams == 1) {
												mi->video = *pVIH;
												if (mi->video.bmiHeader.biWidth && mi->video.bmiHeader.biHeight)
													mi->fVideoAspectRatio = abs(mi->video.bmiHeader.biWidth) / (double)abs(mi->video.bmiHeader.biHeight);
												mi->video.dwBitRate = 0; // don't use this value
												mi->strVideoFormat = GetVideoFormatName(mi->video.bmiHeader.biCompression);
												pMediaDet->get_FrameRate(&mi->fVideoFrameRate);
												bFoundHeader = true;
											} else {
												mi->strInfo << _T("   ") << GetResString(_T("CODEC")) << _T(":\t") << GetVideoFormatName(pVIH->bmiHeader.biCompression) << _T("\n");
												mi->strInfo << _T("   ") << GetResString(_T("WIDTH")) << _T(" x ") << GetResString(_T("HEIGHT")) << _T(":\t") << abs(pVIH->bmiHeader.biWidth) << _T(" x ") << abs(pVIH->bmiHeader.biHeight) << _T("\n");
												// do not use that 'dwBitRate', whatever this number is, it's not
												// the bit rate of the *encoded* video stream. Seems to be the bit rate
												// of the *decoded* stream

												double fFrameRate;
												if (SUCCEEDED(pMediaDet->get_FrameRate(&fFrameRate)) && fFrameRate)
													mi->strInfo << _T("   ") << GetResString(_T("FPS")) << _T(":\t") << fFrameRate << _T("\n");
											}
										}
									}

									double fLength;
									if (SUCCEEDED(pMediaDet->get_StreamLength(&fLength)) && fLength) {
										if (mi->iVideoStreams == 1)
											mi->fVideoLengthSec = fLength;
										else {
											mi->strInfo << _T("   ") << GetResString(_T("LENGTH")) << _T(":\t") << SecToTimeLength((UINT)fLength);
											if (bIsPartFile) {
												mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
												mi->strInfo << _T(" (") << GetResString(_T("ESTIMATED")) << _T(")");
												mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
											}
											mi->strInfo << _T("\n");
										}
									}

									if (mt.pUnk != NULL)
										mt.pUnk->Release();
									::CoTaskMemFree(mt.pbFormat);
								} else if (major_type == MEDIATYPE_Audio) {
									++mi->iAudioStreams;

									if (mi->iAudioStreams > 1) {
										if (!mi->strInfo.IsEmpty())
											mi->strInfo << _T("\n");
										mi->OutputFileName();
										mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
										mi->strInfo << GetResString(_T("AUDIO")) << _T(" #") << mi->iAudioStreams << _T("\n");
									}

									AM_MEDIA_TYPE mt;
									if (SUCCEEDED(pMediaDet->get_StreamMediaType(&mt))) {
										if (mt.formattype == FORMAT_WaveFormatEx) {
											WAVEFORMATEX *wfx = (WAVEFORMATEX*)mt.pbFormat;

											// Try to determine if the stream is VBR.
											//
											// MediaDet seems to only look at the AVI stream headers to get a hint
											// about CBR/VBR. If the stream headers are looking "odd", MediaDet
											// reports "VBR". Typically, files muxed with Nandub get identified as
											// VBR (just because of the stream headers) even if they are CBR. Also,
											// real VBR MP3 files still get reported as CBR. Though, basically it's
											// better to report VBR even if it's CBR. The other way round is even
											// more ugly.
											if (!mt.bFixedSizeSamples)
												wfx->nAvgBytesPerSec = _UI32_MAX;

											if (mi->iAudioStreams == 1) {
												mi->audio = *(WAVEFORMAT*)wfx;
												mi->strAudioFormat = GetAudioFormatName(wfx->wFormatTag);
											} else {
												mi->strInfo << _T("   ") << GetResString(_T("CODEC")) << _T(":\t") << GetAudioFormatName(wfx->wFormatTag) << _T("\n");

												if (wfx->nAvgBytesPerSec) {
													CString strBitrate;
													if (wfx->nAvgBytesPerSec == _UI32_MAX)
														strBitrate = _T("Variable");
													else
														strBitrate.Format(_T("%u %s"), (unsigned)(((wfx->nAvgBytesPerSec * 16ull) + SEC2MS(1)) / SEC2MS(2)), (LPCTSTR)GetResString(_T("KBITSSEC")));
													mi->strInfo << _T("   ") << GetResString(_T("BITRATE")) << _T(":\t") << strBitrate << _T("\n");
												}

												if (wfx->nChannels) {
													mi->strInfo << _T("   ") << GetResString(_T("CHANNELS")) << _T(":\t");
													if (wfx->nChannels == 1)
														mi->strInfo << _T("1 (Mono)");
													else if (wfx->nChannels == 2)
														mi->strInfo << _T("2 (Stereo)");
													else if (wfx->nChannels == 5)
														mi->strInfo << _T("5.1 (Surround)");
													else
														mi->strInfo << wfx->nChannels;
													mi->strInfo << _T("\n");
												}

												if (wfx->nSamplesPerSec)
													mi->strInfo << _T("   ") << GetResString(_T("SAMPLERATE")) << _T(":\t") << wfx->nSamplesPerSec / 1000.0 << _T(" kHz\n");

												if (wfx->wBitsPerSample)
													mi->strInfo << _T("   Bit/sample:\t") << wfx->wBitsPerSample << _T(" Bit\n");
											}
											bFoundHeader = true;
										}
									}

									double fLength;
									if (SUCCEEDED(pMediaDet->get_StreamLength(&fLength)) && fLength) {
										if (mi->iAudioStreams == 1)
											mi->fAudioLengthSec = fLength;
										else {
											mi->strInfo << _T("   ") << GetResString(_T("LENGTH")) << _T(":\t") << SecToTimeLength((UINT)fLength);
											if (bIsPartFile) {
												mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
												mi->strInfo << _T(" (") << GetResString(_T("ESTIMATED")) << _T(")");
												mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
											}
											mi->strInfo << _T("\n");
										}
									}

									if (mt.pUnk != NULL)
										mt.pUnk->Release();
									::CoTaskMemFree(mt.pbFormat);
								} else {
									if (!mi->strInfo.IsEmpty())
										mi->strInfo << _T("\n");
									mi->OutputFileName();
									mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
									mi->strInfo << GetResString(_T("UNKNOWN")) << _T(" Stream #") << i + 1 << _T("\n");

									double fLength;
									if (SUCCEEDED(pMediaDet->get_StreamLength(&fLength)) && fLength) {
										mi->strInfo << _T("   ") << GetResString(_T("LENGTH")) << _T(":\t") << SecToTimeLength((UINT)fLength);
										if (bIsPartFile) {
											mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
											mi->strInfo << _T(" (") << GetResString(_T("ESTIMATED")) << _T(")");
											mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
										}
										mi->strInfo << _T("\n");
									}
									mi->strInfo << _T("\n");
								}
							}
						}
					}
					if (bFoundHeader)
						mi->InitFileLength();
				}
			} else
	TRACE(_T("Failed to open \"%s\" - %s\n"), (LPCTSTR)strFilePath, (LPCTSTR)GetErrorMessage(hr, 1));
		}
	} catch (...) {
		ASSERT(0);
	}
#else//HAVE_QEDIT_H
#pragma message("WARNING: Missing 'qedit.h' header file - some features will get disabled. See the file 'emule_site_config.h' for more information.")
#endif//HAVE_QEDIT_

	return bFoundHeader || !mi->strMimeType.IsEmpty() || bHasDRM;
}

void CFileInfoDialog::DoDataExchange(CDataExchange *pDX)
{
	CResizablePage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FULL_FILE_INFO, m_fi);
}

void CFileInfoDialog::Localize()
{
	if (!m_hWnd)
		return;
	SetTabTitle(_T("CONTENT_INFO"), this);

	SetDlgItemText(IDC_GENERAL, GetResString(_T("FD_GENERAL")));
	SetDlgItemText(IDC_FD_XI2, GetResString(_T("LENGTH")) + _T(':'));
	SetDlgItemText(IDC_FD_XI3, GetResString(_T("VIDEO")));
	SetDlgItemText(IDC_FD_XI4, GetResString(_T("AUDIO")));
	SetDlgItemText(IDC_FD_XI5, GetResString(_T("CODEC")) + _T(':'));
	SetDlgItemText(IDC_FD_XI6, GetResString(_T("CODEC")) + _T(':'));
	SetDlgItemText(IDC_FD_XI7, GetResString(_T("BITRATE")) + _T(':'));
	SetDlgItemText(IDC_FD_XI8, GetResString(_T("BITRATE")) + _T(':'));
	CString sWH;
	sWH.Format(_T("%s x %s:"), (LPCTSTR)GetResString(_T("WIDTH")), (LPCTSTR)GetResString(_T("HEIGHT")));
	SetDlgItemText(IDC_FD_XI9, sWH);
	SetDlgItemText(IDC_FD_XI13, GetResString(_T("FPS")) + _T(':'));
	SetDlgItemText(IDC_FD_XI10, GetResString(_T("CHANNELS")) + _T(':'));
	SetDlgItemText(IDC_FD_XI12, GetResString(_T("SAMPLERATE")) + _T(':'));
	SetDlgItemText(IDC_STATICFI, GetResString(_T("FILEFORMAT")) + _T(':'));
	SetDlgItemText(IDC_FD_XI14, GetResString(_T("ASPECTRATIO")) + _T(':'));
	SetDlgItemText(IDC_STATIC_LANGUAGE, GetResString(_T("PW_LANG")) + _T(':'));
	if (!m_bReducedDlg)
		SetDlgItemText(IDC_FD_XI1, GetResString(_T("FD_SIZE")));
}
