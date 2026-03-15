#pragma once

class CButtonsTabCtrl : public CTabCtrl
{
	DECLARE_DYNAMIC(CButtonsTabCtrl)

public:
	CButtonsTabCtrl() = default;

protected:
	virtual void PreSubclassWindow();
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);

	DECLARE_MESSAGE_MAP()
};