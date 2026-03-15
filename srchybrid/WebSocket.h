#pragma once

#include "mbedtls/ssl.h"

class CWebServer;

void StartSockets(CWebServer *pThis);
void StopSockets();

class CWebSocket
{
public:
	void SetParent(CWebServer *pParent);
	CWebServer *m_pParent;

	class CChunk
	{
	public:
		char *m_pData;
		char *m_pToSend;
		CChunk *m_pNext;
		DWORD m_dwSize;

		// Ensure members are initialized to avoid undefined behavior when a plain CChunk is created (see Disconnect where `new CChunk()` is used).
		CChunk()
			: m_pData(NULL)
			, m_pToSend(NULL)
			, m_pNext(NULL)
			, m_dwSize(0)
		{
		}

		~CChunk()
		{
			// Guard deletion; delete[] on NULL is safe, but ensure pointer is valid.
			if (m_pData) {
				delete[] m_pData;
				m_pData = NULL;
			}
			m_pToSend = NULL;
			m_pNext = NULL;
		}
	};

	CChunk *m_pHead; // tails of what has to be sent
	CChunk *m_pTail;

	char *m_pBuf;
	mbedtls_ssl_context *m_ssl;
	SOCKET m_hSocket;
	DWORD m_dwRecv;
	DWORD m_dwBufSize;
	DWORD m_dwHttpHeaderLen;
	DWORD m_dwHttpContentLen;

	bool m_bCanRecv;
	bool m_bCanSend;
	bool m_bValid;

	void OnReceived(void *pData, DWORD dwSize, const in_addr inad); // must be implemented
	void SendData(const void *pData, DWORD dwDataSize);
	void SendData(LPCSTR szText)		{ SendData(szText, (DWORD)strlen(szText)); }
	void SendContent(LPCSTR szStdResponse, const void *pContent, DWORD dwContentSize);
	void SendContent(LPCSTR szStdResponse, const CString &rstr);
	void SendReply(LPCSTR szReply);
	void Disconnect();

	void OnRequestReceived(const char *pHeader, DWORD dwHeaderLen, const char *pData, DWORD dwDataLen, const in_addr inad);
};