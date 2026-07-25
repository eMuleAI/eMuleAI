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
#include "eMuleAI/PPgDownloadValidator.h"
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
#include "ToolTipCtrlX.h"
#include <memory>
#include <vector>

class CPreferencesDlg : public CTreePropSheet
{
	DECLARE_DYNAMIC(CPreferencesDlg)

	void LocalizeItemText(int i, LPCTSTR strid);
	bool InitSideBanner();
	void UpdateBannerLayout();
	void RegisterActivePageToolTips(bool bUpdateExisting = false);
	bool ScalePreferencesButtons();
	bool CreateResetButton();
	void ResetActivePageToDefaults();
	void UpdateActiveTreeOptionToolTip(MSG* pMsg);
	CString BuildOptionToolTipText(CWnd* pPage, CWnd* pControl);
	UINT GetActivePageDialogId();
	UINT GetPageDialogId(const CPropertyPage* pPage) const;
	bool PrepareScaledPageTemplates();
	bool IsOptionToolTipWindowRegistered(HWND hWnd) const;
	void ClearModalReopenRequest();
	std::vector<std::unique_ptr<BYTE[]> > m_aScaledPageTemplates;
public:
	CPreferencesDlg();
	virtual INT_PTR DoModal();
	static int ScaleOptionsValue(int iValue);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	CPPgGeneral		m_wndGeneral;
	CPPgConnection	m_wndConnection;
	CPPgNetworkInterface	m_wndNetworkInterface;
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
	CPPgDownloadValidator m_wndDownloadValidator;
	CPPgProtectionPanel	m_wndProtectionPanel;
	CPPgBlacklistPanel m_wndBlacklistPanel;
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	CPPgDebug		m_wndDebug;
#endif

	void Localize();
	void SetStartPage(UINT uStartPageID)	{ m_uPshStartPageId = uStartPageID; };
	void SetOptionsToolTipsEnabled(bool bEnabled);
	void RefreshActivePageToolTips();
	void RequestOptionsWindowScaleRefresh()	{ m_bOptionsWindowScaleRefreshPending = true; }
	void RequestModalReopen(UINT uStartPageID);
	bool ConsumeModalReopenRequest(UINT& uStartPageID);
	bool GetOptionsToolTipsEnabled() const	{ return m_bShowOptionsToolTips; }

	bool m_bApplyButtonClicked;

protected:
	UINT m_uPshStartPageId;
	bool m_bSaveIniFile;
	CWnd* m_pBannerWnd;
	CButton m_btnReset;
	int m_nBannerWidth;
	CToolTipCtrlX m_tooltipOptions;
	CArray<HWND, HWND> m_aOptionToolTipWindows;
	CString m_strActiveTreeOptionToolTip;
	bool m_bShowOptionsToolTips;
	bool m_bOptionsWindowScaleRefreshPending;
	UINT m_uReopenPageId;
	bool m_bModalReopenClosePosted;
	bool m_bClosingForModalReopen;
	HWND m_hRegisteredOptionPage;
	HWND m_hActiveTreeOptionToolTip;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg LRESULT OnCloseForModalReopen(WPARAM wParam, LPARAM lParam);
};

// Returns the applied state while Options is open and the persisted state otherwise.
bool AreOptionsToolTipsEnabled(const CWnd* pWnd);
