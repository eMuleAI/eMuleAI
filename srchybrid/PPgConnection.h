#pragma once

#include "eMuleAI/NetBind.h"
#include "eMuleAI/IpGuard.h"
#include "ToolTipCtrlX.h"
#include "ComboBoxEx2.h"

class CPPgConnection : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgConnection)

	enum
	{
		IDD = IDD_PPG_CONNECTION
	};
	uint16 m_lastudp;
	bool m_bUpdatingControls;
	bool m_bDisplaySpeedsInKB;
	uint32 m_nDisplayDownloadCapacityKBS;
	uint32 m_nDisplayUploadCapacityKBS;
	void ChangePorts(uint8 iWhat); //0 - UDP, 1 - TCP, 2 - enable/disable "Test ports"
	bool ChangeUDP();
	void UpdateRandomPortRangeControls();
	void UpdatePortControlsForRandomization(bool bKeepCurrentPorts);
	uint16 GetActiveTCPPortForDisplay() const;
	uint16 GetActiveUDPPortForDisplay() const;
	bool GetRandomPortRangeFromControls(UINT& rnStart, UINT& rnEnd, bool bShowError);
	void UpdateSpeedDisplayUnitControls();
	void UpdateCapacityValuesFromControls();

public:
	CPPgConnection();

	void Localize();
	void ResetToDefaults();
	void LoadSettings();

	static bool CheckUp(uint32 mUp, uint32 &mDown);
	static bool CheckDown(uint32 &mUp, uint32 mDown);
protected:
	CSliderCtrl m_ctlMaxDown;
	CSliderCtrl m_ctlMaxUp;

	void ShowLimitValues();
	void SetRateSliderTicks(CSliderCtrl &rRate);

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg void OnSettingsChange()				{ if (!m_bUpdatingControls) SetModified(); }
	afx_msg void OnCapacityChange();
	afx_msg void OnForceSpeedsToKBChange();
	afx_msg void OnEnChangeUDPDisable();
	afx_msg void OnLimiterChange();
	afx_msg void OnBnClickedWizard();
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg void OnStartPortTest();
	afx_msg void OnRandomizePortsOnStartup();
	afx_msg void OnEnKillFocusTCP();
	afx_msg void OnEnKillFocusUDP();
};

class CPPgNetworkInterface : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgNetworkInterface)

	enum
	{
		IDD = IDD_PPG_NETWORK_INTERFACE
	};
	std::vector<SNetBindInterface> m_bindInterfaces;
	bool m_bUpdatingControls;
	void LoadBindableInterfaces();
	void FillBindInterfaceCombo(bool bPreserveCurrentSelection = false);
	void GetBindSelection(CString& strInterfaceId, CString& strInterfaceName, CString& strAddress);
	void UpdateIpGuardControls();
	void FillVpnGuardCountryCombo(bool bPreserveCurrentSelection = false);
	CString GetVpnGuardCountrySelection() const;
	void SetVpnGuardCountrySelection(const CString& strCountryCode);
	void UpdateVpnGuardControls();
	void UpdateNetworkInterfaceTooltips();
	void UpdateBindStatus();

public:
	CPPgNetworkInterface();

	void Localize();
	void ResetToDefaults();
	void LoadSettings();

protected:
	CToolTipCtrlX m_ToolTip;
	CComboBox m_ctlBindInterface;
	CComboBoxEx2 m_ctlVpnGuardCountry;

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBindSettingsChange();
	afx_msg void OnVpnGuardSettingsChange();
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
};
