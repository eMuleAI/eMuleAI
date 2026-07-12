//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "emule.h"
#include "SpeedGraph.h"
#include "MemDC.h"
#include <commctrl.h>
#include <intshcut.h>
#include <wininet.h> 

IMPLEMENT_DYNAMIC(CSpeedGraph, CDialog)
CSpeedGraph::CSpeedGraph(CWnd* pParent /*=NULL*/)
	: CDialog(CSpeedGraph::IDD, pParent)
{
	brushInitalized = FALSE;
	interfaceCallBack = NULL;
	TrafficStats=NULL;
	gridxstartpos = 0;
	gridystartpos = 0;
	gridxresolution		=	GRIDXRESOLUTION;
	gridyresolution		=	GRIDYRESOLUTION;
	gridscrollxspeed	=	GRIDSCROLLXSPEED;
	gridscrollyspeed	=	GRIDSCROLLYSPEED; 
	plotgranularity		=	PLOTGRANULATRITY;
	gridupdatespeed		=	GRIDUPDATESPEED;
}

CSpeedGraph::~CSpeedGraph()
{
	if(TrafficStats)
		delete[] TrafficStats; 

	redbrush.DeleteObject();
	greenbrush.DeleteObject();
	bluebrush.DeleteObject();
}

void CSpeedGraph::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CSpeedGraph, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CSpeedGraph::OnPaint()
{
	CPaintDC dc(this); // device context for painting
	CDC * pDC   = CDC::FromHandle( dc );
	int nSavedDC = pDC -> SaveDC( );

	// Create the brush for the color bar
	if(brushInitalized == FALSE) {
		CBitmap bmp;
		CMemoryDC *memDC = new CMemoryDC(pDC);
		RECT clipRect;
		memDC->GetClipBox(&clipRect);

			if(clipRect.right - clipRect.left > 1) {
				bmp.CreateCompatibleBitmap(memDC,plotgranularity, TGSize.cy);
				CBitmap *pOld = memDC->SelectObject(&bmp);
				
				// Need for scaling the color to the size of button
				double factor = 255.0 / (float)TGSize.cy;
			BYTE r,g,b;
			for(int x = 0; x<TGSize.cy; x++) {
				g = (BYTE)(255-factor*x);
				r = (BYTE)(factor*x);
				b = (BYTE)64;
				memDC->SetPixelV(0,x,RGB(r,g,b));
				memDC->SetPixelV(1,x,RGB(r,g,b));
			}

			memDC->SelectObject(pOld);
			colorbrush.CreatePatternBrush(&bmp);	
			brushInitalized = TRUE;
		}

		delete memDC;
	}

	if(initalized == TRUE) {
		COLORREF backcolor = GetSysColor(COLOR_BTNFACE);
		CMemoryDC *memDC = new CMemoryDC(pDC);
		RECT clipRect;
		memDC->GetClipBox(&clipRect);
		memDC->FillSolidRect(&clipRect,backcolor);
		
		CFont *oldFont;
		int xp, yp, xx, yy;
		orgBrushOrigin = memDC->GetBrushOrg();
		oldFont = memDC->SelectObject(&smallFont);
		double scale = (double)TGSize.cy / (double)m_ui_MaxAmount;
		yp = TrafficDrawRectangle.bottom;
		xp = TrafficDrawRectangle.left;
		RECT fillrect;
		CString tmp;
		
		// Fill the background
		back = memDC->GetBkColor();
		bluebrush.DeleteObject();
		bluebrush.CreateSolidBrush(darkblue);
		memDC->FillRect(&TrafficDrawRectangle, &bluebrush);

		// draw the grid
		int xgridlines, ygridlines;
		
		xgridlines = TGSize.cx / gridxresolution;
		ygridlines = TGSize.cy / gridyresolution;
		CPen* oldPen = memDC->SelectObject(&GridPen);

		// Create the vertical lines
		for (int x=0; x<= xgridlines; x++) {
			memDC->MoveTo(x*gridxresolution + gridxstartpos, 0			);
			memDC->LineTo(x*gridxresolution + gridxstartpos, TGSize.cy	);
		}

		// And the horizontal lines
		for (int y=0; y<= ygridlines; y++) {
			memDC->MoveTo(0			, gridystartpos + TGSize.cy - y*gridyresolution - 2);
			memDC->LineTo(TGSize.cx	, gridystartpos + TGSize.cy - y*gridyresolution - 2);
		}

		gridxstartpos += gridscrollxspeed;
		gridystartpos += gridscrollyspeed;
		if(gridxstartpos < 0				) gridxstartpos = gridxresolution;
		if(gridxstartpos > gridxresolution	) gridxstartpos = 0;
		if(gridystartpos < 0				) gridystartpos = gridyresolution;
		if(gridystartpos > gridyresolution	) gridystartpos = 0;
		memDC->SelectObject(oldPen );

		for(DWORD cnt=0; cnt<TrafficEntries; cnt++)	{
			xx = xp + cnt*plotgranularity;
			yy = yp - (int)((double)TrafficStats[cnt].value * scale);

			// Just paint if we are connected...
			fillrect.bottom = yp;
			fillrect.top	= yy;
			fillrect.left	= xx;
			fillrect.right	= xx+plotgranularity;
			memDC->SetBrushOrg(xx,yp);
			if(TrafficStats[cnt].value > 0.0) {
				memDC->FillRect(&fillrect, &colorbrush);
				memDC->SetPixelV(xx, yy, orange);
			}
		}
	
		// last print the textual statistic
		tmp.Format(_T("%8.1f"),TrafficStats[TrafficEntries-1].value);
		COLORREF textcolor = memDC->GetTextColor();
		int bkmode = memDC->GetBkMode();
		memDC->SetBkMode(TRANSPARENT);
		memDC->SetTextColor(darkblue);
		memDC->TextOut(6,9,AllTraffic);
		memDC->SetTextColor(white);
		memDC->TextOut(5,9,AllTraffic); 
		memDC->SetTextColor(textcolor);
		memDC->SetBkMode(bkmode);
		memDC->SelectObject(oldFont);
		memDC->SetBrushOrg(orgBrushOrigin.x, orgBrushOrigin.y);
		delete memDC;
	}

	pDC -> RestoreDC( nSavedDC );
}

void CSpeedGraph::PreSubclassWindow()
{
	// make sure we are an owner draw button
	CDialog::PreSubclassWindow();
}

void CSpeedGraph::Set_TrafficValue(ULONGLONG ul_value) 
{
	double traffic = (double)ul_value;
	double delta1;
	delta1 = (double)(traffic) / 1024.0;
	
	// Shift whole array 1 step to left and calculate local maximum
	for(DWORD x=0; x<TrafficEntries-1; x++)
		TrafficStats[x].value	= TrafficStats[x+1].value;

	TrafficStats[TrafficEntries-1].value = traffic;
	AllTraffic.Format(_T("%s %.1f kB/s"),m_s_Type,delta1);
	Invalidate(FALSE);
}

void CSpeedGraph::OnTimer(UINT nIDEvent) 
{
	Invalidate(FALSE);
	CDialog::OnTimer(nIDEvent);
}

void CSpeedGraph::Init_Graph(CString s_type,UINT i_maxvalue)
{
	m_s_Type.Format(_T("%s"),s_type);
	m_ui_MaxAmount = i_maxvalue*1024;
}

BOOL CSpeedGraph::OnInitDialog()
{
	CDialog::OnInitDialog();
	ModifyStyle(0, WS_CLIPSIBLINGS);

	SetWindowPos(NULL,m_rect.left,m_rect.top,m_rect.right-m_rect.left,m_rect.bottom-m_rect.top,SWP_SHOWWINDOW);

	this->GetWindowRect(&TrafficDrawRectangle);
	this->GetWindowRect(&TrafficDrawUpdateRectangle);
	ScreenToClient(&TrafficDrawUpdateRectangle);
	ScreenToClient(&TrafficDrawRectangle);

	TGSize.cx = TrafficDrawRectangle.right - TrafficDrawRectangle.left;
	TGSize.cy = TrafficDrawRectangle.bottom - TrafficDrawRectangle.top;

	initalized = FALSE;
	m_ui_MaxAmount = 0.0;

	smallFont.CreateFont(-10,0,0,0,FW_THIN, FALSE, FALSE, FALSE, DEFAULT_CHARSET,OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH, _T("Arial"));

	red		= RGB(255, 0, 0);
	green	= RGB(30, 200, 30);
	darkblue= RGB(0, 0, 105);
	darkgray= RGB(50, 50, 50);
	white	= RGB(255, 255, 255);
	black	= RGB(0, 0, 0);
	lightgreen	= RGB(156, 255, 156);
	gray	= RGB(75, 75, 75);
	orange	= RGB(255, 190, 0);

	greenbrush.DeleteObject();
	greenbrush.CreateSolidBrush(green);
	redbrush.DeleteObject();
	redbrush.CreateSolidBrush(red);

	GridPen.CreatePen(PS_SOLID ,1 , gray);

	GetWindowRect(&TrafficDrawRectangle);
	ScreenToClient(&TrafficDrawRectangle);
	
	TGSize.cx = TrafficDrawRectangle.right - TrafficDrawRectangle.left;
	TGSize.cy = TrafficDrawRectangle.bottom - TrafficDrawRectangle.top;
	
	TrafficEntries = TGSize.cx / plotgranularity;
	TrafficStats = new TRAFFICENTRY[TrafficEntries];

	for(DWORD x=0; x<TrafficEntries; x++)
		TrafficStats[x].value	= 0.0;
	
	m_ui_MaxAmount = 0.0;

	CRgn rectRgn, ellRgn, finalRgn;
	rectRgn.CreateRectRgn(0,0,TGSize.cx, TGSize.cy);
	ShapeWNDRegion.CreateRectRgn(0,0,TGSize.cx, TGSize.cy);;
	ShapeDCRegion.CreateRectRgn(0,0,TGSize.cx, TGSize.cy);;

	int x1,x2,y1,y2,xe,ye,xs,ys;
	int xof, yof;
	int r;
	xs = TGSize.cx;
	ys = TGSize.cy;
	x1 = 0;
	y1 = 0;
	x2 = xs;
	y2 = ys;
	xe = 5; //Radius of edge
	ye = 5; //Radius of edge
	xof = (int)( (float)xs*0.0);
	yof = (int)( (float)ys*0.0);
	r = ellRgn.CreateRoundRectRgn(x1,y1,x2,y2,xe,ye);
	r = ellRgn.OffsetRgn(-xof, -yof);
	r = ShapeWNDRegion.CombineRgn(&rectRgn, &ellRgn,RGN_AND );
	r = ShapeDCRegion.CombineRgn(&rectRgn, &ellRgn,RGN_AND );

	this->SetWindowRgn((HRGN)ShapeWNDRegion, TRUE);
	initalized = TRUE;
	SetTimer(GRIDTIMER,	gridupdatespeed,	0);

	return TRUE;  // return TRUE unless you set the focus to a control
}

BOOL CSpeedGraph::Create(UINT nIDTemplate,RECT &rect,CWnd* pParentWnd)
{
	m_rect = rect;
	return CDialog::Create(nIDTemplate, pParentWnd);
}

BOOL CSpeedGraph::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}
