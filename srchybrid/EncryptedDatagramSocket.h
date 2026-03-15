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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "eMuleAI/Address.h"

class CEncryptedDatagramSocket
{
public:
	CEncryptedDatagramSocket() = default;
	virtual	~CEncryptedDatagramSocket() = default;

protected:
	static int DecryptReceivedClient(BYTE* pbyBufIn, int nBufLen, BYTE** ppbyBufOut, const CAddress& IP, uint32* nReceiverVerifyKey, uint32* nSenderVerifyKey);
	static uint32 EncryptSendClient(uchar* pbyBuf, uint32 nBufLen, const uchar* pachClientHashOrKadID, bool bKad, uint32 nReceiverVerifyKey, uint32 nSenderVerifyKey, bool bIPv6);

	static int DecryptReceivedServer(BYTE *pbyBufIn, int nBufLen, BYTE **ppbyBufOut, uint32 dwBaseKey, const SOCKADDR_IN &dbgIP);
	static uint32 EncryptSendServer(uchar *pbyBuf, uint32 nBufLen, uint32 dwBaseKey);

	static int EncryptOverheadSize(bool bKad);
};