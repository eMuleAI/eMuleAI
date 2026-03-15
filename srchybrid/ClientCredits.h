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
#pragma once
#include "MapKey.h"
#include "cryptopp/rsa.h"
#include "eMuleAI/Address.h"

#define	 MAXPUBKEYSIZE		80

#define CRYPT_CIP_REMOTECLIENT	10
#define CRYPT_CIP_LOCALCLIENT	20
#define CRYPT_CIP_NONECLIENT	30

#pragma pack(push, 1)
struct CreditStruct_29a
{
	uchar		abyKey[MDX_DIGEST_SIZE];
	uint32		nUploadedLo;	// uploaded TO him
	uint32		nDownloadedLo;	// downloaded from him
	uint32		nLastSeen;
	uint32		nUploadedHi;	// upload high 32
	uint32		nDownloadedHi;	// download high 32
	uint16		nReserved3;
};

struct CreditStruct
{
	uchar		abyKey[MDX_DIGEST_SIZE];
	uint32		nUploadedLo;	// uploaded TO him
	uint32		nDownloadedLo;	// downloaded from him
	uint32		nLastSeen;
	uint32		nUploadedHi;	// upload high 32
	uint32		nDownloadedHi;	// download high 32
	uint16		nReserved3;
	uint8		nKeySize;
	uchar		abySecureIdent[MAXPUBKEYSIZE];
};
#pragma pack(pop)

enum EIdentState
{
	IS_NOTAVAILABLE,
	IS_IDNEEDED,
	IS_IDENTIFIED,
	IS_IDFAILED,
	IS_IDBADGUY,
};

enum CreditSystemSelection {
	//becareful the sort order for the damn radio button in PPgEastShare.cpp and the check on creditSystemMode in preferences.cpp
	CS_OFFICIAL = 0,
	CS_LOVELACE,
	CS_RATIO,
	CS_PAWCIO,
	CS_EASTSHARE,
	CS_MAGICANGEL,
	CS_MAGICANGELPLUS,
	CS_SIVKA,
	CS_SWAT,
	CS_TK4,
	CS_XTREME,
	CS_ZZUL
};

class CClientCredits
{
	friend class CClientCreditsList;
public:
	explicit CClientCredits(const CreditStruct &in_credits);
	explicit CClientCredits(const uchar *key);
	~CClientCredits() = default;
	CClientCredits(const CClientCredits&) = delete;
	CClientCredits& operator=(const CClientCredits&) = delete;

	const uchar* GetKey() const					{ return m_Credits.abyKey; }
	uchar*	GetSecureIdent()					{ return m_abyPublicKey; }
	uint8	GetSecIDKeyLen() const				{ return m_nPublicKeyLen; }
	const CreditStruct*	GetDataStruct()	const	{ return &m_Credits; }
	void	ClearWaitStartTime();
	void	AddDownloaded(uint32 bytes, const CAddress& ForIP);
	void	AddUploaded(uint32 bytes, const CAddress& ForIP);
	uint64	GetUploadedTotal() const;
	uint64	GetDownloadedTotal() const;
	float	GetScoreRatio(const CAddress& ForIP);
	const float	GetMyScoreRatio(const CAddress& dwForIP) const;
	void	SetLastSeen()						{ m_Credits.nLastSeen = static_cast<uint32>(time(NULL)); }
	bool	SetSecureIdent(const uchar *pachIdent, uint8 nIdentLen); // Public key cannot change, use only if there is not public key yet
	void	ResetTransientSecureIdentState();
	uint32	m_dwCryptRndChallengeFor;
	uint32	m_dwCryptRndChallengeFrom;
	EIdentState	GetCurrentIdentState(const CAddress& ForIP) const; // can be != IdentState
	DWORD	GetSecureWaitStartTime(const CAddress& ForIP);
	void	SetSecWaitStartTime(const CAddress& ForIP);
	void	ResetCheckScoreRatio() { m_bCheckScoreRatio = true; }
protected:
	void	Verified(const CAddress& ForIP);
	EIdentState IdentState;
private:
	void	InitalizeIdent();
	CreditStruct m_Credits;
	//uint32	m_dwIdentIP;
	CAddress m_dwIdentIP;
	DWORD	m_dwSecureWaitTime;
	DWORD	m_dwUnSecureWaitTime;
	CAddress	m_dwWaitTimeIP;			// client IP assigned to the waittime
	//uint32	m_dwWaitTimeIP;			// client IP assigned to the waittime
	byte	m_abyPublicKey[80];		// even keys which are not verified will be stored here, and - if verified - copied into the struct
	uint8	m_nPublicKeyLen;
	bool	m_bCheckScoreRatio;
	float	m_fLastScoreRatio;
};

class CClientCreditsList
{
	friend class CUploadQueue;
public:
	CClientCreditsList();
	~CClientCreditsList();

			// return signature size, 0 = Failed | use sigkey param for debug only
	uint8	CreateSignature(CClientCredits* pTarget, uchar* pachOutput, uint8 nMaxSize, const CAddress& ChallengeIP, uint8 byChaIPKind, CryptoPP::RSASSA_PKCS1v15_SHA_Signer* sigkey = NULL);
	bool	VerifyIdent(CClientCredits* pTarget, const uchar* pachSignature, uint8 nInputSize, const CAddress& dwForIP, uint8 byChaIPKind);

	CClientCredits* GetCredit(const uchar *key, bool bSetLastSeen = true);
	void	Process();
	uint8	GetPubKeyLen() const			{ return m_nMyPublicKeyLen; }
	byte*	GetPublicKey()					{ return m_abyMyPublicKey; }
	bool	CryptoAvailable() const;
	void	ResetCheckScoreRatio();
protected:
	void	LoadList();
	void	SaveList();
	void	InitalizeCrypting();
	static bool CreateKeyPair();
#ifdef _DEBUG
	bool	Debug_CheckCrypting();
#endif
private:
	typedef CMap<CCKey, const CCKey&, CClientCredits*, CClientCredits*> CClientCreditsMap;
	CClientCreditsMap m_mapClients;
	CryptoPP::RSASSA_PKCS1v15_SHA_Signer *m_pSignkey;
	DWORD			m_nLastSaved;
	byte			m_abyMyPublicKey[80];
	uint8			m_nMyPublicKeyLen;
};
