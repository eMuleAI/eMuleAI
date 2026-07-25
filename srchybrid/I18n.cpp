#include "stdafx.h"
#include "emule.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Mdump.h"
#include "eMuleAI/DarkMode.h"
#include "resource.h"
#include "zlib/zlib.h"
#include <unordered_map>
#include "translations/translations_data.gen.h"
#include "translations/lang_registry.gen.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	#ifndef LOCALE_NAME_MAX_LENGTH
	#define LOCALE_NAME_MAX_LENGTH 85
	#endif

	#ifndef LOCALE_ALLOW_NEUTRAL_NAMES
	#define LOCALE_ALLOW_NEUTRAL_NAMES 0x08000000
	#endif

	#ifndef LOCALE_CUSTOM_UNSPECIFIED
	#define LOCALE_CUSTOM_UNSPECIFIED 0x1000
	#endif

	static_assert(sizeof(TCHAR) == 2, "Translation package requires UTF-16 TCHAR strings");

	static constexpr uint32_t kMaxTranslationSourceBlockSize = 64u * 1024u * 1024u;
	static constexpr uint32_t kMaxTranslationRuntimeBlockSize = 128u * 1024u * 1024u;

	struct TranslationRuntimeState
	{
		bool packageLoadAttempted = false;
		bool packageLoaded = false;
		const BYTE *packageData = nullptr;
		const Translations::TranslationPackageHeader *header = nullptr;
		const uint32_t *keyOffsets = nullptr;
		const BYTE *keyStrings = nullptr;
		const Translations::TranslationLanguageRecord *languageRecords = nullptr;
		uint16_t activeLanguage = Translations::kDefaultLanguage;
		bool englishLoaded = false;
		bool activeLoaded = false;
		std::vector<BYTE> englishBlock;
		std::vector<BYTE> activeBlock;
	};

	CCriticalSection s_translationLock;
	TranslationRuntimeState s_translationState;

	bool IsPackageRangeValid(uint32_t offset, uint64_t size, uint32_t totalSize)
	{
		return offset <= totalSize && size <= static_cast<uint64_t>(totalSize - offset);
	}

	uint32_t CalculateTranslationCrc32(const BYTE *data, size_t size)
	{
		const uLong initialCrc = crc32(0, Z_NULL, 0);
		return static_cast<uint32_t>(crc32(initialCrc, reinterpret_cast<const Bytef *>(data), static_cast<uInt>(size)));
	}

	LPCTSTR GetPackageKey(uint32_t index)
	{
		const uint32_t keyOffset = s_translationState.keyOffsets[index];
		return reinterpret_cast<LPCTSTR>(s_translationState.keyStrings + keyOffset);
	}

	bool ValidatePackageKeyStrings(const Translations::TranslationPackageHeader *header, const uint32_t *keyOffsets, const BYTE *keyStrings)
	{
		LPCTSTR previousKey = nullptr;
		for (uint32_t index = 0; index < header->keyCount; ++index) {
			const uint32_t keyOffset = keyOffsets[index];
			if ((keyOffset & 1u) != 0 || keyOffset > header->keyStringsSize || header->keyStringsSize - keyOffset < sizeof(TCHAR))
				return false;

			LPCTSTR currentKey = reinterpret_cast<LPCTSTR>(keyStrings + keyOffset);
			const size_t availableChars = (header->keyStringsSize - keyOffset) / sizeof(TCHAR);
			bool terminated = false;
			for (size_t charIndex = 0; charIndex < availableChars; ++charIndex) {
				if (currentKey[charIndex] == _T('\0')) {
					terminated = true;
					break;
				}
			}
			if (!terminated || currentKey[0] == _T('\0'))
				return false;
			if (previousKey != nullptr && _tcscmp(previousKey, currentKey) >= 0)
				return false;
			previousKey = currentKey;
		}
		return true;
	}

	bool LoadTranslationPackageUnlocked()
	{
		if (s_translationState.packageLoadAttempted)
			return s_translationState.packageLoaded;
		s_translationState.packageLoadAttempted = true;

		const HMODULE resourceModule = AfxGetInstanceHandle();
		const HRSRC resourceInfo = ::FindResource(resourceModule, MAKEINTRESOURCE(IDR_TRANSLATIONS_DATA), RT_RCDATA);
		if (resourceInfo == NULL)
			return false;

		const DWORD resourceSize = ::SizeofResource(resourceModule, resourceInfo);
		if (resourceSize < sizeof(Translations::TranslationPackageHeader))
			return false;

		const HGLOBAL resourceData = ::LoadResource(resourceModule, resourceInfo);
		const BYTE *packageData = resourceData != NULL ? static_cast<const BYTE *>(::LockResource(resourceData)) : nullptr;
		if (packageData == nullptr)
			return false;

		const Translations::TranslationPackageHeader *header = reinterpret_cast<const Translations::TranslationPackageHeader *>(packageData);
		if (header->magic != Translations::kPackageMagic || header->version != Translations::kPackageVersion || header->headerSize != sizeof(*header) || header->totalSize != resourceSize)
			return false;
		if (header->keyCount == 0 || header->languageCount != Translations::kLanguageCount || header->defaultLanguage != Translations::kDefaultLanguage
			|| header->languageRegistryCrc32 != Translations::kLanguageRegistryCrc32)
			return false;

		const uint64_t keyOffsetsSize = static_cast<uint64_t>(header->keyCount) * sizeof(uint32_t);
		const uint64_t languageRecordsSize = static_cast<uint64_t>(header->languageCount) * sizeof(Translations::TranslationLanguageRecord);
		if (!IsPackageRangeValid(header->keyOffsetsOffset, keyOffsetsSize, resourceSize)
			|| !IsPackageRangeValid(header->keyStringsOffset, header->keyStringsSize, resourceSize)
			|| !IsPackageRangeValid(header->languageRecordsOffset, languageRecordsSize, resourceSize))
			return false;

		const uint64_t keyOffsetsEnd = static_cast<uint64_t>(header->keyOffsetsOffset) + keyOffsetsSize;
		const uint64_t keyStringsEnd = static_cast<uint64_t>(header->keyStringsOffset) + header->keyStringsSize;
		const uint64_t languageRecordsEnd = static_cast<uint64_t>(header->languageRecordsOffset) + languageRecordsSize;
		if (header->keyOffsetsOffset != header->headerSize || header->keyStringsOffset != keyOffsetsEnd
			|| header->keyStringsSize == 0 || (header->keyStringsOffset & 1u) != 0 || (header->keyStringsSize & 1u) != 0
			|| header->languageRecordsOffset < keyStringsEnd || header->languageRecordsOffset - keyStringsEnd > 3u
			|| (header->languageRecordsOffset & 3u) != 0)
			return false;

		const uint32_t metadataEnd = static_cast<uint32_t>(languageRecordsEnd);
		if (CalculateTranslationCrc32(packageData + header->headerSize, metadataEnd - header->headerSize) != header->indexCrc32)
			return false;

		const uint32_t *keyOffsets = reinterpret_cast<const uint32_t *>(packageData + header->keyOffsetsOffset);
		const BYTE *keyStrings = packageData + header->keyStringsOffset;
		const Translations::TranslationLanguageRecord *languageRecords = reinterpret_cast<const Translations::TranslationLanguageRecord *>(packageData + header->languageRecordsOffset);
		if (!ValidatePackageKeyStrings(header, keyOffsets, keyStrings))
			return false;

		const uint64_t minimumBlockSize = static_cast<uint64_t>(header->keyCount) * sizeof(uint32_t) + 1u;
		uint32_t previousBlockEnd = metadataEnd;
		for (uint32_t index = 0; index < header->languageCount; ++index) {
			const Translations::TranslationLanguageRecord &record = languageRecords[index];
			if (record.compressedSize == 0 || record.uncompressedSize < minimumBlockSize || record.uncompressedSize > kMaxTranslationSourceBlockSize)
				return false;
			if ((record.compressedOffset & 3u) != 0 || record.compressedOffset < previousBlockEnd || !IsPackageRangeValid(record.compressedOffset, record.compressedSize, resourceSize))
				return false;
			previousBlockEnd = record.compressedOffset + record.compressedSize;
		}

		s_translationState.packageData = packageData;
		s_translationState.header = header;
		s_translationState.keyOffsets = keyOffsets;
		s_translationState.keyStrings = keyStrings;
		s_translationState.languageRecords = languageRecords;
		s_translationState.packageLoaded = true;
		return true;
	}

	bool ConvertTranslationBlockToUtf16(const std::vector<BYTE> &sourceBlock, uint32_t keyCount, bool allowFallback, std::vector<BYTE> &convertedBlock)
	{
		const size_t offsetsSize = static_cast<size_t>(keyCount) * sizeof(uint32_t);
		if (sourceBlock.size() < offsetsSize + 1u)
			return false;

		std::vector<BYTE> outputBlock(offsetsSize);
		std::unordered_map<uint32_t, uint32_t> convertedOffsets;
		convertedOffsets.reserve(keyCount);
		for (uint32_t index = 0; index < keyCount; ++index) {
			uint32_t sourceOffset = 0;
			memcpy(&sourceOffset, sourceBlock.data() + static_cast<size_t>(index) * sizeof(uint32_t), sizeof(sourceOffset));
			if (sourceOffset == Translations::kInvalidOffset) {
				if (!allowFallback)
					return false;
				memcpy(outputBlock.data() + static_cast<size_t>(index) * sizeof(uint32_t), &sourceOffset, sizeof(sourceOffset));
				continue;
			}
			if (sourceOffset < offsetsSize || sourceOffset >= sourceBlock.size() || (sourceOffset > offsetsSize && sourceBlock[sourceOffset - 1] != 0))
				return false;

			uint32_t targetOffset = Translations::kInvalidOffset;
			const auto existingOffset = convertedOffsets.find(sourceOffset);
			if (existingOffset != convertedOffsets.end()) {
				targetOffset = existingOffset->second;
			}
			else {
				size_t terminatorOffset = sourceOffset;
				while (terminatorOffset < sourceBlock.size() && sourceBlock[terminatorOffset] != 0)
					++terminatorOffset;
				if (terminatorOffset == sourceBlock.size())
					return false;

				const size_t sourceLength = terminatorOffset - sourceOffset;
				const char *sourceText = reinterpret_cast<const char *>(sourceBlock.data() + sourceOffset);
				const int sourceLengthInt = static_cast<int>(sourceLength);
				const int convertedLength = sourceLengthInt == 0 ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, sourceText, sourceLengthInt, nullptr, 0);
				if (sourceLengthInt > 0 && convertedLength <= 0)
					return false;

				const uint64_t requiredSize = static_cast<uint64_t>(outputBlock.size()) + static_cast<uint64_t>(convertedLength + 1) * sizeof(TCHAR);
				if (requiredSize > kMaxTranslationRuntimeBlockSize)
					return false;

				targetOffset = static_cast<uint32_t>(outputBlock.size());
				outputBlock.resize(static_cast<size_t>(requiredSize));
				TCHAR *targetText = reinterpret_cast<TCHAR *>(outputBlock.data() + targetOffset);
				if (sourceLengthInt > 0 && ::MultiByteToWideChar(CP_UTF8, 0, sourceText, sourceLengthInt, targetText, convertedLength) != convertedLength)
					return false;
				targetText[convertedLength] = _T('\0');
				convertedOffsets.emplace(sourceOffset, targetOffset);
			}

			memcpy(outputBlock.data() + static_cast<size_t>(index) * sizeof(uint32_t), &targetOffset, sizeof(targetOffset));
		}

		convertedBlock.swap(outputBlock);
		return true;
	}

	bool DecompressTranslationBlockUnlocked(uint16_t languageIndex, std::vector<BYTE> &block)
	{
		if (!LoadTranslationPackageUnlocked() || languageIndex >= s_translationState.header->languageCount)
			return false;

		const Translations::TranslationLanguageRecord &record = s_translationState.languageRecords[languageIndex];
		std::vector<BYTE> sourceBlock(record.uncompressedSize);
		uLongf decodedSize = static_cast<uLongf>(sourceBlock.size());
		const int result = uncompress(sourceBlock.data(), &decodedSize, s_translationState.packageData + record.compressedOffset, static_cast<uLong>(record.compressedSize));
		if (result != Z_OK || decodedSize != record.uncompressedSize)
			return false;
		if (CalculateTranslationCrc32(sourceBlock.data(), sourceBlock.size()) != record.uncompressedCrc32)
			return false;

		std::vector<BYTE> decodedBlock;
		if (!ConvertTranslationBlockToUtf16(sourceBlock, s_translationState.header->keyCount, languageIndex != Translations::kDefaultLanguage, decodedBlock))
			return false;
		block.swap(decodedBlock);
		return true;
	}

	bool EnsureLanguageLoadedUnlocked(uint16_t languageIndex)
	{
		if (!LoadTranslationPackageUnlocked() || languageIndex >= Translations::kLanguageCount)
			return false;

		if (!s_translationState.englishLoaded) {
			std::vector<BYTE> englishBlock;
			if (!DecompressTranslationBlockUnlocked(Translations::kDefaultLanguage, englishBlock))
				return false;
			s_translationState.englishBlock.swap(englishBlock);
			s_translationState.englishLoaded = true;
		}

		if (languageIndex == Translations::kDefaultLanguage) {
			std::vector<BYTE>().swap(s_translationState.activeBlock);
			s_translationState.activeLanguage = Translations::kDefaultLanguage;
			s_translationState.activeLoaded = true;
			return true;
		}
		if (s_translationState.activeLoaded && s_translationState.activeLanguage == languageIndex)
			return true;

		std::vector<BYTE> activeBlock;
		if (!DecompressTranslationBlockUnlocked(languageIndex, activeBlock))
			return false;
		s_translationState.activeBlock.swap(activeBlock);
		s_translationState.activeLanguage = languageIndex;
		s_translationState.activeLoaded = true;
		return true;
	}

	uint32_t FindTranslationKeyIndexUnlocked(LPCTSTR key)
	{
		size_t low = 0;
		size_t high = s_translationState.header->keyCount;
		while (low < high) {
			const size_t middle = low + (high - low) / 2;
			const int comparison = _tcscmp(GetPackageKey(static_cast<uint32_t>(middle)), key);
			if (comparison < 0)
				low = middle + 1;
			else
				high = middle;
		}
		if (low >= s_translationState.header->keyCount || _tcscmp(GetPackageKey(static_cast<uint32_t>(low)), key) != 0)
			return Translations::kInvalidOffset;
		return static_cast<uint32_t>(low);
	}

	LPCTSTR GetTranslationFromBlock(const std::vector<BYTE> &block, uint32_t keyIndex)
	{
		if (block.empty())
			return nullptr;
		const uint32_t stringOffset = reinterpret_cast<const uint32_t *>(block.data())[keyIndex];
		return stringOffset == Translations::kInvalidOffset ? nullptr : reinterpret_cast<LPCTSTR>(block.data() + stringOffset);
	}

	bool SetActiveLanguageByIndex(uint16_t index)
	{
		if (index >= Translations::kLanguageCount)
			return false;

		CSingleLock translationLock(&s_translationLock, TRUE);
		if (EnsureLanguageLoadedUnlocked(index))
			return true;
		(void)EnsureLanguageLoadedUnlocked(Translations::kDefaultLanguage);
		return false;
	}

	bool LocaleCodesEqual(LPCTSTR leftCode, LPCTSTR rightCode)
	{
		if (leftCode == nullptr || rightCode == nullptr)
			return false;
		const size_t leftLength = _tcslen(leftCode);
		const size_t rightLength = _tcslen(rightCode);
		if (leftLength != rightLength)
			return false;
		for (size_t i = 0; i < leftLength; ++i) {
			TCHAR left = leftCode[i];
			TCHAR right = rightCode[i];
			if (left == _T('_'))
				left = _T('-');
			if (right == _T('_'))
				right = _T('-');
			if (_totlower(left) != _totlower(right))
				return false;
		}
		return true;
	}

	bool TryLookupLanguageIndexByCode(LPCTSTR code, uint16_t &index)
	{
		if (code == nullptr || code[0] == _T('\0'))
			return false;
		for (uint16_t idx = 0; idx < Translations::kLanguageCount; ++idx) {
			LPCTSTR recordCode = Translations::kLanguages[idx].code;
			if (recordCode == nullptr)
				continue;
			if (LocaleCodesEqual(recordCode, code)) {
				index = idx;
				return true;
			}
		}
		return false;
	}

	bool TryGetPrimaryLanguageTag(LPCTSTR code, CString &primaryTag)
	{
		primaryTag.Empty();
		if (code == nullptr || code[0] == _T('\0'))
			return false;
		primaryTag = code;
		primaryTag.Replace(_T('_'), _T('-'));
		const int separator = primaryTag.Find(_T('-'));
		if (separator > 0)
			primaryTag = primaryTag.Left(separator);
		return !primaryTag.IsEmpty();
	}

	LPCTSTR GetLocaleAlias(LPCTSTR code)
	{
		if (code == nullptr)
			return nullptr;
		if (_tcsicmp(code, _T("iw")) == 0)
			return _T("he");
		if (_tcsicmp(code, _T("jw")) == 0)
			return _T("jv");
		return nullptr;
	}

	bool TryResolveLanguageIndex(LPCTSTR code, uint16_t &index)
	{
		if (TryLookupLanguageIndexByCode(code, index))
			return true;

		CString primaryTag;
		if (TryGetPrimaryLanguageTag(code, primaryTag) && TryLookupLanguageIndexByCode(primaryTag, index))
			return true;

		LPCTSTR alias = GetLocaleAlias(primaryTag);
		if (alias != nullptr && TryLookupLanguageIndexByCode(alias, index))
			return true;

		return false;
	}

	LCID LocaleNameToLcidCompat(LPCTSTR localeName)
	{
		if (localeName == nullptr || localeName[0] == _T('\0'))
			return 0;
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
		return ::LocaleNameToLCID(localeName, LOCALE_ALLOW_NEUTRAL_NAMES);
#else
		typedef LCID(WINAPI *PFNLocaleNameToLCID)(LPCWSTR, DWORD);
		HMODULE hKernel = ::GetModuleHandleW(L"kernel32.dll");
		PFNLocaleNameToLCID pLocaleNameToLCID = hKernel ? reinterpret_cast<PFNLocaleNameToLCID>(::GetProcAddress(hKernel, "LocaleNameToLCID")) : NULL;
		return pLocaleNameToLCID ? pLocaleNameToLCID(localeName, LOCALE_ALLOW_NEUTRAL_NAMES) : 0;
#endif
	}

	bool TryResolveLanguageId(LPCTSTR code, LANGID &languageId)
	{
		if (code == nullptr || code[0] == _T('\0'))
			return false;

		CString normalizedCode(code);
		normalizedCode.Replace(_T('_'), _T('-'));

		LCID lcid = LocaleNameToLcidCompat(normalizedCode);
		if (lcid != 0 && lcid != LOCALE_CUSTOM_UNSPECIFIED) {
			languageId = LANGIDFROMLCID(lcid);
			return true;
		}

		LPCTSTR alias = GetLocaleAlias(normalizedCode);
		if (alias != nullptr) {
			lcid = LocaleNameToLcidCompat(alias);
			if (lcid != 0 && lcid != LOCALE_CUSTOM_UNSPECIFIED) {
				languageId = LANGIDFROMLCID(lcid);
				return true;
			}
		}

		CString primaryTag;
		if (!TryGetPrimaryLanguageTag(normalizedCode, primaryTag))
			return false;

		lcid = LocaleNameToLcidCompat(primaryTag);
		if (lcid != 0 && lcid != LOCALE_CUSTOM_UNSPECIFIED) {
			languageId = LANGIDFROMLCID(lcid);
			return true;
		}

		alias = GetLocaleAlias(primaryTag);
		if (alias == nullptr)
			return false;

		lcid = LocaleNameToLcidCompat(alias);
		if (lcid == 0 || lcid == LOCALE_CUSTOM_UNSPECIFIED)
			return false;

		languageId = LANGIDFROMLCID(lcid);
		return true;
	}

	bool TryGetLocaleNameFromLangId(LANGID languageId, CString &localeName)
	{
		localeName.Empty();
		if (languageId == 0)
			return false;

		const LCID lcid = MAKELCID(languageId, SORT_DEFAULT);
		TCHAR localeNameBuffer[LOCALE_NAME_MAX_LENGTH] = {};
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
		const int length = ::LCIDToLocaleName(lcid, localeNameBuffer, _countof(localeNameBuffer), 0);
#else
		typedef int (WINAPI *PFNLCIDToLocaleName)(LCID, LPWSTR, int, DWORD);
		HMODULE hKernel = ::GetModuleHandleW(L"kernel32.dll");
		PFNLCIDToLocaleName pLcidToLocaleName = hKernel ? reinterpret_cast<PFNLCIDToLocaleName>(::GetProcAddress(hKernel, "LCIDToLocaleName")) : NULL;
		const int length = pLcidToLocaleName ? pLcidToLocaleName(lcid, localeNameBuffer, _countof(localeNameBuffer), 0) : 0;
#endif

		if (length > 0 && localeNameBuffer[0] != _T('\0')) {
			localeName = localeNameBuffer;
			localeName.Replace(_T('_'), _T('-'));
			return true;
		}

		TCHAR languageCode[16] = {};
		if (::GetLocaleInfo(lcid, LOCALE_SISO639LANGNAME, languageCode, _countof(languageCode)) <= 0)
			return false;

		TCHAR regionCode[16] = {};
		if (::GetLocaleInfo(lcid, LOCALE_SISO3166CTRYNAME, regionCode, _countof(regionCode)) > 0 && regionCode[0] != _T('\0'))
			localeName.Format(_T("%s-%s"), languageCode, regionCode);
		else
			localeName = languageCode;

		return true;
	}

	uint16_t MapLangIdToLanguageIndex(LANGID languageId)
	{
		CString localeName;
		uint16_t index = Translations::kDefaultLanguage;
		if (TryGetLocaleNameFromLangId(languageId, localeName) && TryResolveLanguageIndex(localeName, index))
			return index;
		return Translations::kDefaultLanguage;
	}

	LANGID GetLangIdFromCodeCompat(LPCTSTR code)
	{
		if (code == nullptr || code[0] == _T('\0') || _tcsicmp(code, _T("system")) == 0)
			return LANGIDFROMLCID(::GetThreadLocale());

		LANGID languageId = 0;
		if (TryResolveLanguageId(code, languageId))
			return languageId;

		return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}

	void ApplyWindowsUiLanguage(LANGID languageId)
	{
		(void)::SetThreadUILanguage(languageId);

		typedef void (WINAPI *PFNInitMUILanguage)(LANGID);
		HMODULE hComCtl = ::GetModuleHandleW(L"comctl32.dll");
		PFNInitMUILanguage pInitMUILanguage = hComCtl != NULL ? reinterpret_cast<PFNInitMUILanguage>(::GetProcAddress(hComCtl, "InitMUILanguage")) : NULL;
		if (pInitMUILanguage != NULL)
			pInitMUILanguage(languageId);
	}
}

// Public API: resolve a localized string by string key.
CString GetResString(LPCTSTR key)
{
	if (key == nullptr || key[0] == _T('\0'))
		return CString();

	CString result;
	bool found = false;
	{
		CSingleLock translationLock(&s_translationLock, TRUE);
		if (EnsureLanguageLoadedUnlocked(s_translationState.activeLanguage)) {
			const uint32_t keyIndex = FindTranslationKeyIndexUnlocked(key);
			if (keyIndex != Translations::kInvalidOffset) {
				LPCTSTR translatedText = s_translationState.activeLanguage == Translations::kDefaultLanguage ? GetTranslationFromBlock(s_translationState.englishBlock, keyIndex) : GetTranslationFromBlock(s_translationState.activeBlock, keyIndex);
				if (translatedText == nullptr)
					translatedText = GetTranslationFromBlock(s_translationState.englishBlock, keyIndex);
				if (translatedText != nullptr) {
					result = translatedText;
					found = true;
				}
			}
		}
	}

	if (found)
		return result;
	TRACE(_T("Missing translation for %s\n"), key);
	return CString(key);
}

void ClearTranslationKeyIndex()
{
	CSingleLock translationLock(&s_translationLock, TRUE);
	s_translationState = TranslationRuntimeState();
}

CString AddAcceleratorKey(const CString& strText, TCHAR cAccel)
{
	if (strText.IsEmpty())
		return strText;

	if (strText.Find(_T('&')) != -1)
		return strText;

	if (cAccel != 0) {
		TCHAR cUpperAccel = (TCHAR)::CharUpper((LPTSTR)(ULONG_PTR)cAccel);
		int nLen = strText.GetLength();

		for (int i = 0; i < nLen; ++i) {
			TCHAR c = strText[i];
			if ((TCHAR)::CharUpper((LPTSTR)(ULONG_PTR)c) == cUpperAccel) {
				return strText.Left(i) + _T('&') + strText.Mid(i);
			}
		}

		for (int i = 0; i < nLen; ++i) {
			TCHAR c = strText[i];
			if ((c >= _T('A') && c <= _T('Z')) || (c >= _T('a') && c <= _T('z'))) {
				return strText.Left(i) + _T('&') + strText.Mid(i);
			}
		}

		CString strResult;
		strResult.Format(_T("%s (&%c)"), (LPCTSTR)strText, cUpperAccel);
		return strResult;
	}

	int nLen = strText.GetLength();
	for (int i = 0; i < nLen; ++i) {
		TCHAR c = strText[i];
		if (_istalnum(c)) {
			return strText.Left(i) + _T('&') + strText.Mid(i);
		}
	}

	return strText;
}

CString AddColon(const CString& strText)
{
	if (strText.IsEmpty())
		return strText;
	if (strText.Right(1) == _T(":"))
		return strText;
	return strText + _T(":");
}

CString AddExclamation(const CString& strText)
{
	if (strText.IsEmpty())
		return strText;
	if (strText.Right(1) == _T("!"))
		return strText;
	return strText + _T("!");
}

CString AddParens(const CString& strText)
{
	if (strText.IsEmpty())
		return strText;
	if (strText.Left(1) == _T("(") && strText.Right(1) == _T(")"))
		return strText;
	return _T("(") + strText + _T(")");
}

CString GetResStringWithColon(LPCTSTR key)
{
	CString strText = GetResString(key);
	return AddColon(strText);
}

CString GetResStringWithExclamation(LPCTSTR key)
{
	CString strText = GetResString(key);
	return AddExclamation(strText);
}

CString GetResStringWithParens(LPCTSTR key)
{
	CString strText = GetResString(key);
	return AddParens(strText);
}

CString AddEllipsis(const CString& strText)
{
	if (strText.IsEmpty())
		return strText;

	if (strText.Right(3) == _T("...") || strText.Right(1) == _T("…"))
		return strText;

	return strText + _T("...");
}

CString GetResStringWithEllipsis(LPCTSTR key)
{
	CString strText = GetResString(key);
	return AddEllipsis(strText);
}

CString GetResStringWithAccelAndEllipsis(LPCTSTR key, TCHAR cAccel)
{
	CString strText = GetResStringWithAccel(key, cAccel);
	return AddEllipsis(strText);
}

CString GetResStringWithAccel(LPCTSTR key, TCHAR cAccel)
{
	CString strText = GetResString(key);
	return AddAcceleratorKey(strText, cAccel);
}

CString GetResNoAmp(LPCTSTR key)
{
	CString str(GetResString(key));
	str.Remove(_T('&'));
	return str;
}

int LocMessageBox(LPCTSTR key, UINT nType, UINT nIDHelp)
{
	return CDarkMode::MessageBox(GetResString(key), nType, nIDHelp);
}



static void ApplySelectedLanguage(LANGID lid)
{
	CString sel = thePrefs.GetUiLanguage();
	if (sel.IsEmpty())
		sel = _T("system");

	uint16_t index = Translations::kDefaultLanguage;
	if (sel.CompareNoCase(_T("system")) == 0) {
		index = MapLangIdToLanguageIndex(lid);
	}
	else {
		if (!TryResolveLanguageIndex(sel, index))
			index = Translations::kDefaultLanguage;
	}
	SetActiveLanguageByIndex(index);
}

LANGID CPreferences::GetLanguageID()
{
	return m_wLanguageID;
}

void CPreferences::SetLanguage()
{
	CString chosen = m_strUiLanguage;
	if (chosen.IsEmpty())
		chosen = _T("system");

	if (chosen.CompareNoCase(_T("system")) == 0)
		m_wLanguageID = LANGIDFROMLCID(::GetThreadLocale());
	else
		m_wLanguageID = GetLangIdFromCodeCompat(chosen);

	ApplySelectedLanguage(m_wLanguageID);

	// Force English if the active language cannot resolve string lookups.
	if (GetResString(_T("MB_LANGUAGEINFO")).IsEmpty()) {
		SetActiveLanguageByIndex(Translations::kDefaultLanguage);
		m_wLanguageID = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}

	// Keep Windows-created controls aligned with the selected application language.
	ApplyWindowsUiLanguage(m_wLanguageID);
	theCrashDumper.UpdateLocalizedStrings();
}

static HHOOK s_hRTLWindowsLayoutOldCbtFilterHook = NULL;

LRESULT CALLBACK RTLWindowsLayoutCbtFilterHook(int code, WPARAM wParam, LPARAM lParam) noexcept
{
	if (code == HCBT_CREATEWND) {


		if ((::GetWindowLongPtr((HWND)wParam, GWL_STYLE) & WS_CHILD) == 0)
			::SetWindowLongPtr((HWND)wParam, GWL_EXSTYLE, ::GetWindowLongPtr((HWND)wParam, GWL_EXSTYLE) | WS_EX_LAYOUTRTL);
	}
	return CallNextHookEx(s_hRTLWindowsLayoutOldCbtFilterHook, code, wParam, lParam);
}

void CemuleApp::EnableRTLWindowsLayout()
{
	::SetProcessDefaultLayout(LAYOUT_RTL);

	s_hRTLWindowsLayoutOldCbtFilterHook = ::SetWindowsHookEx(WH_CBT, RTLWindowsLayoutCbtFilterHook, NULL, GetCurrentThreadId());
}

void CemuleApp::DisableRTLWindowsLayout()
{
	if (s_hRTLWindowsLayoutOldCbtFilterHook) {
		VERIFY(UnhookWindowsHookEx(s_hRTLWindowsLayoutOldCbtFilterHook));
		s_hRTLWindowsLayoutOldCbtFilterHook = NULL;

		::SetProcessDefaultLayout(0);
	}
}
