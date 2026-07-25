#pragma once

class CCustomAutoComplete;

class CPPgSecurity : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgSecurity)

	enum
	{
		IDD = IDD_PPG_SECURITY
	};

public:
	CPPgSecurity();
	static CString GetStoredIPFilterUpdateURL();
	static bool UpdateIPFilterFromURL(const CString &url, bool bInteractive = true);
	static bool IsIPFilterDownloadActive();
	static bool GetIPFilterDownloadOverlayInfo(CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal);
	static void FinishIPFilterDownloadOverlayDelay();
	static void CancelIPFilterDownload();
	static LRESULT OnIPFilterDownloadProgress(LPARAM lParam);
	static LRESULT OnIPFilterDownloadFinished(LPARAM lParam);

	void Localize();
	void ResetToDefaults();
	void DeleteDDB();

protected:
	CCustomAutoComplete *m_pacIPFilterURL;
	bool				 m_bAutoUpdate;
	int					 m_nPeriodDays;

	void LoadSettings();

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnSettingsChange()					{ SetModified(); }
	afx_msg void OnReloadIPFilter();
	afx_msg void OnEditIPFilter();
	afx_msg void OnLoadIPFFromURL();
	afx_msg void OnEnChangeUpdateUrl();
	afx_msg void OnDDClicked();
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg void OnBnClickedRunAsUser();
	afx_msg void OnDestroy();
	afx_msg void OnObfuscatedDisabledChange();
	afx_msg void OnObfuscatedRequestedChange();
	afx_msg void OnBnClickedAutoupdateIpfilter();
	afx_msg void OnEnChangeIpfilterperiod();
};
