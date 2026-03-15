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
#include "kademlia/kademlia/tag.h"
#include "MetaDataDlg.h"
#include "Preferences.h"
#include "MenuCmds.h"
#include "Packets.h"
#include "KnownFile.h"
#include "UserMsgs.h"
#include "MediaInfo.h"
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/MenuXP.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


//////////////////////////////////////////////////////////////////////////////
// COLUMN_INIT -- List View Columns

enum EMetaDataCols
{
	META_DATA_COL_NAME = 0,
	META_DATA_COL_TYPE,
	META_DATA_COL_VALUE
};

static LCX_COLUMN_INIT s_aColumns[] =
{
	{ META_DATA_COL_NAME,	_T("Name"),	 _T("SW_NAME"), LVCFMT_LEFT, -1, 0, ASCENDING,  NONE, _T("Temporary file MMMMM") },
	{ META_DATA_COL_TYPE,	_T("Type"),	 _T("TYPE"),	 LVCFMT_LEFT, -1, 1, ASCENDING,  NONE, _T("Integer") },
	{ META_DATA_COL_VALUE,	_T("Value"), _T("VALUE"),	 LVCFMT_LEFT, -1, 2, ASCENDING,  NONE, _T("long long long long long long long long file name.avi") }
};

#define	PREF_INI_SECTION	_T("MetaDataDlg")

// CMetaDataDlg dialog

IMPLEMENT_DYNAMIC(CMetaDataDlg, CResizablePage)

BEGIN_MESSAGE_MAP(CMetaDataDlg, CResizablePage)
	ON_COMMAND(MP_COPYSELECTED, OnCopyTags)
	ON_COMMAND(MP_SELECTALL, OnSelectAllTags)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_NOTIFY(LVN_KEYDOWN, IDC_TAGS, OnLvnKeydownTags)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CMetaDataDlg::CMetaDataDlg()
	: CResizablePage(CMetaDataDlg::IDD)
	, m_pFile()
	, m_paFiles()
	, m_taglist()
	, m_pMenuTags()
	, m_bDataChanged()
{
	m_strCaption = GetResString(_T("META_DATA"));
	m_psp.pszTitle = m_strCaption;
	m_psp.dwFlags |= PSP_USETITLE;

	m_tags.m_pParent = this;
	m_tags.SetRegistryKey(PREF_INI_SECTION);
	m_tags.SetRegistryPrefix(_T("MetaDataTags_"));
}

CMetaDataDlg::~CMetaDataDlg()
{
	delete m_pMenuTags;
}

void CMetaDataDlg::SetTagList(Kademlia::TagList *taglist)
{
	m_taglist = taglist;
}

void CMetaDataDlg::Localize()
{
	if (!m_hWnd)
		return;
	SetTabTitle(_T("META_DATA"), this);

	CHeaderCtrl *pHeaderCtrl = m_tags.GetHeaderCtrl();
	if (pHeaderCtrl) {
		HDITEM hdi;
		hdi.mask = HDI_TEXT;

		for (int i = (int)_countof(s_aColumns); --i >= 0;) {
			const CString &sHdr(GetResString(s_aColumns[i].strHeadResID));
			hdi.pszText = const_cast<LPTSTR>((LPCTSTR)sHdr);
			pHeaderCtrl->SetItem(i, &hdi);
		}
	}
	RefreshData();
}

void CMetaDataDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizablePage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TAGS, m_tags);
}

BOOL CMetaDataDlg::OnInitDialog()
{
	CResizablePage::OnInitDialog();
	InitWindowStyles(this);

	AddOrReplaceAnchor(this, IDC_TAGS, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_TOTAL_TAGS, BOTTOM_LEFT, BOTTOM_RIGHT);

	SetDlgItemText(IDC_TOTAL_TAGS, GetResString(_T("METATAGS")));

	m_tags.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_GRIDLINES);
	m_tags.ReadColumnStats(_countof(s_aColumns), s_aColumns);
	m_tags.CreateColumns(_countof(s_aColumns), s_aColumns);

	m_pMenuTags = new CMenuXP();
	if (m_pMenuTags->CreatePopupMenu()) {
		m_pMenuTags->AppendMenu(MF_ENABLED | MF_STRING, MP_COPYSELECTED, GetResString(_T("COPY")));
		m_pMenuTags->AppendMenu(MF_SEPARATOR);
		m_pMenuTags->AppendMenu(MF_ENABLED | MF_STRING, MP_SELECTALL, GetResString(_T("SELECTALL")));
	}
	m_tags.m_pMenu = m_pMenuTags;
	m_tags.m_pParent = this;

	Localize();
	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CMetaDataDlg::OnSetActive()
{
	if (!CResizablePage::OnSetActive())
		return FALSE;
	if (m_bDataChanged) {
		RefreshData();
		m_bDataChanged = false;
	}
	return TRUE;
}

LRESULT CMetaDataDlg::OnDataChanged(WPARAM, LPARAM)
{
	m_bDataChanged = true;
	return 1;
}

CString GetTagNameByID(UINT id)
{
	LPCTSTR uid;
	switch (id) {
	case FT_FILENAME:
		uid = _T("SW_NAME");
		break;
	case FT_FILESIZE:
		uid = _T("DL_SIZE");
		break;
	case FT_FILETYPE:
		uid = _T("TYPE");
		break;
	case FT_FILEFORMAT:
		uid = _T("FORMAT");
		break;
	case FT_LASTSEENCOMPLETE:	// 01-Nov-2004: Sent in server<->client protocol as 'Last Seen Complete'
		uid = _T("LASTSEENCOMPL");
		break;
	case 0x06:
		uid = _T("META_PARTPATH");
		break;
	case 0x07:
		uid = _T("META_PARTHASH");
		break;
	case FT_TRANSFERRED:
		uid = _T("DL_TRANSF");
		break;
	case FT_GAPSTART:
		uid = _T("META_GAPSTART");
		break;
	case FT_GAPEND:
		uid = _T("META_GAPEND");
		break;
	case 0x0B:
		uid = _T("DESCRIPTION");
		break;
	case 0x0C:
		uid = _T("PING");
		break;
	case 0x0D:
		uid = _T("FAIL");
		break;
	case 0x0E:
		uid = _T("META_PREFERENCES");
		break;
	case 0x0F:
		uid = _T("PORT");
		break;
	case 0x10:
		uid = _T("IP");
		break;
	case 0x11:
		uid = _T("META_VERSION");
		break;
	case FT_PARTFILENAME:
		uid = _T("META_TEMPFILE");
		break;
	case 0x13:
		uid = _T("PRIORITY");
		break;
	case FT_STATUS:
		uid = _T("STATUS");
		break;
	case FT_SOURCES:
		uid = _T("SEARCHAVAIL");
		break;
	case 0x16:
		uid = _T("PERMISSION");
		break;
	case 0x17:
		uid = _T("FD_PARTS");
		break;
	case FT_COMPLETE_SOURCES:
		uid = _T("COMPLSOURCES");
		break;
	case FT_MEDIA_ARTIST:
		uid = _T("ARTIST");
		break;
	case FT_MEDIA_ALBUM:
		uid = _T("ALBUM");
		break;
	case FT_MEDIA_TITLE:
		uid = _T("TITLE");
		break;
	case FT_MEDIA_LENGTH:
		uid = _T("LENGTH");
		break;
	case FT_MEDIA_BITRATE:
		uid = _T("BITRATE");
		break;
	case FT_MEDIA_CODEC:
		uid = _T("CODEC");
		break;
	case FT_FILECOMMENT:
		uid = _T("COMMENT");
		break;
	case FT_FILERATING:
		uid = _T("QL_RATING");
		break;
	case FT_FILEHASH:
		uid = _T("FILEHASH");
		break;
	case 0xFA:
		uid = _T("META_SERVERPORT");
		break;
	case 0xFB:
		uid = _T("META_SERVERIP");
		break;
	case 0xFC:
		uid = _T("META_SRCUDPPORT");
		break;
	case 0xFD:
		uid = _T("META_SRCTCPPORT");
		break;
	case 0xFE:
		uid = _T("META_SRCIP");
		break;
	case 0xFF:
		uid = _T("META_SRCTYPE");
		break;
	default:
		{
			CString buffer;
			buffer.Format(_T("Tag0x%02X"), id);
			return buffer;
		}
	}
	return GetResString(uid);
}

CString GetMetaTagName(UINT uTagID)
{
	CString strName(GetTagNameByID(uTagID));
	StripTrailingColon(strName);
	return strName;
}

CString GetName(const CTag *pTag)
{
	CString strName;
	if (pTag->GetNameID())
		strName = GetTagNameByID(pTag->GetNameID());
	else
		strName = pTag->GetName();
	StripTrailingColon(strName);
	return strName;
}

CString GetName(const Kademlia::CKadTag *pTag)
{
	CString strName;
	if (pTag->m_name.GetLength() == 1)
		strName = GetTagNameByID((BYTE)pTag->m_name[0]);
	else
		strName = pTag->m_name;
	StripTrailingColon(strName);
	return strName;
}

CString GetValue(const CTag *pTag)
{
	CString strValue;
	if (pTag->IsStr()) {
		strValue = pTag->GetStr();
		if (pTag->GetNameID() == FT_MEDIA_CODEC)
			strValue = GetCodecDisplayName(strValue);
	} else if (pTag->IsInt()) {
		if (pTag->GetNameID() == FT_MEDIA_LENGTH || pTag->GetNameID() == FT_LASTSEENCOMPLETE)
			strValue = SecToTimeLength(pTag->GetInt());
		else if (pTag->GetNameID() == FT_FILERATING)
			strValue = GetRateString(pTag->GetInt());
		else if (pTag->GetNameID() == 0x10 || pTag->GetNameID() >= 0xFA)
			strValue.Format(_T("%u"), pTag->GetInt());
		else
			strValue = GetFormatedUInt(pTag->GetInt());
	} else if (pTag->IsFloat())
		strValue.Format(_T("%f"), pTag->GetFloat());
	else if (pTag->IsHash())
		strValue = md4str(pTag->GetHash());
	else if (pTag->IsInt64(false))
		strValue = GetFormatedUInt64(pTag->GetInt64());
	else
		strValue.Format(_T("<Unknown value of type 0x%02X>"), pTag->GetType());
	return strValue;
}

CString GetValue(const Kademlia::CKadTag *pTag) // FIXME LARGE FILES
{
	CString strValue;
	if (pTag->IsStr()) {
		strValue = pTag->GetStr();
		if (pTag->m_name.Compare(TAG_MEDIA_CODEC) == 0)
			strValue = GetCodecDisplayName(strValue);
	} else if (pTag->IsInt()) {
		if (pTag->m_name.Compare(TAG_MEDIA_LENGTH) == 0)
			strValue = SecToTimeLength((UINT)pTag->GetInt());
		else if (pTag->m_name.Compare(TAG_FILERATING) == 0)
			strValue = GetRateString((UINT)pTag->GetInt());
		else if ((BYTE)pTag->m_name[0] == 0x10 || (BYTE)pTag->m_name[0] >= 0xFA)
			strValue.Format(_T("%I64u"), pTag->GetInt());
		else
			strValue = GetFormatedUInt((UINT)pTag->GetInt());
	} else if (pTag->m_type == TAGTYPE_FLOAT32)
		strValue.Format(_T("%f"), pTag->GetFloat());
	else if (pTag->m_type == TAGTYPE_BOOL)
		strValue.Format(_T("%u"), (UINT)pTag->GetBool());
	else
		strValue.Format(_T("<Unknown value of type 0x%02X>"), pTag->m_type);
	return strValue;
}

CString GetType(UINT uType)
{
	LPCTSTR pStr;
	switch (uType) {
	case TAGTYPE_HASH:
		pStr = _T("Hash");
		break;
	case TAGTYPE_STRING:
		pStr = _T("String");
		break;
	case TAGTYPE_UINT32:
		pStr = _T("Int32");
		break;
	case TAGTYPE_FLOAT32:
		pStr = _T("Float");
		break;
	case TAGTYPE_BOOL:
		pStr = _T("Bool");
		break;
	case TAGTYPE_UINT16:
		pStr = _T("Int16");
		break;
	case TAGTYPE_UINT8:
		pStr = _T("Int8");
		break;
	case TAGTYPE_UINT64:
		pStr = _T("Int64");
		break;
	default:
		{
			CString strValue;
			strValue.Format(_T("<Unknown type 0x%02X>"), uType);
			return strValue;
		}
	}
	return CString(pStr);
}

void CMetaDataDlg::RefreshData()
{
	if (m_paFiles->GetSize() <= 0)
		m_pFile = NULL;
	else if (m_pFile == static_cast<CAbstractFile*>((*m_paFiles)[0]))
		return;
	//CWaitCursor curWait; commented out to avoid cursor blinking
	m_tags.SetRedraw(FALSE);
	m_tags.DeleteAllItems();

	int iMetaTags = 0;
	if (m_paFiles->GetSize() >= 1) {
		CString strBuff;
		m_pFile = static_cast<CAbstractFile*>((*m_paFiles)[0]);
		if (!m_pFile->HasNullHash()) {
			LVITEM lvi;
			lvi.mask = LVIF_TEXT;
			lvi.iItem = INT_MAX;
			lvi.iSubItem = META_DATA_COL_NAME;
			strBuff = GetResString(_T("FD_HASH"));
			StripTrailingColon(strBuff);
			lvi.pszText = const_cast<LPTSTR>((LPCTSTR)strBuff);
			int iItem = m_tags.InsertItem(&lvi);
			if (iItem >= 0) {
				lvi.iItem = iItem;

				// intentionally left blank as it's not a real meta tag
				lvi.pszText = _T("");
				lvi.iSubItem = META_DATA_COL_TYPE;
				m_tags.SetItem(&lvi);


				const CString &sMD4(md4str(m_pFile->GetFileHash()));
				lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sMD4);
				lvi.iSubItem = META_DATA_COL_VALUE;
				m_tags.SetItem(&lvi);
			}
		}

		const CArray<CTag*, CTag*> &aTags = m_pFile->GetTags();
		INT_PTR iTags = aTags.GetCount();
		for (int i = 0; i < iTags; ++i) {
			const CTag *pTag = aTags[i];
			LVITEM lvi;
			lvi.mask = LVIF_TEXT;
			lvi.iItem = INT_MAX;
			lvi.iSubItem = META_DATA_COL_NAME;
			const CString &sName(GetName(pTag));
			lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sName);
			int iItem = m_tags.InsertItem(&lvi);
			if (iItem >= 0) {
				lvi.iItem = iItem;

				const CString &sType(GetType(pTag->GetType()));
				lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sType);
				lvi.iSubItem = META_DATA_COL_TYPE;
				m_tags.SetItem(&lvi);

				const CString &sValue(GetValue(pTag));
				lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sValue);
				lvi.iSubItem = META_DATA_COL_VALUE;
				m_tags.SetItem(&lvi);

				++iMetaTags;
			}
		}
	} else if (m_taglist != NULL) {
		for (Kademlia::TagList::const_iterator it = m_taglist->begin(); it != m_taglist->end(); ++it) {
			const Kademlia::CKadTag *pTag = *it;
			LVITEM lvi;
			lvi.mask = LVIF_TEXT;
			lvi.iItem = INT_MAX;
			lvi.iSubItem = META_DATA_COL_NAME;
			const CString &sName(GetName(pTag));
			lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sName);
			int iItem = m_tags.InsertItem(&lvi);
			if (iItem >= 0) {
				lvi.iItem = iItem;

				const CString &sType(GetType(pTag->m_type));
				lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sType);
				lvi.iSubItem = META_DATA_COL_TYPE;
				m_tags.SetItem(&lvi);

				const CString &sValue(GetValue(pTag));
				lvi.pszText = const_cast<LPTSTR>((LPCTSTR)sValue);
				lvi.iSubItem = META_DATA_COL_VALUE;
				m_tags.SetItem(&lvi);

				++iMetaTags;
			}
		}
	}
	CString sTotal(GetResString(_T("METATAGS")));
	sTotal.AppendFormat(_T(" %i"), iMetaTags);
	SetDlgItemText(IDC_TOTAL_TAGS, sTotal);
	m_tags.SetRedraw();
}

void CMetaDataDlg::OnCopyTags()
{
	CWaitCursor curWait;
	int iSelected = 0;
	CString strData;
	for (POSITION pos = m_tags.GetFirstSelectedItemPosition(); pos != NULL;) {
		int iItem = m_tags.GetNextSelectedItem(pos);
		const CString &strValue(m_tags.GetItemText(iItem, META_DATA_COL_VALUE));
		if (!strValue.IsEmpty()) {
			if (!strData.IsEmpty())
				strData += _T("\r\n");
			strData += strValue;
			++iSelected;
		}
	}

	if (!strData.IsEmpty()) {
		if (iSelected > 1)
			strData += _T("\r\n");
		theApp.CopyTextToClipboard(strData);
	}
}

void CMetaDataDlg::OnSelectAllTags()
{
	m_tags.SelectAllItems();
}

void CMetaDataDlg::OnLvnKeydownTags(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);

	if (pLVKeyDown->wVKey == 'C' && GetKeyState(VK_CONTROL) < 0)
		OnCopyTags();
	*pResult = 0;
}

void CMetaDataDlg::OnDestroy()
{
	m_tags.WriteColumnStats(_countof(s_aColumns), s_aColumns);
	CResizablePage::OnDestroy();
}