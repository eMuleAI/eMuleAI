//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "preferences.h"
#include "ClosableTabCtrl.h"
#include "ToolTipCtrlX.h"

class CPPgBlacklistPanel : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgBlacklistPanel)

public:
	CPPgBlacklistPanel();
	virtual ~CPPgBlacklistPanel();
	virtual BOOL OnApply();
	virtual BOOL OnInitDialog();
	virtual BOOL OnKillActive();

	void Localize(void);
	void ResetToDefaults();
	void LocalizeCommonItems(void);
	void LoadSettings(void);
	void ConfigureBlacklistDefinitionsTextBox();
	void UpdateBlacklistDefinitionsTextBoxColors();

	enum { IDD = IDD_PPG_BLACKLIST_PANEL }; // Dialog Data

protected:
	enum EViewMode
	{
		ViewSettings = 0,
		ViewHelp
	};

	CClosableTabCtrl m_ctrlViewTabs;
	CToolTipCtrlX m_tooltipControls;
	EViewMode m_eViewMode;
	bool m_bUpdatingDefinitions;
	bool m_bDefinitionsDirty;
	bool m_bSettingsDirty;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
	afx_msg void OnSettingsChange();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSysColorChange();
	void UpdateOptionsLayout();
	void UpdateLayout();
	void InitializeViewTabs();
	void UpdateViewTabs();
	void SwitchView(EViewMode eViewMode);
	void UpdateToolTips();
	bool ValidateDefinitions(UINT& uErrorLine, UINT& uRuleCount, bool bShowMessage);
	void SelectDefinitionErrorLine(UINT uLineNumber);
	afx_msg void OnDefinitionsChanged();
	afx_msg void OnValidateDefinitions();
	afx_msg void OnReloadDefinitions();
	afx_msg void OnTcnSelchangeViewTabs(NMHDR* pNMHDR, LRESULT* pResult);
};