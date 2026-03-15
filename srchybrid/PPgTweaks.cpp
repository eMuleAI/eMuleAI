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
#include "opcodes.h"
#include "OtherFunctions.h"
#include "SearchDlg.h"
#include "PPgTweaks.h"
#include "Scheduler.h"
#include "DownloadQueue.h"
#include "Preferences.h"
#include "TransferDlg.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "ServerWnd.h"
#include "HelpIDs.h"
#include "Log.h"
#include "UserMsgs.h"
#include "PartFileWriteThread.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	DFLT_MAXCONPERFIVE	20
#define DFLT_MAXHALFOPEN	9

///////////////////////////////////////////////////////////////////////////////
// CPPgTweaks dialog

IMPLEMENT_DYNAMIC(CPPgTweaks, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgTweaks, CPropertyPage)
	ON_WM_HSCROLL()
	ON_WM_DESTROY()
	ON_MESSAGE(UM_TREEOPTSCTRL_NOTIFY, OnTreeOptsCtrlNotify)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_OPENPREFINI, OnBnClickedOpenprefini)
END_MESSAGE_MAP()

CPPgTweaks::CPPgTweaks()
	: CPropertyPage(CPPgTweaks::IDD)
	, m_ctrlTreeOptions(theApp.m_iDfltImageListColorFlags)
	, m_htiA4AFSaveCpu()
	, m_htiAutoArch()
	, m_htiAutoTakeEd2kLinks()
	, m_htiCheckDiskspace()
	, m_htiCloseUPnPPorts()
	, m_htiCommit()
	, m_htiCommitAlways()
	, m_htiCommitNever()
	, m_htiCommitOnShutdown()
	, m_htiConditionalTCPAccept()
	, m_htiCreditSystem()
	, m_htiDebug2Disk()
	, m_htiDebugSourceExchange()
	, m_htiDynUp()
	, m_htiDynUpEnabled()
	, m_htiDynUpGoingDownDivider()
	, m_htiDynUpGoingUpDivider()
	, m_htiDynUpMinUpload()
	, m_htiDynUpNumberOfPings()
	, m_htiDynUpPingTolerance()
	, m_htiDynUpPingToleranceGroup()
	, m_htiDynUpPingToleranceMilliseconds()
	, m_htiDynUpRadioPingTolerance()
	, m_htiDynUpRadioPingToleranceMilliseconds()
	, m_htiExtControls()
	, m_htiExtractMetaData()
	, m_htiExtractMetaDataID3Lib()
	, m_htiExtractMetaDataNever()
	, m_htiFilterLANIPs()
	, m_htiFirewallStartup()
	, m_htiFullAlloc()
	, m_htiImportParts()
	, m_htiLog2Disk()
	, m_htiLogA4AF()
	, m_htiLogBannedClients()
	, m_htiLogFileSaving()
	, m_htiLogFilteredIPs()
	, m_htiLogLevel()
	, m_htiLogRatingDescReceived()
	, m_htiLogSecureIdent()
	, m_htiLogUlDlEvents()
	, m_htiLogSpamRating()
	, m_htiLogRetryFailedTcp()
	, m_htiLogExtendedSXEvents()
	, m_htiLogNatTraversalEvents()
	, m_htiMaxCon5Sec()
	, m_htiMaxHalfOpen()
	, m_htiMinFreeDiskSpace()
	, m_htiFreeDiskSpaceCheckPeriod()
	, m_htiResolveShellLinks()
	, m_htiServerKeepAliveTimeout()
	, m_htiSkipWANIPSetup()
	, m_htiSkipWANPPPSetup()
	, m_htiSparsePartFiles()
	, m_htiTCPGroup()
	, m_htiUPnP()
	, m_htiVerbose()
	, m_htiVerboseGroup()
	, m_htiYourHostname()
	, m_fMinFreeDiskSpaceMB()
	, m_uFreeDiskSpaceCheckPeriod()
	, m_iQueueSize()
	, m_uFileBufferTimeLimit()
	, m_uFileBufferSize()
	, m_uServerKeepAliveTimeout()
	, m_iCommitFiles()
	, m_iDynUpGoingDownDivider()
	, m_iDynUpGoingUpDivider()
	, m_iDynUpMinUpload()
	, m_iDynUpNumberOfPings()
	, m_iDynUpPingTolerance()
	, m_iDynUpPingToleranceMilliseconds()
	, m_iDynUpRadioPingTolerance()
	, m_iExtractMetaData()
	, m_iLogLevel()
	, m_iMaxConnPerFive()
	, m_iMaxHalfOpen()
	, m_bA4AFSaveCpu()
	, m_bAutoArchDisable(true)
	, m_bAutoTakeEd2kLinks()
	, m_bCheckDiskspace()
	, m_bCloseUPnPOnExit(true)
	, m_bConditionalTCPAccept()
	, m_bCreditSystem()
	, m_bDebug2Disk()
	, m_bDebugSourceExchange()
	, m_bDynUpEnabled()
	, m_bExtControls()
	, m_bFilterLANIPs()
	, m_bFirewallStartup()
	, m_bFullAlloc()
	, m_bImportParts()
	, m_bInitializedTreeOpts()
	, m_bLog2Disk()
	, m_bLogA4AF()
	, m_bLogBannedClients()
	, m_bLogFileSaving()
	, m_bLogFilteredIPs()
	, m_bLogRatingDescReceived()
	, m_bLogSecureIdent()
	, m_bLogUlDlEvents()
	, m_bLogSpamRating()
	, m_bLogRetryFailedTcp()
	, m_bLogExtendedSXEvents()
	, m_bLogNatTraversalEvents()
	, m_bResolveShellLinks()
	, m_bSkipWANIPSetup()
	, m_bSkipWANPPPSetup()
	, m_bSparsePartFiles()
	, m_bVerbose()
{
}

void CPPgTweaks::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BTL, m_ctlFileBufferTimeLimit);
	DDX_Control(pDX, IDC_FILEBUFFERSIZE, m_ctlFileBuffSize);
	DDX_Control(pDX, IDC_QUEUESIZE, m_ctlQueueSize);
	DDX_Control(pDX, IDC_EXT_OPTS, m_ctrlTreeOptions);
	if (!m_bInitializedTreeOpts) {
		int iImgBackup = 8; // default icon
		int iImgLog = 8;
		int iImgDynyp = 8;
		int iImgConnection = 8;
		int iImgMetaData = 8;
		int iImgUPnP = 8;
		CImageList *piml = m_ctrlTreeOptions.GetImageList(TVSIL_NORMAL);
		if (piml) {
			iImgBackup = piml->Add(CTempIconLoader(_T("Harddisk")));
			iImgLog = piml->Add(CTempIconLoader(_T("Log")));
			iImgDynyp = piml->Add(CTempIconLoader(_T("upload")));
			iImgConnection = piml->Add(CTempIconLoader(_T("connection")));
			iImgMetaData = piml->Add(CTempIconLoader(_T("MediaInfo")));
			iImgUPnP = piml->Add(CTempIconLoader(_T("connectedhighhigh")));
		}

		/////////////////////////////////////////////////////////////////////////////
		// TCP/IP group
		//
		m_htiTCPGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("TCPIP_CONNS")), iImgConnection, TVI_ROOT);
		m_htiMaxCon5Sec = m_ctrlTreeOptions.InsertItem(GetResString(_T("MAXCON5SECLABEL")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxCon5Sec, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMaxHalfOpen = m_ctrlTreeOptions.InsertItem(GetResString(_T("MAXHALFOPENCONS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxHalfOpen, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiConditionalTCPAccept = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CONDTCPACCEPT")), m_htiTCPGroup, m_bConditionalTCPAccept);
		m_htiServerKeepAliveTimeout = m_ctrlTreeOptions.InsertItem(GetResString(_T("SERVERKEEPALIVETIMEOUT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiServerKeepAliveTimeout, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// Miscellaneous group
		//
		m_htiAutoTakeEd2kLinks = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("AUTOTAKEED2KLINKS")), TVI_ROOT, m_bAutoTakeEd2kLinks);
		m_htiCreditSystem = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("USECREDITSYSTEM")), TVI_ROOT, m_bCreditSystem);
		m_htiFirewallStartup = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("FO_PREF_STARTUP")), TVI_ROOT, m_bFirewallStartup);
		m_htiFilterLANIPs = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PW_FILTER")), TVI_ROOT, m_bFilterLANIPs);
		m_htiExtControls = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOWEXTSETTINGS")), TVI_ROOT, m_bExtControls);
		m_htiA4AFSaveCpu = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("A4AF_SAVE_CPU")), TVI_ROOT, m_bA4AFSaveCpu); // ZZ:DownloadManager
		m_htiAutoArch = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DISABLE_AUTOARCHPREV")), TVI_ROOT, m_bAutoArchDisable);
		m_htiYourHostname = m_ctrlTreeOptions.InsertItem(GetResString(_T("YOURHOSTNAME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, TVI_ROOT);
		m_ctrlTreeOptions.AddEditBox(m_htiYourHostname, RUNTIME_CLASS(CTreeOptionsEditEx));

		/////////////////////////////////////////////////////////////////////////////
		// File related group
		//
		m_htiSparsePartFiles = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SPARSEPARTFILES")), TVI_ROOT, m_bSparsePartFiles);
		m_htiFullAlloc = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("FULLALLOC")), TVI_ROOT, m_bFullAlloc);
		m_htiCheckDiskspace = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CHECKDISKSPACE")), TVI_ROOT, m_bCheckDiskspace);
		m_htiMinFreeDiskSpace = m_ctrlTreeOptions.InsertItem(GetResString(_T("MINFREEDISKSPACE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiCheckDiskspace);
		m_ctrlTreeOptions.AddEditBox(m_htiMinFreeDiskSpace, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiFreeDiskSpaceCheckPeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("FREEDISKSPACECHECKPERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiCheckDiskspace);
		m_ctrlTreeOptions.AddEditBox(m_htiFreeDiskSpaceCheckPeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiCommit = m_ctrlTreeOptions.InsertGroup(GetResString(_T("COMMITFILES")), iImgBackup, TVI_ROOT);
		m_htiCommitNever = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NEVER")), m_htiCommit, m_iCommitFiles == 0);
		m_htiCommitOnShutdown = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("ONSHUTDOWN")), m_htiCommit, m_iCommitFiles == 1);
		m_htiCommitAlways = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("ALWAYS")), m_htiCommit, m_iCommitFiles == 2);
		m_htiExtractMetaData = m_ctrlTreeOptions.InsertGroup(GetResString(_T("EXTRACT_META_DATA")), iImgMetaData, TVI_ROOT);
		m_htiExtractMetaDataNever = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NEVER")), m_htiExtractMetaData, m_iExtractMetaData == 0);
		m_htiExtractMetaDataID3Lib = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("META_DATA_ID3LIB")), m_htiExtractMetaData, m_iExtractMetaData == 1);
		m_htiResolveShellLinks = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RESOLVELINKS")), TVI_ROOT, m_bResolveShellLinks);

		/////////////////////////////////////////////////////////////////////////////
		// Logging group
		//
		m_htiLog2Disk = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG2DISK")), TVI_ROOT, m_bLog2Disk);
		if (thePrefs.GetEnableVerboseOptions()) {
			m_htiVerboseGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("VERBOSE")), iImgLog, TVI_ROOT);
			m_htiVerbose = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ENABLED")), m_htiVerboseGroup, m_bVerbose);
			m_htiLogLevel = m_ctrlTreeOptions.InsertItem(GetResString(_T("LOG_LEVEL")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiVerboseGroup);
			m_ctrlTreeOptions.AddEditBox(m_htiLogLevel, RUNTIME_CLASS(CNumTreeOptionsEdit));
			m_htiDebug2Disk = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG2DISK")), m_htiVerboseGroup, m_bDebug2Disk);
			m_htiDebugSourceExchange = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DEBUG_SOURCE_EXCHANGE")), m_htiVerboseGroup, m_bDebugSourceExchange);
			m_htiLogBannedClients = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_BANNED_CLIENTS")), m_htiVerboseGroup, m_bLogBannedClients);
			m_htiLogRatingDescReceived = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_RATING_RECV")), m_htiVerboseGroup, m_bLogRatingDescReceived);
			m_htiLogSecureIdent = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_SECURE_IDENT")), m_htiVerboseGroup, m_bLogSecureIdent);
			m_htiLogFilteredIPs = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_FILTERED_IPS")), m_htiVerboseGroup, m_bLogFilteredIPs);
			m_htiLogFileSaving = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_FILE_SAVING")), m_htiVerboseGroup, m_bLogFileSaving);
			m_htiLogA4AF = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_A4AF")), m_htiVerboseGroup, m_bLogA4AF); // ZZ:DownloadManager
			m_htiLogUlDlEvents = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_ULDL_EVENTS")), m_htiVerboseGroup, m_bLogUlDlEvents);
			m_htiLogSpamRating = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SPAMRATING_LOGEVENTS")), m_htiVerboseGroup, m_bLogSpamRating);
			m_htiLogRetryFailedTcp = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RETRYFAILEDTCP_LOGEVENTS")), m_htiVerboseGroup, m_bLogRetryFailedTcp);
			m_htiLogExtendedSXEvents = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EXTENDED_SOURCE_EXCHANGE_LOGEVENTS")), m_htiVerboseGroup, m_bLogExtendedSXEvents);
			m_htiLogNatTraversalEvents = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("NATTRAVERSAL_LOGEVENTS")), m_htiVerboseGroup, m_bLogNatTraversalEvents);
		}

		/////////////////////////////////////////////////////////////////////////////
		// USS group
		//
		m_htiDynUp = m_ctrlTreeOptions.InsertGroup(GetResString(_T("DYNUP")), iImgDynyp, TVI_ROOT);
		m_htiDynUpEnabled = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DYNUPENABLED")), m_htiDynUp, m_bDynUpEnabled);
		m_htiDynUpMinUpload = m_ctrlTreeOptions.InsertItem(GetResString(_T("DYNUP_MINUPLOAD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDynUp);
		m_ctrlTreeOptions.AddEditBox(m_htiDynUpMinUpload, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDynUpPingTolerance = m_ctrlTreeOptions.InsertItem(GetResString(_T("DYNUP_PINGTOLERANCE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDynUp);
		m_ctrlTreeOptions.AddEditBox(m_htiDynUpPingTolerance, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDynUpPingToleranceMilliseconds = m_ctrlTreeOptions.InsertItem(GetResString(_T("DYNUP_PINGTOLERANCE_MS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDynUp);
		m_ctrlTreeOptions.AddEditBox(m_htiDynUpPingToleranceMilliseconds, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDynUpPingToleranceGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("DYNUP_RADIO_PINGTOLERANCE_HEADER")), iImgDynyp, m_htiDynUp);
		m_htiDynUpRadioPingTolerance = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DYNUP_RADIO_PINGTOLERANCE_PERCENT")), m_htiDynUpPingToleranceGroup, m_iDynUpRadioPingTolerance == 0);
		m_htiDynUpRadioPingToleranceMilliseconds = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DYNUP_RADIO_PINGTOLERANCE_MS")), m_htiDynUpPingToleranceGroup, m_iDynUpRadioPingTolerance == 1);
		m_htiDynUpGoingUpDivider = m_ctrlTreeOptions.InsertItem(GetResString(_T("DYNUP_GOINGUPDIVIDER")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDynUp);
		m_ctrlTreeOptions.AddEditBox(m_htiDynUpGoingUpDivider, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDynUpGoingDownDivider = m_ctrlTreeOptions.InsertItem(GetResString(_T("DYNUP_GOINGDOWNDIVIDER")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDynUp);
		m_ctrlTreeOptions.AddEditBox(m_htiDynUpGoingDownDivider, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDynUpNumberOfPings = m_ctrlTreeOptions.InsertItem(GetResString(_T("DYNUP_NUMBEROFPINGS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDynUp);
		m_ctrlTreeOptions.AddEditBox(m_htiDynUpNumberOfPings, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// UPnP group
		//
		m_htiUPnP = m_ctrlTreeOptions.InsertGroup(GetResString(_T("UPNP")), iImgUPnP, TVI_ROOT);
		m_htiCloseUPnPPorts = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPNPCLOSEONEXIT")), m_htiUPnP, m_bCloseUPnPOnExit);
		m_htiSkipWANIPSetup = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPNPSKIPWANIP")), m_htiUPnP, m_bSkipWANIPSetup);
		m_htiSkipWANPPPSetup = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPNPSKIPWANPPP")), m_htiUPnP, m_bSkipWANPPPSetup);

		m_htiImportParts = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ENABLEIMPORTPARTS")), TVI_ROOT, m_bImportParts);

		m_ctrlTreeOptions.Expand(m_htiTCPGroup, TVE_EXPAND);
		if (m_htiVerboseGroup)
			m_ctrlTreeOptions.Expand(m_htiVerboseGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiCommit, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiCheckDiskspace, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiDynUp, m_bDynUpEnabled ? TVE_EXPAND : TVE_COLLAPSE);
		m_ctrlTreeOptions.Expand(m_htiDynUpPingToleranceGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiExtractMetaData, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiUPnP, TVE_EXPAND);
		m_ctrlTreeOptions.SendMessage(WM_VSCROLL, SB_TOP);
		m_bInitializedTreeOpts = true;
	}

	/////////////////////////////////////////////////////////////////////////////
	// TCP/IP group
	//
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiMaxCon5Sec, m_iMaxConnPerFive);
	DDV_MinMaxInt(pDX, m_iMaxConnPerFive, 1, INT_MAX);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiMaxHalfOpen, m_iMaxHalfOpen);
	DDV_MinMaxInt(pDX, m_iMaxHalfOpen, 1, INT_MAX);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiConditionalTCPAccept, m_bConditionalTCPAccept);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiServerKeepAliveTimeout, m_uServerKeepAliveTimeout);

	/////////////////////////////////////////////////////////////////////////////
	// Miscellaneous group
	//
	WORD wv = thePrefs.GetWindowsVersion();
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoTakeEd2kLinks, m_bAutoTakeEd2kLinks);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCreditSystem, m_bCreditSystem);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFirewallStartup, m_bFirewallStartup);
	m_ctrlTreeOptions.SetCheckBoxEnable(m_htiFirewallStartup, wv == _WINVER_XP_ && !IsRunningXPSP2());
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFilterLANIPs, m_bFilterLANIPs);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiExtControls, m_bExtControls);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiA4AFSaveCpu, m_bA4AFSaveCpu);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiYourHostname, m_sYourHostname);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoArch, m_bAutoArchDisable);

	/////////////////////////////////////////////////////////////////////////////
	// File related group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSparsePartFiles, m_bSparsePartFiles);
	m_ctrlTreeOptions.SetCheckBoxEnable(m_htiSparsePartFiles, wv != _WINVER_VISTA_ /*only disable on Vista, not later versions*/);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiImportParts, m_bImportParts);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFullAlloc, m_bFullAlloc);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCheckDiskspace, m_bCheckDiskspace);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiMinFreeDiskSpace, m_fMinFreeDiskSpaceMB);
	DDV_MinMaxFloat(pDX, m_fMinFreeDiskSpaceMB, 0.0, _UI32_MAX / (1024.0f * 1024.0f));
	UINT m_uTemp = SEC2MIN(m_uFreeDiskSpaceCheckPeriod);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiFreeDiskSpaceCheckPeriod, m_uTemp);
	DDV_MinMaxInt(pDX, m_uTemp, 1, 60);
	m_uFreeDiskSpaceCheckPeriod = MIN2S(m_uTemp);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiCommit, m_iCommitFiles);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiExtractMetaData, m_iExtractMetaData);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiResolveShellLinks, m_bResolveShellLinks);

	/////////////////////////////////////////////////////////////////////////////
	// Logging group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLog2Disk, m_bLog2Disk);
	if (m_htiLogLevel) {
		DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiLogLevel, m_iLogLevel);
		DDV_MinMaxInt(pDX, m_iLogLevel, 1, 5);
	}
	if (m_htiVerbose)
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiVerbose, m_bVerbose);
	if (m_htiDebug2Disk) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiDebug2Disk, m_bDebug2Disk);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebug2Disk, m_bVerbose);
	}
	if (m_htiDebugSourceExchange) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiDebugSourceExchange, m_bDebugSourceExchange);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebugSourceExchange, m_bVerbose);
	}
	if (m_htiLogBannedClients) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogBannedClients, m_bLogBannedClients);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogBannedClients, m_bVerbose);
	}
	if (m_htiLogRatingDescReceived) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogRatingDescReceived, m_bLogRatingDescReceived);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogRatingDescReceived, m_bVerbose);
	}
	if (m_htiLogSecureIdent) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogSecureIdent, m_bLogSecureIdent);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogSecureIdent, m_bVerbose);
	}
	if (m_htiLogFilteredIPs) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogFilteredIPs, m_bLogFilteredIPs);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFilteredIPs, m_bVerbose);
	}
	if (m_htiLogFileSaving) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogFileSaving, m_bLogFileSaving);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFileSaving, m_bVerbose);
	}
	if (m_htiLogA4AF) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogA4AF, m_bLogA4AF);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogA4AF, m_bVerbose);
	}
	if (m_htiLogUlDlEvents) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogUlDlEvents, m_bLogUlDlEvents);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogUlDlEvents, m_bVerbose);
	}
	if (m_htiLogSpamRating) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogSpamRating, m_bLogSpamRating);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogSpamRating, m_bVerbose);
	}
	if (m_htiLogRetryFailedTcp) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogRetryFailedTcp, m_bLogRetryFailedTcp);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogRetryFailedTcp, m_bVerbose);
	}
	if (m_htiLogExtendedSXEvents) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogExtendedSXEvents, m_bLogExtendedSXEvents);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogExtendedSXEvents, m_bVerbose);
	}
	if (m_htiLogNatTraversalEvents) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogNatTraversalEvents, m_bLogNatTraversalEvents);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogNatTraversalEvents, m_bVerbose);
	}
	/////////////////////////////////////////////////////////////////////////////
	// USS group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiDynUpEnabled, m_bDynUpEnabled);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDynUpMinUpload, m_iDynUpMinUpload);
	DDV_MinMaxInt(pDX, m_iDynUpMinUpload, 1, INT_MAX);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDynUpPingTolerance, m_iDynUpPingTolerance);
	DDV_MinMaxInt(pDX, m_iDynUpPingTolerance, 100, INT_MAX);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDynUpPingToleranceMilliseconds, m_iDynUpPingToleranceMilliseconds);
	DDV_MinMaxInt(pDX, m_iDynUpPingTolerance, 1, INT_MAX);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiDynUpPingToleranceGroup, m_iDynUpRadioPingTolerance);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDynUpGoingUpDivider, m_iDynUpGoingUpDivider);
	DDV_MinMaxInt(pDX, m_iDynUpGoingUpDivider, 1, INT_MAX);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDynUpGoingDownDivider, m_iDynUpGoingDownDivider);
	DDV_MinMaxInt(pDX, m_iDynUpGoingDownDivider, 1, INT_MAX);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDynUpNumberOfPings, m_iDynUpNumberOfPings);
	DDV_MinMaxInt(pDX, m_iDynUpNumberOfPings, 1, INT_MAX);

	/////////////////////////////////////////////////////////////////////////////
	// UPnP group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCloseUPnPPorts, m_bCloseUPnPOnExit);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSkipWANIPSetup, m_bSkipWANIPSetup);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSkipWANPPPSetup, m_bSkipWANPPPSetup);
}

BOOL CPPgTweaks::OnInitDialog()
{
	m_iMaxConnPerFive = thePrefs.GetMaxConperFive();
	m_iMaxHalfOpen = thePrefs.GetMaxHalfConnections();
	m_bConditionalTCPAccept = thePrefs.GetConditionalTCPAccept();
	m_bAutoTakeEd2kLinks = thePrefs.AutoTakeED2KLinks();
	if (thePrefs.GetEnableVerboseOptions()) {
		m_bVerbose = thePrefs.m_bVerbose;
		m_bDebug2Disk = thePrefs.debug2disk;							// do *not* use the corresponding 'Get...' function here!
		m_bDebugSourceExchange = thePrefs.m_bDebugSourceExchange;		// do *not* use the corresponding 'Get...' function here!
		m_bLogBannedClients = thePrefs.m_bLogBannedClients;				// do *not* use the corresponding 'Get...' function here!
		m_bLogRatingDescReceived = thePrefs.m_bLogRatingDescReceived;	// do *not* use the corresponding 'Get...' function here!
		m_bLogSecureIdent = thePrefs.m_bLogSecureIdent;					// do *not* use the corresponding 'Get...' function here!
		m_bLogFilteredIPs = thePrefs.m_bLogFilteredIPs;					// do *not* use the corresponding 'Get...' function here!
		m_bLogFileSaving = thePrefs.m_bLogFileSaving;					// do *not* use the corresponding 'Get...' function here!
		m_bLogA4AF = thePrefs.m_bLogA4AF;							    // do *not* use the corresponding 'Get...' function here! // ZZ:DownloadManager
		m_bLogUlDlEvents = thePrefs.m_bLogUlDlEvents;
		m_bLogSpamRating = thePrefs.m_bLogSpamRating;
		m_bLogRetryFailedTcp = thePrefs.m_bLogRetryFailedTcp;
		m_bLogExtendedSXEvents = thePrefs.m_bLogExtendedSXEvents;
		m_bLogNatTraversalEvents = thePrefs.m_bLogNatTraversalEvents;
		m_iLogLevel = 5 - thePrefs.m_byLogLevel;
	}
	m_bLog2Disk = thePrefs.log2disk;
	m_bCreditSystem = thePrefs.m_bCreditSystem;
	m_iCommitFiles = thePrefs.m_iCommitFiles;
	m_iExtractMetaData = thePrefs.m_iExtractMetaData;
	m_bFilterLANIPs = thePrefs.filterLANIPs;
	m_bExtControls = thePrefs.m_bExtControls;
	m_uServerKeepAliveTimeout = thePrefs.m_dwServerKeepAliveTimeout / MIN2MS(1);
	m_bSparsePartFiles = thePrefs.GetSparsePartFiles();
	m_bImportParts = thePrefs.m_bImportParts;
	m_bFullAlloc = thePrefs.m_bAllocFull;
	m_bCheckDiskspace = thePrefs.checkDiskspace;
	m_bResolveShellLinks = thePrefs.GetResolveSharedShellLinks();
	m_fMinFreeDiskSpaceMB = (float)(thePrefs.m_uMinFreeDiskSpace / (1024.0 * 1024.0));
	m_uFreeDiskSpaceCheckPeriod = thePrefs.m_uFreeDiskSpaceCheckPeriod;
	m_sYourHostname = thePrefs.GetYourHostname();
	m_bFirewallStartup = ((thePrefs.GetWindowsVersion() == _WINVER_XP_) ? thePrefs.m_bOpenPortsOnStartUp : 0);
	m_bAutoArchDisable = !thePrefs.m_bAutomaticArcPreviewStart;

	m_bDynUpEnabled = thePrefs.m_bDynUpEnabled;
	m_iDynUpMinUpload = thePrefs.GetMinUpload();
	m_iDynUpPingTolerance = thePrefs.GetDynUpPingTolerance();
	m_iDynUpPingToleranceMilliseconds = thePrefs.GetDynUpPingToleranceMilliseconds();
	m_iDynUpRadioPingTolerance = static_cast<int>(thePrefs.IsDynUpUseMillisecondPingTolerance());
	m_iDynUpGoingUpDivider = thePrefs.GetDynUpGoingUpDivider();
	m_iDynUpGoingDownDivider = thePrefs.GetDynUpGoingDownDivider();
	m_iDynUpNumberOfPings = thePrefs.GetDynUpNumberOfPings();

	m_bCloseUPnPOnExit = thePrefs.CloseUPnPOnExit();
	m_bSkipWANIPSetup = thePrefs.GetSkipWANIPSetup();
	m_bSkipWANPPPSetup = thePrefs.GetSkipWANPPPSetup();

	m_bA4AFSaveCpu = thePrefs.GetA4AFSaveCpu();

	m_ctrlTreeOptions.SetImageListColorFlags(theApp.m_iDfltImageListColorFlags);
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	m_ctrlTreeOptions.SetItemHeight(m_ctrlTreeOptions.GetItemHeight() + 2);

	m_uFileBufferTimeLimit = thePrefs.GetFileBufferTimeLimit() / 60000 ;
	m_ctlFileBufferTimeLimit.SetRange(1, 30, TRUE);
	m_ctlFileBufferTimeLimit.SetPos(m_uFileBufferTimeLimit);
	m_ctlFileBufferTimeLimit.SetTicFreq(1);
	m_ctlFileBufferTimeLimit.SetPageSize(1);

	m_uFileBufferSize = thePrefs.m_uFileBufferSize;
	m_ctlFileBuffSize.SetRange(1, 80, TRUE);
	m_ctlFileBuffSize.SetPos(m_uFileBufferSize / (1024 * 512));
	m_ctlFileBuffSize.SetTicFreq(2);
	m_ctlFileBuffSize.SetPageSize(2);

	m_iQueueSize = thePrefs.m_iQueueSize;
	m_ctlQueueSize.SetRange(20, 200, TRUE);
	m_ctlQueueSize.SetPos((int)(m_iQueueSize / 100));
	m_ctlQueueSize.SetTicFreq(10);
	m_ctlQueueSize.SetPageSize(10);

	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPgTweaks::OnKillActive()
{
	// if prop page is closed by pressing ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	return CPropertyPage::OnKillActive();
}

BOOL CPPgTweaks::OnApply()
{
	// if prop page is closed by pressing ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();

	if (!UpdateData())
		return FALSE;

	if (thePrefs.m_bVerbose != m_bVerbose || thePrefs.m_iCommitFiles != m_iCommitFiles) {
		CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
		if (pThread) {
			CSingleLock sSavePartFilePrefsLock(&pThread->m_lockSavePartFilePrefs, TRUE);
			pThread->m_bVerbose = m_bVerbose;
			pThread->m_iCommitFiles = m_iCommitFiles;
		}
	}

	thePrefs.SetMaxConsPerFive(m_iMaxConnPerFive ? m_iMaxConnPerFive : DFLT_MAXCONPERFIVE);
	theApp.scheduler->original_cons5s = thePrefs.GetMaxConperFive();
	thePrefs.SetMaxHalfConnections(m_iMaxHalfOpen ? m_iMaxHalfOpen : DFLT_MAXHALFOPEN);
	thePrefs.m_bConditionalTCPAccept = m_bConditionalTCPAccept;

	if (thePrefs.AutoTakeED2KLinks() != m_bAutoTakeEd2kLinks) {
		thePrefs.autotakeed2klinks = m_bAutoTakeEd2kLinks;
		if (thePrefs.AutoTakeED2KLinks())
			Ask4RegFix(false, true, false);
		else
			RevertReg();
	}

	if (!thePrefs.log2disk && m_bLog2Disk)
		theLog.Open();
	else if (thePrefs.log2disk && !m_bLog2Disk)
		theLog.Close();
	thePrefs.log2disk = m_bLog2Disk;

	if (thePrefs.GetEnableVerboseOptions()) {
		if (!thePrefs.GetDebug2Disk() && m_bVerbose && m_bDebug2Disk)
			theVerboseLog.Open();
		else if (thePrefs.GetDebug2Disk() && (!m_bVerbose || !m_bDebug2Disk))
			theVerboseLog.Close();
		thePrefs.debug2disk = m_bDebug2Disk;

		thePrefs.m_bDebugSourceExchange = m_bDebugSourceExchange;
		thePrefs.m_bLogBannedClients = m_bLogBannedClients;
		thePrefs.m_bLogRatingDescReceived = m_bLogRatingDescReceived;
		thePrefs.m_bLogSecureIdent = m_bLogSecureIdent;
		thePrefs.m_bLogFilteredIPs = m_bLogFilteredIPs;
		thePrefs.m_bLogFileSaving = m_bLogFileSaving;
		thePrefs.m_bLogA4AF = m_bLogA4AF;
		thePrefs.m_bLogUlDlEvents = m_bLogUlDlEvents;
		thePrefs.m_bLogSpamRating = m_bLogSpamRating;
		thePrefs.m_bLogRetryFailedTcp = m_bLogRetryFailedTcp;
		thePrefs.m_bLogExtendedSXEvents = m_bLogExtendedSXEvents;
		thePrefs.m_bLogNatTraversalEvents = m_bLogNatTraversalEvents;
		thePrefs.m_byLogLevel = 5 - m_iLogLevel;

		thePrefs.m_bVerbose = m_bVerbose; // store after related options were stored!
	}

	thePrefs.m_bCreditSystem = m_bCreditSystem;
	thePrefs.m_iCommitFiles = m_iCommitFiles;
	thePrefs.m_iExtractMetaData = m_iExtractMetaData;
	thePrefs.filterLANIPs = m_bFilterLANIPs;
	thePrefs.m_uFileBufferTimeLimit = MIN2MS(m_uFileBufferTimeLimit);
	thePrefs.m_uFileBufferSize = m_uFileBufferSize;
	thePrefs.m_iQueueSize = m_iQueueSize;

	bool bUpdateDLmenu = (thePrefs.m_bImportParts != m_bImportParts);
	thePrefs.m_bImportParts = m_bImportParts;
	if (thePrefs.m_bExtControls != m_bExtControls) {
		bUpdateDLmenu = true;
		thePrefs.m_bExtControls = m_bExtControls;
		theApp.emuledlg->searchwnd->CreateMenus();
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.CreateMenus();
	}
	if (bUpdateDLmenu)
		theApp.emuledlg->transferwnd->GetDownloadList()->CreateMenus();

	thePrefs.m_dwServerKeepAliveTimeout = MIN2MS(m_uServerKeepAliveTimeout);
	thePrefs.m_bSparsePartFiles = m_bSparsePartFiles;
	thePrefs.m_bAllocFull = m_bFullAlloc;
	thePrefs.checkDiskspace = m_bCheckDiskspace;
	thePrefs.m_bResolveSharedShellLinks = m_bResolveShellLinks;
	thePrefs.m_uMinFreeDiskSpace = (UINT)(m_fMinFreeDiskSpaceMB * (1024 * 1024));
	thePrefs.m_uFreeDiskSpaceCheckPeriod = m_uFreeDiskSpaceCheckPeriod;
	if (thePrefs.GetYourHostname() != m_sYourHostname) {
		thePrefs.SetYourHostname(m_sYourHostname);
		theApp.emuledlg->serverwnd->UpdateMyInfo();
	}
	thePrefs.m_bOpenPortsOnStartUp = m_bFirewallStartup;

	thePrefs.m_bDynUpEnabled = m_bDynUpEnabled;
	thePrefs.m_minupload = (uint32)m_iDynUpMinUpload;
	thePrefs.m_iDynUpPingTolerance = m_iDynUpPingTolerance;
	thePrefs.m_iDynUpPingToleranceMilliseconds = m_iDynUpPingToleranceMilliseconds;
	thePrefs.m_bDynUpUseMillisecondPingTolerance = (m_iDynUpRadioPingTolerance == 1);
	thePrefs.m_iDynUpGoingUpDivider = m_iDynUpGoingUpDivider;
	thePrefs.m_iDynUpGoingDownDivider = m_iDynUpGoingDownDivider;
	thePrefs.m_iDynUpNumberOfPings = m_iDynUpNumberOfPings;
	thePrefs.m_bAutomaticArcPreviewStart = !m_bAutoArchDisable;

	thePrefs.m_bCloseUPnPOnExit = m_bCloseUPnPOnExit;
	thePrefs.SetSkipWANIPSetup(m_bSkipWANIPSetup);
	thePrefs.SetSkipWANPPPSetup(m_bSkipWANPPPSetup);

	thePrefs.m_bA4AFSaveCpu = m_bA4AFSaveCpu;

	if (thePrefs.GetEnableVerboseOptions()) {
		theApp.emuledlg->serverwnd->ToggleDebugWindow();
		theApp.emuledlg->serverwnd->UpdateLogTabSelection();
	}
	theApp.downloadqueue->CheckDiskspace();

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgTweaks::OnHScroll(UINT /*nSBCode*/, UINT /*nPos*/, CScrollBar *pScrollBar)
{
	if (pScrollBar->GetSafeHwnd() == m_ctlFileBuffSize.m_hWnd) {
		m_uFileBufferSize = m_ctlFileBuffSize.GetPos() * 1024 * 512;
		CString temp(GetResString(_T("FILEBUFFERSIZE")));
		temp.AppendFormat(_T(": %s"), (LPCTSTR)CastItoXBytes(m_uFileBufferSize));
		SetDlgItemText(IDC_FILEBUFFERSIZE_STATIC, temp);
		SetModified(TRUE);
	} else if (pScrollBar->GetSafeHwnd() == m_ctlQueueSize.m_hWnd) {
		m_iQueueSize = reinterpret_cast<CSliderCtrl*>(pScrollBar)->GetPos() * 100;
		CString temp(GetResString(_T("QUEUESIZE")));
		temp.AppendFormat(_T(": %s"), (LPCTSTR)GetFormatedUInt((ULONG)m_iQueueSize));
		SetDlgItemText(IDC_QUEUESIZE_STATIC, temp);
		SetModified(TRUE);
	}
	else if (pScrollBar->GetSafeHwnd() == m_ctlFileBufferTimeLimit.m_hWnd)  // moved down - sFrQlXeRt
	{
		m_uFileBufferTimeLimit = m_ctlFileBufferTimeLimit.GetPos();
		CString temp;
		temp.Format(_T("%s: %i min"), GetResString(_T("BTL_TEXT")), m_uFileBufferTimeLimit);
		GetDlgItem(IDC_BTL_TEXT)->SetWindowText(temp);
		SetModified(TRUE);
	}
}

void CPPgTweaks::LocalizeItemText(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetItemText(item, GetResString(strid));
}

void CPPgTweaks::LocalizeEditLabel(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetEditLabel(item, GetResString(strid));
}

void CPPgTweaks::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_TWEAK")));
		SetDlgItemText(IDC_WARNING, GetResString(_T("TWEAKS_WARNING")));
		SetDlgItemText(IDC_PREFINI_STATIC, GetResString(_T("PW_TWEAK")));
		SetDlgItemText(IDC_OPENPREFINI, GetResString(_T("OPENPREFINI")));

		LocalizeEditLabel(m_htiDynUpGoingDownDivider, _T("DYNUP_GOINGDOWNDIVIDER"));
		LocalizeEditLabel(m_htiDynUpGoingUpDivider, _T("DYNUP_GOINGUPDIVIDER"));
		LocalizeEditLabel(m_htiDynUpMinUpload, _T("DYNUP_MINUPLOAD"));
		LocalizeEditLabel(m_htiDynUpNumberOfPings, _T("DYNUP_NUMBEROFPINGS"));
		LocalizeEditLabel(m_htiDynUpPingTolerance, _T("DYNUP_PINGTOLERANCE"));
		LocalizeEditLabel(m_htiLogLevel, _T("LOG_LEVEL"));
		LocalizeEditLabel(m_htiMaxCon5Sec, _T("MAXCON5SECLABEL"));
		LocalizeEditLabel(m_htiMaxHalfOpen, _T("MAXHALFOPENCONS"));
		LocalizeEditLabel(m_htiMinFreeDiskSpace, _T("MINFREEDISKSPACE"));
		LocalizeEditLabel(m_htiFreeDiskSpaceCheckPeriod, _T("FREEDISKSPACECHECKPERIOD"));
		LocalizeEditLabel(m_htiServerKeepAliveTimeout, _T("SERVERKEEPALIVETIMEOUT"));
		LocalizeEditLabel(m_htiYourHostname, _T("YOURHOSTNAME"));	// itsonlyme: hostnameSource
		LocalizeItemText(m_htiA4AFSaveCpu, _T("A4AF_SAVE_CPU"));
		LocalizeItemText(m_htiAutoArch, _T("DISABLE_AUTOARCHPREV"));
		LocalizeItemText(m_htiAutoTakeEd2kLinks, _T("AUTOTAKEED2KLINKS"));
		LocalizeItemText(m_htiCheckDiskspace, _T("CHECKDISKSPACE"));
		LocalizeItemText(m_htiCloseUPnPPorts, _T("UPNPCLOSEONEXIT"));
		LocalizeItemText(m_htiCommit, _T("COMMITFILES"));
		LocalizeItemText(m_htiCommitAlways, _T("ALWAYS"));
		LocalizeItemText(m_htiCommitNever, _T("NEVER"));
		LocalizeItemText(m_htiCommitOnShutdown, _T("ONSHUTDOWN"));
		LocalizeItemText(m_htiConditionalTCPAccept, _T("CONDTCPACCEPT"));
		LocalizeItemText(m_htiCreditSystem, _T("USECREDITSYSTEM"));
		LocalizeItemText(m_htiDebug2Disk, _T("LOG2DISK"));
		LocalizeItemText(m_htiDebugSourceExchange, _T("DEBUG_SOURCE_EXCHANGE"));
		LocalizeItemText(m_htiDynUp, _T("DYNUP"));
		LocalizeItemText(m_htiDynUpEnabled, _T("DYNUPENABLED"));
		LocalizeItemText(m_htiExtControls, _T("SHOWEXTSETTINGS"));
		LocalizeItemText(m_htiExtractMetaData, _T("EXTRACT_META_DATA"));
		LocalizeItemText(m_htiExtractMetaDataID3Lib, _T("META_DATA_ID3LIB"));
		LocalizeItemText(m_htiExtractMetaDataNever, _T("NEVER"));
		LocalizeItemText(m_htiFilterLANIPs, _T("PW_FILTER"));
		LocalizeItemText(m_htiFirewallStartup, _T("FO_PREF_STARTUP"));
		LocalizeItemText(m_htiFullAlloc, _T("FULLALLOC"));
		LocalizeItemText(m_htiImportParts, _T("ENABLEIMPORTPARTS"));
		LocalizeItemText(m_htiLog2Disk, _T("LOG2DISK"));
		LocalizeItemText(m_htiLogA4AF, _T("LOG_A4AF"));
		LocalizeItemText(m_htiLogBannedClients, _T("LOG_BANNED_CLIENTS"));
		LocalizeItemText(m_htiLogFileSaving, _T("LOG_FILE_SAVING"));
		LocalizeItemText(m_htiLogFilteredIPs, _T("LOG_FILTERED_IPS"));
		LocalizeItemText(m_htiLogRatingDescReceived, _T("LOG_RATING_RECV"));
		LocalizeItemText(m_htiLogSecureIdent, _T("LOG_SECURE_IDENT"));
		LocalizeItemText(m_htiLogUlDlEvents, _T("LOG_ULDL_EVENTS"));
		LocalizeItemText(m_htiLogSpamRating, _T("SPAMRATING_LOGEVENTS"));
		LocalizeItemText(m_htiLogRetryFailedTcp, _T("RETRYFAILEDTCP_LOGEVENTS"));
		LocalizeItemText(m_htiLogExtendedSXEvents, _T("EXTENDED_SOURCE_EXCHANGE_LOGEVENTS"));
		LocalizeItemText(m_htiLogNatTraversalEvents, _T("NATTRAVERSAL_LOGEVENTS"));
		LocalizeItemText(m_htiResolveShellLinks, _T("RESOLVELINKS"));
		LocalizeItemText(m_htiSkipWANIPSetup, _T("UPNPSKIPWANIP"));
		LocalizeItemText(m_htiSkipWANPPPSetup, _T("UPNPSKIPWANPPP"));
		LocalizeItemText(m_htiSparsePartFiles, _T("SPARSEPARTFILES"));
		LocalizeItemText(m_htiTCPGroup, _T("TCPIP_CONNS"));
		LocalizeItemText(m_htiUPnP, _T("UPNP"));
		LocalizeItemText(m_htiVerbose, _T("ENABLED"));
		LocalizeItemText(m_htiVerboseGroup, _T("VERBOSE"));

		CString temp;
		temp.Format(_T("%s: %i min"), GetResString(_T("BTL_TEXT")), m_uFileBufferTimeLimit);
		GetDlgItem(IDC_BTL_TEXT)->SetWindowText(temp);
		temp.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("FILEBUFFERSIZE")), (LPCTSTR)CastItoXBytes(m_uFileBufferSize));
		SetDlgItemText(IDC_FILEBUFFERSIZE_STATIC, temp);
		temp.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("QUEUESIZE")), (LPCTSTR)GetFormatedUInt((ULONG)m_iQueueSize));
		SetDlgItemText(IDC_QUEUESIZE_STATIC, temp);
	}
}

void CPPgTweaks::OnDestroy()
{
	m_ctrlTreeOptions.DeleteAllItems();
	m_ctrlTreeOptions.DestroyWindow();
	m_bInitializedTreeOpts = false;
	m_htiTCPGroup = NULL;
	m_htiMaxCon5Sec = NULL;
	m_htiMaxHalfOpen = NULL;
	m_htiConditionalTCPAccept = NULL;
	m_htiAutoTakeEd2kLinks = NULL;
	m_htiVerboseGroup = NULL;
	m_htiVerbose = NULL;
	m_htiDebugSourceExchange = NULL;
	m_htiLogBannedClients = NULL;
	m_htiLogRatingDescReceived = NULL;
	m_htiLogSecureIdent = NULL;
	m_htiLogFilteredIPs = NULL;
	m_htiLogFileSaving = NULL;
	m_htiLogA4AF = NULL;
	m_htiLogLevel = NULL;
	m_htiLogUlDlEvents = NULL;
	m_htiLogSpamRating = NULL;
	m_htiLogRetryFailedTcp = NULL;
	m_htiLogExtendedSXEvents = NULL;
	m_htiLogNatTraversalEvents = NULL;
	m_htiCreditSystem = NULL;
	m_htiLog2Disk = NULL;
	m_htiDebug2Disk = NULL;
	m_htiCommit = NULL;
	m_htiCommitNever = NULL;
	m_htiCommitOnShutdown = NULL;
	m_htiCommitAlways = NULL;
	m_htiFilterLANIPs = NULL;
	m_htiExtControls = NULL;
	m_htiServerKeepAliveTimeout = NULL;
	m_htiSparsePartFiles = NULL;
	m_htiImportParts = NULL;
	m_htiFullAlloc = NULL;
	m_htiCheckDiskspace = NULL;
	m_htiMinFreeDiskSpace = NULL;
	m_htiFreeDiskSpaceCheckPeriod = NULL;
	m_htiYourHostname = NULL;
	m_htiFirewallStartup = NULL;
	m_htiDynUp = NULL;
	m_htiDynUpEnabled = NULL;
	m_htiDynUpMinUpload = NULL;
	m_htiDynUpPingTolerance = NULL;
	m_htiDynUpPingToleranceMilliseconds = NULL;
	m_htiDynUpPingToleranceGroup = NULL;
	m_htiDynUpRadioPingTolerance = NULL;
	m_htiDynUpRadioPingToleranceMilliseconds = NULL;
	m_htiDynUpGoingUpDivider = NULL;
	m_htiDynUpGoingDownDivider = NULL;
	m_htiDynUpNumberOfPings = NULL;
	m_htiA4AFSaveCpu = NULL;
	m_htiExtractMetaData = NULL;
	m_htiExtractMetaDataNever = NULL;
	m_htiExtractMetaDataID3Lib = NULL;
	m_htiAutoArch = NULL;
	m_htiUPnP = NULL;
	m_htiCloseUPnPPorts = NULL;
	m_htiSkipWANIPSetup = NULL;
	m_htiSkipWANPPPSetup = NULL;
	m_htiResolveShellLinks = NULL;

	CPropertyPage::OnDestroy();
}

LRESULT CPPgTweaks::OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam)
{
	if (wParam == IDC_EXT_OPTS) {
		TREEOPTSCTRLNOTIFY *pton = (TREEOPTSCTRLNOTIFY*)lParam;
		if (m_htiVerbose && pton->hItem == m_htiVerbose) {
			BOOL bCheck;
			if (m_ctrlTreeOptions.GetCheckBox(m_htiVerbose, bCheck)) {
				if (m_htiDebug2Disk)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebug2Disk, bCheck);
				if (m_htiDebugSourceExchange)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebugSourceExchange, bCheck);
				if (m_htiLogBannedClients)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogBannedClients, bCheck);
				if (m_htiLogRatingDescReceived)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogRatingDescReceived, bCheck);
				if (m_htiLogSecureIdent)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogSecureIdent, bCheck);
				if (m_htiLogFilteredIPs)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFilteredIPs, bCheck);
				if (m_htiLogFileSaving)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFileSaving, bCheck);
				if (m_htiLogA4AF)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogA4AF, bCheck);
				if (m_htiLogUlDlEvents)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogUlDlEvents, bCheck);
				if (m_htiLogSpamRating)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogSpamRating, bCheck);
				if (m_htiLogRetryFailedTcp)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogRetryFailedTcp, bCheck);
				if (m_htiLogExtendedSXEvents)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogExtendedSXEvents, bCheck);
				if (m_htiLogNatTraversalEvents)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogNatTraversalEvents, bCheck);
			}
		}
		SetModified();
	}
	return 0;
}

void CPPgTweaks::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Extended_Settings);
}

BOOL CPPgTweaks::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgTweaks::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgTweaks::OnBnClickedOpenprefini()
{
	ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("preferences.ini"));
}
