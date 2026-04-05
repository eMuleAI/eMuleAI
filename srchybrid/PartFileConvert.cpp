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
#include <io.h>
#include "emule.h"
#include "PartFileConvert.h"
#include "OtherFunctions.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "SharedFileList.h"
#include "emuledlg.h"
#include "Log.h"
#include "opcodes.h"
#include "MuleListCtrl.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

enum convstatus
{
	CONV_OK = 0,
	CONV_QUEUE,
	CONV_INPROGRESS,
	CONV_OUTOFDISKSPACE,
	CONV_PARTMETNOTFOUND,
	CONV_IOERROR,
	CONV_FAILED,
	CONV_BADFORMAT,
	CONV_ALREADYEXISTS
};

struct ConvertJob
{
	uint64	size;
	uint64	spaceneeded;
	CString folder;
	CString filename;
	CString filehash;
	int     format;
	int		state;
	EPartFileFormat	partmettype;
	bool	removeSource;
	ConvertJob()
		: size()
		, spaceneeded()
		, format()
		, state()
		, partmettype(PMT_UNKNOWN)
		, removeSource(true)
	{
	}
};

static CWinThread	*convertPfThread;
static CList<ConvertJob*, ConvertJob*> m_jobs;
static ConvertJob	*pfconverting;
static CPartFileConvertDlg *m_convertgui;

int CPartFileConvert::ScanFolderToAdd(const CString &folder, bool deletesource)
{
	ASSERT(folder.Right(1) != _T("\\"));
	int count = 0;
	CFileFind finder;
	for (BOOL bFound = finder.FindFile(folder + _T("\\*.part.met")); bFound;) {
		bFound = finder.FindNextFile();
		ConvertToeMule(finder.GetFilePath(), deletesource);
		++count;
	}
	// Shareaza
	for (BOOL bFound = finder.FindFile(folder + _T("\\*.sd")); bFound;) {
		bFound = finder.FindNextFile();
		ConvertToeMule(finder.GetFilePath(), deletesource);
		++count;
	}

	for (BOOL bFound = finder.FindFile(folder + _T("\\*.*")); bFound;) {
		bFound = finder.FindNextFile();
		if (finder.IsDirectory() && !finder.IsDots())
			count += ScanFolderToAdd(finder.GetFilePath(), deletesource);
	}
	return count;
}

void CPartFileConvert::ConvertToeMule(const CString &folder, bool deletesource)
{
	if (!::PathFileExists(folder))
		return;
	//	return;

	ConvertJob *newjob = new ConvertJob();
	newjob->folder = folder;
	newjob->removeSource = deletesource;
	newjob->state = CONV_QUEUE;
	m_jobs.AddTail(newjob);

	if (m_convertgui)
		m_convertgui->AddJob(newjob);

	StartThread();
	m_convertgui->SetForegroundWindow();
}

void CPartFileConvert::StartThread()
{
	if (convertPfThread == NULL)
		convertPfThread = AfxBeginThread(run, NULL, THREAD_PRIORITY_IDLE);
}

UINT AFX_CDECL CPartFileConvert::run(LPVOID /*lpParam*/)
{
	DbgSetThreadName("Partfile-Converter");

	int imported = 0;

	for (;;) {
		// search next queued job and start it
		pfconverting = NULL;
		for (POSITION pos = m_jobs.GetHeadPosition(); pos != NULL;) {
			pfconverting = m_jobs.GetNext(pos);
			if (pfconverting->state == CONV_QUEUE)
				break;
			pfconverting = NULL;
		}
		if (pfconverting == NULL)
			break;// nothing more to do now
		pfconverting->state = CONV_INPROGRESS;
		UpdateGUI(pfconverting);
		pfconverting->state = performConvertToeMule(pfconverting->folder);

		if (pfconverting->state == CONV_OK)
			++imported;

		UpdateGUI(pfconverting);
		AddLogLine(true, GetResString(_T("IMP_STATUS")), (LPCTSTR)EscPercent(pfconverting->folder), (LPCTSTR)EscPercent(GetReturncodeText(pfconverting->state)));
	}

	// clean up
	UpdateGUI(NULL);

	if (imported)
		theApp.sharedfiles->PublishNextTurn();

	convertPfThread = NULL;
	return 0;
}

int CPartFileConvert::performConvertToeMule(const CString &folder)
{
	int i = folder.ReverseFind(_T('\\')) + 1;
	if (i <= 0)
		return CONV_FAILED;
	const CString &partfile(folder.Mid(i));
	CString sDir(folder, i); //keep the trailing backslash

	UpdateGUI(0, GetResString(_T("IMP_STEPREADPF")), true);

	const CString &filepartindex(partfile.Left(partfile.Find(_T('.'))));

	UpdateGUI(4.0f, GetResString(_T("IMP_STEPBASICINF")));

	CPartFile *file = new CPartFile();
	EPartFileFormat eFormat;
	if (file->LoadPartFile(sDir, partfile, &eFormat) != PLR_CHECKSUCCESS)
		eFormat = PMT_UNKNOWN;
	switch (eFormat) {
	case PMT_UNKNOWN:
	case PMT_BADFORMAT:
		delete file;
		return CONV_BADFORMAT;
	}
	if (theApp.downloadqueue->GetFileByID(file->GetFileHash()) != NULL) {
		delete file;
		return CONV_ALREADYEXISTS;
	}

	pfconverting->size = file->GetFileSize();
	pfconverting->filename = file->GetFileName();
	pfconverting->filehash = EncodeBase16(file->GetFileHash(), 16);
	pfconverting->partmettype = eFormat;
	UpdateGUI(pfconverting);

	CString newfilename;
	CFileFind finder;
	if (pfconverting->partmettype == PMT_SPLITTED) {
		try {
			CByteArray ba;
			ba.SetSize(PARTSIZE);

			// find highest part file number
			int maxindex = 0;
			UINT partfilecount = 0;
			for (BOOL bFound = finder.FindFile(sDir + filepartindex + _T(".*.part")); bFound;) {
				bFound = finder.FindNextFile();
				++partfilecount;
				const CString &filename(finder.GetFileName());
				int pos1 = filename.Find('.');
				int pos2 = filename.Find('.', pos1 + 1);
				int fileindex = _tstoi(filename.Mid(pos1 + 1, pos2 - pos1));
				if (fileindex > maxindex)
					maxindex = fileindex;
			}
			float stepperpart;
			if (partfilecount > 0) {
				stepperpart = (80.0f / partfilecount);
				if (maxindex * PARTSIZE <= pfconverting->size)
					pfconverting->spaceneeded = maxindex * PARTSIZE;
				else
					pfconverting->spaceneeded = ((pfconverting->size / PARTSIZE) * PARTSIZE) + (pfconverting->size % PARTSIZE);
			} else {
				stepperpart = 80.0f;
				pfconverting->spaceneeded = 0;
			}

			if (GetFreeDiskSpaceX(thePrefs.GetTempDir()) < maxindex * PARTSIZE) {
				delete file;
				return CONV_OUTOFDISKSPACE;
			}

			UpdateGUI(pfconverting);

			// create new partmetfile, and remember the new name
			file->CreatePartFile();
			newfilename = file->GetFullName();

			UpdateGUI(8.0f, GetResString(_T("IMP_STEPCRDESTFILE")));
			file->m_hpartfile.SetLength(pfconverting->spaceneeded);

			CString buffer;
			CFile inputfile;
			unsigned curindex = 0;
			for (BOOL bFound = finder.FindFile(sDir + filepartindex + _T(".*.part")); bFound;) {
				bFound = finder.FindNextFile();

				//stats
				++curindex;
				float fPercent = 10 + curindex * stepperpart;
				buffer.Format(GetResString(_T("IMP_LOADDATA")), curindex, partfilecount);
				UpdateGUI(fPercent, buffer);

				const CString &filename(finder.GetFileName());
				int pos1 = filename.Find('.');
				int pos2 = filename.Find('.', pos1 + 1);
				int fileindex = _tstoi(filename.Mid(pos1 + 1, pos2 - pos1));
				if (fileindex <= 0)
					continue;

				ULONGLONG chunkstart = fileindex * PARTSIZE + PARTSIZE - 1;

				// open, read data of the part-part-file into buffer, close file
				inputfile.Open(finder.GetFilePath(), CFile::modeRead | CFile::shareDenyWrite);
				UINT nRead = inputfile.Read(ba.GetData(), (UINT)PARTSIZE);
				inputfile.Close();

				buffer.Format(GetResString(_T("IMP_SAVEDATA")), curindex, partfilecount);
				UpdateGUI(fPercent, buffer);

				// write the buffered data
				OVERLAPPED ov = { 0, 0, {{LODWORD(chunkstart), HIDWORD(chunkstart)}} };
				if (!::WriteFile((HANDLE)file->m_hpartfile, ba.GetData(), nRead, NULL, &ov))
					CFileException::ThrowOsError((LONG)::GetLastError(), file->m_hpartfile.GetFileName());
			}
		} catch (CFileException *ex) {
			LogError(LOG_DEFAULT, _T("%s%s"), (LPCTSTR)GetResString(_T("IMP_IOERROR")), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
			delete file;
			return CONV_IOERROR;
		}
		file->m_hpartfile.Close();
	}
	else {
		// import an external common format part download
		const CString& oldfile(sDir + partfile.Left(partfile.GetLength() - ((pfconverting->partmettype == PMT_SHAREAZA) ? 3 : 4)));

		if (!pfconverting->removeSource)
			pfconverting->spaceneeded = GetDiskFileSize(oldfile);

		if (!pfconverting->removeSource && (GetFreeDiskSpaceX(thePrefs.GetTempDir()) < pfconverting->spaceneeded)) {
			delete file;
			return CONV_OUTOFDISKSPACE;
		}

		UpdateGUI(pfconverting);

		file->CreatePartFile();
		newfilename = file->GetFullName();

		file->m_hpartfile.Close();

		UpdateGUI(92.0f, GetResString(_T("COPY")));
		// Long-path safe file operations (data file)
		CString dataTarget = newfilename.Left(newfilename.GetLength() - 4);
		::DeleteFile(PreparePathForWin32LongPath(dataTarget));

		BOOL ret;
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (!GetFileAttributesEx(PreparePathForWin32LongPath(oldfile), GetFileExInfoStandard, &fad)) {
			// Create empty data file if source does not exist
			HANDLE hFile = ::CreateFile(PreparePathForWin32LongPath(dataTarget), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			ret = (hFile != INVALID_HANDLE_VALUE);
			if (hFile != INVALID_HANDLE_VALUE)
				::CloseHandle(hFile);
		}
		else if (pfconverting->removeSource)
			ret = MoveFile(oldfile, dataTarget);
		else
			ret = CopyFile(oldfile, dataTarget, FALSE);

		if (!ret) {
			file->DeletePartFile(); //delayed delete
			return CONV_FAILED;
		}
	}

	UpdateGUI(94.0f, GetResString(_T("IMP_GETPFINFO")));
	::DeleteFile(PreparePathForWin32LongPath(newfilename));

	if (pfconverting->removeSource)
		MoveFile(PreparePathForWin32LongPath(sDir + partfile), PreparePathForWin32LongPath(newfilename));
	else
		CopyFile(PreparePathForWin32LongPath(sDir + partfile), PreparePathForWin32LongPath(newfilename), FALSE);

	// clean up
	file->GetFileIdentifier().DeleteMD4Hashset();
	file->m_gaplist.RemoveAll();
	file->ClearTags();
	// reload from part.met file
	if (file->LoadPartFile(thePrefs.GetTempDir(), file->GetPartMetFileName()) != PLR_LOADSUCCESS) {
		file->DeletePartFile(); //delayed delete
		return CONV_BADFORMAT;
	}

	if (pfconverting->partmettype == PMT_NEWOLD || pfconverting->partmettype == PMT_SPLITTED) {
		file->m_completedsize = file->m_uTransferred;
		file->m_uCompressionGain = 0;
		file->m_uCorruptionLoss = 0;
	}

	UpdateGUI(100.0f, GetResString(_T("IMP_ADDDWL")));

	theApp.downloadqueue->AddDownload(file, thePrefs.AddNewFilesPaused());
	file->SavePartFile();

	if (file->GetStatus(true) == PS_READY)
		theApp.sharedfiles->SafeAddKFile(file); // part files are always shared files

	if (pfconverting->removeSource) {
		for (BOOL bFound = finder.FindFile(sDir + filepartindex + _T(".*")); bFound;) {
			bFound = finder.FindNextFile();
			VERIFY(_tunlink(finder.GetFilePath()) == 0);
		}

		if (pfconverting->partmettype == PMT_SPLITTED)
			RemoveDirectory(sDir);
	}

	return CONV_OK;
}

void CPartFileConvert::UpdateGUI(float percent, const CString &text, bool fullinfo)
{
	if (m_convertgui == NULL)
		return;

	m_convertgui->pb_current.SetPos((int)percent);
	CString buffer;
	buffer.Format(_T("%.2f %%"), percent);
	m_convertgui->SetDlgItemText(IDC_CONV_PROZENT, buffer);

	if (!text.IsEmpty())
		m_convertgui->SetDlgItemText(IDC_CONV_PB_LABEL, text);

	if (fullinfo)
		m_convertgui->SetDlgItemText(IDC_CURJOB, pfconverting->folder);
}

void CPartFileConvert::UpdateGUI(ConvertJob *job)
{
	if (m_convertgui != NULL)
		m_convertgui->UpdateJobInfo(job);
}


void CPartFileConvert::ShowGUI()
{
	if (m_convertgui)
		m_convertgui->SetForegroundWindow();
	else {
		m_convertgui = new CPartFileConvertDlg();
		m_convertgui->Create(IDD_CONVERTPARTFILES, CWnd::GetDesktopWindow());//,  );
		InitWindowStyles(m_convertgui);
		m_convertgui->ShowWindow(SW_SHOW);

		AddOrReplaceAnchor(m_convertgui, IDC_CONV_PB_CURRENT, TOP_LEFT, TOP_RIGHT);
		AddOrReplaceAnchor(m_convertgui, IDC_CURJOB, TOP_LEFT, TOP_RIGHT);
		AddOrReplaceAnchor(m_convertgui, IDC_CONV_PB_LABEL, TOP_LEFT, TOP_RIGHT);
		AddOrReplaceAnchor(m_convertgui, IDC_CONV_PROZENT, TOP_RIGHT);
		AddOrReplaceAnchor(m_convertgui, IDC_JOBLIST, TOP_LEFT, BOTTOM_RIGHT);
		AddOrReplaceAnchor(m_convertgui, IDC_ADDITEM, BOTTOM_LEFT);
		AddOrReplaceAnchor(m_convertgui, IDC_RETRY, BOTTOM_LEFT);
		AddOrReplaceAnchor(m_convertgui, IDC_CONVREMOVE, BOTTOM_LEFT);
		AddOrReplaceAnchor(m_convertgui, IDC_HIDECONVDLG, BOTTOM_RIGHT);

		m_convertgui->SetIcon(m_convertgui->m_icoWnd = theApp.LoadIcon(_T("Convert")), FALSE);

		// init GUI
		// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
		m_convertgui->joblist.InsertColumn(0, GetResString(_T("DL_FILENAME")), LVCFMT_LEFT, DFLT_FILENAME_COL_WIDTH);
		m_convertgui->joblist.InsertColumn(1, GetResString(_T("STATUS")), LVCFMT_LEFT, 110);
		m_convertgui->joblist.InsertColumn(2, GetResString(_T("DL_SIZE")), LVCFMT_RIGHT, DFLT_SIZE_COL_WIDTH);
		m_convertgui->joblist.InsertColumn(3, GetResString(_T("FILEHASH")), LVCFMT_LEFT, DFLT_HASH_COL_WIDTH);
		m_convertgui->pb_current.SetRange(0, 100);
		m_convertgui->joblist.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

		if (!pfconverting == NULL) {
			UpdateGUI(pfconverting);
			UpdateGUI(50.0f, GetResString(_T("IMP_FETCHSTATUS")), true);
		}

		Localize();

		// fill job list
		for (POSITION pos = m_jobs.GetHeadPosition(); pos != NULL;) {
			ConvertJob *job = m_jobs.GetNext(pos);
			m_convertgui->AddJob(job);
			UpdateGUI(job);
		}
	}
}

void CPartFileConvert::Localize()
{
	if (!m_convertgui)
		return;


	// set GUI labels
	m_convertgui->SetDlgItemText(IDC_ADDITEM, GetResString(_T("IMP_ADDBTN")));
	m_convertgui->SetDlgItemText(IDC_RETRY, GetResString(_T("IMP_RETRYBTN")));
	m_convertgui->SetDlgItemText(IDC_CONVREMOVE, GetResString(_T("IMP_REMOVEBTN")));
	m_convertgui->SetDlgItemText(IDC_HIDECONVDLG, GetResString(_T("FD_CLOSE")));
	m_convertgui->SetWindowText(GetResString(_T("IMPORTSPLPF")));
}

void CPartFileConvert::CloseGUI()
{
	if (m_convertgui != NULL) {
		m_convertgui->DestroyWindow();
		ClosedGUI();
	}
}

void CPartFileConvert::ClosedGUI()
{
	m_convertgui = NULL;
}

void CPartFileConvert::RemoveAllJobs()
{
	while (!m_jobs.IsEmpty()) {
		ConvertJob *del = m_jobs.RemoveHead();
		if (m_convertgui)
			m_convertgui->RemoveJob(del);
		delete del;
	}
}

void CPartFileConvert::RemoveJob(ConvertJob *job)
{
	POSITION pos = m_jobs.Find(job);
	if (pos) {
		ConvertJob *del = m_jobs.GetAt(pos);
		if (m_convertgui)
			m_convertgui->RemoveJob(del);
		m_jobs.RemoveAt(pos);
		delete del;
	}
}

void CPartFileConvert::RemoveAllSuccJobs()
{
	for (POSITION pos = m_jobs.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		ConvertJob *del = m_jobs.GetNext(pos);
		if (del->state == CONV_OK) {
			if (m_convertgui)
				m_convertgui->RemoveJob(del);
			m_jobs.RemoveAt(pos2);
			delete del;
		}
	}
}

CString CPartFileConvert::GetReturncodeText(int ret)
{
	LPCTSTR uid;
	switch (ret) {
	case CONV_OK:
		uid = _T("DL_TRANSFCOMPL");
		break;
	case CONV_INPROGRESS:
		uid = _T("IMP_INPROGR");
		break;
	case CONV_OUTOFDISKSPACE:
		uid = _T("IMP_ERR_DISKSP");
		break;
	case CONV_PARTMETNOTFOUND:
		uid = _T("IMP_ERR_PARTMETIO");
		if (theApp.IsClosing())
			RemoveAllJobs(); // Ensure any leftover list nodes are deterministically cleared when shutting down
		break;
	case CONV_IOERROR:
		uid = _T("IMP_ERR_IO");
		break;
	case CONV_FAILED:
		uid = _T("IMP_ERR_FAILED");
		break;
	case CONV_QUEUE:
		uid = _T("IMP_STATUSQUEUED");
		break;
	case CONV_ALREADYEXISTS:
		uid = _T("IMP_ALRDWL");
		break;
	case CONV_BADFORMAT:
		uid = _T("IMP_ERR_BADFORMAT");
		break;
	default:
		return CString(_T('?'));
	}
	return GetResString(uid);
}


// Modeless Dialog Implementation
// CPartFileConvertDlg dialog

IMPLEMENT_DYNAMIC(CPartFileConvertDlg, CDialog)

BEGIN_MESSAGE_MAP(CPartFileConvertDlg, CResizableDialog)
	ON_WM_SYSCOLORCHANGE()
	ON_BN_CLICKED(IDC_HIDECONVDLG, OnBnClickedOk)
	ON_BN_CLICKED(IDC_ADDITEM, OnAddFolder)
	ON_BN_CLICKED(IDC_RETRY, RetrySel)
	ON_BN_CLICKED(IDC_CONVREMOVE, RemoveSel)
END_MESSAGE_MAP()

CPartFileConvertDlg::CPartFileConvertDlg(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CPartFileConvertDlg::IDD, pParent)
	, m_pParent(pParent)
	, m_icoWnd()
{
}

CPartFileConvertDlg::~CPartFileConvertDlg()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
}

void CPartFileConvertDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_CONV_PB_CURRENT, pb_current);
	DDX_Control(pDX, IDC_JOBLIST, joblist);
}

// CPartFileConvertDlg message handlers

void CPartFileConvertDlg::OnBnClickedOk()
{
	DestroyWindow();
}

void CPartFileConvertDlg::OnCancel()
{
	DestroyWindow();
}

void CPartFileConvertDlg::PostNcDestroy()
{
	CPartFileConvert::ClosedGUI();

	CResizableDialog::PostNcDestroy();
	delete this;
}

void CPartFileConvertDlg::OnAddFolder()
{
	// browse...
	LPMALLOC pMalloc = NULL;
	if (SHGetMalloc(&pMalloc) == NOERROR) {
		// buffer - a place to hold the file system pathname
		TCHAR buffer[MAX_PATH];

		// This struct holds the various options for the dialog
		BROWSEINFO bi;
		bi.hwndOwner = m_hWnd;
		bi.pidlRoot = NULL;
		bi.pszDisplayName = buffer;
		CString title(GetResString(_T("IMP_SELFOLDER")));
		bi.lpszTitle = title.GetBuffer(title.GetLength());
		bi.ulFlags = BIF_EDITBOX | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON | BIF_SHAREABLE;
		bi.lpfn = NULL;

		// Now cause the dialog to appear.
		LPITEMIDLIST pidlRoot;
		if ((pidlRoot = CDarkMode::BrowseForFolder(bi)) != NULL) {
			int reply;
			if (thePrefs.IsExtControlsEnabled())
				reply = LocMessageBox(_T("IMP_DELSRC"), MB_YESNOCANCEL | MB_DEFBUTTON2, 0);
			else
				reply = IDNO;

			if (reply != IDCANCEL) {
				bool removesrc = (reply == IDYES);

				//
				// Again, almost undocumented. How to get an ASCII pathname
				// from the LPITEMIDLIST struct.
				// I guess you just have to "know" this stuff.
				//
				if (SHGetPathFromIDList(pidlRoot, buffer)) {
					// Do something with the converted string.
					CPartFileConvert::ScanFolderToAdd(CString(buffer), removesrc);
				}
			}

			// Free the returned item identifier list using the
			// shell's task allocator!Arghhhh.
			pMalloc->Free(pidlRoot);
		}
		pMalloc->Release();
	}
}

void CPartFileConvertDlg::UpdateJobInfo(ConvertJob *job)
{
	if (job == NULL) {
		SetDlgItemText(IDC_CURJOB, GetResString(_T("FSTAT_WAITING")));
		SetDlgItemText(IDC_CONV_PROZENT, EMPTY);
		pb_current.SetPos(0);
		SetDlgItemText(IDC_CONV_PB_LABEL, EMPTY);
		return;
	}

	// search job item in listctrl
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)job;
	int iItem = joblist.FindItem(&find);
	if (iItem >= 0) {
		joblist.SetItemText(iItem, 0, job->filename.IsEmpty() ? job->folder : job->filename);
		joblist.SetItemText(iItem, 1, CPartFileConvert::GetReturncodeText(job->state));
		CString buffer;
		if (job->size > 0)
			buffer.Format(GetResString(_T("IMP_SIZE")), (LPCTSTR)CastItoXBytes(job->size), (LPCTSTR)CastItoXBytes(job->spaceneeded));
		joblist.SetItemText(iItem, 2, buffer);
		joblist.SetItemText(iItem, 3, job->filehash);
	}
}

void CPartFileConvertDlg::RemoveJob(ConvertJob *job)
{
	// search jobitem in listctrl
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)job;
	int iItem = joblist.FindItem(&find);
	if (iItem >= 0)
		joblist.DeleteItem(iItem);
}

void CPartFileConvertDlg::AddJob(ConvertJob *job)
{
	int ix = joblist.InsertItem(LVIF_TEXT | LVIF_PARAM, joblist.GetItemCount(), job->folder, 0, 0, 0, (LPARAM)job);
	joblist.SetItemText(ix, 1, CPartFileConvert::GetReturncodeText(job->state));
}

void CPartFileConvertDlg::RemoveSel()
{
	if (joblist.GetSelectedCount() == 0)
		return;

	for (POSITION pos = joblist.GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = joblist.GetNextSelectedItem(pos);
		if (index >= 0) {
			ConvertJob *job = reinterpret_cast<ConvertJob*>(joblist.GetItemData(index));
			if (job->state != CONV_INPROGRESS) {
				CPartFileConvert::RemoveJob(job);
				pos = joblist.GetFirstSelectedItemPosition();
			}
		}
	}
}

void CPartFileConvertDlg::RetrySel()
{
	if (joblist.GetSelectedCount() == 0)
		return;

	for (POSITION pos = joblist.GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = joblist.GetNextSelectedItem(pos);
		if (index >= 0) {
			ConvertJob *job = reinterpret_cast<ConvertJob*>(joblist.GetItemData(index));
			if (job->state != CONV_OK && job->state != CONV_INPROGRESS) {
				UpdateJobInfo(job);
				job->state = CONV_QUEUE;
			}
		}
	}

	CPartFileConvert::StartThread();
}

void CPartFileConvertDlg::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
}
