//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "emule.h"
#include "MuleStatusbarCtrl.h"
#include "ClientList.h"
#include "PPgProtectionPanel.h"
#include "emuledlg.h"
#include "UserMsgs.h"
#include "OtherFunctions.h"
#include "eMuleAI/Shield.h"
#include <Log.h>
#include "UpDownClient.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

///////////////////////////////////////////////////////////////////////////////
// CPPgProtectionPanel dialog

IMPLEMENT_DYNAMIC(CPPgProtectionPanel, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgProtectionPanel, CPropertyPage)
	ON_WM_HSCROLL()
	ON_WM_DESTROY()
	ON_MESSAGE(UM_TREEOPTSCTRL_NOTIFY, OnTreeOptsCtrlNotify)
	ON_MESSAGE(WM_TREEITEM_HELP, DrawTreeItemHelp)
	ON_BN_CLICKED(IDC_SHIELD_RELOAD, OnBnClickedShieldReload)
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
END_MESSAGE_MAP()

CPPgProtectionPanel::CPPgProtectionPanel()
	: CPropertyPage(CPPgProtectionPanel::IDD)
	, m_ctrlTreeOptions(theApp.m_iDfltImageListColorFlags)
{
	m_bInitializedTreeOpts = false;

	m_htiGeneralOptions = NULL;
	m_htiTimingOptions = NULL;
	m_htiPunishmentCancelationScanPeriod = NULL;
	m_htiClientBanTime = NULL; // adjust ClientBanTime - Stulle
	m_htiClientScoreReducingTime = NULL;
	m_htiDontPunishFriends = NULL;
	m_htiDontAllowFileHotSwapping = NULL;
	m_htiInformBadClients = NULL; // => Inform Leechers - sFrQlXeRt
	m_htiInformBadClientsText = NULL; // => Inform Leechers Text - evcz
	m_htiAntiUploadProtection = NULL;
	m_htiAntiUploadProtectionLimit = NULL;
	m_htiUploaderPunishmentPrevention = NULL;
	m_htiUploaderPunishmentPreventionLimit = NULL;
	m_htiAntiCase1 = NULL;
	m_htiAntiCase2 = NULL;
	m_htiAntiCase3 = NULL;

	m_htiLeecherDetection = NULL;
	m_htiDetectModNames = NULL;
	m_htiDetectUserNames = NULL;
	m_htiDetectUserHashes = NULL;
	m_htiIPUserHash = NULL;
	m_htiUserHashBan = NULL;
	m_htiUploadBan = NULL;
	m_htiScore01 = NULL;
	m_htiScore02 = NULL;
	m_htiScore03 = NULL;
	m_htiScore04 = NULL;
	m_htiScore05 = NULL;
	m_htiScore06 = NULL;
	m_htiScore07 = NULL;
	m_htiScore08 = NULL;
	m_htiScore09 = NULL;
	m_htiNoPunishment = NULL;
	m_htiBanBadKadNodes = NULL;
	m_htiDetectAntiP2PBots = NULL;
	m_htiAntiP2PBotsPunishment = NULL;
	m_htiDetectUnknownTag = NULL;
	m_htiUnknownTagPunishment = NULL;
	m_htiDetectHashThief = NULL;
	m_htiHashThiefPunishment = NULL;
	m_htiDetectModThief = NULL;
	m_htiModThiefPunishment = NULL;
	m_htiDetectUserNameThief = NULL;
	m_htiUserNameThiefPunishment = NULL;
	m_htiDetectModChanger = NULL;
	m_htiModChangerInterval = NULL;
	m_htiModChangerThreshold = NULL;
	m_htiModChangerPunishment = NULL;
	m_htiDetectUserNameChanger = NULL;
	m_htiUserNameChangerInterval = NULL;
	m_htiUserNameChangerThreshold = NULL;
	m_htiUserNameChangerPunishment = NULL;
	m_htiDetectTCPErrorFlooder = NULL;
	m_htiTCPErrorFlooderInterval = NULL;
	m_htiTCPErrorFlooderThreshold = NULL;
	m_htiTCPErrorFlooderPunishment = NULL;
	m_htiDetectEmptyUserNameEmule = NULL;
	m_htiEmptyUserNameEmulePunishment = NULL;
	m_htiDetectCommunity = NULL;
	m_htiCommunityPunishment = NULL;
	m_htiDetectFakeEmule = NULL;
	m_htiFakeEmulePunishment = NULL;
	m_htiDetectHexModName = NULL;
	m_htiHexModNamePunishment = NULL;
	m_htiDetectGhostMod = NULL;
	m_htiGhostModPunishment = NULL;
	m_htiDetectSpam = NULL;
	m_htiSpamPunishment = NULL;
	m_htiDetectEmcrypt = NULL;
	m_htiEmcryptPunishment = NULL;
	m_htiDetectXSExploiter = NULL;
	m_htiXSExploiterPunishment = NULL;
	m_htiDetectFileFaker = NULL;
	m_htiFileFakerPunishment = NULL;
	m_htiDetectUploadFaker = NULL;
	m_htiUploadFakerPunishment = NULL;
	m_htiDetectUploadRequestAbuse = NULL;
	m_htiDetectUploadRequestAbuseNoRequestSlots = NULL;
	m_htiDetectUploadRequestAbuseQueueDrops = NULL;
	m_htiUploadRequestAbuseCounterGuard = NULL;
	m_htiUploadRequestAbuseHashRotationTracking = NULL;
	m_htiUploadRequestAbusePostHelloDisconnect = NULL;
	m_htiUploadRequestAbusePunishment = NULL;
	
	m_htiDetectAgressive = NULL;
	m_htiAgressiveTime = NULL;
	m_htiAgressiveCounter = NULL;
	m_htiAgressiveLog = NULL;
	m_htiAgressivePunishment = NULL;

	m_htiPunishNonSuiClients = NULL;
	m_htiPunishNonSuiMlDonkey = NULL;
	m_htiPunishNonSuiEdonkey = NULL;
	m_htiPunishNonSuiEdonkeyHybrid = NULL;
	m_htiPunishNonSuiShareaza = NULL;
	m_htiPunishNonSuiLphant = NULL;
	m_htiPunishNonSuiAmule = NULL;
	m_htiPunishNonSuiEmule = NULL;
	m_htiNonSuiPunishment = NULL;

	m_htiTweakOfficialFeatures = NULL;
	m_htiDetectCorruptedDataSender = NULL;
	m_htiDetectHashChanger = NULL;
	m_htiDetectFileScanner = NULL;
	m_htiDetectRankFlooder = NULL;
	m_htiDetectKadRequestFlooder = NULL;
	m_htiKadRequestFloodBanTreshold = NULL;
}

CPPgProtectionPanel::~CPPgProtectionPanel()
{
}

void CPPgProtectionPanel::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PROTECTION_PANEL_OPTS, m_ctrlTreeOptions);
	if (!m_bInitializedTreeOpts)
	{
		int	iImgGeneralOptions = 8;
		int iImgTimingOptions = 8;
		int iImgLeecher = 8;
		int iImgPunishment = 8;
		int	iImgPunishNonSuiClients = 8;
		int iImgTweakOfficialFeatures = 8;
		CImageList* piml = m_ctrlTreeOptions.GetImageList(TVSIL_NORMAL);
		if (piml){
			iImgGeneralOptions = piml->Add(CTempIconLoader(_T("PREFERENCESRED")));
			iImgTimingOptions = piml->Add(CTempIconLoader(_T("CLOCKBLUE")));
			iImgLeecher= piml->Add(CTempIconLoader(_T("PROTECTION_PANEL")));
			iImgPunishment = piml->Add(CTempIconLoader(_T("PUNISHMENT")));
			iImgPunishNonSuiClients = piml->Add(CTempIconLoader(_T("EMULATOR")));
			iImgTweakOfficialFeatures = piml->Add(CTempIconLoader(_T("TRAYCONNECTED")));
		}

		m_htiGeneralOptions = m_ctrlTreeOptions.InsertGroup(GetResString(_T("GENERAL_OPTIONS")), iImgGeneralOptions, TVI_ROOT);
		m_htiTimingOptions = m_ctrlTreeOptions.InsertGroup(GetResString(_T("TIMING_OPTIONS")), iImgTimingOptions, m_htiGeneralOptions);
		m_htiPunishmentCancelationScanPeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("PUNISHMENT_CANCELLATION_SCAN_PERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTimingOptions);
		m_ctrlTreeOptions.AddEditBox(m_htiPunishmentCancelationScanPeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiClientBanTime = m_ctrlTreeOptions.InsertItem(GetResString(_T("CLIENT_BAN_TIME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTimingOptions);
		m_ctrlTreeOptions.AddEditBox(m_htiClientBanTime, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiClientScoreReducingTime = m_ctrlTreeOptions.InsertItem(GetResString(_T("CLIENT_SCORE_REDUCING_TIME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTimingOptions);
		m_ctrlTreeOptions.Expand(m_htiTimingOptions, TVE_EXPAND);
		m_ctrlTreeOptions.AddEditBox(m_htiClientScoreReducingTime, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDontPunishFriends = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DONT_PUNISH_FRIENDS")), m_htiGeneralOptions, m_bDontPunishFriends);
		m_htiDontAllowFileHotSwapping = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DONT_ALLOW_FILE_HOT_SWAPPING")), m_htiGeneralOptions, m_bDontAllowFileHotSwapping);

		m_htiInformBadClients = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("INFORM_BAD_CLIENTS")), m_htiGeneralOptions, m_bInformBadClients); // => Inform Leechers - sFrQlXeRt
		m_htiInformBadClientsText = m_ctrlTreeOptions.InsertItem(GetResString(_T("INFORM_BAD_CLIENTS_TEXT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiInformBadClients);
		m_ctrlTreeOptions.AddEditBox(m_htiInformBadClientsText, RUNTIME_CLASS(CTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiInformBadClients, TVE_EXPAND); // ==> Inform Leechers Text - sFrQlXeRt

		m_htiAntiUploadProtection = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ANTI_UPLOAD_PROTECTION")), m_htiGeneralOptions, m_bAntiUploadProtection);
		m_htiAntiUploadProtectionLimit = m_ctrlTreeOptions.InsertItem(GetResString(_T("ANTI_UPLOAD_PROTECTION_LIMIT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAntiUploadProtection);
		m_ctrlTreeOptions.AddEditBox(m_htiAntiUploadProtectionLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiAntiUploadProtection, TVE_EXPAND); // => Anti Upload Protection - sFrQlXeRt

		m_htiUploaderPunishmentPrevention = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ENABLE_UPLOADER_PUNISMENT_PREVENTION")), m_htiGeneralOptions, m_bUploaderPunishmentPrevention);
		m_htiUploaderPunishmentPreventionLimit = m_ctrlTreeOptions.InsertItem(GetResString(_T("UPLOADER_PUNISMENT_PREVENTION_LIMIT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUploaderPunishmentPrevention);
		m_ctrlTreeOptions.Expand(m_htiUploaderPunishmentPrevention, TVE_EXPAND); // => Uploader Punishment Prevention [Stulle] - sFrQlXeRt
		m_ctrlTreeOptions.AddEditBox(m_htiUploaderPunishmentPreventionLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiAntiCase1 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOADER_PUNISMENT_PREVENTION_CASE_1")), m_htiUploaderPunishmentPreventionLimit, m_iUploaderPunishmentPreventionCase == 0);
		m_htiAntiCase2 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOADER_PUNISMENT_PREVENTION_CASE_2")), m_htiUploaderPunishmentPreventionLimit, m_iUploaderPunishmentPreventionCase == 1);
		m_htiAntiCase3 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOADER_PUNISMENT_PREVENTION_CASE_3")), m_htiUploaderPunishmentPreventionLimit, m_iUploaderPunishmentPreventionCase == 2);
		m_ctrlTreeOptions.Expand(m_htiUploaderPunishmentPreventionLimit, TVE_EXPAND); // => Uploader Punishment Prevention [Stulle] - sFrQlXeRt

		m_htiLeecherDetection = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SHIELD")), iImgLeecher, TVI_ROOT);
		m_htiDetectModNames = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CHECK_MODNAMES")), m_htiLeecherDetection, m_bDetectModNames);
		m_htiDetectUserNames = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CHECK_USERNAMES")), m_htiLeecherDetection, m_bDetectUserNames);
		m_htiDetectUserHashes = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CHECK_USERHASHES")), m_htiLeecherDetection, m_bDetectUserHashes);

		m_htiHardLeecherPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("HARD_LEECHER_PUNISHMENT")), iImgPunishment, m_htiLeecherDetection);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiHardLeecherPunishment, m_iHardLeecherPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiHardLeecherPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiSoftLeecherPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SOFT_LEECHER_PUNISHMENT")), iImgPunishment, m_htiLeecherDetection);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiSoftLeecherPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiBanBadKadNodes = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("BAN_BAD_KAD_NODES")), TVI_ROOT, m_bBanBadKadNodes);

		m_htiBanWrongPackage = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("BAN_WRONG_PACKAGE")), TVI_ROOT, m_bBanWrongPackage);
		LocalizeCommonItems();

		m_htiDetectAntiP2PBots = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_ANTI_P2P_BOTS")), TVI_ROOT, m_bDetectAntiP2PBots);
		m_htiAntiP2PBotsPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectAntiP2PBots);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiAntiP2PBotsPunishment, m_iAntiP2PBotsPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiAntiP2PBotsPunishment, m_iAntiP2PBotsPunishment == 1);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiAntiP2PBotsPunishment, m_iAntiP2PBotsPunishment == 2);
		m_ctrlTreeOptions.Expand(m_htiAntiP2PBotsPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectWrongTag = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_WRONG_TAG")), TVI_ROOT, m_bDetectWrongTag);
		m_htiWrongTagPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectWrongTag);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiWrongTagPunishment, m_iWrongTagPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiWrongTagPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectUnknownTag = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_UNKNOWN_TAG")), TVI_ROOT, m_bDetectUnknownTag);
		m_htiUnknownTagPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectUnknownTag);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiUnknownTagPunishment, m_iUnknownTagPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiUnknownTagPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectHashThief = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_HASH_THIEF")), TVI_ROOT, m_bDetectHashThief);
		m_htiHashThiefPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectHashThief);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiHashThiefPunishment, m_iHashThiefPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiHashThiefPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectModThief = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_MOD_THIEF")), TVI_ROOT, m_bDetectModThief);
		m_htiModThiefPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectModThief);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiModThiefPunishment, m_iModThiefPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiModThiefPunishment, m_iModThiefPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiModThiefPunishment, m_iModThiefPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiModThiefPunishment, m_iModThiefPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiModThiefPunishment, m_iModThiefPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiModThiefPunishment, m_iModThiefPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiModThiefPunishment, m_iModThiefPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiModThiefPunishment, m_iModThiefPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiModThiefPunishment, m_iModThiefPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiModThiefPunishment, m_iModThiefPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiModThiefPunishment, m_iModThiefPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiModThiefPunishment, m_iModThiefPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiModThiefPunishment, m_iModThiefPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiModThiefPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectUserNameThief = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_USERNAME_THIEF")), TVI_ROOT, m_bDetectUserNameThief);
		m_htiUserNameThiefPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectUserNameThief);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiUserNameThiefPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectEmptyUserNameEmule = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_EMPTY_USERNAME_EMULE")), TVI_ROOT, m_bDetectEmptyUserNameEmule);
		m_htiEmptyUserNameEmulePunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectEmptyUserNameEmule);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiEmptyUserNameEmulePunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectModChanger = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_MOD_CHANGER")), TVI_ROOT, m_bDetectModChanger);
		m_htiModChangerInterval = m_ctrlTreeOptions.InsertItem(GetResString(_T("MOD_CHANGER_INTERVALS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectModChanger);
		m_ctrlTreeOptions.AddEditBox(m_htiModChangerInterval, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiModChangerThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("MOD_CHANGER_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectModChanger);
		m_ctrlTreeOptions.AddEditBox(m_htiModChangerThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiModChangerPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectModChanger);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiModChangerPunishment, m_iModChangerPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiModChangerPunishment, m_iModChangerPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiModChangerPunishment, m_iModChangerPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiModChangerPunishment, m_iModChangerPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiModChangerPunishment, m_iModChangerPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiModChangerPunishment, m_iModChangerPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiModChangerPunishment, m_iModChangerPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiModChangerPunishment, m_iModChangerPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiModChangerPunishment, m_iModChangerPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiModChangerPunishment, m_iModChangerPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiModChangerPunishment, m_iModChangerPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiModChangerPunishment, m_iModChangerPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiModChangerPunishment, m_iModChangerPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiModChangerPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectUserNameChanger = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_USER_NAME_CHANGER")), TVI_ROOT, m_bDetectUserNameChanger);
		m_htiUserNameChangerInterval = m_ctrlTreeOptions.InsertItem(GetResString(_T("USER_NAME_CHANGER_INTERVALS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectUserNameChanger);
		m_ctrlTreeOptions.AddEditBox(m_htiUserNameChangerInterval, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUserNameChangerThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("USER_NAME_CHANGER_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectUserNameChanger);
		m_ctrlTreeOptions.AddEditBox(m_htiUserNameChangerThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUserNameChangerPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectUserNameChanger);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiUserNameChangerPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectTCPErrorFlooder = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_TCP_ERROR_FLOODER")), TVI_ROOT, m_bDetectTCPErrorFlooder);
		m_htiTCPErrorFlooderInterval = m_ctrlTreeOptions.InsertItem(GetResString(_T("TCP_ERROR_FLOODER_INTERVALS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectTCPErrorFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiTCPErrorFlooderInterval, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiTCPErrorFlooderThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("TCP_ERROR_FLOODER_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectTCPErrorFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiTCPErrorFlooderThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiTCPErrorFlooderPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectTCPErrorFlooder);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiTCPErrorFlooderPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectCommunity = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_COMMUNITY")), TVI_ROOT, m_bDetectCommunity);
		m_htiCommunityPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectCommunity);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiCommunityPunishment, m_iCommunityPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiCommunityPunishment, m_iCommunityPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiCommunityPunishment, m_iCommunityPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiCommunityPunishment, m_iCommunityPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiCommunityPunishment, m_iCommunityPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiCommunityPunishment, m_iCommunityPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiCommunityPunishment, m_iCommunityPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiCommunityPunishment, m_iCommunityPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiCommunityPunishment, m_iCommunityPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiCommunityPunishment, m_iCommunityPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiCommunityPunishment, m_iCommunityPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiCommunityPunishment, m_iCommunityPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiCommunityPunishment, m_iCommunityPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiCommunityPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectFakeEmule = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_FAKE_EMULE")), TVI_ROOT, m_bDetectFakeEmule);
		m_htiFakeEmulePunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectFakeEmule);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiFakeEmulePunishment, m_iFakeEmulePunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiFakeEmulePunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectHexModName = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_HEX_MOD_NAME")), TVI_ROOT, m_bDetectHexModName);
		m_htiHexModNamePunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectHexModName);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiHexModNamePunishment, m_iHexModNamePunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiHexModNamePunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectGhostMod = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_GHOST_MOD")), TVI_ROOT, m_bDetectGhostMod);
		m_htiGhostModPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectGhostMod);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiGhostModPunishment, m_iGhostModPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiGhostModPunishment, m_iGhostModPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiGhostModPunishment, m_iGhostModPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiGhostModPunishment, m_iGhostModPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiGhostModPunishment, m_iGhostModPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiGhostModPunishment, m_iGhostModPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiGhostModPunishment, m_iGhostModPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiGhostModPunishment, m_iGhostModPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiGhostModPunishment, m_iGhostModPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiGhostModPunishment, m_iGhostModPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiGhostModPunishment, m_iGhostModPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiGhostModPunishment, m_iGhostModPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiGhostModPunishment, m_iGhostModPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiGhostModPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectSpam = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_SPAM")), TVI_ROOT, m_bDetectSpam);
		m_htiSpamPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectSpam);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiSpamPunishment, m_iSpamPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiSpamPunishment, m_iSpamPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiSpamPunishment, m_iSpamPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiSpamPunishment, m_iSpamPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiSpamPunishment, m_iSpamPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiSpamPunishment, m_iSpamPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiSpamPunishment, m_iSpamPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiSpamPunishment, m_iSpamPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiSpamPunishment, m_iSpamPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiSpamPunishment, m_iSpamPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiSpamPunishment, m_iSpamPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiSpamPunishment, m_iSpamPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiSpamPunishment, m_iSpamPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiSpamPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectEmcrypt = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_EMCRYPT")), TVI_ROOT, m_bDetectEmcrypt);
		m_htiEmcryptPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectEmcrypt);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiEmcryptPunishment, m_iEmcryptPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiEmcryptPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectXSExploiter = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_XS_EXPLOITER")), TVI_ROOT, m_bDetectXSExploiter);
		m_htiXSExploiterPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectXSExploiter);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiXSExploiterPunishment, m_iXSExploiterPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiXSExploiterPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectFileFaker = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_FILEFAKER")), TVI_ROOT, m_bDetectFileFaker);
		m_htiFileFakerPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectFileFaker);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiFileFakerPunishment, m_iFileFakerPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiFileFakerPunishment, m_iEmcryptPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiFileFakerPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectUploadFaker = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_UPLOADFAKER")), TVI_ROOT, m_bDetectUploadFaker);
		m_htiUploadFakerPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectUploadFaker);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiUploadFakerPunishment, m_iUploadFakerPunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiUploadFakerPunishment, m_iEmcryptPunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiUploadFakerPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectUploadRequestAbuse = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_UPLOAD_REQUEST_ABUSE")), TVI_ROOT, m_bDetectUploadRequestAbuse);
		m_htiDetectUploadRequestAbuseNoRequestSlots = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPLOAD_REQUEST_ABUSE_NO_REQUEST_SLOTS")), m_htiDetectUploadRequestAbuse, m_bDetectUploadRequestAbuseNoRequestSlots);
		m_htiDetectUploadRequestAbuseQueueDrops = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPLOAD_REQUEST_ABUSE_QUEUE_DROPS")), m_htiDetectUploadRequestAbuse, m_bDetectUploadRequestAbuseQueueDrops);
		m_htiUploadRequestAbuseCounterGuard = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPLOAD_REQUEST_ABUSE_COUNTER_GUARD")), m_htiDetectUploadRequestAbuse, m_bUploadRequestAbuseCounterGuard);
		m_htiUploadRequestAbuseHashRotationTracking = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPLOAD_REQUEST_ABUSE_HASH_ROTATION")), m_htiDetectUploadRequestAbuse, m_bUploadRequestAbuseHashRotationTracking);
		m_htiUploadRequestAbusePostHelloDisconnect = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPLOAD_REQUEST_ABUSE_POST_HELLO_DISCONNECT")), m_htiDetectUploadRequestAbuse, m_bUploadRequestAbusePostHelloDisconnect);
		m_htiUploadRequestAbusePunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectUploadRequestAbuse);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiUploadRequestAbusePunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiDetectAgressive = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_AGRESSIVE")), TVI_ROOT, m_bDetectAgressive);
		m_htiAgressiveTime = m_ctrlTreeOptions.InsertItem(GetResString(_T("AGRESSIVE_TIME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectAgressive);
		m_ctrlTreeOptions.AddEditBox(m_htiAgressiveTime, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiAgressiveCounter = m_ctrlTreeOptions.InsertItem(GetResString(_T("AGRESSIVE_COUNTER")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectAgressive);
		m_ctrlTreeOptions.AddEditBox(m_htiAgressiveCounter, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiAgressiveLog = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("AGRESSIVE_LOG")), m_htiDetectAgressive, m_bAgressiveLog);
		m_htiAgressivePunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiDetectAgressive);
		m_htiIPUserHash = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IP_USER_HASH_BAN")), m_htiAgressivePunishment, m_iAgressivePunishment == 0);
		m_htiUserHashBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("USER_HASH_BAN")), m_htiAgressivePunishment, m_iAgressivePunishment == 1);
		m_htiUploadBan = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_BAN")), m_htiAgressivePunishment, m_iAgressivePunishment == 2);
		m_htiScore01 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_01")), m_htiAgressivePunishment, m_iAgressivePunishment == 3);
		m_htiScore02 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_02")), m_htiAgressivePunishment, m_iAgressivePunishment == 4);
		m_htiScore03 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_03")), m_htiAgressivePunishment, m_iAgressivePunishment == 5);
		m_htiScore04 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_04")), m_htiAgressivePunishment, m_iAgressivePunishment == 6);
		m_htiScore05 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_05")), m_htiAgressivePunishment, m_iAgressivePunishment == 7);
		m_htiScore06 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_06")), m_htiAgressivePunishment, m_iAgressivePunishment == 8);
		m_htiScore07 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_07")), m_htiAgressivePunishment, m_iAgressivePunishment == 9);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiAgressivePunishment, m_iAgressivePunishment == 10);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiAgressivePunishment, m_iAgressivePunishment == 11);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiAgressivePunishment, m_iAgressivePunishment == 12);
		m_ctrlTreeOptions.Expand(m_htiAgressivePunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiPunishNonSuiClients = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISH_NONSUI_CLIENTS")), iImgPunishNonSuiClients, TVI_ROOT);
		m_htiPunishNonSuiMlDonkey = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_MLDONKEY")), m_htiPunishNonSuiClients, m_bPunishNonSuiMlDonkey);
		m_htiPunishNonSuiEdonkey = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_EDONKEY")), m_htiPunishNonSuiClients, m_bPunishNonSuiEdonkey);
		m_htiPunishNonSuiEdonkeyHybrid = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_EDONKEY_HYBRID")), m_htiPunishNonSuiClients, m_bPunishNonSuiEdonkeyHybrid);
		m_htiPunishNonSuiShareaza = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_SHAREAZA")), m_htiPunishNonSuiClients, m_bPunishNonSuiShareaza);
		m_htiPunishNonSuiLphant = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_LPHANT")), m_htiPunishNonSuiClients, m_bPunishNonSuiLphant);
		m_htiPunishNonSuiAmule = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_AMULE")), m_htiPunishNonSuiClients, m_bPunishNonSuiAmule);
		m_htiPunishNonSuiEmule = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PUNISH_NONSUI_EMULE")), m_htiPunishNonSuiClients, m_bPunishNonSuiEmule);
		m_htiNonSuiPunishment = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PUNISHMENT")), iImgPunishment, m_htiPunishNonSuiClients);
		m_htiScore08 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_08")), m_htiNonSuiPunishment, m_iNonSuiPunishment == 0);
		m_htiScore09 = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SCORE_09")), m_htiNonSuiPunishment, m_iNonSuiPunishment == 1);
		m_htiNoPunishment = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NO_PUNISHMENT")), m_htiNonSuiPunishment, m_iNonSuiPunishment == 2);
		m_ctrlTreeOptions.Expand(m_htiNonSuiPunishment, TVE_EXPAND);
		LocalizeCommonItems();

		m_htiTweakOfficialFeatures = m_ctrlTreeOptions.InsertGroup(GetResString(_T("TWEAK_OFFICIAL_FEATURES")), iImgTweakOfficialFeatures, TVI_ROOT);
		m_htiDetectCorruptedDataSender = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_CORRUPTED_DATA_SENDER")), m_htiTweakOfficialFeatures, m_bDetectCorruptedDataSender);
		m_htiDetectHashChanger = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_HASH_CHANGER")), m_htiTweakOfficialFeatures, m_bDetectHashChanger);
		m_htiDetectFileScanner = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_FILE_SCANNER")), m_htiTweakOfficialFeatures, m_bDetectFileScanner);
		m_htiDetectRankFlooder = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_RANK_FLOODER")), m_htiTweakOfficialFeatures, m_bDetectRankFlooder);
		m_htiDetectKadRequestFlooder = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DETECT_KAD_REQ_FLOODER")), m_htiTweakOfficialFeatures, m_bDetectKadRequestFlooder);
		m_htiKadRequestFloodBanTreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("KAD_REQ_FLOOD_TRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectKadRequestFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiKadRequestFloodBanTreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiDetectKadRequestFlooder, TVE_EXPAND);

		m_ctrlTreeOptions.SendMessage(WM_VSCROLL, SB_TOP);
		m_bInitializedTreeOpts = true;
	}

	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishmentCancelationScanPeriod, m_iPunishmentCancelationScanPeriod);
	DDV_MinMaxInt(pDX, m_iPunishmentCancelationScanPeriod, 1, 20);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiClientBanTime, m_iClientBanTime);
	DDV_MinMaxInt(pDX, m_iClientBanTime, 1, 720); //24 hours * 30 days
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiClientScoreReducingTime, m_iClientScoreReducingTime);
	DDV_MinMaxInt(pDX, m_iClientScoreReducingTime, 1, 720); //24 hours * 30 days
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiInformBadClients, m_bInformBadClients); // => Inform Leechers - sFrQlXeRt
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDontPunishFriends, m_bDontPunishFriends);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDontAllowFileHotSwapping, m_bDontAllowFileHotSwapping);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiInformBadClientsText, m_strInformBadClientsText);
	DDV_MaxChars(pDX, m_strInformBadClientsText, 30);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAntiUploadProtection, m_bAntiUploadProtection);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAntiUploadProtectionLimit, m_iAntiUploadProtectionLimit);
	DDV_MinMaxInt(pDX, m_iAntiUploadProtectionLimit, 1000, 2800);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploaderPunishmentPrevention, m_bUploaderPunishmentPrevention);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploaderPunishmentPreventionLimit, m_iUploaderPunishmentPreventionLimit);
	DDV_MinMaxInt(pDX, m_iUploaderPunishmentPreventionLimit, 1, 20000);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploaderPunishmentPreventionLimit, (int &)m_iUploaderPunishmentPreventionCase);

	if (m_htiDetectModNames) DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectModNames, m_bDetectModNames);
	if (m_htiDetectUserNames) DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUserNames, m_bDetectUserNames);
	if (m_htiDetectUserHashes) DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUserHashes, m_bDetectUserHashes);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiHardLeecherPunishment, m_iHardLeecherPunishment);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiSoftLeecherPunishment, m_iSoftLeecherPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiBanBadKadNodes, m_bBanBadKadNodes);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiBanWrongPackage, m_bBanWrongPackage);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectAntiP2PBots, m_bDetectAntiP2PBots);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAntiP2PBotsPunishment, m_iAntiP2PBotsPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectWrongTag, m_bDetectWrongTag);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiWrongTagPunishment, m_iWrongTagPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUnknownTag, m_bDetectUnknownTag);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUnknownTagPunishment, m_iUnknownTagPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectHashThief, m_bDetectHashThief);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiHashThiefPunishment, m_iHashThiefPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectModThief, m_bDetectModThief);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiModThiefPunishment, m_iModThiefPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUserNameThief, m_bDetectUserNameThief);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUserNameThiefPunishment, m_iUserNameThiefPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectModChanger, m_bDetectModChanger);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiModChangerInterval, m_iModChangerInterval);
	DDV_MinMaxInt(pDX, m_iModChangerInterval, 30, 1440);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiModChangerThreshold, m_iModChangerThreshold);
	DDV_MinMaxInt(pDX, m_iModChangerThreshold, 1, 24);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiModChangerPunishment, m_iModChangerPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUserNameChanger, m_bDetectUserNameChanger);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUserNameChangerInterval, m_iUserNameChangerInterval);
	DDV_MinMaxInt(pDX, m_iUserNameChangerInterval, 30, 1440);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUserNameChangerThreshold, m_iUserNameChangerThreshold);
	DDV_MinMaxInt(pDX, m_iUserNameChangerThreshold, 1, 24);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUserNameChangerPunishment, m_iUserNameChangerPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectTCPErrorFlooder, m_bDetectTCPErrorFlooder);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiTCPErrorFlooderInterval, m_iTCPErrorFlooderInterval);
	DDV_MinMaxInt(pDX, m_iTCPErrorFlooderInterval, 30, 1440);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiTCPErrorFlooderThreshold, m_iTCPErrorFlooderThreshold);
	DDV_MinMaxInt(pDX, m_iTCPErrorFlooderThreshold, 7, INT_MAX);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiTCPErrorFlooderPunishment, m_iTCPErrorFlooderPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectEmptyUserNameEmule, m_bDetectEmptyUserNameEmule);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiEmptyUserNameEmulePunishment, m_iEmptyUserNameEmulePunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectCommunity, m_bDetectCommunity);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiCommunityPunishment, m_iCommunityPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectFakeEmule, m_bDetectFakeEmule);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiFakeEmulePunishment, m_iFakeEmulePunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectHexModName, m_bDetectHexModName);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiHexModNamePunishment, m_iHexModNamePunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectGhostMod, m_bDetectGhostMod);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiGhostModPunishment, m_iGhostModPunishment);
	if (m_htiDetectSpam) DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectSpam, m_bDetectSpam);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiSpamPunishment, m_iSpamPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectEmcrypt, m_bDetectEmcrypt);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiEmcryptPunishment, m_iEmcryptPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectXSExploiter, m_bDetectXSExploiter);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiXSExploiterPunishment, m_iXSExploiterPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectFileFaker, m_bDetectFileFaker);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiFileFakerPunishment, m_iFileFakerPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUploadFaker, m_bDetectUploadFaker);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploadFakerPunishment, m_iUploadFakerPunishment);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUploadRequestAbuse, m_bDetectUploadRequestAbuse);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUploadRequestAbuseNoRequestSlots, m_bDetectUploadRequestAbuseNoRequestSlots);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectUploadRequestAbuseQueueDrops, m_bDetectUploadRequestAbuseQueueDrops);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploadRequestAbuseCounterGuard, m_bUploadRequestAbuseCounterGuard);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploadRequestAbuseHashRotationTracking, m_bUploadRequestAbuseHashRotationTracking);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploadRequestAbusePostHelloDisconnect, m_bUploadRequestAbusePostHelloDisconnect);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiUploadRequestAbusePunishment, m_iUploadRequestAbusePunishment);

	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectAgressive, m_bDetectAgressive);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAgressiveTime, m_iAgressiveTime);
	DDV_MinMaxInt(pDX, m_iAgressiveTime, 5, 15);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAgressiveCounter, m_iAgressiveCounter);
	DDV_MinMaxInt(pDX, m_iAgressiveCounter, 3, 10);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAgressiveLog, m_bAgressiveLog);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiAgressivePunishment, m_iAgressivePunishment);

	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiMlDonkey, m_bPunishNonSuiMlDonkey);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiEdonkey, m_bPunishNonSuiEdonkey);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiEdonkeyHybrid, m_bPunishNonSuiEdonkeyHybrid);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiShareaza, m_bPunishNonSuiShareaza);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiLphant, m_bPunishNonSuiLphant);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiAmule, m_bPunishNonSuiAmule);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiPunishNonSuiEmule, m_bPunishNonSuiEmule);
	DDX_TreeRadio(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiNonSuiPunishment, m_iNonSuiPunishment);

	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectCorruptedDataSender, m_bDetectCorruptedDataSender);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectHashChanger, m_bDetectHashChanger);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectFileScanner, m_bDetectFileScanner);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectRankFlooder, m_bDetectRankFlooder);
	DDX_TreeCheck(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiDetectKadRequestFlooder, m_bDetectKadRequestFlooder);
	DDX_TreeEdit(pDX, IDC_PROTECTION_PANEL_OPTS, m_htiKadRequestFloodBanTreshold, m_iKadRequestFloodBanTreshold);
	DDV_MinMaxInt(pDX, m_iKadRequestFloodBanTreshold, 1, 4);}

BOOL CPPgProtectionPanel::OnInitDialog()
{
	m_iPunishmentCancelationScanPeriod = thePrefs.GetPunishmentCancelationScanPeriod() / 60000; //Miliseconds to minutes
	m_iClientBanTime = thePrefs.GetClientBanTime() / 3600; //Seconds to hours // adjust ClientBanTime - Stulle
	m_iClientScoreReducingTime = thePrefs.GetClientScoreReducingTime() / 3600; //Seconds to hours
	m_bDontPunishFriends = thePrefs.IsDontPunishFriends();
	m_bDontAllowFileHotSwapping = thePrefs.IsDontAllowFileHotSwapping();
	m_bInformBadClients = thePrefs.IsInformBadClients(); // => Inform Leechers - sFrQlXeRt
	m_strInformBadClientsText = thePrefs.GetInformBadClientsText(); // => Inform Leechers Text - evcz
	m_bAntiUploadProtection = thePrefs.IsAntiUploadProtection();
	m_iAntiUploadProtectionLimit = thePrefs.GetAntiUploadProtectionLimit();
	m_bUploaderPunishmentPrevention = thePrefs.GetUploaderPunishmentPrevention();
	m_iUploaderPunishmentPreventionLimit = thePrefs.GetUploaderPunishmentPreventionLimit();
	m_iUploaderPunishmentPreventionCase = thePrefs.GetUploaderPunishmentPreventionCase();

	m_bDetectModNames = thePrefs.IsDetectModNames();
	m_bDetectUserNames = thePrefs.IsDetectUserNames();
	m_bDetectUserHashes = thePrefs.IsDetectUserHashes();
	m_iHardLeecherPunishment = thePrefs.GetHardLeecherPunishment();
	m_iSoftLeecherPunishment = thePrefs.GetSoftLeecherPunishment();

	m_bBanBadKadNodes = thePrefs.IsBanBadKadNodes();

	m_bBanWrongPackage = thePrefs.IsBanWrongPackage();

	m_bDetectAntiP2PBots = thePrefs.IsDetectAntiP2PBots();
	m_iAntiP2PBotsPunishment = thePrefs.GetAntiP2PBotsPunishment();

	m_bDetectWrongTag = thePrefs.IsDetectWrongTag();
	m_iWrongTagPunishment= thePrefs.GetWrongTagPunishment();

	m_bDetectUnknownTag = thePrefs.IsDetectUnknownTag();
	m_iUnknownTagPunishment = thePrefs.GetUnknownTagPunishment();

	m_bDetectHashThief = thePrefs.IsDetectHashThief();
	m_iHashThiefPunishment = thePrefs.GetHashThiefPunishment();

	m_bDetectModThief = thePrefs.IsDetectModThief();
	m_iModThiefPunishment = thePrefs.GetModThiefPunishment();

	m_bDetectUserNameThief = thePrefs.IsDetectUserNameThief();
	m_iUserNameThiefPunishment = thePrefs.GetUserNameThiefPunishment();

	m_bDetectModChanger = thePrefs.IsDetectModChanger();
	m_iModChangerPunishment = thePrefs.GetModChangerPunishment();

	m_iModChangerInterval = thePrefs.GetModChangerInterval() / 60000; //Miliseconds to minutes
	m_iModChangerThreshold = thePrefs.GetModChangerThreshold();

	m_bDetectUserNameChanger = thePrefs.IsDetectUserNameChanger();
	m_iUserNameChangerPunishment = thePrefs.GetUserNameChangerPunishment();

	m_iUserNameChangerInterval = thePrefs.GetUserNameChangerInterval() / 60000; //Miliseconds to minutes
	m_iUserNameChangerThreshold = thePrefs.GetUserNameChangerThreshold();

	m_bDetectTCPErrorFlooder = thePrefs.IsDetectTCPErrorFlooder();
	m_iTCPErrorFlooderPunishment = thePrefs.GetTCPErrorFlooderPunishment();

	m_iTCPErrorFlooderInterval = thePrefs.GetTCPErrorFlooderInterval() / 60000; //Miliseconds to minutes
	m_iTCPErrorFlooderThreshold = thePrefs.GetTCPErrorFlooderThreshold();

	m_bDetectEmptyUserNameEmule = thePrefs.IsDetectEmptyUserNameEmule();
	m_iEmptyUserNameEmulePunishment = thePrefs.GetEmptyUserNameEmulePunishment();

	m_bDetectCommunity = thePrefs.IsDetectCommunity();
	m_iCommunityPunishment = thePrefs.GetCommunityPunishment();

	m_bDetectFakeEmule = thePrefs.IsDetectFakeEmule();
	m_iFakeEmulePunishment = thePrefs.GetFakeEmulePunishment();

	m_bDetectHexModName = thePrefs.IsDetectHexModName();
	m_iHexModNamePunishment = thePrefs.GetHexModNamePunishment();

	m_bDetectGhostMod = thePrefs.IsDetectGhostMod();
	m_iGhostModPunishment = thePrefs.GetGhostModPunishment();

	m_bDetectSpam = thePrefs.IsDetectSpam();
	m_iSpamPunishment = thePrefs.GetSpamPunishment();

	m_bDetectEmcrypt = thePrefs.IsDetectEmcrypt();
	m_iEmcryptPunishment = thePrefs.GetEmcryptPunishment();

	m_bDetectXSExploiter = thePrefs.IsDetectXSExploiter();
	m_iXSExploiterPunishment = thePrefs.GetXSExploiterPunishment();

	m_bDetectFileFaker = thePrefs.IsDetectFileFaker();
	m_iFileFakerPunishment = thePrefs.GetFileFakerPunishment();

	m_bDetectUploadFaker = thePrefs.IsDetectUploadFaker();
	m_iUploadFakerPunishment = thePrefs.GetUploadFakerPunishment();
	m_bDetectUploadRequestAbuse = thePrefs.IsDetectUploadRequestAbuse();
	m_bDetectUploadRequestAbuseNoRequestSlots = thePrefs.IsDetectUploadRequestAbuseNoRequestSlots();
	m_bDetectUploadRequestAbuseQueueDrops = thePrefs.IsDetectUploadRequestAbuseQueueDrops();
	m_bUploadRequestAbuseCounterGuard = thePrefs.IsUploadRequestAbuseCounterGuard();
	m_bUploadRequestAbuseHashRotationTracking = thePrefs.IsUploadRequestAbuseHashRotationTracking();
	m_bUploadRequestAbusePostHelloDisconnect = thePrefs.IsUploadRequestAbusePostHelloDisconnect();
	m_iUploadRequestAbusePunishment = thePrefs.GetUploadRequestAbusePunishment();

	m_bDetectAgressive = thePrefs.IsDetectAgressive();
	m_iAgressiveTime = (int)(thePrefs.GetAgressiveTime());
	m_iAgressiveCounter = (int)(thePrefs.GetAgressiveCounter());
	m_bAgressiveLog = thePrefs.IsAgressiveLog();
	m_iAgressivePunishment = thePrefs.GetAgressivePunishment();

	m_bPunishNonSuiMlDonkey = thePrefs.IsPunishNonSuiMlDonkey();
	m_bPunishNonSuiEdonkey = thePrefs.IsPunishNonSuiEdonkey();
	m_bPunishNonSuiEdonkeyHybrid = thePrefs.IsPunishNonSuiEdonkeyHybrid();
	m_bPunishNonSuiShareaza = thePrefs.IsPunishNonSuiShareaza();
	m_bPunishNonSuiLphant = thePrefs.IsPunishNonSuiLphant();
	m_bPunishNonSuiAmule = thePrefs.IsPunishNonSuiAmule();
	m_bPunishNonSuiEmule = thePrefs.IsPunishNonSuiEmule();
	m_iNonSuiPunishment = (int)(thePrefs.GetNonSuiPunishment());

	m_bDetectCorruptedDataSender = thePrefs.IsDetectCorruptedDataSender();
	m_bDetectHashChanger = thePrefs.IsDetectHashChanger();
	m_bDetectFileScanner = thePrefs.IsDetectFileScanner();
	m_bDetectRankFlooder = thePrefs.IsDetectRankFlooder();
	m_bDetectKadRequestFlooder = thePrefs.IsDetectKadRequestFlooder();
	m_iKadRequestFloodBanTreshold = thePrefs.GetKadRequestFloodBanTreshold();

	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	Localize();
	UpdateLayout();

	return TRUE;  // return TRUE unless you set the focus to the control. EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgProtectionPanel::UpdateLayout()
{
	CWnd* pTree = GetDlgItem(IDC_PROTECTION_PANEL_OPTS);
	CWnd* pInfo = GetDlgItem(IDC_PROTECTION_PANEL_OPTS_INFO);
	if (pTree == NULL || pInfo == NULL || !::IsWindow(pTree->GetSafeHwnd()) || !::IsWindow(pInfo->GetSafeHwnd()))
		return;

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcTree;
	pTree->GetWindowRect(&rcTree);
	ScreenToClient(&rcTree);

	CRect rcInfo;
	pInfo->GetWindowRect(&rcInfo);
	ScreenToClient(&rcInfo);

	const int iBottomMargin = max(1, rcInfo.left);
	const int iInfoTop = max(rcTree.top + 1, rcClient.bottom - iBottomMargin - rcInfo.Height());
	rcInfo.OffsetRect(0, iInfoTop - rcInfo.top);
	pInfo->MoveWindow(&rcInfo);

	rcTree.bottom = rcInfo.top;
	if (rcTree.bottom > rcTree.top)
		pTree->MoveWindow(&rcTree);
}

void CPPgProtectionPanel::OnSize(UINT nType, int cx, int cy)
{
	CPropertyPage::OnSize(nType, cx, cy);
	UpdateLayout();
}

BOOL CPPgProtectionPanel::OnKillActive()
{
	// if prop page is closed by pressing VK_ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	return CPropertyPage::OnKillActive();
}

BOOL CPPgProtectionPanel::OnApply()
{
	// if prop page is closed by pressing VK_ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();

	if (!UpdateData())
		return FALSE;

	if (!thePrefs.m_bDontPunishFriends && m_bDontPunishFriends)
		theApp.clientlist->CancelFriendPunishments();

	if (thePrefs.m_bDetectModNames && !m_bDetectModNames)
		theApp.clientlist->CancelCategoryPunishments(PR_BADMODSOFT);

	if (thePrefs.m_bDetectUserNames && !m_bDetectUserNames)
		theApp.clientlist->CancelCategoryPunishments(PR_BADUSERSOFT);

	if (thePrefs.m_bDetectUserHashes && !m_bDetectUserHashes)
		theApp.clientlist->CancelCategoryPunishments(PR_BADHASHSOFT);

	if (thePrefs.m_bDetectModNames && !m_bDetectModNames && thePrefs.m_bDetectUserNames && !m_bDetectUserNames && thePrefs.m_bDetectUserHashes && !m_bDetectUserHashes)
		theApp.clientlist->CancelCategoryPunishments(PR_BADMODUSERHASHHARD);

	if (thePrefs.m_bBanWrongPackage && !m_bBanWrongPackage)
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGPACKAGE);

	if (thePrefs.m_bDetectAntiP2PBots && !m_bDetectAntiP2PBots)
		theApp.clientlist->CancelCategoryPunishments(PR_ANTIP2PBOT);

	if (thePrefs.m_bDetectWrongTag && !m_bDetectWrongTag) {
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGORDER);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGFORMAT);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGINFOSIZE);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGINFOFORMAT);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGMD4STRING);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGUPLOADREQ);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGAICH);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGHELLOSIZE);
		theApp.clientlist->CancelCategoryPunishments(PR_WRONGTAGHASH);
	}

	if (thePrefs.m_bDetectUnknownTag && !m_bDetectUnknownTag)
		theApp.clientlist->CancelCategoryPunishments(PR_UNKOWNTAG);

	if (thePrefs.m_bDetectHashThief && !m_bDetectHashThief)
		theApp.clientlist->CancelCategoryPunishments(PR_HASHTHIEF);

	if (thePrefs.m_bDetectModThief && !m_bDetectModThief)
		theApp.clientlist->CancelCategoryPunishments(PR_MODTHIEF);

	if (thePrefs.m_bDetectUserNameThief && !m_bDetectUserNameThief)
		theApp.clientlist->CancelCategoryPunishments(PR_USERNAMETHIEF);

	if (thePrefs.m_bDetectModChanger && !m_bDetectModChanger)
		theApp.clientlist->CancelCategoryPunishments(PR_MODCHANGER);

	if (thePrefs.m_bDetectUserNameChanger && !m_bDetectUserNameChanger)
		theApp.clientlist->CancelCategoryPunishments(PR_USERNAMECHANGER);

	if (thePrefs.m_bDetectTCPErrorFlooder && !m_bDetectTCPErrorFlooder)
		theApp.clientlist->CancelCategoryPunishments(PR_TCPERRORFLOODER);

	if (thePrefs.m_bDetectEmptyUserNameEmule && !m_bDetectEmptyUserNameEmule)
		theApp.clientlist->CancelCategoryPunishments(PR_EMPTYUSERNAME);

	if (thePrefs.m_bDetectCommunity && !m_bDetectCommunity)
		theApp.clientlist->CancelCategoryPunishments(PR_BADCOMMUNITY);

	if (thePrefs.m_bDetectFakeEmule && !m_bDetectFakeEmule) {
		theApp.clientlist->CancelCategoryPunishments(PR_FAKEMULEVERSION);
		theApp.clientlist->CancelCategoryPunishments(PR_FAKEMULEVERSIONVAGAA);
	}

	if (thePrefs.m_bDetectHexModName && !m_bDetectHexModName)
		theApp.clientlist->CancelCategoryPunishments(PR_HEXMODNAME);

	if (thePrefs.m_bDetectGhostMod && !m_bDetectGhostMod)
		theApp.clientlist->CancelCategoryPunishments(PR_GHOSTMOD);

	if (thePrefs.m_bDetectSpam && !m_bDetectSpam)
		theApp.clientlist->CancelCategoryPunishments(PR_SPAMMER);

	if (thePrefs.m_bDetectEmcrypt && !m_bDetectEmcrypt)
		theApp.clientlist->CancelCategoryPunishments(PR_EMCRYPT);

	if (thePrefs.m_bDetectXSExploiter && !m_bDetectXSExploiter)
		theApp.clientlist->CancelCategoryPunishments(PR_XSEXPLOITER);

	if (thePrefs.m_bDetectFileFaker && !m_bDetectFileFaker)
		theApp.clientlist->CancelCategoryPunishments(PR_FILEFAKER);

	if (thePrefs.m_bDetectUploadFaker && !m_bDetectUploadFaker)
		theApp.clientlist->CancelCategoryPunishments(PR_UPLOADFAKER);

	if (thePrefs.m_bDetectUploadRequestAbuse && !m_bDetectUploadRequestAbuse)
		theApp.clientlist->CancelCategoryPunishments(PR_UPLOADREQUESTABUSE);

	if (thePrefs.m_bDetectAgressive && !m_bDetectAgressive)
		theApp.clientlist->CancelCategoryPunishments(PR_AGGRESSIVE);

	if (thePrefs.m_bPunishNonSuiMlDonkey && !m_bPunishNonSuiMlDonkey)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUIMLDONKEY);

	if (thePrefs.m_bPunishNonSuiEdonkey && !m_bPunishNonSuiEdonkey)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUIEDONKEY);

	if (thePrefs.m_bPunishNonSuiEdonkeyHybrid && !m_bPunishNonSuiEdonkeyHybrid)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUIEDONKEYHYBRID);

	if (thePrefs.m_bPunishNonSuiShareaza && !m_bPunishNonSuiShareaza)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUISHAREAZA);

	if (thePrefs.m_bPunishNonSuiLphant && !m_bPunishNonSuiLphant)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUILPHANT);

	if (thePrefs.m_bPunishNonSuiAmule && !m_bPunishNonSuiAmule)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUIAMULE);

	if (thePrefs.m_bPunishNonSuiEmule && !m_bPunishNonSuiEmule)
		theApp.clientlist->CancelCategoryPunishments(PR_NONSUIEMULE);

	thePrefs.m_iPunishmentCancelationScanPeriod = m_iPunishmentCancelationScanPeriod * 60000; //Minutes to miliseconds 
	thePrefs.m_tClientBanTime = m_iClientBanTime * 3600; //Hours to seconds  // adjust ClientBanTime - Stulle
	thePrefs.m_tClientScoreReducingTime = m_iClientScoreReducingTime * 3600; //Hours to seconds
	thePrefs.m_bDontPunishFriends = m_bDontPunishFriends;
	thePrefs.m_bDontAllowFileHotSwapping = m_bDontAllowFileHotSwapping;
	thePrefs.m_bInformBadClients = m_bInformBadClients; // => Inform Leechers - sFrQlXeRt
	thePrefs.m_strInformBadClientsText = m_strInformBadClientsText; // => Inform Leechers Text - evcz
	thePrefs.m_bAntiUploadProtection = m_bAntiUploadProtection;
	thePrefs.m_iAntiUploadProtectionLimit = (uint16)m_iAntiUploadProtectionLimit;
	thePrefs.m_bUploaderPunishmentPrevention = m_bUploaderPunishmentPrevention;
	thePrefs.m_iUploaderPunishmentPreventionLimit = (uint16)m_iUploaderPunishmentPreventionLimit;
	thePrefs.m_iUploaderPunishmentPreventionCase = (uint8)m_iUploaderPunishmentPreventionCase;

	thePrefs.m_bDetectModNames = m_bDetectModNames;
	thePrefs.m_bDetectUserNames = m_bDetectUserNames;
	thePrefs.m_bDetectUserHashes = m_bDetectUserHashes;
	thePrefs.m_uHardLeecherPunishment = (uint8)m_iHardLeecherPunishment;
	thePrefs.m_uSoftLeecherPunishment = (uint8)m_iSoftLeecherPunishment;

	thePrefs.m_bBanBadKadNodes = m_bBanBadKadNodes;

	thePrefs.m_bBanWrongPackage = m_bBanWrongPackage;

	thePrefs.m_bDetectAntiP2PBots = m_bDetectAntiP2PBots;
	thePrefs.m_uAntiP2PBotsPunishment = (uint8)m_iAntiP2PBotsPunishment;

	thePrefs.m_bDetectWrongTag = m_bDetectWrongTag;
	thePrefs.m_uWrongTagPunishment = (uint8)m_iWrongTagPunishment;

	thePrefs.m_bDetectUnknownTag = m_bDetectUnknownTag;
	thePrefs.m_uUnknownTagPunishment = (uint8)m_iUnknownTagPunishment;
	
	thePrefs.m_bDetectHashThief = m_bDetectHashThief;
	thePrefs.m_uHashThiefPunishment = (uint8)m_iHashThiefPunishment;
	
	thePrefs.m_bDetectModThief = m_bDetectModThief;
	thePrefs.m_uModThiefPunishment = (uint8)m_iModThiefPunishment;

	thePrefs.m_bDetectUserNameThief = m_bDetectUserNameThief;
	thePrefs.m_uUserNameThiefPunishment = (uint8)m_iUserNameThiefPunishment;

	thePrefs.m_bDetectModChanger = m_bDetectModChanger;
	thePrefs.m_uModChangerPunishment = (uint8)m_iModChangerPunishment;

	thePrefs.m_iModChangerInterval = m_iModChangerInterval * 60000; //Minutes to miliseconds 
	thePrefs.m_iModChangerThreshold = m_iModChangerThreshold;

	thePrefs.m_bDetectUserNameChanger = m_bDetectUserNameChanger;
	thePrefs.m_uUserNameChangerPunishment = (uint8)m_iUserNameChangerPunishment;

	thePrefs.m_iUserNameChangerInterval = m_iUserNameChangerInterval * 60000; //Minutes to miliseconds 
	thePrefs.m_iUserNameChangerThreshold = m_iUserNameChangerThreshold;

	thePrefs.m_bDetectTCPErrorFlooder = m_bDetectTCPErrorFlooder;
	thePrefs.m_uTCPErrorFlooderPunishment = (uint8)m_iTCPErrorFlooderPunishment;

	thePrefs.m_iTCPErrorFlooderInterval = m_iTCPErrorFlooderInterval * 60000; //Minutes to miliseconds 
	thePrefs.m_iTCPErrorFlooderThreshold = m_iTCPErrorFlooderThreshold;

	thePrefs.m_bDetectEmptyUserNameEmule = m_bDetectEmptyUserNameEmule;
	thePrefs.m_uEmptyUserNameEmulePunishment = (uint8)m_iEmptyUserNameEmulePunishment;

	thePrefs.m_bDetectCommunity = m_bDetectCommunity;
	thePrefs.m_uCommunityPunishment = (uint8)m_iCommunityPunishment;

	thePrefs.m_bDetectFakeEmule = m_bDetectFakeEmule;
	thePrefs.m_uFakeEmulePunishment = (uint8)m_iFakeEmulePunishment;

	thePrefs.m_bDetectHexModName = m_bDetectHexModName;
	thePrefs.m_uHexModNamePunishment = (uint8)m_iHexModNamePunishment;

	thePrefs.m_bDetectGhostMod = m_bDetectGhostMod;
	thePrefs.m_uGhostModPunishment = (uint8)m_iGhostModPunishment;
	
	thePrefs.m_bDetectSpam = m_bDetectSpam;
	thePrefs.m_uSpamPunishment = (uint8)m_iSpamPunishment;

	thePrefs.m_bDetectEmcrypt = m_bDetectEmcrypt;
	thePrefs.m_uEmcryptPunishment = (uint8)m_iEmcryptPunishment;

	thePrefs.m_bDetectXSExploiter = m_bDetectXSExploiter;
	thePrefs.m_uXSExploiterPunishment = m_iXSExploiterPunishment;

	thePrefs.m_bDetectFileFaker = m_bDetectFileFaker;
	thePrefs.m_uFileFakerPunishment = m_iFileFakerPunishment;

	thePrefs.m_bDetectUploadFaker = m_bDetectUploadFaker;
	thePrefs.m_uUploadFakerPunishment = m_iUploadFakerPunishment;
	thePrefs.m_bDetectUploadRequestAbuse = m_bDetectUploadRequestAbuse;
	thePrefs.m_bDetectUploadRequestAbuseNoRequestSlots = m_bDetectUploadRequestAbuseNoRequestSlots;
	thePrefs.m_bDetectUploadRequestAbuseQueueDrops = m_bDetectUploadRequestAbuseQueueDrops;
	thePrefs.m_bUploadRequestAbuseCounterGuard = m_bUploadRequestAbuseCounterGuard;
	thePrefs.m_bUploadRequestAbuseHashRotationTracking = m_bUploadRequestAbuseHashRotationTracking;
	thePrefs.m_bUploadRequestAbusePostHelloDisconnect = m_bUploadRequestAbusePostHelloDisconnect;
	thePrefs.m_uUploadRequestAbusePunishment = m_iUploadRequestAbusePunishment;

	thePrefs.m_bDetectAgressive = m_bDetectAgressive;
	thePrefs.m_iAgressiveTime = (uint16)m_iAgressiveTime;
	thePrefs.m_iAgressiveCounter = (uint16)m_iAgressiveCounter;
	thePrefs.m_bAgressiveLog = m_bAgressiveLog;
	thePrefs.m_uAgressivePunishment = (uint8)m_iAgressivePunishment;

	thePrefs.m_bPunishNonSuiMlDonkey = m_bPunishNonSuiMlDonkey;
	thePrefs.m_bPunishNonSuiEdonkey = m_bPunishNonSuiEdonkey;
	thePrefs.m_bPunishNonSuiEdonkeyHybrid = m_bPunishNonSuiEdonkeyHybrid;
	thePrefs.m_bPunishNonSuiShareaza = m_bPunishNonSuiShareaza;
	thePrefs.m_bPunishNonSuiLphant = m_bPunishNonSuiLphant;
	thePrefs.m_bPunishNonSuiAmule = m_bPunishNonSuiAmule;
	thePrefs.m_bPunishNonSuiEmule = m_bPunishNonSuiEmule;
	thePrefs.m_uNonSuiPunishment = (uint8)m_iNonSuiPunishment;

	thePrefs.m_bDetectCorruptedDataSender = m_bDetectCorruptedDataSender;
	thePrefs.m_bDetectHashChanger = m_bDetectHashChanger;
	thePrefs.m_bDetectFileScanner = m_bDetectFileScanner;
	thePrefs.m_bDetectRankFlooder = m_bDetectRankFlooder;
	thePrefs.m_bDetectKadRequestFlooder = m_bDetectKadRequestFlooder;
	thePrefs.m_iKadRequestFloodBanTreshold = m_iKadRequestFloodBanTreshold;

	theApp.shield->FillCategoryPunishmentMap();

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgProtectionPanel::LocalizeItemText(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetItemText(item, GetResString(strid));
}

void CPPgProtectionPanel::LocalizeItemInfoText(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetItemInfo(item, GetResString(strid));
}

void CPPgProtectionPanel::LocalizeEditLabel(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetEditLabel(item, GetResString(strid));
}

void CPPgProtectionPanel::Localize(void)
{
	if(m_hWnd)
	{
		GetDlgItem(IDC_SHIELD_RELOAD)->SetWindowText(_T("Reload"));
		if (theApp.shield->m_bShieldFileLoaded) {
			CString buffer;
			buffer.Format(GetResString(_T("SHIELD_LOADED")), theApp.shield->GetLoadedDefinitionCount());
			GetDlgItem(IDC_SHIELD_STATIC)->SetWindowText(buffer);
		} else
			GetDlgItem(IDC_SHIELD_STATIC)->SetWindowText(GetResString(_T("SHIELD_NOT_LOADED")));

		LocalizeCommonItems();

		LocalizeItemText(m_htiGeneralOptions, _T("GENERAL_OPTIONS"));
		LocalizeItemInfoText(m_htiGeneralOptions, _T("GENERAL_OPTIONS_INFO"));

		LocalizeEditLabel(m_htiTimingOptions, _T("TIMING_OPTIONS"));
		LocalizeItemInfoText(m_htiTimingOptions, _T("TIMING_OPTIONS_INFO"));

		LocalizeEditLabel(m_htiPunishmentCancelationScanPeriod, _T("PUNISHMENT_CANCELLATION_SCAN_PERIOD"));
		LocalizeItemInfoText(m_htiPunishmentCancelationScanPeriod, _T("PUNISHMENT_CANCELLATION_SCAN_PERIOD_INFO"));

		LocalizeEditLabel(m_htiClientBanTime, _T("CLIENT_BAN_TIME")); // adjust ClientBanTime - Stulle
		LocalizeItemInfoText(m_htiClientBanTime, _T("CLIENT_BAN_TIME_INFO"));

		LocalizeEditLabel(m_htiClientScoreReducingTime, _T("CLIENT_SCORE_REDUCING_TIME"));
		LocalizeItemInfoText(m_htiClientScoreReducingTime, _T("CLIENT_SCORE_REDUCING_TIME_INFO"));

		LocalizeItemText(m_htiDontPunishFriends, _T("DONT_PUNISH_FRIENDS"));
		LocalizeItemInfoText(m_htiDontPunishFriends, _T("DONT_PUNISH_FRIENDS_INFO"));

		LocalizeItemText(m_htiDontAllowFileHotSwapping, _T("DONT_ALLOW_FILE_HOT_SWAPPING"));
		LocalizeItemInfoText(m_htiDontAllowFileHotSwapping, _T("DONT_ALLOW_FILE_HOT_SWAPPING_INFO"));

		LocalizeItemText(m_htiInformBadClients, _T("INFORM_BAD_CLIENTS")); // => Inform Leechers - sFrQlXeRt
		LocalizeItemInfoText(m_htiInformBadClients, _T("INFORM_BAD_CLIENTS_INFO"));

		LocalizeEditLabel(m_htiInformBadClientsText, _T("INFORM_BAD_CLIENTS_TEXT")); // => Inform Leechers Text - evcz
		LocalizeItemInfoText(m_htiInformBadClientsText, _T("INFORM_BAD_CLIENTS_TEXT_INFO"));

		LocalizeItemText(m_htiAntiUploadProtection, _T("ANTI_UPLOAD_PROTECTION")); // => Anti Upload Protection - sFrQlXeRt
		LocalizeItemInfoText(m_htiAntiUploadProtection, _T("ANTI_UPLOAD_PROTECTION_INFO"));

		LocalizeEditLabel(m_htiAntiUploadProtectionLimit, _T("ANTI_UPLOAD_PROTECTION_LIMIT"));
		LocalizeItemInfoText(m_htiAntiUploadProtectionLimit, _T("ANTI_UPLOAD_PROTECTION_LIMIT_INFO"));

		LocalizeEditLabel(m_htiUploaderPunishmentPrevention, _T("ENABLE_UPLOADER_PUNISMENT_PREVENTION")); // => Uploader Punishment Prevention [Stulle] - sFrQlXeRt
		LocalizeItemInfoText(m_htiUploaderPunishmentPrevention, _T("ENABLE_UPLOADER_PUNISMENT_PREVENTION_INFO"));
		LocalizeEditLabel(m_htiUploaderPunishmentPreventionLimit, _T("UPLOADER_PUNISMENT_PREVENTION_LIMIT"));
		LocalizeItemInfoText(m_htiUploaderPunishmentPreventionLimit, _T("UPLOADER_PUNISMENT_PREVENTION_LIMIT_INFO"));

		LocalizeItemText(m_htiAntiCase1, _T("UPLOADER_PUNISMENT_PREVENTION_CASE_1"));
		LocalizeItemInfoText(m_htiAntiCase1, _T("UPLOADER_PUNISMENT_PREVENTION_CASE_1_INFO"));

		LocalizeItemText(m_htiAntiCase2, _T("UPLOADER_PUNISMENT_PREVENTION_CASE_2"));
		LocalizeItemInfoText(m_htiAntiCase2, _T("UPLOADER_PUNISMENT_PREVENTION_CASE_2_INFO"));

		LocalizeItemText(m_htiAntiCase3, _T("UPLOADER_PUNISMENT_PREVENTION_CASE_3"));
		LocalizeItemInfoText(m_htiAntiCase3, _T("UPLOADER_PUNISMENT_PREVENTION_CASE_3_INFO"));

		LocalizeItemText(m_htiLeecherDetection, _T("SHIELD"));
		LocalizeItemInfoText(m_htiLeecherDetection, _T("SHIELD_INFO"));

		LocalizeItemText(m_htiDetectModNames, _T("CHECK_MODNAMES"));
		LocalizeItemInfoText(m_htiDetectModNames, _T("CHECK_MODNAMES_INFO"));

		LocalizeItemText(m_htiDetectUserNames, _T("CHECK_USERNAMES"));
		LocalizeItemInfoText(m_htiDetectUserNames, _T("CHECK_USERNAMES_INFO"));

		LocalizeItemText(m_htiDetectUserHashes, _T("CHECK_USERHASHES"));
		LocalizeItemInfoText(m_htiDetectUserHashes, _T("CHECK_USERHASHES_INFO"));

		LocalizeItemText(m_htiHardLeecherPunishment, _T("HARD_LEECHER_PUNISHMENT"));
		LocalizeItemInfoText(m_htiHardLeecherPunishment, _T("HARD_LEECHER_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiIPUserHash, _T("IP_USER_HASH_BAN"));
		LocalizeItemInfoText(m_htiIPUserHash, _T("IP_USER_HASH_BAN_INFO"));

		LocalizeItemText(m_htiUserHashBan, _T("USER_HASH_BAN"));
		LocalizeItemInfoText(m_htiUserHashBan, _T("USER_HASH_BAN_INFO"));

		LocalizeItemText(m_htiUploadBan, _T("UPLOAD_BAN"));
		LocalizeItemInfoText(m_htiUploadBan, _T("UPLOAD_BAN_INFO"));

		LocalizeItemText(m_htiScore01, _T("SCORE_01"));
		LocalizeItemInfoText(m_htiScore01, _T("SCORE_01_INFO"));

		LocalizeItemText(m_htiScore02, _T("SCORE_02"));
		LocalizeItemInfoText(m_htiScore02, _T("SCORE_02_INFO"));

		LocalizeItemText(m_htiScore03, _T("SCORE_03"));
		LocalizeItemInfoText(m_htiScore03, _T("SCORE_03_INFO"));

		LocalizeItemText(m_htiScore04, _T("SCORE_04"));
		LocalizeItemInfoText(m_htiScore04, _T("SCORE_04_INFO"));

		LocalizeItemText(m_htiScore05, _T("SCORE_05"));
		LocalizeItemInfoText(m_htiScore05, _T("SCORE_05_INFO"));

		LocalizeItemText(m_htiScore06, _T("SCORE_06"));
		LocalizeItemInfoText(m_htiScore06, _T("SCORE_06_INFO"));

		LocalizeItemText(m_htiScore07, _T("SCORE_07"));
		LocalizeItemInfoText(m_htiScore07, _T("SCORE_07_INFO"));

		LocalizeItemText(m_htiScore08, _T("SCORE_08"));
		LocalizeItemInfoText(m_htiScore08,_T("SCORE_08_INFO"));

		LocalizeItemText(m_htiScore09, _T("SCORE_09"));
		LocalizeItemInfoText(m_htiScore09,_T("SCORE_09_INFO"));

		LocalizeItemText(m_htiNoPunishment, _T("NO_PUNISHMENT"));
		LocalizeItemInfoText(m_htiNoPunishment, _T("NO_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiSoftLeecherPunishment, _T("SOFT_LEECHER_PUNISHMENT"));
		LocalizeItemInfoText(m_htiSoftLeecherPunishment,_T("SOFT_LEECHER_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiBanBadKadNodes, _T("BAN_BAD_KAD_NODES"));
		LocalizeItemInfoText(m_htiBanBadKadNodes,_T("BAN_BAD_KAD_NODES_INFO"));

		LocalizeItemText(m_htiDetectAntiP2PBots, _T("DETECT_ANTI_P2P_BOTS"));
		LocalizeItemInfoText(m_htiDetectAntiP2PBots,_T("DETECT_ANTI_P2P_BOTS_INFO"));

		LocalizeItemText(m_htiBanWrongPackage, _T("BAN_WRONG_PACKAGE"));
		LocalizeItemInfoText(m_htiBanWrongPackage,_T("BAN_WRONG_PACKAGE_INFO"));

		LocalizeItemText(m_htiAntiP2PBotsPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiAntiP2PBotsPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectWrongTag, _T("DETECT_WRONG_TAG"));
		LocalizeItemInfoText(m_htiDetectWrongTag,_T("DETECT_WRONG_TAG_INFO"));

		LocalizeItemText(m_htiWrongTagPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiWrongTagPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectUnknownTag, _T("DETECT_UNKNOWN_TAG"));
		LocalizeItemInfoText(m_htiDetectUnknownTag,_T("DETECT_UNKNOWN_TAG_INFO"));

		LocalizeItemText(m_htiUnknownTagPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiUnknownTagPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectHashThief, _T("DETECT_HASH_THIEF"));
		LocalizeItemInfoText(m_htiDetectHashThief,_T("DETECT_HASH_THIEF_INFO"));

		LocalizeItemText(m_htiHashThiefPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiHashThiefPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectModThief, _T("DETECT_MOD_THIEF"));
		LocalizeItemInfoText(m_htiDetectModThief,_T("DETECT_MOD_THIEF_INFO"));

		LocalizeItemText(m_htiModThiefPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiModThiefPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectUserNameThief, _T("DETECT_USERNAME_THIEF"));
		LocalizeItemInfoText(m_htiDetectUserNameThief,_T("DETECT_USERNAME_THIEF_INFO"));

		LocalizeItemText(m_htiUserNameThiefPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiUserNameThiefPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectModChanger, _T("DETECT_MOD_CHANGER"));
		LocalizeItemInfoText(m_htiDetectModChanger,_T("DETECT_MOD_CHANGER_INFO"));

		LocalizeEditLabel(m_htiModChangerInterval, _T("MOD_CHANGER_INTERVALS"));
		LocalizeItemInfoText(m_htiModChangerInterval,_T("MOD_CHANGER_INTERVALS_INFO"));

		LocalizeEditLabel(m_htiModChangerThreshold, _T("MOD_CHANGER_THRESHOLD"));
		LocalizeItemInfoText(m_htiModChangerThreshold,_T("MOD_CHANGER_THRESHOLD_INFO"));

		LocalizeItemText(m_htiModChangerPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiModChangerPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectUserNameChanger, _T("DETECT_USER_NAME_CHANGER"));
		LocalizeItemInfoText(m_htiDetectUserNameChanger,_T("DETECT_USER_NAME_CHANGER_INFO"));

		LocalizeEditLabel(m_htiUserNameChangerInterval, _T("USER_NAME_CHANGER_INTERVALS"));
		LocalizeItemInfoText(m_htiUserNameChangerInterval,_T("USER_NAME_CHANGER_INTERVALS_INFO"));

		LocalizeEditLabel(m_htiUserNameChangerThreshold, _T("USER_NAME_CHANGER_THRESHOLD"));
		LocalizeItemInfoText(m_htiUserNameChangerThreshold,_T("USER_NAME_CHANGER_THRESHOLD_INFO"));

		LocalizeItemText(m_htiUserNameChangerPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiUserNameChangerPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectTCPErrorFlooder, _T("DETECT_TCP_ERROR_FLOODER"));
		LocalizeItemInfoText(m_htiDetectTCPErrorFlooder, _T("DETECT_TCP_ERROR_FLOODER_INFO"));

		LocalizeEditLabel(m_htiTCPErrorFlooderInterval, _T("TCP_ERROR_FLOODER_INTERVALS"));
		LocalizeItemInfoText(m_htiTCPErrorFlooderInterval,_T("TCP_ERROR_FLOODER_INTERVALS_INFO"));

		LocalizeEditLabel(m_htiTCPErrorFlooderThreshold, _T("TCP_ERROR_FLOODER_THRESHOLD"));
		LocalizeItemInfoText(m_htiTCPErrorFlooderThreshold,_T("TCP_ERROR_FLOODER_THRESHOLD_INFO"));

		LocalizeItemText(m_htiTCPErrorFlooderPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiTCPErrorFlooderPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectEmptyUserNameEmule, _T("DETECT_EMPTY_USERNAME_EMULE"));
		LocalizeItemInfoText(m_htiDetectEmptyUserNameEmule,_T("DETECT_EMPTY_USERNAME_EMULE_INFO"));

		LocalizeItemText(m_htiEmptyUserNameEmulePunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiEmptyUserNameEmulePunishment,_T("IDS_GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectCommunity, _T("DETECT_COMMUNITY"));
		LocalizeItemInfoText(m_htiDetectCommunity,_T("DETECT_COMMUNITY_INFO"));

		LocalizeItemText(m_htiCommunityPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiCommunityPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectFakeEmule, _T("DETECT_FAKE_EMULE"));
		LocalizeItemInfoText(m_htiDetectFakeEmule,_T("DETECT_FAKE_EMULE_INFO"));

		LocalizeItemText(m_htiFakeEmulePunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiFakeEmulePunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectHexModName, _T("DETECT_HEX_MOD_NAME"));
		LocalizeItemInfoText(m_htiDetectHexModName,_T("DETECT_HEX_MOD_NAME_INFO"));

		LocalizeItemText(m_htiHexModNamePunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiHexModNamePunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectGhostMod, _T("DETECT_GHOST_MOD"));
		LocalizeItemInfoText(m_htiDetectGhostMod,_T("DETECT_GHOST_MOD_INFO"));

		LocalizeItemText(m_htiGhostModPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiGhostModPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectSpam, _T("DETECT_SPAM"));
		LocalizeItemInfoText(m_htiDetectSpam,_T("DETECT_SPAM_INFO"));

		LocalizeItemText(m_htiSpamPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiSpamPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectEmcrypt, _T("DETECT_EMCRYPT"));
		LocalizeItemInfoText(m_htiDetectEmcrypt,_T("DETECT_EMCRYPT_INFO"));

		LocalizeItemText(m_htiEmcryptPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiEmcryptPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectXSExploiter, _T("DETECT_XS_EXPLOITER"));
		LocalizeItemInfoText(m_htiDetectXSExploiter,_T("DETECT_XS_EXPLOITER_INFO"));

		LocalizeItemText(m_htiXSExploiterPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiXSExploiterPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectFileFaker, _T("DETECT_FILEFAKER"));
		LocalizeItemInfoText(m_htiDetectFileFaker,_T("DETECT_FILEFAKER_INFO"));

		LocalizeItemText(m_htiFileFakerPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiFileFakerPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectUploadFaker, _T("DETECT_UPLOADFAKER"));
		LocalizeItemInfoText(m_htiDetectUploadFaker,_T("DETECT_UPLOADFAKER_INFO"));

		LocalizeItemText(m_htiUploadFakerPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiUploadFakerPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectUploadRequestAbuse, _T("DETECT_UPLOAD_REQUEST_ABUSE"));
		LocalizeItemInfoText(m_htiDetectUploadRequestAbuse,_T("DETECT_UPLOAD_REQUEST_ABUSE_INFO"));
		LocalizeItemText(m_htiDetectUploadRequestAbuseNoRequestSlots, _T("UPLOAD_REQUEST_ABUSE_NO_REQUEST_SLOTS"));
		LocalizeItemInfoText(m_htiDetectUploadRequestAbuseNoRequestSlots,_T("UPLOAD_REQUEST_ABUSE_NO_REQUEST_SLOTS_INFO"));
		LocalizeItemText(m_htiDetectUploadRequestAbuseQueueDrops, _T("UPLOAD_REQUEST_ABUSE_QUEUE_DROPS"));
		LocalizeItemInfoText(m_htiDetectUploadRequestAbuseQueueDrops,_T("UPLOAD_REQUEST_ABUSE_QUEUE_DROPS_INFO"));
		LocalizeItemText(m_htiUploadRequestAbuseCounterGuard, _T("UPLOAD_REQUEST_ABUSE_COUNTER_GUARD"));
		LocalizeItemInfoText(m_htiUploadRequestAbuseCounterGuard,_T("UPLOAD_REQUEST_ABUSE_COUNTER_GUARD_INFO"));
		LocalizeItemText(m_htiUploadRequestAbuseHashRotationTracking, _T("UPLOAD_REQUEST_ABUSE_HASH_ROTATION"));
		LocalizeItemInfoText(m_htiUploadRequestAbuseHashRotationTracking,_T("UPLOAD_REQUEST_ABUSE_HASH_ROTATION_INFO"));
		LocalizeItemText(m_htiUploadRequestAbusePostHelloDisconnect, _T("UPLOAD_REQUEST_ABUSE_POST_HELLO_DISCONNECT"));
		LocalizeItemInfoText(m_htiUploadRequestAbusePostHelloDisconnect,_T("UPLOAD_REQUEST_ABUSE_POST_HELLO_DISCONNECT_INFO"));
		LocalizeItemText(m_htiUploadRequestAbusePunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiUploadRequestAbusePunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiDetectAgressive, _T("DETECT_AGRESSIVE"));
		LocalizeItemInfoText(m_htiDetectAgressive,_T("DETECT_AGRESSIVE_INFO"));
		
		LocalizeEditLabel(m_htiAgressiveTime, _T("AGRESSIVE_TIME"));
		LocalizeItemInfoText(m_htiAgressiveTime,_T("AGRESSIVE_TIME_INFO"));

		LocalizeEditLabel(m_htiAgressiveCounter, _T("AGRESSIVE_COUNTER"));
		LocalizeItemInfoText(m_htiAgressiveCounter,_T("AGRESSIVE_COUNTER_INFO"));

		LocalizeItemText(m_htiAgressiveLog, _T("AGRESSIVE_LOG"));
		LocalizeItemInfoText(m_htiAgressiveLog,_T("AGRESSIVE_LOG_INFO"));

		LocalizeItemText(m_htiAgressivePunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiAgressivePunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiPunishNonSuiClients, _T("PUNISH_NONSUI_CLIENTS"));
		LocalizeItemInfoText(m_htiPunishNonSuiClients,_T("PUNISH_NONSUI_CLIENTS_INFO"));

		LocalizeItemText(m_htiPunishNonSuiMlDonkey, _T("PUNISH_NONSUI_MLDONKEY"));
		LocalizeItemInfoText(m_htiPunishNonSuiMlDonkey,_T("PUNISH_NONSUI_MLDONKEY_INFO"));

		LocalizeItemText(m_htiPunishNonSuiEdonkey, _T("PUNISH_NONSUI_EDONKEY"));
		LocalizeItemInfoText(m_htiPunishNonSuiEdonkey,_T("PUNISH_NONSUI_EDONKEY_INFO"));

		LocalizeItemText(m_htiNonSuiPunishment, _T("PUNISHMENT"));
		LocalizeItemInfoText(m_htiNonSuiPunishment,_T("GENERAL_PUNISHMENT_INFO"));

		LocalizeItemText(m_htiPunishNonSuiEdonkeyHybrid, _T("PUNISH_NONSUI_EDONKEY_HYBRID"));
		LocalizeItemInfoText(m_htiPunishNonSuiEdonkeyHybrid,_T("PUNISH_NONSUI_EDONKEY_HYBRID_INFO"));

		LocalizeItemText(m_htiPunishNonSuiShareaza, _T("PUNISH_NONSUI_SHAREAZA"));
		LocalizeItemInfoText(m_htiPunishNonSuiShareaza,_T("PUNISH_NONSUI_SHAREAZA_INFO"));

		LocalizeItemText(m_htiPunishNonSuiLphant, _T("PUNISH_NONSUI_LPHANT"));
		LocalizeItemInfoText(m_htiPunishNonSuiLphant,_T("PUNISH_NONSUI_LPHANT_INFO"));

		LocalizeItemText(m_htiPunishNonSuiAmule, _T("PUNISH_NONSUI_AMULE"));
		LocalizeItemInfoText(m_htiPunishNonSuiAmule,_T("PUNISH_NONSUI_AMULE_INFO"));

		LocalizeItemText(m_htiPunishNonSuiEmule, _T("PUNISH_NONSUI_EMULE"));
		LocalizeItemInfoText(m_htiPunishNonSuiEmule,_T("PUNISH_NONSUI_EMULE_INFO"));

		LocalizeItemText(m_htiDetectCorruptedDataSender, _T("DETECT_CORRUPTED_DATA_SENDER"));
		LocalizeItemInfoText(m_htiDetectCorruptedDataSender,_T("DETECT_CORRUPTED_DATA_SENDER_INFO"));

		LocalizeItemText(m_htiDetectHashChanger, _T("DETECT_HASH_CHANGER"));
		LocalizeItemInfoText(m_htiDetectHashChanger,_T("DETECT_HASH_CHANGER_INFO"));

		LocalizeItemText(m_htiDetectFileScanner, _T("DETECT_FILE_SCANNER"));
		LocalizeItemInfoText(m_htiDetectFileScanner,_T("DETECT_FILE_SCANNER_INFO"));

		LocalizeItemText(m_htiDetectRankFlooder, _T("DETECT_RANK_FLOODER"));
		LocalizeItemInfoText(m_htiDetectRankFlooder,_T("DETECT_RANK_FLOODER_INFO"));

		LocalizeItemText(m_htiDetectKadRequestFlooder, _T("DETECT_KAD_REQ_FLOODER"));
		LocalizeItemInfoText(m_htiDetectKadRequestFlooder,_T("DETECT_KAD_REQ_FLOODER_INFO"));

		LocalizeEditLabel(m_htiKadRequestFloodBanTreshold, _T("KAD_REQ_FLOOD_TRESHOLD"));
		LocalizeItemInfoText(m_htiKadRequestFloodBanTreshold,_T("KAD_REQ_FLOOD_TRESHOLD_INFO"));
	}

}

void CPPgProtectionPanel::LocalizeCommonItems(void)
{
	if (m_hWnd)
	{
		LocalizeItemText(m_htiIPUserHash, _T("IP_USER_HASH_BAN"));
		LocalizeItemInfoText(m_htiIPUserHash,_T("IP_USER_HASH_BAN_INFO"));

		LocalizeItemText(m_htiUserHashBan, _T("USER_HASH_BAN"));
		LocalizeItemInfoText(m_htiUserHashBan,_T("USER_HASH_BAN_INFO"));

		LocalizeItemText(m_htiUploadBan, _T("UPLOAD_BAN"));
		LocalizeItemInfoText(m_htiUploadBan,_T("UPLOAD_BAN_INFO"));

		LocalizeItemText(m_htiScore01, _T("SCORE_01"));
		LocalizeItemInfoText(m_htiScore01,_T("SCORE_01_INFO"));

		LocalizeItemText(m_htiScore02, _T("SCORE_02"));
		LocalizeItemInfoText(m_htiScore02,_T("SCORE_02_INFO"));

		LocalizeItemText(m_htiScore03, _T("SCORE_03"));
		LocalizeItemInfoText(m_htiScore03,_T("SCORE_03_INFO"));

		LocalizeItemText(m_htiScore04, _T("SCORE_04"));
		LocalizeItemInfoText(m_htiScore04,_T("SCORE_04_INFO"));

		LocalizeItemText(m_htiScore05, _T("SCORE_05"));
		LocalizeItemInfoText(m_htiScore05,_T("SCORE_05_INFO"));

		LocalizeItemText(m_htiScore06, _T("SCORE_06"));
		LocalizeItemInfoText(m_htiScore06,_T("SCORE_06_INFO"));

		LocalizeItemText(m_htiScore07, _T("SCORE_07"));
		LocalizeItemInfoText(m_htiScore07,_T("SCORE_07_INFO"));

		LocalizeItemText(m_htiScore08, _T("SCORE_08"));
		LocalizeItemInfoText(m_htiScore08,_T("SCORE_08_INFO"));

		LocalizeItemText(m_htiScore09, _T("SCORE_09"));
		LocalizeItemInfoText(m_htiScore09,_T("SCORE_09_INFO"));

		LocalizeItemText(m_htiNoPunishment, _T("NO_PUNISHMENT"));
		LocalizeItemInfoText(m_htiNoPunishment,_T("NO_PUNISHMENT_INFO"));
	}
}

void CPPgProtectionPanel::OnDestroy()
{
	m_ctrlTreeOptions.DeleteAllItems();
	m_ctrlTreeOptions.DestroyWindow();
	m_bInitializedTreeOpts = false;

	m_htiGeneralOptions = NULL;
	m_htiTimingOptions = NULL;
	m_htiPunishmentCancelationScanPeriod = NULL;
	m_htiClientBanTime = NULL; // adjust ClientBanTime - Stulle
	m_htiClientScoreReducingTime = NULL;
	m_htiDontPunishFriends = NULL;
	m_htiDontAllowFileHotSwapping = NULL;
	m_htiInformBadClients = NULL; // => Inform Leechers - sFrQlXeRt
	m_htiInformBadClientsText = NULL; // => Inform Leechers Text - evcz
	m_htiAntiUploadProtection = NULL;// ==> Anti Upload Protection - sFrQlXeRt
	m_htiAntiUploadProtectionLimit = NULL;// ==> Anti Upload Protection - sFrQlXeRt
	m_htiUploaderPunishmentPrevention = NULL;
	m_htiUploaderPunishmentPreventionLimit = NULL;
	m_htiAntiCase1 = NULL;
	m_htiAntiCase2 = NULL;
	m_htiAntiCase3 = NULL;

	m_htiLeecherDetection = NULL;
	m_htiDetectModNames = NULL;
	m_htiDetectUserNames = NULL;
	m_htiDetectUserHashes = NULL;
	m_htiIPUserHash = NULL;
	m_htiUserHashBan = NULL;
	m_htiUploadBan = NULL;
	m_htiScore01 = NULL;
	m_htiScore02 = NULL;
	m_htiScore03 = NULL;
	m_htiScore04 = NULL;
	m_htiScore05 = NULL;
	m_htiScore06 = NULL;
	m_htiScore07 = NULL;
	m_htiScore08 = NULL;
	m_htiScore09 = NULL;
	m_htiNoPunishment = NULL;
	m_htiBanBadKadNodes = NULL;
	m_htiBanWrongPackage = NULL;
	m_htiDetectAntiP2PBots = NULL;
	m_htiAntiP2PBotsPunishment = NULL;
	m_htiDetectWrongTag = NULL;
	m_htiWrongTagPunishment = NULL;
	m_htiDetectUnknownTag = NULL;
	m_htiUnknownTagPunishment = NULL;
	m_htiDetectHashThief = NULL;
	m_htiHashThiefPunishment = NULL;
	m_htiDetectModThief = NULL;
	m_htiModThiefPunishment = NULL;
	m_htiDetectUserNameThief = NULL;
	m_htiUserNameThiefPunishment = NULL;
	m_htiDetectModChanger = NULL;
	m_htiModChangerInterval = NULL;
	m_htiModChangerThreshold = NULL;
	m_htiModChangerPunishment = NULL;
	m_htiDetectUserNameChanger = NULL;
	m_htiUserNameChangerInterval = NULL;
	m_htiUserNameChangerThreshold = NULL;
	m_htiUserNameChangerPunishment = NULL;
	m_htiDetectTCPErrorFlooder = NULL;
	m_htiTCPErrorFlooderInterval = NULL;
	m_htiTCPErrorFlooderThreshold = NULL;
	m_htiTCPErrorFlooderPunishment = NULL;
	m_htiDetectEmptyUserNameEmule = NULL;
	m_htiEmptyUserNameEmulePunishment = NULL;
	m_htiDetectCommunity = NULL;
	m_htiCommunityPunishment = NULL;
	m_htiDetectFakeEmule = NULL;
	m_htiFakeEmulePunishment = NULL;
	m_htiDetectHexModName = NULL;
	m_htiHexModNamePunishment = NULL;
	m_htiDetectGhostMod = NULL;
	m_htiGhostModPunishment = NULL;
	m_htiDetectSpam = NULL;
	m_htiSpamPunishment = NULL;
	m_htiDetectEmcrypt = NULL;
	m_htiEmcryptPunishment = NULL;
	m_htiDetectXSExploiter = NULL;
	m_htiXSExploiterPunishment = NULL;
	m_htiDetectFileFaker = NULL;
	m_htiFileFakerPunishment = NULL;
	m_htiDetectUploadFaker = NULL;
	m_htiUploadFakerPunishment = NULL;
	m_htiDetectUploadRequestAbuse = NULL;
	m_htiDetectUploadRequestAbuseNoRequestSlots = NULL;
	m_htiDetectUploadRequestAbuseQueueDrops = NULL;
	m_htiUploadRequestAbuseCounterGuard = NULL;
	m_htiUploadRequestAbuseHashRotationTracking = NULL;
	m_htiUploadRequestAbusePostHelloDisconnect = NULL;
	m_htiUploadRequestAbusePunishment = NULL;

	m_htiDetectAgressive = NULL;
	m_htiAgressiveTime = NULL;
	m_htiAgressiveCounter = NULL;
	m_htiAgressiveLog = NULL;
	m_htiAgressivePunishment = NULL;

	m_htiPunishNonSuiClients = NULL;
	m_htiPunishNonSuiMlDonkey = NULL;
	m_htiPunishNonSuiEdonkey = NULL;
	m_htiPunishNonSuiEdonkeyHybrid = NULL;
	m_htiPunishNonSuiShareaza = NULL;
	m_htiPunishNonSuiLphant = NULL;
	m_htiPunishNonSuiAmule = NULL;
	m_htiPunishNonSuiEmule = NULL;
	m_htiNonSuiPunishment = NULL;

	m_htiTweakOfficialFeatures = NULL;
	m_htiDetectCorruptedDataSender = NULL;
	m_htiDetectHashChanger = NULL;
	m_htiDetectFileScanner = NULL;
	m_htiDetectRankFlooder = NULL;
	m_htiDetectKadRequestFlooder = NULL;
	m_htiKadRequestFloodBanTreshold = NULL;

	CPropertyPage::OnDestroy();
}

LRESULT CPPgProtectionPanel::OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM /*lParam*/)
{
	if (wParam == IDC_PROTECTION_PANEL_OPTS){


		SetModified();
	}
	return 0;
}

void CPPgProtectionPanel::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) 
{
	SetModified(TRUE);
	CString temp;
	CPropertyPage::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CPPgProtectionPanel::OnHelp()
{
}

BOOL CPPgProtectionPanel::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_HELP)
	{
		OnHelp();
		return TRUE;
	}
	return __super::OnCommand(wParam, lParam);
}

BOOL CPPgProtectionPanel::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	OnHelp();
	return TRUE;
}

LRESULT CPPgProtectionPanel::DrawTreeItemHelp(WPARAM wParam, LPARAM lParam)
{
	if (!IsWindowVisible())
		return 0;

	if (wParam == IDC_PROTECTION_PANEL_OPTS) {
		CString* sInfo = (CString*)lParam;
		SetDlgItemText(IDC_PROTECTION_PANEL_OPTS_INFO, *sInfo);
	}
	return FALSE;
}

void CPPgProtectionPanel::OnBnClickedShieldReload()
{
	theApp.shield->LoadShieldFile();

	if (theApp.shield->m_bShieldFileLoaded) {
		CString buffer;
		buffer.Format(GetResString(_T("SHIELD_LOADED")), theApp.shield->GetLoadedDefinitionCount());
		GetDlgItem(IDC_SHIELD_STATIC)->SetWindowText(buffer);
	} else
		GetDlgItem(IDC_SHIELD_STATIC)->SetWindowText(GetResString(_T("SHIELD_NOT_LOADED")));
}

HBRUSH CPPgProtectionPanel::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	switch (nCtlColor)
	{
	case CTLCOLOR_STATIC:
		if (pWnd->GetSafeHwnd() == GetDlgItem(IDC_PROTECTION_PANEL_OPTS_INFO)->GetSafeHwnd()) {
			pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));	// Text font colour
			pDC->SetBkColor(RGB(230, 230, 230)); // Text background colour
			return CDarkMode::s_brHelpTextBackground;
		}
	default:
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}
}
