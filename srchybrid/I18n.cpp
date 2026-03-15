#include "stdafx.h"
#include "emule.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "eMuleAI/DarkMode.h"
#include "translations/translations_data.gen.h"

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

	uint16_t s_activeLanguageIndex = Translations::kDefaultLanguage;

	const Translations::TranslationValue* Follow(uint32_t index)
	{
		return index == Translations::kInvalidIndex ? nullptr : &Translations::kValues[index];
	}


	// Indexed string-key lookup: build a sorted index once and binary search by key.
	struct KeyIndexEntry { LPCTSTR key; uint32_t head; };
	static const std::vector<KeyIndexEntry>& BuildKeyIndex()
	{
		static std::vector<KeyIndexEntry> s_index;
		static bool s_built = false;
		if (!s_built) {
			s_index.reserve(Translations::kBucketCount);
			for (uint32_t i = 0; i < Translations::kBucketCount; ++i) {
				const Translations::TranslationBucket &b = Translations::kBuckets[i];
				if (b.key != nullptr && b.value != Translations::kInvalidIndex) {
					KeyIndexEntry e{ b.key, b.value };
					s_index.push_back(e);
				}
			}
			std::sort(s_index.begin(), s_index.end(), [](const KeyIndexEntry &a, const KeyIndexEntry &b) {
				return _tcscmp(a.key, b.key) < 0;
			});
			s_built = true;
		}
		return s_index;
	}

	const Translations::TranslationValue* FindTranslationEntry(LPCTSTR key)
	{
		if (key == nullptr || key[0] == _T('\0'))
			return nullptr;
		const auto &idx = BuildKeyIndex();
		int lo = 0;
		int hi = static_cast<int>(idx.size()) - 1;
		while (lo <= hi) {
			int mid = (lo + hi) >> 1;
			int cmp = _tcscmp(idx[mid].key, key);
			if (cmp == 0) return Follow(idx[mid].head);
			if (cmp < 0) lo = mid + 1; else hi = mid - 1;
		}
		return nullptr;
	}

	const Translations::TranslationValue* ResolveTranslation(const Translations::TranslationValue *head, uint16_t languageIndex)
	{
		if (head == nullptr)
			return nullptr;
		uint16_t lang = languageIndex;
		for (;;) {
			const Translations::TranslationValue *node = head;
			while (node != nullptr) {
				if (node->language == lang && node->text != nullptr)
					return node;
				node = Follow(node->next);
			}
			const uint16_t fallback = (lang < Translations::kLanguageCount)
				? Translations::kLanguages[lang].fallback
				: Translations::kDefaultLanguage;
			if (fallback == lang)
				break;
			lang = fallback;
		}
		return nullptr;
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

	bool TryResolveLanguageIndex(LPCTSTR code, uint16_t &index)
	{
		if (TryLookupLanguageIndexByCode(code, index))
			return true;

		CString primaryTag;
		return TryGetPrimaryLanguageTag(code, primaryTag) && TryLookupLanguageIndexByCode(primaryTag, index);
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

	bool SetActiveLanguageByIndex(uint16_t index)
	{
		if (index >= Translations::kLanguageCount)
			return false;
		s_activeLanguageIndex = index;
		return true;
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
}

// Public API: resolve a localized string by string key.
CString GetResString(LPCTSTR key)
{
	if (key == nullptr)
		return CString();

	if (key[0] == _T('\0'))
		return CString();

	const Translations::TranslationValue* head = FindTranslationEntry(key);
	if (head != nullptr) {
		const Translations::TranslationValue* resolved = ResolveTranslation(head, s_activeLanguageIndex);
		if (resolved == nullptr)
			resolved = ResolveTranslation(head, Translations::kDefaultLanguage);
		if (resolved != nullptr && resolved->text != nullptr)
			return CString(resolved->text);
	}
	TRACE(_T("Missing translation for %s\n"), key);
	return CString(key);
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
