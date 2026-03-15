//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "emuleDlg.h"
#include "SplashScreen.h"
#include "OtherFunctions.h"
#include <gdiplus.h>
#include <math.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


namespace
{
	const int SPLASH_EXTRA_HEIGHT = 28;
	const int SPECIAL_THANKS_STAR_COUNT = 112;
	const int SPECIAL_THANKS_INTRO_GAP_LINE_COUNT = 4;
	const int SPECIAL_THANKS_INTRO_WRAP_CHAR_COUNT = 34;
	const float SPECIAL_THANKS_CRAWL_STEP = 0.925f;
	const float SPECIAL_THANKS_STAR_DRIFT_STEP = 0.55f;
	const float SPECIAL_THANKS_TOP_PADDING = 7.0f;
	const float SPECIAL_THANKS_TITLE_HEIGHT = 26.0f;
	const float SPECIAL_THANKS_VIEWPORT_MARGIN = 5.0f;
	const float SPECIAL_THANKS_HEADING_SPACING = 34.0f;
	const float SPECIAL_THANKS_INTRO_SPACING = 30.0f;
	const float SPECIAL_THANKS_BODY_SPACING = 24.0f;
	const float SPECIAL_THANKS_SECTION_GAP = 24.0f;
	const float SPECIAL_THANKS_BOTTOM_ENTRY_OFFSET = 26.0f;
	const float SPECIAL_THANKS_TOP_FADE_HEIGHT = 76.0f;
	const float SPECIAL_THANKS_BOTTOM_FADE_HEIGHT = 116.0f;
	const float SPECIAL_THANKS_FADE_EDGE_OVERDRAW = 28.0f;
	const float SPECIAL_THANKS_PERSPECTIVE_MIN_SCALE = 0.18f;
	const float SPECIAL_THANKS_PERSPECTIVE_BOTTOM_OVERSCAN = 92.0f;
	const float SPECIAL_THANKS_HEADING_VERTICAL_COMPRESSION = 0.72f;
	const float SPECIAL_THANKS_INTRO_VERTICAL_COMPRESSION = 0.69f;
	const float SPECIAL_THANKS_BODY_VERTICAL_COMPRESSION = 0.64f;
	const float PI_F = 3.14159265358979323846f;

	struct SpecialThanksSectionDef
	{
		LPCTSTR pszHeadingKey;
		LPCTSTR pszNames;
	};

	const SpecialThanksSectionDef kSpecialThanksSections[] =
	{
		{
			_T("EMULE_AI_SPECIAL_THANKS_HEADING_DEVELOPERS"),
			_T("Merkur\n")
			_T("John aka. Unknown1\n")
			_T("Ornis\n")
			_T("Bluecow\n")
			_T("Tecxx\n")
			_T("Pach2\n")
			_T("Juanjo\n")
			_T("Dirus\n")
			_T("Barry\n")
			_T("zz\n")
			_T("Some Support\n")
			_T("fox88")
		},
		{
			_T("EMULE_AI_SPECIAL_THANKS_HEADING_MODDERS"),
			_T("David Xanatos\n")
			_T("Stulle\n")
			_T("XMan\n")
			_T("netfinity\n")
			_T("WiZaRd\n")
			_T("leuk_he\n")
			_T("enkeyDev\n")
			_T("SLUGFILLER\n")
			_T("SiRoB\n")
			_T("khaos\n")
			_T("Enig123\n")
			_T("TAHO\n")
			_T("Pretender\n")
			_T("Mighty Knife\n")
			_T("Ottavio84\n")
			_T("Dolphin\n")
			_T("sFrQlXeRt\n")
			_T("evcz\n")
			_T("cyrex2001\n")
			_T("zz_fly\n")
			_T("Slaham\n")
			_T("Spike\n")
			_T("shadow2004\n")
			_T("gomez82\n")
			_T("JvA\n")
			_T("Pawcio\n")
			_T("lovelace\n")
			_T("MoNKi\n")
			_T("Avi3k\n")
			_T("Commander\n")
			_T("emulEspa\u00F1a\n")
			_T("Maella\n")
			_T("VQB\n")
			_T("J.C.Conner")
		},
		{
			_T("EMULE_AI_SPECIAL_THANKS_HEADING_TESTERS"),
			_T("Sony\n")
			_T("Monk\n")
			_T("Myxin\n")
			_T("Mr Ozon\n")
			_T("Daan\n")
			_T("Elandal\n")
			_T("Frozen_North\n")
			_T("kayfam\n")
			_T("Khandurian\n")
			_T("Masta2002\n")
			_T("mrLabr\n")
			_T("Nesi-San\n")
			_T("SeveredCross\n")
			_T("Skynetman")
		}
	};

	template <typename T>
	T ClampValue(T value, T minimumValue, T maximumValue)
	{
		if (value < minimumValue)
			return minimumValue;
		if (value > maximumValue)
			return maximumValue;
		return value;
	}

	float WrapPositive(float value, float range)
	{
		if (range <= 0.0f)
			return 0.0f;
		while (value >= range)
			value -= range;
		while (value < 0.0f)
			value += range;
		return value;
	}

	void SplitMultilineText(LPCTSTR pszText, std::vector<CString>& lines)
	{
		lines.clear();
		if (pszText == NULL || pszText[0] == _T('\0'))
			return;

		CString text(pszText);
		int nStart = 0;
		while (nStart <= text.GetLength()) {
			const int nEnd = text.Find(_T('\n'), nStart);
			CString line = (nEnd == -1) ? text.Mid(nStart) : text.Mid(nStart, nEnd - nStart);
			if (!line.IsEmpty())
				lines.push_back(line);
			if (nEnd == -1)
				break;
			nStart = nEnd + 1;
		}
	}

	int FindWrapBreakPosition(const CString& text, int nMaxCharacters)
	{
		const int nSearchLimit = min(text.GetLength(), nMaxCharacters);
		for (int i = nSearchLimit; i > 0; --i) {
			if (_istspace(text[i - 1]))
				return i - 1;
		}
		return nSearchLimit;
	}

	void SplitWrappedText(LPCTSTR pszText, int nMaxCharacters, std::vector<CString>& lines)
	{
		lines.clear();
		if (pszText == NULL || pszText[0] == _T('\0'))
			return;

		std::vector<CString> paragraphs;
		SplitMultilineText(pszText, paragraphs);
		for (size_t i = 0; i < paragraphs.size(); ++i) {
			CString remaining = paragraphs[i];
			remaining.Trim();
			while (!remaining.IsEmpty()) {
				if (remaining.GetLength() <= nMaxCharacters) {
					lines.push_back(remaining);
					break;
				}

				int nBreakPos = FindWrapBreakPosition(remaining, nMaxCharacters);
				if (nBreakPos <= 0)
					nBreakPos = nMaxCharacters;

				CString line = remaining.Left(nBreakPos);
				line.TrimRight();
				if (line.IsEmpty()) {
					line = remaining.Left(nMaxCharacters);
					nBreakPos = nMaxCharacters;
				}

				lines.push_back(line);
				remaining = remaining.Mid(nBreakPos);
				remaining.TrimLeft();
			}
		}
	}

	int CountMultilineEntries(LPCTSTR pszText)
	{
		std::vector<CString> lines;
		SplitMultilineText(pszText, lines);
		return static_cast<int>(lines.size());
	}

	int CountWrappedEntries(LPCTSTR pszText, int nMaxCharacters)
	{
		std::vector<CString> lines;
		SplitWrappedText(pszText, nMaxCharacters, lines);
		return static_cast<int>(lines.size());
	}

	float GetSpecialThanksContentHeight()
	{
		float fHeight = 58.0f;
		fHeight += static_cast<float>(CountWrappedEntries(GetResString(_T("EMULE_AI_SPECIAL_THANKS_INTRO")), SPECIAL_THANKS_INTRO_WRAP_CHAR_COUNT)) * SPECIAL_THANKS_INTRO_SPACING;
		fHeight += SPECIAL_THANKS_BODY_SPACING * SPECIAL_THANKS_INTRO_GAP_LINE_COUNT;
		for (size_t i = 0; i < _countof(kSpecialThanksSections); ++i) {
			fHeight += SPECIAL_THANKS_HEADING_SPACING;
			fHeight += static_cast<float>(CountMultilineEntries(kSpecialThanksSections[i].pszNames)) * SPECIAL_THANKS_BODY_SPACING;
			fHeight += SPECIAL_THANKS_SECTION_GAP;
		}
		return fHeight;
	}

	float GetSpecialThanksViewportHeight(const CRect& rcClient)
	{
		const float fViewportHeight = static_cast<float>(rcClient.Height()) - (SPECIAL_THANKS_TOP_PADDING + SPECIAL_THANKS_TITLE_HEIGHT + 14.0f);
		return (fViewportHeight > 0.0f) ? fViewportHeight : 0.0f;
	}

	float GetSpecialThanksLoopHeight(float fViewportHeight)
	{
		return GetSpecialThanksContentHeight() + fViewportHeight + SPECIAL_THANKS_BOTTOM_ENTRY_OFFSET;
	}

	class CGdiPlusSession
	{
	public:
		CGdiPlusSession()
			: m_uToken(0)
			, m_bReady(false)
		{
			Gdiplus::GdiplusStartupInput startupInput;
			m_bReady = (Gdiplus::GdiplusStartup(&m_uToken, &startupInput, NULL) == Gdiplus::Ok);
		}

		~CGdiPlusSession()
		{
			if (m_bReady)
				Gdiplus::GdiplusShutdown(m_uToken);
		}

		bool IsReady() const
		{
			return m_bReady;
		}

	private:
		ULONG_PTR m_uToken;
		bool m_bReady;
	};

	CGdiPlusSession& GetGdiPlusSession()
	{
		static CGdiPlusSession session;
		return session;
	}

	void DrawGlowingTitle(Gdiplus::Graphics& gfx, const CString& strText, const Gdiplus::RectF& rcTitle)
	{
		if (strText.IsEmpty())
			return;

		const Gdiplus::REAL fEmSize = (strText.GetLength() > 22) ? 14.0f : 16.0f;
		Gdiplus::FontFamily fontFamily(L"Georgia");
		Gdiplus::StringFormat format;
		format.SetAlignment(Gdiplus::StringAlignmentCenter);
		format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		Gdiplus::GraphicsPath titlePath;
		titlePath.AddString(strText, -1, &fontFamily, Gdiplus::FontStyleBold, fEmSize, rcTitle, &format);

		Gdiplus::GraphicsPath shadowPath;
		shadowPath.AddPath(&titlePath, FALSE);
		Gdiplus::Matrix shadowMatrix;
		shadowMatrix.Translate(0.0f, 2.0f);
		shadowPath.Transform(&shadowMatrix);
		Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(190, 0, 0, 0));
		gfx.FillPath(&shadowBrush, &shadowPath);

		Gdiplus::Pen glowPen(Gdiplus::Color(120, 255, 196, 96), 4.0f);
		gfx.DrawPath(&glowPen, &titlePath);

		Gdiplus::RectF rcBounds;
		titlePath.GetBounds(&rcBounds);
		Gdiplus::LinearGradientBrush fillBrush(
			Gdiplus::PointF(rcBounds.X, rcBounds.Y),
			Gdiplus::PointF(rcBounds.X, rcBounds.Y + rcBounds.Height),
			Gdiplus::Color(255, 255, 238, 188),
			Gdiplus::Color(255, 255, 172, 92));
		gfx.FillPath(&fillBrush, &titlePath);

		Gdiplus::Pen outlinePen(Gdiplus::Color(210, 255, 244, 220), 1.0f);
		gfx.DrawPath(&outlinePen, &titlePath);
	}

	void DrawPerspectiveCreditsLine(Gdiplus::Graphics& gfx, const Gdiplus::RectF& rcViewport, Gdiplus::REAL fY, LPCTSTR pszText, bool bHeading, bool bIntro)
	{
		if (pszText == NULL || pszText[0] == _T('\0'))
			return;

		const Gdiplus::REAL fVanishY = rcViewport.Y - 10.0f;
		const Gdiplus::REAL fBottomY = rcViewport.Y + rcViewport.Height + SPECIAL_THANKS_PERSPECTIVE_BOTTOM_OVERSCAN;
		if (fBottomY <= fVanishY)
			return;

		Gdiplus::REAL fPerspective = (fY - fVanishY) / (fBottomY - fVanishY);
		if (fPerspective <= 0.0f || fPerspective >= 1.0f)
			return;

		// Keep the crawl on a single plane from entry to exit.
		const Gdiplus::REAL fScaleRange = bHeading ? 1.55f : (bIntro ? 1.78f : 1.38f);
		Gdiplus::REAL fScale = SPECIAL_THANKS_PERSPECTIVE_MIN_SCALE + (fPerspective * fScaleRange);
		const Gdiplus::REAL fCenterX = rcViewport.X + (rcViewport.Width / 2.0f);
		const Gdiplus::REAL fEmSize = bHeading ? 40.0f : (bIntro ? 38.0f : 31.0f);
		const INT nFontStyle = bHeading ? Gdiplus::FontStyleBold : (bIntro ? Gdiplus::FontStyleItalic : (Gdiplus::FontStyleBold | Gdiplus::FontStyleItalic));
		Gdiplus::FontFamily fontFamily(bHeading ? L"Georgia" : (bIntro ? L"Georgia" : L"Arial"));
		Gdiplus::StringFormat format;
		format.SetAlignment(Gdiplus::StringAlignmentCenter);
		format.SetLineAlignment(Gdiplus::StringAlignmentNear);

		Gdiplus::RectF rcLayout(0.0f, 0.0f, rcViewport.Width * (bIntro ? 1.78f : 1.45f), fEmSize * (bIntro ? 3.2f : 2.6f));
		Gdiplus::GraphicsPath basePath;
		basePath.AddString(pszText, -1, &fontFamily, nFontStyle, fEmSize, rcLayout, &format);

		Gdiplus::RectF rcBounds;
		basePath.GetBounds(&rcBounds);
		if (rcBounds.Width <= 0.0f || rcBounds.Height <= 0.0f)
			return;

		const Gdiplus::REAL fMaxWidth = bIntro
			? ClampValue<Gdiplus::REAL>(rcViewport.Width * (0.30f + (fPerspective * 0.66f)), rcViewport.Width * 0.30f, rcViewport.Width * 0.96f)
			: rcViewport.Width * (0.12f + (fPerspective * 0.80f));
		if ((rcBounds.Width * fScale) > fMaxWidth)
			fScale = fMaxWidth / rcBounds.Width;

		const Gdiplus::REAL fVerticalCompression = bHeading ? SPECIAL_THANKS_HEADING_VERTICAL_COMPRESSION : (bIntro ? SPECIAL_THANKS_INTRO_VERTICAL_COMPRESSION : SPECIAL_THANKS_BODY_VERTICAL_COMPRESSION);
		const Gdiplus::REAL fScaleY = fScale * fVerticalCompression;

		Gdiplus::GraphicsPath shadowPath;
		shadowPath.AddPath(&basePath, FALSE);
		Gdiplus::Matrix shadowMatrix;
		shadowMatrix.Translate(-(rcBounds.X + (rcBounds.Width / 2.0f)), -(rcBounds.Y + (rcBounds.Height / 2.0f)));
		shadowMatrix.Scale(fScale, fScaleY, Gdiplus::MatrixOrderAppend);
		shadowMatrix.Translate(fCenterX + 1.8f, fY + 2.4f, Gdiplus::MatrixOrderAppend);
		shadowPath.Transform(&shadowMatrix);

		const BYTE uShadowAlpha = static_cast<BYTE>(ClampValue<Gdiplus::REAL>(40.0f + (fScale * 126.0f), 0.0f, 255.0f));
		Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(uShadowAlpha, 0, 0, 0));
		gfx.FillPath(&shadowBrush, &shadowPath);

		Gdiplus::GraphicsPath textPath;
		textPath.AddPath(&basePath, FALSE);
		Gdiplus::Matrix textMatrix;
		textMatrix.Translate(-(rcBounds.X + (rcBounds.Width / 2.0f)), -(rcBounds.Y + (rcBounds.Height / 2.0f)));
		textMatrix.Scale(fScale, fScaleY, Gdiplus::MatrixOrderAppend);
		textMatrix.Translate(fCenterX, fY, Gdiplus::MatrixOrderAppend);
		textPath.Transform(&textMatrix);

		Gdiplus::RectF rcTextBounds;
		textPath.GetBounds(&rcTextBounds);
		const BYTE uAlpha = static_cast<BYTE>(ClampValue<Gdiplus::REAL>(70.0f + (fPerspective * 185.0f), 0.0f, 255.0f));
		if (bHeading) {
			Gdiplus::Pen glowPen(Gdiplus::Color(uAlpha / 2, 255, 198, 92), ClampValue<Gdiplus::REAL>(4.1f * fScale, 1.0f, 4.2f));
			gfx.DrawPath(&glowPen, &textPath);

			Gdiplus::LinearGradientBrush fillBrush(
				Gdiplus::PointF(0.0f, rcTextBounds.Y),
				Gdiplus::PointF(0.0f, rcTextBounds.Y + rcTextBounds.Height),
				Gdiplus::Color(uAlpha, 255, 244, 182),
				Gdiplus::Color(uAlpha, 255, 176, 38));
			gfx.FillPath(&fillBrush, &textPath);

			Gdiplus::Pen outlinePen(Gdiplus::Color(uAlpha / 2, 88, 44, 0), ClampValue<Gdiplus::REAL>(1.0f * fScale, 0.8f, 1.6f));
			gfx.DrawPath(&outlinePen, &textPath);

			const Gdiplus::REAL fUnderlineHalf = ClampValue<Gdiplus::REAL>((rcTextBounds.Width * 0.34f) + (18.0f * fScale), 26.0f * fScale, rcViewport.Width * 0.30f);
			const Gdiplus::REAL fUnderlineY = rcTextBounds.Y + rcTextBounds.Height + (3.0f * fScale);
			Gdiplus::Pen underlinePen(Gdiplus::Color(uAlpha, 255, 188, 76), ClampValue<Gdiplus::REAL>(1.7f * fScale, 1.0f, 2.6f));
			gfx.DrawLine(&underlinePen, fCenterX - fUnderlineHalf, fUnderlineY, fCenterX + fUnderlineHalf, fUnderlineY);
		} else if (bIntro) {
			Gdiplus::Pen glowPen(Gdiplus::Color(uAlpha / 3, 255, 220, 112), ClampValue<Gdiplus::REAL>(2.7f * fScale, 0.8f, 3.4f));
			gfx.DrawPath(&glowPen, &textPath);

			Gdiplus::LinearGradientBrush fillBrush(
				Gdiplus::PointF(0.0f, rcTextBounds.Y),
				Gdiplus::PointF(0.0f, rcTextBounds.Y + rcTextBounds.Height),
				Gdiplus::Color(uAlpha, 255, 236, 170),
				Gdiplus::Color(uAlpha, 255, 188, 86));
			gfx.FillPath(&fillBrush, &textPath);

			Gdiplus::Pen outlinePen(Gdiplus::Color(uAlpha / 2, 90, 44, 0), ClampValue<Gdiplus::REAL>(0.9f * fScale, 0.6f, 1.3f));
			gfx.DrawPath(&outlinePen, &textPath);
		} else {
			Gdiplus::Pen glowPen(Gdiplus::Color(uAlpha / 3, 255, 206, 88), ClampValue<Gdiplus::REAL>(2.9f * fScale, 0.8f, 3.9f));
			gfx.DrawPath(&glowPen, &textPath);

			Gdiplus::LinearGradientBrush fillBrush(
				Gdiplus::PointF(0.0f, rcTextBounds.Y),
				Gdiplus::PointF(0.0f, rcTextBounds.Y + rcTextBounds.Height),
				Gdiplus::Color(uAlpha, 255, 225, 92),
				Gdiplus::Color(uAlpha, 255, 174, 28));
			gfx.FillPath(&fillBrush, &textPath);

			Gdiplus::Pen outlinePen(Gdiplus::Color(uAlpha / 2, 92, 45, 0), ClampValue<Gdiplus::REAL>(0.9f * fScale, 0.6f, 1.4f));
			gfx.DrawPath(&outlinePen, &textPath);
		}
	}
}

IMPLEMENT_DYNAMIC(CSplashScreen, CDialog)

BEGIN_MESSAGE_MAP(CSplashScreen, CDialog)
	ON_WM_PAINT()
	ON_BN_CLICKED(IDC_BTN_THIRDPARTY, &CSplashScreen::OnBnClickedBtnThirdparty)
END_MESSAGE_MAP()

CSplashScreen::CSplashScreen(CWnd *pParent /*=NULL*/)
	: CDialog(CSplashScreen::IDD, pParent)
	, m_eDisplayMode(DisplayModeSplash)
	, m_imgSplash()
	, m_imgSpecialThanksBackground()
	, m_rcCloseBtn(0, 0, 0, 0)
	, m_specialThanksStars()
	, m_fSpecialThanksCrawlOffset(0.0f)
	, m_fSpecialThanksStarDrift(0.0f)
	, m_uSpecialThanksFrame(0)
	, m_bAutoClose(true)
{
}

CSplashScreen::~CSplashScreen()
{
	m_imgSplash.DeleteObject();
	m_imgSpecialThanksBackground.DeleteObject();
}

void CSplashScreen::InitializeSpecialThanksScene()
{
	m_specialThanksStars.clear();

	CRect rcClient;
	GetClientRect(&rcClient);
	if (rcClient.IsRectEmpty())
		return;

	m_specialThanksStars.reserve(SPECIAL_THANKS_STAR_COUNT);
	const int nWidth = (rcClient.Width() > 0) ? rcClient.Width() : 1;
	const int nHeight = (rcClient.Height() > 0) ? rcClient.Height() : 1;
	for (int i = 0; i < SPECIAL_THANKS_STAR_COUNT; ++i) {
		const UINT uSeed = 1664525u * static_cast<UINT>(i + 1) + 1013904223u;

		StarParticle star = {};
		star.fX = static_cast<float>(uSeed % static_cast<UINT>(nWidth));
		star.fBaseY = static_cast<float>(((uSeed >> 8) ^ (uSeed >> 17)) % static_cast<UINT>(nHeight));
		star.fSpeed = 0.24f + static_cast<float>((uSeed >> 4) & 0x7F) / 150.0f;
		star.fSize = 1.0f + static_cast<float>((uSeed >> 19) & 0x03) * 0.55f;
		star.fTwinklePhase = static_cast<float>((uSeed >> 12) & 0xFF) / 255.0f * PI_F * 2.0f;
		star.uAlpha = static_cast<BYTE>(80 + ((uSeed >> 24) & 0x5F));
		m_specialThanksStars.push_back(star);
	}
}

BOOL CSplashScreen::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);

	VERIFY(m_imgSplash.Attach(theApp.LoadImage(_T("LOGO"), _T("JPG"))));
	if (m_imgSplash.GetSafeHandle()) {
		BITMAP bmp = {};
		if (m_imgSplash.GetBitmap(&bmp)) {
			const int nExtraHeight = m_bAutoClose ? 0 : SPLASH_EXTRA_HEIGHT;
			const int nSplashWidth = bmp.bmWidth;
			const int nSplashHeight = bmp.bmHeight + nExtraHeight;

			CPoint ptCenter;
			bool bCenterFound = false;
			CWnd* pMainWnd = theApp.m_pMainWnd;
			if (pMainWnd && pMainWnd->IsWindowVisible()) {
				CRect rcMain;
				pMainWnd->GetWindowRect(&rcMain);
				ptCenter = rcMain.CenterPoint();
				bCenterFound = true;
			} else {
				const WINDOWPLACEMENT& wpMain = thePrefs.EmuleWindowPlacement;
				if (wpMain.length == sizeof(WINDOWPLACEMENT)) {
					CRect rcMain(wpMain.rcNormalPosition);
					ptCenter = rcMain.CenterPoint();
					bCenterFound = true;
				}
			}

			WINDOWPLACEMENT wp = {};
			GetWindowPlacement(&wp);
			if (bCenterFound) {
				wp.rcNormalPosition.left = ptCenter.x - (nSplashWidth / 2);
				wp.rcNormalPosition.top = ptCenter.y - (nSplashHeight / 2);
			}
			wp.rcNormalPosition.right = wp.rcNormalPosition.left + nSplashWidth;
			wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + nSplashHeight;
			SetWindowPlacement(&wp);

			if (!m_bAutoClose) {
				CWnd* pBtn = GetDlgItem(IDC_BTN_THIRDPARTY);
				if (pBtn) {
					if (m_eDisplayMode == DisplayModeAbout) {
						const int nBtnWidth = 140;
						const int nBtnHeight = 20;
						const int nBtnX = (bmp.bmWidth - nBtnWidth) / 2;
						const int nBtnY = bmp.bmHeight + 2;
						pBtn->MoveWindow(nBtnX, nBtnY, nBtnWidth, nBtnHeight);
						pBtn->SetWindowText(GetResString(_T("EMULE_AI_SPLASH_THIRDPARTY")));
						pBtn->ShowWindow(SW_SHOW);
					} else {
						pBtn->ShowWindow(SW_HIDE);
					}
				}

				const int nCloseBtnSize = 14;
				const int nMargin = 6;
				m_rcCloseBtn.SetRect(
					bmp.bmWidth - nCloseBtnSize - nMargin,
					nMargin,
					bmp.bmWidth - nMargin,
					nMargin + nCloseBtnSize);
			}
		}
	}

	if (IsSpecialThanksMode()) {
		HBITMAP hbmSpecialThanksBackground = theApp.LoadImage(_T("SPECIAL_THANKS_BACKGROUND"), _T("PNG"));
		if (hbmSpecialThanksBackground != NULL)
			VERIFY(m_imgSpecialThanksBackground.Attach(hbmSpecialThanksBackground));
		else
			TRACE(_T("Failed to load Special Thanks background image.\n"));

		InitializeSpecialThanksScene();
	}

	return TRUE;
}

BOOL CSplashScreen::PreTranslateMessage(MSG *pMsg)
{
	if (!m_bAutoClose) {
		switch (pMsg->message) {
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		{
			CWnd* pWnd = WindowFromPoint(pMsg->pt);
			if (m_eDisplayMode == DisplayModeAbout && pWnd && pWnd->GetDlgCtrlID() == IDC_BTN_THIRDPARTY)
				return CDialog::PreTranslateMessage(pMsg);

			if (pMsg->message == WM_LBUTTONDOWN && !m_rcCloseBtn.IsRectEmpty()) {
				CPoint ptClient(pMsg->pt);
				ScreenToClient(&ptClient);
				if (m_rcCloseBtn.PtInRect(ptClient)) {
					DestroyWindow();
					return TRUE;
				}
			}
			return TRUE;
		}
		case WM_KEYDOWN:
			if (pMsg->wParam == VK_ESCAPE) {
				DestroyWindow();
				return TRUE;
			}
			return CDialog::PreTranslateMessage(pMsg);
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
			return TRUE;
		}
		return CDialog::PreTranslateMessage(pMsg);
	}

	switch (pMsg->message) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_NCLBUTTONDOWN:
	case WM_NCRBUTTONDOWN:
	case WM_NCMBUTTONDOWN:
		DestroyWindow();
		break;
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CSplashScreen::AdvanceAnimationFrame()
{
	if (!IsSpecialThanksMode())
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	const float fLoopHeight = GetSpecialThanksLoopHeight(GetSpecialThanksViewportHeight(rcClient));
	m_fSpecialThanksCrawlOffset = WrapPositive(m_fSpecialThanksCrawlOffset + SPECIAL_THANKS_CRAWL_STEP, fLoopHeight);
	m_fSpecialThanksStarDrift = WrapPositive(m_fSpecialThanksStarDrift + SPECIAL_THANKS_STAR_DRIFT_STEP, 8192.0f);
	++m_uSpecialThanksFrame;

	if (GetSafeHwnd() != NULL)
		Invalidate(FALSE);
}

void CSplashScreen::OnCancel()
{
	if (GetSafeHwnd() != NULL)
		DestroyWindow();
}

void CSplashScreen::OnOK()
{
	OnCancel();
}

void CSplashScreen::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (theApp.emuledlg != NULL && theApp.emuledlg->m_pSplashWnd == this)
		theApp.emuledlg->m_pSplashWnd = NULL;
	delete this;
}

void CSplashScreen::OnBnClickedBtnThirdparty()
{
	CString strPath = thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR) + _T("Licenses\\THIRD-PARTY-NOTICES.txt");
	::ShellExecute(m_hWnd, _T("open"), strPath, NULL, NULL, SW_SHOW);
}

void CSplashScreen::DrawCloseButton(CDC& dc)
{
	if (m_rcCloseBtn.IsRectEmpty() || !GetGdiPlusSession().IsReady())
		return;

	Gdiplus::Graphics gfx(dc.GetSafeHdc());
	gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

	Gdiplus::RectF rcBtn(
		static_cast<Gdiplus::REAL>(m_rcCloseBtn.left),
		static_cast<Gdiplus::REAL>(m_rcCloseBtn.top),
		static_cast<Gdiplus::REAL>(m_rcCloseBtn.Width()),
		static_cast<Gdiplus::REAL>(m_rcCloseBtn.Height()));

	Gdiplus::SolidBrush backBrush(Gdiplus::Color(180, 40, 40, 40));
	gfx.FillEllipse(&backBrush, rcBtn);

	Gdiplus::Pen borderPen(Gdiplus::Color(100, 200, 200, 200), 1.0f);
	gfx.DrawEllipse(&borderPen, rcBtn);

	const Gdiplus::REAL fInset = 3.5f;
	Gdiplus::Pen xPen(Gdiplus::Color(230, 230, 230), 1.4f);
	xPen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
	gfx.DrawLine(&xPen, rcBtn.X + fInset, rcBtn.Y + fInset, rcBtn.X + rcBtn.Width - fInset, rcBtn.Y + rcBtn.Height - fInset);
	gfx.DrawLine(&xPen, rcBtn.X + rcBtn.Width - fInset, rcBtn.Y + fInset, rcBtn.X + fInset, rcBtn.Y + rcBtn.Height - fInset);
}

void CSplashScreen::DrawAboutContent(CDC& dc, const BITMAP& BM)
{
	CRect rcClient;
	GetClientRect(&rcClient);
	if (rcClient.bottom > BM.bmHeight)
		dc.FillSolidRect(0, BM.bmHeight, rcClient.Width(), rcClient.bottom - BM.bmHeight, RGB(0, 0, 0));

	CRect rc(0, BM.bmHeight * 65 / 100, BM.bmWidth, BM.bmHeight);
	dc.FillSolidRect(rc.left + 1, rc.top + 1, rc.Width() - 2, rc.Height() - 2, RGB(0, 0, 0));
	dc.SetTextColor(RGB(255, 255, 255));

	LOGFONT lf = {};
#ifdef _BOOTSTRAPNODESDAT
	lf.lfHeight = 24;
#else
#if defined(_DEBUG) && (defined(_BETA) || defined(_DEVBUILD))
	lf.lfHeight = 28;
#else
	lf.lfHeight = 30;
#endif
#endif
	lf.lfWeight = FW_BOLD;
	lf.lfQuality = ANTIALIASED_QUALITY;
	_tcscpy(lf.lfFaceName, _T("Arial"));
	CFont font;
	font.CreateFontIndirect(&lf);
	CFont *pOldFont = dc.SelectObject(&font);
	rc.top += dc.DrawText(theApp.GetAppVersion(), &rc, DT_CENTER | DT_NOPREFIX);
	if (pOldFont)
		dc.SelectObject(pOldFont);
	font.DeleteObject();

	rc.top += 30;
	lf.lfHeight = 14;
	lf.lfWeight = FW_NORMAL;
	lf.lfQuality = ANTIALIASED_QUALITY;
	_tcscpy(lf.lfFaceName, _T("Arial"));
	font.CreateFontIndirect(&lf);
	pOldFont = dc.SelectObject(&font);
	dc.DrawText(GetResString(_T("EMULE_AI_SPLASH_COPYRIGHT")), &rc, DT_CENTER | DT_NOPREFIX);
	rc.top += 16;
	dc.DrawText(GetResString(_T("EMULE_AI_SPLASH_LICENSE")), &rc, DT_CENTER | DT_NOPREFIX);
	rc.top += 16;
	dc.DrawText(GetResString(_T("EMULE_AI_SPLASH_WARRANTY")), &rc, DT_CENTER | DT_NOPREFIX);
	if (pOldFont)
		dc.SelectObject(pOldFont);
	font.DeleteObject();

	if (!m_bAutoClose)
		DrawCloseButton(dc);
}

void CSplashScreen::DrawSpecialThanksScene(CDC& dc, const CRect& rcClient)
{
	dc.FillSolidRect(rcClient, RGB(0, 0, 0));
	if (!GetGdiPlusSession().IsReady())
		return;

	Gdiplus::Graphics gfx(dc.GetSafeHdc());
	gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
	gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
	gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

	const Gdiplus::RectF rcScene(0.0f, 0.0f, static_cast<Gdiplus::REAL>(rcClient.Width()), static_cast<Gdiplus::REAL>(rcClient.Height()));
	if (m_imgSpecialThanksBackground.GetSafeHandle() != NULL) {
		Gdiplus::Bitmap backgroundBitmap(reinterpret_cast<HBITMAP>(m_imgSpecialThanksBackground.GetSafeHandle()), NULL);
		if (backgroundBitmap.GetLastStatus() == Gdiplus::Ok)
			gfx.DrawImage(&backgroundBitmap, rcScene);
		else
			gfx.Clear(Gdiplus::Color(255, 0, 0, 0));
	} else {
		gfx.Clear(Gdiplus::Color(255, 0, 0, 0));
	}

	Gdiplus::LinearGradientBrush sceneShadeBrush(
		Gdiplus::PointF(0.0f, 0.0f),
		Gdiplus::PointF(0.0f, rcScene.Height),
		Gdiplus::Color(46, 0, 0, 0),
		Gdiplus::Color(28, 0, 0, 0));
	gfx.FillRectangle(&sceneShadeBrush, rcScene);

	for (size_t i = 0; i < m_specialThanksStars.size(); ++i) {
		const StarParticle& star = m_specialThanksStars[i];
		const float fY = WrapPositive(star.fBaseY + (m_fSpecialThanksStarDrift * star.fSpeed), rcScene.Height);
		const float fTwinkle = 0.45f + 0.55f * sinf((static_cast<float>(m_uSpecialThanksFrame) * 0.11f) + star.fTwinklePhase);
		const BYTE uAlpha = static_cast<BYTE>(ClampValue<float>(static_cast<float>(star.uAlpha) * (0.55f + (fTwinkle * 0.45f)), 32.0f, 255.0f));

		if (star.fSize > 1.7f) {
			Gdiplus::SolidBrush glowBrush(Gdiplus::Color(uAlpha / 4, 200, 225, 255));
			gfx.FillEllipse(&glowBrush, star.fX - (star.fSize * 1.4f), fY - (star.fSize * 1.4f), star.fSize * 2.8f, star.fSize * 2.8f);
		}

		Gdiplus::SolidBrush starBrush(Gdiplus::Color(uAlpha, 245, 250, 255));
		gfx.FillEllipse(&starBrush, star.fX, fY, star.fSize, star.fSize);
	}

	DrawGlowingTitle(gfx, GetResString(_T("EMULE_AI_MENU_SPECIAL_THANKS")), Gdiplus::RectF(0.0f, SPECIAL_THANKS_TOP_PADDING, rcScene.Width, SPECIAL_THANKS_TITLE_HEIGHT));

	Gdiplus::Pen titleSeparator(Gdiplus::Color(96, 255, 204, 92), 1.0f);
	const Gdiplus::REAL fSeparatorY = SPECIAL_THANKS_TOP_PADDING + SPECIAL_THANKS_TITLE_HEIGHT + 3.0f;
	gfx.DrawLine(&titleSeparator, 22.0f, fSeparatorY, rcScene.Width - 22.0f, fSeparatorY);

	const Gdiplus::RectF rcViewport(
		SPECIAL_THANKS_VIEWPORT_MARGIN,
		SPECIAL_THANKS_TOP_PADDING + SPECIAL_THANKS_TITLE_HEIGHT + 7.0f,
		rcScene.Width - (SPECIAL_THANKS_VIEWPORT_MARGIN * 2.0f),
		rcScene.Height - (SPECIAL_THANKS_TOP_PADDING + SPECIAL_THANKS_TITLE_HEIGHT + 14.0f));

	const Gdiplus::GraphicsState nState = gfx.Save();
	gfx.SetClip(rcViewport);

	const float fLoopHeight = GetSpecialThanksLoopHeight(static_cast<float>(rcViewport.Height));
	const float fCrawlOffset = WrapPositive(m_fSpecialThanksCrawlOffset, fLoopHeight);
	std::vector<CString> lines;
	for (int nRepeat = 0; nRepeat < 2; ++nRepeat) {
		Gdiplus::REAL fCursorY = rcViewport.Y + rcViewport.Height + SPECIAL_THANKS_BOTTOM_ENTRY_OFFSET + (static_cast<Gdiplus::REAL>(nRepeat) * fLoopHeight) - fCrawlOffset;

		SplitWrappedText(GetResString(_T("EMULE_AI_SPECIAL_THANKS_INTRO")), SPECIAL_THANKS_INTRO_WRAP_CHAR_COUNT, lines);
		for (size_t nLine = 0; nLine < lines.size(); ++nLine) {
			DrawPerspectiveCreditsLine(gfx, rcViewport, fCursorY, lines[nLine], false, true);
			fCursorY += SPECIAL_THANKS_INTRO_SPACING;
		}
		fCursorY += SPECIAL_THANKS_BODY_SPACING * SPECIAL_THANKS_INTRO_GAP_LINE_COUNT;

		for (size_t i = 0; i < _countof(kSpecialThanksSections); ++i) {
			DrawPerspectiveCreditsLine(gfx, rcViewport, fCursorY, GetResString(kSpecialThanksSections[i].pszHeadingKey), true, false);
			fCursorY += SPECIAL_THANKS_HEADING_SPACING;

			SplitMultilineText(kSpecialThanksSections[i].pszNames, lines);
			for (size_t nLine = 0; nLine < lines.size(); ++nLine) {
				DrawPerspectiveCreditsLine(gfx, rcViewport, fCursorY, lines[nLine], false, false);
				fCursorY += SPECIAL_THANKS_BODY_SPACING;
			}

			fCursorY += SPECIAL_THANKS_SECTION_GAP;
		}
	}

	gfx.Restore(nState);

	Gdiplus::LinearGradientBrush topFadeBrush(
		Gdiplus::PointF(0.0f, rcViewport.Y - SPECIAL_THANKS_FADE_EDGE_OVERDRAW),
		Gdiplus::PointF(0.0f, rcViewport.Y + SPECIAL_THANKS_TOP_FADE_HEIGHT),
		Gdiplus::Color(150, 0, 0, 0),
		Gdiplus::Color(0, 0, 0, 0));
	gfx.FillRectangle(&topFadeBrush, Gdiplus::RectF(rcViewport.X - 4.0f, rcViewport.Y - SPECIAL_THANKS_FADE_EDGE_OVERDRAW, rcViewport.Width + 8.0f, SPECIAL_THANKS_TOP_FADE_HEIGHT + SPECIAL_THANKS_FADE_EDGE_OVERDRAW));

	Gdiplus::LinearGradientBrush bottomFadeBrush(
		Gdiplus::PointF(0.0f, rcViewport.Y + rcViewport.Height - SPECIAL_THANKS_BOTTOM_FADE_HEIGHT),
		Gdiplus::PointF(0.0f, rcViewport.Y + rcViewport.Height + SPECIAL_THANKS_FADE_EDGE_OVERDRAW),
		Gdiplus::Color(0, 0, 0, 0),
		Gdiplus::Color(146, 0, 0, 0));
	gfx.FillRectangle(&bottomFadeBrush, Gdiplus::RectF(rcViewport.X - 4.0f, rcViewport.Y + rcViewport.Height - SPECIAL_THANKS_BOTTOM_FADE_HEIGHT, rcViewport.Width + 8.0f, SPECIAL_THANKS_BOTTOM_FADE_HEIGHT + SPECIAL_THANKS_FADE_EDGE_OVERDRAW));

	DrawCloseButton(dc);
}

void CSplashScreen::OnPaint()
{
	CPaintDC dc(this);

	CRect rcClient;
	GetClientRect(&rcClient);
	if (rcClient.IsRectEmpty())
		return;

	CDC dcBuffer;
	if (!dcBuffer.CreateCompatibleDC(&dc))
		return;

	CBitmap bmpBuffer;
	if (!bmpBuffer.CreateCompatibleBitmap(&dc, rcClient.Width(), rcClient.Height()))
		return;

	CBitmap* pOldBufferBitmap = dcBuffer.SelectObject(&bmpBuffer);
	dcBuffer.FillSolidRect(rcClient, RGB(0, 0, 0));

	if (IsSpecialThanksMode()) {
		DrawSpecialThanksScene(dcBuffer, rcClient);
	} else if (m_imgSplash.GetSafeHandle()) {
		CDC dcImage;
		if (dcImage.CreateCompatibleDC(&dc)) {
			CBitmap* pOldImageBitmap = dcImage.SelectObject(&m_imgSplash);
			BITMAP BM = {};
			m_imgSplash.GetBitmap(&BM);
			dcBuffer.BitBlt(0, 0, BM.bmWidth, BM.bmHeight, &dcImage, 0, 0, SRCCOPY);
			if (pOldImageBitmap)
				dcImage.SelectObject(pOldImageBitmap);
			DrawAboutContent(dcBuffer, BM);
		}
	}

	dc.BitBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcBuffer, 0, 0, SRCCOPY);
	if (pOldBufferBitmap)
		dcBuffer.SelectObject(pOldBufferBitmap);
}
