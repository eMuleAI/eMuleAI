//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "preferences.h"
#include "TreeOptionsCtrlEx.h"
#include "ToolTipCtrlX.h"

class CPPgProtectionPanel : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgProtectionPanel)

public:
	CPPgProtectionPanel();
	virtual ~CPPgProtectionPanel();

	enum { IDD = IDD_PPG_PROTECTION_PANEL }; // Dialog Data

protected:

	void LocalizeItemText(HTREEITEM item, LPCTSTR strid);
	void LocalizeItemInfoText(HTREEITEM item, LPCTSTR strid);
	void LocalizeEditLabel(HTREEITEM item, LPCTSTR strid);

	bool	m_bDontPunishFriends;
	int		m_iClientBanTime; // adjust ClientBanTime - Stulle
	int		m_iClientScoreReducingTime;
	bool	m_bDontAllowFileHotSwapping;
	int		m_iPunishmentCancelationScanPeriod;
	bool	m_bInformBadClients; // => Inform Leechers - sFrQlXeRt
	CString	m_strInformBadClientsText; // => Inform Leechers Text - evcz
	bool	m_bAntiUploadProtection;
	int		m_iAntiUploadProtectionLimit;
	bool	m_bUploaderPunishmentPrevention;
	int		m_iUploaderPunishmentPreventionLimit;
	int		m_iUploaderPunishmentPreventionCase;

	bool	m_bDetectModNames;
	bool	m_bDetectUserNames;
	bool	m_bDetectUserHashes;
	int		m_iHardLeecherPunishment;
	int		m_iSoftLeecherPunishment;
	bool	m_bBanBadKadNodes;
	bool	m_bBanWrongPackage;
	bool	m_bDetectAntiP2PBots;
	int		m_iAntiP2PBotsPunishment;
	bool	m_bDetectWrongTag;
	int		m_iWrongTagPunishment;
	bool	m_bDetectUnknownTag;
	int		m_iUnknownTagPunishment;
	bool	m_bDetectHashThief;
	int		m_iHashThiefPunishment;
	bool	m_bDetectModThief;
	int		m_iModThiefPunishment;
	bool	m_bDetectUserNameThief;
	int		m_iUserNameThiefPunishment;
	bool	m_bDetectModChanger;
	int 	m_iModChangerInterval;
	int 	m_iModChangerThreshold;
	int		m_iModChangerPunishment;
	bool	m_bDetectUserNameChanger;
	int 	m_iUserNameChangerInterval;
	int 	m_iUserNameChangerThreshold;
	int		m_iUserNameChangerPunishment;
	bool	m_bDetectTCPErrorFlooder;
	int 	m_iTCPErrorFlooderInterval;
	int 	m_iTCPErrorFlooderThreshold;
	int		m_iTCPErrorFlooderPunishment;
	bool	m_bDetectEmptyUserNameEmule;
	int		m_iEmptyUserNameEmulePunishment;
	bool	m_bDetectCommunity;
	int		m_iCommunityPunishment;
	bool	m_bDetectFakeEmule;
	int		m_iFakeEmulePunishment;
	bool	m_bDetectHexModName;
	int		m_iHexModNamePunishment;
	bool	m_bDetectGhostMod;
	int		m_iGhostModPunishment;
	bool	m_bDetectSpam;
	int		m_iSpamPunishment;
	bool	m_bDetectEmcrypt;
	int		m_iEmcryptPunishment;
	bool	m_bDetectXSExploiter;
	int		m_iXSExploiterPunishment;
	bool	m_bDetectFileFaker;
	int		m_iFileFakerPunishment;
	bool	m_bDetectUploadFaker;
	int		m_iUploadFakerPunishment;
	bool	m_bDetectUploadRequestAbuse;
	bool	m_bDetectUploadRequestAbuseNoRequestSlots;
	bool	m_bDetectUploadRequestAbuseQueueDrops;
	bool	m_bUploadRequestAbuseCounterGuard;
	bool	m_bUploadRequestAbuseHashRotationTracking;
	bool	m_bUploadRequestAbusePostHelloDisconnect;
	int		m_iUploadRequestAbusePunishment;

	bool	m_bDetectAgressive;
	int		m_iAgressiveTime;
	int		m_iAgressiveCounter;
	bool	m_bAgressiveLog;
	int		m_iAgressivePunishment;

	bool	m_bPunishNonSuiMlDonkey;
	bool	m_bPunishNonSuiEdonkey;
	bool	m_bPunishNonSuiEdonkeyHybrid;
	bool	m_bPunishNonSuiShareaza;
	bool	m_bPunishNonSuiLphant;
	bool	m_bPunishNonSuiAmule;
	bool	m_bPunishNonSuiEmule;
	int		m_iNonSuiPunishment;

	bool	m_bDetectCorruptedDataSender;
	bool	m_bDetectHashChanger;
	bool	m_bDetectFileScanner;
	bool	m_bDetectRankFlooder;
	bool	m_bDetectKadRequestFlooder;
	int		m_iKadRequestFloodBanTreshold;

	CTreeOptionsCtrlEx m_ctrlTreeOptions;
	CToolTipCtrlX m_tooltipTreeOptions;
	CString m_strTreeOptionsToolTip;
	bool m_bInitializedTreeOpts;

	HTREEITEM	m_htiGeneralOptions;
	HTREEITEM	m_htiTimingOptions;
	HTREEITEM	m_htiPunishmentCancelationScanPeriod;
	HTREEITEM	m_htiClientBanTime; // adjust ClientBanTime - Stulle
	HTREEITEM	m_htiClientScoreReducingTime;
	HTREEITEM	m_htiDontPunishFriends;
	HTREEITEM	m_htiDontAllowFileHotSwapping;
	HTREEITEM	m_htiInformBadClients; // => Inform Leechers - sFrQlXeRt
	HTREEITEM	m_htiInformBadClientsText; // => Inform Leechers Text - evcz
	HTREEITEM	m_htiAntiUploadProtection;
	HTREEITEM	m_htiAntiUploadProtectionLimit;
	HTREEITEM m_htiUploaderPunishmentPrevention;
	HTREEITEM m_htiUploaderPunishmentPreventionLimit;
	HTREEITEM m_htiAntiCase1;
	HTREEITEM m_htiAntiCase2;
	HTREEITEM m_htiAntiCase3;

	HTREEITEM	m_htiLeecherDetection;
	HTREEITEM	m_htiDetectModNames;
	HTREEITEM	m_htiDetectUserNames;
	HTREEITEM	m_htiDetectUserHashes;
	HTREEITEM	m_htiHardLeecherPunishment;
	HTREEITEM	m_htiSoftLeecherPunishment;
	HTREEITEM	m_htiIPUserHash;
	HTREEITEM	m_htiUserHashBan;
	HTREEITEM	m_htiUploadBan;
	HTREEITEM	m_htiScore01;
	HTREEITEM	m_htiScore02;
	HTREEITEM	m_htiScore03;
	HTREEITEM	m_htiScore04;
	HTREEITEM	m_htiScore05;
	HTREEITEM	m_htiScore06;
	HTREEITEM	m_htiScore07;
	HTREEITEM	m_htiScore08;
	HTREEITEM	m_htiScore09;
	HTREEITEM	m_htiNoPunishment;
	HTREEITEM	m_htiBanBadKadNodes;
	HTREEITEM	m_htiDetectAntiP2PBots;
	HTREEITEM	m_htiBanWrongPackage;
	HTREEITEM	m_htiAntiP2PBotsPunishment;
	HTREEITEM	m_htiDetectWrongTag;
	HTREEITEM	m_htiWrongTagPunishment;
	HTREEITEM	m_htiDetectUnknownTag;
	HTREEITEM	m_htiUnknownTagPunishment;
	HTREEITEM	m_htiDetectHashThief;
	HTREEITEM	m_htiHashThiefPunishment;
	HTREEITEM	m_htiDetectModThief;
	HTREEITEM	m_htiModThiefPunishment;
	HTREEITEM	m_htiDetectUserNameThief;
	HTREEITEM	m_htiUserNameThiefPunishment;
	HTREEITEM	m_htiDetectModChanger;
	HTREEITEM	m_htiModChangerInterval;
	HTREEITEM	m_htiModChangerThreshold;
	HTREEITEM	m_htiModChangerPunishment;
	HTREEITEM	m_htiDetectUserNameChanger;
	HTREEITEM	m_htiUserNameChangerInterval;
	HTREEITEM	m_htiUserNameChangerThreshold;
	HTREEITEM	m_htiUserNameChangerPunishment;
	HTREEITEM	m_htiDetectTCPErrorFlooder;
	HTREEITEM	m_htiTCPErrorFlooderInterval;
	HTREEITEM	m_htiTCPErrorFlooderThreshold;
	HTREEITEM	m_htiTCPErrorFlooderPunishment;
	HTREEITEM	m_htiDetectEmptyUserNameEmule;
	HTREEITEM	m_htiEmptyUserNameEmulePunishment;
	HTREEITEM	m_htiDetectCommunity;
	HTREEITEM	m_htiCommunityPunishment;
	HTREEITEM	m_htiDetectFakeEmule;
	HTREEITEM	m_htiFakeEmulePunishment;
	HTREEITEM	m_htiDetectHexModName;
	HTREEITEM	m_htiHexModNamePunishment;
	HTREEITEM	m_htiDetectGhostMod;
	HTREEITEM	m_htiGhostModPunishment;
	HTREEITEM	m_htiDetectSpam;
	HTREEITEM	m_htiSpamPunishment;
	HTREEITEM	m_htiDetectEmcrypt;
	HTREEITEM	m_htiEmcryptPunishment;
	HTREEITEM	m_htiDetectXSExploiter;
	HTREEITEM	m_htiXSExploiterPunishment;
	HTREEITEM	m_htiDetectFileFaker;
	HTREEITEM	m_htiFileFakerPunishment;
	HTREEITEM	m_htiDetectUploadFaker;
	HTREEITEM	m_htiUploadFakerPunishment;
	HTREEITEM	m_htiDetectUploadRequestAbuse;
	HTREEITEM	m_htiDetectUploadRequestAbuseNoRequestSlots;
	HTREEITEM	m_htiDetectUploadRequestAbuseQueueDrops;
	HTREEITEM	m_htiUploadRequestAbuseCounterGuard;
	HTREEITEM	m_htiUploadRequestAbuseHashRotationTracking;
	HTREEITEM	m_htiUploadRequestAbusePostHelloDisconnect;
	HTREEITEM	m_htiUploadRequestAbusePunishment;

	HTREEITEM	m_htiDetectAgressive;
	HTREEITEM	m_htiAgressiveTime;
	HTREEITEM	m_htiAgressiveCounter;
	HTREEITEM	m_htiAgressiveLog;
	HTREEITEM	m_htiAgressivePunishment;

	HTREEITEM	m_htiPunishNonSuiClients;
	HTREEITEM	m_htiPunishNonSuiMlDonkey;
	HTREEITEM	m_htiPunishNonSuiEdonkey;
	HTREEITEM	m_htiPunishNonSuiEdonkeyHybrid;
	HTREEITEM	m_htiPunishNonSuiShareaza;
	HTREEITEM	m_htiPunishNonSuiLphant;
	HTREEITEM	m_htiPunishNonSuiAmule;
	HTREEITEM	m_htiPunishNonSuiEmule;
	HTREEITEM	m_htiNonSuiPunishment;

	HTREEITEM	m_htiTweakOfficialFeatures;
	HTREEITEM	m_htiDetectCorruptedDataSender;
	HTREEITEM	m_htiDetectHashChanger;
	HTREEITEM	m_htiDetectFileScanner;
	HTREEITEM	m_htiDetectRankFlooder;
	HTREEITEM	m_htiDetectKadRequestFlooder;
	HTREEITEM	m_htiKadRequestFloodBanTreshold;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT DrawTreeItemHelp(WPARAM wParam, LPARAM lParam);
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	void UpdateLayout();
	void UpdateTreeOptionsToolTip();

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
public:
	void Localize(void);
	void ResetToDefaults();
	void LocalizeCommonItems(void);
	virtual BOOL OnApply();
	virtual BOOL OnInitDialog();
	virtual BOOL OnKillActive();
	afx_msg void OnSettingsChange()			{ SetModified(); }
	afx_msg void OnBnClickedShieldReload();
};