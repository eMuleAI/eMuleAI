//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include <float.h>
#include <locale.h>
#include "emule.h"
#include "SearchDlg.h"
#include "PPgMod.h"
#include "Scheduler.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "TransferDlg.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "ServerWnd.h"
#include "HelpIDs.h"
#include "Log.h"
#include "UserMsgs.h"
#include "opcodes.h"
#include "eMuleAI/IPGeolocation.h"
#include "ClientCredits.h"
#include "SharedFileList.h"
#include "KnownFile.h"
#include "ClientList.h"
#include "KadContactListCtrl.h"
#include "KadSearchListCtrl.h"
#include "kademlia\kademlia\Defines.h"
#include "MuleToolbarCtrl.h"
#include "eMuleAI/DarkMode.h"
#include "PreferencesDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

///////////////////////////////////////////////////////////////////////////////
// CPPgMod dialog

IMPLEMENT_DYNAMIC(CPPgMod, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgMod, CPropertyPage)
	ON_WM_HSCROLL()
	ON_WM_DESTROY()
	ON_MESSAGE(UM_TREEOPTSCTRL_NOTIFY, OnTreeOptsCtrlNotify)
	ON_MESSAGE(WM_TREEITEM_HELP, DrawTreeItemHelp)
	ON_WM_HELPINFO()
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
END_MESSAGE_MAP()

namespace
{
	bool ParseFloatOptionText(CString strText, float& fValue)
	{
		strText.Trim();
		if (strText.IsEmpty())
			return false;

		strText.Replace(_T(','), _T('.'));
		LPCTSTR pszText = strText;
		LPTSTR pszEnd = NULL;
		_locale_t locale = _create_locale(LC_NUMERIC, "C");
		const double dValue = locale != NULL ? _tcstod_l(pszText, &pszEnd, locale) : _tcstod(pszText, &pszEnd);
		if (locale != NULL)
			_free_locale(locale);
		if (pszEnd == pszText || !_finite(dValue))
			return false;

		CString strTail(pszEnd);
		strTail.Trim();
		if (!strTail.IsEmpty())
			return false;

		fValue = static_cast<float>(dValue);
		return true;
	}
}

CPPgMod::CPPgMod()
	: CPropertyPage(CPPgMod::IDD)
	, m_ctrlTreeOptions(theApp.m_iDfltImageListColorFlags)
	, m_bInitializedTreeOpts()

	, m_htiUITweaks()
	, m_htiUIDarkMode()
	, m_htiUIDarkModeAuto()
	, m_htiUIDarkModeOn()
	, m_htiUIDarkModeOff()
	, m_htiUITweaksSpeedGraph()
	, m_htiShowDownloadCommandsToolbar()
	, m_htiDisableFindAsYouType()
	, m_htiUITweaksListUpdatePeriod()
	, m_htiUITweaksMaxSortHistory()
	, m_htiUITweaksServerMaxSortHistory()
	, m_htiUITweaksSearchMaxSortHistory()
	, m_htiUITweaksFilesMaxSortHistory()
	, m_htiUITweaksDownloadMaxSortHistory()
	, m_htiUITweaksDownloadClientsMaxSortHistory()
	, m_htiUITweaksUploadMaxSortHistory()
	, m_htiUITweaksQueueMaxSortHistory()
	, m_htiUITweaksClientMaxSortHistory()
	, m_htiUITweaksKadContactSortHistory()
	, m_htiUITweaksKadSearchSortHistory()
	, m_iUIDarkMode()
	, m_bUITweaksSpeedGraph()
	, m_bShowDownloadCommandsToolbar()
	, m_bDisableFindAsYouType()
	, m_iUITweaksListUpdatePeriod()
	, m_iUITweaksServerListMaxSortHistory()
	, m_iUITweaksSearchMaxSortHistory()
	, m_iUITweaksFilesMaxSortHistory()
	, m_iUITweaksDownloadMaxSortHistory()
	, m_iUITweaksDownloadClientsMaxSortHistory()
	, m_iUITweaksUploadMaxSortHistory()
	, m_iUITweaksQueueMaxSortHistory()
	, m_iUITweaksClientMaxSortHistory()
	, m_iUITweaksKadContactSortHistory()
	, m_iUITweaksKadSearchSortHistory()

	, m_htiIPGeolocation()
	, m_htiIPGeolocationNameDisable()
	, m_htiIPGeolocationCountryCode()
	, m_htiIPGeolocationCountry()
	, m_htiIPGeolocationCountryCity()
	, m_htiIPGeolocationShowFlag()
	, m_htiIPGeolocationAutoUpdate()
	, m_htiIPGeolocationUpdateURL()
	, m_htiIPGeolocationUpdateNow()
	, m_htiIPGeolocationPeriodDays()
	, m_iIPGeolocationMode()
	, m_bIPGeolocationShowFlag()
	, m_bIPGeolocationAutoUpdate()
	, m_sIPGeolocationUpdateURL()
	, m_iIPGeolocationUpdatePeriodDays()

	, m_htiConTweaks()

	, m_htiUploadSettings()
	, m_htiHighBandwidthUploadPolicy()
	, m_htiHighBandwidthTargetUploadClients()
	, m_htiHighBandwidthUploadSlotElasticPercent()
	, m_htiAutoHighBandwidthDownloadBuffer()
	, m_htiHighBandwidthSlowUploadThresholdFactor()
	, m_htiHighBandwidthSlowUploadGraceSeconds()
	, m_htiHighBandwidthSlowUploadWarmupSeconds()
	, m_htiHighBandwidthZeroUploadGraceSeconds()
	, m_htiHighBandwidthSlowUploadCooldownSeconds()
	, m_bHighBandwidthUploadPolicy()
	, m_iHighBandwidthTargetUploadClients()
	, m_iHighBandwidthUploadSlotElasticPercent()
	, m_bAutoHighBandwidthDownloadBuffer()
	, m_sHighBandwidthSlowUploadThresholdFactor()
	, m_iHighBandwidthSlowUploadGraceSeconds()
	, m_iHighBandwidthSlowUploadWarmupSeconds()
	, m_iHighBandwidthZeroUploadGraceSeconds()
	, m_iHighBandwidthSlowUploadCooldownSeconds()

	, m_htiConnectionChecker()
	, m_htiConnectionCheckerActivate()
	, m_htiConnectionCheckerServer()
	, m_bConnectionChecker()
	, m_sConnectionCheckerServer()

	, m_htiEnableNatTraversal()
	, m_bEnableNatTraversal()
	, m_htiNatTraversalProtocol()
	, m_htiNatTraversalProtocolPreferQuic()
	, m_htiNatTraversalProtocolUtpOnly()
	, m_htiNatTraversalPortWindow()
	, m_htiNatTraversalSweepWindow()
	, m_htiNatTraversalJitterMinMs()
	, m_htiNatTraversalJitterMaxMs()
	, m_iNatTraversalProtocolMode()
	, m_iNatTraversalPortWindow()
	, m_iNatTraversalSweepWindow()
	, m_iNatTraversalJitterMinMs()
	, m_iNatTraversalJitterMaxMs()

	, m_htiMaxServedBuddies()
	, m_iMaxServedBuddies()

	, m_htiMaxEServerBuddySlots()
	, m_iMaxEServerBuddySlots()

	, m_htiRetryFailedTcpConnectionAttempts()
	, m_bRetryFailedTcpConnectionAttempts()

	, m_htiIsreaskSourceAfterIPChange()
	, m_htiInformQueuedClientsAfterIPChange()
	, m_bIsreaskSourceAfterIPChange()
	, m_bInformQueuedClientsAfterIPChange()

	, m_htiReAskFileSrc()
	, m_iReAskFileSrc()

	, m_htiDownloadValidator()
	, m_htiDownloadValidatorPassive()
	, m_htiDownloadValidatorAlwaysAsk()
	, m_htiDownloadValidatorReject()
	, m_htiDownloadValidatorAccept()
	, m_htiDownloadValidatorAcceptPercentage()
	, m_htiDownloadValidatorRejectCanceled()
	, m_htiDownloadValidatorRejectSameHash()
	, m_htiDownloadValidatorRejectBlacklisted()
	, m_htiDownloadValidatorCaseInsensitive()
	, m_htiDownloadValidatorIgnoreExtension()
	, m_htiDownloadValidatorIgnoreTags()
	, m_htiDownloadValidatorDontIgnoreNumericTags()
	, m_htiDownloadValidatorIgnoreNonAlphaNumeric()
	, m_htiDownloadValidatorMinimumComparisonLength()
	, m_htiDownloadValidatorSkipIncompleteFileConfirmation()
	, m_htiDownloadValidatorMarkAsBlacklisted()
	, m_htiDownloadValidatorAutoMarkAsBlacklisted()
	, m_htiDownloadValidatorDateTimeMatching()
	, m_htiDownloadValidatorDateTimeUseYearRange()
	, m_htiDownloadValidatorDateTimeYearStart()
	, m_htiDownloadValidatorDateTimeYearEnd()
	, m_htiDownloadValidatorDateTimeCheckSeconds()
	, m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues()
	, m_iDownloadValidator()
	, m_iDownloadValidatorAcceptPercentage()
	, m_bDownloadValidatorRejectCanceled()
	, m_bDownloadValidatorRejectSameHash()
	, m_bDownloadValidatorRejectBlacklisted()
	, m_bDownloadValidatorCaseInsensitive()
	, m_bDownloadValidatorIgnoreExtension()
	, m_bDownloadValidatorIgnoreTags()
	, m_bDownloadValidatorDontIgnoreNumericTags()
	, m_bDownloadValidatorIgnoreNonAlphaNumeric()
	, m_iDownloadValidatorMinimumComparisonLength()
	, m_bDownloadValidatorSkipIncompleteFileConfirmation()
	, m_bDownloadValidatorMarkAsBlacklisted()
	, m_bDownloadValidatorAutoMarkAsBlacklisted()
	, m_bDownloadValidatorDateTimeMatching()
	, m_bDownloadValidatorDateTimeUseYearRange()
	, m_iDownloadValidatorDateTimeYearStart()
	, m_iDownloadValidatorDateTimeYearEnd()
	, m_bDownloadValidatorDateTimeCheckSeconds()
	, m_bDownloadValidatorDateTimeIncludeFollowingNumericValues()

	, m_htiDownloadInspector()
	, m_htiDownloadInspectorDisable()
	, m_htiDownloadInspectorLogOnly()
	, m_htiDownloadInspectorDelete()
	, m_htiDownloadInspectorFake()
	, m_htiDownloadInspectorDRM()
	, m_htiDownloadInspectorCheckPeriod()
	, m_htiDownloadInspectorCompletedThreshold()
	, m_htiDownloadInspectorZeroPercentageThreshold()
	, m_htiDownloadInspectorCompressionThreshold()
	, m_htiDownloadInspectorBypassZeroPercentage()
	, m_htiDownloadInspectorCompressionThresholdToBypassZero()
	, m_iDownloadInspector()
	, m_bDownloadInspectorFake()
	, m_bDownloadInspectorDRM()
	, m_htiDownloadInspectorInvalidExt()
	, m_htiDownloadInspectorAutoRenameToMajorityName()
	, m_htiDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly()
	, m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent()
	, m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes()
	, m_htiDownloadInspectorAutoDelete()
	, m_htiDownloadInspectorAutoDeleteAddedBefore()
	, m_htiDownloadInspectorAutoDeleteAddedBeforeDays()
	, m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore()
	, m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays()
	, m_htiDownloadInspectorAutoDeleteLastReceivedBefore()
	, m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays()
	, m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent()
	, m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue()
	, m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb()
	, m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue()
	, m_htiDownloadInspectorAutoDeleteBackupEd2kLinks()
	, m_htiDownloadInspectorAutoDeleteDontMarkAsCanceled()
	, m_bDownloadInspectorInvalidExt()
	, m_bDownloadInspectorAutoRenameToMajorityName()
	, m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly()
	, m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent()
	, m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes()
	, m_bDownloadInspectorAutoDeleteEnabled()
	, m_bDownloadInspectorAutoDeleteAddedBeforeEnabled()
	, m_iDownloadInspectorAutoDeleteAddedBeforeDays()
	, m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled()
	, m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays()
	, m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled()
	, m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays()
	, m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled()
	, m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent()
	, m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled()
	, m_iDownloadInspectorAutoDeleteDownloadedLessThanMb()
	, m_bDownloadInspectorAutoDeleteBackupEd2kLinks()
	, m_bDownloadInspectorAutoDeleteDontMarkAsCanceled()
	, m_iDownloadInspectorCheckPeriod()
	, m_iDownloadInspectorCompletedThreshold()
	, m_iDownloadInspectorZeroPercentageThreshold()
	, m_iDownloadInspectorCompressionThreshold()
	, m_bDownloadInspectorBypassZeroPercentage()
	, m_iDownloadInspectorCompressionThresholdToBypassZero()

	, m_htiSearchTweaksGroup()
	, m_htiGroupKnownAtTheBottom()
	, m_bGroupKnownAtTheBottom()
	, m_htiSpamThreshold()
	, m_iSpamThreshold()
	, m_htiEd2kSearchMaxResults()
	, m_iEd2kSearchMaxResults()
	, m_htiEd2kSearchMaxMoreRequests()
	, m_iEd2kSearchMaxMoreRequests()
	, m_htiKadFileSearchTotal()
	, m_iKadFileSearchTotal()
	, m_htiKadSearchKeywordTotal()
	, m_iKadSearchKeywordTotal()
	, m_htiKadFileSearchLifetime()
	, m_iKadFileSearchLifetime()
	, m_htiKadSearchKeywordLifetime()
	, m_iKadSearchKeywordLifetime()
	, m_htiShowCloseButtonOnSearchTabs()
	, m_bShowCloseButtonOnSearchTabs()

	, m_htiServerTweaksGroup()
	, m_htiRepeatServerList()
	, m_bRepeatServerList()
	, m_htiDontRemoveStaticServers()
	, m_bDontRemoveStaticServers()
	, m_htiDontSavePartOnReconnect()
	, m_bDontSavePartOnReconnect()

	, m_htiFileTweaks()
	, m_htiFileHistoryShowPart()
	, m_htiFileHistoryShowShared()
	, m_htiShareTweaks()
	, m_htiFileHistoryShowDuplicate()
	, m_htiAutoShareSubdirs()
	, m_bFileHistoryShowPart()
	, m_bFileHistoryShowShared()
	, m_bFileHistoryShowDuplicate()
	, m_bAutoShareSubdirs()
	, m_htiDontShareExtensions()
	, m_htiDontShareExtensionsList()
	, m_bDontShareExtensions()
	, m_sDontShareExtensionsList()
	, m_htiAdjustNTFSDaylightFileTime()
	, m_bAdjustNTFSDaylightFileTime()
	, m_htiAllowDSTTimeTolerance()
	, m_bAllowDSTTimeTolerance()
	, m_htiSpreadbarSetStatus()
	, m_htiHideOvershares()
	, m_htiSelectiveShare()
	, m_htiShareOnlyTheNeed()
	, m_htiPowerShareGroup()
	, m_htiPowerShareDisabled()
	, m_htiPowerShareActivated()
	, m_htiPowerShareAuto()
	, m_htiPowerShareLimited()
	, m_htiPowerShareLimit()
	, m_htiPowerShareInternalPrio()
	, m_htiPermissionsGroup()
	, m_htiPermissionColorRows()
	, m_htiPermissionEverybody()
	, m_htiPermissionFriendsOnly()
	, m_htiPermissionHidden()
	, m_bSpreadbarSetStatus()
	, m_iHideOvershares()
	, m_bSelectiveShare()
	, m_bShareOnlyTheNeed()
	, m_iPowerShareMode()
	, m_bPowerShareInternalPrio()
	, m_iPowerShareLimit()
	, m_iSharePermissions()
	, m_bSharePermissionColorRows()

	, m_htiEmulator()
	, m_htiEmulateMLDonkey()
	, m_htiEmulateEdonkey()
	, m_htiEmulateEdonkeyHybrid()
	, m_htiEmulateShareaza()
	, m_htiEmulateLphant()
	, m_htiEmulateCommunity()
	, m_htiLogEmulator()
	, m_bEmulateEdonkey()
	, m_bEmulateEdonkeyHybrid()
	, m_bEmulateShareaza()
	, m_bEmulateLphant()
	, m_bEmulateCommunity()
	, m_iEmulateCommunityTagSavingTreshold()
	, m_bLogEmulator()

	, m_htiUseIntelligentChunkSelection()
	, m_bUseIntelligentChunkSelection()

	, m_htiCreditSystem()
	, m_htiOfficialCredit()
	, m_htiLovelaceCredit()
	, m_htiRatioCredit()
	, m_htiPawcioCredit()
	, m_htiESCredit()
	, m_htiMagicAngelCredit()
	, m_htiMagicAngelPlusCredit()
	, m_htiSivkaCredit()
	, m_htiSwatCredit()
	, m_htiTk4Credit()
	, m_htiXtremeCredit()
	, m_htiZzulCredit()

	, m_htiClientHistory()
	, m_htiClientHistoryActivate()
	, m_htiClientHistoryExpDays()
	, m_htiClientHistoryLog()
	, m_bClientHistory()
	, m_iClientHistoryExpDays()
	, m_bClientHistoryLog()

	, m_htiRemoteSharedFiles()
	, m_htiRemoteSharedFilesUserHash()
	, m_htiRemoteSharedFilesClientNote()
	, m_htiRemoteSharedFilesAutoQueryPeriod()
	, m_htiRemoteSharedFilesAutoQueryMaxClients()
	, m_htiRemoteSharedFilesAutoQueryClientPeriod()
	, m_htiRemoteSharedFilesSetAutoQueryDownload()
	, m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold()
	, m_htiRemoteSharedFilesSetAutoQueryUpload()
	, m_htiRemoteSharedFilesSetAutoQueryUploadThreshold()
	, m_bRemoteSharedFilesUserHash()
	, m_bRemoteSharedFilesClientNote()
	, m_iRemoteSharedFilesAutoQueryPeriod()
	, m_iRemoteSharedFilesAutoQueryMaxClients()
	, m_iRemoteSharedFilesAutoQueryClientPeriod()
	, m_bRemoteSharedFilesSetAutoQueryDownload()
	, m_iRemoteSharedFilesSetAutoQueryDownloadThreshold()
	, m_bRemoteSharedFilesSetAutoQueryUpload()
	, m_iRemoteSharedFilesSetAutoQueryUploadThreshold()

	, m_htiSaveLoadSources()
	, m_htiSaveLoadSourcesActivate()
	, m_htiSaveLoadSourcesMaxSources()
	, m_htiSaveLoadSourcesExpirationDays()
	, m_bSaveLoadSources()
	, m_iSaveLoadSourcesMaxSources()
	, m_iSaveLoadSourcesExpirationDays()

	, m_htiMetControl()
	, m_htiDontPurge()
	, m_htiPartiallyPurge()
	, m_htiCompletelyPurge()
	, m_htiKnownMet()
	, m_htiRemoveAichImmediately()
	, m_htiClientsExp()
	, m_iPurgeMode()
	, m_iKnownMetDays()
	, m_bRemoveAichImmediately()
	, m_iClientsExpDays()

	, m_htiBackup()
	, m_htiBackupOnExit()
	, m_htiBackupPeriodic()
	, m_htiBackupPeriod()
	, m_htiBackupMax()
	, m_htiBackupCompressed()
	, m_bBackupOnExit()
	, m_bBackupPeriodic()
	, m_iBackupPeriod()
	, m_iBackupMax()
	, m_bBackupCompressed()

	, m_htiAdvancedPreferences()
	, m_htiAllowedIPs()
	, m_htiBeeper()
	, m_htiCreateCrashDump()
	, m_htiCryptTCPPaddingLength()
	, m_htiDateTimeFormat()
	, m_htiDateTimeFormat4Log()
	, m_htiDateTimeFormat4list()
	, m_htiDebugSearchResultDetailLevel()
	, m_htiDisplay()
	, m_htiDontCompressAvi()
	, m_htiHighresTimer()
	, m_htiICH()
	, m_htiIconflashOnNewMessage()
	, m_htiIgnoreInstances()
	, m_htiInternetSecurityZone()
	, m_htiKeepUnavailableFixedSharedDirs()
	, m_htiLog()
	, m_htiLogFileFormat()
	, m_htiMaxChatHistory()
	, m_htiMaxLogBuff()
	, m_htiMaxMsgSessions()
	, m_htiMiniMule()
	, m_htiMiniMuleAutoClose()
	, m_htiMiniMuleTransparency()
	, m_htiMsgOnlySec()
	, m_htiNotifierMailEncryptCertName()
	, m_htiPreferRestrictedOverUser()
	, m_htiPreviewCopiedArchives()
	, m_htiPreviewOnIconDblClk()
	, m_htiPreviewSmallBlocks()
	, m_htiRTLWindowsLayout()
	, m_htiReBarToolbar()
	, m_htiRearrangeKadSearchKeywords()
	, m_htiRemoveFilesToBin()
	, m_htiRepaint()
	, m_htiRestoreLastLogPane()
	, m_htiRestoreLastMainWndDlg()
	, m_htiServerUDPPort()
	, m_htiShowCopyEd2kLinkCmd()
	, m_htiShowUpDownIconInTaskbar()
	, m_htiShowVerticalHourMarkers()
	, m_htiStraightWindowStyles()
	, m_htiShowActiveDownloadsBold()
	, m_htiTrustEveryHash()
	, m_htiTxtEditor()
	, m_htiUpdateQueue()
	, m_htiUseUserSortedServerList()
	, m_htiWebFileUploadSizeLimitMB()
	, bIgnoreInstances()
	, sNotifierMailEncryptCertName()
	, m_iMaxChatHistory()
	, m_iPreviewSmallBlocks()
	, m_bRestoreLastMainWndDlg()
	, m_bRestoreLastLogPane()
	, m_bPreviewCopiedArchives()
	, m_iStraightWindowStyles()
	, m_bShowActiveDownloadsBold()
	, m_iLogFileFormat()
	, m_bRTLWindowsLayout()
	, m_bPreviewOnIconDblClk()
	, m_bRemoveFilesToBin()
	, m_bHighresTimer()
	, m_bTrustEveryHash()
	, m_umaxmsgsessions()
	, m_bPreferRestrictedOverUser()
	, m_bUseUserSortedServerList()
	, m_iWebFileUploadSizeLimitMB()
	, m_sAllowedIPs()
	, m_iDebugSearchResultDetailLevel()
	, m_iCryptTCPPaddingLength()
	, m_strDateTimeFormat()
	, m_strDateTimeFormat4Log()
	, m_strDateTimeFormat4List()
	, m_bShowVerticalHourMarkers()
	, m_bReBarToolbar()
	, m_bIconflashOnNewMessage()
	, m_bShowCopyEd2kLinkCmd()
	, m_dontcompressavi()
	, m_ICH()
	, m_bRearrangeKadSearchKeywords()
	, m_bUpdateQueue()
	, m_bRepaint()
	, m_bBeeper()
	, m_bMsgOnlySec()
	, m_bShowUpDownIconInTaskbar()
	, m_bKeepUnavailableFixedSharedDirs()
	, m_bExtControls()
{
	if (thePrefs.DoPartiallyPurgeOldKnownFiles())
		m_iPurgeMode = 1;
	else if (thePrefs.GetCompletlyPurgeOldKnownFiles())
		m_iPurgeMode = 2;
	else
		m_iPurgeMode = 0;
}

void CPPgMod::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MOD_OPTS, m_ctrlTreeOptions);
	if (!m_bInitializedTreeOpts) {
		int iImgLog = 8;
		int iImgConnection = 8;
		int iImgUITweaks = 8;
		int iImgUIDarkMode = 8;
		int iImgSort = 8;
		int iImgIPGeolocation = 8;
		int iImgConTweaks = 8;
		int iImgUploadSettings = 8;
		int iImgPrefBlue = 8;
		int iImgInspect = 8;
		int iImgESearch = 8;
		int iImgEServer = 8;
		int iImgFileHistory = 8;
		int iImgShareTweaks = 8;
		int iImgPowerShare = 8;
		int iImgPermission = 8;
		int iImgEmulator = 8;
		int iImgCreditSystem = 8;
		int iImgClientHistory = 8;
		int iImgRemoteSharedFiles = 8;
		int iImgSaveLoad = 8;
		int iImgPrefPink = 8;
		int iImgMinimule = 8;
		int iImgDisplay = 8;
		int iImgMETC = 8;
		int iImgBackup = 8;
		CImageList *piml = m_ctrlTreeOptions.GetImageList(TVSIL_NORMAL);
		if (piml) {
			iImgLog = piml->Add(CTempIconLoader(_T("Log")));
			iImgConnection = piml->Add(CTempIconLoader(_T("connection")));
			iImgUITweaks = piml->Add(CTempIconLoader(_T("UI")));
			iImgUIDarkMode = piml->Add(CTempIconLoader(_T("DARKMODE")));
			iImgSort = piml->Add(CTempIconLoader(_T("SORT")));
			iImgIPGeolocation = piml->Add(CTempIconLoader(_T("LOCATION")));
			iImgConTweaks = piml->Add(CTempIconLoader(_T("CONNECTION2")));
			iImgUploadSettings = piml->Add(CTempIconLoader(_T("UPLOADSETTINGS")));
			iImgPrefBlue = piml->Add(CTempIconLoader(_T("PREFERENCESBLUE")));
			iImgInspect = piml->Add(CTempIconLoader(_T("INSPECT")));
			iImgESearch = piml->Add(CTempIconLoader(_T("SEARCH")));
			iImgEServer = piml->Add(CTempIconLoader(_T("ESERVER")));
			iImgFileHistory = piml->Add(CTempIconLoader(_T("FILE")));
			iImgShareTweaks = piml->Add(CTempIconLoader(_T("SHAREDFILESLIST")));
			iImgPowerShare = piml->Add(CTempIconLoader(_T("FILEPRIORITY")));
			iImgPermission = piml->Add(CTempIconLoader(_T("FRIEND")));
			iImgEmulator = piml->Add(CTempIconLoader(_T("EMULATOR")));
			iImgCreditSystem = piml->Add(CTempIconLoader(_T("CREDIT"))); // EastShare START - Added by Pretender, CS icon
			iImgClientHistory = piml->Add(CTempIconLoader(_T("CLIENTSKNOWN")));
			iImgRemoteSharedFiles = piml->Add(CTempIconLoader(_T("SHAREDFILESLIST")));
			iImgSaveLoad = piml->Add(CTempIconLoader(_T("DATABASE")));
			iImgPrefPink = piml->Add(CTempIconLoader(_T("PREFERENCESPINK")));
			iImgMinimule = piml->Add(CTempIconLoader(_T("CLIENTCOMPATIBLE")));
			iImgDisplay = piml->Add(CTempIconLoader(_T("Display")));
			iImgMETC = piml->Add(CTempIconLoader(_T("HARDDISK")));
			iImgBackup = piml->Add(CTempIconLoader(_T("BACKUP")));
		}

		m_htiUITweaks = m_ctrlTreeOptions.InsertGroup(GetResString(_T("UI_TWEAKS")), iImgUITweaks, TVI_ROOT);
		m_htiUIDarkMode = m_ctrlTreeOptions.InsertGroup(GetResString(_T("DARK_MODE")), iImgUIDarkMode, m_htiUITweaks);
		m_htiUIDarkModeAuto = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DARK_MODE_AUTO")), m_htiUIDarkMode, m_iUIDarkMode == 0);
		m_htiUIDarkModeOn = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DARK_MODE_ON")), m_htiUIDarkMode, m_iUIDarkMode == 1);
		m_htiUIDarkModeOff = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DARK_MODE_OFF")), m_htiUIDarkMode, m_iUIDarkMode == 2);
		m_ctrlTreeOptions.Expand(m_htiUIDarkMode, TVE_EXPAND);
		m_htiUITweaksSpeedGraph = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOW_SPEED_GRAPH")), m_htiUITweaks, m_bUITweaksSpeedGraph);
		m_htiShowDownloadCommandsToolbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOW_DOWNLOAD_COMMANDS_TOOLBAR")), m_htiUITweaks, m_bShowDownloadCommandsToolbar);
		m_htiDisableFindAsYouType = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DISABLE_AS_YOU_TYPE")), m_htiUITweaks, m_bDisableFindAsYouType);
		m_htiUITweaksListUpdatePeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_LIST_UPDATE_PERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaks);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksListUpdatePeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksMaxSortHistory = m_ctrlTreeOptions.InsertGroup(GetResString(_T("UI_TWEAKS_MAX_SORT")), iImgSort, m_htiUITweaks);
		m_htiUITweaksServerMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_SERVER")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksServerMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksSearchMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_SEARCH")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksSearchMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksFilesMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_FILES")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksFilesMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksDownloadMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_DOWNLOAD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksDownloadMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksDownloadClientsMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_DCLIENTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksDownloadClientsMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksUploadMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_UPLOAD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksUploadMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksQueueMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_QUEUE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksQueueMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksClientMaxSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_CLIENT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksClientMaxSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksKadContactSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_KADCONTACT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksKadContactSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUITweaksKadSearchSortHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("UI_TWEAKS_MAX_SORT_KADSEARCH")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUITweaksMaxSortHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiUITweaksKadSearchSortHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiUITweaksMaxSortHistory, TVE_EXPAND);
	
		m_htiIPGeolocation = m_ctrlTreeOptions.InsertGroup(GetResString(_T("IPGEOLOCATION_MAIN")), iImgIPGeolocation, TVI_ROOT);
		m_htiIPGeolocationNameDisable = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IPGEOLOCATION_DISABLED")), m_htiIPGeolocation, m_iIPGeolocationMode == IPGEO_DISABLE);
		m_htiIPGeolocationCountryCode = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IPGEOLOCATION_COUNTRYCODE")), m_htiIPGeolocation, m_iIPGeolocationMode == IPGEO_COUNTRYCODE);
		m_htiIPGeolocationCountry = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IPGEOLOCATION_COUNTRY")), m_htiIPGeolocation, m_iIPGeolocationMode == IPGEO_COUNTRY);
		m_htiIPGeolocationCountryCity = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("IPGEOLOCATION_COUNTRYCITY")), m_htiIPGeolocation, m_iIPGeolocationMode == IPGEO_COUNTRYCITY);
		m_htiIPGeolocationShowFlag = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("IPGEOLOCATION_FLAGS")), m_htiIPGeolocation, m_bIPGeolocationShowFlag);
		m_htiIPGeolocationAutoUpdate = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("AUTO_UPDATE")), m_htiIPGeolocation, m_bIPGeolocationAutoUpdate);
		m_htiIPGeolocationUpdateURL = m_ctrlTreeOptions.InsertItem(GetResString(_T("IPGEOLOCATION_UPDATE_URL")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiIPGeolocation);
		m_ctrlTreeOptions.AddEditBox(m_htiIPGeolocationUpdateURL, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiIPGeolocationPeriodDays = m_ctrlTreeOptions.InsertItem(GetResString(_T("IPGEOLOCATION_PERIOD_DAYS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiIPGeolocation);
		m_ctrlTreeOptions.AddEditBox(m_htiIPGeolocationPeriodDays, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiIPGeolocationUpdateNow = m_ctrlTreeOptions.InsertCommandButton(GetResString(_T("IPGEOLOCATION_UPDATE_NOW")), m_htiIPGeolocation);

		m_htiConTweaks = m_ctrlTreeOptions.InsertGroup(GetResString(_T("CON_TWEAKS")), iImgConTweaks, TVI_ROOT);

		m_htiConnectionChecker = m_ctrlTreeOptions.InsertGroup(GetResString(_T("CONNECTION_CHECK")), iImgConnection, m_htiConTweaks);
		m_htiConnectionCheckerActivate = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CONNECTION_CHECK_ACTIVATE")), m_htiConnectionChecker, m_bConnectionChecker);
		m_htiConnectionCheckerServer = m_ctrlTreeOptions.InsertItem(GetResString(_T("URL_ADDR")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiConnectionChecker);
		m_ctrlTreeOptions.AddEditBox(m_htiConnectionCheckerServer, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_ctrlTreeOptions.Expand(m_htiConnectionChecker, TVE_EXPAND);

		m_htiEnableNatTraversal = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ENABLE_NATT")), m_htiConTweaks, m_bEnableNatTraversal);
		m_htiNatTraversalProtocol = m_ctrlTreeOptions.InsertGroup(GetResString(_T("NATT_TRANSFER_PROTOCOL")), 0, m_htiEnableNatTraversal);
		m_htiNatTraversalProtocolPreferQuic = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NATT_PROTOCOL_PREFER_QUIC")), m_htiNatTraversalProtocol, m_iNatTraversalProtocolMode == NAT_TRAVERSAL_PROTOCOL_PREFER_QUIC);
		m_htiNatTraversalProtocolUtpOnly = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("NATT_PROTOCOL_UTP_ONLY")), m_htiNatTraversalProtocol, m_iNatTraversalProtocolMode == NAT_TRAVERSAL_PROTOCOL_UTP_ONLY);
		m_ctrlTreeOptions.Expand(m_htiNatTraversalProtocol, TVE_EXPAND);
		m_htiNatTraversalPortWindow = m_ctrlTreeOptions.InsertItem(GetResString(_T("NATT_PORT_WINDOW")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiEnableNatTraversal);
		m_ctrlTreeOptions.AddEditBox(m_htiNatTraversalPortWindow, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiNatTraversalSweepWindow = m_ctrlTreeOptions.InsertItem(GetResString(_T("NATT_SWEEP_WINDOW")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiEnableNatTraversal);
		m_ctrlTreeOptions.AddEditBox(m_htiNatTraversalSweepWindow, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiNatTraversalJitterMinMs = m_ctrlTreeOptions.InsertItem(GetResString(_T("NATT_UTP_JITTER_MIN")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiEnableNatTraversal);
		m_ctrlTreeOptions.AddEditBox(m_htiNatTraversalJitterMinMs, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiNatTraversalJitterMaxMs = m_ctrlTreeOptions.InsertItem(GetResString(_T("NATT_UTP_JITTER_MAX")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiEnableNatTraversal);
		m_ctrlTreeOptions.AddEditBox(m_htiNatTraversalJitterMaxMs, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiEnableNatTraversal, TVE_EXPAND);
		UpdateNatTraversalProtocolUi();

		m_htiMaxServedBuddies = m_ctrlTreeOptions.InsertItem(GetResString(_T("KAD_BUDDY_SLOTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiConTweaks);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxServedBuddies, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiMaxEServerBuddySlots = m_ctrlTreeOptions.InsertItem(GetResString(_T("ESERVER_BUDDY_SLOTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiConTweaks);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxEServerBuddySlots, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiUseIntelligentChunkSelection = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("USE_INTELLIGENT_CHUNK_SELECTION")), m_htiConTweaks, m_bUseIntelligentChunkSelection);

		m_htiRetryFailedTcpConnectionAttempts = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RETRY_CONNECTION_ATTEMPTS")), m_htiConTweaks, m_bRetryFailedTcpConnectionAttempts);

		m_htiIsreaskSourceAfterIPChange = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RSAIC")), m_htiConTweaks, m_bIsreaskSourceAfterIPChange);
		m_htiInformQueuedClientsAfterIPChange = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("IQCAOC")), m_htiConTweaks, m_bInformQueuedClientsAfterIPChange);

		m_htiReAskFileSrc = m_ctrlTreeOptions.InsertItem(GetResString(_T("REASK_FILE_SRC")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiConTweaks);
		m_ctrlTreeOptions.AddEditBox(m_htiReAskFileSrc, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiUploadSettings = m_ctrlTreeOptions.InsertGroup(GetResString(_T("UPLOAD_SETTINGS")), iImgUploadSettings, TVI_ROOT);
		m_htiHighBandwidthUploadPolicy = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("HIGH_BANDWIDTH_UPLOAD_POLICY")), m_htiUploadSettings, m_bHighBandwidthUploadPolicy);
		m_htiHighBandwidthTargetUploadClients = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_TARGET_UPLOAD_CLIENTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthTargetUploadClients, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiHighBandwidthUploadSlotElasticPercent = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_UPLOAD_SLOT_ELASTIC_PERCENT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthUploadSlotElasticPercent, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiHighBandwidthSlowUploadThresholdFactor = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_SLOW_UPLOAD_THRESHOLD_FACTOR")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthSlowUploadThresholdFactor, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiHighBandwidthSlowUploadGraceSeconds = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_SLOW_UPLOAD_GRACE_SECONDS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthSlowUploadGraceSeconds, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiHighBandwidthSlowUploadWarmupSeconds = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_SLOW_UPLOAD_WARMUP_SECONDS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthSlowUploadWarmupSeconds, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiHighBandwidthZeroUploadGraceSeconds = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_ZERO_UPLOAD_GRACE_SECONDS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthZeroUploadGraceSeconds, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiHighBandwidthSlowUploadCooldownSeconds = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIGH_BANDWIDTH_SLOW_UPLOAD_COOLDOWN_SECONDS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHighBandwidthUploadPolicy);
		m_ctrlTreeOptions.AddEditBox(m_htiHighBandwidthSlowUploadCooldownSeconds, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiLowRatioQueueScoreBoost = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOW_RATIO_QUEUE_SCORE_BOOST")), m_htiUploadSettings, m_bLowRatioQueueScoreBoost);
		m_htiLowRatioQueueScoreThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("LOW_RATIO_QUEUE_SCORE_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLowRatioQueueScoreBoost);
		m_ctrlTreeOptions.AddEditBox(m_htiLowRatioQueueScoreThreshold, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiLowRatioQueueScoreBonus = m_ctrlTreeOptions.InsertItem(GetResString(_T("LOW_RATIO_QUEUE_SCORE_BONUS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLowRatioQueueScoreBoost);
		m_ctrlTreeOptions.AddEditBox(m_htiLowRatioQueueScoreBonus, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUploadSessionTransferLimit = m_ctrlTreeOptions.InsertGroup(GetResString(_T("UPLOAD_SESSION_TRANSFER_LIMIT")), 0, m_htiUploadSettings);
		m_htiUploadSessionTransferLimitDisabled = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_SESSION_TRANSFER_LIMIT_DISABLED")), m_htiUploadSessionTransferLimit, m_iUploadSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::Disabled));
		m_htiUploadSessionTransferLimitPercent = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_SESSION_TRANSFER_LIMIT_PERCENT")), m_htiUploadSessionTransferLimit, m_iUploadSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::PercentOfFile));
		m_htiUploadSessionTransferLimitPercentValue = m_ctrlTreeOptions.InsertItem(GetResString(_T("UPLOAD_SESSION_TRANSFER_LIMIT_PERCENT_VALUE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUploadSessionTransferLimitPercent);
		m_ctrlTreeOptions.AddEditBox(m_htiUploadSessionTransferLimitPercentValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUploadSessionTransferLimitMiB = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UPLOAD_SESSION_TRANSFER_LIMIT_MIB")), m_htiUploadSessionTransferLimit, m_iUploadSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::AbsoluteMiB));
		m_htiUploadSessionTransferLimitMiBValue = m_ctrlTreeOptions.InsertItem(GetResString(_T("UPLOAD_SESSION_TRANSFER_LIMIT_MIB_VALUE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUploadSessionTransferLimitMiB);
		m_ctrlTreeOptions.AddEditBox(m_htiUploadSessionTransferLimitMiBValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiUploadSessionTimeLimitSeconds = m_ctrlTreeOptions.InsertItem(GetResString(_T("UPLOAD_SESSION_TIME_LIMIT_SECONDS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiUploadSettings);
		m_ctrlTreeOptions.AddEditBox(m_htiUploadSessionTimeLimitSeconds, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiAutoHighBandwidthDownloadBuffer = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("AUTO_HIGH_BANDWIDTH_DOWNLOAD_BUFFER")), m_htiUploadSettings, m_bAutoHighBandwidthDownloadBuffer);
		m_ctrlTreeOptions.Expand(m_htiHighBandwidthUploadPolicy, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiLowRatioQueueScoreBoost, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiUploadSessionTransferLimit, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiUploadSessionTransferLimitPercent, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiUploadSessionTransferLimitMiB, TVE_EXPAND);

		m_htiDownloadValidator = m_ctrlTreeOptions.InsertGroup(GetResString(_T("DOWNLOAD_VALIDATOR")), iImgPrefBlue, TVI_ROOT);
		m_htiDownloadValidatorPassive = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_PASSIVE")), m_htiDownloadValidator, m_iDownloadValidator == 0);
		m_htiDownloadValidatorAlwaysAsk = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_ALWAYS_ASK")), m_htiDownloadValidator, m_iDownloadValidator == 1);
		m_htiDownloadValidatorReject = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT")), m_htiDownloadValidator, m_iDownloadValidator == 2);
		m_htiDownloadValidatorAccept = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_ACCEPT")), m_htiDownloadValidator, m_iDownloadValidator == 3);
		m_htiDownloadValidatorAcceptPercentage = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_ACCEPT_PERCENTAGE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorAccept);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorAcceptPercentage, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorAccept, TVE_EXPAND);
		m_htiDownloadValidatorRejectCanceled = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_CANCELED")), m_htiDownloadValidator, m_bDownloadValidatorRejectCanceled);
		m_htiDownloadValidatorRejectSameHash = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_SAME_HASH")), m_htiDownloadValidator, m_bDownloadValidatorRejectSameHash);
		m_htiDownloadValidatorRejectBlacklisted = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_BLACKLISTED")), m_htiDownloadValidator, m_bDownloadValidatorRejectBlacklisted);
		m_htiDownloadValidatorCaseInsensitive = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_CASE_INSENSITIVE")), m_htiDownloadValidator, m_bDownloadValidatorCaseInsensitive);
		m_htiDownloadValidatorIgnoreExtension = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_IGNORE_EXTENSION")), m_htiDownloadValidator, m_bDownloadValidatorIgnoreExtension);
		m_htiDownloadValidatorIgnoreTags = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_IGNORE_TAGS")), m_htiDownloadValidator, m_bDownloadValidatorIgnoreTags);
		m_htiDownloadValidatorDontIgnoreNumericTags = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DONT_IGNORE_NUMERIC_TAGS")), m_htiDownloadValidator, m_bDownloadValidatorDontIgnoreNumericTags);
		m_htiDownloadValidatorIgnoreNonAlphaNumeric = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_IGNORE_NON_ALPHANUMERIC")), m_htiDownloadValidator, m_bDownloadValidatorIgnoreNonAlphaNumeric);
		m_htiDownloadValidatorMinimumComparisonLength = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_MINIMUM_COMPARISON_LENGTH")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidator);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorMinimumComparisonLength, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorDateTimeMatching = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_MATCHING")), m_htiDownloadValidator, m_bDownloadValidatorDateTimeMatching);
		m_htiDownloadValidatorDateTimeUseYearRange = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_USE_YEAR_RANGE")), m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeUseYearRange);
		m_htiDownloadValidatorDateTimeYearStart = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_START")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorDateTimeUseYearRange);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorDateTimeYearStart, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorDateTimeYearEnd = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_END")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorDateTimeUseYearRange);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorDateTimeYearEnd, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorDateTimeCheckSeconds = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_CHECK_SECONDS")), m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeCheckSeconds);
		m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_INCLUDE_FOLLOWING_NUMERIC_VALUES")),
			m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeIncludeFollowingNumericValues);
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorDateTimeMatching, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorDateTimeUseYearRange, TVE_EXPAND);
		m_htiDownloadValidatorSkipIncompleteFileConfirmation = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_SKIP_INCOMPLETE_CONFIRMATION")), m_htiDownloadValidator, m_bDownloadValidatorSkipIncompleteFileConfirmation);
		m_htiDownloadValidatorMarkAsBlacklisted = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_MARK_AS_BLACKLISTED")), m_htiDownloadValidator, m_bDownloadValidatorMarkAsBlacklisted);
		m_htiDownloadValidatorAutoMarkAsBlacklisted = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_AUTO_MARK_AS_BLACKLISTED")), m_htiDownloadValidator, m_bDownloadValidatorAutoMarkAsBlacklisted);
		
		m_htiDownloadInspector = m_ctrlTreeOptions.InsertGroup(GetResString(_T("DOWNLOAD_INSPECTOR")), iImgInspect, TVI_ROOT);
		m_htiDownloadInspectorDisable = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_INSPECTOR_DISABLE")), m_htiDownloadInspector, m_iDownloadInspector == 0);
		m_htiDownloadInspectorLogOnly = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_INSPECTOR_LOG_ONLY")), m_htiDownloadInspector, m_iDownloadInspector == 1);
		m_htiDownloadInspectorDelete = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_INSPECTOR_DELETE")), m_htiDownloadInspector, m_iDownloadInspector == 2);
		m_htiDownloadInspectorFake = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_INCLUDE_FAKE")), m_htiDownloadInspector, m_bDownloadInspectorFake);
		m_htiDownloadInspectorDRM = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_INCLUDE_DRM")), m_htiDownloadInspector, m_bDownloadInspectorDRM);
		m_htiDownloadInspectorInvalidExt = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REPLACE_INVALID_FILE_EXTENSION")), m_htiDownloadInspector, m_bDownloadInspectorInvalidExt);
		m_htiDownloadInspectorAutoRenameToMajorityName = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME")), m_htiDownloadInspector, m_bDownloadInspectorAutoRenameToMajorityName);
		m_htiDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_NEW_DOWNLOADS_ONLY")), m_htiDownloadInspectorAutoRenameToMajorityName, m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly);
		m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent = m_ctrlTreeOptions.InsertItem(GetResString(_T("PERCENT_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoRenameToMajorityName);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_MINIMUM_VOTES")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoRenameToMajorityName);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoDelete = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE")), m_htiDownloadInspector, m_bDownloadInspectorAutoDeleteEnabled);
		m_htiDownloadInspectorAutoDeleteAddedBefore = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_ADDED_BEFORE")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteAddedBeforeEnabled);
		m_htiDownloadInspectorAutoDeleteAddedBeforeDays = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DAYS_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoDeleteAddedBefore);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoDeleteAddedBeforeDays, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_SEEN_COMPLETE_BEFORE")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled);
		m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DAYS_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoDeleteLastReceivedBefore = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_RECEIVED_BEFORE")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled);
		m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DAYS_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoDeleteLastReceivedBefore);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_PERCENT")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled);
		m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue = m_ctrlTreeOptions.InsertItem(GetResString(_T("PERCENT_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_MB")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled);
		m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_MB_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorAutoDeleteBackupEd2kLinks = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_BACKUP_ED2K")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteBackupEd2kLinks);
		m_htiDownloadInspectorAutoDeleteDontMarkAsCanceled = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DONT_MARK_AS_CANCELED")), m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteDontMarkAsCanceled);
		m_htiDownloadInspectorCheckPeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_CHECK_PERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspector);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorCheckPeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorCompletedThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_DATA_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspector);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorCompletedThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorZeroPercentageThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_ZERO_PERCENTAGE_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspector);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorZeroPercentageThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorCompressionThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_COMPRESSION_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspector);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorCompressionThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadInspectorBypassZeroPercentage = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_INSPECTOR_BYPASS_ZERO_PERCENTAGE")), m_htiDownloadInspector, m_bDownloadInspectorBypassZeroPercentage);
		m_htiDownloadInspectorCompressionThresholdToBypassZero = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_INSPECTOR_COMPRESSION_THRESHOLD_TO_BYPASS_ZERO")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadInspector);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadInspectorCompressionThresholdToBypassZero, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiSearchTweaksGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SEARCH_TWEAKS")), iImgESearch, TVI_ROOT);
		m_htiGroupKnownAtTheBottom = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("GROUP_KNOWN_AT_THE_BOTTOM")), m_htiSearchTweaksGroup, m_bGroupKnownAtTheBottom);
		m_htiShowCloseButtonOnSearchTabs = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOW_CLOSE_BUTTON_ON_SEARCH_TABS")), m_htiSearchTweaksGroup, m_bShowCloseButtonOnSearchTabs);
		m_htiSpamThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("SPAM_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSpamThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiEd2kSearchMaxResults = m_ctrlTreeOptions.InsertItem(GetResString(_T("ED2K_SEARCH_MAX_RESULTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiEd2kSearchMaxResults, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiEd2kSearchMaxMoreRequests = m_ctrlTreeOptions.InsertItem(GetResString(_T("ED2K_SEARCH_MAX_MORE_REQUESTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiEd2kSearchMaxMoreRequests, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiKadFileSearchTotal = m_ctrlTreeOptions.InsertItem(GetResString(_T("KAD_FILE_SEARCH_TOTAL")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiKadFileSearchTotal, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiKadSearchKeywordTotal = m_ctrlTreeOptions.InsertItem(GetResString(_T("KAD_SEARCH_KEYWORD_TOTAL")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiKadSearchKeywordTotal, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiKadFileSearchLifetime = m_ctrlTreeOptions.InsertItem(GetResString(_T("KAD_FILE_SEARCH_LIFETIME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiKadFileSearchLifetime, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiKadSearchKeywordLifetime = m_ctrlTreeOptions.InsertItem(GetResString(_T("KAD_SEARCH_KEYWORD_LIFETIME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchTweaksGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiKadSearchKeywordLifetime, RUNTIME_CLASS(CNumTreeOptionsEdit));
	
		m_htiServerTweaksGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("ESERVER_TWEAKS")), iImgEServer, TVI_ROOT);
		m_htiRepeatServerList = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REPEAT_SERVER_LIST")), m_htiServerTweaksGroup, m_bRepeatServerList);
		m_htiDontRemoveStaticServers = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DONT_REMOVE_STATIC_SERVERS")), m_htiServerTweaksGroup, m_bDontRemoveStaticServers);
		m_htiDontSavePartOnReconnect = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DONT_SAVE_PART_ON_RECONNECT")), m_htiServerTweaksGroup, m_bDontSavePartOnReconnect);

		m_htiFileTweaks = m_ctrlTreeOptions.InsertGroup(GetResString(_T("FILE_TWEAKS")), iImgFileHistory, TVI_ROOT);
		m_htiFileHistoryShowPart = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("FILE_HISTORY_SHOW_PART")), m_htiFileTweaks, m_bFileHistoryShowPart);
		m_htiFileHistoryShowShared = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("FILE_HISTORY_SHOW_SHARED")), m_htiFileTweaks, m_bFileHistoryShowShared);
		m_htiShareTweaks = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SHARE_TWEAKS")), iImgShareTweaks, TVI_ROOT);
		m_htiFileHistoryShowDuplicate = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("FILE_HISTORY_SHOW_DUPLICATE")), m_htiShareTweaks, m_bFileHistoryShowDuplicate);
		m_htiAutoShareSubdirs = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("AUTO_SHARE_SUBDIRS")), m_htiShareTweaks, m_bAutoShareSubdirs);
		m_htiDontShareExtensions = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DONT_SHARE_EXTENSIONS")), m_htiShareTweaks, m_bDontShareExtensions);
		m_htiDontShareExtensionsList = m_ctrlTreeOptions.InsertItem(GetResString(_T("DONT_SHARE_EXTENSIONS_LIST")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDontShareExtensions);
		m_ctrlTreeOptions.AddEditBox(m_htiDontShareExtensionsList, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiSpreadbarSetStatus = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SPREADBAR_DEFAULT_CHECKBOX")), m_htiShareTweaks, m_bSpreadbarSetStatus);
		m_htiHideOvershares = m_ctrlTreeOptions.InsertItem(GetResString(_T("HIDEOVERSHARES")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSpreadbarSetStatus);
		m_ctrlTreeOptions.AddEditBox(m_htiHideOvershares, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSelectiveShare = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SELECTIVESHARE")), m_htiHideOvershares, m_bSelectiveShare);
		m_htiShareOnlyTheNeed = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHAREONLYTHENEED")), m_htiShareTweaks, m_bShareOnlyTheNeed);
		m_htiPowerShareGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("POWERSHARE")), iImgPowerShare, m_htiShareTweaks);
		m_htiPowerShareDisabled = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("POWERSHARE_DISABLED")), m_htiPowerShareGroup, m_iPowerShareMode == 0);
		m_htiPowerShareActivated = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("POWERSHARE_ACTIVATED")), m_htiPowerShareGroup, m_iPowerShareMode == 1);
		m_htiPowerShareAuto = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("POWERSHARE_AUTO")), m_htiPowerShareGroup, m_iPowerShareMode == 2);
		m_htiPowerShareLimited = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("POWERSHARE_LIMITED")), m_htiPowerShareGroup, m_iPowerShareMode == 3);
		m_htiPowerShareLimit = m_ctrlTreeOptions.InsertItem(GetResString(_T("POWERSHARE_LIMIT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiPowerShareLimited);
		m_ctrlTreeOptions.AddEditBox(m_htiPowerShareLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiPowerShareInternalPrio = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("POWERSHARE_INTERPRIO")), m_htiPowerShareGroup, m_bPowerShareInternalPrio);
		m_htiPermissionsGroup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SHARE_PERMISSION_GROUP")), iImgPermission, m_htiShareTweaks);
		m_htiPermissionEverybody = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SHARE_PERMISSION_EVERYBODY")), m_htiPermissionsGroup, m_iSharePermissions == PERM_ALL);
		m_htiPermissionFriendsOnly = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SHARE_PERMISSION_FRIENDSONLY")), m_htiPermissionsGroup, m_iSharePermissions == PERM_FRIENDS);
		m_htiPermissionHidden = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SHARE_PERMISSION_HIDDEN")), m_htiPermissionsGroup, m_iSharePermissions == PERM_NOONE);
		m_htiPermissionColorRows = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHARE_PERMISSION_COLOR_ROWS")), m_htiPermissionsGroup, m_bSharePermissionColorRows);
		m_htiAdjustNTFSDaylightFileTime = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ADJUSTNTFSDAYLIGHTFILETIME")), m_htiFileTweaks, m_bAdjustNTFSDaylightFileTime);
		m_htiAllowDSTTimeTolerance = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ALLOWDSTTIMETOLERANCE")), m_htiFileTweaks, m_bAllowDSTTimeTolerance);
		m_ctrlTreeOptions.Expand(m_htiDontShareExtensions, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiSpreadbarSetStatus, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiHideOvershares, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiPowerShareGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiPowerShareLimited, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiPermissionsGroup, TVE_EXPAND);

		m_htiEmulator = m_ctrlTreeOptions.InsertGroup(GetResString(_T("EMULATOR")), iImgEmulator, TVI_ROOT);
		m_htiEmulateMLDonkey = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EMULATE_MLDONKEY")), m_htiEmulator, m_bEmulateMLDonkey);
		m_htiEmulateEdonkey = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EMULATE_EDONKEY")), m_htiEmulator, m_bEmulateEdonkey);
		m_htiEmulateEdonkeyHybrid = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EMULATE_EDONKEYHYBRID")), m_htiEmulator, m_bEmulateEdonkeyHybrid);
		m_htiEmulateShareaza = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EMULATE_SHAREAZA2")), m_htiEmulator, m_bEmulateShareaza);
		m_htiEmulateLphant = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EMULATE_LPHANT")), m_htiEmulator, m_bEmulateLphant);
		m_htiEmulateCommunity = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("EMULATE_COMMUNITY")), m_htiEmulator, m_bEmulateCommunity);
		m_htiEmulateCommunityTagSavingTreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("EMULATE_COMMUNITY_TAG_SAVING_TRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiEmulateCommunity);
		m_ctrlTreeOptions.AddEditBox(m_htiEmulateCommunityTagSavingTreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiLogEmulator = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOG_EMULATOR")), m_htiEmulator, m_bLogEmulator);
		if (m_htiEmulateCommunity) // show more controls --> still possible to manully expand. 
			m_ctrlTreeOptions.Expand(m_htiEmulateCommunity, TVE_EXPAND);

		m_htiCreditSystem = m_ctrlTreeOptions.InsertGroup(GetResString(_T("CREDIT_SYSTEM")), iImgCreditSystem, TVI_ROOT);
		m_htiOfficialCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("OFFICIAL_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_OFFICIAL);
		m_htiLovelaceCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("LOVELACE_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_LOVELACE);
		m_htiRatioCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("RATIO_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_RATIO);
		m_htiPawcioCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("PAWCIO_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_PAWCIO);
		m_htiESCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("EASTSHARE_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_EASTSHARE);
		m_htiMagicAngelCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("MAGICANGEL_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_MAGICANGEL);
		m_htiMagicAngelPlusCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("MAGICANGEL_PLUS_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_MAGICANGELPLUS);
		m_htiSivkaCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SIVKA_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_SIVKA);
		m_htiSwatCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("SWAT_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_SWAT);
		m_htiTk4Credit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("TK4_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_TK4);
		m_htiXtremeCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("XTREME_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_XTREME);
		m_htiZzulCredit = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("ZZUL_CREDIT")), m_htiCreditSystem, m_iCreditSystem == CS_ZZUL);

		m_htiClientHistory = m_ctrlTreeOptions.InsertGroup(GetResString(_T("CLIENT_HISTORY")), iImgClientHistory, TVI_ROOT);
		m_htiClientHistoryActivate = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ACTIVATE_FEATURE")), m_htiClientHistory, m_bClientHistory);
		m_htiClientHistoryExpDays = m_ctrlTreeOptions.InsertItem(GetResString(_T("CLIENT_HISTORY_EXP_DAYS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiClientHistory);
		m_ctrlTreeOptions.AddEditBox(m_htiClientHistoryExpDays, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiClientHistoryLog = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CLIENT_HISTORY_LOG")), m_htiClientHistory, m_bClientHistoryLog);

		m_htiRemoteSharedFiles = m_ctrlTreeOptions.InsertGroup(GetResString(_T("REMOTE_SF")), iImgRemoteSharedFiles, TVI_ROOT);
		m_htiRemoteSharedFilesUserHash = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REMOTE_SF_USER_HASH")), m_htiRemoteSharedFiles, m_bRemoteSharedFilesUserHash);
		m_htiRemoteSharedFilesClientNote = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REMOTE_SF_CLIENT_NOTE")), m_htiRemoteSharedFiles, m_bRemoteSharedFilesClientNote);
		m_htiRemoteSharedFilesAutoQueryPeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("REMOTE_SF_AUTO_QUERY_PERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiRemoteSharedFiles);
		m_ctrlTreeOptions.AddEditBox(m_htiRemoteSharedFilesAutoQueryPeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRemoteSharedFilesAutoQueryMaxClients = m_ctrlTreeOptions.InsertItem(GetResString(_T("REMOTE_SF_AUTO_MAX_CLIENTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiRemoteSharedFiles);
		m_ctrlTreeOptions.AddEditBox(m_htiRemoteSharedFilesAutoQueryMaxClients, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRemoteSharedFilesAutoQueryClientPeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("REMOTE_SF_AUTO_QUERY_CLIENT_PERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiRemoteSharedFiles);
		m_ctrlTreeOptions.AddEditBox(m_htiRemoteSharedFilesAutoQueryClientPeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRemoteSharedFilesSetAutoQueryDownload = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REMOTE_SF_AUTO_QUERY_DOWNLOAD")), m_htiRemoteSharedFiles, m_bRemoteSharedFilesSetAutoQueryDownload);
		m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("REMOTE_SF_AUTO_QUERY_DOWNLOAD_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiRemoteSharedFilesSetAutoQueryDownload);
		m_ctrlTreeOptions.AddEditBox(m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRemoteSharedFilesSetAutoQueryUpload = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REMOTE_SF_AUTO_QUERY_UPLOAD")), m_htiRemoteSharedFiles, m_bRemoteSharedFilesSetAutoQueryUpload);
		m_htiRemoteSharedFilesSetAutoQueryUploadThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("REMOTE_SF_AUTO_QUERY_UPLOAD_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiRemoteSharedFilesSetAutoQueryUpload);
		m_ctrlTreeOptions.AddEditBox(m_htiRemoteSharedFilesSetAutoQueryUploadThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiRemoteSharedFilesSetAutoQueryDownload, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiRemoteSharedFilesSetAutoQueryUpload, TVE_EXPAND);

		m_htiSaveLoadSources = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SAVE_LOAD_SOURCES")), iImgSaveLoad, TVI_ROOT);
		m_htiSaveLoadSourcesActivate = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ACTIVATE_FEATURE")), m_htiSaveLoadSources, m_bSaveLoadSources);
		m_htiSaveLoadSourcesMaxSources = m_ctrlTreeOptions.InsertItem(GetResString(_T("SAVE_LOAD_SOURCES_MAX_SOURCES")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSaveLoadSources);
		m_ctrlTreeOptions.AddEditBox(m_htiSaveLoadSourcesMaxSources, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSaveLoadSourcesExpirationDays = m_ctrlTreeOptions.InsertItem(GetResString(_T("SAVE_LOAD_SOURCES_EXP_DAYS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSaveLoadSources);
		m_ctrlTreeOptions.AddEditBox(m_htiSaveLoadSourcesExpirationDays, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiMetControl = m_ctrlTreeOptions.InsertGroup(GetResString(_T("MET_FILE_CONTROL")), iImgMETC, TVI_ROOT);
		m_htiDontPurge = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("PURGE_DONT")), m_htiMetControl, m_iPurgeMode == 0);
		m_htiPartiallyPurge = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("PURGE_PARTIALLY")), m_htiMetControl, m_iPurgeMode == 1);
		m_htiCompletelyPurge = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("PURGE_COMPLETLY")), m_htiMetControl, m_iPurgeMode == 2);
		m_htiKnownMet = m_ctrlTreeOptions.InsertItem(GetResString(_T("EXPIRED_KNOWN")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiMetControl);
		m_ctrlTreeOptions.AddEditBox(m_htiKnownMet, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRemoveAichImmediately = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REMOVE_AICH_IMMEDIATELY")), m_htiMetControl, m_bRemoveAichImmediately);
		m_htiClientsExp = m_ctrlTreeOptions.InsertItem(GetResString(_T("EXPIRED_CLIENTS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiMetControl);
		m_ctrlTreeOptions.AddEditBox(m_htiClientsExp, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiBackup = m_ctrlTreeOptions.InsertGroup(GetResString(_T("BACKUP")), iImgBackup, TVI_ROOT);
		m_htiBackupOnExit = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("BACKUP_ON_EXIT")), m_htiBackup, m_bBackupOnExit);
		m_htiBackupPeriodic = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("BACKUP_PERIODIC")), m_htiBackup, m_bBackupPeriodic);
		m_htiBackupPeriod = m_ctrlTreeOptions.InsertItem(GetResString(_T("BACKUP_PERIOD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBackup);
		m_ctrlTreeOptions.AddEditBox(m_htiBackupPeriod, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBackupMax = m_ctrlTreeOptions.InsertItem(GetResString(_T("BACKUP_MAX")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBackup);
		m_ctrlTreeOptions.AddEditBox(m_htiBackupMax, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBackupCompressed = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("BACKUP_COMPRESS")), m_htiBackup, m_bBackupCompressed);

		m_htiAdvancedPreferences = m_ctrlTreeOptions.InsertGroup(GetResString(_T("ADVANCEDPREFS")), iImgPrefPink, TVI_ROOT);
		m_htiMiniMule = m_ctrlTreeOptions.InsertGroup(GetResString(_T("MINIMULE")), iImgMinimule, m_htiAdvancedPreferences);
		m_htiMiniMuleAutoClose = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("MINIMULEAUTOCLOSE")), m_htiMiniMule, bMiniMuleAutoClose);
		m_htiMiniMuleTransparency = m_ctrlTreeOptions.InsertItem(GetResString(_T("MINIMULETRANSPARENCY")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiMiniMule);
		m_ctrlTreeOptions.AddEditBox(m_htiMiniMuleTransparency, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDisplay = m_ctrlTreeOptions.InsertGroup(GetResString(_T("PW_DISPLAY")), iImgDisplay, m_htiAdvancedPreferences);
		m_htiRestoreLastMainWndDlg = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RESTORELASTMAINWNDDLG")), m_htiDisplay, m_bRestoreLastMainWndDlg);
		m_htiRestoreLastLogPane = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RESTORELASTLOGPANE")), m_htiDisplay, m_bRestoreLastLogPane);
		m_htiStraightWindowStyles = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("STRAIGHTWINDOWSTYLES")), m_htiDisplay, m_iStraightWindowStyles);
		m_htiRTLWindowsLayout = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("RTLWINDOWSLAYOUT")), m_htiDisplay, m_bRTLWindowsLayout);
		m_htiShowActiveDownloadsBold = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ACTIVEDOWNLOADSBOLD")), m_htiDisplay, m_bShowActiveDownloadsBold);
		m_htiMaxChatHistory = m_ctrlTreeOptions.InsertItem(GetResString(_T("MAXCHATHISTORY")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDisplay);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxChatHistory, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMaxMsgSessions = m_ctrlTreeOptions.InsertItem(GetResString(_T("MAXMSGSESSIONS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDisplay);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxMsgSessions, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDateTimeFormat = m_ctrlTreeOptions.InsertItem(GetResString(_T("DATETIMEFORMAT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDisplay);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiDateTimeFormat4list = m_ctrlTreeOptions.InsertItem(GetResString(_T("DATETIMEFORMAT4LIST")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDisplay);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat4list, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiShowVerticalHourMarkers = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOWVERTICALHOURMARKERS")), m_htiDisplay, m_bShowVerticalHourMarkers);
		m_htiReBarToolbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REBARTOOLBAR")), m_htiDisplay, m_bReBarToolbar);
		m_htiIconflashOnNewMessage = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ICON_FLASH_ON_NEW_MESSAGE")), m_htiDisplay, m_bIconflashOnNewMessage);
		m_htiShowCopyEd2kLinkCmd = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOWCOPYED2KLINK")), m_htiDisplay, m_bShowCopyEd2kLinkCmd);
		m_htiUpdateQueue = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("UPDATEQUEUE")), m_htiDisplay, m_bUpdateQueue);
		m_htiRepaint = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REPAINTGRAPHS")), m_htiDisplay, m_bRepaint);
		m_htiShowUpDownIconInTaskbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("SHOWUPDOWNICONINTASKBAR")), m_htiDisplay, m_bShowUpDownIconInTaskbar);
		m_htiLog = m_ctrlTreeOptions.InsertGroup(GetResString(_T("SV_LOG")), iImgLog, m_htiAdvancedPreferences);
		m_htiMaxLogBuff = m_ctrlTreeOptions.InsertItem(GetResString(_T("MAXLOGBUFF")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLog);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxLogBuff, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiLogFileFormat = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("LOGFILEFORMAT")), m_htiLog, m_iLogFileFormat);
		m_htiDateTimeFormat4Log = m_ctrlTreeOptions.InsertItem(GetResString(_T("DATETIMEFORMAT4LOG")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLog);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat4Log, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiCreateCrashDump = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("CREATECRASHDUMP")), m_htiAdvancedPreferences, bCreateCrashDump);
		m_htiIgnoreInstances = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("IGNOREINSTANCES")), m_htiAdvancedPreferences, bIgnoreInstances);
		m_htiNotifierMailEncryptCertName = m_ctrlTreeOptions.InsertItem(GetResString(_T("NOTIFIERMAILENCRYPTCERTNAME")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiNotifierMailEncryptCertName, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiPreviewSmallBlocks = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PREVIEWSMALLBLOCKS")), m_htiAdvancedPreferences, m_iPreviewSmallBlocks);
		m_htiPreviewCopiedArchives = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PREVIEWCOPIEDARCHIVES")), m_htiAdvancedPreferences, m_bPreviewCopiedArchives);
		m_htiPreviewOnIconDblClk = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PREVIEWONICONDBLCLK")), m_htiAdvancedPreferences, m_bPreviewOnIconDblClk);
		m_htiInternetSecurityZone = m_ctrlTreeOptions.InsertItem(GetResString(_T("INTERNETSECURITYZONE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiInternetSecurityZone, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiTxtEditor = m_ctrlTreeOptions.InsertItem(GetResString(_T("TXTEDITOR")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddFileEditBox(m_htiTxtEditor, RUNTIME_CLASS(CTreeOptionsEdit), RUNTIME_CLASS(CTreeOptionsBrowseButton));
		m_htiServerUDPPort = m_ctrlTreeOptions.InsertItem(GetResString(_T("SERVERUDPPORT")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiServerUDPPort, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRemoveFilesToBin = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REMOVEFILESTOBIN")), m_htiAdvancedPreferences, m_bRemoveFilesToBin);
		m_htiHighresTimer = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("HIGHRESTIMER")), m_htiAdvancedPreferences, m_bHighresTimer);
		m_htiTrustEveryHash = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("TRUSTEVERYHASH")), m_htiAdvancedPreferences, m_bTrustEveryHash);
		m_htiICH = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("ICH")), m_htiAdvancedPreferences, m_ICH);
		m_htiPreferRestrictedOverUser = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PREFERRESTRICTEDOVERUSER")), m_htiAdvancedPreferences, m_bPreferRestrictedOverUser);
		m_htiUseUserSortedServerList = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("USEUSERSORTEDSERVERLIST")), m_htiAdvancedPreferences, m_bUseUserSortedServerList);
		m_htiWebFileUploadSizeLimitMB = m_ctrlTreeOptions.InsertItem(GetResString(_T("WEBFILEUPLOADSIZELIMITMB")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiWebFileUploadSizeLimitMB, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiAllowedIPs = m_ctrlTreeOptions.InsertItem(GetResString(_T("ALLOWEDIPS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiAllowedIPs, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiDebugSearchResultDetailLevel = m_ctrlTreeOptions.InsertItem(GetResString(_T("DEBUGSEARCHDETAILLEVEL")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiDebugSearchResultDetailLevel, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiCryptTCPPaddingLength = m_ctrlTreeOptions.InsertItem(GetResString(_T("CRYPTTCPPADDINGLENGTH")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiAdvancedPreferences);
		m_ctrlTreeOptions.AddEditBox(m_htiCryptTCPPaddingLength, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDontCompressAvi = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DONTCOMPRESSAVI")), m_htiAdvancedPreferences, m_dontcompressavi);
		m_htiRearrangeKadSearchKeywords = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("REARRANGEKADSEARCH")), m_htiAdvancedPreferences, m_bRearrangeKadSearchKeywords);
		m_htiBeeper = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("PW_BEEP")), m_htiAdvancedPreferences, m_bBeeper);
		m_htiMsgOnlySec = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("MSGONLYSEC")), m_htiAdvancedPreferences, m_bMsgOnlySec);
		m_htiKeepUnavailableFixedSharedDirs = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("KEEPUNAVAILABLEFIXEDSHAREDDIRS")), m_htiAdvancedPreferences, m_bKeepUnavailableFixedSharedDirs);

		if (m_bExtControls) // show more controls --> still possible to manully expand. 
			m_ctrlTreeOptions.Expand(m_htiAdvancedPreferences, TVE_EXPAND);

		m_ctrlTreeOptions.SendMessage(WM_VSCROLL, SB_TOP);
		m_bInitializedTreeOpts = true;
	}

	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiUIDarkMode, m_iUIDarkMode);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiUITweaksSpeedGraph, m_bUITweaksSpeedGraph);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowDownloadCommandsToolbar, m_bShowDownloadCommandsToolbar);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDisableFindAsYouType, m_bDisableFindAsYouType);
	if (m_htiUITweaksListUpdatePeriod) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksListUpdatePeriod, m_iUITweaksListUpdatePeriod);
		DDV_MinMaxInt(pDX, m_iUITweaksListUpdatePeriod, 100, INT_MAX); // Minimum 100ms to prevent performance issues
	}
	if (m_htiUITweaksServerMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksServerMaxSortHistory, m_iUITweaksServerListMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksServerListMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksSearchMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksSearchMaxSortHistory, m_iUITweaksSearchMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksSearchMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksFilesMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksFilesMaxSortHistory, m_iUITweaksFilesMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksFilesMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksDownloadMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksDownloadMaxSortHistory, m_iUITweaksDownloadMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksDownloadMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksDownloadClientsMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksDownloadClientsMaxSortHistory, m_iUITweaksDownloadClientsMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksDownloadClientsMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksUploadMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksUploadMaxSortHistory, m_iUITweaksUploadMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksUploadMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksQueueMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksQueueMaxSortHistory, m_iUITweaksQueueMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksQueueMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksClientMaxSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksClientMaxSortHistory, m_iUITweaksClientMaxSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksClientMaxSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksKadContactSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksKadContactSortHistory, m_iUITweaksKadContactSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksKadContactSortHistory, 0, INT_MAX);
	}
	if (m_htiUITweaksKadSearchSortHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUITweaksKadSearchSortHistory, m_iUITweaksKadSearchSortHistory);
		DDV_MinMaxInt(pDX, m_iUITweaksKadSearchSortHistory, 0, INT_MAX);
	}

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiIPGeolocationShowFlag, m_bIPGeolocationShowFlag);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiIPGeolocationAutoUpdate, m_bIPGeolocationAutoUpdate);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiIPGeolocationUpdateURL, m_sIPGeolocationUpdateURL);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiIPGeolocationPeriodDays, m_iIPGeolocationUpdatePeriodDays);
	DDV_MinMaxInt(pDX, m_iIPGeolocationUpdatePeriodDays, 1, 365);
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiIPGeolocation, m_iIPGeolocationMode);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiConnectionCheckerActivate, m_bConnectionChecker);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiConnectionCheckerServer, m_sConnectionCheckerServer);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEnableNatTraversal, m_bEnableNatTraversal);
	int iNatTraversalProtocolRadio = (m_iNatTraversalProtocolMode == NAT_TRAVERSAL_PROTOCOL_UTP_ONLY) ? 1 : 0;
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiNatTraversalProtocol, iNatTraversalProtocolRadio);
	if (pDX->m_bSaveAndValidate)
		m_iNatTraversalProtocolMode = (iNatTraversalProtocolRadio == 1) ? NAT_TRAVERSAL_PROTOCOL_UTP_ONLY : NAT_TRAVERSAL_PROTOCOL_PREFER_QUIC;
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiNatTraversalPortWindow, m_iNatTraversalPortWindow);
	DDV_MinMaxInt(pDX, m_iNatTraversalPortWindow, 0, 65535);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiNatTraversalSweepWindow, m_iNatTraversalSweepWindow);
	DDV_MinMaxInt(pDX, m_iNatTraversalSweepWindow, 0, 65535);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiNatTraversalJitterMinMs, m_iNatTraversalJitterMinMs);
	DDV_MinMaxInt(pDX, m_iNatTraversalJitterMinMs, 1, 5000);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiNatTraversalJitterMaxMs, m_iNatTraversalJitterMaxMs);
	DDV_MinMaxInt(pDX, m_iNatTraversalJitterMaxMs, 1, 5000);

	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiMaxServedBuddies, m_iMaxServedBuddies);
	DDV_MinMaxInt(pDX, m_iMaxServedBuddies, 5, 100);

	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiMaxEServerBuddySlots, m_iMaxEServerBuddySlots);
	DDV_MinMaxInt(pDX, m_iMaxEServerBuddySlots, ESERVERBUDDY_MIN_SLOTS, ESERVERBUDDY_MAX_SLOTS);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRetryFailedTcpConnectionAttempts, m_bRetryFailedTcpConnectionAttempts);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiIsreaskSourceAfterIPChange, m_bIsreaskSourceAfterIPChange);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiInformQueuedClientsAfterIPChange, m_bInformQueuedClientsAfterIPChange);

	if (m_htiReAskFileSrc) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiReAskFileSrc, m_iReAskFileSrc);
		DDV_MinMaxInt(pDX, m_iReAskFileSrc, (MS2MIN(FILEREASKTIME)), 55);
	}

	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiDownloadValidator, m_iDownloadValidator);
	if (m_htiDownloadValidatorAcceptPercentage) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorAcceptPercentage, m_iDownloadValidatorAcceptPercentage);
		DDV_MinMaxInt(pDX, m_iDownloadValidatorAcceptPercentage, 1, 100);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorRejectCanceled, m_bDownloadValidatorRejectCanceled);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorRejectSameHash, m_bDownloadValidatorRejectSameHash);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorRejectBlacklisted, m_bDownloadValidatorRejectBlacklisted);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorCaseInsensitive, m_bDownloadValidatorCaseInsensitive);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorIgnoreExtension, m_bDownloadValidatorIgnoreExtension);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorIgnoreTags, m_bDownloadValidatorIgnoreTags);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDontIgnoreNumericTags, m_bDownloadValidatorDontIgnoreNumericTags);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorIgnoreNonAlphaNumeric, m_bDownloadValidatorIgnoreNonAlphaNumeric);
	if (m_htiDownloadValidatorMinimumComparisonLength) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorMinimumComparisonLength, m_iDownloadValidatorMinimumComparisonLength);
		DDV_MinMaxInt(pDX, m_iDownloadValidatorMinimumComparisonLength, 4, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeMatching);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDateTimeUseYearRange, m_bDownloadValidatorDateTimeUseYearRange);
	if (m_htiDownloadValidatorDateTimeYearStart) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDateTimeYearStart, m_iDownloadValidatorDateTimeYearStart);
		DDV_MinMaxInt(pDX, m_iDownloadValidatorDateTimeYearStart, 1000, 9999);
	}
	if (m_htiDownloadValidatorDateTimeYearEnd) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDateTimeYearEnd, m_iDownloadValidatorDateTimeYearEnd);
		DDV_MinMaxInt(pDX, m_iDownloadValidatorDateTimeYearEnd, 1000, 9999);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDateTimeCheckSeconds, m_bDownloadValidatorDateTimeCheckSeconds);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, m_bDownloadValidatorDateTimeIncludeFollowingNumericValues);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorSkipIncompleteFileConfirmation, m_bDownloadValidatorSkipIncompleteFileConfirmation);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorMarkAsBlacklisted, m_bDownloadValidatorMarkAsBlacklisted);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadValidatorAutoMarkAsBlacklisted, m_bDownloadValidatorAutoMarkAsBlacklisted);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorFake, m_bDownloadInspectorFake);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorDRM, m_bDownloadInspectorDRM);
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiDownloadInspector, m_iDownloadInspector);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorInvalidExt, m_bDownloadInspectorInvalidExt);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoRenameToMajorityName, m_bDownloadInspectorAutoRenameToMajorityName);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly, m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly);
	if (m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent, m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent, thePrefs.GetMinDownloadInspectorAutoRenameToMajorityNameRequiredPercent(), thePrefs.GetMaxDownloadInspectorAutoRenameToMajorityNameRequiredPercent());
	}
	if (m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes, m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes, 0, thePrefs.GetMaxDownloadInspectorAutoRenameToMajorityNameMinimumVotes());
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDelete, m_bDownloadInspectorAutoDeleteEnabled);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteAddedBefore, m_bDownloadInspectorAutoDeleteAddedBeforeEnabled);
	if (m_htiDownloadInspectorAutoDeleteAddedBeforeDays) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteAddedBeforeDays, m_iDownloadInspectorAutoDeleteAddedBeforeDays);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoDeleteAddedBeforeDays, 1, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore, m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled);
	if (m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays, m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays, 1, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteLastReceivedBefore, m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled);
	if (m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays, m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays, 1, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent, m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled);
	if (m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue, m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent, 0, 100);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb, m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled);
	if (m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue, m_iDownloadInspectorAutoDeleteDownloadedLessThanMb);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorAutoDeleteDownloadedLessThanMb, 0, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteBackupEd2kLinks, m_bDownloadInspectorAutoDeleteBackupEd2kLinks);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorAutoDeleteDontMarkAsCanceled, m_bDownloadInspectorAutoDeleteDontMarkAsCanceled);
	if (m_htiDownloadInspectorCheckPeriod) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorCheckPeriod, m_iDownloadInspectorCheckPeriod);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorCheckPeriod, 5, INT_MAX);
	}
	if (m_htiDownloadInspectorCompletedThreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorCompletedThreshold, m_iDownloadInspectorCompletedThreshold);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorCompletedThreshold, 1, INT_MAX);
	}
	if (m_htiDownloadInspectorZeroPercentageThreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorZeroPercentageThreshold, m_iDownloadInspectorZeroPercentageThreshold);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorZeroPercentageThreshold, 1, 100);
	}
	if (m_htiDownloadInspectorCompressionThreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorCompressionThreshold, m_iDownloadInspectorCompressionThreshold);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorCompressionThreshold, 100, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorBypassZeroPercentage, m_bDownloadInspectorBypassZeroPercentage);
	if (m_htiDownloadInspectorCompressionThresholdToBypassZero) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDownloadInspectorCompressionThresholdToBypassZero, m_iDownloadInspectorCompressionThresholdToBypassZero);
		DDV_MinMaxInt(pDX, m_iDownloadInspectorCompressionThresholdToBypassZero, m_iDownloadInspectorCompressionThreshold+1, INT_MAX);
	}

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiGroupKnownAtTheBottom, m_bGroupKnownAtTheBottom);
	if (m_htiSpamThreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiSpamThreshold, m_iSpamThreshold);
		DDV_MinMaxInt(pDX, m_iSpamThreshold, SEARCH_SPAM_THRESHOLD, INT_MAX);
	}
	if (m_htiEd2kSearchMaxResults) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiEd2kSearchMaxResults, m_iEd2kSearchMaxResults);
		DDV_MinMaxInt(pDX, m_iEd2kSearchMaxResults, CPreferences::GetDefaultEd2kSearchMaxResults(), INT_MAX);
	}
	if (m_htiEd2kSearchMaxMoreRequests) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiEd2kSearchMaxMoreRequests, m_iEd2kSearchMaxMoreRequests);
		DDV_MinMaxInt(pDX, m_iEd2kSearchMaxMoreRequests, CPreferences::GetDefaultEd2kSearchMaxMoreRequests(), INT_MAX);
	}
	if (m_htiKadFileSearchTotal) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiKadFileSearchTotal, m_iKadFileSearchTotal);
		DDV_MinMaxInt(pDX, m_iKadFileSearchTotal, CPreferences::GetMinKadSearchTotal(), CPreferences::GetMaxKadSearchTotal());
	}
	if (m_htiKadSearchKeywordTotal) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiKadSearchKeywordTotal, m_iKadSearchKeywordTotal);
		DDV_MinMaxInt(pDX, m_iKadSearchKeywordTotal, CPreferences::GetMinKadSearchTotal(), CPreferences::GetMaxKadSearchTotal());
	}
	if (m_htiKadFileSearchLifetime) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiKadFileSearchLifetime, m_iKadFileSearchLifetime);
		DDV_MinMaxInt(pDX, m_iKadFileSearchLifetime, CPreferences::GetMinKadSearchLifetime(), CPreferences::GetMaxKadSearchLifetime());
	}
	if (m_htiKadSearchKeywordLifetime) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiKadSearchKeywordLifetime, m_iKadSearchKeywordLifetime);
		DDV_MinMaxInt(pDX, m_iKadSearchKeywordLifetime, CPreferences::GetMinKadSearchLifetime(), CPreferences::GetMaxKadSearchLifetime());
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowCloseButtonOnSearchTabs, m_bShowCloseButtonOnSearchTabs);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRepeatServerList, m_bRepeatServerList);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDontRemoveStaticServers, m_bDontRemoveStaticServers);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDontSavePartOnReconnect, m_bDontSavePartOnReconnect);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowCloseButtonOnSearchTabs, m_bShowCloseButtonOnSearchTabs);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiFileHistoryShowPart, m_bFileHistoryShowPart);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiFileHistoryShowShared, m_bFileHistoryShowShared);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiFileHistoryShowDuplicate, m_bFileHistoryShowDuplicate);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiAutoShareSubdirs, m_bAutoShareSubdirs);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDontShareExtensions, m_bDontShareExtensions);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDontShareExtensionsList, m_sDontShareExtensionsList);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiSpreadbarSetStatus, m_bSpreadbarSetStatus);
	if (m_htiHideOvershares) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHideOvershares, m_iHideOvershares);
		DDV_MinMaxInt(pDX, m_iHideOvershares, 0, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiSelectiveShare, m_bSelectiveShare);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShareOnlyTheNeed, m_bShareOnlyTheNeed);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiHighBandwidthUploadPolicy, m_bHighBandwidthUploadPolicy);
	if (m_htiHighBandwidthTargetUploadClients) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthTargetUploadClients, m_iHighBandwidthTargetUploadClients);
		DDV_MinMaxInt(pDX, m_iHighBandwidthTargetUploadClients, static_cast<int>(thePrefs.GetMinHighBandwidthTargetUploadClients()), static_cast<int>(thePrefs.GetMaxHighBandwidthTargetUploadClients()));
	}
	if (m_htiHighBandwidthUploadSlotElasticPercent) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthUploadSlotElasticPercent, m_iHighBandwidthUploadSlotElasticPercent);
		DDV_MinMaxInt(pDX, m_iHighBandwidthUploadSlotElasticPercent, static_cast<int>(thePrefs.GetMinHighBandwidthUploadSlotElasticPercent()), static_cast<int>(thePrefs.GetMaxHighBandwidthUploadSlotElasticPercent()));
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiAutoHighBandwidthDownloadBuffer, m_bAutoHighBandwidthDownloadBuffer);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiLowRatioQueueScoreBoost, m_bLowRatioQueueScoreBoost);
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiUploadSessionTransferLimit, m_iUploadSessionTransferLimitMode);
	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthSlowUploadThresholdFactor, m_sHighBandwidthSlowUploadThresholdFactor);
	if (m_htiHighBandwidthSlowUploadGraceSeconds) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthSlowUploadGraceSeconds, m_iHighBandwidthSlowUploadGraceSeconds);
		DDV_MinMaxInt(pDX, m_iHighBandwidthSlowUploadGraceSeconds, static_cast<int>(thePrefs.GetMinHighBandwidthSlowUploadGraceSeconds()), static_cast<int>(thePrefs.GetMaxHighBandwidthSlowUploadGraceSeconds()));
	}
	if (m_htiHighBandwidthSlowUploadWarmupSeconds) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthSlowUploadWarmupSeconds, m_iHighBandwidthSlowUploadWarmupSeconds);
		DDV_MinMaxInt(pDX, m_iHighBandwidthSlowUploadWarmupSeconds, static_cast<int>(thePrefs.GetMinHighBandwidthSlowUploadWarmupSeconds()), static_cast<int>(thePrefs.GetMaxHighBandwidthSlowUploadWarmupSeconds()));
	}
	if (m_htiHighBandwidthZeroUploadGraceSeconds) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthZeroUploadGraceSeconds, m_iHighBandwidthZeroUploadGraceSeconds);
		DDV_MinMaxInt(pDX, m_iHighBandwidthZeroUploadGraceSeconds, static_cast<int>(thePrefs.GetMinHighBandwidthZeroUploadGraceSeconds()), static_cast<int>(thePrefs.GetMaxHighBandwidthZeroUploadGraceSeconds()));
	}
	if (m_htiHighBandwidthSlowUploadCooldownSeconds) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiHighBandwidthSlowUploadCooldownSeconds, m_iHighBandwidthSlowUploadCooldownSeconds);
		DDV_MinMaxInt(pDX, m_iHighBandwidthSlowUploadCooldownSeconds, static_cast<int>(thePrefs.GetMinHighBandwidthSlowUploadCooldownSeconds()), static_cast<int>(thePrefs.GetMaxHighBandwidthSlowUploadCooldownSeconds()));
	}
	if (m_htiLowRatioQueueScoreThreshold)
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiLowRatioQueueScoreThreshold, m_sLowRatioQueueScoreThreshold);
	if (m_htiLowRatioQueueScoreBonus) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiLowRatioQueueScoreBonus, m_iLowRatioQueueScoreBonus);
		DDV_MinMaxInt(pDX, m_iLowRatioQueueScoreBonus, static_cast<int>(thePrefs.GetMinLowRatioQueueScoreBonus()), static_cast<int>(thePrefs.GetMaxLowRatioQueueScoreBonus()));
	}
	if (m_htiUploadSessionTransferLimitPercentValue) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUploadSessionTransferLimitPercentValue, m_iUploadSessionTransferLimitPercent);
		DDV_MinMaxInt(pDX, m_iUploadSessionTransferLimitPercent, static_cast<int>(thePrefs.GetMinUploadSessionTransferLimitPercent()), static_cast<int>(thePrefs.GetMaxUploadSessionTransferLimitPercent()));
	}
	if (m_htiUploadSessionTransferLimitMiBValue) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUploadSessionTransferLimitMiBValue, m_iUploadSessionTransferLimitMiB);
		DDV_MinMaxInt(pDX, m_iUploadSessionTransferLimitMiB, static_cast<int>(thePrefs.GetMinUploadSessionTransferLimitMiB()), static_cast<int>(thePrefs.GetMaxUploadSessionTransferLimitMiB()));
	}
	if (m_htiUploadSessionTimeLimitSeconds) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiUploadSessionTimeLimitSeconds, m_iUploadSessionTimeLimitSeconds);
		DDV_MinMaxInt(pDX, m_iUploadSessionTimeLimitSeconds, static_cast<int>(thePrefs.GetMinUploadSessionTimeLimitSeconds()), static_cast<int>(thePrefs.GetMaxUploadSessionTimeLimitSeconds()));
	}
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiPowerShareGroup, m_iPowerShareMode);
	if (m_htiPowerShareLimit) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiPowerShareLimit, m_iPowerShareLimit);
		DDV_MinMaxInt(pDX, m_iPowerShareLimit, 0, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiPowerShareInternalPrio, m_bPowerShareInternalPrio);
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiPermissionsGroup, m_iSharePermissions);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiPermissionColorRows, m_bSharePermissionColorRows);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiAdjustNTFSDaylightFileTime, m_bAdjustNTFSDaylightFileTime);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiAllowDSTTimeTolerance, m_bAllowDSTTimeTolerance);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEmulateMLDonkey, m_bEmulateMLDonkey);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEmulateEdonkey, m_bEmulateEdonkey);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEmulateEdonkeyHybrid, m_bEmulateEdonkeyHybrid);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEmulateShareaza, m_bEmulateShareaza);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEmulateLphant, m_bEmulateLphant);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiEmulateCommunity, m_bEmulateCommunity);
	if (m_htiEmulateCommunityTagSavingTreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiEmulateCommunityTagSavingTreshold, m_iEmulateCommunityTagSavingTreshold);
		DDV_MinMaxInt(pDX, m_iEmulateCommunityTagSavingTreshold, 2, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiLogEmulator, m_bLogEmulator);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiUseIntelligentChunkSelection, m_bUseIntelligentChunkSelection);
	
	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiCreditSystem, m_iCreditSystem);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiClientHistoryActivate, m_bClientHistory);
	if (m_htiClientHistoryExpDays) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiClientHistoryExpDays, m_iClientHistoryExpDays);
		DDV_MinMaxInt(pDX, m_iClientHistoryExpDays, 1, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiClientHistoryLog, m_bClientHistoryLog);
	
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesUserHash, m_bRemoteSharedFilesUserHash);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesClientNote, m_bRemoteSharedFilesClientNote);
	if (m_htiRemoteSharedFilesAutoQueryPeriod) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesAutoQueryPeriod, m_iRemoteSharedFilesAutoQueryPeriod);
		DDV_MinMaxInt(pDX, m_iRemoteSharedFilesAutoQueryPeriod, 1, INT_MAX);
	}
	if (m_htiRemoteSharedFilesAutoQueryMaxClients) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesAutoQueryMaxClients, m_iRemoteSharedFilesAutoQueryMaxClients);
		DDV_MinMaxInt(pDX, m_iRemoteSharedFilesAutoQueryMaxClients, 1, INT_MAX);
	}
	if (m_htiRemoteSharedFilesAutoQueryClientPeriod) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesAutoQueryClientPeriod, m_iRemoteSharedFilesAutoQueryClientPeriod);
		DDV_MinMaxInt(pDX, m_iRemoteSharedFilesAutoQueryClientPeriod, 30, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesSetAutoQueryDownload, m_bRemoteSharedFilesSetAutoQueryDownload);
	if (m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold, m_iRemoteSharedFilesSetAutoQueryDownloadThreshold);
		DDV_MinMaxInt(pDX, m_iRemoteSharedFilesSetAutoQueryDownloadThreshold, 1, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesSetAutoQueryUpload, m_bRemoteSharedFilesSetAutoQueryUpload);
	if (m_htiRemoteSharedFilesSetAutoQueryUploadThreshold) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiRemoteSharedFilesSetAutoQueryUploadThreshold, m_iRemoteSharedFilesSetAutoQueryUploadThreshold);
		DDV_MinMaxInt(pDX, m_iRemoteSharedFilesSetAutoQueryUploadThreshold, 1, INT_MAX);
	}

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiSaveLoadSourcesActivate, m_bSaveLoadSources);
	if (m_htiSaveLoadSourcesMaxSources) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiSaveLoadSourcesMaxSources, m_iSaveLoadSourcesMaxSources);
		DDV_MinMaxInt(pDX, m_iSaveLoadSourcesMaxSources, 1, INT_MAX);
	}
	if (m_htiSaveLoadSourcesExpirationDays) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiSaveLoadSourcesExpirationDays, m_iSaveLoadSourcesExpirationDays);
		DDV_MinMaxInt(pDX, m_iSaveLoadSourcesExpirationDays, 1, INT_MAX);
	}

	DDX_TreeRadio(pDX, IDC_MOD_OPTS, m_htiMetControl, m_iPurgeMode);
	if (m_htiKnownMet) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiKnownMet, m_iKnownMetDays);
		DDV_MinMaxInt(pDX, m_iKnownMetDays, 1, INT_MAX);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRemoveAichImmediately, m_bRemoveAichImmediately);
	if (m_htiClientsExp) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiClientsExp, m_iClientsExpDays);
		DDV_MinMaxInt(pDX, m_iClientsExpDays, 1, INT_MAX);
	}

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiBackupOnExit, m_bBackupOnExit);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiBackupPeriodic, m_bBackupPeriodic);
	if (m_htiBackupPeriod) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiBackupPeriod, m_iBackupPeriod);
		DDV_MinMaxInt(pDX, m_iBackupPeriod, 1, INT_MAX);
	}
	if (m_htiBackupMax) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiBackupMax, m_iBackupMax);
		DDV_MinMaxInt(pDX, m_iBackupMax, 1, 999);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiBackupCompressed, m_bBackupCompressed);

	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiMiniMuleAutoClose, bMiniMuleAutoClose);
	if (m_htiMiniMuleTransparency) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiMiniMuleTransparency, iMiniMuleTransparency);
		DDV_MinMaxInt(pDX, iMiniMuleTransparency, 0, 100);
	}
	if (m_htiRestoreLastMainWndDlg) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRestoreLastMainWndDlg, m_bRestoreLastMainWndDlg);
	if (m_htiRestoreLastLogPane) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRestoreLastLogPane, m_bRestoreLastLogPane);
	if (m_htiStraightWindowStyles) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiStraightWindowStyles, m_iStraightWindowStyles);
	if (m_htiRTLWindowsLayout) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRTLWindowsLayout, m_bRTLWindowsLayout);
	if (m_htiShowActiveDownloadsBold) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowActiveDownloadsBold, m_bShowActiveDownloadsBold);
	if (m_htiMaxChatHistory) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiMaxChatHistory, m_iMaxChatHistory);
		DDV_MinMaxInt(pDX, m_iMaxChatHistory, 3, 2048);
	}
	if (m_htiMaxMsgSessions) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiMaxMsgSessions, m_umaxmsgsessions);
		DDV_MinMaxInt(pDX, m_umaxmsgsessions, 0, 6000);
	}
	DDX_Text(pDX, IDC_MOD_OPTS, m_htiDateTimeFormat, m_strDateTimeFormat);
	DDX_Text(pDX, IDC_MOD_OPTS, m_htiDateTimeFormat4list, m_strDateTimeFormat4List);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowVerticalHourMarkers, m_bShowVerticalHourMarkers);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiReBarToolbar, m_bReBarToolbar);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiIconflashOnNewMessage, m_bIconflashOnNewMessage);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowCopyEd2kLinkCmd, m_bShowCopyEd2kLinkCmd);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiUpdateQueue, m_bUpdateQueue);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRepaint, m_bRepaint);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiShowUpDownIconInTaskbar, m_bShowUpDownIconInTaskbar);
	if (m_htiMaxLogBuff) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiMaxLogBuff, iMaxLogBuff);
		DDV_MinMaxInt(pDX, iMaxLogBuff, 64, 512);
	}
	if (m_htiLogFileFormat) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiLogFileFormat, m_iLogFileFormat);
	DDX_Text(pDX, IDC_MOD_OPTS, m_htiDateTimeFormat4Log, m_strDateTimeFormat4Log);
	if (m_htiCreateCrashDump) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiCreateCrashDump, bCreateCrashDump);
	if (m_htiIgnoreInstances) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiIgnoreInstances, bIgnoreInstances);
	if (m_htiNotifierMailEncryptCertName) DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiNotifierMailEncryptCertName, sNotifierMailEncryptCertName);
	if (m_htiPreviewSmallBlocks) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiPreviewSmallBlocks, m_iPreviewSmallBlocks);
	if (m_htiPreviewCopiedArchives) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiPreviewCopiedArchives, m_bPreviewCopiedArchives);
	if (m_htiPreviewOnIconDblClk) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiPreviewOnIconDblClk, m_bPreviewOnIconDblClk);
	if (m_htiInternetSecurityZone) { DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiInternetSecurityZone, sInternetSecurityZone); }
	//TODO only allow  Untrusted|Internet|Intranet|Trusted|LocalMachine 
	if (m_htiTxtEditor)	DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiTxtEditor, sTxtEditor);
	if (m_htiServerUDPPort) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiServerUDPPort, iServerUDPPort);
		DDV_MinMaxInt(pDX, iServerUDPPort, 0, 65535);
	}
	if (m_htiRemoveFilesToBin) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRemoveFilesToBin, m_bRemoveFilesToBin);
	if (m_htiHighresTimer) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiHighresTimer, m_bHighresTimer);
	if (m_htiTrustEveryHash) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiTrustEveryHash, m_bTrustEveryHash);
	if (m_htiPreferRestrictedOverUser) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiPreferRestrictedOverUser, m_bPreferRestrictedOverUser);
	if (m_htiUseUserSortedServerList) DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiUseUserSortedServerList, m_bUseUserSortedServerList);
	if (m_htiWebFileUploadSizeLimitMB) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiWebFileUploadSizeLimitMB, m_iWebFileUploadSizeLimitMB);
		DDV_MinMaxInt(pDX, m_iWebFileUploadSizeLimitMB, 0, INT_MAX);
	}
	if (m_htiAllowedIPs) DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiAllowedIPs, m_sAllowedIPs); //TODO: check string for ip
	if (m_htiDebugSearchResultDetailLevel) DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiDebugSearchResultDetailLevel, m_iDebugSearchResultDetailLevel); //TODO: check string for ip
	if (m_htiCryptTCPPaddingLength) {
		DDX_TreeEdit(pDX, IDC_MOD_OPTS, m_htiCryptTCPPaddingLength, m_iCryptTCPPaddingLength);
		DDV_MinMaxInt(pDX, m_iCryptTCPPaddingLength, 1, 256);
	}
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiICH, m_ICH);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiDontCompressAvi, m_dontcompressavi);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiRearrangeKadSearchKeywords, m_bRearrangeKadSearchKeywords);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiBeeper, m_bBeeper);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiMsgOnlySec, m_bMsgOnlySec);
	DDX_TreeCheck(pDX, IDC_MOD_OPTS, m_htiKeepUnavailableFixedSharedDirs, m_bKeepUnavailableFixedSharedDirs);
}

BOOL CPPgMod::OnInitDialog()
{	
	m_iUIDarkMode = thePrefs.GetUIDarkMode();
	m_bUITweaksSpeedGraph = thePrefs.GetUITweaksSpeedGraph();
	m_bShowDownloadCommandsToolbar = thePrefs.IsDownloadToolbarEnabled();
	m_bDisableFindAsYouType = thePrefs.m_bDisableFindAsYouType;
	m_iUITweaksListUpdatePeriod = thePrefs.GetUITweaksListUpdatePeriod();
	m_iUITweaksServerListMaxSortHistory = theApp.emuledlg->serverwnd->serverlistctrl.GetMaxSortHistory();
	m_iUITweaksSearchMaxSortHistory = theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.GetMaxSortHistory();
	m_iUITweaksFilesMaxSortHistory = theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetMaxSortHistory();
	m_iUITweaksDownloadMaxSortHistory = theApp.emuledlg->transferwnd->m_pwndTransfer->downloadlistctrl.GetMaxSortHistory();
	m_iUITweaksDownloadClientsMaxSortHistory = theApp.emuledlg->transferwnd->m_pwndTransfer->downloadclientsctrl.GetMaxSortHistory();
	m_iUITweaksUploadMaxSortHistory = theApp.emuledlg->transferwnd->m_pwndTransfer->uploadlistctrl.GetMaxSortHistory();
	m_iUITweaksQueueMaxSortHistory = theApp.emuledlg->transferwnd->m_pwndTransfer->queuelistctrl.GetMaxSortHistory();
	m_iUITweaksClientMaxSortHistory = theApp.emuledlg->transferwnd->m_pwndTransfer->clientlistctrl.GetMaxSortHistory();
	m_iUITweaksKadContactSortHistory = theApp.emuledlg->kademliawnd->m_contactListCtrl->GetMaxSortHistory();
	m_iUITweaksKadSearchSortHistory = theApp.emuledlg->kademliawnd->searchList->GetMaxSortHistory();

	m_iIPGeolocationMode = thePrefs.GetIPGeolocationMode();
	m_bIPGeolocationShowFlag = thePrefs.GetIPGeolocationShowFlag();
	m_bIPGeolocationAutoUpdate = thePrefs.GetAutoIPGeolocationUpdate();
	m_sIPGeolocationUpdateURL = thePrefs.GetIPGeolocationUpdateURL();
	m_iIPGeolocationUpdatePeriodDays = thePrefs.GetIPGeolocationUpdatePeriodDays();

	m_bConnectionChecker = thePrefs.GetConnectionChecker();
	m_sConnectionCheckerServer = thePrefs.GetConnectionCheckerServer();

	m_bEnableNatTraversal = thePrefs.m_bEnableNatTraversal;
	m_iNatTraversalProtocolMode = (int)thePrefs.GetNatTraversalProtocolMode();
	m_iNatTraversalPortWindow = thePrefs.GetNatTraversalPortWindow();
	m_iNatTraversalSweepWindow = thePrefs.GetNatTraversalSweepWindow();
	m_iNatTraversalJitterMinMs = (int)thePrefs.GetNatTraversalJitterMinMs();
	m_iNatTraversalJitterMaxMs = (int)thePrefs.GetNatTraversalJitterMaxMs();

	m_iMaxServedBuddies = (int)thePrefs.GetMaxServedBuddies();

	m_bRetryFailedTcpConnectionAttempts = thePrefs.m_bRetryFailedTcpConnectionAttempts;

	m_bIsreaskSourceAfterIPChange = thePrefs.IsRASAIC();
	m_bInformQueuedClientsAfterIPChange = thePrefs.IsIQCAOC();

	m_iReAskFileSrc = MS2MIN(thePrefs.GetReAskTimeDif() + FILEREASKTIME);

	m_iDownloadValidator = thePrefs.GetDownloadValidator();
	m_iDownloadValidatorAcceptPercentage = thePrefs.GetDownloadValidatorAcceptPercentage();
	m_bDownloadValidatorRejectCanceled = thePrefs.GetDownloadValidatorRejectCanceled();
	m_bDownloadValidatorRejectSameHash = thePrefs.GetDownloadValidatorRejectSameHash();
	m_bDownloadValidatorRejectBlacklisted = thePrefs.GetDownloadValidatorRejectBlacklisted();
	m_bDownloadValidatorCaseInsensitive = thePrefs.GetDownloadValidatorCaseInsensitive();
	m_bDownloadValidatorIgnoreExtension = thePrefs.GetDownloadValidatorIgnoreExtension();
	m_bDownloadValidatorIgnoreTags = thePrefs.GetDownloadValidatorIgnoreTags();
	m_bDownloadValidatorDontIgnoreNumericTags = thePrefs.GetDownloadValidatorDontIgnoreNumericTags();
	m_bDownloadValidatorIgnoreNonAlphaNumeric = thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric();
	m_iDownloadValidatorMinimumComparisonLength = thePrefs.GetDownloadValidatorMinimumComparisonLength();
	m_bDownloadValidatorSkipIncompleteFileConfirmation = thePrefs.GetDownloadValidatorSkipIncompleteFileConfirmation();
	m_bDownloadValidatorMarkAsBlacklisted = thePrefs.GetDownloadValidatorMarkAsBlacklisted();
	m_bDownloadValidatorAutoMarkAsBlacklisted = thePrefs.GetDownloadValidatorAutoMarkAsBlacklisted();
	m_bDownloadValidatorDateTimeMatching = thePrefs.GetDownloadValidatorDateTimeMatching();
	m_bDownloadValidatorDateTimeUseYearRange = thePrefs.GetDownloadValidatorDateTimeUseYearRange();
	m_iDownloadValidatorDateTimeYearStart = thePrefs.GetDownloadValidatorDateTimeYearStart();
	m_iDownloadValidatorDateTimeYearEnd = thePrefs.GetDownloadValidatorDateTimeYearEnd();
	m_bDownloadValidatorDateTimeCheckSeconds = thePrefs.GetDownloadValidatorDateTimeCheckSeconds();
	m_bDownloadValidatorDateTimeIncludeFollowingNumericValues = thePrefs.GetDownloadValidatorDateTimeIncludeFollowingNumericValues();

	m_iDownloadInspector = thePrefs.GetDownloadInspector();
	m_bDownloadInspectorFake = thePrefs.GetDownloadInspectorFake();
	m_bDownloadInspectorDRM = thePrefs.GetDownloadInspectorDRM();
	m_bDownloadInspectorInvalidExt = thePrefs.m_bDownloadInspectorInvalidExt;
	m_bDownloadInspectorAutoRenameToMajorityName = thePrefs.IsDownloadInspectorAutoRenameToMajorityName();
	m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly = thePrefs.IsDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly();
	m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent = thePrefs.GetDownloadInspectorAutoRenameToMajorityNameRequiredPercent();
	m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes = thePrefs.GetDownloadInspectorAutoRenameToMajorityNameMinimumVotes();
	m_bDownloadInspectorAutoDeleteEnabled = thePrefs.IsDownloadInspectorAutoDeleteEnabled();
	m_bDownloadInspectorAutoDeleteAddedBeforeEnabled = thePrefs.IsDownloadInspectorAutoDeleteAddedBeforeEnabled();
	m_iDownloadInspectorAutoDeleteAddedBeforeDays = thePrefs.GetDownloadInspectorAutoDeleteAddedBeforeDays();
	m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled = thePrefs.IsDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled();
	m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays = thePrefs.GetDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays();
	m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled = thePrefs.IsDownloadInspectorAutoDeleteLastReceivedBeforeEnabled();
	m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays = thePrefs.GetDownloadInspectorAutoDeleteLastReceivedBeforeDays();
	m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled = thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled();
	m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent = thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanPercent();
	m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled = thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled();
	m_iDownloadInspectorAutoDeleteDownloadedLessThanMb = thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanMb();
	m_bDownloadInspectorAutoDeleteBackupEd2kLinks = thePrefs.IsDownloadInspectorAutoDeleteBackupEd2kLinksEnabled();
	m_bDownloadInspectorAutoDeleteDontMarkAsCanceled = thePrefs.IsDownloadInspectorAutoDeleteDontMarkAsCanceledEnabled();
	m_iDownloadInspectorCheckPeriod = thePrefs.GetDownloadInspectorCheckPeriod();
	m_iDownloadInspectorCompletedThreshold = thePrefs.GetDownloadInspectorCompletedThreshold();
	m_iDownloadInspectorZeroPercentageThreshold = thePrefs.GetDownloadInspectorZeroPercentageThreshold();
	m_iDownloadInspectorCompressionThreshold = thePrefs.GetDownloadInspectorCompressionThreshold();
	m_bDownloadInspectorBypassZeroPercentage = thePrefs.GetDownloadInspectorBypassZeroPercentage();
	m_iDownloadInspectorCompressionThresholdToBypassZero = thePrefs.GetDownloadInspectorCompressionThresholdToBypassZero();

	m_bGroupKnownAtTheBottom = thePrefs.GetGroupKnownAtTheBottom();
	m_iSpamThreshold = thePrefs.GetSpamThreshold();
	m_iEd2kSearchMaxResults = thePrefs.GetEd2kSearchMaxResults();
	m_iEd2kSearchMaxMoreRequests = thePrefs.GetEd2kSearchMaxMoreRequests();
	m_iKadFileSearchTotal = thePrefs.GetKadFileSearchTotal();
	m_iKadSearchKeywordTotal = thePrefs.GetKadSearchKeywordTotal();
	m_iKadFileSearchLifetime = thePrefs.GetKadFileSearchLifetime();
	m_iKadSearchKeywordLifetime = thePrefs.GetKadSearchKeywordLifetime();
	m_bShowCloseButtonOnSearchTabs = thePrefs.GetShowCloseButtonOnSearchTabs();

	m_bRepeatServerList = thePrefs.GetRepeatServerList();
	m_bDontRemoveStaticServers = thePrefs.GetDontRemoveStaticServers();
	m_bDontSavePartOnReconnect = thePrefs.GetDontSavePartOnReconnect();

	m_bFileHistoryShowPart = thePrefs.GetFileHistoryShowPart();
	m_bFileHistoryShowShared = thePrefs.GetFileHistoryShowShared();
	m_bFileHistoryShowDuplicate = thePrefs.GetFileHistoryShowDuplicate();
	m_bAutoShareSubdirs = thePrefs.GetAutoShareSubdirs();
	m_bDontShareExtensions = thePrefs.GetDontShareExtensions();
	m_sDontShareExtensionsList = thePrefs.GetDontShareExtensionsList();
	m_bSpreadbarSetStatus = thePrefs.GetSpreadbarSetStatus();
	m_iHideOvershares = thePrefs.GetHideOvershares();
	m_bSelectiveShare = thePrefs.IsSelectiveShareEnabled();
	m_bShareOnlyTheNeed = thePrefs.GetShareOnlyTheNeed();
	m_iPowerShareMode = thePrefs.GetPowerShareMode();
	m_bPowerShareInternalPrio = thePrefs.IsPowerShareInternalPrioEnabled();
	m_iPowerShareLimit = thePrefs.GetPowerShareLimit();
	m_iSharePermissions = thePrefs.GetSharePermissions();
	m_bSharePermissionColorRows = thePrefs.GetSharePermissionColorRows();
	m_bHighBandwidthUploadPolicy = thePrefs.IsHighBandwidthUploadPolicyEnabled();
	m_iHighBandwidthTargetUploadClients = static_cast<int>(thePrefs.GetHighBandwidthTargetUploadClients());
	m_iHighBandwidthUploadSlotElasticPercent = static_cast<int>(thePrefs.GetHighBandwidthUploadSlotElasticPercent());
	m_bAutoHighBandwidthDownloadBuffer = thePrefs.IsAutoHighBandwidthDownloadBufferEnabled();
	m_sHighBandwidthSlowUploadThresholdFactor.Format(_T("%.2f"), thePrefs.GetHighBandwidthSlowUploadThresholdFactor());
	m_iHighBandwidthSlowUploadGraceSeconds = static_cast<int>(thePrefs.GetHighBandwidthSlowUploadGraceSeconds());
	m_iHighBandwidthSlowUploadWarmupSeconds = static_cast<int>(thePrefs.GetHighBandwidthSlowUploadWarmupSeconds());
	m_iHighBandwidthZeroUploadGraceSeconds = static_cast<int>(thePrefs.GetHighBandwidthZeroUploadGraceSeconds());
	m_iHighBandwidthSlowUploadCooldownSeconds = static_cast<int>(thePrefs.GetHighBandwidthSlowUploadCooldownSeconds());
	m_bLowRatioQueueScoreBoost = thePrefs.IsLowRatioQueueScoreBoostEnabled();
	m_sLowRatioQueueScoreThreshold.Format(_T("%.2f"), thePrefs.GetLowRatioQueueScoreThreshold());
	m_iLowRatioQueueScoreBonus = static_cast<int>(thePrefs.GetLowRatioQueueScoreBonus());
	m_iUploadSessionTransferLimitMode = static_cast<int>(thePrefs.GetUploadSessionTransferLimitMode());
	m_iUploadSessionTransferLimitPercent = static_cast<int>(thePrefs.GetUploadSessionTransferLimitPercent());
	m_iUploadSessionTransferLimitMiB = static_cast<int>(thePrefs.GetUploadSessionTransferLimitMiB());
	m_iUploadSessionTimeLimitSeconds = static_cast<int>(thePrefs.GetUploadSessionTimeLimitSeconds());
	m_bAdjustNTFSDaylightFileTime = thePrefs.GetAdjustNTFSDaylightFileTime(); // Official preference
	m_bAllowDSTTimeTolerance = thePrefs.GetAllowDSTTimeTolerance();

	m_bEmulateMLDonkey = thePrefs.m_bEmulateMLDonkey;
	m_bEmulateEdonkey = thePrefs.m_bEmulateEdonkey;
	m_bEmulateEdonkeyHybrid = thePrefs.m_bEmulateEdonkeyHybrid;
	m_bEmulateShareaza = thePrefs.m_bEmulateShareaza;
	m_bEmulateLphant = thePrefs.m_bEmulateLphant;
	m_bEmulateCommunity = thePrefs.m_bEmulateCommunity;
	m_iEmulateCommunityTagSavingTreshold = thePrefs.m_iEmulateCommunityTagSavingTreshold;
	m_bLogEmulator = thePrefs.m_bLogEmulator;

	m_bUseIntelligentChunkSelection = thePrefs.m_bUseIntelligentChunkSelection;
	
	m_iCreditSystem = thePrefs.GetCreditSystem();

	m_bClientHistory = thePrefs.GetClientHistory();
	m_iClientHistoryExpDays = thePrefs.GetClientHistoryExpDays();
	m_bClientHistoryLog = thePrefs.GetClientHistoryLog();

	m_bRemoteSharedFilesUserHash = thePrefs.GetRemoteSharedFilesUserHash();
	m_bRemoteSharedFilesClientNote = thePrefs.GetRemoteSharedFilesClientNote();
	m_iRemoteSharedFilesAutoQueryPeriod = thePrefs.GetRemoteSharedFilesAutoQueryPeriod();
	m_iRemoteSharedFilesAutoQueryMaxClients = thePrefs.GetRemoteSharedFilesAutoQueryMaxClients();
	m_iRemoteSharedFilesAutoQueryClientPeriod = thePrefs.GetRemoteSharedFilesAutoQueryClientPeriod();
	m_bRemoteSharedFilesSetAutoQueryDownload = thePrefs.GetRemoteSharedFilesSetAutoQueryDownload();
	m_iRemoteSharedFilesSetAutoQueryDownloadThreshold = thePrefs.GetRemoteSharedFilesSetAutoQueryDownloadThreshold();
	m_bRemoteSharedFilesSetAutoQueryUpload = thePrefs.GetRemoteSharedFilesSetAutoQueryUpload();
	m_iRemoteSharedFilesSetAutoQueryUploadThreshold = thePrefs.GetRemoteSharedFilesSetAutoQueryUploadThreshold();

	m_bSaveLoadSources = thePrefs.GetSaveLoadSources();
	m_iSaveLoadSourcesMaxSources = thePrefs.GetSaveLoadSourcesMaxSources();
	m_iSaveLoadSourcesExpirationDays = thePrefs.GetSaveLoadSourcesExpirationDays();

	m_iKnownMetDays = thePrefs.GetKnownMetDays();
	m_bRemoveAichImmediately = thePrefs.GetRemoveAichImmediately();
	m_iClientsExpDays = thePrefs.GetClientsExpDays();

	m_bBackupOnExit = thePrefs.GetBackupOnExit();
	m_bBackupPeriodic = thePrefs.GetBackupPeriodic();
	m_iBackupPeriod = thePrefs.GetBackupPeriod();
	m_iBackupMax = thePrefs.GetBackupMax();
	m_bBackupCompressed = thePrefs.GetBackupCompressed();

	bMiniMuleAutoClose = thePrefs.bMiniMuleAutoClose;
	iMiniMuleTransparency = thePrefs.iMiniMuleTransparency;
	m_bRestoreLastMainWndDlg = thePrefs.m_bRestoreLastMainWndDlg;
	m_bRestoreLastLogPane = thePrefs.m_bRestoreLastLogPane;
	m_iStraightWindowStyles = thePrefs.m_iStraightWindowStyles;
	m_bRTLWindowsLayout = thePrefs.m_bRTLWindowsLayout;
	m_bShowActiveDownloadsBold = thePrefs.m_bShowActiveDownloadsBold;
	m_iMaxChatHistory = thePrefs.m_iMaxChatHistory;
	m_umaxmsgsessions = thePrefs.maxmsgsessions;
	m_strDateTimeFormat = thePrefs.m_strDateTimeFormat;
	m_strDateTimeFormat4List = thePrefs.m_strDateTimeFormat4Lists;
	m_bShowVerticalHourMarkers = thePrefs.m_bShowVerticalHourMarkers;
	m_bReBarToolbar = !thePrefs.m_bReBarToolbar;
	m_bIconflashOnNewMessage = thePrefs.m_bIconflashOnNewMessage;
	m_bShowCopyEd2kLinkCmd = thePrefs.m_bShowCopyEd2kLinkCmd;
	m_bUpdateQueue = !thePrefs.m_bupdatequeuelist;
	m_bRepaint = thePrefs.IsGraphRecreateDisabled();
	m_bShowUpDownIconInTaskbar = thePrefs.IsShowUpDownIconInTaskbar();

	iMaxLogBuff = thePrefs.GetMaxLogBuff() / 1024;
	m_iLogFileFormat = thePrefs.m_iLogFileFormat;
	m_strDateTimeFormat4Log = thePrefs.m_strDateTimeFormat4Log;

	bCreateCrashDump = thePrefs.bCreateCrashDump;
	bIgnoreInstances = thePrefs.bIgnoreInstances;
	sNotifierMailEncryptCertName = thePrefs.sNotifierMailEncryptCertName;
	m_iPreviewSmallBlocks = thePrefs.m_iPreviewSmallBlocks;
	m_bPreviewCopiedArchives = thePrefs.m_bPreviewCopiedArchives;
	m_bPreviewOnIconDblClk = thePrefs.m_bPreviewOnIconDblClk;
	sInternetSecurityZone = thePrefs.sInternetSecurityZone;
	sTxtEditor = thePrefs.GetTxtEditor();
	iServerUDPPort = thePrefs.GetServerUDPPort();
	m_bRemoveFilesToBin = thePrefs.GetRemoveToBin();
	m_bHighresTimer = thePrefs.m_bHighresTimer;
	m_bTrustEveryHash = thePrefs.m_bTrustEveryHash;
	m_bPreferRestrictedOverUser = thePrefs.m_bPreferRestrictedOverUser;
	m_bUseUserSortedServerList = thePrefs.m_bUseUserSortedServerList;
	m_iWebFileUploadSizeLimitMB = thePrefs.m_iWebFileUploadSizeLimitMB;
	m_iMaxServedBuddies = thePrefs.m_uMaxServedBuddies;
	m_iMaxEServerBuddySlots = thePrefs.GetMaxEServerBuddySlots();
	m_sAllowedIPs = EMPTY;
	if (thePrefs.GetAllowedRemoteAccessIPs().GetCount() > 0)
		for (int i = 0; i < thePrefs.GetAllowedRemoteAccessIPs().GetCount(); i++)
			m_sAllowedIPs = m_sAllowedIPs + _T(";") + ipstr(thePrefs.GetAllowedRemoteAccessIPs()[i]);
	m_iDebugSearchResultDetailLevel = thePrefs.GetDebugSearchResultDetailLevel();
	m_iCryptTCPPaddingLength = thePrefs.GetCryptTCPPaddingLength();
	m_ICH = thePrefs.ICH;
	m_dontcompressavi = thePrefs.dontcompressavi;
	m_bRearrangeKadSearchKeywords = thePrefs.GetRearrangeKadSearchKeywords();
	m_bBeeper = thePrefs.beepOnError;
	m_bMsgOnlySec = thePrefs.msgsecure;
	m_bKeepUnavailableFixedSharedDirs = thePrefs.m_bKeepUnavailableFixedSharedDirs;

	m_ctrlTreeOptions.SetImageListColorFlags(theApp.m_iDfltImageListColorFlags);
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	Localize();
	UpdateDownloadValidatorDateTimeUi();
	UpdateLayout();

	return TRUE;  // return TRUE unless you set the focus to the control. EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgMod::UpdateLayout()
{
	CWnd* pTree = GetDlgItem(IDC_MOD_OPTS);
	CWnd* pInfo = GetDlgItem(IDC_MOD_OPTS_INFO);
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

void CPPgMod::OnSize(UINT nType, int cx, int cy)
{
	CPropertyPage::OnSize(nType, cx, cy);
	UpdateLayout();
}

BOOL CPPgMod::OnKillActive()
{
	// if prop page is closed by pressing ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	return CPropertyPage::OnKillActive();
}

BOOL CPPgMod::OnApply()
{
	// if prop page is closed by pressing ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();

	if (!UpdateData())
		return FALSE;

	float fHighBandwidthSlowUploadThresholdFactor = 0.0f;
	if (!ParseFloatOptionText(m_sHighBandwidthSlowUploadThresholdFactor, fHighBandwidthSlowUploadThresholdFactor)) {
		m_ctrlTreeOptions.SelectItem(m_htiHighBandwidthSlowUploadThresholdFactor);
		CDarkMode::MessageBox(AFX_IDP_PARSE_REAL);
		return FALSE;
	}

	float fLowRatioQueueScoreThreshold = 0.0f;
	if (!ParseFloatOptionText(m_sLowRatioQueueScoreThreshold, fLowRatioQueueScoreThreshold)) {
		m_ctrlTreeOptions.SelectItem(m_htiLowRatioQueueScoreThreshold);
		CDarkMode::MessageBox(AFX_IDP_PARSE_REAL);
		return FALSE;
	}

	if (m_bUITweaksSpeedGraph != thePrefs.GetUITweaksSpeedGraph()) {
		thePrefs.SetUITweaksSpeedGraph(m_bUITweaksSpeedGraph);
		theApp.emuledlg->ShowSpeedGraph(m_bUITweaksSpeedGraph);
	}
	if (m_bShowDownloadCommandsToolbar != thePrefs.IsDownloadToolbarEnabled()) {
		thePrefs.SetDownloadToolbar(m_bShowDownloadCommandsToolbar);
		theApp.emuledlg->transferwnd->ShowToolbar(m_bShowDownloadCommandsToolbar);
	}
	thePrefs.m_bDisableFindAsYouType = m_bDisableFindAsYouType;
	thePrefs.SetUITweaksListUpdatePeriod(m_iUITweaksListUpdatePeriod);
	theApp.emuledlg->serverwnd->serverlistctrl.SetMaxSortHistory(m_iUITweaksServerListMaxSortHistory);
	theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.SetMaxSortHistory(m_iUITweaksSearchMaxSortHistory);
	theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SetMaxSortHistory(m_iUITweaksFilesMaxSortHistory);
	theApp.emuledlg->transferwnd->m_pwndTransfer->downloadlistctrl.SetMaxSortHistory(m_iUITweaksDownloadMaxSortHistory);
	theApp.emuledlg->transferwnd->m_pwndTransfer->downloadclientsctrl.SetMaxSortHistory(m_iUITweaksDownloadClientsMaxSortHistory);
	theApp.emuledlg->transferwnd->m_pwndTransfer->uploadlistctrl.SetMaxSortHistory(m_iUITweaksUploadMaxSortHistory);
	theApp.emuledlg->transferwnd->m_pwndTransfer->queuelistctrl.SetMaxSortHistory(m_iUITweaksQueueMaxSortHistory);
	theApp.emuledlg->transferwnd->m_pwndTransfer->clientlistctrl.SetMaxSortHistory(m_iUITweaksClientMaxSortHistory);
	theApp.emuledlg->kademliawnd->m_contactListCtrl->SetMaxSortHistory(m_iUITweaksKadContactSortHistory);
	theApp.emuledlg->kademliawnd->searchList->SetMaxSortHistory(m_iUITweaksKadSearchSortHistory);

	m_sIPGeolocationUpdateURL.Trim();
	thePrefs.SetAutoIPGeolocationUpdate(m_bIPGeolocationAutoUpdate);
	thePrefs.SetIPGeolocationUpdateURL(m_sIPGeolocationUpdateURL);
	thePrefs.SetIPGeolocationUpdatePeriodDays(m_iIPGeolocationUpdatePeriodDays);

	if (thePrefs.GetIPGeolocationMode() != m_iIPGeolocationMode || thePrefs.GetIPGeolocationShowFlag() != m_bIPGeolocationShowFlag) {
		if (thePrefs.GetIPGeolocationMode() == IPGEO_DISABLE && m_iIPGeolocationMode != IPGEO_DISABLE)
			theApp.ipgeolocation->LoadIPGeolocation();// Load IP geolocation database file if it is not already loaded
		thePrefs.SetIPGeolocationMode(m_iIPGeolocationMode);
		thePrefs.SetIPGeolocationShowFlag(m_bIPGeolocationShowFlag);
		theApp.ipgeolocation->Redraw(); // Refresh passive windows
	}

	const CString strPreviousConnectionCheckerServer = thePrefs.GetConnectionCheckerServer();
	const bool bConnectionCheckerWasEnabled = thePrefs.GetConnectionChecker();
	const bool bConnectionCheckerServerChanged = strPreviousConnectionCheckerServer.Compare(m_sConnectionCheckerServer) != 0;
	thePrefs.SetConnectionCheckerServer(m_sConnectionCheckerServer);
	if (bConnectionCheckerWasEnabled && !m_bConnectionChecker) { // Connection Checker is deactivated
		thePrefs.SetConnectionChecker(m_bConnectionChecker);
		if (!theApp.ConChecker->Stop())
			AddDebugLogLine(DLP_HIGH, _T("Connection Checker disable request could not stop the worker thread immediately."));
	} else if (!bConnectionCheckerWasEnabled && m_bConnectionChecker) { // Connection Checker is activated
		thePrefs.SetConnectionChecker(m_bConnectionChecker);
		if (!theApp.ConChecker->Start())
			AddDebugLogLine(DLP_HIGH, _T("Connection Checker enable request could not start the worker thread."));
	} else if (bConnectionCheckerWasEnabled && m_bConnectionChecker && bConnectionCheckerServerChanged) {
		if (!theApp.ConChecker->Stop())
			AddDebugLogLine(DLP_HIGH, _T("Connection Checker server change could not restart immediately because the current worker thread is still stopping."));
		else if (!theApp.ConChecker->Start())
			AddDebugLogLine(DLP_HIGH, _T("Connection Checker server change could not start the worker thread with the updated server."));
	}

	thePrefs.m_bEnableNatTraversal = m_bEnableNatTraversal;
	const ENatTraversalProtocolMode eOldNatTraversalProtocolMode = thePrefs.GetNatTraversalProtocolMode();
	thePrefs.SetNatTraversalProtocolMode(m_iNatTraversalProtocolMode);
	m_iNatTraversalProtocolMode = (int)thePrefs.GetNatTraversalProtocolMode();
	if (eOldNatTraversalProtocolMode != thePrefs.GetNatTraversalProtocolMode() && theApp.clientlist != NULL)
		theApp.clientlist->HandleNatTraversalProtocolModeChanged();

	if (m_iNatTraversalPortWindow < 0) 
		m_iNatTraversalPortWindow = 0;

	if (m_iNatTraversalPortWindow > 65535) 
		m_iNatTraversalPortWindow = 65535;

	if (m_iNatTraversalSweepWindow < 0)
		m_iNatTraversalSweepWindow = 0;

	if (m_iNatTraversalSweepWindow > 65535)
		m_iNatTraversalSweepWindow = 65535;

	if (m_iNatTraversalJitterMinMs < 1)
		m_iNatTraversalJitterMinMs = 1;

	if (m_iNatTraversalJitterMaxMs < m_iNatTraversalJitterMinMs)
		m_iNatTraversalJitterMaxMs = m_iNatTraversalJitterMinMs;

	if (m_iNatTraversalJitterMaxMs > 5000)
		m_iNatTraversalJitterMaxMs = 5000;

	thePrefs.m_uNatTraversalPortWindow = (uint16)m_iNatTraversalPortWindow;
	thePrefs.m_uNatTraversalSweepWindow = (uint16)m_iNatTraversalSweepWindow;
	thePrefs.m_uNatTraversalJitterMinMs = (uint32)m_iNatTraversalJitterMinMs;
	thePrefs.m_uNatTraversalJitterMaxMs = (uint32)m_iNatTraversalJitterMaxMs;

	if (m_iMaxServedBuddies > 100)
		m_iMaxServedBuddies = 100;

	if (m_iMaxServedBuddies < 5)
		m_iMaxServedBuddies = 5;

	thePrefs.m_uMaxServedBuddies = (uint32)m_iMaxServedBuddies; 
	
	if (m_iMaxEServerBuddySlots > ESERVERBUDDY_MAX_SLOTS) m_iMaxEServerBuddySlots = ESERVERBUDDY_MAX_SLOTS;
	if (m_iMaxEServerBuddySlots < ESERVERBUDDY_MIN_SLOTS) m_iMaxEServerBuddySlots = ESERVERBUDDY_MIN_SLOTS;
	thePrefs.SetMaxEServerBuddySlots((uint8)m_iMaxEServerBuddySlots); 

	thePrefs.m_bRetryFailedTcpConnectionAttempts = m_bRetryFailedTcpConnectionAttempts;

	thePrefs.m_breaskSourceAfterIPChange = m_bIsreaskSourceAfterIPChange;
	thePrefs.m_bInformQueuedClientsAfterIPChange = m_bInformQueuedClientsAfterIPChange;

	thePrefs.m_uReAskTimeDif = MIN2MS(m_iReAskFileSrc) - FILEREASKTIME;

	if (thePrefs.GetDownloadValidator() != m_iDownloadValidator) {
		thePrefs.SetDownloadValidator(m_iDownloadValidator);
		theApp.emuledlg->searchwnd->CreateMenus();

	} // Download Validator selection changed

	if (m_iDownloadValidatorDateTimeYearStart > m_iDownloadValidatorDateTimeYearEnd) {
		const int iTemp = m_iDownloadValidatorDateTimeYearStart;
		m_iDownloadValidatorDateTimeYearStart = m_iDownloadValidatorDateTimeYearEnd;
		m_iDownloadValidatorDateTimeYearEnd = iTemp;
	}

	// Download Validator parameters changed
	if (thePrefs.GetDownloadValidatorIgnoreExtension() != m_bDownloadValidatorIgnoreExtension || thePrefs.GetDownloadValidatorIgnoreTags() != m_bDownloadValidatorIgnoreTags || thePrefs.GetDownloadValidatorDontIgnoreNumericTags() != m_bDownloadValidatorDontIgnoreNumericTags
			|| thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() != m_bDownloadValidatorIgnoreNonAlphaNumeric || thePrefs.GetDownloadValidatorCaseInsensitive() != m_bDownloadValidatorCaseInsensitive
			|| thePrefs.GetDownloadValidatorDateTimeMatching() != m_bDownloadValidatorDateTimeMatching || thePrefs.GetDownloadValidatorDateTimeUseYearRange() != m_bDownloadValidatorDateTimeUseYearRange
			|| thePrefs.GetDownloadValidatorDateTimeYearStart() != m_iDownloadValidatorDateTimeYearStart || thePrefs.GetDownloadValidatorDateTimeYearEnd() != m_iDownloadValidatorDateTimeYearEnd
			|| thePrefs.GetDownloadValidatorDateTimeCheckSeconds() != m_bDownloadValidatorDateTimeCheckSeconds || thePrefs.GetDownloadValidatorDateTimeIncludeFollowingNumericValues() != m_bDownloadValidatorDateTimeIncludeFollowingNumericValues) {
		thePrefs.SetDownloadValidatorIgnoreExtension(m_bDownloadValidatorIgnoreExtension);
		thePrefs.SetDownloadValidatorIgnoreTags(m_bDownloadValidatorIgnoreTags);
		thePrefs.SetDownloadValidatorDontIgnoreNumericTags(m_bDownloadValidatorDontIgnoreNumericTags);
		thePrefs.SetDownloadValidatorIgnoreNonAlphaNumeric(m_bDownloadValidatorIgnoreNonAlphaNumeric);
		thePrefs.SetDownloadValidatorCaseInsensitive(m_bDownloadValidatorCaseInsensitive);
		thePrefs.SetDownloadValidatorDateTimeMatching(m_bDownloadValidatorDateTimeMatching);
		thePrefs.SetDownloadValidatorDateTimeUseYearRange(m_bDownloadValidatorDateTimeUseYearRange);
		thePrefs.SetDownloadValidatorDateTimeYearStart(m_iDownloadValidatorDateTimeYearStart);
		thePrefs.SetDownloadValidatorDateTimeYearEnd(m_iDownloadValidatorDateTimeYearEnd);
		thePrefs.SetDownloadValidatorDateTimeCheckSeconds(m_bDownloadValidatorDateTimeCheckSeconds);
		thePrefs.SetDownloadValidatorDateTimeIncludeFollowingNumericValues(m_bDownloadValidatorDateTimeIncludeFollowingNumericValues);
		theApp.DownloadValidator->ReloadMap();
	}

	thePrefs.SetDownloadValidatorAcceptPercentage(m_iDownloadValidatorAcceptPercentage);
	thePrefs.SetDownloadValidatorRejectCanceled(m_bDownloadValidatorRejectCanceled);
	thePrefs.SetDownloadValidatorRejectSameHash(m_bDownloadValidatorRejectSameHash);
	thePrefs.SetDownloadValidatorRejectBlacklisted(m_bDownloadValidatorRejectBlacklisted);
	thePrefs.SetDownloadValidatorMinimumComparisonLength(m_iDownloadValidatorMinimumComparisonLength);
	thePrefs.SetDownloadValidatorSkipIncompleteFileConfirmation(m_bDownloadValidatorSkipIncompleteFileConfirmation);
	thePrefs.SetDownloadValidatorMarkAsBlacklisted(m_bDownloadValidatorMarkAsBlacklisted);
	thePrefs.SetDownloadValidatorAutoMarkAsBlacklisted(m_bDownloadValidatorAutoMarkAsBlacklisted);

		const bool bDownloadInspectorAutoDeleteSettingsChanged = thePrefs.GetDownloadInspector() != m_iDownloadInspector
			|| thePrefs.IsDownloadInspectorAutoDeleteEnabled() != m_bDownloadInspectorAutoDeleteEnabled
			|| thePrefs.IsDownloadInspectorAutoDeleteAddedBeforeEnabled() != m_bDownloadInspectorAutoDeleteAddedBeforeEnabled
			|| thePrefs.GetDownloadInspectorAutoDeleteAddedBeforeDays() != m_iDownloadInspectorAutoDeleteAddedBeforeDays
			|| thePrefs.IsDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled() != m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled
			|| thePrefs.GetDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays() != m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays
			|| thePrefs.IsDownloadInspectorAutoDeleteLastReceivedBeforeEnabled() != m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled
			|| thePrefs.GetDownloadInspectorAutoDeleteLastReceivedBeforeDays() != m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays
			|| thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled() != m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled
			|| thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanPercent() != m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent
			|| thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled() != m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled
			|| thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanMb() != m_iDownloadInspectorAutoDeleteDownloadedLessThanMb
			|| thePrefs.IsDownloadInspectorAutoDeleteBackupEd2kLinksEnabled() != m_bDownloadInspectorAutoDeleteBackupEd2kLinks
			|| thePrefs.IsDownloadInspectorAutoDeleteDontMarkAsCanceledEnabled() != m_bDownloadInspectorAutoDeleteDontMarkAsCanceled;
		const bool bOldDownloadInspectorAutoRenameToMajorityName = thePrefs.IsDownloadInspectorAutoRenameToMajorityName();
		const bool bOldDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly = thePrefs.IsDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly();
		const bool bDownloadInspectorAutoRenameSettingsChanged = thePrefs.GetDownloadInspector() != m_iDownloadInspector
			|| bOldDownloadInspectorAutoRenameToMajorityName != m_bDownloadInspectorAutoRenameToMajorityName
			|| bOldDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly != m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly
			|| thePrefs.GetDownloadInspectorAutoRenameToMajorityNameRequiredPercent() != m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent
			|| thePrefs.GetDownloadInspectorAutoRenameToMajorityNameMinimumVotes() != m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes;

		thePrefs.SetDownloadInspector(m_iDownloadInspector);
	thePrefs.SetDownloadInspectorFake(m_bDownloadInspectorFake);
	thePrefs.SetDownloadInspectorDRM(m_bDownloadInspectorDRM);
	thePrefs.m_bDownloadInspectorInvalidExt = m_bDownloadInspectorInvalidExt;
	thePrefs.SetDownloadInspectorAutoRenameToMajorityName(m_bDownloadInspectorAutoRenameToMajorityName);
	thePrefs.SetDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly(m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly);
	thePrefs.SetDownloadInspectorAutoRenameToMajorityNameRequiredPercent(m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent);
	thePrefs.SetDownloadInspectorAutoRenameToMajorityNameMinimumVotes(m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes);
	thePrefs.SetDownloadInspectorAutoDeleteEnabled(m_bDownloadInspectorAutoDeleteEnabled);
	thePrefs.SetDownloadInspectorAutoDeleteAddedBeforeEnabled(m_bDownloadInspectorAutoDeleteAddedBeforeEnabled);
	thePrefs.SetDownloadInspectorAutoDeleteAddedBeforeDays(m_iDownloadInspectorAutoDeleteAddedBeforeDays);
	thePrefs.SetDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled(m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled);
	thePrefs.SetDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays(m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays);
	thePrefs.SetDownloadInspectorAutoDeleteLastReceivedBeforeEnabled(m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled);
	thePrefs.SetDownloadInspectorAutoDeleteLastReceivedBeforeDays(m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays);
	thePrefs.SetDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled(m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled);
	thePrefs.SetDownloadInspectorAutoDeleteDownloadedLessThanPercent(m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent);
	thePrefs.SetDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled(m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled);
	thePrefs.SetDownloadInspectorAutoDeleteDownloadedLessThanMb(m_iDownloadInspectorAutoDeleteDownloadedLessThanMb);
	thePrefs.SetDownloadInspectorAutoDeleteBackupEd2kLinksEnabled(m_bDownloadInspectorAutoDeleteBackupEd2kLinks);
	thePrefs.SetDownloadInspectorAutoDeleteDontMarkAsCanceledEnabled(m_bDownloadInspectorAutoDeleteDontMarkAsCanceled);
	thePrefs.SetDownloadInspectorCheckPeriod(m_iDownloadInspectorCheckPeriod);
	thePrefs.SetDownloadInspectorCompletedThreshold(m_iDownloadInspectorCompletedThreshold);
	thePrefs.SetDownloadInspectorZeroPercentageThreshold(m_iDownloadInspectorZeroPercentageThreshold);
	thePrefs.SetDownloadInspectorCompressionThreshold(m_iDownloadInspectorCompressionThreshold);
	thePrefs.SetDownloadInspectorBypassZeroPercentage(m_bDownloadInspectorBypassZeroPercentage);
	thePrefs.SetDownloadInspectorCompressionThresholdToBypassZero(m_iDownloadInspectorCompressionThresholdToBypassZero);
		if (bDownloadInspectorAutoRenameSettingsChanged && theApp.downloadqueue != NULL) {
			for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != NULL;) {
				CPartFile* pPartFile = theApp.downloadqueue->filelist.GetNext(pos);
				if (pPartFile != NULL)
					pPartFile->RefreshAutoRenameToMajorityNamePolicy(bOldDownloadInspectorAutoRenameToMajorityName, bOldDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly);
			}
		}
		if (bDownloadInspectorAutoDeleteSettingsChanged && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL)
			theApp.emuledlg->transferwnd->GetDownloadList()->ResetDownloadInspectorAutoDeleteState();

	thePrefs.SetGroupKnownAtTheBottom(m_bGroupKnownAtTheBottom);
	thePrefs.SetSpamThreshold(m_iSpamThreshold);
	thePrefs.SetEd2kSearchMaxResults(m_iEd2kSearchMaxResults);
	thePrefs.SetEd2kSearchMaxMoreRequests(m_iEd2kSearchMaxMoreRequests);
	thePrefs.SetKadFileSearchTotal(m_iKadFileSearchTotal);
	thePrefs.SetKadSearchKeywordTotal(m_iKadSearchKeywordTotal);
	thePrefs.SetKadFileSearchLifetime(m_iKadFileSearchLifetime);
	thePrefs.SetKadSearchKeywordLifetime(m_iKadSearchKeywordLifetime);
	thePrefs.SetShowCloseButtonOnSearchTabs(m_bShowCloseButtonOnSearchTabs);
	theApp.emuledlg->searchwnd->m_pwndResults->searchselect.m_bShowCloseButton = m_bShowCloseButtonOnSearchTabs;

	thePrefs.SetRepeatServerList(m_bRepeatServerList);
	thePrefs.SetDontRemoveStaticServers(m_bDontRemoveStaticServers);
	thePrefs.SetDontSavePartOnReconnect(m_bDontSavePartOnReconnect);

	bool bReloadSharedFilesCtrl = false;
	bool bRefreshShareManagement = false;
	bool bRefreshUploadChunkStatusCache = false;
	if (m_bFileHistoryShowPart != thePrefs.GetFileHistoryShowPart()) {
		thePrefs.SetFileHistoryShowPart(m_bFileHistoryShowPart);
		bReloadSharedFilesCtrl = true;
	}
	if (m_bFileHistoryShowShared != thePrefs.GetFileHistoryShowShared()) {
		thePrefs.SetFileHistoryShowShared(m_bFileHistoryShowShared);
		bReloadSharedFilesCtrl = true;
	}
	if (m_bFileHistoryShowDuplicate != thePrefs.GetFileHistoryShowDuplicate()) {
		thePrefs.SetFileHistoryShowDuplicate(m_bFileHistoryShowDuplicate);
		bReloadSharedFilesCtrl = true;
	}
	// AutoShareSubdirs toggle: if changed, force shared files reload (tree + scan)
	{
		const bool was = thePrefs.GetAutoShareSubdirs();
		thePrefs.SetAutoShareSubdirs(m_bAutoShareSubdirs);
		if (was != m_bAutoShareSubdirs) {
			CemuleDlg* pDlg = theApp.emuledlg;
			if (pDlg && pDlg->sharedfileswnd && ::IsWindow(pDlg->sharedfileswnd->m_hWnd))
				pDlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(1);
		}
	}
	if (thePrefs.GetDontShareExtensions() != m_bDontShareExtensions) {
		thePrefs.SetDontShareExtensions(m_bDontShareExtensions);
		bReloadSharedFilesCtrl = true;
	} else {
		thePrefs.SetDontShareExtensions(m_bDontShareExtensions);
	}
	if (thePrefs.GetDontShareExtensionsList() != m_sDontShareExtensionsList)
		bReloadSharedFilesCtrl = true;
	thePrefs.SetDontShareExtensionsList(m_sDontShareExtensionsList);
	if (thePrefs.GetSpreadbarSetStatus() != m_bSpreadbarSetStatus) {
		thePrefs.SetSpreadbarSetStatus(m_bSpreadbarSetStatus);
		bRefreshShareManagement = true;
		bRefreshUploadChunkStatusCache = true;
	}
	if (thePrefs.GetHideOvershares() != m_iHideOvershares) {
		thePrefs.SetHideOvershares(m_iHideOvershares);
		bRefreshShareManagement = true;
		bRefreshUploadChunkStatusCache = true;
	}
	if (thePrefs.IsSelectiveShareEnabled() != m_bSelectiveShare) {
		thePrefs.SetSelectiveShare(m_bSelectiveShare);
		bRefreshShareManagement = true;
		bRefreshUploadChunkStatusCache = true;
	}
	if (thePrefs.GetShareOnlyTheNeed() != m_bShareOnlyTheNeed) {
		thePrefs.SetShareOnlyTheNeed(m_bShareOnlyTheNeed);
		bRefreshShareManagement = true;
		bRefreshUploadChunkStatusCache = true;
	}
	if (thePrefs.GetPowerShareMode() != m_iPowerShareMode) {
		thePrefs.SetPowerShareMode(m_iPowerShareMode);
		bRefreshShareManagement = true;
	}
	if (thePrefs.IsPowerShareInternalPrioEnabled() != m_bPowerShareInternalPrio) {
		thePrefs.SetPowerShareInternalPrio(m_bPowerShareInternalPrio);
		bRefreshShareManagement = true;
	}
	if (thePrefs.GetPowerShareLimit() != m_iPowerShareLimit) {
		thePrefs.SetPowerShareLimit(m_iPowerShareLimit);
		bRefreshShareManagement = true;
	}
	if (thePrefs.GetSharePermissions() != m_iSharePermissions) {
		thePrefs.SetSharePermissions(m_iSharePermissions);
		bRefreshShareManagement = true;
	}
	if (thePrefs.GetSharePermissionColorRows() != m_bSharePermissionColorRows) {
		thePrefs.SetSharePermissionColorRows(m_bSharePermissionColorRows);
		bReloadSharedFilesCtrl = true;
	}
	thePrefs.SetHighBandwidthUploadPolicy(m_bHighBandwidthUploadPolicy);
	if (static_cast<int>(thePrefs.GetHighBandwidthTargetUploadClients()) != m_iHighBandwidthTargetUploadClients)
		thePrefs.SetHighBandwidthTargetUploadClients(static_cast<uint32>(m_iHighBandwidthTargetUploadClients));
	if (static_cast<int>(thePrefs.GetHighBandwidthUploadSlotElasticPercent()) != m_iHighBandwidthUploadSlotElasticPercent)
		thePrefs.SetHighBandwidthUploadSlotElasticPercent(static_cast<uint32>(m_iHighBandwidthUploadSlotElasticPercent));
	thePrefs.SetAutoHighBandwidthDownloadBuffer(m_bAutoHighBandwidthDownloadBuffer);
	thePrefs.SetHighBandwidthSlowUploadThresholdFactor(fHighBandwidthSlowUploadThresholdFactor);
	thePrefs.SetHighBandwidthSlowUploadGraceSeconds(static_cast<UINT>(m_iHighBandwidthSlowUploadGraceSeconds));
	thePrefs.SetHighBandwidthSlowUploadWarmupSeconds(static_cast<UINT>(m_iHighBandwidthSlowUploadWarmupSeconds));
	thePrefs.SetHighBandwidthZeroUploadGraceSeconds(static_cast<UINT>(m_iHighBandwidthZeroUploadGraceSeconds));
	thePrefs.SetHighBandwidthSlowUploadCooldownSeconds(static_cast<UINT>(m_iHighBandwidthSlowUploadCooldownSeconds));
	thePrefs.SetLowRatioQueueScoreBoost(m_bLowRatioQueueScoreBoost);
	thePrefs.SetLowRatioQueueScoreThreshold(fLowRatioQueueScoreThreshold);
	thePrefs.SetLowRatioQueueScoreBonus(static_cast<UINT>(m_iLowRatioQueueScoreBonus));
	thePrefs.SetUploadSessionTransferLimitMode(static_cast<ESessionTransferLimitMode>(m_iUploadSessionTransferLimitMode));
	thePrefs.SetUploadSessionTransferLimitPercent(static_cast<UINT>(m_iUploadSessionTransferLimitPercent));
	thePrefs.SetUploadSessionTransferLimitMiB(static_cast<UINT>(m_iUploadSessionTransferLimitMiB));
	thePrefs.SetUploadSessionTimeLimitSeconds(static_cast<UINT>(m_iUploadSessionTimeLimitSeconds));
	thePrefs.SetAdjustNTFSDaylightFileTime(m_bAdjustNTFSDaylightFileTime); // Official preference
	thePrefs.SetAllowDSTTimeTolerance(m_bAllowDSTTimeTolerance);
	if (bRefreshShareManagement && theApp.sharedfiles != NULL) {
		CKnownFilesMap sharedFilesMap;
		theApp.sharedfiles->CopySharedFileMap(sharedFilesMap);
		POSITION pos = sharedFilesMap.GetStartPosition();
		while (pos != NULL) {
			CCKey key;
			CKnownFile* pKnownFile = NULL;
			sharedFilesMap.GetNextAssoc(pos, key, pKnownFile);
			if (pKnownFile != NULL) {
				pKnownFile->UpdatePartsInfo();
				if (bRefreshUploadChunkStatusCache)
					pKnownFile->RefreshUploadingClientPartStatusCache(true);
			}
		}
		bReloadSharedFilesCtrl = true;
	}
	if (bReloadSharedFilesCtrl) {
		CemuleDlg* pDlg = theApp.emuledlg;
		if (pDlg && pDlg->sharedfileswnd && ::IsWindow(pDlg->sharedfileswnd->m_hWnd)) {
			pDlg->sharedfileswnd->sharedfilesctrl.ReloadList(false, LSF_SELECTION);
			pDlg->sharedfileswnd->sharedfilesctrl.Invalidate(FALSE);
		}
	}

	thePrefs.m_bEmulateMLDonkey = m_bEmulateMLDonkey;
	thePrefs.m_bEmulateEdonkey = m_bEmulateEdonkey;
	thePrefs.m_bEmulateEdonkeyHybrid = m_bEmulateEdonkeyHybrid;
	thePrefs.m_bEmulateShareaza = m_bEmulateShareaza;
	thePrefs.m_bEmulateLphant = m_bEmulateLphant;
	thePrefs.m_bEmulateCommunity = m_bEmulateCommunity;
	thePrefs.m_iEmulateCommunityTagSavingTreshold = m_iEmulateCommunityTagSavingTreshold;
	thePrefs.m_bLogEmulator = m_bLogEmulator;

    thePrefs.m_bUseIntelligentChunkSelection = m_bUseIntelligentChunkSelection;
		
	if (thePrefs.creditSystemMode != m_iCreditSystem) {
		thePrefs.creditSystemMode = m_iCreditSystem;
		theApp.clientcredits->ResetCheckScoreRatio();
	}

	thePrefs.SetClientHistory(m_bClientHistory);
	thePrefs.SetClientHistoryExpDays(m_iClientHistoryExpDays);
	thePrefs.SetClientHistoryLog(m_bClientHistoryLog);

	if (thePrefs.GetRemoteSharedFilesUserHash() != m_bRemoteSharedFilesUserHash || thePrefs.GetRemoteSharedFilesClientNote() != m_bRemoteSharedFilesClientNote) {
		thePrefs.SetRemoteSharedFilesUserHash(m_bRemoteSharedFilesUserHash);
		thePrefs.SetRemoteSharedFilesClientNote(m_bRemoteSharedFilesClientNote);
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.UpdateTabHeader(0, EMPTY, true);
	}

	thePrefs.SetRemoteSharedFilesAutoQueryPeriod(m_iRemoteSharedFilesAutoQueryPeriod);
	thePrefs.SetRemoteSharedFilesAutoQueryMaxClients(m_iRemoteSharedFilesAutoQueryMaxClients);
	thePrefs.SetRemoteSharedFilesAutoQueryClientPeriod(m_iRemoteSharedFilesAutoQueryClientPeriod);
	thePrefs.SetRemoteSharedFilesSetAutoQueryDownload(m_bRemoteSharedFilesSetAutoQueryDownload);
	thePrefs.SetRemoteSharedFilesSetAutoQueryDownloadThreshold(m_iRemoteSharedFilesSetAutoQueryDownloadThreshold);
	thePrefs.SetRemoteSharedFilesSetAutoQueryUpload(m_bRemoteSharedFilesSetAutoQueryUpload);
	thePrefs.SetRemoteSharedFilesSetAutoQueryUploadThreshold(m_iRemoteSharedFilesSetAutoQueryUploadThreshold);

	thePrefs.SetSaveLoadSources(m_bSaveLoadSources);
	thePrefs.SetSaveLoadSourcesMaxSources(m_iSaveLoadSourcesMaxSources);
	thePrefs.SetSaveLoadSourcesExpirationDays(m_iSaveLoadSourcesExpirationDays);

	switch (m_iPurgeMode)
	{
	case 1: // partial purge
	{
		thePrefs.m_bPartiallyPurgeOldKnownFiles = true;
		thePrefs.m_bCompletlyPurgeOldKnownFiles = false;
	} break;
	case 2: // complete purge
	{
		thePrefs.m_bPartiallyPurgeOldKnownFiles = false;
		thePrefs.m_bCompletlyPurgeOldKnownFiles = true;
	} break;
	default:
		ASSERT(false); //wth?! anyway, we proceed to don't purge
	case 0: // don't purge
	{
		thePrefs.m_bPartiallyPurgeOldKnownFiles = false;
		thePrefs.m_bCompletlyPurgeOldKnownFiles = false;
	} break;
	}
	thePrefs.SetKnownMetDays(m_iKnownMetDays);
	thePrefs.m_bRemoveAichImmediately = m_bRemoveAichImmediately;
	thePrefs.SetClientsExpDays(m_iClientsExpDays);

	thePrefs.SetBackupOnExit(m_bBackupOnExit);
	thePrefs.SetBackupPeriodic(m_bBackupPeriodic);
	thePrefs.SetBackupPeriod(m_iBackupPeriod);
	thePrefs.SetBackupMax(m_iBackupMax);
	thePrefs.SetBackupCompressed(m_bBackupCompressed);

	thePrefs.bMiniMuleAutoClose = bMiniMuleAutoClose;
	thePrefs.iMiniMuleTransparency = iMiniMuleTransparency;
	thePrefs.m_bRestoreLastMainWndDlg = m_bRestoreLastMainWndDlg;
	thePrefs.m_bRestoreLastLogPane = m_bRestoreLastLogPane;
	thePrefs.m_iStraightWindowStyles = m_iStraightWindowStyles;
	thePrefs.m_bRTLWindowsLayout = m_bRTLWindowsLayout;
	thePrefs.m_bShowActiveDownloadsBold = m_bShowActiveDownloadsBold;
	theApp.emuledlg->transferwnd->GetDownloadList()->ShowActiveDownloadsBold(m_bShowActiveDownloadsBold);
	thePrefs.m_iMaxChatHistory = m_iMaxChatHistory;
	thePrefs.maxmsgsessions = m_umaxmsgsessions;
	thePrefs.m_strDateTimeFormat = m_strDateTimeFormat;
	thePrefs.m_strDateTimeFormat4Lists = m_strDateTimeFormat4List;
	thePrefs.m_bShowVerticalHourMarkers = m_bShowVerticalHourMarkers;
	thePrefs.m_bReBarToolbar = !m_bReBarToolbar;
	thePrefs.m_bIconflashOnNewMessage = m_bIconflashOnNewMessage;
	thePrefs.m_bShowCopyEd2kLinkCmd = m_bShowCopyEd2kLinkCmd;
	thePrefs.m_bupdatequeuelist = !m_bUpdateQueue;
	thePrefs.dontRecreateGraphs = m_bRepaint;
	thePrefs.m_bShowUpDownIconInTaskbar = m_bShowUpDownIconInTaskbar;
	thePrefs.iMaxLogBuff = iMaxLogBuff * 1024;
	thePrefs.m_iLogFileFormat = (ELogFileFormat)m_iLogFileFormat;
	thePrefs.m_strDateTimeFormat4Log = m_strDateTimeFormat4Log;
	thePrefs.bCreateCrashDump = bCreateCrashDump;
	thePrefs.bIgnoreInstances = bIgnoreInstances;
	thePrefs.sNotifierMailEncryptCertName = sNotifierMailEncryptCertName;
	thePrefs.m_iPreviewSmallBlocks = m_iPreviewSmallBlocks;
	thePrefs.m_bPreviewCopiedArchives = m_bPreviewCopiedArchives;
	thePrefs.m_bPreviewOnIconDblClk = m_bPreviewOnIconDblClk;
	thePrefs.sInternetSecurityZone = sInternetSecurityZone;
	thePrefs.m_strTxtEditor = sTxtEditor;
	thePrefs.nServerUDPPort = (uint16)iServerUDPPort;
	thePrefs.m_bRemove2bin = m_bRemoveFilesToBin;
	thePrefs.m_bHighresTimer = m_bHighresTimer;
	thePrefs.m_bTrustEveryHash = m_bTrustEveryHash;
	thePrefs.m_bPreferRestrictedOverUser = m_bPreferRestrictedOverUser;
	thePrefs.m_bUseUserSortedServerList = m_bUseUserSortedServerList;
	thePrefs.m_iWebFileUploadSizeLimitMB = m_iWebFileUploadSizeLimitMB;
	int iPos = 0;
	CString strIP = m_sAllowedIPs.Tokenize(L";", iPos);
	thePrefs.m_aAllowedRemoteAccessIPs.RemoveAll();
	while (!strIP.IsEmpty())
	{
		u_long nIP = inet_addr(CStringA(strIP));
		if (nIP != INADDR_ANY && nIP != INADDR_NONE)
			thePrefs.m_aAllowedRemoteAccessIPs.Add(nIP);
		strIP = m_sAllowedIPs.Tokenize(L";", iPos);
	}
	thePrefs.m_iDebugSearchResultDetailLevel = m_iDebugSearchResultDetailLevel;
	if (m_iCryptTCPPaddingLength > 255) m_iCryptTCPPaddingLength = 255;
	thePrefs.m_byCryptTCPPaddingLength = (uint8)m_iCryptTCPPaddingLength;

	thePrefs.ICH = m_ICH;
	thePrefs.dontcompressavi = m_dontcompressavi;
	thePrefs.m_bRearrangeKadSearchKeywords = m_bRearrangeKadSearchKeywords;
	thePrefs.beepOnError = m_bBeeper;
	thePrefs.msgsecure = m_bMsgOnlySec;
	thePrefs.m_bKeepUnavailableFixedSharedDirs = m_bKeepUnavailableFixedSharedDirs;

	if (thePrefs.GetUIDarkMode() != m_iUIDarkMode) { // Check if the dark mode setting has changed	
		thePrefs.SetUIDarkMode(m_iUIDarkMode);
		SetModified(FALSE);
		::PostMessage(theApp.emuledlg->GetSafeHwnd(), UM_APP_SWITCH_DARKMODE, 0, 0); // Post a message to CEmuleDlg to handle dark mode asynchronously
	}

    SetModified(FALSE);
    return CPropertyPage::OnApply();
}

void CPPgMod::LocalizeItemText(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetItemText(item, GetResString(strid));
}

void CPPgMod::LocalizeItemInfoText(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetItemInfo(item, GetResString(strid));
}

void CPPgMod::LocalizeEditLabel(HTREEITEM item, LPCTSTR strid)
{
	if (item)
		m_ctrlTreeOptions.SetEditLabel(item, GetResString(strid));
}

void CPPgMod::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_MOD")));
		SetDlgItemText(IDC_WARNING, GetResString(_T("TWEAKS_WARNING")));
		SetDlgItemText(IDC_PREFINI_STATIC, GetResString(_T("PW_MOD")));
		SetDlgItemText(IDC_OPENPREFINI, GetResString(_T("OPENPREFINI")));

		LocalizeItemText(m_htiUITweaks, _T("UI_TWEAKS"));
		LocalizeItemInfoText(m_htiUITweaks, _T("UI_TWEAKS_INFO"));
		LocalizeItemText(m_htiUIDarkMode, _T("DARK_MODE"));
		LocalizeItemInfoText(m_htiUIDarkMode, _T("DARK_MODE_INFO"));
		LocalizeItemText(m_htiUIDarkModeAuto, _T("DARK_MODE_AUTO"));
		LocalizeItemInfoText(m_htiUIDarkModeAuto, _T("DARK_MODE_AUTO_INFO"));
		LocalizeItemText(m_htiUIDarkModeOn, _T("DARK_MODE_ON"));
		LocalizeItemInfoText(m_htiUIDarkModeOn, _T("DARK_MODE_ON_INFO"));
		LocalizeItemText(m_htiUIDarkModeOff, _T("DARK_MODE_OFF"));
		LocalizeItemInfoText(m_htiUIDarkModeOff, _T("DARK_MODE_OFF_INFO"));
		LocalizeEditLabel(m_htiUITweaksSpeedGraph, _T("SHOW_SPEED_GRAPH"));
		LocalizeItemInfoText(m_htiUITweaksSpeedGraph, _T("SHOW_SPEED_GRAPH"));
		LocalizeItemText(m_htiShowDownloadCommandsToolbar, _T("SHOW_DOWNLOAD_COMMANDS_TOOLBAR"));
		LocalizeItemInfoText(m_htiShowDownloadCommandsToolbar, _T("SHOW_DOWNLOAD_COMMANDS_TOOLBAR_INFO"));
		LocalizeItemText(m_htiDisableFindAsYouType, _T("DISABLE_AS_YOU_TYPE"));
		LocalizeItemInfoText(m_htiDisableFindAsYouType, _T("DISABLE_AS_YOU_TYPE_INFO"));
		LocalizeEditLabel(m_htiUITweaksListUpdatePeriod, _T("UI_TWEAKS_LIST_UPDATE_PERIOD"));
		LocalizeItemInfoText(m_htiUITweaksListUpdatePeriod, _T("UI_TWEAKS_LIST_UPDATE_PERIOD_INFO"));
		LocalizeItemText(m_htiUITweaksMaxSortHistory, _T("UI_TWEAKS_MAX_SORT"));
		LocalizeItemInfoText(m_htiUITweaksMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksServerMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_SERVER"));
		LocalizeItemInfoText(m_htiUITweaksServerMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksSearchMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_SEARCH"));
		LocalizeItemInfoText(m_htiUITweaksSearchMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksFilesMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_FILES"));
		LocalizeItemInfoText(m_htiUITweaksFilesMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksDownloadMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_DOWNLOAD"));
		LocalizeItemInfoText(m_htiUITweaksDownloadMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksDownloadClientsMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_DCLIENTS"));
		LocalizeItemInfoText(m_htiUITweaksDownloadClientsMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksUploadMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_UPLOAD"));
		LocalizeItemInfoText(m_htiUITweaksUploadMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksQueueMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_QUEUE"));
		LocalizeItemInfoText(m_htiUITweaksQueueMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksClientMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_CLIENT"));
		LocalizeItemInfoText(m_htiUITweaksClientMaxSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksKadContactSortHistory, _T("UI_TWEAKS_MAX_SORT_KADCONTACT"));
		LocalizeItemInfoText(m_htiUITweaksKadContactSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));
		LocalizeEditLabel(m_htiUITweaksKadSearchSortHistory, _T("UI_TWEAKS_MAX_SORT_KADSEARCH"));
		LocalizeItemInfoText(m_htiUITweaksKadSearchSortHistory, _T("UI_TWEAKS_MAX_SORT_INFO"));

		LocalizeItemText(m_htiIPGeolocation, _T("IPGEOLOCATION_MAIN"));
		LocalizeItemInfoText(m_htiIPGeolocation, _T("IPGEOLOCATION_MAIN_INFO"));
		LocalizeItemText(m_htiIPGeolocationNameDisable, _T("IPGEOLOCATION_DISABLED"));
		LocalizeItemInfoText(m_htiIPGeolocationNameDisable, _T("IPGEOLOCATION_DISABLED_INFO"));
		LocalizeItemText(m_htiIPGeolocationCountryCode, _T("IPGEOLOCATION_COUNTRYCODE"));
		LocalizeItemInfoText(m_htiIPGeolocationCountryCode, _T("IPGEOLOCATION_COUNTRYCODE_INFO"));
		LocalizeItemText(m_htiIPGeolocationCountry, _T("IPGEOLOCATION_COUNTRY"));
		LocalizeItemInfoText(m_htiIPGeolocationCountry, _T("IPGEOLOCATION_COUNTRY_INFO"));
		LocalizeItemText(m_htiIPGeolocationCountryCity, _T("IPGEOLOCATION_COUNTRYCITY"));
		LocalizeItemInfoText(m_htiIPGeolocationCountryCity, _T("IPGEOLOCATION_COUNTRYCITY_INFO"));
		LocalizeItemText(m_htiIPGeolocationShowFlag, _T("IPGEOLOCATION_FLAGS"));
		LocalizeItemInfoText(m_htiIPGeolocationShowFlag, _T("IPGEOLOCATION_FLAGS_INFO"));
		LocalizeItemText(m_htiIPGeolocationAutoUpdate, _T("AUTO_UPDATE"));
		LocalizeItemInfoText(m_htiIPGeolocationAutoUpdate, _T("IPGEOLOCATION_AUTO_UPDATE_INFO"));
		LocalizeEditLabel(m_htiIPGeolocationUpdateURL, _T("IPGEOLOCATION_UPDATE_URL"));
		LocalizeItemInfoText(m_htiIPGeolocationUpdateURL, _T("IPGEOLOCATION_UPDATE_URL_INFO"));
		LocalizeEditLabel(m_htiIPGeolocationPeriodDays, _T("IPGEOLOCATION_PERIOD_DAYS"));
		LocalizeItemInfoText(m_htiIPGeolocationPeriodDays, _T("IPGEOLOCATION_PERIOD_DAYS_INFO"));
		LocalizeItemText(m_htiIPGeolocationUpdateNow, _T("IPGEOLOCATION_UPDATE_NOW"));
		LocalizeItemInfoText(m_htiIPGeolocationUpdateNow, _T("IPGEOLOCATION_UPDATE_NOW_INFO"));

		LocalizeItemText(m_htiConTweaks, _T("CON_TWEAKS"));
		LocalizeItemInfoText(m_htiConTweaks, _T("CON_TWEAKS_INFO"));

		LocalizeItemText(m_htiUploadSettings, _T("UPLOAD_SETTINGS"));
		LocalizeItemInfoText(m_htiUploadSettings, _T("UPLOAD_SETTINGS_INFO"));
		LocalizeItemText(m_htiHighBandwidthUploadPolicy, _T("HIGH_BANDWIDTH_UPLOAD_POLICY"));
		LocalizeItemInfoText(m_htiHighBandwidthUploadPolicy, _T("HIGH_BANDWIDTH_UPLOAD_POLICY_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthTargetUploadClients, _T("HIGH_BANDWIDTH_TARGET_UPLOAD_CLIENTS"));
		LocalizeItemInfoText(m_htiHighBandwidthTargetUploadClients, _T("HIGH_BANDWIDTH_TARGET_UPLOAD_CLIENTS_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthUploadSlotElasticPercent, _T("HIGH_BANDWIDTH_UPLOAD_SLOT_ELASTIC_PERCENT"));
		LocalizeItemInfoText(m_htiHighBandwidthUploadSlotElasticPercent, _T("HIGH_BANDWIDTH_UPLOAD_SLOT_ELASTIC_PERCENT_INFO"));
		LocalizeItemText(m_htiAutoHighBandwidthDownloadBuffer, _T("AUTO_HIGH_BANDWIDTH_DOWNLOAD_BUFFER"));
		LocalizeItemInfoText(m_htiAutoHighBandwidthDownloadBuffer, _T("AUTO_HIGH_BANDWIDTH_DOWNLOAD_BUFFER_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthSlowUploadThresholdFactor, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_THRESHOLD_FACTOR"));
		LocalizeItemInfoText(m_htiHighBandwidthSlowUploadThresholdFactor, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_THRESHOLD_FACTOR_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthSlowUploadGraceSeconds, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_GRACE_SECONDS"));
		LocalizeItemInfoText(m_htiHighBandwidthSlowUploadGraceSeconds, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_GRACE_SECONDS_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthSlowUploadWarmupSeconds, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_WARMUP_SECONDS"));
		LocalizeItemInfoText(m_htiHighBandwidthSlowUploadWarmupSeconds, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_WARMUP_SECONDS_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthZeroUploadGraceSeconds, _T("HIGH_BANDWIDTH_ZERO_UPLOAD_GRACE_SECONDS"));
		LocalizeItemInfoText(m_htiHighBandwidthZeroUploadGraceSeconds, _T("HIGH_BANDWIDTH_ZERO_UPLOAD_GRACE_SECONDS_INFO"));
		LocalizeEditLabel(m_htiHighBandwidthSlowUploadCooldownSeconds, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_COOLDOWN_SECONDS"));
		LocalizeItemInfoText(m_htiHighBandwidthSlowUploadCooldownSeconds, _T("HIGH_BANDWIDTH_SLOW_UPLOAD_COOLDOWN_SECONDS_INFO"));
		LocalizeItemText(m_htiLowRatioQueueScoreBoost, _T("LOW_RATIO_QUEUE_SCORE_BOOST"));
		LocalizeItemInfoText(m_htiLowRatioQueueScoreBoost, _T("LOW_RATIO_QUEUE_SCORE_BOOST_INFO"));
		LocalizeEditLabel(m_htiLowRatioQueueScoreThreshold, _T("LOW_RATIO_QUEUE_SCORE_THRESHOLD"));
		LocalizeItemInfoText(m_htiLowRatioQueueScoreThreshold, _T("LOW_RATIO_QUEUE_SCORE_THRESHOLD_INFO"));
		LocalizeEditLabel(m_htiLowRatioQueueScoreBonus, _T("LOW_RATIO_QUEUE_SCORE_BONUS"));
		LocalizeItemInfoText(m_htiLowRatioQueueScoreBonus, _T("LOW_RATIO_QUEUE_SCORE_BONUS_INFO"));
		LocalizeItemText(m_htiUploadSessionTransferLimit, _T("UPLOAD_SESSION_TRANSFER_LIMIT"));
		LocalizeItemInfoText(m_htiUploadSessionTransferLimit, _T("UPLOAD_SESSION_TRANSFER_LIMIT_INFO"));
		LocalizeItemText(m_htiUploadSessionTransferLimitDisabled, _T("UPLOAD_SESSION_TRANSFER_LIMIT_DISABLED"));
		LocalizeItemInfoText(m_htiUploadSessionTransferLimitDisabled, _T("UPLOAD_SESSION_TRANSFER_LIMIT_DISABLED_INFO"));
		LocalizeItemText(m_htiUploadSessionTransferLimitPercent, _T("UPLOAD_SESSION_TRANSFER_LIMIT_PERCENT"));
		LocalizeItemInfoText(m_htiUploadSessionTransferLimitPercent, _T("UPLOAD_SESSION_TRANSFER_LIMIT_PERCENT_INFO"));
		LocalizeEditLabel(m_htiUploadSessionTransferLimitPercentValue, _T("UPLOAD_SESSION_TRANSFER_LIMIT_PERCENT_VALUE"));
		LocalizeItemInfoText(m_htiUploadSessionTransferLimitPercentValue, _T("UPLOAD_SESSION_TRANSFER_LIMIT_PERCENT_VALUE_INFO"));
		LocalizeItemText(m_htiUploadSessionTransferLimitMiB, _T("UPLOAD_SESSION_TRANSFER_LIMIT_MIB"));
		LocalizeItemInfoText(m_htiUploadSessionTransferLimitMiB, _T("UPLOAD_SESSION_TRANSFER_LIMIT_MIB_INFO"));
		LocalizeEditLabel(m_htiUploadSessionTransferLimitMiBValue, _T("UPLOAD_SESSION_TRANSFER_LIMIT_MIB_VALUE"));
		LocalizeItemInfoText(m_htiUploadSessionTransferLimitMiBValue, _T("UPLOAD_SESSION_TRANSFER_LIMIT_MIB_VALUE_INFO"));
		LocalizeEditLabel(m_htiUploadSessionTimeLimitSeconds, _T("UPLOAD_SESSION_TIME_LIMIT_SECONDS"));
		LocalizeItemInfoText(m_htiUploadSessionTimeLimitSeconds, _T("UPLOAD_SESSION_TIME_LIMIT_SECONDS_INFO"));

		LocalizeItemText(m_htiConnectionChecker, _T("CONNECTION_CHECK"));
		LocalizeItemInfoText(m_htiConnectionChecker, _T("CONNECTION_CHECK_INFO"));
		LocalizeItemText(m_htiConnectionCheckerActivate, _T("CONNECTION_CHECK_ACTIVATE"));
		LocalizeItemInfoText(m_htiConnectionCheckerActivate, _T("CONNECTION_CHECK_ACTIVATE_INFO"));
		LocalizeEditLabel(m_htiConnectionCheckerServer, _T("URL_ADDR"));
		LocalizeItemInfoText(m_htiConnectionCheckerServer, _T("CONNECTION_CHECK_SERVER_INFO"));
		
		LocalizeItemText(m_htiEnableNatTraversal, _T("ENABLE_NATT"));
		LocalizeItemInfoText(m_htiEnableNatTraversal, _T("ENABLE_NATT_INFO"));
		LocalizeItemText(m_htiNatTraversalProtocol, _T("NATT_TRANSFER_PROTOCOL"));
		LocalizeItemInfoText(m_htiNatTraversalProtocol, _T("NATT_TRANSFER_PROTOCOL_INFO"));
		LocalizeItemText(m_htiNatTraversalProtocolPreferQuic, _T("NATT_PROTOCOL_PREFER_QUIC"));
		LocalizeItemInfoText(m_htiNatTraversalProtocolPreferQuic, _T("NATT_PROTOCOL_PREFER_QUIC_INFO"));
		LocalizeItemText(m_htiNatTraversalProtocolUtpOnly, _T("NATT_PROTOCOL_UTP_ONLY"));
		LocalizeItemInfoText(m_htiNatTraversalProtocolUtpOnly, _T("NATT_PROTOCOL_UTP_ONLY_INFO"));

		LocalizeEditLabel(m_htiNatTraversalPortWindow, _T("NATT_PORT_WINDOW"));
		LocalizeItemInfoText(m_htiNatTraversalPortWindow, _T("NATT_PORT_WINDOW_INFO"));

		LocalizeEditLabel(m_htiNatTraversalSweepWindow, _T("NATT_SWEEP_WINDOW"));
		LocalizeItemInfoText(m_htiNatTraversalSweepWindow, _T("NATT_SWEEP_WINDOW_INFO"));

		LocalizeEditLabel(m_htiNatTraversalJitterMinMs, _T("NATT_UTP_JITTER_MIN"));
		LocalizeItemInfoText(m_htiNatTraversalJitterMinMs, _T("NATT_UTP_JITTER_MIN_INFO"));

		LocalizeEditLabel(m_htiNatTraversalJitterMaxMs, _T("NATT_UTP_JITTER_MAX"));
		LocalizeItemInfoText(m_htiNatTraversalJitterMaxMs, _T("NATT_UTP_JITTER_MAX_INFO"));

		LocalizeEditLabel(m_htiMaxServedBuddies, _T("KAD_BUDDY_SLOTS"));
		LocalizeItemInfoText(m_htiMaxServedBuddies, _T("KAD_BUDDY_SLOTS_INFO"));

		LocalizeEditLabel(m_htiMaxEServerBuddySlots, _T("ESERVER_BUDDY_SLOTS"));
		LocalizeItemInfoText(m_htiMaxEServerBuddySlots, _T("ESERVER_BUDDY_SLOTS_INFO"));

		LocalizeItemText(m_htiRetryFailedTcpConnectionAttempts, _T("RETRY_CONNECTION_ATTEMPTS"));
		LocalizeItemInfoText(m_htiRetryFailedTcpConnectionAttempts, _T("RETRY_CONNECTION_ATTEMPTS_INFO"));

		LocalizeItemText(m_htiIsreaskSourceAfterIPChange, _T("RSAIC"));
		LocalizeItemInfoText(m_htiIsreaskSourceAfterIPChange, _T("RSAIC_INFO"));
		LocalizeItemText(m_htiInformQueuedClientsAfterIPChange, _T("IQCAOC"));
		LocalizeItemInfoText(m_htiInformQueuedClientsAfterIPChange, _T("IQCAOC_INFO"));

		LocalizeEditLabel(m_htiReAskFileSrc, _T("REASK_FILE_SRC"));
		LocalizeItemInfoText(m_htiReAskFileSrc, _T("REASK_FILE_SRC_INFO"));

		LocalizeItemText(m_htiDownloadValidator, _T("DOWNLOAD_VALIDATOR"));
		LocalizeItemInfoText(m_htiDownloadValidator, _T("DOWNLOAD_VALIDATOR_INFO"));
		LocalizeItemText(m_htiDownloadValidatorPassive, _T("DOWNLOAD_VALIDATOR_PASSIVE"));
		LocalizeItemInfoText(m_htiDownloadValidatorPassive, _T("DOWNLOAD_VALIDATOR_PASSIVE_INFO"));
		LocalizeItemText(m_htiDownloadValidatorAlwaysAsk, _T("DOWNLOAD_VALIDATOR_ALWAYS_ASK"));
		LocalizeItemInfoText(m_htiDownloadValidatorAlwaysAsk, _T("DOWNLOAD_VALIDATOR_ALWAYS_ASK_INFO"));
		LocalizeItemText(m_htiDownloadValidatorReject, _T("DOWNLOAD_VALIDATOR_REJECT"));
		LocalizeItemInfoText(m_htiDownloadValidatorReject, _T("DOWNLOAD_VALIDATOR_REJECT_INFO"));
		LocalizeItemText(m_htiDownloadValidatorAccept, _T("DOWNLOAD_VALIDATOR_ACCEPT"));
		LocalizeItemInfoText(m_htiDownloadValidatorAccept, _T("DOWNLOAD_VALIDATOR_ACCEPT_INFO"));
		LocalizeEditLabel(m_htiDownloadValidatorAcceptPercentage, _T("DOWNLOAD_VALIDATOR_ACCEPT_PERCENTAGE"));
		LocalizeItemInfoText(m_htiDownloadValidatorAcceptPercentage, _T("DOWNLOAD_VALIDATOR_ACCEPT_PERCENTAGE_INFO"));
		LocalizeItemText(m_htiDownloadValidatorRejectCanceled, _T("DOWNLOAD_VALIDATOR_REJECT_CANCELED"));
		LocalizeItemInfoText(m_htiDownloadValidatorRejectCanceled, _T("DOWNLOAD_VALIDATOR_REJECT_CANCELED_INFO"));
		LocalizeItemText(m_htiDownloadValidatorRejectSameHash, _T("DOWNLOAD_VALIDATOR_REJECT_SAME_HASH"));
		LocalizeItemInfoText(m_htiDownloadValidatorRejectSameHash, _T("DOWNLOAD_VALIDATOR_REJECT_SAME_HASH_INFO"));
		LocalizeItemText(m_htiDownloadValidatorRejectBlacklisted, _T("DOWNLOAD_VALIDATOR_REJECT_BLACKLISTED"));
		LocalizeItemInfoText(m_htiDownloadValidatorRejectBlacklisted, _T("DOWNLOAD_VALIDATOR_REJECT_BLACKLISTED_INFO"));
		LocalizeItemText(m_htiDownloadValidatorCaseInsensitive, _T("DOWNLOAD_VALIDATOR_CASE_INSENSITIVE"));
		LocalizeItemInfoText(m_htiDownloadValidatorCaseInsensitive, _T("DOWNLOAD_VALIDATOR_CASE_INSENSITIVE_INFO"));
		LocalizeItemText(m_htiDownloadValidatorIgnoreExtension, _T("DOWNLOAD_VALIDATOR_IGNORE_EXTENSION"));
		LocalizeItemInfoText(m_htiDownloadValidatorIgnoreExtension, _T("DOWNLOAD_VALIDATOR_IGNORE_EXTENSION_INFO"));
		LocalizeItemText(m_htiDownloadValidatorIgnoreTags, _T("DOWNLOAD_VALIDATOR_IGNORE_TAGS"));
		LocalizeItemInfoText(m_htiDownloadValidatorIgnoreTags, _T("DOWNLOAD_VALIDATOR_IGNORE_TAGS_INFO"));
		LocalizeItemText(m_htiDownloadValidatorDontIgnoreNumericTags, _T("DOWNLOAD_VALIDATOR_DONT_IGNORE_NUMERIC_TAGS"));
		LocalizeItemInfoText(m_htiDownloadValidatorDontIgnoreNumericTags, _T("DOWNLOAD_VALIDATOR_DONT_IGNORE_NUMERIC_TAGS_INFO"));
		LocalizeItemText(m_htiDownloadValidatorIgnoreNonAlphaNumeric, _T("DOWNLOAD_VALIDATOR_IGNORE_NON_ALPHANUMERIC"));
		LocalizeItemInfoText(m_htiDownloadValidatorIgnoreNonAlphaNumeric, _T("DOWNLOAD_VALIDATOR_IGNORE_NON_ALPHANUMERIC_INFO"));
		LocalizeEditLabel(m_htiDownloadValidatorMinimumComparisonLength, _T("DOWNLOAD_VALIDATOR_MINIMUM_COMPARISON_LENGTH"));
		LocalizeItemInfoText(m_htiDownloadValidatorMinimumComparisonLength, _T("DOWNLOAD_VALIDATOR_MINIMUM_COMPARISON_LENGTH_INFO"));
		LocalizeItemText(m_htiDownloadValidatorDateTimeMatching, _T("DOWNLOAD_VALIDATOR_DATETIME_MATCHING"));
		LocalizeItemInfoText(m_htiDownloadValidatorDateTimeMatching, _T("DOWNLOAD_VALIDATOR_DATETIME_MATCHING_INFO"));
		LocalizeItemText(m_htiDownloadValidatorDateTimeUseYearRange, _T("DOWNLOAD_VALIDATOR_DATETIME_USE_YEAR_RANGE"));
		LocalizeItemInfoText(m_htiDownloadValidatorDateTimeUseYearRange, _T("DOWNLOAD_VALIDATOR_DATETIME_USE_YEAR_RANGE_INFO"));
		LocalizeEditLabel(m_htiDownloadValidatorDateTimeYearStart, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_START"));
		LocalizeItemInfoText(m_htiDownloadValidatorDateTimeYearStart, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_START_INFO"));
		LocalizeEditLabel(m_htiDownloadValidatorDateTimeYearEnd, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_END"));
		LocalizeItemInfoText(m_htiDownloadValidatorDateTimeYearEnd, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_END_INFO"));
		LocalizeItemText(m_htiDownloadValidatorDateTimeCheckSeconds, _T("DOWNLOAD_VALIDATOR_DATETIME_CHECK_SECONDS"));
		LocalizeItemInfoText(m_htiDownloadValidatorDateTimeCheckSeconds, _T("DOWNLOAD_VALIDATOR_DATETIME_CHECK_SECONDS_INFO"));
		LocalizeItemText(m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, _T("DOWNLOAD_VALIDATOR_DATETIME_INCLUDE_FOLLOWING_NUMERIC_VALUES"));
		LocalizeItemInfoText(m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, _T("DOWNLOAD_VALIDATOR_DATETIME_INCLUDE_FOLLOWING_NUMERIC_VALUES_INFO"));
		LocalizeItemText(m_htiDownloadValidatorSkipIncompleteFileConfirmation, _T("DOWNLOAD_VALIDATOR_SKIP_INCOMPLETE_CONFIRMATION"));
		LocalizeItemInfoText(m_htiDownloadValidatorSkipIncompleteFileConfirmation, _T("DOWNLOAD_VALIDATOR_SKIP_INCOMPLETE_CONFIRMATION_INFO"));
		LocalizeItemText(m_htiDownloadValidatorMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_MARK_AS_BLACKLISTED"));
		LocalizeItemInfoText(m_htiDownloadValidatorMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_MARK_AS_BLACKLISTED_INFO"));
		LocalizeItemText(m_htiDownloadValidatorAutoMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_AUTO_MARK_AS_BLACKLISTED"));
		LocalizeItemInfoText(m_htiDownloadValidatorAutoMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_AUTO_MARK_AS_BLACKLISTED_INFO"));

		LocalizeItemText(m_htiDownloadInspector, _T("DOWNLOAD_INSPECTOR"));
		LocalizeItemInfoText(m_htiDownloadInspector, _T("DOWNLOAD_INSPECTOR_INFO"));
		LocalizeItemText(m_htiDownloadInspectorDisable, _T("DOWNLOAD_INSPECTOR_DISABLE"));
		LocalizeItemInfoText(m_htiDownloadInspectorDisable, _T("DOWNLOAD_INSPECTOR_DISABLE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorLogOnly, _T("DOWNLOAD_INSPECTOR_LOG_ONLY"));
		LocalizeItemInfoText(m_htiDownloadInspectorLogOnly, _T("DOWNLOAD_INSPECTOR_LOG_ONLY_INFO"));
		LocalizeItemText(m_htiDownloadInspectorDelete, _T("DOWNLOAD_INSPECTOR_DELETE"));
		LocalizeItemInfoText(m_htiDownloadInspectorDelete, _T("DOWNLOAD_INSPECTOR_DELETE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorFake, _T("DOWNLOAD_INSPECTOR_INCLUDE_FAKE"));
		LocalizeItemInfoText(m_htiDownloadInspectorFake, _T("DOWNLOAD_INSPECTOR_INCLUDE_FAKE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorDRM, _T("DOWNLOAD_INSPECTOR_INCLUDE_DRM"));
		LocalizeItemInfoText(m_htiDownloadInspectorDRM, _T("DOWNLOAD_INSPECTOR_INCLUDE_DRM_INFO"));
		LocalizeItemText(m_htiDownloadInspectorInvalidExt, _T("REPLACE_INVALID_FILE_EXTENSION"));
		LocalizeItemInfoText(m_htiDownloadInspectorInvalidExt, _T("REPLACE_INVALID_FILE_EXTENSION_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoRenameToMajorityName, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoRenameToMajorityName, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_NEW_DOWNLOADS_ONLY"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_NEW_DOWNLOADS_ONLY_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent, _T("PERCENT_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_REQUIRED_PERCENT_THRESHOLD_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_MINIMUM_VOTES"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes, _T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME_MINIMUM_VOTES_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDelete, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDelete, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteAddedBefore, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_ADDED_BEFORE"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteAddedBefore, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_ADDED_BEFORE_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoDeleteAddedBeforeDays, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DAYS_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteAddedBeforeDays, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_ADDED_BEFORE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_SEEN_COMPLETE_BEFORE"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_SEEN_COMPLETE_BEFORE_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DAYS_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_SEEN_COMPLETE_BEFORE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteLastReceivedBefore, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_RECEIVED_BEFORE"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteLastReceivedBefore, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_RECEIVED_BEFORE_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DAYS_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LAST_RECEIVED_BEFORE_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_PERCENT"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_PERCENT_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue, _T("PERCENT_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_PERCENT_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_MB"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_MB_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_MB_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DOWNLOADED_LESS_THAN_MB_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteBackupEd2kLinks, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_BACKUP_ED2K"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteBackupEd2kLinks, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_BACKUP_ED2K_INFO"));
		LocalizeItemText(m_htiDownloadInspectorAutoDeleteDontMarkAsCanceled, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DONT_MARK_AS_CANCELED"));
		LocalizeItemInfoText(m_htiDownloadInspectorAutoDeleteDontMarkAsCanceled, _T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DONT_MARK_AS_CANCELED_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorCheckPeriod, _T("DOWNLOAD_INSPECTOR_CHECK_PERIOD"));
		LocalizeItemInfoText(m_htiDownloadInspectorCheckPeriod, _T("DOWNLOAD_INSPECTOR_CHECK_PERIOD_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorCompletedThreshold, _T("DOWNLOAD_INSPECTOR_DATA_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorCompletedThreshold, _T("DOWNLOAD_INSPECTOR_DATA_THRESHOLD_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorZeroPercentageThreshold, _T("DOWNLOAD_INSPECTOR_ZERO_PERCENTAGE_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorZeroPercentageThreshold, _T("DOWNLOAD_INSPECTOR_ZERO_PERCENTAGE_THRESHOLD_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorCompressionThreshold, _T("DOWNLOAD_INSPECTOR_COMPRESSION_THRESHOLD"));
		LocalizeItemInfoText(m_htiDownloadInspectorCompressionThreshold, _T("DOWNLOAD_INSPECTOR_COMPRESSION_THRESHOLD_INFO"));
		LocalizeItemText(m_htiDownloadInspectorBypassZeroPercentage, _T("DOWNLOAD_INSPECTOR_BYPASS_ZERO_PERCENTAGE"));
		LocalizeItemInfoText(m_htiDownloadInspectorBypassZeroPercentage, _T("DOWNLOAD_INSPECTOR_BYPASS_ZERO_PERCENTAGE_INFO"));
		LocalizeEditLabel(m_htiDownloadInspectorCompressionThresholdToBypassZero, _T("DOWNLOAD_INSPECTOR_COMPRESSION_THRESHOLD_TO_BYPASS_ZERO"));
		LocalizeItemInfoText(m_htiDownloadInspectorCompressionThresholdToBypassZero, _T("DOWNLOAD_INSPECTOR_COMPRESSION_THRESHOLD_TO_BYPASS_ZERO_INFO"));

		LocalizeItemText(m_htiSearchTweaksGroup, _T("SEARCH_TWEAKS"));
		LocalizeItemInfoText(m_htiSearchTweaksGroup, _T("SEARCH_TWEAKS_INFO"));
		LocalizeItemText(m_htiGroupKnownAtTheBottom, _T("GROUP_KNOWN_AT_THE_BOTTOM"));
		LocalizeItemInfoText(m_htiGroupKnownAtTheBottom, _T("GROUP_KNOWN_AT_THE_BOTTOM_INFO"));
		LocalizeEditLabel(m_htiSpamThreshold, _T("SPAM_THRESHOLD"));
		LocalizeItemInfoText(m_htiSpamThreshold, _T("SPAM_THRESHOLD_INFO"));
		LocalizeEditLabel(m_htiEd2kSearchMaxResults, _T("ED2K_SEARCH_MAX_RESULTS"));
		LocalizeItemInfoText(m_htiEd2kSearchMaxResults, _T("ED2K_SEARCH_MAX_RESULTS_INFO"));
		LocalizeEditLabel(m_htiEd2kSearchMaxMoreRequests, _T("ED2K_SEARCH_MAX_MORE_REQUESTS"));
		LocalizeItemInfoText(m_htiEd2kSearchMaxMoreRequests, _T("ED2K_SEARCH_MAX_MORE_REQUESTS_INFO"));
		LocalizeEditLabel(m_htiKadFileSearchTotal, _T("KAD_FILE_SEARCH_TOTAL"));
		LocalizeItemInfoText(m_htiKadFileSearchTotal, _T("KAD_FILE_SEARCH_TOTAL_INFO"));
		LocalizeEditLabel(m_htiKadSearchKeywordTotal, _T("KAD_SEARCH_KEYWORD_TOTAL"));
		LocalizeItemInfoText(m_htiKadSearchKeywordTotal, _T("KAD_SEARCH_KEYWORD_TOTAL_INFO"));
		LocalizeEditLabel(m_htiKadFileSearchLifetime, _T("KAD_FILE_SEARCH_LIFETIME"));
		LocalizeItemInfoText(m_htiKadFileSearchLifetime, _T("KAD_FILE_SEARCH_LIFETIME_INFO"));
		LocalizeEditLabel(m_htiKadSearchKeywordLifetime, _T("KAD_SEARCH_KEYWORD_LIFETIME"));
		LocalizeItemInfoText(m_htiKadSearchKeywordLifetime, _T("KAD_SEARCH_KEYWORD_LIFETIME_INFO"));
		LocalizeItemText(m_htiShowCloseButtonOnSearchTabs, _T("SHOW_CLOSE_BUTTON_ON_SEARCH_TABS"));
		LocalizeItemInfoText(m_htiShowCloseButtonOnSearchTabs, _T("SHOW_CLOSE_BUTTON_ON_SEARCH_TABS_INFO"));

		LocalizeItemText(m_htiServerTweaksGroup, _T("ESERVER_TWEAKS"));
		LocalizeItemInfoText(m_htiServerTweaksGroup, _T("ESERVER_TWEAKS_INFO"));
		LocalizeItemText(m_htiRepeatServerList, _T("REPEAT_SERVER_LIST"));
		LocalizeItemInfoText(m_htiRepeatServerList, _T("REPEAT_SERVER_LIST_INFO"));
		LocalizeItemText(m_htiDontRemoveStaticServers, _T("DONT_REMOVE_STATIC_SERVERS"));
		LocalizeItemInfoText(m_htiDontRemoveStaticServers, _T("DONT_REMOVE_STATIC_SERVERS_INFO"));
		LocalizeItemText(m_htiDontSavePartOnReconnect, _T("DONT_SAVE_PART_ON_RECONNECT"));
		LocalizeItemInfoText(m_htiDontSavePartOnReconnect, _T("DONT_SAVE_PART_ON_RECONNECT_INFO"));

		LocalizeItemText(m_htiFileTweaks, _T("FILE_TWEAKS"));
		LocalizeItemInfoText(m_htiFileTweaks, _T("FILE_TWEAKS_INFO"));
		LocalizeItemText(m_htiFileHistoryShowPart, _T("FILE_HISTORY_SHOW_PART"));
		LocalizeItemInfoText(m_htiFileHistoryShowPart, _T("FILE_HISTORY_SHOW_PART_INFO"));
		LocalizeItemText(m_htiFileHistoryShowShared, _T("FILE_HISTORY_SHOW_SHARED"));
		LocalizeItemInfoText(m_htiFileHistoryShowShared, _T("FILE_HISTORY_SHOW_SHARED_INFO"));
		LocalizeItemText(m_htiShareTweaks, _T("SHARE_TWEAKS"));
		LocalizeItemInfoText(m_htiShareTweaks, _T("SHARE_TWEAKS_INFO"));
		LocalizeItemText(m_htiFileHistoryShowDuplicate, _T("FILE_HISTORY_SHOW_DUPLICATE"));
		LocalizeItemInfoText(m_htiFileHistoryShowDuplicate, _T("FILE_HISTORY_SHOW_DUPLICATE_INFO"));
		LocalizeItemText(m_htiAutoShareSubdirs, _T("AUTO_SHARE_SUBDIRS"));
		LocalizeItemInfoText(m_htiAutoShareSubdirs, _T("AUTO_SHARE_SUBDIRS_INFO"));
		LocalizeItemText(m_htiDontShareExtensions, _T("DONT_SHARE_EXTENSIONS"));
		LocalizeItemInfoText(m_htiDontShareExtensions, _T("DONT_SHARE_EXTENSIONS_INFO"));
		LocalizeEditLabel(m_htiDontShareExtensionsList, _T("DONT_SHARE_EXTENSIONS_LIST"));
		LocalizeItemInfoText(m_htiDontShareExtensionsList, _T("DONT_SHARE_EXTENSIONS_LIST_INFO"));
		LocalizeItemText(m_htiSpreadbarSetStatus, _T("SPREADBAR_DEFAULT_CHECKBOX"));
		LocalizeItemInfoText(m_htiSpreadbarSetStatus, _T("SPREADBAR_DEFAULT_CHECKBOX_INFO"));
		LocalizeEditLabel(m_htiHideOvershares, _T("HIDEOVERSHARES"));
		LocalizeItemInfoText(m_htiHideOvershares, _T("HIDEOVERSHARES_INFO"));
		LocalizeItemText(m_htiSelectiveShare, _T("SELECTIVESHARE"));
		LocalizeItemInfoText(m_htiSelectiveShare, _T("SELECTIVESHARE_INFO"));
		LocalizeItemText(m_htiShareOnlyTheNeed, _T("SHAREONLYTHENEED"));
		LocalizeItemInfoText(m_htiShareOnlyTheNeed, _T("SHAREONLYTHENEED_INFO"));
		LocalizeItemText(m_htiPowerShareGroup, _T("POWERSHARE"));
		LocalizeItemInfoText(m_htiPowerShareGroup, _T("POWERSHARE_INFO"));
		LocalizeItemText(m_htiPowerShareDisabled, _T("POWERSHARE_DISABLED"));
		LocalizeItemInfoText(m_htiPowerShareDisabled, _T("POWERSHARE_DISABLED_INFO"));
		LocalizeItemText(m_htiPowerShareActivated, _T("POWERSHARE_ACTIVATED"));
		LocalizeItemInfoText(m_htiPowerShareActivated, _T("POWERSHARE_ACTIVATED_INFO"));
		LocalizeItemText(m_htiPowerShareAuto, _T("POWERSHARE_AUTO"));
		LocalizeItemInfoText(m_htiPowerShareAuto, _T("POWERSHARE_AUTO_INFO"));
		LocalizeItemText(m_htiPowerShareLimited, _T("POWERSHARE_LIMITED"));
		LocalizeItemInfoText(m_htiPowerShareLimited, _T("POWERSHARE_LIMITED_INFO"));
		LocalizeEditLabel(m_htiPowerShareLimit, _T("POWERSHARE_LIMIT"));
		LocalizeItemInfoText(m_htiPowerShareLimit, _T("POWERSHARE_LIMIT_INFO"));
		LocalizeItemText(m_htiPowerShareInternalPrio, _T("POWERSHARE_INTERPRIO"));
		LocalizeItemInfoText(m_htiPowerShareInternalPrio, _T("POWERSHARE_INTERPRIO_INFO"));
		LocalizeItemText(m_htiPermissionsGroup, _T("SHARE_PERMISSION_GROUP"));
		LocalizeItemInfoText(m_htiPermissionsGroup, _T("SHARE_PERMISSION_GROUP_INFO"));
		LocalizeItemText(m_htiPermissionColorRows, _T("SHARE_PERMISSION_COLOR_ROWS"));
		LocalizeItemInfoText(m_htiPermissionColorRows, _T("SHARE_PERMISSION_COLOR_ROWS_INFO"));
		LocalizeItemText(m_htiPermissionEverybody, _T("SHARE_PERMISSION_EVERYBODY"));
		LocalizeItemInfoText(m_htiPermissionEverybody, _T("SHARE_PERMISSION_EVERYBODY_INFO"));
		LocalizeItemText(m_htiPermissionFriendsOnly, _T("SHARE_PERMISSION_FRIENDSONLY"));
		LocalizeItemInfoText(m_htiPermissionFriendsOnly, _T("SHARE_PERMISSION_FRIENDSONLY_INFO"));
		LocalizeItemText(m_htiPermissionHidden, _T("SHARE_PERMISSION_HIDDEN"));
		LocalizeItemInfoText(m_htiPermissionHidden, _T("SHARE_PERMISSION_HIDDEN_INFO"));
		LocalizeItemText(m_htiAdjustNTFSDaylightFileTime, _T("ADJUSTNTFSDAYLIGHTFILETIME"));
		LocalizeItemInfoText(m_htiAdjustNTFSDaylightFileTime, _T("ADJUSTNTFSDAYLIGHTFILETIME_INFO"));
		LocalizeItemText(m_htiAllowDSTTimeTolerance, _T("ALLOWDSTTIMETOLERANCE"));
		LocalizeItemInfoText(m_htiAllowDSTTimeTolerance, _T("ALLOWDSTTIMETOLERANCE_INFO"));

		LocalizeItemText(m_htiEmulator, _T("EMULATOR"));
		LocalizeItemInfoText(m_htiEmulator, _T("EMULATOR_INFO"));

		LocalizeItemText(m_htiEmulateMLDonkey, _T("EMULATE_MLDONKEY"));
		LocalizeItemInfoText(m_htiEmulateMLDonkey, _T("EMULATE_MLDONKEY_INFO"));

		LocalizeItemText(m_htiEmulateEdonkey, _T("EMULATE_EDONKEY"));
		LocalizeItemInfoText(m_htiEmulateEdonkey, _T("EMULATE_EDONKEY_INFO"));

		LocalizeItemText(m_htiEmulateEdonkeyHybrid, _T("EMULATE_EDONKEYHYBRID"));
		LocalizeItemInfoText(m_htiEmulateEdonkeyHybrid, _T("EMULATE_EDONKEYHYBRID_INFO"));

		LocalizeItemText(m_htiEmulateShareaza, _T("EMULATE_SHAREAZA2"));
		LocalizeItemInfoText(m_htiEmulateShareaza, _T("EMULATE_SHAREAZA2_INFO"));

		LocalizeItemText(m_htiEmulateLphant, _T("EMULATE_LPHANT"));
		LocalizeItemInfoText(m_htiEmulateLphant, _T("EMULATE_LPHANT_INFO"));

		LocalizeItemText(m_htiEmulateCommunity, _T("EMULATE_COMMUNITY"));
		LocalizeItemInfoText(m_htiEmulateCommunity, _T("EMULATE_COMMUNITY_INFO"));

		LocalizeEditLabel(m_htiEmulateCommunityTagSavingTreshold, _T("EMULATE_COMMUNITY_TAG_SAVING_TRESHOLD"));
		LocalizeItemInfoText(m_htiEmulateCommunityTagSavingTreshold, _T("EMULATE_COMMUNITY_TAG_SAVING_TRESHOLD_INFO"));

		LocalizeItemText(m_htiLogEmulator, _T("LOG_EMULATOR"));
		LocalizeItemInfoText(m_htiLogEmulator, _T("LOG_EMULATOR_INFO"));

		LocalizeItemText(m_htiUseIntelligentChunkSelection, _T("USE_INTELLIGENT_CHUNK_SELECTION"));
		LocalizeItemInfoText(m_htiUseIntelligentChunkSelection, _T("USE_INTELLIGENT_CHUNK_SELECTION_INFO"));
	
		LocalizeItemText(m_htiCreditSystem, _T("CREDIT_SYSTEM"));
		LocalizeItemInfoText(m_htiCreditSystem, _T("CREDIT_SYSTEM_INFO"));
		LocalizeItemText(m_htiOfficialCredit, _T("OFFICIAL_CREDIT"));
		LocalizeItemInfoText(m_htiOfficialCredit, _T("OFFICIAL_CREDIT_INFO"));
		LocalizeItemText(m_htiLovelaceCredit, _T("LOVELACE_CREDIT"));
		LocalizeItemInfoText(m_htiLovelaceCredit, _T("LOVELACE_CREDIT_INFO"));
		LocalizeItemText(m_htiRatioCredit, _T("RATIO_CREDIT"));
		LocalizeItemInfoText(m_htiRatioCredit, _T("RATIO_CREDIT_INFO"));
		LocalizeItemText(m_htiPawcioCredit, _T("PAWCIO_CREDIT"));
		LocalizeItemInfoText(m_htiPawcioCredit, _T("PAWCIO_CREDIT_INFO"));
		LocalizeItemText(m_htiESCredit, _T("EASTSHARE_CREDIT"));
		LocalizeItemInfoText(m_htiESCredit, _T("EASTSHARE_CREDIT_INFO"));
		LocalizeItemText(m_htiMagicAngelCredit, _T("MAGICANGEL_CREDIT"));
		LocalizeItemInfoText(m_htiMagicAngelCredit, _T("MAGICANGEL_CREDIT_INFO"));
		LocalizeItemText(m_htiMagicAngelPlusCredit, _T("MAGICANGEL_PLUS_CREDIT"));
		LocalizeItemInfoText(m_htiMagicAngelPlusCredit, _T("MAGICANGEL_PLUS_CREDIT_INFO"));
		LocalizeItemText(m_htiSivkaCredit, _T("SIVKA_CREDIT"));
		LocalizeItemInfoText(m_htiSivkaCredit, _T("SIVKA_CREDIT_INFO"));
		LocalizeItemText(m_htiSwatCredit, _T("SWAT_CREDIT"));
		LocalizeItemInfoText(m_htiSwatCredit, _T("SWAT_CREDIT_INFO"));
		LocalizeItemText(m_htiTk4Credit, _T("TK4_CREDIT"));
		LocalizeItemInfoText(m_htiTk4Credit, _T("TK4_CREDIT_INFO"));
		LocalizeItemText(m_htiXtremeCredit, _T("XTREME_CREDIT"));
		LocalizeItemInfoText(m_htiXtremeCredit, _T("XTREME_CREDIT_INFO"));
		LocalizeItemText(m_htiZzulCredit, _T("ZZUL_CREDIT"));
		LocalizeItemInfoText(m_htiZzulCredit, _T("ZZUL_CREDIT_INFO"));

		LocalizeItemText(m_htiClientHistory, _T("CLIENT_HISTORY"));
		LocalizeItemInfoText(m_htiClientHistory, _T("CLIENT_HISTORY_INFO"));
		LocalizeItemText(m_htiClientHistoryActivate, _T("ACTIVATE_FEATURE"));
		LocalizeItemInfoText(m_htiClientHistoryActivate, _T("ACTIVATE_FEATURE_INFO"));
		LocalizeEditLabel(m_htiClientHistoryExpDays, _T("CLIENT_HISTORY_EXP_DAYS"));
		LocalizeItemInfoText(m_htiClientHistoryExpDays, _T("CLIENT_HISTORY_EXP_DAYS_INFO"));
		LocalizeItemText(m_htiClientHistoryLog, _T("CLIENT_HISTORY_LOG"));
		LocalizeItemInfoText(m_htiClientHistoryLog, _T("CLIENT_HISTORY_LOG_INFO"));

		LocalizeItemText(m_htiRemoteSharedFiles, _T("REMOTE_SF"));
		LocalizeItemInfoText(m_htiRemoteSharedFiles, _T("REMOTE_SF_INFO"));
		LocalizeItemText(m_htiRemoteSharedFilesUserHash, _T("REMOTE_SF_USER_HASH"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesUserHash, _T("REMOTE_SF_USER_HASH_INFO"));
		LocalizeItemText(m_htiRemoteSharedFilesClientNote, _T("REMOTE_SF_CLIENT_NOTE"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesClientNote, _T("REMOTE_SF_CLIENT_NOTE_INFO"));
		LocalizeEditLabel(m_htiRemoteSharedFilesAutoQueryPeriod, _T("REMOTE_SF_AUTO_QUERY_PERIOD"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesAutoQueryPeriod, _T("REMOTE_SF_AUTO_QUERY_PERIOD_INFO"));
		LocalizeEditLabel(m_htiRemoteSharedFilesAutoQueryMaxClients, _T("REMOTE_SF_AUTO_MAX_CLIENTS"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesAutoQueryMaxClients, _T("REMOTE_SF_AUTO_MAX_CLIENTS_INFO"));
		LocalizeEditLabel(m_htiRemoteSharedFilesAutoQueryClientPeriod, _T("REMOTE_SF_AUTO_QUERY_CLIENT_PERIOD"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesAutoQueryClientPeriod, _T("REMOTE_SF_AUTO_QUERY_CLIENT_PERIOD_INFO"));
		LocalizeItemText(m_htiRemoteSharedFilesSetAutoQueryDownload, _T("REMOTE_SF_AUTO_QUERY_DOWNLOAD"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesSetAutoQueryDownload, _T("REMOTE_SF_AUTO_QUERY_DOWNLOAD_INFO"));
		LocalizeEditLabel(m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold, _T("REMOTE_SF_AUTO_QUERY_DOWNLOAD_THRESHOLD"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold, _T("REMOTE_SF_AUTO_QUERY_DOWNLOAD_THRESHOLD_INFO"));
		LocalizeItemText(m_htiRemoteSharedFilesSetAutoQueryUpload, _T("REMOTE_SF_AUTO_QUERY_UPLOAD"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesSetAutoQueryUpload, _T("REMOTE_SF_AUTO_QUERY_UPLOAD_INFO"));
		LocalizeEditLabel(m_htiRemoteSharedFilesSetAutoQueryUploadThreshold, _T("REMOTE_SF_AUTO_QUERY_UPLOAD_THRESHOLD"));
		LocalizeItemInfoText(m_htiRemoteSharedFilesSetAutoQueryUploadThreshold, _T("REMOTE_SF_AUTO_QUERY_UPLOAD_THRESHOLD_INFO"));

		LocalizeItemText(m_htiSaveLoadSources, _T("SAVE_LOAD_SOURCES"));
		LocalizeItemInfoText(m_htiSaveLoadSources, _T("SAVE_LOAD_SOURCES_INFO"));
		LocalizeItemText(m_htiSaveLoadSourcesActivate, _T("ACTIVATE_FEATURE"));
		LocalizeItemInfoText(m_htiSaveLoadSourcesActivate, _T("ACTIVATE_FEATURE_INFO"));
		LocalizeEditLabel(m_htiSaveLoadSourcesMaxSources, _T("SAVE_LOAD_SOURCES_MAX_SOURCES"));
		LocalizeItemInfoText(m_htiSaveLoadSourcesMaxSources, _T("SAVE_LOAD_SOURCES_MAX_SOURCES_INFO"));
		LocalizeEditLabel(m_htiSaveLoadSourcesExpirationDays, _T("SAVE_LOAD_SOURCES_EXP_DAYS"));
		LocalizeItemInfoText(m_htiSaveLoadSourcesExpirationDays, _T("SAVE_LOAD_SOURCES_EXP_DAYS_INFO"));

		LocalizeItemText(m_htiMetControl, _T("MET_FILE_CONTROL"));
		LocalizeItemInfoText(m_htiMetControl, _T("MET_FILE_CONTROL_INFO"));
		LocalizeItemText(m_htiDontPurge, _T("PURGE_DONT"));
		LocalizeItemInfoText(m_htiDontPurge, _T("PURGE_DONT_INFO"));
		LocalizeItemText(m_htiPartiallyPurge, _T("PURGE_PARTIALLY"));
		LocalizeItemInfoText(m_htiPartiallyPurge, _T("PURGE_PARTIALLY_INFO"));
		LocalizeItemText(m_htiCompletelyPurge, _T("PURGE_COMPLETLY"));
		LocalizeItemInfoText(m_htiCompletelyPurge, _T("PURGE_COMPLETLY_INFO"));
		LocalizeEditLabel(m_htiKnownMet, _T("EXPIRED_KNOWN"));
		LocalizeItemInfoText(m_htiKnownMet, _T("EXPIRED_KNOWN_INFO"));
		LocalizeItemText(m_htiRemoveAichImmediately, _T("REMOVE_AICH_IMMEDIATELY"));
		LocalizeItemInfoText(m_htiRemoveAichImmediately, _T("REMOVE_AICH_IMMEDIATELY_INFO"));
		LocalizeEditLabel(m_htiClientsExp, _T("EXPIRED_CLIENTS"));
		LocalizeItemInfoText(m_htiClientsExp, _T("EXPIRED_CLIENTS_INFO"));

		LocalizeItemText(m_htiBackup, _T("BACKUP"));
		LocalizeItemInfoText(m_htiBackup, _T("BACKUP_INFO"));
		LocalizeItemText(m_htiBackupOnExit, _T("BACKUP_ON_EXIT"));
		LocalizeItemInfoText(m_htiBackupOnExit, _T("BACKUP_ON_EXIT_INFO"));
		LocalizeItemText(m_htiBackupPeriodic, _T("BACKUP_PERIODIC"));
		LocalizeItemInfoText(m_htiBackupPeriodic, _T("BACKUP_PERIODIC_INFO"));
		LocalizeEditLabel(m_htiBackupPeriod, _T("BACKUP_PERIOD"));
		LocalizeItemInfoText(m_htiBackupPeriod, _T("BACKUP_PERIOD_INFO"));
		LocalizeEditLabel(m_htiBackupMax, _T("BACKUP_MAX"));
		LocalizeItemInfoText(m_htiBackupMax, _T("BACKUP_MAX_INFO"));
		LocalizeItemText(m_htiBackupCompressed, _T("BACKUP_COMPRESS"));
		LocalizeItemInfoText(m_htiBackupCompressed, _T("BACKUP_COMPRESS_INFO"));

		LocalizeItemText(m_htiAdvancedPreferences, _T("ADVANCEDPREFS"));
		LocalizeItemInfoText(m_htiAdvancedPreferences, _T("ADVANCEDPREFS_INFO"));

		LocalizeItemText(m_htiMiniMule, _T("MINIMULE"));
		LocalizeItemInfoText(m_htiMiniMule, _T("MINIMULE_INFO"));

		LocalizeItemText(m_htiMiniMuleAutoClose, _T("MINIMULEAUTOCLOSE"));
		LocalizeItemInfoText(m_htiMiniMuleAutoClose, _T("MINIMULEAUTOCLOSE_INFO"));

		LocalizeEditLabel(m_htiMiniMuleTransparency, _T("MINIMULETRANSPARENCY"));
		LocalizeItemInfoText(m_htiMiniMuleTransparency, _T("MINIMULETRANSPARENCY_INFO"));

		LocalizeItemText(m_htiDisplay, _T("PW_DISPLAY"));
		LocalizeItemInfoText(m_htiDisplay, _T("PW_DISPLAY_INFO"));

		LocalizeItemText(m_htiRestoreLastMainWndDlg, _T("RESTORELASTMAINWNDDLG"));
		LocalizeItemInfoText(m_htiRestoreLastMainWndDlg, _T("RESTORELASTMAINWNDDLG_INFO"));

		LocalizeItemText(m_htiRestoreLastLogPane, _T("RESTORELASTLOGPANE"));
		LocalizeItemInfoText(m_htiRestoreLastLogPane, _T("RESTORELASTLOGPANE_INFO"));

		LocalizeItemText(m_htiStraightWindowStyles, _T("STRAIGHTWINDOWSTYLES"));
		LocalizeItemInfoText(m_htiStraightWindowStyles, _T("STRAIGHTWINDOWSTYLES_INFO"));

		LocalizeItemText(m_htiRTLWindowsLayout, _T("RTLWINDOWSLAYOUT"));
		LocalizeItemInfoText(m_htiRTLWindowsLayout, _T("RTLWINDOWSLAYOUT_INFO"));

		LocalizeItemText(m_htiShowActiveDownloadsBold, _T("ACTIVEDOWNLOADSBOLD"));
		LocalizeItemInfoText(m_htiShowActiveDownloadsBold, _T("ACTIVEDOWNLOADSBOLD_INFO"));

		LocalizeEditLabel(m_htiMaxChatHistory, _T("MAXCHATHISTORY"));
		LocalizeItemInfoText(m_htiMaxChatHistory, _T("MAXCHATHISTORY_INFO"));

		LocalizeEditLabel(m_htiMaxMsgSessions, _T("MAXMSGSESSIONS"));
		LocalizeItemInfoText(m_htiMaxMsgSessions, _T("MAXMSGSESSIONS_INFO"));

		LocalizeEditLabel(m_htiDateTimeFormat, _T("DATETIMEFORMAT"));
		LocalizeItemInfoText(m_htiDateTimeFormat, _T("DATETIMEFORMAT_INFO"));

		LocalizeEditLabel(m_htiDateTimeFormat4list, _T("DATETIMEFORMAT4LIST"));
		LocalizeItemInfoText(m_htiDateTimeFormat4list, _T("DATETIMEFORMAT4LIST_INFO"));

		LocalizeItemText(m_htiShowVerticalHourMarkers, _T("SHOWVERTICALHOURMARKERS"));
		LocalizeItemInfoText(m_htiShowVerticalHourMarkers, _T("SHOWVERTICALHOURMARKERS_INFO"));

		LocalizeItemText(m_htiReBarToolbar, _T("REBARTOOLBAR"));
		LocalizeItemInfoText(m_htiReBarToolbar, _T("REBARTOOLBAR_INFO"));

		LocalizeItemText(m_htiIconflashOnNewMessage, _T("ICON_FLASH_ON_NEW_MESSAGE"));
		LocalizeItemInfoText(m_htiIconflashOnNewMessage, _T("ICON_FLASH_ON_NEW_MESSAGE_INFO"));

		LocalizeItemText(m_htiShowCopyEd2kLinkCmd, _T("SHOWCOPYED2KLINK"));
		LocalizeItemInfoText(m_htiShowCopyEd2kLinkCmd, _T("SHOWCOPYED2KLINK_INFO"));

		LocalizeItemText(m_htiUpdateQueue, _T("UPDATEQUEUE"));
		LocalizeItemInfoText(m_htiUpdateQueue, _T("UPDATEQUEUE_INFO"));

		LocalizeItemText(m_htiRepaint, _T("REPAINTGRAPHS"));
		LocalizeItemInfoText(m_htiRepaint, _T("REPAINTGRAPHS_INFO"));

		LocalizeItemText(m_htiShowUpDownIconInTaskbar, _T("SHOWUPDOWNICONINTASKBAR"));
		LocalizeItemInfoText(m_htiShowUpDownIconInTaskbar, _T("SHOWUPDOWNICONINTASKBAR_INFO"));


		LocalizeItemText(m_htiLog, _T("SV_LOG"));
		LocalizeItemInfoText(m_htiLog, _T("SV_LOG_INFO"));

		LocalizeEditLabel(m_htiMaxLogBuff, _T("MAXLOGBUFF"));
		LocalizeItemInfoText(m_htiMaxLogBuff, _T("MAXLOGBUFF_INFO"));

		LocalizeItemText(m_htiLogFileFormat, _T("LOGFILEFORMAT"));
		LocalizeItemInfoText(m_htiLogFileFormat, _T("LOGFILEFORMAT_INFO"));

		LocalizeEditLabel(m_htiDateTimeFormat4Log, _T("DATETIMEFORMAT4LOG"));
		LocalizeItemInfoText(m_htiDateTimeFormat4Log, _T("DATETIMEFORMAT4LOG_INFO"));

		LocalizeItemText(m_htiCreateCrashDump, _T("CREATECRASHDUMP"));
		LocalizeItemInfoText(m_htiCreateCrashDump, _T("CREATECRASHDUMP_INFO"));

		LocalizeItemText(m_htiIgnoreInstances, _T("IGNOREINSTANCES"));
		LocalizeItemInfoText(m_htiIgnoreInstances, _T("IGNOREINSTANCES_INFO"));

		LocalizeEditLabel(m_htiNotifierMailEncryptCertName, _T("NOTIFIERMAILENCRYPTCERTNAME"));
		LocalizeItemInfoText(m_htiNotifierMailEncryptCertName, _T("NOTIFIERMAILENCRYPTCERTNAME_INFO"));

		LocalizeItemText(m_htiPreviewSmallBlocks, _T("PREVIEWSMALLBLOCKS"));
		LocalizeItemInfoText(m_htiPreviewSmallBlocks, _T("PREVIEWSMALLBLOCKS_INFO"));

		LocalizeItemText(m_htiPreviewCopiedArchives, _T("PREVIEWCOPIEDARCHIVES"));
		LocalizeItemInfoText(m_htiPreviewCopiedArchives, _T("PREVIEWCOPIEDARCHIVES_INFO"));

		LocalizeItemText(m_htiPreviewOnIconDblClk, _T("PREVIEWONICONDBLCLK"));
		LocalizeItemInfoText(m_htiPreviewOnIconDblClk, _T("PREVIEWONICONDBLCLK_INFO"));

		LocalizeEditLabel(m_htiInternetSecurityZone, _T("INTERNETSECURITYZONE"));
		LocalizeItemInfoText(m_htiInternetSecurityZone, _T("INTERNETSECURITYZONE_INFO"));

		LocalizeEditLabel(m_htiTxtEditor, _T("TXTEDITOR"));
		LocalizeItemInfoText(m_htiTxtEditor, _T("TXTEDITOR_INFO"));

		LocalizeEditLabel(m_htiServerUDPPort, _T("SERVERUDPPORT"));
		LocalizeItemInfoText(m_htiServerUDPPort, _T("SERVERUDPPORT_INFO"));

		LocalizeItemText(m_htiRemoveFilesToBin, _T("REMOVEFILESTOBIN"));
		LocalizeItemInfoText(m_htiRemoveFilesToBin, _T("REMOVEFILESTOBIN_INFO"));

		LocalizeItemText(m_htiHighresTimer, _T("HIGHRESTIMER"));
		LocalizeItemInfoText(m_htiHighresTimer, _T("HIGHRESTIMER_INFO"));

		LocalizeItemText(m_htiTrustEveryHash, _T("TRUSTEVERYHASH"));
		LocalizeItemInfoText(m_htiTrustEveryHash, _T("TRUSTEVERYHASH_INFO"));

		LocalizeItemText(m_htiICH, _T("ICH"));
		LocalizeItemInfoText(m_htiICH, _T("ICH_INFO"));

		LocalizeItemText(m_htiPreferRestrictedOverUser, _T("PREFERRESTRICTEDOVERUSER"));
		LocalizeItemInfoText(m_htiPreferRestrictedOverUser, _T("PREFERRESTRICTEDOVERUSER_INFO"));

		LocalizeItemText(m_htiUseUserSortedServerList, _T("USEUSERSORTEDSERVERLIST"));
		LocalizeItemInfoText(m_htiUseUserSortedServerList, _T("USEUSERSORTEDSERVERLIST_INFO"));

		LocalizeEditLabel(m_htiWebFileUploadSizeLimitMB, _T("WEBFILEUPLOADSIZELIMITMB"));
		LocalizeItemInfoText(m_htiWebFileUploadSizeLimitMB, _T("WEBFILEUPLOADSIZELIMITMB_INFO"));

		LocalizeEditLabel(m_htiAllowedIPs, _T("ALLOWEDIPS"));
		LocalizeItemInfoText(m_htiAllowedIPs, _T("ALLOWEDIPS_INFO"));

		LocalizeEditLabel(m_htiDebugSearchResultDetailLevel, _T("DEBUGSEARCHDETAILLEVEL"));
		LocalizeItemInfoText(m_htiDebugSearchResultDetailLevel, _T("DEBUGSEARCHDETAILLEVEL_INFO"));

		LocalizeEditLabel(m_htiCryptTCPPaddingLength, _T("CRYPTTCPPADDINGLENGTH"));
		LocalizeItemInfoText(m_htiCryptTCPPaddingLength, _T("CRYPTTCPPADDINGLENGTH_INFO"));

		LocalizeItemText(m_htiDontCompressAvi, _T("DONTCOMPRESSAVI"));
		LocalizeItemInfoText(m_htiDontCompressAvi, _T("DONTCOMPRESSAVI_INFO"));

		LocalizeItemText(m_htiRearrangeKadSearchKeywords, _T("REARRANGEKADSEARCH"));
		LocalizeItemInfoText(m_htiRearrangeKadSearchKeywords, _T("REARRANGEKADSEARCH_INFO"));

		LocalizeItemText(m_htiBeeper, _T("PW_BEEP"));
		LocalizeItemInfoText(m_htiBeeper, _T("PW_BEEP_INFO"));

		LocalizeItemText(m_htiMsgOnlySec, _T("MSGONLYSEC"));
		LocalizeItemInfoText(m_htiMsgOnlySec, _T("MSGONLYSEC_INFO"));

		LocalizeItemText(m_htiKeepUnavailableFixedSharedDirs, _T("KEEPUNAVAILABLEFIXEDSHAREDDIRS"));
		LocalizeItemInfoText(m_htiKeepUnavailableFixedSharedDirs, _T("KEEPUNAVAILABLEFIXEDSHAREDDIRS_INFO"));
	}
}

void CPPgMod::OnDestroy()
{
	m_ctrlTreeOptions.DeleteAllItems();
	m_ctrlTreeOptions.DestroyWindow();
	m_bInitializedTreeOpts = false;

	m_htiUITweaks = NULL;
	m_htiUIDarkMode = NULL;
	m_htiUIDarkModeAuto = NULL;
	m_htiUIDarkModeOn = NULL;
	m_htiUIDarkModeOff = NULL;
	m_htiUITweaksSpeedGraph = NULL;
	m_htiShowDownloadCommandsToolbar = NULL;
	m_htiDisableFindAsYouType = NULL;
	m_htiUITweaksListUpdatePeriod = NULL;
	m_htiUITweaksMaxSortHistory = NULL;
	m_htiUITweaksServerMaxSortHistory = NULL;
	m_htiUITweaksSearchMaxSortHistory = NULL;
	m_htiUITweaksFilesMaxSortHistory = NULL;
	m_htiUITweaksDownloadMaxSortHistory = NULL;
	m_htiUITweaksDownloadClientsMaxSortHistory = NULL;
	m_htiUITweaksUploadMaxSortHistory = NULL;
	m_htiUITweaksQueueMaxSortHistory = NULL;
	m_htiUITweaksClientMaxSortHistory = NULL;
	m_htiUITweaksKadContactSortHistory = NULL;
	m_htiUITweaksKadSearchSortHistory = NULL;

	m_htiIPGeolocation = NULL;
	m_htiIPGeolocationNameDisable = NULL;
	m_htiIPGeolocationCountryCode = NULL;
	m_htiIPGeolocationCountry = NULL;
	m_htiIPGeolocationCountryCity = NULL;
	m_htiIPGeolocationShowFlag = NULL;
	m_htiIPGeolocationAutoUpdate = NULL;
	m_htiIPGeolocationUpdateURL = NULL;
	m_htiIPGeolocationUpdateNow = NULL;
	m_htiIPGeolocationPeriodDays = NULL;

	m_htiConTweaks = NULL;

	m_htiUploadSettings = NULL;
	m_htiHighBandwidthUploadPolicy = NULL;
	m_htiHighBandwidthTargetUploadClients = NULL;
	m_htiHighBandwidthUploadSlotElasticPercent = NULL;
	m_htiAutoHighBandwidthDownloadBuffer = NULL;
	m_htiHighBandwidthSlowUploadThresholdFactor = NULL;
	m_htiHighBandwidthSlowUploadGraceSeconds = NULL;
	m_htiHighBandwidthSlowUploadWarmupSeconds = NULL;
	m_htiHighBandwidthZeroUploadGraceSeconds = NULL;
	m_htiHighBandwidthSlowUploadCooldownSeconds = NULL;
	m_htiLowRatioQueueScoreBoost = NULL;
	m_htiLowRatioQueueScoreThreshold = NULL;
	m_htiLowRatioQueueScoreBonus = NULL;
	m_htiUploadSessionTransferLimit = NULL;
	m_htiUploadSessionTransferLimitDisabled = NULL;
	m_htiUploadSessionTransferLimitPercent = NULL;
	m_htiUploadSessionTransferLimitPercentValue = NULL;
	m_htiUploadSessionTransferLimitMiB = NULL;
	m_htiUploadSessionTransferLimitMiBValue = NULL;
	m_htiUploadSessionTimeLimitSeconds = NULL;

	m_htiConnectionChecker = NULL;
	m_htiConnectionCheckerActivate = NULL;
	m_htiConnectionCheckerServer = NULL;
	
	m_htiEnableNatTraversal = NULL;
	m_htiNatTraversalProtocol = NULL;
	m_htiNatTraversalProtocolPreferQuic = NULL;
	m_htiNatTraversalProtocolUtpOnly = NULL;
	m_htiNatTraversalPortWindow = NULL;
	m_htiNatTraversalSweepWindow = NULL;
	m_htiNatTraversalJitterMinMs = NULL;
	m_htiNatTraversalJitterMaxMs = NULL;

	m_htiMaxServedBuddies = NULL;

	m_htiRetryFailedTcpConnectionAttempts = NULL;

	m_htiIsreaskSourceAfterIPChange = NULL;
	m_htiInformQueuedClientsAfterIPChange = NULL;

	m_htiReAskFileSrc = NULL;

	m_htiDownloadValidator = NULL;
	m_htiDownloadValidatorPassive = NULL;
	m_htiDownloadValidatorAlwaysAsk = NULL;
	m_htiDownloadValidatorReject = NULL;
	m_htiDownloadValidatorAccept = NULL;
	m_htiDownloadValidatorAcceptPercentage = NULL;
	m_htiDownloadValidatorRejectCanceled = NULL;
	m_htiDownloadValidatorRejectSameHash = NULL;
	m_htiDownloadValidatorRejectBlacklisted = NULL;
	m_htiDownloadValidatorCaseInsensitive = NULL;
	m_htiDownloadValidatorIgnoreExtension = NULL;
	m_htiDownloadValidatorIgnoreTags = NULL;
	m_htiDownloadValidatorDontIgnoreNumericTags = NULL;
	m_htiDownloadValidatorIgnoreNonAlphaNumeric = NULL;
	m_htiDownloadValidatorMinimumComparisonLength = NULL;
	m_htiDownloadValidatorSkipIncompleteFileConfirmation = NULL;
	m_htiDownloadValidatorMarkAsBlacklisted = NULL;
	m_htiDownloadValidatorAutoMarkAsBlacklisted = NULL;
	m_htiDownloadValidatorDateTimeMatching = NULL;
	m_htiDownloadValidatorDateTimeUseYearRange = NULL;
	m_htiDownloadValidatorDateTimeYearStart = NULL;
	m_htiDownloadValidatorDateTimeYearEnd = NULL;
	m_htiDownloadValidatorDateTimeCheckSeconds = NULL;
	m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues = NULL;

	m_htiDownloadInspector = NULL;
	m_htiDownloadInspectorDisable = NULL;
	m_htiDownloadInspectorLogOnly = NULL;
	m_htiDownloadInspectorDelete = NULL;
	m_htiDownloadInspectorFake = NULL;
	m_htiDownloadInspectorDRM = NULL;
	m_htiDownloadInspectorInvalidExt = NULL;
	m_htiDownloadInspectorAutoRenameToMajorityName = NULL;
	m_htiDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly = NULL;
	m_htiDownloadInspectorAutoRenameToMajorityNameRequiredPercent = NULL;
	m_htiDownloadInspectorAutoRenameToMajorityNameMinimumVotes = NULL;
	m_htiDownloadInspectorAutoDelete = NULL;
	m_htiDownloadInspectorAutoDeleteAddedBefore = NULL;
	m_htiDownloadInspectorAutoDeleteAddedBeforeDays = NULL;
	m_htiDownloadInspectorAutoDeleteLastSeenCompleteBefore = NULL;
	m_htiDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays = NULL;
	m_htiDownloadInspectorAutoDeleteLastReceivedBefore = NULL;
	m_htiDownloadInspectorAutoDeleteLastReceivedBeforeDays = NULL;
	m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercent = NULL;
	m_htiDownloadInspectorAutoDeleteDownloadedLessThanPercentValue = NULL;
	m_htiDownloadInspectorAutoDeleteDownloadedLessThanMb = NULL;
	m_htiDownloadInspectorAutoDeleteDownloadedLessThanMbValue = NULL;
	m_htiDownloadInspectorAutoDeleteBackupEd2kLinks = NULL;
	m_htiDownloadInspectorAutoDeleteDontMarkAsCanceled = NULL;
	m_htiDownloadInspectorCheckPeriod = NULL;
	m_htiDownloadInspectorCompletedThreshold = NULL;
	m_htiDownloadInspectorZeroPercentageThreshold = NULL;
	m_htiDownloadInspectorCompressionThreshold = NULL;
	m_htiDownloadInspectorBypassZeroPercentage = NULL;
	m_htiDownloadInspectorCompressionThresholdToBypassZero = NULL;
	m_iDownloadInspector = NULL;
	m_bDownloadInspectorFake = NULL;
	m_bDownloadInspectorDRM = NULL;
	m_bDownloadInspectorInvalidExt = NULL;
	m_bDownloadInspectorAutoRenameToMajorityName = NULL;
	m_bDownloadInspectorAutoRenameToMajorityNameForNewDownloadsOnly = NULL;
	m_iDownloadInspectorAutoRenameToMajorityNameRequiredPercent = NULL;
	m_iDownloadInspectorAutoRenameToMajorityNameMinimumVotes = NULL;
	m_bDownloadInspectorAutoDeleteEnabled = NULL;
	m_bDownloadInspectorAutoDeleteAddedBeforeEnabled = NULL;
	m_iDownloadInspectorAutoDeleteAddedBeforeDays = NULL;
	m_bDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled = NULL;
	m_iDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays = NULL;
	m_bDownloadInspectorAutoDeleteLastReceivedBeforeEnabled = NULL;
	m_iDownloadInspectorAutoDeleteLastReceivedBeforeDays = NULL;
	m_bDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled = NULL;
	m_iDownloadInspectorAutoDeleteDownloadedLessThanPercent = NULL;
	m_bDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled = NULL;
	m_iDownloadInspectorAutoDeleteDownloadedLessThanMb = NULL;
	m_bDownloadInspectorAutoDeleteBackupEd2kLinks = NULL;
	m_iDownloadInspectorCheckPeriod = NULL;
	m_iDownloadInspectorCompletedThreshold = NULL;
	m_iDownloadInspectorZeroPercentageThreshold = NULL;
	m_iDownloadInspectorCompressionThreshold = NULL;
	m_bDownloadInspectorBypassZeroPercentage = NULL;
	m_iDownloadInspectorCompressionThresholdToBypassZero = NULL;

	m_htiSearchTweaksGroup = NULL;
	m_htiGroupKnownAtTheBottom = NULL;
	m_htiSpamThreshold = NULL;
	m_htiEd2kSearchMaxResults = NULL;
	m_htiEd2kSearchMaxMoreRequests = NULL;
	m_htiKadFileSearchTotal = NULL;
	m_htiKadSearchKeywordTotal = NULL;
	m_htiKadFileSearchLifetime = NULL;
	m_htiKadSearchKeywordLifetime = NULL;
	m_htiShowCloseButtonOnSearchTabs = NULL;

	m_htiServerTweaksGroup = NULL;
	m_htiRepeatServerList = NULL;
	m_htiDontRemoveStaticServers = NULL;
	m_htiDontSavePartOnReconnect = NULL;

	m_htiFileTweaks = NULL;
	m_htiFileHistoryShowPart = NULL;
	m_htiFileHistoryShowShared = NULL;
	m_htiShareTweaks = NULL;
	m_htiFileHistoryShowDuplicate = NULL;
	m_htiAutoShareSubdirs = NULL;
	m_htiDontShareExtensions = NULL;
	m_htiDontShareExtensionsList = NULL;
	m_htiSpreadbarSetStatus = NULL;
	m_htiHideOvershares = NULL;
	m_htiSelectiveShare = NULL;
	m_htiShareOnlyTheNeed = NULL;
	m_htiPowerShareGroup = NULL;
	m_htiPowerShareDisabled = NULL;
	m_htiPowerShareActivated = NULL;
	m_htiPowerShareAuto = NULL;
	m_htiPowerShareLimited = NULL;
	m_htiPowerShareLimit = NULL;
	m_htiPowerShareInternalPrio = NULL;
	m_htiPermissionsGroup = NULL;
	m_htiPermissionColorRows = NULL;
	m_htiPermissionEverybody = NULL;
	m_htiPermissionFriendsOnly = NULL;
	m_htiPermissionHidden = NULL;
	m_htiAdjustNTFSDaylightFileTime = NULL;
	m_htiAllowDSTTimeTolerance = NULL;

	m_htiEmulator = NULL;
	m_htiEmulateMLDonkey = NULL;
	m_htiEmulateEdonkey = NULL;
	m_htiEmulateEdonkeyHybrid = NULL;
	m_htiEmulateShareaza = NULL;
	m_htiEmulateLphant = NULL;
	m_htiEmulateCommunity = NULL;
	m_htiEmulateCommunityTagSavingTreshold = NULL;
	m_htiLogEmulator = NULL;

	m_htiUseIntelligentChunkSelection = NULL;

	m_htiCreditSystem = NULL;
	m_htiOfficialCredit = NULL;
	m_htiLovelaceCredit = NULL;
	m_htiRatioCredit = NULL;
	m_htiPawcioCredit = NULL;
	m_htiESCredit = NULL;
	m_htiMagicAngelCredit = NULL;
	m_htiMagicAngelPlusCredit = NULL;
	m_htiSivkaCredit = NULL;
	m_htiSwatCredit = NULL;
	m_htiTk4Credit = NULL;
	m_htiXtremeCredit = NULL;
	m_htiZzulCredit = NULL;

	m_htiClientHistory = NULL;
	m_htiClientHistoryActivate = NULL;
	m_htiClientHistoryExpDays = NULL;
	m_htiClientHistoryLog = NULL;

	m_htiRemoteSharedFiles = NULL;
	m_htiRemoteSharedFilesUserHash = NULL;
	m_htiRemoteSharedFilesClientNote = NULL;
	m_htiRemoteSharedFilesAutoQueryPeriod = NULL;
	m_htiRemoteSharedFilesAutoQueryMaxClients = NULL;
	m_htiRemoteSharedFilesAutoQueryClientPeriod = NULL;
	m_htiRemoteSharedFilesSetAutoQueryDownload = NULL;
	m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold = NULL;
	m_htiRemoteSharedFilesSetAutoQueryUpload = NULL;
	m_htiRemoteSharedFilesSetAutoQueryUploadThreshold = NULL;

	m_htiSaveLoadSourcesActivate = NULL;
	m_htiSaveLoadSourcesMaxSources = NULL;
	m_htiSaveLoadSourcesExpirationDays = NULL;

	m_htiMetControl = NULL;
	m_htiDontPurge = NULL;
	m_htiPartiallyPurge = NULL;
	m_htiCompletelyPurge = NULL;
	m_htiKnownMet = NULL;
	m_htiRemoveAichImmediately = NULL;
	m_htiClientsExp = NULL;

	m_htiBackup = NULL;
	m_htiBackupOnExit = NULL;
	m_htiBackupPeriodic = NULL;
	m_htiBackupPeriod = NULL;
	m_htiBackupMax = NULL;
	m_htiBackupCompressed = NULL;

	m_htiAllowedIPs = NULL;
	m_htiBeeper = NULL;
	m_htiCreateCrashDump = NULL;
	m_htiCryptTCPPaddingLength = NULL;
	m_htiDateTimeFormat = NULL;
	m_htiDateTimeFormat4Log = NULL;
	m_htiDateTimeFormat4list = NULL;
	m_htiDebugSearchResultDetailLevel = NULL;
	m_htiDisplay = NULL;
	m_htiDontCompressAvi = NULL;
	m_htiHighresTimer = NULL;
	m_htiICH = NULL;
	m_htiIconflashOnNewMessage = NULL;
	m_htiIgnoreInstances = NULL;
	m_htiInternetSecurityZone = NULL;
	m_htiKeepUnavailableFixedSharedDirs = NULL;
	m_htiLog = NULL;
	m_htiLogFileFormat = NULL;
	m_htiMaxChatHistory = NULL;
	m_htiMaxLogBuff = NULL;
	m_htiMaxMsgSessions = NULL;
	m_htiMiniMule = NULL;
	m_htiMiniMuleAutoClose = NULL;
	m_htiMiniMuleTransparency = NULL;
	m_htiMsgOnlySec = NULL;
	m_htiNotifierMailEncryptCertName = NULL;
	m_htiPreferRestrictedOverUser = NULL;
	m_htiPreviewCopiedArchives = NULL;
	m_htiPreviewOnIconDblClk = NULL;
	m_htiPreviewSmallBlocks = NULL;
	m_htiRTLWindowsLayout = NULL;
	m_htiReBarToolbar = NULL;
	m_htiRearrangeKadSearchKeywords = NULL;
	m_htiRemoveFilesToBin = NULL;
	m_htiRepaint = NULL;
	m_htiRestoreLastLogPane = NULL;
	m_htiRestoreLastMainWndDlg = NULL;
	m_htiServerUDPPort = NULL;
	m_htiShowCopyEd2kLinkCmd = NULL;
	m_htiShowUpDownIconInTaskbar = NULL;
	m_htiShowVerticalHourMarkers = NULL;
	m_htiStraightWindowStyles = NULL;
	m_htiShowActiveDownloadsBold = NULL;
	m_htiTrustEveryHash = NULL;
	m_htiTxtEditor = NULL;
	m_htiUpdateQueue = NULL;
	m_htiUseUserSortedServerList = NULL;
	m_htiWebFileUploadSizeLimitMB = NULL;

	CPropertyPage::OnDestroy();
}

LRESULT CPPgMod::OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam)
{
	if (wParam == IDC_MOD_OPTS) {
		TREEOPTSCTRLNOTIFY *pton = (TREEOPTSCTRLNOTIFY*)lParam;
		if (pton != NULL) {
			if (pton->hItem == m_htiIPGeolocationUpdateNow) {
				OnIPGeolocationUpdateNow();
				return TRUE;
			}
			if (pton->hItem == m_htiNatTraversalProtocolPreferQuic || pton->hItem == m_htiNatTraversalProtocolUtpOnly)
				UpdateNatTraversalProtocolUi();
			if (pton->hItem == m_htiDownloadValidatorDateTimeMatching)
				UpdateDownloadValidatorDateTimeUi();
		}
		SetModified();
	}
	return 0;
}

void CPPgMod::UpdateNatTraversalProtocolUi()
{
	if (!m_bInitializedTreeOpts || m_htiNatTraversalProtocol == NULL)
		return;

	m_ctrlTreeOptions.Invalidate(FALSE);
}

void CPPgMod::UpdateDownloadValidatorDateTimeUi()
{
	if (!m_bInitializedTreeOpts || m_htiDownloadValidatorDateTimeMatching == NULL)
		return;

	BOOL bDateTimeMatching = FALSE;
	m_ctrlTreeOptions.GetCheckBox(m_htiDownloadValidatorDateTimeMatching, bDateTimeMatching);

	if (m_htiDownloadValidatorDateTimeUseYearRange != NULL)
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDownloadValidatorDateTimeUseYearRange, bDateTimeMatching);
	if (m_htiDownloadValidatorDateTimeCheckSeconds != NULL)
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDownloadValidatorDateTimeCheckSeconds, bDateTimeMatching);
	if (m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues != NULL)
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, bDateTimeMatching);
	m_ctrlTreeOptions.Invalidate(FALSE);
}

void CPPgMod::OnHelp()
{
}

void CPPgMod::OnIPGeolocationUpdateNow()
{
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	if (!UpdateData(TRUE))
		return;

	m_sIPGeolocationUpdateURL.Trim();
	if (m_sIPGeolocationUpdateURL.IsEmpty()) {
		CDarkMode::MessageBox(GetResString(_T("IPGEOLOCATION_UPDATE_NO_URL")), MB_ICONWARNING);
		return;
	}

	(void)CIPGeolocation::UpdateIPGeolocationFromURL(m_sIPGeolocationUpdateURL, true);
}

BOOL CPPgMod::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (LOWORD(wParam) == IDC_IPGEOLOCATION_UPDATE_NOW) {
		OnIPGeolocationUpdateNow();
		return TRUE;
	}
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgMod::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

LRESULT CPPgMod::DrawTreeItemHelp(WPARAM wParam, LPARAM lParam)
{
	if (!IsWindowVisible())
		return 0;

	if (wParam == IDC_MOD_OPTS) {
		CString* sInfo = (CString*)lParam;
		SetDlgItemText(IDC_MOD_OPTS_INFO, *sInfo);
	}
	return FALSE;
}

HBRUSH CPPgMod::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	switch (nCtlColor)
	{
	case CTLCOLOR_STATIC:
		if (pWnd->GetSafeHwnd() == GetDlgItem(IDC_MOD_OPTS_INFO)->GetSafeHwnd()) {
			pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));	// Text font colour
			pDC->SetBkColor(RGB(230, 230, 230)); // Text background colour
			return CDarkMode::s_brHelpTextBackground;
		}
	default:
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}
}
