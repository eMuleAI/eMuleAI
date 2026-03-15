#pragma once
#include "PPgGeneral.h"
#include "PPgConnection.h"
#include "PPgServer.h"
#include "PPgDirectories.h"
#include "PPgFiles.h"
#include "PPgStats.h"
#include "PPgNotify.h"
#include "PPgIRC.h"
#include "PPgTweaks.h"
#include "eMuleAI/PPgMod.h"
#include "eMuleAI/PPgProtectionPanel.h"
#include "eMuleAI/PPgBlacklistPanel.h"
#include "PPgDisplay.h"
#include "PPgSecurity.h"
#include "PPgWebServer.h"
#include "PPgScheduler.h"
#include "PPgProxy.h"
#include "PPgMessages.h"
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
#include "PPgDebug.h"
#endif
#include "otherfunctions.h"
#include "TreePropSheet.h"

class CPreferencesDlg : public CTreePropSheet
{
	DECLARE_DYNAMIC(CPreferencesDlg)

	void LocalizeItemText(int i, LPCTSTR strid);
	bool InitSideBanner();
	void UpdateBannerLayout();
public:
	CPreferencesDlg();
	virtual BOOL OnInitDialog();

	CPPgGeneral		m_wndGeneral;
	CPPgConnection	m_wndConnection;
	CPPgServer		m_wndServer;
	CPPgDirectories	m_wndDirectories;
	CPPgFiles		m_wndFiles;
	CPPgStats		m_wndStats;
	CPPgNotify		m_wndNotify;
	CPPgIRC			m_wndIRC;
	CPPgTweaks		m_wndTweaks;
	CPPgDisplay		m_wndDisplay;
	CPPgSecurity	m_wndSecurity;
	CPPgWebServer	m_wndWebServer;
	CPPgScheduler	m_wndScheduler;
	CPPgProxy		m_wndProxy;
	CPPgMessages	m_wndMessages;
	CPPgMod			m_wndMod;
	CPPgProtectionPanel	m_wndProtectionPanel;
	CPPgBlacklistPanel m_wndBlacklistPanel;
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	CPPgDebug		m_wndDebug;
#endif

	void Localize();
	void SetStartPage(UINT uStartPageID)	{ m_pPshStartPage = MAKEINTRESOURCE(uStartPageID); };

	bool m_bApplyButtonClicked;

protected:
	LPCTSTR m_pPshStartPage;
	bool m_bSaveIniFile;
	CWnd* m_pBannerWnd;
	int m_nBannerWidth;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
};
