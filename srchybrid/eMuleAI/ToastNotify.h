//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include "../TaskbarNotifier.h"
#include <memory>

class CToastNotify
{
public:
	CToastNotify();
	~CToastNotify();

	bool Show(HWND hWndNotify, LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink);
	void Shutdown();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
