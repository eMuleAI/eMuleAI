// MuleSystrayDlg.cpp : implementation file
//

#include "stdafx.h"
#include <locale.h>
#include <math.h>
#include "MuleSystrayDlg.h"
#include "emule.h"
#include "preferences.h"
#include "opcodes.h"
#include "otherfunctions.h"
#include "PPgConnection.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	double KBytesPerSecToMbitPerSec(uint32 nKBytesPerSec)
	{
		return (double)nKBytesPerSec * 8192.0 / 1000000.0;
	}

	bool TryParseDisplayDouble(CString strValue, double& rfValue)
	{
		strValue.Trim();
		if (strValue.IsEmpty())
			return false;
		strValue.Replace(_T(','), _T('.'));
		LPCTSTR pszText = strValue;
		LPTSTR pszEnd = NULL;
		_locale_t locale = _create_locale(LC_NUMERIC, "C");
		const double fValue = locale != NULL ? _tcstod_l(pszText, &pszEnd, locale) : _tcstod(pszText, &pszEnd);
		if (locale != NULL)
			_free_locale(locale);
		if (pszEnd == pszText || !_finite(fValue))
			return false;
		CString strTail(pszEnd);
		strTail.Trim();
		if (!strTail.IsEmpty())
			return false;
		rfValue = fValue;
		return true;
	}

	uint32 NumericDisplayToKBytesPerSec(double fValue)
	{
		if (fValue <= 0.0)
			return 0;
		if (!thePrefs.GetForceSpeedsToKB())
			fValue = fValue * 1000000.0 / 8192.0;
		if (fValue >= (double)UNLIMITED)
			return UNLIMITED - 1;
		return (uint32)(fValue + 0.5);
	}

	CString FormatTraySpeedValue(uint32 nKBytesPerSec)
	{
		CString strValue;
		if (thePrefs.GetForceSpeedsToKB())
			strValue.Format(_T("%u"), nKBytesPerSec);
		else {
			const double fMbitPerSec = KBytesPerSecToMbitPerSec(nKBytesPerSec);
			const double fRounded = floor(fMbitPerSec + 0.5);
			if (fabs(fMbitPerSec - fRounded) < 0.05)
				strValue.Format(_T("%.0f"), fMbitPerSec);
			else
				strValue.Format(_T("%.1f"), fMbitPerSec);
		}
		return strValue;
	}

	bool TryParseTraySpeedValue(const CString& rstrValue, uint32& rnKBytesPerSec)
	{
		double fValue = 0.0;
		if (!TryParseDisplayDouble(rstrValue, fValue))
			return false;
		rnKBytesPerSec = NumericDisplayToKBytesPerSec(fValue);
		return true;
	}
}


//Cax2 - new class without context menu
BEGIN_MESSAGE_MAP(CInputBox, CEdit)
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

void CInputBox::OnContextMenu(CWnd*, CPoint)
{
	//Cax2 - nothing to see here!
}

/////////////////////////////////////////////////////////////////////////////
// CMuleSystrayDlg dialog

CMuleSystrayDlg::CMuleSystrayDlg(CWnd *pParent, CPoint pt, int iMaxUp, int iMaxDown, int iCurUp, int iCurDown)
	: CDialog(CMuleSystrayDlg::IDD, pParent)
	, m_bClosingDown()
	, m_iMaxUp(iMaxUp)
	, m_iMaxDown(iMaxDown)
	, m_ptInitialPosition(pt)
	, m_hUpArrow()
	, m_hDownArrow()
	, m_nExitCode()
	, m_bUpdatingControls(false)
{
	if (iCurDown == UNLIMITED)
		iCurDown = 0;
	if (iCurUp == UNLIMITED)
		iCurUp = 0;

	m_nDownSpeedTxt = iMaxDown < iCurDown ? iMaxDown : iCurDown;
	m_nUpSpeedTxt = iMaxUp < iCurUp ? iMaxUp : iCurUp;
	//}}AFX_DATA_INIT
}

CMuleSystrayDlg::~CMuleSystrayDlg()
{
	if (m_hUpArrow)
		::DestroyIcon(m_hUpArrow);
	if (m_hDownArrow)
		::DestroyIcon(m_hDownArrow);
}

void CMuleSystrayDlg::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TRAYUP, m_ctrlUpArrow);
	DDX_Control(pDX, IDC_TRAYDOWN, m_ctrlDownArrow);
	DDX_Control(pDX, IDC_SIDEBAR, m_ctrlSidebar);
	DDX_Control(pDX, IDC_UPSLD, m_ctrlUpSpeedSld);
	DDX_Control(pDX, IDC_DOWNSLD, m_ctrlDownSpeedSld);
	DDX_Control(pDX, IDC_DOWNTXT, m_DownSpeedInput);
	DDX_Control(pDX, IDC_UPTXT, m_UpSpeedInput);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMuleSystrayDlg, CDialog)
	ON_WM_MOUSEMOVE()
	ON_EN_CHANGE(IDC_DOWNTXT, OnChangeDowntxt)
	ON_EN_CHANGE(IDC_UPTXT, OnChangeUptxt)
	ON_WM_HSCROLL()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_KILLFOCUS()
	ON_WM_SHOWWINDOW()
	ON_WM_CAPTURECHANGED()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CMuleSystrayDlg message handlers

void CMuleSystrayDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	CWnd *pWnd = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
	if (pWnd) {
		if (pWnd == this || pWnd == &m_ctrlSidebar)
			SetCapture();			// me, myself and i
		else
			::ReleaseCapture();		// sweet child of mine
	} else
		SetCapture();				// I'm on the outside, I'm looking in...

	CDialog::OnMouseMove(nFlags, point);
}

BOOL CMuleSystrayDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_bClosingDown = false;

	m_hUpArrow = theApp.LoadIcon(_T("UPLOAD"));
	m_hDownArrow = theApp.LoadIcon(_T("DOWNLOAD"));
	m_ctrlUpArrow.SetIcon(m_hUpArrow);
	m_ctrlDownArrow.SetIcon(m_hDownArrow);

	LOGFONT lfStaticFont;

	CWnd *p = GetDlgItem(IDC_SPEED);
	bool bValidFont = (p != NULL);

	CRect r;
	if (p) {
		bValidFont = (p->GetFont()->GetLogFont(&lfStaticFont) != 0);
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlSpeed.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_SPEED);
		m_ctrlSpeed.m_nBtnID = IDC_SPEED;
		m_ctrlSpeed.m_strText = GetResNoAmp(_T("TRAYDLG_SPEED"));

		m_ctrlSpeed.m_bUseIcon = true;
		m_ctrlSpeed.m_sIcon.cx = 16;
		m_ctrlSpeed.m_sIcon.cy = 16;
		m_ctrlSpeed.m_hIcon = theApp.LoadIcon(_T("SPEED"), m_ctrlSpeed.m_sIcon.cx, m_ctrlSpeed.m_sIcon.cy);
		m_ctrlSpeed.m_bParentCapture = true;
		m_ctrlSpeed.m_bNoHover = true;
		if (bValidFont) {
			LOGFONT lfFont = lfStaticFont;
			lfFont.lfWeight += 200;		// make it bold
			m_ctrlSpeed.m_cfFont.CreateFontIndirect(&lfFont);
		}
	}

	p = GetDlgItem(IDC_TOMAX);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlAllToMax.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_TOMAX);
		m_ctrlAllToMax.m_nBtnID = IDC_TOMAX;
		m_ctrlAllToMax.m_strText = GetResNoAmp(_T("PW_UA"));

		m_ctrlAllToMax.m_bUseIcon = true;
		m_ctrlAllToMax.m_sIcon.cx = 16;
		m_ctrlAllToMax.m_sIcon.cy = 16;
		m_ctrlAllToMax.m_hIcon = theApp.LoadIcon(_T("SPEEDMAX"), m_ctrlAllToMax.m_sIcon.cx, m_ctrlAllToMax.m_sIcon.cy);
		m_ctrlAllToMax.m_bParentCapture = true;
		if (bValidFont)
			m_ctrlAllToMax.m_cfFont.CreateFontIndirect(&lfStaticFont);
	}

	p = GetDlgItem(IDC_TOMIN);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlAllToMin.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_TOMIN);
		m_ctrlAllToMin.m_nBtnID = IDC_TOMIN;
		m_ctrlAllToMin.m_strText = GetResNoAmp(_T("PW_PA"));

		m_ctrlAllToMin.m_bUseIcon = true;
		m_ctrlAllToMin.m_sIcon.cx = 16;
		m_ctrlAllToMin.m_sIcon.cy = 16;
		m_ctrlAllToMin.m_hIcon = theApp.LoadIcon(_T("SPEEDMIN"), m_ctrlAllToMin.m_sIcon.cx, m_ctrlAllToMin.m_sIcon.cy);
		m_ctrlAllToMin.m_bParentCapture = true;
		if (bValidFont)
			m_ctrlAllToMin.m_cfFont.CreateFontIndirect(&lfStaticFont);
	}

	p = GetDlgItem(IDC_RESTORE);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlRestore.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_RESTORE);
		m_ctrlRestore.m_nBtnID = IDC_RESTORE;
		m_ctrlRestore.m_strText = GetResNoAmp(_T("MAIN_POPUP_RESTORE"));

		m_ctrlRestore.m_bUseIcon = true;
		m_ctrlRestore.m_sIcon.cx = 16;
		m_ctrlRestore.m_sIcon.cy = 16;
		m_ctrlRestore.m_hIcon = theApp.LoadIcon(_T("RESTOREWINDOW"), m_ctrlRestore.m_sIcon.cx, m_ctrlRestore.m_sIcon.cy);
		m_ctrlRestore.m_bParentCapture = true;
		if (bValidFont) {
			LOGFONT lfFont = lfStaticFont;
			lfFont.lfWeight += 200;		// make it bold
			m_ctrlRestore.m_cfFont.CreateFontIndirect(&lfFont);
		}
	}

	p = GetDlgItem(IDC_CONNECT);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlConnect.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_CONNECT);
		m_ctrlConnect.m_nBtnID = IDC_CONNECT;
		m_ctrlConnect.m_strText = GetResNoAmp(_T("MAIN_BTN_CONNECT"));

		m_ctrlConnect.m_bUseIcon = true;
		m_ctrlConnect.m_sIcon.cx = 16;
		m_ctrlConnect.m_sIcon.cy = 16;
		m_ctrlConnect.m_hIcon = theApp.LoadIcon(_T("CONNECT"), m_ctrlConnect.m_sIcon.cx, m_ctrlConnect.m_sIcon.cy);
		m_ctrlConnect.m_bParentCapture = true;
		if (bValidFont)
			m_ctrlConnect.m_cfFont.CreateFontIndirect(&lfStaticFont);
	}

	p = GetDlgItem(IDC_DISCONNECT);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlDisconnect.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_DISCONNECT);
		m_ctrlDisconnect.m_nBtnID = IDC_DISCONNECT;
		m_ctrlDisconnect.m_strText = GetResNoAmp(_T("MAIN_BTN_DISCONNECT"));

		m_ctrlDisconnect.m_bUseIcon = true;
		m_ctrlDisconnect.m_sIcon.cx = 16;
		m_ctrlDisconnect.m_sIcon.cy = 16;
		m_ctrlDisconnect.m_hIcon = theApp.LoadIcon(_T("DISCONNECT"), m_ctrlDisconnect.m_sIcon.cx, m_ctrlDisconnect.m_sIcon.cy);
		m_ctrlDisconnect.m_bParentCapture = true;
		if (bValidFont)
			m_ctrlDisconnect.m_cfFont.CreateFontIndirect(&lfStaticFont);
	}

	p = GetDlgItem(IDC_PREFERENCES);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlPreferences.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_PREFERENCES);
		m_ctrlPreferences.m_nBtnID = IDC_PREFERENCES;
		m_ctrlPreferences.m_strText = GetResNoAmp(_T("EM_PREFS"));

		m_ctrlPreferences.m_bUseIcon = true;
		m_ctrlPreferences.m_sIcon.cx = 16;
		m_ctrlPreferences.m_sIcon.cy = 16;
		m_ctrlPreferences.m_hIcon = theApp.LoadIcon(_T("Preferences"), m_ctrlPreferences.m_sIcon.cx, m_ctrlPreferences.m_sIcon.cy);
		m_ctrlPreferences.m_bParentCapture = true;
		if (bValidFont)
			m_ctrlPreferences.m_cfFont.CreateFontIndirect(&lfStaticFont);
	}

	p = GetDlgItem(IDC_TRAY_EXIT);
	if (p) {
		p->GetWindowRect(r);
		ScreenToClient(r);
		m_ctrlExit.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, r, this, IDC_EXIT);
		m_ctrlExit.m_nBtnID = IDC_EXIT;
		m_ctrlExit.m_strText = GetResNoAmp(_T("EXIT"));

		m_ctrlExit.m_bUseIcon = true;
		m_ctrlExit.m_sIcon.cx = 16;
		m_ctrlExit.m_sIcon.cy = 16;
		m_ctrlExit.m_hIcon = theApp.LoadIcon(_T("EXIT"), m_ctrlExit.m_sIcon.cx, m_ctrlExit.m_sIcon.cy);
		m_ctrlExit.m_bParentCapture = true;
		if (bValidFont)
			m_ctrlExit.m_cfFont.CreateFontIndirect(&lfStaticFont);
	}

	SetDlgItemText(IDC_DOWNLBL, GetResString(_T("PW_CON_DOWNLBL")));
	SetDlgItemText(IDC_UPLBL, GetResString(_T("PW_CON_UPLBL")));
	const CString strSpeedUnit(GetResString(thePrefs.GetForceSpeedsToKB() ? _T("KBYTESPERSEC") : _T("MBITSSEC")));
	SetDlgItemText(IDC_DOWNKB, strSpeedUnit);
	SetDlgItemText(IDC_UPKB, strSpeedUnit);

	m_ctrlDownSpeedSld.SetRange(0, m_iMaxDown);
	m_ctrlDownSpeedSld.SetPos(m_nDownSpeedTxt);

	m_ctrlUpSpeedSld.SetRange(0, m_iMaxUp);
	m_ctrlUpSpeedSld.SetPos(m_nUpSpeedTxt);

	m_DownSpeedInput.EnableWindow(m_nDownSpeedTxt > 0);
	m_UpSpeedInput.EnableWindow(m_nUpSpeedTxt > 0);
	UpdateSpeedTextControls();

	CFont Font;
	Font.CreateFont(-16, 0, 900, 0, 700, 0, 0, 0, 0, 3, 2, 1, 34, _T("Tahoma"));

	UINT winver = thePrefs.GetWindowsVersion();
	int iClr = (winver == _WINVER_95_ || winver == _WINVER_NT4_ || g_bLowColorDesktop)
		? COLOR_ACTIVECAPTION : COLOR_GRADIENTACTIVECAPTION;
	m_ctrlSidebar.SetColors(GetCustomSysColor(COLOR_CAPTIONTEXT), GetCustomSysColor(COLOR_ACTIVECAPTION), GetCustomSysColor(iClr));

	m_ctrlSidebar.SetHorizontal(false);
	m_ctrlSidebar.SetFont(&Font);
	m_ctrlSidebar.SetWindowText(theApp.GetAppVersion());

	CWnd *pDesktopWnd = GetDesktopWindow();
	RECT rDesktop;
	pDesktopWnd->GetClientRect(&rDesktop);

	CPoint pt(m_ptInitialPosition);
	pDesktopWnd->ScreenToClient(&pt);

	GetWindowRect(r);
	if (m_ptInitialPosition.x + r.Width() >= rDesktop.right)
		pt.x -= r.Width();
	if (m_ptInitialPosition.y - r.Height() >= rDesktop.top)
		pt.y -= r.Height();

	MoveWindow(pt.x, pt.y, r.Width(), r.Height());
	SetCapture();
	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CMuleSystrayDlg::SetSpeedText(UINT nID, uint32 nKBytesPerSec)
{
	const bool bOldUpdatingControls = m_bUpdatingControls;
	m_bUpdatingControls = true;
	SetDlgItemText(nID, FormatTraySpeedValue(nKBytesPerSec));
	m_bUpdatingControls = bOldUpdatingControls;
}

bool CMuleSystrayDlg::TryGetSpeedText(UINT nID, uint32& rnKBytesPerSec) const
{
	CString strValue;
	GetDlgItemText(nID, strValue);
	return TryParseTraySpeedValue(strValue, rnKBytesPerSec);
}

void CMuleSystrayDlg::UpdateSpeedTextControls()
{
	SetSpeedText(IDC_DOWNTXT, m_nDownSpeedTxt);
	SetSpeedText(IDC_UPTXT, m_nUpSpeedTxt);
}

bool CMuleSystrayDlg::UpdateSpeedFromText(UINT nID, bool bCommitText)
{
	uint32 nKBytesPerSec = 0;
	if (!TryGetSpeedText(nID, nKBytesPerSec)) {
		if (bCommitText)
			SetSpeedText(nID, nID == IDC_DOWNTXT ? m_nDownSpeedTxt : m_nUpSpeedTxt);
		return false;
	}

	const uint32 nEnteredKBytesPerSec = nKBytesPerSec;
	const uint32 nMaxKBytesPerSec = nID == IDC_DOWNTXT ? (m_iMaxDown > 0 ? (uint32)m_iMaxDown : 0) : (m_iMaxUp > 0 ? (uint32)m_iMaxUp : 0);
	if (nMaxKBytesPerSec > 0 && nKBytesPerSec > nMaxKBytesPerSec)
		nKBytesPerSec = nMaxKBytesPerSec;

	if (nID == IDC_DOWNTXT) {
		m_nDownSpeedTxt = nKBytesPerSec;
		m_ctrlDownSpeedSld.SetPos(m_nDownSpeedTxt);
		thePrefs.SetMaxDownload(m_nDownSpeedTxt ? m_nDownSpeedTxt : UNLIMITED);
	} else {
		m_nUpSpeedTxt = nKBytesPerSec;
		m_ctrlUpSpeedSld.SetPos(m_nUpSpeedTxt);
		thePrefs.SetMaxUpload(m_nUpSpeedTxt ? m_nUpSpeedTxt : UNLIMITED);
	}

	if (bCommitText || nKBytesPerSec != nEnteredKBytesPerSec)
		SetSpeedText(nID, nID == IDC_DOWNTXT ? m_nDownSpeedTxt : m_nUpSpeedTxt);

	return true;
}

void CMuleSystrayDlg::CommitSpeedTextControls()
{
	UpdateSpeedFromText(IDC_DOWNTXT, true);
	UpdateSpeedFromText(IDC_UPTXT, true);
}

void CMuleSystrayDlg::OnChangeDowntxt()
{
	if (m_bUpdatingControls)
		return;
	UpdateSpeedFromText(IDC_DOWNTXT, false);
}

void CMuleSystrayDlg::OnChangeUptxt()
{
	if (m_bUpdatingControls)
		return;
	UpdateSpeedFromText(IDC_UPTXT, false);
}

void CMuleSystrayDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	m_nUpSpeedTxt = m_ctrlUpSpeedSld.GetPos();
	m_nDownSpeedTxt = m_ctrlDownSpeedSld.GetPos();

	if (pScrollBar->GetSafeHwnd() == m_ctrlUpSpeedSld.m_hWnd) {
		if (CPPgConnection::CheckUp(m_nUpSpeedTxt, m_nDownSpeedTxt)) {
			if (CPPgConnection::CheckDown(m_nUpSpeedTxt, m_nDownSpeedTxt))
				m_ctrlUpSpeedSld.SetPos(m_nUpSpeedTxt);
			m_ctrlDownSpeedSld.SetPos(m_nDownSpeedTxt);
		}
		UpdateSpeedTextControls();
		thePrefs.SetMaxDownload((m_nDownSpeedTxt == 0) ? UNLIMITED : m_nDownSpeedTxt);
	} else { /*if (hWnd == m_ctrlDownSpeedSld.m_hWnd) { */
		if (CPPgConnection::CheckDown(m_nUpSpeedTxt, m_nDownSpeedTxt)) {
			if (CPPgConnection::CheckUp(m_nUpSpeedTxt, m_nDownSpeedTxt))
				m_ctrlDownSpeedSld.SetPos(m_nDownSpeedTxt);
			m_ctrlUpSpeedSld.SetPos(m_nUpSpeedTxt);
		}
		UpdateSpeedTextControls();
		thePrefs.SetMaxUpload((m_nUpSpeedTxt == 0) ? UNLIMITED : m_nUpSpeedTxt);
	}

	m_UpSpeedInput.EnableWindow(m_nUpSpeedTxt > 0);
	m_DownSpeedInput.EnableWindow(m_nDownSpeedTxt > 0);

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CMuleSystrayDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	::ReleaseCapture();
	EndDialog(m_nExitCode);
	m_bClosingDown = true;

	CDialog::OnLButtonUp(nFlags, point);
}

//bond006: systray menu gets stuck (bugfix)
void CMuleSystrayDlg::OnRButtonDown(UINT nFlags, CPoint point)
{
	CRect systrayRect;
	GetClientRect(&systrayRect);
	if (!systrayRect.PtInRect(point)) {
		::ReleaseCapture();
		EndDialog(m_nExitCode);
		m_bClosingDown = true;
	}

	CDialog::OnRButtonDown(nFlags, point);
}

void CMuleSystrayDlg::OnKillFocus(CWnd *pNewWnd)
{
	CDialog::OnKillFocus(pNewWnd);

	if (!m_bClosingDown) {
		::ReleaseCapture();
		EndDialog(m_nExitCode);
		m_bClosingDown = true;
	}
}

void CMuleSystrayDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if (!bShow && !m_bClosingDown) {
		::ReleaseCapture();
		EndDialog(m_nExitCode);
		m_bClosingDown = true;
	}

	CDialog::OnShowWindow(bShow, nStatus);

	ApplyTheme(GetSafeHwnd());
}

void CMuleSystrayDlg::OnCaptureChanged(CWnd *pWnd)
{
	if (pWnd && pWnd != this && !IsChild(pWnd)) {
		EndDialog(m_nExitCode);
		m_bClosingDown = true;
	}
	CDialog::OnCaptureChanged(pWnd);
}

void CMuleSystrayDlg::OnOK()
{
	CommitSpeedTextControls();
	::ReleaseCapture();
	EndDialog(m_nExitCode);
	m_bClosingDown = true;
}

BOOL CMuleSystrayDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (LOWORD(wParam) == IDOK) {
		OnOK();
		return TRUE;
	}
	if (HIWORD(wParam) == BN_CLICKED) {
		::ReleaseCapture();
		m_nExitCode = LOWORD(wParam);
		EndDialog(m_nExitCode);
		m_bClosingDown = true;
	}
	return CDialog::OnCommand(wParam, lParam);
}