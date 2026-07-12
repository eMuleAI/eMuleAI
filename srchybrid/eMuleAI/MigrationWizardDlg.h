//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once

class CMigrationWizardDlg : public CDialog
{
	DECLARE_DYNAMIC(CMigrationWizardDlg)

public:
	CMigrationWizardDlg(bool bStartupMode, CWnd *pParent = NULL);
	virtual ~CMigrationWizardDlg();

	enum { IDD = IDD_MIGRATION_WIZARD };

protected:
	virtual void DoDataExchange(CDataExchange *pDX);
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedSelectDir();
	afx_msg void OnOK();
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	bool m_bStartupMode;
	bool m_bDefaultConfigAvailable;
	bool m_bRestoreCompleted;
	bool m_bRestoreHadErrors;
	bool m_bUseDarkModeTheme;
	UINT m_uCopiedCount;
	CString m_strDefaultConfigDir;
	CString m_strSelectedConfigDir;
	CString m_strInlineDetails;
	CStringArray m_astrFailedFiles;

	void Localize();
	void UpdateDialogState();
	void UpdateLayout(bool bShowSourceDir, bool bShowDetails, bool bShowOk, bool bShowSelect, bool bShowCancel);
	void SetDefaultButton(UINT nID);
	bool BrowseAndRestore();
	bool RestoreFromConfigDir(const CString &configDir);
	bool CopyMigrationFile(const CString &sourceConfigDir, LPCTSTR pszFileName, bool bAllowParentFallback, int &copiedCount);
	CString BuildResultSummaryText() const;
	CString BuildResultDetailsText() const;
	static CString GetProfileLegacyConfigDir();
	static CString GetParentDirectory(const CString &configDir);
	static bool TryResolveConfigDir(const CString &candidateDir, CString &resolvedConfigDir);
	static bool HasAnyMigrationFile(const CString &configDir);
};
