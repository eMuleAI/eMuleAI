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
#include "Scheduler.h"
#include "OtherFunctions.h"
#include "ini2.h"
#include "Preferences.h"
#include "DownloadQueue.h"
#include "emuledlg.h"
#include "MenuCmds.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CScheduler::CScheduler()
{
	LoadFromFile();
	SaveOriginals();
	m_iLastCheckedMinute = 60;
}

CScheduler::~CScheduler()
{
	SaveToFile();
	RemoveAll();
}

int CScheduler::LoadFromFile()
{

	CString strName(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	strName += _T("preferences.ini");
	CIni ini(strName, _T("Scheduler"));

	UINT max = ini.GetInt(_T("Count"), 0);
	UINT count;
	for (count = 0; count < max; ++count) {
		strName.Format(_T("Schedule#%u"), count);
		const CString &temp(ini.GetString(_T("Title"), EMPTY, strName));
		if (temp.IsEmpty())
			break;
		Schedule_Struct *news = new Schedule_Struct();
		news->title = temp;
		news->day = ini.GetInt(_T("Day"), 0);
		news->enabled = ini.GetBool(_T("Enabled"));
		news->time = ini.GetInt(_T("StartTime"));
		news->time2 = ini.GetInt(_T("EndTime"));
		ini.SerGet(true, news->actions, _countof(news->actions), _T("Actions"));
		ini.SerGet(true, news->values, _countof(news->values), _T("Values"));
		AddSchedule(news);
	}
	return count;
}

void CScheduler::SaveToFile()
{
	CIni ini(thePrefs.GetConfigFile(), _T("Scheduler"));
	ini.WriteInt(_T("Count"), (int)GetCount());

	for (INT_PTR i = 0; i < GetCount(); ++i) {
		Schedule_Struct *schedule = theApp.scheduler->GetSchedule(i);
		CString temp;
		temp.Format(_T("Schedule#%i"), (int)i);
		ini.WriteString(_T("Title"), schedule->title, temp);
		ini.WriteInt(_T("Day"), schedule->day);
		ini.WriteInt(_T("StartTime"), (int)schedule->time);
		ini.WriteInt(_T("EndTime"), (int)schedule->time2);
		ini.WriteBool(_T("Enabled"), schedule->enabled);

		ini.SerGet(false, schedule->actions, _countof(schedule->actions), _T("Actions"));
		ini.SerGet(false, schedule->values, _countof(schedule->values), _T("Values"));
	}
}

void CScheduler::RemoveSchedule(INT_PTR index)
{
	if (index < GetCount()) {
		delete schedulelist[index];
		schedulelist.RemoveAt(index);
	}
}

void CScheduler::RemoveAll()
{
	while (!schedulelist.IsEmpty())
		RemoveSchedule(0);
}

INT_PTR CScheduler::AddSchedule(Schedule_Struct *schedule)
{
	schedulelist.Add(schedule);
	return GetCount() - 1;
}

int CScheduler::Check(bool forcecheck)
{
	if (!thePrefs.IsSchedulerEnabled()
		|| theApp.scheduler->GetCount() == 0
		|| theApp.IsClosing())
	{
		return -1;
	}
	struct tm tmTemp;
	CTime tNow = CTime(safe_mktime(CTime::GetCurrentTime().GetLocalTm(&tmTemp)));

	if (!forcecheck && tNow.GetMinute() == m_iLastCheckedMinute)
		return -1;

	m_iLastCheckedMinute = tNow.GetMinute();
	theApp.scheduler->RestoreOriginals();

	for (INT_PTR i = 0; i < theApp.scheduler->GetCount(); ++i) {
		const Schedule_Struct *schedule = theApp.scheduler->GetSchedule(i);
		if (!schedule->actions[0] || !schedule->enabled)
			continue;

		// check the day of the week
		if (schedule->day != DAY_DAILY) {
			UINT dow = (UINT)tNow.GetDayOfWeek();
			switch (schedule->day) {
			case DAY_MO:
			case DAY_DI:
			case DAY_MI:
			case DAY_DO:
			case DAY_FR:
			case DAY_SA:
			case DAY_SO:
				if ((schedule->day % 7) + 1 != dow)
					continue;
				break;
			case DAY_MO_FR:
				if (dow == 7 || dow == 1)
					continue;
				break;
			case DAY_MO_SA:
				if (dow == 1)
					continue;
				break;
			case DAY_SA_SO:
				if (dow >= 2 && dow <= 6)
					continue;
			}
		}
		//check time
		CTime t1 = CTime(schedule->time);
		CTime t2 = CTime(schedule->time2);
		int it1 = t1.GetHour() * 60 + t1.GetMinute();
		int it2 = t2.GetHour() * 60 + t2.GetMinute();
		int itn = tNow.GetHour() * 60 + tNow.GetMinute();
		if (it1 <= it2) { // normal timespan
			if (itn < it1 || itn >= it2)
				continue;
		} else {		   // reversed timespan (23:30 to 5:10)  now 10
			if (itn < it1 && itn >= it2)
				continue;
		}
		// OK, lets do the actions of this schedule
		ActivateSchedule(i, schedule->time2 == 0);
	}

	return -1;
}

void CScheduler::SaveOriginals()
{
	original_upload = thePrefs.GetMaxUpload();
	original_download = thePrefs.GetMaxDownload();
	original_connections = thePrefs.GetMaxConnections();
	original_cons5s = thePrefs.GetMaxConperFive();
	original_sources = thePrefs.GetMaxSourcePerFileDefault();
}

void CScheduler::RestoreOriginals()
{
	thePrefs.SetMaxUpload(original_upload);
	thePrefs.SetMaxDownload(original_download);
	thePrefs.SetMaxConnections(original_connections);
	thePrefs.SetMaxConsPerFive(original_cons5s);
	thePrefs.SetMaxSourcesPerFile(original_sources);
}

void CScheduler::ActivateSchedule(INT_PTR index, bool makedefault)
{
	Schedule_Struct *schedule = GetSchedule(index);

	for (int ai = 0; ai < 16 && schedule->actions[ai]; ++ai) {
		if (schedule->values[ai].IsEmpty() /* maybe ignore in some future cases...*/)
			continue;

		switch (schedule->actions[ai]) {
		case ACTION_SETUPL:
			thePrefs.SetMaxUpload(_tstoi(schedule->values[ai]));
			if (makedefault)
				original_upload = (uint16)_tstoi(schedule->values[ai]);
			break;
		case ACTION_SETDOWNL:
			thePrefs.SetMaxDownload(_tstoi(schedule->values[ai]));
			if (makedefault)
				original_download = (uint16)_tstoi(schedule->values[ai]);
			break;
		case ACTION_SOURCESL:
			thePrefs.SetMaxSourcesPerFile(_tstoi(schedule->values[ai]));
			if (makedefault)
				original_sources = _tstoi(schedule->values[ai]);
			break;
		case ACTION_CON5SEC:
			thePrefs.SetMaxConsPerFive(_tstoi(schedule->values[ai]));
			if (makedefault)
				original_cons5s = _tstoi(schedule->values[ai]);
			break;
		case ACTION_CONS:
			thePrefs.SetMaxConnections(_tstoi(schedule->values[ai]));
			if (makedefault)
				original_connections = _tstoi(schedule->values[ai]);
			break;
		case ACTION_CATSTOP:
			theApp.downloadqueue->SetCatStatus(_tstoi(schedule->values[ai]), MP_STOP);
			break;
		case ACTION_CATRESUME:
			theApp.downloadqueue->SetCatStatus(_tstoi(schedule->values[ai]), MP_RESUME);
		}
	}
}