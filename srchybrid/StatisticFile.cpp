// parts of this file are based on work from pan One (http://home-3.tiscali.nl/~meost/pms/)
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
#include "StatisticFile.h"
#include "emule.h"
#include "KnownFile.h"
#include "KnownFileList.h"
#include "SharedFileList.h"
#include "BarShader.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	CBarShader* g_pSharedFileSpreadHistoryBar = NULL;

	CBarShader& GetSharedFileSpreadHistoryBar()
	{
		if (g_pSharedFileSpreadHistoryBar == NULL)
			g_pSharedFileSpreadHistoryBar = new CBarShader(16);
		return *g_pSharedFileSpreadHistoryBar;
	}
}

void CStatisticFile::MergeFileStats(CStatisticFile *toMerge)
{
	requested += toMerge->GetRequests();
	accepted += toMerge->GetAccepts();
	transferred += toMerge->GetTransferred();
	SetAllTimeRequests(alltimerequested + toMerge->GetAllTimeRequests());
	SetAllTimeTransferred(alltimetransferred + toMerge->GetAllTimeTransferred());
	SetAllTimeAccepts(alltimeaccepted + toMerge->GetAllTimeAccepts());

	CSpreadEntryArray aSpreadEntries;
	toMerge->GetSpreadListSnapshot(aSpreadEntries);
	if (!aSpreadEntries.IsEmpty()) {
		uint64 start = aSpreadEntries[0].uStart;
		uint64 count = aSpreadEntries[0].uCount;
		for (INT_PTR i = 1; i < aSpreadEntries.GetSize(); ++i) {
			uint64 end = aSpreadEntries[i].uStart;
			if (count != 0)
				AddBlockTransferred(start, end, count);
			start = end;
			count = aSpreadEntries[i].uCount;
		}
	}
}

void CStatisticFile::AddRequest()
{
	++requested;
	++alltimerequested;
	++theApp.knownfiles->requested;
	theApp.sharedfiles->UpdateFile(fileParent);
}

void CStatisticFile::AddAccepted()
{
	++accepted;
	++alltimeaccepted;
	++theApp.knownfiles->accepted;
	theApp.sharedfiles->UpdateFile(fileParent);
}

void CStatisticFile::AddTransferred(uint64 bytes)
{
	transferred += bytes;
	alltimetransferred += bytes;
	theApp.knownfiles->transferred += bytes;
	theApp.sharedfiles->UpdateFile(fileParent);
}

void CStatisticFile::AddBlockTransferred(uint64 start, uint64 end, uint64 count)
{
	if (start >= end || count == 0 || fileParent == NULL)
		return;

	const int iSpreadbarSetStatus = fileParent->GetSpreadbarSetStatus();
	if (iSpreadbarSetStatus == 0 || (iSpreadbarSetStatus < 0 && !thePrefs.GetSpreadbarSetStatus()))
		return;

	CSingleLock lockSpreadList(&m_mutSpreadList, TRUE);
	if (spreadlist.IsEmpty())
		spreadlist.SetAt(0, 0);

	POSITION endpos = spreadlist.FindFirstKeyAfter(end);
	if (endpos != NULL)
		spreadlist.GetPrev(endpos);
	else
		endpos = spreadlist.GetTailPosition();

	if (endpos == NULL)
		return;

	uint64 endcount = spreadlist.GetValueAt(endpos);
	endpos = spreadlist.SetAt(end, endcount);

	POSITION startpos = spreadlist.FindFirstKeyAfter(start);
	for (POSITION pos = startpos; pos != endpos && pos != NULL; spreadlist.GetNext(pos))
		spreadlist.SetValueAt(pos, spreadlist.GetValueAt(pos) + count);

	spreadlist.GetPrev(startpos);
	if (startpos == NULL)
		return;

	uint64 startcount = spreadlist.GetValueAt(startpos) + count;
	startpos = spreadlist.SetAt(start, startcount);

	POSITION prevpos = startpos;
	spreadlist.GetPrev(prevpos);
	if (prevpos != NULL && spreadlist.GetValueAt(prevpos) == startcount)
		spreadlist.RemoveAt(startpos);

	prevpos = endpos;
	spreadlist.GetPrev(prevpos);
	if (prevpos != NULL && spreadlist.GetValueAt(prevpos) == endcount)
		spreadlist.RemoveAt(endpos);
}

void CStatisticFile::ResetSpreadBar()
{
	CSingleLock lockSpreadList(&m_mutSpreadList, TRUE);
	spreadlist.RemoveAll();
	spreadlist.SetAt(0, 0);
}

void CStatisticFile::GetSpreadListSnapshot(CSpreadEntryArray& aEntries) const
{
	aEntries.RemoveAll();

	CSingleLock lockSpreadList(&m_mutSpreadList, TRUE);
	if (spreadlist.IsEmpty())
		return;

	aEntries.SetSize(spreadlist.GetCount());
	INT_PTR iEntry = 0;
	for (POSITION pos = spreadlist.GetHeadPosition(); pos != NULL;) {
		aEntries[iEntry].uStart = spreadlist.GetKeyAt(pos);
		aEntries[iEntry].uCount = spreadlist.GetValueAt(pos);
		spreadlist.GetNext(pos);
		++iEntry;
	}
}

void CStatisticFile::DrawSpreadBar(CDC* dc, LPCRECT rect, bool bFlat) const
{
	if (dc == NULL || rect == NULL || fileParent == NULL)
		return;

	const int iWidth = rect->right - rect->left;
	const int iHeight = rect->bottom - rect->top;
	if (iWidth <= 0 || iHeight <= 0)
		return;

	CSpreadEntryArray aSpreadEntries;
	GetSpreadListSnapshot(aSpreadEntries);

	CBarShader& spreadBar = GetSharedFileSpreadHistoryBar();
	spreadBar.SetHeight(iHeight);
	spreadBar.SetWidth(iWidth);
	const uint64 uFileSize = static_cast<uint64>(fileParent->GetFileSize());
	spreadBar.SetFileSize((uFileSize > 0) ? uFileSize : static_cast<uint64>(1));
	spreadBar.Fill(RGB(0, 0, 0));

	for (INT_PTR i = 0; i + 1 < aSpreadEntries.GetSize(); ++i) {
		const uint64 uCount = aSpreadEntries[i].uCount;
		if (uCount == 0)
			continue;

		const int iGreen = (uCount >= 11) ? 0 : static_cast<int>(232 - (22 * uCount));
		spreadBar.FillRange(aSpreadEntries[i].uStart, aSpreadEntries[i + 1].uStart, RGB(0, iGreen, 255));
	}

	spreadBar.Draw(dc, rect->left, rect->top, bFlat);
}

float CStatisticFile::GetSpreadSortValue() const
{
	const uint64 uFileSize = (fileParent != NULL) ? static_cast<uint64>(fileParent->GetFileSize()) : static_cast<uint64>(0);
	if (uFileSize == 0)
		return 0.0f;

	CSpreadEntryArray aSpreadEntries;
	GetSpreadListSnapshot(aSpreadEntries);
	if (aSpreadEntries.GetSize() < 2)
		return 0.0f;

	uint64 uTotalWeightedSpread = 0;
	for (INT_PTR i = 0; i + 1 < aSpreadEntries.GetSize(); ++i) {
		const uint64 uSpan = aSpreadEntries[i + 1].uStart - aSpreadEntries[i].uStart;
		uTotalWeightedSpread += uSpan * aSpreadEntries[i].uCount;
	}

	const double dAverageSpread = static_cast<double>(uTotalWeightedSpread) / static_cast<double>(uFileSize);
	double dSpreadScore = 0.0;
	for (INT_PTR i = 0; i + 1 < aSpreadEntries.GetSize(); ++i) {
		const uint64 uSpan = aSpreadEntries[i + 1].uStart - aSpreadEntries[i].uStart;
		const double dCount = static_cast<double>(aSpreadEntries[i].uCount);
		dSpreadScore += ((dCount > dAverageSpread) ? dAverageSpread : dCount) * static_cast<double>(uSpan);
	}

	return static_cast<float>(dSpreadScore / static_cast<double>(uFileSize));
}

void CStatisticFile::SetAllTimeRequests(uint32 nVal)
{
	alltimerequested = nVal;
}

void CStatisticFile::SetAllTimeAccepts(uint32 nVal)
{
	alltimeaccepted = nVal;
}

void CStatisticFile::SetAllTimeTransferred(uint64 nVal)
{
	alltimetransferred = nVal;
}
