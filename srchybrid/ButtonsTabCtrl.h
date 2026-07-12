#pragma once

class CButtonsTabCtrl : public CTabCtrl
{
	DECLARE_DYNAMIC(CButtonsTabCtrl)

public:
	CButtonsTabCtrl() = default;
	void RefreshDarkScrollButtons();

protected:
	virtual void PreSubclassWindow();
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
	afx_msg void OnSize(UINT nType, int cx, int cy);

	DECLARE_MESSAGE_MAP()
};