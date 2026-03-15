#pragma once
#include <vector>

class CSplashScreen : public CDialog
{
	DECLARE_DYNAMIC(CSplashScreen)

public:
	enum DisplayMode
	{
		DisplayModeSplash = 0,
		DisplayModeAbout,
		DisplayModeSpecialThanks
	};

	struct StarParticle
	{
		float fX;
		float fBaseY;
		float fSpeed;
		float fSize;
		float fTwinklePhase;
		BYTE uAlpha;
	};

public:
	enum
	{
		IDD = IDD_SPLASH
	};

	explicit CSplashScreen(CWnd *pParent = NULL);   // standard constructor
	virtual	~CSplashScreen();
	void SetDisplayMode(DisplayMode eDisplayMode) { m_eDisplayMode = eDisplayMode; }
	bool IsSpecialThanksMode() const { return m_eDisplayMode == DisplayModeSpecialThanks; }
	void AdvanceAnimationFrame();

protected:
	DisplayMode m_eDisplayMode;
	CBitmap m_imgSplash;
	CBitmap m_imgSpecialThanksBackground;
	CRect m_rcCloseBtn;
	std::vector<StarParticle> m_specialThanksStars;
	float m_fSpecialThanksCrawlOffset;
	float m_fSpecialThanksStarDrift;
	UINT m_uSpecialThanksFrame;

	BOOL OnInitDialog();
	void OnPaint();
	BOOL PreTranslateMessage(MSG *pMsg);
	virtual void OnCancel();
	virtual void OnOK();
	virtual void PostNcDestroy();
	void InitializeSpecialThanksScene();
	void DrawAboutContent(CDC& dc, const BITMAP& BM);
	void DrawCloseButton(CDC& dc);
	void DrawSpecialThanksScene(CDC& dc, const CRect& rcClient);
	afx_msg void OnBnClickedBtnThirdparty();

	DECLARE_MESSAGE_MAP()
public:
	bool m_bAutoClose;
};
