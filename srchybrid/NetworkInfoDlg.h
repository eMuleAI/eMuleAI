#pragma once

#include "ResizableLib/ResizableDialog.h"
#include "RichEditCtrlX.h"

// CNetworkInfoDlg dialog

class CNetworkInfoDlg : public CResizableDialog
{
	DECLARE_DYNAMIC(CNetworkInfoDlg)

	enum
	{
		IDD = IDD_NETWORK_INFO
	};

public:
	explicit CNetworkInfoDlg(CWnd *pParent = NULL);   // standard constructor
    static CNetworkInfoDlg* GetActiveInstance();
    void RefreshInfo();
	bool IsAutoRefreshEnabled();

protected:
	CRichEditCtrlX m_info;
    CHARFORMAT m_cfDef;
    CHARFORMAT m_cfBold;
    static CNetworkInfoDlg* s_pActiveInstance;

	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
    afx_msg void OnDestroy();
	afx_msg void OnNattForcePublish();
private:
    void FixNickLink();
    void FixAllLinksOff();
};

void CreateNetworkInfo(CRichEditCtrlX &rCtrl, CHARFORMAT &rcfDef, CHARFORMAT &rcfBold, bool bFullInfo = false);