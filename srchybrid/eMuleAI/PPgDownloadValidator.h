//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "ClosableTabCtrl.h"
#include "TreeOptionsCtrlEx.h"
#include "DownloadValidator.h"
#include "ToolTipCtrlX.h"


class CPPgDownloadValidator : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgDownloadValidator)

public:
	CPPgDownloadValidator();
	virtual ~CPPgDownloadValidator() = default;
	void Localize();
	void ResetToDefaults();

	enum { IDD = IDD_PPG_DOWNLOAD_VALIDATOR };

protected:
	enum EViewMode
	{
		ViewSettings = 0,
		ViewRules,
		ViewHelp
	};

	CTreeOptionsCtrlEx m_ctrlTreeOptions;
	CClosableTabCtrl m_ctrlViewTabs;
	CToolTipCtrlX m_tooltipTreeOptions;
	CString m_strTreeOptionsToolTip;
	bool m_bInitializedTreeOpts;
	bool m_bUpdatingRulesText;
	bool m_bRulesDirty;
	bool m_bSettingsDirty;
	EViewMode m_eViewMode;
	CString m_strRulesText;

	HTREEITEM m_htiDownloadValidator;
	HTREEITEM m_htiDownloadValidatorPassive;
	HTREEITEM m_htiDownloadValidatorAlwaysAsk;
	HTREEITEM m_htiDownloadValidatorReject;
	HTREEITEM m_htiDownloadValidatorAccept;
	HTREEITEM m_htiDownloadValidatorAcceptPercentage;
	HTREEITEM m_htiDownloadValidatorRejectCanceled;
	HTREEITEM m_htiDownloadValidatorRejectSameHash;
	HTREEITEM m_htiDownloadValidatorRejectBlacklisted;
	HTREEITEM m_htiDownloadValidatorCaseInsensitive;
	HTREEITEM m_htiDownloadValidatorIgnoreExtension;
	HTREEITEM m_htiDownloadValidatorIgnoreTags;
	HTREEITEM m_htiDownloadValidatorDontIgnoreNumericTags;
	HTREEITEM m_htiDownloadValidatorIgnoreNonAlphaNumeric;
	HTREEITEM m_htiDownloadValidatorCleanMojibake;
	HTREEITEM m_htiDownloadValidatorMinimumComparisonLength;
	HTREEITEM m_htiDownloadValidatorSkipIncompleteFileConfirmation;
	HTREEITEM m_htiDownloadValidatorMarkAsBlacklisted;
	HTREEITEM m_htiDownloadValidatorAutoMarkAsBlacklisted;
	HTREEITEM m_htiDownloadValidatorDateTimeMatching;
	HTREEITEM m_htiDownloadValidatorDateTimeUseYearRange;
	HTREEITEM m_htiDownloadValidatorDateTimeYearStart;
	HTREEITEM m_htiDownloadValidatorDateTimeYearEnd;
	HTREEITEM m_htiDownloadValidatorDateTimeCheckSeconds;
	HTREEITEM m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues;
	HTREEITEM m_htiDownloadValidatorRegexMatching;
	HTREEITEM m_htiDownloadValidatorFuzzyMatching;
	HTREEITEM m_htiDownloadValidatorFuzzySimilarityThreshold;
	HTREEITEM m_htiDownloadValidatorFuzzyDisplayThreshold;
	HTREEITEM m_htiDownloadValidatorFuzzyMinimumSharedTokens;
	HTREEITEM m_htiDownloadValidatorFuzzyMinimumTokenCoverage;
	HTREEITEM m_htiDownloadValidatorFuzzyMinimumLengthSimilarity;
	HTREEITEM m_htiDownloadValidatorFuzzyMinimumEditSimilarity;
	HTREEITEM m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters;
	HTREEITEM m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits;
	HTREEITEM m_htiDownloadValidatorMediaLengthMatching;
	HTREEITEM m_htiDownloadValidatorMediaLengthTolerance;

	int m_iDownloadValidator;
	int m_iDownloadValidatorAcceptPercentage;
	bool m_bDownloadValidatorRejectCanceled;
	bool m_bDownloadValidatorRejectSameHash;
	bool m_bDownloadValidatorRejectBlacklisted;
	bool m_bDownloadValidatorCaseInsensitive;
	bool m_bDownloadValidatorIgnoreExtension;
	bool m_bDownloadValidatorIgnoreTags;
	bool m_bDownloadValidatorDontIgnoreNumericTags;
	bool m_bDownloadValidatorIgnoreNonAlphaNumeric;
	bool m_bDownloadValidatorCleanMojibake;
	int m_iDownloadValidatorMinimumComparisonLength;
	bool m_bDownloadValidatorSkipIncompleteFileConfirmation;
	bool m_bDownloadValidatorMarkAsBlacklisted;
	bool m_bDownloadValidatorAutoMarkAsBlacklisted;
	bool m_bDownloadValidatorDateTimeMatching;
	bool m_bDownloadValidatorDateTimeUseYearRange;
	int m_iDownloadValidatorDateTimeYearStart;
	int m_iDownloadValidatorDateTimeYearEnd;
	bool m_bDownloadValidatorDateTimeCheckSeconds;
	bool m_bDownloadValidatorDateTimeIncludeFollowingNumericValues;
	bool m_bDownloadValidatorRegexMatching;
	bool m_bDownloadValidatorFuzzyMatching;
	CString m_strDownloadValidatorFuzzySimilarityThreshold;
	int m_iDownloadValidatorFuzzyDisplayThreshold;
	int m_iDownloadValidatorFuzzyMinimumSharedTokens;
	int m_iDownloadValidatorFuzzyMinimumTokenCoverage;
	int m_iDownloadValidatorFuzzyMinimumLengthSimilarity;
	int m_iDownloadValidatorFuzzyMinimumEditSimilarity;
	int m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters;
	int m_iDownloadValidatorFuzzyStructuralMinimumIDDigits;
	bool m_bDownloadValidatorMediaLengthMatching;
	int m_iDownloadValidatorMediaLengthTolerance;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL OnKillActive();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	void LoadSettings();
	void LocalizeItemText(HTREEITEM item, LPCTSTR strid);
	void LocalizeItemInfoText(HTREEITEM item, LPCTSTR strid);
	void LocalizeEditLabel(HTREEITEM item, LPCTSTR strid);
	void UpdateDownloadValidatorDateTimeUi();
	void UpdateDownloadValidatorFuzzyUi();
	void UpdateDownloadValidatorMediaLengthUi();
	bool ParseFuzzySimilarityThreshold(uint32& uThreshold) const;
	void UpdateLayout();
	void InitializeViewTabs();
	void UpdateViewTabs();
	void UpdateTreeOptionsToolTip();
	CString FormatToolTipText(const CString& strText) const;
	void UpdateRulesTextBoxColors();
	void SwitchView(EViewMode eViewMode);
	void CaptureRulesText();
	void SetRulesEditorText(const CString& strText);
	CString GetRegexResultText(const CDownloadValidator::SRegexRulesResult& result, bool bSuccess) const;
	void SelectRegexErrorLine(UINT uLineNumber);
	bool ValidateRules(CDownloadValidator::SRegexRulesResult& result, bool bShowMessage);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSysColorChange();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg LRESULT OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT DrawTreeItemHelp(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTcnSelchangeViewTabs(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnValidateRules();
	afx_msg void OnReloadRules();
	afx_msg void OnRulesChanged();
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
};
