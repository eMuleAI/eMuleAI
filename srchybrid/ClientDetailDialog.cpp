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
#include "ClientDetailDialog.h"
#include "UpDownClient.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "Server.h"
#include "ServerList.h"
#include "SharedFileList.h"
#include "HighColorTab.hpp"
#include "UserMsgs.h"
#include "ListenSocket.h"
#include "preferences.h"
#include "ClientList.h"
#include "eMuleAI/GeoLite2.h"
#include "emuledlg.h" 
#include "eMuleAI/DarkMode.h"
#include <unordered_map>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	class CClientDetailRuntimeToken : public CObject
	{
	public:
		explicit CClientDetailRuntimeToken(ClientRuntimeID uRuntimeID)
			: m_uRuntimeID(uRuntimeID)
		{
		}

		ClientRuntimeID GetRuntimeID() const
		{
			return m_uRuntimeID;
		}

	private:
		ClientRuntimeID m_uRuntimeID;
	};

	CCriticalSection s_csClientDetailRuntimeTokens;
	std::unordered_map<const CObject*, ClientRuntimeID> s_ClientDetailRuntimeTokens;

	CObject* CreateClientDetailToken(ClientRuntimeID uRuntimeID)
	{
		if (uRuntimeID == 0)
			return NULL;

		CObject* pToken = new CClientDetailRuntimeToken(uRuntimeID);
		{
			CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
			s_ClientDetailRuntimeTokens[pToken] = uRuntimeID;
		}
		return pToken;
	}

	void DestroyClientDetailToken(CObject* pToken)
	{
		if (pToken == NULL)
			return;

		{
			CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
			const auto itToken = s_ClientDetailRuntimeTokens.find(pToken);
			if (itToken != s_ClientDetailRuntimeTokens.end())
				s_ClientDetailRuntimeTokens.erase(itToken);
		}
		delete static_cast<const CClientDetailRuntimeToken*>(pToken);
	}

	ClientRuntimeID ClientDetailRuntimeIDFromToken(const CObject* pToken)
	{
		if (pToken == NULL)
			return 0;

		{
			CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
			const auto itToken = s_ClientDetailRuntimeTokens.find(pToken);
			if (itToken != s_ClientDetailRuntimeTokens.end())
				return itToken->second;
		}

		const ULONG_PTR uTokenValue = reinterpret_cast<ULONG_PTR>(pToken);
		return (uTokenValue & 1) != 0 ? static_cast<ClientRuntimeID>(uTokenValue >> 1) : 0;
	}

	CObject* CreateTrackedClientDetailToken(const CUpDownClient* pClient)
	{
		if (pClient == NULL || theApp.clientlist == NULL)
			return NULL;

		CUpDownClient* pTrackedClient = theApp.clientlist->AcquireTrackedClientByPointer(pClient);
		if (pTrackedClient == NULL)
			return NULL;

		CObject* pToken = CreateClientDetailToken(pTrackedClient->GetRuntimeID());
		pTrackedClient->ReleaseRuntimeReference();
		return pToken;
	}
}

static void SetClientDetailText(CWnd* pWnd, int nControlID, LPCTSTR pszText)
{
	if (pWnd != NULL)
		pWnd->SetDlgItemText(nControlID, pszText);
}

static void ClearClientDetailView(CClientDetailPage* pPage)
{
	if (pPage == NULL)
		return;

	CWnd* pFlagIcon = pPage->GetDlgItem(IDC_COUNTRY_FLAG_ICON);
	if (pFlagIcon != NULL) {
		((CStatic*)pFlagIcon)->SetIcon(NULL);
		pFlagIcon->ShowWindow(SW_HIDE);
	}

	SetClientDetailText(pPage, IDC_DNAME, _T("?"));
	SetClientDetailText(pPage, IDC_DHASH, _T("?"));
	SetClientDetailText(pPage, IDC_DSOFT, _T("?"));
	SetClientDetailText(pPage, IDC_GEOLOCATION_TXT, _T("?"));
	SetClientDetailText(pPage, IDC_CLIENT_IP, _T("?"));
	SetClientDetailText(pPage, IDC_OBFUSCATION_STAT, _T("?"));
	SetClientDetailText(pPage, IDC_DID, _T("?"));
	SetClientDetailText(pPage, IDC_DSIP, _T("?"));
	SetClientDetailText(pPage, IDC_DSNAME, _T("?"));
	SetClientDetailText(pPage, IDC_LEECHER, _T("?"));
	SetClientDetailText(pPage, IDC_PUNISHMENT, _T("?"));
	SetClientDetailText(pPage, IDC_DDOWNLOADING, _T("-"));
	SetClientDetailText(pPage, IDC_UPLOADING, _T("-"));
	SetClientDetailText(pPage, IDC_DDUP, _T("?"));
	SetClientDetailText(pPage, IDC_DDOWN, _T("?"));
	SetClientDetailText(pPage, IDC_DAVUR, _T("?"));
	SetClientDetailText(pPage, IDC_DAVDR, _T("?"));
	SetClientDetailText(pPage, IDC_DUPTOTAL, _T("?"));
	SetClientDetailText(pPage, IDC_DDOWNTOTAL, _T("?"));
	SetClientDetailText(pPage, IDC_DRATIO, _T("?"));
	SetClientDetailText(pPage, IDC_CDIDENT, _T("?"));
	SetClientDetailText(pPage, IDC_DRATING, _T("?"));
	SetClientDetailText(pPage, IDC_DSCORE, _T("-"));
	SetClientDetailText(pPage, IDC_CLIENTDETAIL_KADCON, _T("?"));
}

class CScopedDetailClientRef
{
public:
	CScopedDetailClientRef()
		: m_pClient(NULL)
	{
	}

	explicit CScopedDetailClientRef(ClientRuntimeID uRuntimeID)
		: m_pClient(NULL)
	{
		AttachRuntimeID(uRuntimeID);
	}

	~CScopedDetailClientRef()
	{
		Release();
	}

	void AttachRuntimeID(ClientRuntimeID uRuntimeID)
	{
		Release();
		if (uRuntimeID != 0 && theApp.clientlist != NULL)
			m_pClient = theApp.clientlist->AcquireTrackedClientByRuntimeID(uRuntimeID);
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

static CUpDownClient* ResolveClientDetailClient(const CObject* pToken, CScopedDetailClientRef& clientRef)
{
	const ClientRuntimeID uRuntimeID = ClientDetailRuntimeIDFromToken(pToken);
	clientRef.AttachRuntimeID(uRuntimeID);
	return clientRef.Get();
}


///////////////////////////////////////////////////////////////////////////////
// CClientDetailPage

IMPLEMENT_DYNAMIC(CClientDetailPage, CResizablePage)

BEGIN_MESSAGE_MAP(CClientDetailPage, CResizablePage)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

CClientDetailPage::CClientDetailPage()
	: CResizablePage(CClientDetailPage::IDD)
	, m_paClients()
	, m_bDataChanged()
{
	m_strCaption = GetResString(_T("CD_TITLE"));
	m_psp.pszTitle = m_strCaption;
	m_psp.dwFlags |= PSP_USETITLE;
}

void CClientDetailPage::DoDataExchange(CDataExchange *pDX)
{
	CResizablePage::DoDataExchange(pDX);
}

BOOL CClientDetailPage::OnInitDialog()
{
	CResizablePage::OnInitDialog();
	InitWindowStyles(this);

	AddOrReplaceAnchor(this, IDC_STATIC30, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_STATIC40, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_STATIC50, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_DNAME, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_DSNAME, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_DSOFT, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_GEOLOCATION_TXT, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_DDOWNLOADING, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_UPLOADING, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_OBFUSCATION_STAT, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_CLIENT_IP, TOP_LEFT, TOP_RIGHT);

	AddAllOtherAnchors();
	Localize();
	return TRUE;
}

BOOL CClientDetailPage::OnSetActive()
{
	if (!CResizablePage::OnSetActive())
		return FALSE;

	if (m_bDataChanged) {
		const CObject* pToken = (m_paClients != NULL && m_paClients->GetSize() > 0) ? (*m_paClients)[0] : NULL;
		CScopedDetailClientRef clientRef;
		CUpDownClient* client = ResolveClientDetailClient(pToken, clientRef);
		if (client == NULL) {
			ClearClientDetailView(this);
			m_bDataChanged = false;
			return TRUE;
		}

			SetDlgItemText(IDC_DNAME, (client->GetUserName() ? client->GetUserName() : _T("?")));
			SetDlgItemText(IDC_DHASH, (client->HasValidHash() ? (LPCTSTR)md4str(client->GetUserHash()) : _T("?")));
		SetDlgItemText(IDC_DSOFT, client->DbgGetFullClientSoftVer());

		bool longCountryName = true;
		if (theApp.geolite2->IsGeoLite2Active())
			GetDlgItem(IDC_GEOLOCATION_TXT)->SetWindowText(client->GetGeolocationData(longCountryName));
		if (theApp.geolite2->ShowCountryFlag()) {
			countryflag = theApp.geolite2->GetFlagImageList()->ExtractIcon(client->GetCountryFlagIndex());
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->SetIcon(countryflag);
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->ShowWindow(SW_SHOW);
			RECT rect1;
			RECT rect2;
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->GetWindowRect(&rect1);
			GetDlgItem(IDC_GEOLOCATION_TXT)->GetWindowRect(&rect2);
			ScreenToClient(&rect1);
			ScreenToClient(&rect2);
			GetDlgItem(IDC_GEOLOCATION_TXT)->MoveWindow(CRect(rect1.right + 2, rect2.top, rect2.right, rect2.bottom), TRUE);
		}

		LPCTSTR uid;
		if (!client->SupportsCryptLayer())
			uid = _T("IDENTNOSUPPORT");
		else
			uid = (	   thePrefs.IsCryptLayerEnabled()
					&& (client->RequestsCryptLayer() || thePrefs.IsCryptLayerPreferred())
					&& (client->IsObfuscatedConnectionEstablished() || client->socket == NULL || !client->socket->IsConnected())
				  )
				? _T("ENABLED") : _T("SUPPORTED");
		CString buffer(GetResString(uid));
#if defined(_DEBUG)
		if (client->IsObfuscatedConnectionEstablished())
			buffer += _T("(In Use)");
#endif
		GetDlgItem(IDC_CLIENT_IP)->SetWindowText(ipstr(!client->GetIP().IsNull() ? client->GetIP() : client->GetConnectIP()));

		SetDlgItemText(IDC_OBFUSCATION_STAT, buffer);

		SetDlgItemText(IDC_DID, GetResString(client->HasLowID() ? _T("IDLOW") : _T("IDHIGH")));

		if (client->GetServerIP()) {
			SetDlgItemText(IDC_DSIP, ipstr(client->GetServerIP()));
			const CServer *cserver = theApp.serverlist->GetServerByIPTCP(client->GetServerIP(), client->GetServerPort());
			SetDlgItemText(IDC_DSNAME, cserver ? (LPCTSTR)cserver->GetListName() : _T("?"));
		} else {
			SetDlgItemText(IDC_DSIP, _T("?"));
			SetDlgItemText(IDC_DSNAME, _T("?"));
		}

		GetDlgItem(IDC_LEECHER)->SetWindowText(client->GetPunishmentReason());
		GetDlgItem(IDC_PUNISHMENT)->SetWindowText(client->GetPunishmentText());

		const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
		SetDlgItemText(IDC_DDOWNLOADING, file ? (LPCTSTR)file->GetFileName() : _T("-"));

		SetDlgItemText(IDC_UPLOADING, client->GetRequestFile() ? (LPCTSTR)client->GetRequestFile()->GetFileName() : _T("-"));
		SetDlgItemText(IDC_DDUP, CastItoXBytes(client->GetTransferredDown()));
		SetDlgItemText(IDC_DDOWN, CastItoXBytes(client->GetTransferredUp()));
		SetDlgItemText(IDC_DAVUR, CastItoXBytes(client->GetDownloadDatarate(), false, true));
		SetDlgItemText(IDC_DAVDR, CastItoXBytes(client->GetUploadDatarate(), false, true));

		CClientCredits* clcredits = client->Credits();
		if (clcredits) {
			SetDlgItemText(IDC_DUPTOTAL, CastItoXBytes(clcredits->GetDownloadedTotal()));
			SetDlgItemText(IDC_DDOWNTOTAL, CastItoXBytes(clcredits->GetUploadedTotal()));
			buffer.Format(_T("%.1f [%.1f]"), clcredits->GetScoreRatio(client->GetIP()), (float)client->Credits()->GetMyScoreRatio(client->GetIP()));
			SetDlgItemText(IDC_DRATIO, buffer);

			if (theApp.clientcredits->CryptoAvailable()) {
				switch (clcredits->GetCurrentIdentState(client->GetIP())) {
				case IS_NOTAVAILABLE:
					SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTNOSUPPORT")));
					break;
				case IS_IDFAILED:
				case IS_IDNEEDED:
				case IS_IDBADGUY:
					SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTFAILED")));
					break;
				case IS_IDENTIFIED:
					SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTOK")));
				}
			} else
				SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTNOSUPPORT")));
		} else {
			SetDlgItemText(IDC_DDOWNTOTAL, _T("?"));
			SetDlgItemText(IDC_DUPTOTAL, _T("?"));
			SetDlgItemText(IDC_DRATIO, _T("?"));
			SetDlgItemText(IDC_CDIDENT, _T("?"));
		}

		if (client->GetUserName() && clcredits != NULL) {
			buffer.Format(_T("%.1f"), (float)client->GetScore(false, client->IsDownloading(), true));
			SetDlgItemText(IDC_DRATING, buffer);
		} else
			SetDlgItemText(IDC_DRATING, _T("?"));

		if (client->GetUploadState() != US_NONE && clcredits != NULL) {
			if (!client->GetFriendSlot())
				SetDlgItemInt(IDC_DSCORE, client->GetScore(false, client->IsDownloading(), false));
			else
				SetDlgItemText(IDC_DSCORE, GetResString(_T("FRIENDDETAIL")));
		} else
			SetDlgItemText(IDC_DSCORE, _T("-"));

		SetDlgItemText(IDC_CLIENTDETAIL_KADCON, GetResString(client->GetKadPort() ? _T("CONNECTED") : _T("DISCONNECTED")));

		m_bDataChanged = false;
	}
	return TRUE;
}

LRESULT CClientDetailPage::OnDataChanged(WPARAM, LPARAM)
{
	m_bDataChanged = true;
	return 1;
}

void CClientDetailPage::Localize()
{
	if (!m_hWnd)
		return;
	SetTabTitle(_T("CD_TITLE"), this);

	SetDlgItemText(IDC_STATIC30, GetResString(_T("CD_GENERAL")));
	SetDlgItemText(IDC_STATIC31, GetResString(_T("CD_UNAME")));
	SetDlgItemText(IDC_STATIC32, GetResString(_T("CD_UHASH")));
	SetDlgItemText(IDC_STATIC_SOFTWARE, GetResString(_T("CD_CSOFT")) + _T(':'));
	SetDlgItemText(IDC_STATIC35, GetResString(_T("CD_SIP")));
	SetDlgItemText(IDC_STATIC38, GetResString(_T("CD_SNAME")));
	SetDlgItemText(IDC_STATIC_OBF_LABEL, GetResString(_T("OBFUSCATION")) + _T(':'));

	SetDlgItemText(IDC_STATIC40, GetResString(_T("CD_TRANS")));
	SetDlgItemText(IDC_STATIC41, GetResString(_T("CD_CDOWN")));
	SetDlgItemText(IDC_STATIC42, GetResString(_T("CD_DOWN")));
	SetDlgItemText(IDC_STATIC43, GetResString(_T("CD_ADOWN")));
	SetDlgItemText(IDC_STATIC44, GetResString(_T("CD_TDOWN")));
	SetDlgItemText(IDC_STATIC45, GetResString(_T("CD_UP")));
	SetDlgItemText(IDC_STATIC46, GetResString(_T("CD_AUP")));
	SetDlgItemText(IDC_STATIC47, GetResString(_T("CD_TUP")));
	SetDlgItemText(IDC_STATIC48, GetResString(_T("CD_UPLOADREQ")));

	SetDlgItemText(IDC_STATIC50, GetResString(_T("CD_SCORES")));
	SetDlgItemText(IDC_STATIC51, GetResString(_T("CD_MOD")));
	SetDlgItemText(IDC_STATIC52, GetResString(_T("CD_RATING")));
	SetDlgItemText(IDC_STATIC53, GetResString(_T("CD_USCORE")));
	SetDlgItemText(IDC_STATIC133x, GetResString(_T("CD_IDENT")));
	SetDlgItemText(IDC_CLIENTDETAIL_KAD, GetResString(_T("KADEMLIA")) + _T(':'));
	SetDlgItemText(IDC_STATIC_GEOLOCATION, GetResString(_T("GEOLOCATION")) + _T(':'));
	SetDlgItemText(IDC_STATIC_BAD_CLIENT_TYPE, GetResString(_T("REASON")));
	SetDlgItemText(IDC_STATIC_PUNISHMENT, GetResString(_T("PUNISHMENT")) + _T(':'));
	SetDlgItemText(IDC_STATIC_SOFTWARE, GetResString(_T("CD_CSOFT")) + _T(':'));
	SetDlgItemText(IDC_STATIC_CLIENT_IP, GetResString(_T("CD_UIP")));
}


///////////////////////////////////////////////////////////////////////////////
// CClientDetailDialog

IMPLEMENT_DYNAMIC(CClientDetailDialog, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CClientDetailDialog, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CClientDetailDialog::Localize()
{
	m_wndClient.Localize();
	SetTabTitle(_T("CD_TITLE"), &m_wndClient, this);
}

CClientDetailDialog::CClientDetailDialog(CUpDownClient *pClient, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
{
	if (pListCtrl != NULL)
		AddRuntimeToken(pClient);
	else
		AddTrackedRuntimeToken(pClient);
	Construct();
}

CClientDetailDialog::CClientDetailDialog(const CSimpleArray<CUpDownClient*> *paClients, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
{
	for (int i = 0; i < paClients->GetSize(); ++i) {
		CUpDownClient* pClient = (*paClients)[i];
		if (pListCtrl != NULL)
			AddRuntimeToken(pClient);
		else
			AddTrackedRuntimeToken(pClient);
	}
	Construct();
}

CClientDetailDialog::~CClientDetailDialog()
{
	DestroyOwnedRuntimeTokens();
}

void CClientDetailDialog::AddRuntimeToken(CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	CObject* pToken = CreateTrackedClientDetailToken(pClient);
	if (pToken != NULL) {
		m_aOwnedRuntimeTokens.Add(pToken);
		m_aItems.Add(pToken);
	}
}

void CClientDetailDialog::AddTrackedRuntimeToken(CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	CObject* pToken = CreateTrackedClientDetailToken(pClient);
	if (pToken != NULL) {
		m_aOwnedRuntimeTokens.Add(pToken);
		m_aItems.Add(pToken);
	}
}

void CClientDetailDialog::DestroyOwnedRuntimeTokens()
{
	for (INT_PTR i = 0; i < m_aOwnedRuntimeTokens.GetCount(); ++i)
		DestroyClientDetailToken(static_cast<CObject*>(m_aOwnedRuntimeTokens[i]));
	m_aOwnedRuntimeTokens.RemoveAll();
}

void CClientDetailDialog::Construct()
{
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_NOAPPLYNOW;

	m_wndClient.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndClient.m_psp.dwFlags |= PSP_USEICONID;
	m_wndClient.m_psp.pszIcon = _T("CLIENTDETAILS");
	m_wndClient.SetClients(&m_aItems);
	AddPage(&m_wndClient);
}

void CClientDetailDialog::OnDestroy()
{
	CListViewWalkerPropertySheet::OnDestroy();
	DestroyOwnedRuntimeTokens();
}

BOOL CClientDetailDialog::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("ClientDetailDialog")); // call this after(!) OnInitDialog
	SetWindowText(GetResString(_T("CD_TITLE")));

	m_tabDark.m_bClosable = false;

	if (IsDarkModeEnabled()) {
		HWND hTab = PropSheet_GetTabControl(m_hWnd);
		if (hTab != NULL) {
			::SetWindowTheme(hTab, _T(""), _T(""));
			m_tabDark.SubclassWindow(hTab);
		}

		ApplyTheme(m_hWnd);
	}

	return bResult;
}
