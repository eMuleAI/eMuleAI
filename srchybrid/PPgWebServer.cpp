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
#include "PPgWebServer.h"
#include "otherfunctions.h"
#include "WebServer.h"
#include "emuledlg.h"
#include "Preferences.h"
#include "ServerWnd.h"
#include "HelpIDs.h"
#include "ppgwebserver.h"
#include "UPnPImplWrapper.h"
#include "UPnPImpl.h"
#include "Log.h"
#include "TLSthreading.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

struct options
{
	LPCTSTR	issuer_key;		//filename of the issuer key file
	LPCTSTR cert_file;		//where to store the constructed certificate file
	LPCSTR	subject_name;	//subject name for certificate
	LPCSTR	issuer_name;	//issuer name for certificate
	LPCSTR	not_before;		//validity period not before
	LPCSTR	not_after;		//validity period not after
	uint16	serial;			//serial number string
};

static int write_buffer(LPCTSTR output_file, const unsigned char *buffer)
{
	FILE *f = _tfopen(output_file, _T("wb"));
	if (f == NULL)
		return -1;
	size_t len = strlen((char*)buffer);
	if (fwrite((void*)buffer, 1, len, f) != len) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int write_private_key(const mbedtls_pk_context *key, LPCTSTR output_file)
{
	unsigned char output_buf[16000];

	int ret = mbedtls_pk_write_key_pem(key, output_buf, sizeof(output_buf));
	return ret ? ret : write_buffer(output_file, output_buf);
}
//create RSA 2048 key

int KeyCreate(mbedtls_pk_context *key, LPCTSTR output_file)
{
	LPCTSTR pmsg = NULL;
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_algorithm(&attr, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH));
	psa_set_key_type(&attr, PSA_KEY_TYPE_RSA_KEY_PAIR);
	psa_set_key_bits(&attr, 2048);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT);

	mbedtls_svc_key_id_t key_id;
	int ret = (int)psa_generate_key(&attr, &key_id);
	psa_reset_key_attributes(&attr);
	if (ret)
		pmsg = _T("psa_generate_key");
	else {
		ret = mbedtls_pk_wrap_psa(key, key_id);
		if (ret)
			pmsg = _T("mbedtls_pk_wrap_psa");
		else {
			ret = write_private_key(key, output_file);	//write the key to a file
			if (ret)
				pmsg = _T("write_private_key");
		}
	}

	if (pmsg)
		DebugLogError(_T("Error: %s returned -0x%04x - %s"), pmsg, -ret, (LPCTSTR)EscPercent(SSLerror(ret)));
	return ret;
}

int write_certificate(mbedtls_x509write_cert *crt, LPCTSTR output_file)
{
	unsigned char output_buf[4096];

	int ret = mbedtls_x509write_crt_pem(crt, output_buf, sizeof output_buf);
	return ret ? ret : write_buffer(output_file, output_buf);
}

int CertCreate(const struct options &opt)
{
	mbedtls_pk_context issuer_key;
	mbedtls_x509write_cert crt;
	LPCTSTR pmsg = NULL;

	mbedtls_threading_set_alt(threading_mutex_init_alt, threading_mutex_destroy_alt, threading_mutex_lock_alt, threading_mutex_unlock_alt
		, cond_init_alt, cond_destroy_alt, cond_signal_alt, cond_broadcast_alt, cond_wait_alt);
	mbedtls_x509write_crt_init(&crt);
	mbedtls_pk_init(&issuer_key);
	int ret = (int)psa_crypto_init();
	if (ret) {
		pmsg = _T("psa_crypto_init");
		goto exit;
	}

	//generate the key
	ret = KeyCreate(&issuer_key, opt.issuer_key);
	if (ret)
		goto exit; //the bug already was reported

	mbedtls_x509write_crt_set_subject_key(&crt, &issuer_key);
	mbedtls_x509write_crt_set_issuer_key(&crt, &issuer_key);


	//set parameters
	ret = mbedtls_x509write_crt_set_subject_name(&crt, opt.subject_name);
	if (ret) {
		pmsg = _T("mbedtls_x509write_crt_set_subject_name");
		goto exit;
	}
	ret = mbedtls_x509write_crt_set_issuer_name(&crt, opt.issuer_name);
	if (ret) {
		pmsg = _T("mbedtls_x509write_crt_set_issuer_name");
		goto exit;
	}

	mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

	ret = mbedtls_x509write_crt_set_serial_raw(&crt, (unsigned char*)&opt.serial, 1 + static_cast<size_t>(opt.serial > 0xff));
	if (ret) {
		pmsg = _T("mbedtls_x509write_crt_set_serial_raw");
		goto exit;
	}
	ret = mbedtls_x509write_crt_set_validity(&crt, opt.not_before, opt.not_after);
	if (ret) {
		pmsg = _T("mbedtls_x509write_crt_set_validity");
		goto exit;
	}
	ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, 0);
	if (ret) {
		pmsg = _T("x509write_crt_set_basic_contraints");
		goto exit;
	}
	ret = mbedtls_x509write_crt_set_subject_key_identifier(&crt);
	if (ret) {
		pmsg = _T("mbedtls_x509write_crt_set_subject_key_identifier");
		goto exit;
	}
	ret = mbedtls_x509write_crt_set_authority_key_identifier(&crt);
	if (ret) {
		pmsg = _T("mbedtls_x509write_crt_set_authority_key_identifier");
		goto exit;
	}

	//write the certificate to a file
	ret = write_certificate(&crt, opt.cert_file);
	if (ret)
		pmsg = _T("write_certificate");

exit:
	mbedtls_pk_free(&issuer_key);
	mbedtls_x509write_crt_free(&crt);
	mbedtls_psa_crypto_free();
	mbedtls_threading_free_alt();

	if (pmsg)
		DebugLogError(_T("Error: %s returned -0x%04x - %s"), pmsg, -ret, (LPCTSTR)EscPercent(SSLerror(ret)));
	return ret;
}

IMPLEMENT_DYNAMIC(CPPgWebServer, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgWebServer, CPropertyPage)
	ON_EN_CHANGE(IDC_WSPASS, OnDataChange)
	ON_EN_CHANGE(IDC_WSPASSLOW, OnDataChange)
	ON_EN_CHANGE(IDC_WSPORT, OnDataChange)
	ON_EN_CHANGE(IDC_TMPLPATH, OnDataChange)
	ON_EN_CHANGE(IDC_CERTPATH, OnDataChange)
	ON_EN_CHANGE(IDC_KEYPATH, OnDataChange)
	ON_EN_CHANGE(IDC_WSTIMEOUT, OnDataChange)
	ON_BN_CLICKED(IDC_WSENABLED, OnEnChangeWSEnabled)
	ON_BN_CLICKED(IDC_WEB_HTTPS, OnChangeHTTPS)
	ON_BN_CLICKED(IDC_WEB_GENERATE, OnGenerateCertificate)
	ON_BN_CLICKED(IDC_WSENABLEDLOW, OnEnChangeWSEnabled)
	ON_BN_CLICKED(IDC_WSRELOADTMPL, OnReloadTemplates)
	ON_BN_CLICKED(IDC_TMPLBROWSE, OnBnClickedTmplbrowse)
	ON_BN_CLICKED(IDC_CERTBROWSE, OnBnClickedCertbrowse)
	ON_BN_CLICKED(IDC_KEYBROWSE, OnBnClickedKeybrowse)
	ON_BN_CLICKED(IDC_WS_GZIP, OnDataChange)
	ON_BN_CLICKED(IDC_WS_ALLOWHILEVFUNC, OnDataChange)
	ON_BN_CLICKED(IDC_WSUPNP, OnDataChange)
	ON_WM_HELPINFO()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgWebServer::CPPgWebServer()
	: CPropertyPage(CPPgWebServer::IDD)
	, m_generating()
	, m_bNewCert()
	, m_bModified()
	, m_icoBrowse()
{
}

void CPPgWebServer::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BOOL CPPgWebServer::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	AddBuddyButton(GetDlgItem(IDC_TMPLPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_TMPLBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_TMPLBROWSE), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_CERTPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_CERTBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_CERTBROWSE), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_KEYPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_KEYBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_KEYBROWSE), m_icoBrowse);

	static_cast<CEdit*>(GetDlgItem(IDC_WSPASS))->SetLimitText(32);
	static_cast<CEdit*>(GetDlgItem(IDC_WSPASSLOW))->SetLimitText(32);
	static_cast<CEdit*>(GetDlgItem(IDC_WSPORT))->SetLimitText(5);

	LoadSettings();
	Localize();

	OnEnChangeWSEnabled();

	return TRUE;
}

void CPPgWebServer::LoadSettings()
{
	CheckDlgButton(IDC_WSENABLED, static_cast<UINT>(thePrefs.GetWSIsEnabled()));
	CheckDlgButton(IDC_WS_GZIP, static_cast<UINT>(thePrefs.GetWebUseGzip()));

	CheckDlgButton(IDC_WSUPNP, static_cast<UINT>(thePrefs.m_bWebUseUPnP));
	GetDlgItem(IDC_WSUPNP)->EnableWindow(thePrefs.IsUPnPEnabled() && thePrefs.GetWSIsEnabled());
	SetDlgItemInt(IDC_WSPORT, thePrefs.GetWSPort());

	SetDlgItemText(IDC_TMPLPATH, thePrefs.GetTemplate());
	SetDlgItemInt(IDC_WSTIMEOUT, thePrefs.GetWebTimeoutMins());

	CheckDlgButton(IDC_WEB_HTTPS, static_cast<UINT>(thePrefs.GetWebUseHttps()));
	SetDlgItemText(IDC_CERTPATH, thePrefs.GetWebCertPath());
	SetDlgItemText(IDC_KEYPATH, thePrefs.GetWebKeyPath());

	SetDlgItemText(IDC_WSPASS, sHiddenPassword);
	CheckDlgButton(IDC_WS_ALLOWHILEVFUNC, static_cast<UINT>(thePrefs.GetWebAdminAllowedHiLevFunc()));
	CheckDlgButton(IDC_WSENABLEDLOW, static_cast<UINT>(thePrefs.GetWSIsLowUserEnabled()));
	SetDlgItemText(IDC_WSPASSLOW, sHiddenPassword);

	SetModified(FALSE);	// FoRcHa
}

void CPPgWebServer::OnDataChange()
{
	SetModified();
	SetTmplButtonState();
}

BOOL CPPgWebServer::OnApply()
{
	if (m_bModified) {
		bool bUPnP = thePrefs.GetWSUseUPnP();
		bool bWSIsEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
		// get and check template file existence...
		CString sBuf;
		GetDlgItemText(IDC_TMPLPATH, sBuf);
		if (bWSIsEnabled && !::PathFileExists(sBuf)) {
			CString buffer;
			buffer.Format(GetResString(_T("WEB_ERR_CANTLOAD")), (LPCTSTR)sBuf);
			CDarkMode::MessageBox(buffer, MB_OK);
			return FALSE;
		}
		thePrefs.SetTemplate(sBuf);
		if (!theApp.webserver->ReloadTemplates()) {
			GetDlgItem(IDC_TMPLPATH)->SetFocus();
			return FALSE;
		}

		bool bHTTPS = IsDlgButtonChecked(IDC_WEB_HTTPS) != 0;
		GetDlgItemText(IDC_CERTPATH, sBuf);
		if (bWSIsEnabled && bHTTPS) {
			if (!::PathFileExists(sBuf)) {
				CDarkMode::MessageBox(GetResString(_T("CERT_NOT_FOUND")), MB_OK);
				return FALSE;
			}
			if (!m_bNewCert)
				m_bNewCert = !thePrefs.GetWebCertPath().CompareNoCase(sBuf);
		}
		thePrefs.SetWebCertPath(sBuf);

		GetDlgItemText(IDC_KEYPATH, sBuf);
		if (bWSIsEnabled && bHTTPS) {
			if (!::PathFileExists(sBuf)) {
				CDarkMode::MessageBox(GetResString(_T("KEY_NOT_FOUND")), MB_OK);
				return FALSE;
			}
			if (!m_bNewCert)
				m_bNewCert = !thePrefs.GetWebKeyPath().CompareNoCase(sBuf);
		}
		thePrefs.SetWebKeyPath(sBuf);

		GetDlgItemText(IDC_WSPASS, sBuf);
		if (sBuf != sHiddenPassword) {
			thePrefs.SetWSPass(sBuf);
			SetDlgItemText(IDC_WSPASS, sHiddenPassword);
		}

		GetDlgItemText(IDC_WSPASSLOW, sBuf);
		if (sBuf != sHiddenPassword) {
			thePrefs.SetWSLowPass(sBuf);
			SetDlgItemText(IDC_WSPASSLOW, sHiddenPassword);
		}

		thePrefs.m_iWebTimeoutMins = (int)GetDlgItemInt(IDC_WSTIMEOUT, NULL, FALSE);

		uint16 u = (uint16)GetDlgItemInt(IDC_WSPORT, NULL, FALSE);
		if (u > 0 && u != thePrefs.GetWSPort()) {
			thePrefs.SetWSPort(u);
			theApp.webserver->RestartSockets();
		}

		if (thePrefs.GetWebUseHttps() != bHTTPS || (bHTTPS && m_bNewCert))
			theApp.webserver->StopServer();
		m_bNewCert = false;

		thePrefs.SetWSIsEnabled(bWSIsEnabled);
		thePrefs.SetWebUseGzip(IsDlgButtonChecked(IDC_WS_GZIP) != 0);
		thePrefs.SetWebUseHttps(bHTTPS);
		thePrefs.SetWSIsLowUserEnabled(IsDlgButtonChecked(IDC_WSENABLEDLOW) != 0);
		theApp.webserver->StartServer();
		thePrefs.m_bAllowAdminHiLevFunc = IsDlgButtonChecked(IDC_WS_ALLOWHILEVFUNC) != 0;

		thePrefs.m_bWebUseUPnP = IsDlgButtonChecked(IDC_WSUPNP) != 0;
		//add the port to existing mapping without having eMule restarting (if all conditions are met)
		if (bUPnP != (thePrefs.m_bWebUseUPnP && bWSIsEnabled) && thePrefs.IsUPnPEnabled() && theApp.m_pUPnPFinder != NULL)
			theApp.m_pUPnPFinder->GetImplementation()->LateEnableWebServerPort(bUPnP ? 0 : thePrefs.GetWSPort());

		theApp.emuledlg->serverwnd->UpdateMyInfo();
		SetModified(FALSE);
		SetTmplButtonState();
	}

	return CPropertyPage::OnApply();
}

void CPPgWebServer::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_WS")));

		SetDlgItemText(IDC_WSENABLED, GetResString(_T("ENABLED")));
		SetDlgItemText(IDC_WS_GZIP, GetResString(_T("WEB_GZIP_COMPRESSION")));
		SetDlgItemText(IDC_WSUPNP, GetResString(_T("WEBUPNPINCLUDE")));
		SetDlgItemText(IDC_WSPORT_LBL, GetResString(_T("PORT")) + _T(':'));

		SetDlgItemText(IDC_TEMPLATE, GetResString(_T("WS_RELOAD_TMPL")) + _T(':'));
		SetDlgItemText(IDC_WSRELOADTMPL, GetResString(_T("SF_RELOAD")));

		SetDlgItemText(IDC_STATIC_GENERAL, GetResString(_T("PW_GENERAL")));

		SetDlgItemText(IDC_WSTIMEOUTLABEL, GetResString(_T("WEB_SESSIONTIMEOUT")) + _T(':'));
		SetDlgItemText(IDC_MINS, GetResString(_T("LONGMINS")).MakeLower());

		SetDlgItemText(IDC_WEB_HTTPS, GetResString(_T("WEB_HTTPS")));
		SetDlgItemText(IDC_WEB_GENERATE, GetResString(_T("WEB_GENERATE")));
		SetDlgItemText(IDC_WEB_CERT, GetResString(_T("CERTIFICATE")) + _T(':'));
		SetDlgItemText(IDC_WEB_KEY, GetResString(_T("KEY")) + _T(':'));

		SetDlgItemText(IDC_STATIC_ADMIN, GetResString(_T("ADMIN")));
		SetDlgItemText(IDC_WSPASS_LBL, GetResString(_T("WS_PASS")) + _T(':'));
		SetDlgItemText(IDC_WS_ALLOWHILEVFUNC, GetResString(_T("WEB_ALLOWHILEVFUNC")));

		SetDlgItemText(IDC_STATIC_LOWUSER, GetResString(_T("WEB_LOWUSER")));
		SetDlgItemText(IDC_WSENABLEDLOW, GetResString(_T("ENABLED")));
		SetDlgItemText(IDC_WSPASS_LBL2, GetResString(_T("WS_PASS")) + _T(':'));
	}
}

void CPPgWebServer::SetUPnPState()
{
	GetDlgItem(IDC_WSUPNP)->EnableWindow(thePrefs.IsUPnPEnabled() && IsDlgButtonChecked(IDC_WSENABLED));
}

void CPPgWebServer::OnChangeHTTPS()
{
	BOOL bEnable = IsDlgButtonChecked(IDC_WSENABLED) && IsDlgButtonChecked(IDC_WEB_HTTPS);
	GetDlgItem(IDC_WEB_GENERATE)->EnableWindow(bEnable && !m_generating);
	GetDlgItem(IDC_CERTPATH)->EnableWindow(bEnable);
	GetDlgItem(IDC_CERTBROWSE)->EnableWindow(bEnable);
	GetDlgItem(IDC_KEYPATH)->EnableWindow(bEnable);
	GetDlgItem(IDC_KEYBROWSE)->EnableWindow(bEnable);
	SetModified();
}

void CPPgWebServer::OnEnChangeWSEnabled()
{
	bool bIsWIEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
	GetDlgItem(IDC_WS_GZIP)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSPORT)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_TMPLPATH)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_TMPLBROWSE)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSTIMEOUT)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WEB_HTTPS)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSPASS)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WS_ALLOWHILEVFUNC)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSENABLEDLOW)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSPASSLOW)->EnableWindow(bIsWIEnabled && IsDlgButtonChecked(IDC_WSENABLEDLOW));
	SetUPnPState();
	SetTmplButtonState();
	OnChangeHTTPS();

	SetModified();
}

void CPPgWebServer::OnReloadTemplates()
{
	theApp.webserver->ReloadTemplates();
}

void CPPgWebServer::OnBnClickedTmplbrowse()
{
	CString strTempl;
	GetDlgItemText(IDC_TMPLPATH, strTempl);
	CString buffer(GetResString(_T("WS_RELOAD_TMPL")) + _T(" (*.tmpl)|*.tmpl||"));
	if (DialogBrowseFile(buffer, buffer, strTempl)) {
		SetDlgItemText(IDC_TMPLPATH, buffer);
		SetModified();
	}
	SetTmplButtonState();
}

//create cert.key and cert.crt in config directory
void CPPgWebServer::OnGenerateCertificate()
{
	if (::InterlockedExchange(&m_generating, 1))
		return;
	CWaitCursor curWaiting;

	const CString &confdir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &fkey(confdir + _T("cert.key"));
	const CString &fcrt(confdir + _T("cert.crt"));
	CStringA not_after, not_before;
	SYSTEMTIME st;
	GetSystemTime(&st);
	not_before.Format("%4hu%02hu01000000", st.wYear, st.wMonth);
	not_after.Format("%4hu%02hu01000000", st.wYear + 1, st.wMonth);

	struct options opt;
	opt.issuer_key = (LPCTSTR)fkey;
	opt.cert_file = (LPCTSTR)fcrt;
	opt.subject_name = "CN=Web Interface,O=emule-project.net,OU=eMule";
	opt.issuer_name = "CN=eMule,O=emule-project.net";
	opt.not_before = (LPCSTR)not_before;
	opt.not_after = (LPCSTR)not_after;
	opt.serial = _byteswap_ushort(rand() & 0xfff); //avoid repeated serials (kind of)
	if (!opt.serial)
		++opt.serial; //must be positive
	m_bNewCert = !CertCreate(opt);
	if (m_bNewCert) {
		AddLogLine(false, _T("New certificate created; serial %d"), opt.serial);
		SetDlgItemText(IDC_KEYPATH, fkey);
		SetDlgItemText(IDC_CERTPATH, fcrt);
		GetDlgItem(IDC_WEB_GENERATE)->EnableWindow(FALSE);
		SetModified();
	} else {
		LogError(_T("Certificate creation failed"));
		CDarkMode::MessageBox(GetResString(_T("CERT_ERR_CREATE")));
		::InterlockedExchange(&m_generating, 0); //re-enable only if failed
	}
}

void CPPgWebServer::OnBnClickedCertbrowse()
{
	CString strCert;
	GetDlgItemText(IDC_CERTPATH, strCert);
	CString buffer(GetResString(_T("CERTIFICATE")));
	buffer += _T(" (*.crt)|*.crt|All Files (*.*)|*.*||");
	if (DialogBrowseFile(buffer, buffer, strCert))
		SetDlgItemText(IDC_CERTPATH, buffer);
	if (buffer.CompareNoCase(strCert) != 0)
		SetModified();
}

void CPPgWebServer::OnBnClickedKeybrowse()
{
	CString strKey;
	GetDlgItemText(IDC_KEYPATH, strKey);
	CString buffer(GetResString(_T("KEY")));
	buffer += _T(" (*.key)|*.key|All Files (*.*)|*.*||");
	if (DialogBrowseFile(buffer, buffer, strKey))
		SetDlgItemText(IDC_KEYPATH, buffer);
	if (buffer.CompareNoCase(strKey) != 0)
		SetModified();
}

void CPPgWebServer::SetTmplButtonState()
{
	CString buffer;
	GetDlgItemText(IDC_TMPLPATH, buffer);

	GetDlgItem(IDC_WSRELOADTMPL)->EnableWindow(IsDlgButtonChecked(IDC_WSENABLED) && (buffer.CompareNoCase(thePrefs.GetTemplate()) == 0));
}

void CPPgWebServer::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_WebInterface);
}

BOOL CPPgWebServer::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgWebServer::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgWebServer::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(::DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}
