//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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

#include "StdAfx.h"
#include "collection.h"
#include "KnownFile.h"
#include "CollectionFile.h"
#include "SafeFile.h"
#include "Packets.h"
#include "Preferences.h"
#include "SharedFilelist.h"
#include "emule.h"
#include "Log.h"
#include "md5sum.h"
#include "OtherFunctions.h"
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define COLLECTION_FILE_VERSION1_INITIAL		0x01
#define COLLECTION_FILE_VERSION2_LARGEFILES		0x02

CCollection::CCollection()
	: m_bTextFormat()
	, m_nKeySize()
	, m_pabyCollectionAuthorKey()
{
	m_CollectionFilesMap.InitHashTable(1031);
	m_sCollectionName.Format(_T("New Collection-%u"), ::GetTickCount());
}

CCollection::CCollection(const CCollection *pCollection)
	: m_sCollectionName(pCollection->m_sCollectionName)
	, m_bTextFormat(pCollection->m_bTextFormat)
{
	if (pCollection->m_pabyCollectionAuthorKey != NULL) {
		m_nKeySize = pCollection->m_nKeySize;
		m_pabyCollectionAuthorKey = new BYTE[m_nKeySize];
		memcpy(m_pabyCollectionAuthorKey, pCollection->m_pabyCollectionAuthorKey, m_nKeySize);
		m_sCollectionAuthorName = pCollection->m_sCollectionAuthorName;
	} else {
		m_nKeySize = 0;
		m_pabyCollectionAuthorKey = NULL;
	}

	m_CollectionFilesMap.InitHashTable(1031);
	for (const CCollectionFilesMap::CPair *pair = pCollection->m_CollectionFilesMap.PGetFirstAssoc(); pair != NULL; pair = pCollection->m_CollectionFilesMap.PGetNextAssoc(pair))
		AddFileToCollection(pair->value, true);
}

CCollection::~CCollection()
{
	delete[] m_pabyCollectionAuthorKey;
	CSKey key;
	for (POSITION pos = m_CollectionFilesMap.GetStartPosition(); pos != NULL;) {
		CCollectionFile *pCollectionFile;
		m_CollectionFilesMap.GetNextAssoc(pos, key, pCollectionFile);
		delete pCollectionFile;
	}
}

CCollectionFile* CCollection::AddFileToCollection(CAbstractFile *pAbstractFile, bool bCreateClone)
{
	CSKey key(pAbstractFile->GetFileHash());
	CCollectionFile *pCollectionFile;
	if (m_CollectionFilesMap.Lookup(key, pCollectionFile)) {
		ASSERT(0);
		return pCollectionFile;
	}

	if (bCreateClone)
		pCollectionFile = new CCollectionFile(pAbstractFile);
	else if (pAbstractFile->IsKindOf(RUNTIME_CLASS(CCollectionFile)))
		pCollectionFile = static_cast<CCollectionFile*>(pAbstractFile);
	else
		pCollectionFile = NULL;

	if (pCollectionFile)
		m_CollectionFilesMap[key] = pCollectionFile;

	return pCollectionFile;
}

void CCollection::RemoveFileFromCollection(CAbstractFile *pAbstractFile)
{
	CSKey key(pAbstractFile->GetFileHash());
	CCollectionFile *pCollectionFile;
	if (m_CollectionFilesMap.Lookup(key, pCollectionFile)) {
		m_CollectionFilesMap.RemoveKey(key);
		delete pCollectionFile;
	} else
		ASSERT(0);
}

void CCollection::SetCollectionAuthorKey(const byte *abyCollectionAuthorKey, uint32 nSize)
{
	delete[] m_pabyCollectionAuthorKey;
	m_pabyCollectionAuthorKey = NULL;
	m_nKeySize = 0;
	if (abyCollectionAuthorKey != NULL) {
		m_pabyCollectionAuthorKey = new BYTE[nSize];
		memcpy(m_pabyCollectionAuthorKey, abyCollectionAuthorKey, nSize);
		m_nKeySize = nSize;
	}
}

bool CCollection::InitCollectionFromFile(const CString &sFilePath, const CString &sFileName)
{
	bool bCollectionLoaded = false;

	// 1) Try binary .emulecollection load with long-path aware I/O.
	do {
		if (!IsWin32LongPathsEnabled() && sFilePath.GetLength() >= MAX_PATH)
			break;

		const CString longPath = PreparePathForWin32LongPath(sFilePath);
		HANDLE hFile = ::CreateFile(longPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			break;

		LARGE_INTEGER liSize = {0};
		if (!::GetFileSizeEx(hFile, &liSize) || liSize.QuadPart <= 0) {
			::CloseHandle(hFile);
			break;
		}

		std::vector<BYTE> buf((size_t)liSize.QuadPart);
		DWORD rd = 0;
		BOOL ok = ::ReadFile(hFile, buf.data(), (DWORD)buf.size(), &rd, NULL);
		::CloseHandle(hFile);
		if (!ok || rd < sizeof(uint32))
			break;

		CSafeMemFile data(buf.data(), rd);
		try {
			uint32 nVersion = data.ReadUInt32();
			if (nVersion == COLLECTION_FILE_VERSION1_INITIAL || nVersion == COLLECTION_FILE_VERSION2_LARGEFILES) {
				for (uint32 headerTagCount = data.ReadUInt32(); headerTagCount > 0; --headerTagCount) {
					CTag tag(data, true);
					switch (tag.GetNameID()) {
					case FT_FILENAME:
						if (tag.IsStr())
							m_sCollectionName = tag.GetStr();
						break;
					case FT_COLLECTIONAUTHOR:
						if (tag.IsStr())
							m_sCollectionAuthorName = tag.GetStr();
						break;
					case FT_COLLECTIONAUTHORKEY:
						if (tag.IsBlob())
							SetCollectionAuthorKey(tag.GetBlob(), tag.GetBlobSize());
					}
				}
				for (uint32 fileCount = data.ReadUInt32(); fileCount > 0; --fileCount)
					try {
						CCollectionFile *pCollectionFile = new CCollectionFile(data);
						AddFileToCollection(pCollectionFile, false);
					} catch (CException *e) {
						e->Delete();
						ASSERT(0);
					} catch (...) {
						ASSERT(0);
					}

				bCollectionLoaded = true;
			}

			// Verify optional public key signature if present.
			if (m_pabyCollectionAuthorKey != NULL) {
				bool bResult = false;
				if (data.GetLength() > data.GetPosition()) {
					using namespace CryptoPP;

					const uint32 sigStartPos = (uint32)data.GetPosition();
					const uint32 payloadSize = sigStartPos;
					// Rewind and copy payload for verification.
					data.SeekToBegin();
					std::vector<BYTE> message(payloadSize);
					if (payloadSize != 0)
						VERIFY(data.Read(message.data(), payloadSize) == payloadSize);

					StringSource ss_Pubkey(m_pabyCollectionAuthorKey, m_nKeySize, true, 0);
					RSASSA_PKCS1v15_SHA_Verifier pubkey(ss_Pubkey);

					const UINT sigSize = (UINT)(data.GetLength() - sigStartPos);
					std::vector<BYTE> signature(sigSize);
					if (sigSize != 0)
						VERIFY(data.Read(signature.data(), sigSize) == sigSize);

					bResult = pubkey.VerifyMessage(message.data(), payloadSize, signature.data(), sigSize);
				}

				if (!bResult) {
					DebugLogWarning(_T("Collection %s: Verification of public key failed!"), (LPCTSTR)EscPercent(m_sCollectionName));
					delete[] m_pabyCollectionAuthorKey;
					m_pabyCollectionAuthorKey = NULL;
					m_nKeySize = 0;
					m_sCollectionAuthorName.Empty();
				} else {
					DebugLog(_T("Collection %s: Public key verified"), (LPCTSTR)EscPercent(m_sCollectionName));
				}
			} else {
				m_sCollectionAuthorName.Empty();
			}
		} catch (CFileException *ex) {
			ex->Delete();
			return false;
		} catch (CException *e) {
			e->Delete();
			ASSERT(0);
			return false;
		} catch (...) {
			ASSERT(0);
			return false;
		}
	} while (false);

	// 2) If not a binary collection, try text format (plain ed2k links) with long-path aware I/O.
	if (!bCollectionLoaded) {
		if (IsWin32LongPathsEnabled() || sFilePath.GetLength() < MAX_PATH) {
			const CString tLong = PreparePathForWin32LongPath(sFilePath);
			HANDLE hTxt = ::CreateFile(tLong, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if (hTxt != INVALID_HANDLE_VALUE) {
				LARGE_INTEGER lsz = {0};
				if (::GetFileSizeEx(hTxt, &lsz) && lsz.QuadPart > 0) {
					std::vector<BYTE> tbuf((size_t)lsz.QuadPart);
					DWORD rd = 0;
					if (::ReadFile(hTxt, tbuf.data(), (DWORD)tbuf.size(), &rd, NULL) && rd > 0) {
						// Detect encoding: UTF-16LE BOM, UTF-8 BOM, else ANSI/ACP.
						CStringW wtext;
						if (rd >= 2 && tbuf[0] == 0xFF && tbuf[1] == 0xFE) {
							wtext = CStringW((LPCWSTR)(tbuf.data() + 2), (int)((rd - 2) / 2));
						} else if (rd >= 3 && tbuf[0] == 0xEF && tbuf[1] == 0xBB && tbuf[2] == 0xBF) {
							int cch = ::MultiByteToWideChar(CP_UTF8, 0, (LPCCH)(tbuf.data() + 3), (int)rd - 3, NULL, 0);
							if (cch > 0) {
								wtext.GetBufferSetLength(cch);
								::MultiByteToWideChar(CP_UTF8, 0, (LPCCH)(tbuf.data() + 3), (int)rd - 3, wtext.GetBuffer(), cch);
								wtext.ReleaseBuffer(cch);
							}
						} else {
							int cch = ::MultiByteToWideChar(CP_ACP, 0, (LPCCH)tbuf.data(), (int)rd, NULL, 0);
							if (cch > 0) {
								wtext.GetBufferSetLength(cch);
								::MultiByteToWideChar(CP_ACP, 0, (LPCCH)tbuf.data(), (int)rd, wtext.GetBuffer(), cch);
								wtext.ReleaseBuffer(cch);
							}
						}

						int start = 0;
						while (start < wtext.GetLength()) {
							int nl = wtext.Find(L'\n', start);
							CString sLine = (nl == -1) ? CString(wtext.Mid(start)) : CString(wtext.Mid(start, nl - start));
							sLine.TrimRight(_T("\r\n"));
							if (sLine.Find(_T('#')) != 0) {
								try {
									CCollectionFile *pCollectionFile = new CCollectionFile();
									if (pCollectionFile->InitFromLink(sLine))
										AddFileToCollection(pCollectionFile, false);
									else
										delete pCollectionFile;
								} catch (CException *e) {
									e->Delete();
									ASSERT(0);
									return false;
								} catch (...) {
									ASSERT(0);
									return false;
								}
							}
							if (nl == -1) break;
							start = nl + 1;
						}

						// No collection name tag; use file name without extension.
						int iLen = sFileName.GetLength();
						if (HasCollectionExtention(sFileName))
							iLen -= _countof(COLLECTION_FILEEXTENSION) - 1;
						m_sCollectionName = sFileName.Left(iLen);
						m_bTextFormat = true;
						::CloseHandle(hTxt);
						return true;
					}
				}
				::CloseHandle(hTxt);
			}
		}
	}

	return bCollectionLoaded;
}

void CCollection::WriteToFileAddShared(CryptoPP::RSASSA_PKCS1v15_SHA_Signer *pSignKey)
{
	using namespace CryptoPP;

	CString sFilePath(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	sFilePath.AppendFormat(_T("%s%s"), (LPCTSTR)m_sCollectionName, COLLECTION_FILEEXTENSION);

	if (m_bTextFormat) {
		if (IsWin32LongPathsEnabled() || sFilePath.GetLength() < MAX_PATH) {
			const CString outPath = PreparePathForWin32LongPath(sFilePath);
			HANDLE hFile = ::CreateFile(outPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile != INVALID_HANDLE_VALUE) {
				// Write BOM and content.
				const WCHAR wBOM = 0xFEFF;
				DWORD dw = 0;
				::WriteFile(hFile, &wBOM, sizeof(wBOM), &dw, NULL);
				CString content;
				for (const CCollectionFilesMap::CPair *pair = m_CollectionFilesMap.PGetFirstAssoc(); pair != NULL; pair = m_CollectionFilesMap.PGetNextAssoc(pair))
					if (pair->value)
						content += pair->value->GetED2kLink() + _T('\n');
				if (!content.IsEmpty()) {
					const DWORD cb = (DWORD)(content.GetLength() * sizeof(TCHAR));
					::WriteFile(hFile, (LPCTSTR)content, cb, &dw, NULL);
				}
				::CloseHandle(hFile);
			}
		}
	} else {
		// Build binary collection into memory.
		CSafeMemFile outMem;

		// Version based on presence of large files.
		uint32 dwVersion = COLLECTION_FILE_VERSION1_INITIAL;
		for (const CCollectionFilesMap::CPair *pair = m_CollectionFilesMap.PGetFirstAssoc(); pair != NULL; pair = m_CollectionFilesMap.PGetNextAssoc(pair))
			if (pair->value->IsLargeFile()) { dwVersion = COLLECTION_FILE_VERSION2_LARGEFILES; break; }

		outMem.WriteUInt32(dwVersion);

		// Header tags.
		outMem.WriteUInt32(m_pabyCollectionAuthorKey ? 3 : 1);
		CTag collectionName(FT_FILENAME, m_sCollectionName); collectionName.WriteTagToFile(outMem, UTF8strRaw);
		if (m_pabyCollectionAuthorKey != NULL) {
			CTag collectionAuthor(FT_COLLECTIONAUTHOR, m_sCollectionAuthorName); collectionAuthor.WriteTagToFile(outMem, UTF8strRaw);
			CTag collectionAuthorKey(FT_COLLECTIONAUTHORKEY, m_nKeySize, m_pabyCollectionAuthorKey); collectionAuthorKey.WriteTagToFile(outMem, UTF8strRaw);
		}

		// Files.
		outMem.WriteUInt32((uint32)m_CollectionFilesMap.GetCount());
		for (const CCollectionFilesMap::CPair *pair = m_CollectionFilesMap.PGetFirstAssoc(); pair != NULL; pair = m_CollectionFilesMap.PGetNextAssoc(pair))
			pair->value->WriteCollectionInfo(outMem);

		// Optional signature.
		if (pSignKey != NULL) {
			uint32 nPos = (uint32)outMem.GetLength();
			const BYTE *pMsg = outMem.GetBuffer();
			SecByteBlock sbbSignature(pSignKey->SignatureLength());
			AutoSeededRandomPool rng;
			pSignKey->SignMessage(rng, pMsg, nPos, sbbSignature.begin());
			BYTE abyBuffer2[500];
			ArraySink asink(abyBuffer2, sizeof abyBuffer2);
			asink.Put(sbbSignature.begin(), sbbSignature.size());
			outMem.Write(abyBuffer2, (UINT)asink.TotalPutLength());
		}

		// Flush memory buffer to disk with long-path aware CreateFile.
		if (IsWin32LongPathsEnabled() || sFilePath.GetLength() < MAX_PATH) {
			const CString wLong = PreparePathForWin32LongPath(sFilePath);
			HANDLE hOut = ::CreateFile(wLong, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hOut != INVALID_HANDLE_VALUE) {
				DWORD wr = 0;
				const BYTE *pBuf = outMem.GetBuffer();
				const DWORD cb = (DWORD)outMem.GetLength();
				if (cb > 0)
					::WriteFile(hOut, pBuf, cb, &wr, NULL);
				::CloseHandle(hOut);
			}
		}
	}

	theApp.sharedfiles->AddFileFromNewlyCreatedCollection(sFilePath);
}

bool CCollection::HasCollectionExtention(const CString &sFileName)
{
	return ExtensionIs(sFileName, COLLECTION_FILEEXTENSION);
}

CString CCollection::GetCollectionAuthorKeyString()
{
	return m_pabyCollectionAuthorKey ? EncodeBase16(m_pabyCollectionAuthorKey, m_nKeySize) : CString();
}

CString CCollection::GetAuthorKeyHashString() const
{
	if (m_pabyCollectionAuthorKey == NULL)
		return CString();
	MD5Sum md5(m_pabyCollectionAuthorKey, m_nKeySize);
	return md5.GetHashString().MakeUpper();
}