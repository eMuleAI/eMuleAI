//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include <algorithm>
#include <Richedit.h>
#include <CommCtrl.h>
#include "emule.h"
#include "PPgDownloadValidator.h"
#include "Preferences.h"
#include "PreferencesDlg.h"
#include "OtherFunctions.h"
#include "UserMsgs.h"
#include "emuledlg.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

IMPLEMENT_DYNAMIC(CPPgDownloadValidator, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgDownloadValidator, CPropertyPage)
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_WM_HELPINFO()
	ON_MESSAGE(UM_TREEOPTSCTRL_NOTIFY, OnTreeOptsCtrlNotify)
	ON_MESSAGE(WM_TREEITEM_HELP, DrawTreeItemHelp)
	ON_NOTIFY(TCN_SELCHANGE, IDC_DOWNLOAD_VALIDATOR_VIEW_TABS, OnTcnSelchangeViewTabs)
	ON_BN_CLICKED(IDC_DOWNLOAD_VALIDATOR_REGEX_VALIDATE, OnValidateRules)
	ON_BN_CLICKED(IDC_DOWNLOAD_VALIDATOR_REGEX_RELOAD, OnReloadRules)
	ON_EN_CHANGE(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX, OnRulesChanged)
END_MESSAGE_MAP()

CPPgDownloadValidator::CPPgDownloadValidator()
	: CPropertyPage(CPPgDownloadValidator::IDD)
	, m_ctrlTreeOptions(theApp.m_iDfltImageListColorFlags)
	, m_bInitializedTreeOpts(false)
	, m_bUpdatingRulesText(false)
	, m_bRulesDirty(false)
	, m_bSettingsDirty(false)
	, m_eViewMode(ViewSettings)
	, m_htiDownloadValidator(NULL)
	, m_htiDownloadValidatorPassive(NULL)
	, m_htiDownloadValidatorAlwaysAsk(NULL)
	, m_htiDownloadValidatorReject(NULL)
	, m_htiDownloadValidatorAccept(NULL)
	, m_htiDownloadValidatorAcceptPercentage(NULL)
	, m_htiDownloadValidatorRejectCanceled(NULL)
	, m_htiDownloadValidatorRejectSameHash(NULL)
	, m_htiDownloadValidatorRejectBlacklisted(NULL)
	, m_htiDownloadValidatorCaseInsensitive(NULL)
	, m_htiDownloadValidatorIgnoreExtension(NULL)
	, m_htiDownloadValidatorIgnoreTags(NULL)
	, m_htiDownloadValidatorDontIgnoreNumericTags(NULL)
	, m_htiDownloadValidatorIgnoreNonAlphaNumeric(NULL)
	, m_htiDownloadValidatorCleanMojibake(NULL)
	, m_htiDownloadValidatorMinimumComparisonLength(NULL)
	, m_htiDownloadValidatorSkipIncompleteFileConfirmation(NULL)
	, m_htiDownloadValidatorMarkAsBlacklisted(NULL)
	, m_htiDownloadValidatorAutoMarkAsBlacklisted(NULL)
	, m_htiDownloadValidatorDateTimeMatching(NULL)
	, m_htiDownloadValidatorDateTimeUseYearRange(NULL)
	, m_htiDownloadValidatorDateTimeYearStart(NULL)
	, m_htiDownloadValidatorDateTimeYearEnd(NULL)
	, m_htiDownloadValidatorDateTimeCheckSeconds(NULL)
	, m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues(NULL)
	, m_htiDownloadValidatorRegexMatching(NULL)
	, m_htiDownloadValidatorFuzzyMatching(NULL)
	, m_htiDownloadValidatorFuzzySimilarityThreshold(NULL)
	, m_htiDownloadValidatorFuzzyDisplayThreshold(NULL)
	, m_htiDownloadValidatorFuzzyMinimumSharedTokens(NULL)
	, m_htiDownloadValidatorFuzzyMinimumTokenCoverage(NULL)
	, m_htiDownloadValidatorFuzzyMinimumLengthSimilarity(NULL)
	, m_htiDownloadValidatorFuzzyMinimumEditSimilarity(NULL)
	, m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters(NULL)
	, m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits(NULL)
	, m_htiDownloadValidatorMediaLengthMatching(NULL)
	, m_htiDownloadValidatorMediaLengthTolerance(NULL)
	, m_iDownloadValidator(0)
	, m_iDownloadValidatorAcceptPercentage(0)
	, m_bDownloadValidatorRejectCanceled(false)
	, m_bDownloadValidatorRejectSameHash(false)
	, m_bDownloadValidatorRejectBlacklisted(false)
	, m_bDownloadValidatorCaseInsensitive(false)
	, m_bDownloadValidatorIgnoreExtension(false)
	, m_bDownloadValidatorIgnoreTags(false)
	, m_bDownloadValidatorDontIgnoreNumericTags(false)
	, m_bDownloadValidatorIgnoreNonAlphaNumeric(false)
	, m_bDownloadValidatorCleanMojibake(true)
	, m_iDownloadValidatorMinimumComparisonLength(0)
	, m_bDownloadValidatorSkipIncompleteFileConfirmation(false)
	, m_bDownloadValidatorMarkAsBlacklisted(false)
	, m_bDownloadValidatorAutoMarkAsBlacklisted(false)
	, m_bDownloadValidatorDateTimeMatching(false)
	, m_bDownloadValidatorDateTimeUseYearRange(false)
	, m_iDownloadValidatorDateTimeYearStart(0)
	, m_iDownloadValidatorDateTimeYearEnd(0)
	, m_bDownloadValidatorDateTimeCheckSeconds(false)
	, m_bDownloadValidatorDateTimeIncludeFollowingNumericValues(false)
	, m_bDownloadValidatorRegexMatching(false)
	, m_bDownloadValidatorFuzzyMatching(false)
	, m_strDownloadValidatorFuzzySimilarityThreshold(_T("100"))
	, m_iDownloadValidatorFuzzyDisplayThreshold(30)
	, m_iDownloadValidatorFuzzyMinimumSharedTokens(2)
	, m_iDownloadValidatorFuzzyMinimumTokenCoverage(75)
	, m_iDownloadValidatorFuzzyMinimumLengthSimilarity(75)
	, m_iDownloadValidatorFuzzyMinimumEditSimilarity(80)
	, m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters(3)
	, m_iDownloadValidatorFuzzyStructuralMinimumIDDigits(3)
	, m_bDownloadValidatorMediaLengthMatching(false)
	, m_iDownloadValidatorMediaLengthTolerance(5)
{
}

void CPPgDownloadValidator::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_ctrlTreeOptions);
	DDX_Control(pDX, IDC_DOWNLOAD_VALIDATOR_VIEW_TABS, m_ctrlViewTabs);
	if (!m_bInitializedTreeOpts) {
		m_htiDownloadValidator = TVI_ROOT;
		m_htiDownloadValidatorPassive = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("UDPDISABLED")), TVI_ROOT, m_iDownloadValidator == 0);
		m_htiDownloadValidatorAlwaysAsk = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_ALWAYS_ASK")), TVI_ROOT, m_iDownloadValidator == 1);
		m_htiDownloadValidatorReject = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT")), TVI_ROOT, m_iDownloadValidator == 2);
		m_htiDownloadValidatorAccept = m_ctrlTreeOptions.InsertRadioButton(GetResString(_T("DOWNLOAD_VALIDATOR_ACCEPT")), TVI_ROOT, m_iDownloadValidator == 3);
		m_htiDownloadValidatorAcceptPercentage = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_ACCEPT_PERCENTAGE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorAccept);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorAcceptPercentage, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorAccept, TVE_EXPAND);
		m_htiDownloadValidatorRejectCanceled = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_CANCELED")), TVI_ROOT, m_bDownloadValidatorRejectCanceled);
		m_htiDownloadValidatorRejectSameHash = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_SAME_HASH")), TVI_ROOT, m_bDownloadValidatorRejectSameHash);
		m_htiDownloadValidatorRejectBlacklisted = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_BLACKLISTED")), TVI_ROOT, m_bDownloadValidatorRejectBlacklisted);
		m_htiDownloadValidatorCaseInsensitive = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_CASE_INSENSITIVE")), TVI_ROOT, m_bDownloadValidatorCaseInsensitive);
		m_htiDownloadValidatorIgnoreExtension = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_IGNORE_EXTENSION")), TVI_ROOT, m_bDownloadValidatorIgnoreExtension);
		m_htiDownloadValidatorIgnoreTags = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_IGNORE_TAGS")), TVI_ROOT, m_bDownloadValidatorIgnoreTags);
		m_htiDownloadValidatorDontIgnoreNumericTags = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DONT_IGNORE_NUMERIC_TAGS")), TVI_ROOT, m_bDownloadValidatorDontIgnoreNumericTags);
		m_htiDownloadValidatorIgnoreNonAlphaNumeric = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_IGNORE_NON_ALPHANUMERIC")), TVI_ROOT, m_bDownloadValidatorIgnoreNonAlphaNumeric);
		m_htiDownloadValidatorCleanMojibake = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_CLEAN_MOJIBAKE")), TVI_ROOT, m_bDownloadValidatorCleanMojibake);
		m_htiDownloadValidatorMinimumComparisonLength = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_MINIMUM_COMPARISON_LENGTH")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidator);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorMinimumComparisonLength, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorMediaLengthMatching = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_MEDIA_LENGTH_MATCHING")), TVI_ROOT, m_bDownloadValidatorMediaLengthMatching);
		m_htiDownloadValidatorMediaLengthTolerance = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_MEDIA_LENGTH_TOLERANCE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorMediaLengthMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorMediaLengthTolerance, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorDateTimeMatching = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_MATCHING")), TVI_ROOT, m_bDownloadValidatorDateTimeMatching);
		VERIFY(m_ctrlTreeOptions.SetCheckBoxIndependent(m_htiDownloadValidatorDateTimeMatching, TRUE));
		m_htiDownloadValidatorDateTimeUseYearRange = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_USE_YEAR_RANGE")), m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeUseYearRange);
		m_htiDownloadValidatorDateTimeYearStart = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_START")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorDateTimeUseYearRange);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorDateTimeYearStart, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorDateTimeYearEnd = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_END")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorDateTimeUseYearRange);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorDateTimeYearEnd, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorDateTimeCheckSeconds = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_CHECK_SECONDS")), m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeCheckSeconds);
		m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues = m_ctrlTreeOptions.InsertCheckBox(
			GetResString(_T("DOWNLOAD_VALIDATOR_DATETIME_INCLUDE_FOLLOWING_NUMERIC_VALUES")), m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeIncludeFollowingNumericValues);
		m_htiDownloadValidatorRegexMatching = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_MATCHING")), TVI_ROOT, m_bDownloadValidatorRegexMatching);
		m_htiDownloadValidatorFuzzyMatching = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_MATCHING")), TVI_ROOT, m_bDownloadValidatorFuzzyMatching);
		m_htiDownloadValidatorFuzzySimilarityThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_SIMILARITY_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzySimilarityThreshold, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiDownloadValidatorFuzzyDisplayThreshold = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_DISPLAY_THRESHOLD")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyDisplayThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorFuzzyMinimumSharedTokens = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_SHARED_TOKENS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyMinimumSharedTokens, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorFuzzyMinimumTokenCoverage = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_TOKEN_COVERAGE")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyMinimumTokenCoverage, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorFuzzyMinimumLengthSimilarity = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_LENGTH_SIMILARITY")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyMinimumLengthSimilarity, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorFuzzyMinimumEditSimilarity = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_EDIT_SIMILARITY")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyMinimumEditSimilarity, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_GROUP_LETTERS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits = m_ctrlTreeOptions.InsertItem(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_ID_DIGITS")), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDownloadValidatorFuzzyMatching);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorDateTimeMatching, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorDateTimeUseYearRange, TVE_EXPAND);
		m_htiDownloadValidatorSkipIncompleteFileConfirmation = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_SKIP_INCOMPLETE_CONFIRMATION")), TVI_ROOT, m_bDownloadValidatorSkipIncompleteFileConfirmation);
		m_htiDownloadValidatorMarkAsBlacklisted = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_MARK_AS_BLACKLISTED")), TVI_ROOT, m_bDownloadValidatorMarkAsBlacklisted);
		m_htiDownloadValidatorAutoMarkAsBlacklisted = m_ctrlTreeOptions.InsertCheckBox(GetResString(_T("DOWNLOAD_VALIDATOR_AUTO_MARK_AS_BLACKLISTED")), TVI_ROOT, m_bDownloadValidatorAutoMarkAsBlacklisted);
		m_bInitializedTreeOpts = true;
	}

	if (pDX->m_bSaveAndValidate) {
		BOOL bChecked = FALSE;
		m_iDownloadValidator = 0;
		if (m_ctrlTreeOptions.GetRadioButton(m_htiDownloadValidatorAlwaysAsk, bChecked) && bChecked)
			m_iDownloadValidator = 1;
		else if (m_ctrlTreeOptions.GetRadioButton(m_htiDownloadValidatorReject, bChecked) && bChecked)
			m_iDownloadValidator = 2;
		else if (m_ctrlTreeOptions.GetRadioButton(m_htiDownloadValidatorAccept, bChecked) && bChecked)
			m_iDownloadValidator = 3;
	} else {
		HTREEITEM hSelectedMode = m_htiDownloadValidatorPassive;
		if (m_iDownloadValidator == 1)
			hSelectedMode = m_htiDownloadValidatorAlwaysAsk;
		else if (m_iDownloadValidator == 2)
			hSelectedMode = m_htiDownloadValidatorReject;
		else if (m_iDownloadValidator == 3)
			hSelectedMode = m_htiDownloadValidatorAccept;
		m_ctrlTreeOptions.SetRadioButton(hSelectedMode);
	}
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorAcceptPercentage, m_iDownloadValidatorAcceptPercentage);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorAcceptPercentage, 1, 100);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorRejectCanceled, m_bDownloadValidatorRejectCanceled);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorRejectSameHash, m_bDownloadValidatorRejectSameHash);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorRejectBlacklisted, m_bDownloadValidatorRejectBlacklisted);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorCaseInsensitive, m_bDownloadValidatorCaseInsensitive);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorIgnoreExtension, m_bDownloadValidatorIgnoreExtension);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorIgnoreTags, m_bDownloadValidatorIgnoreTags);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDontIgnoreNumericTags, m_bDownloadValidatorDontIgnoreNumericTags);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorIgnoreNonAlphaNumeric, m_bDownloadValidatorIgnoreNonAlphaNumeric);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorCleanMojibake, m_bDownloadValidatorCleanMojibake);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorMinimumComparisonLength, m_iDownloadValidatorMinimumComparisonLength);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorMinimumComparisonLength, 4, INT_MAX);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDateTimeMatching, m_bDownloadValidatorDateTimeMatching);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDateTimeUseYearRange, m_bDownloadValidatorDateTimeUseYearRange);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDateTimeYearStart, m_iDownloadValidatorDateTimeYearStart);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorDateTimeYearStart, 1000, 9999);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDateTimeYearEnd, m_iDownloadValidatorDateTimeYearEnd);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorDateTimeYearEnd, 1000, 9999);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDateTimeCheckSeconds, m_bDownloadValidatorDateTimeCheckSeconds);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, m_bDownloadValidatorDateTimeIncludeFollowingNumericValues);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorRegexMatching, m_bDownloadValidatorRegexMatching);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyMatching, m_bDownloadValidatorFuzzyMatching);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzySimilarityThreshold, m_strDownloadValidatorFuzzySimilarityThreshold);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyDisplayThreshold, m_iDownloadValidatorFuzzyDisplayThreshold);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyDisplayThreshold, 0, 100);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyMinimumSharedTokens, m_iDownloadValidatorFuzzyMinimumSharedTokens);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyMinimumSharedTokens, 1, 32);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyMinimumTokenCoverage, m_iDownloadValidatorFuzzyMinimumTokenCoverage);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyMinimumTokenCoverage, 0, 100);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyMinimumLengthSimilarity, m_iDownloadValidatorFuzzyMinimumLengthSimilarity);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyMinimumLengthSimilarity, 0, 100);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyMinimumEditSimilarity, m_iDownloadValidatorFuzzyMinimumEditSimilarity);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyMinimumEditSimilarity, 0, 100);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters, m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters, 1, 32);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits, m_iDownloadValidatorFuzzyStructuralMinimumIDDigits);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorFuzzyStructuralMinimumIDDigits, 1, 32);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorMediaLengthMatching, m_bDownloadValidatorMediaLengthMatching);
	DDX_TreeEdit(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorMediaLengthTolerance, m_iDownloadValidatorMediaLengthTolerance);
	DDV_MinMaxInt(pDX, m_iDownloadValidatorMediaLengthTolerance, 0, INT_MAX);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorSkipIncompleteFileConfirmation, m_bDownloadValidatorSkipIncompleteFileConfirmation);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorMarkAsBlacklisted, m_bDownloadValidatorMarkAsBlacklisted);
	DDX_TreeCheck(pDX, IDC_DOWNLOAD_VALIDATOR_OPTIONS, m_htiDownloadValidatorAutoMarkAsBlacklisted, m_bDownloadValidatorAutoMarkAsBlacklisted);
}

void CPPgDownloadValidator::LoadSettings()
{
	m_iDownloadValidator = thePrefs.GetDownloadValidator();
	m_iDownloadValidatorAcceptPercentage = thePrefs.GetDownloadValidatorAcceptPercentage();
	m_bDownloadValidatorRejectCanceled = thePrefs.GetDownloadValidatorRejectCanceled();
	m_bDownloadValidatorRejectSameHash = thePrefs.GetDownloadValidatorRejectSameHash();
	m_bDownloadValidatorRejectBlacklisted = thePrefs.GetDownloadValidatorRejectBlacklisted();
	m_bDownloadValidatorCaseInsensitive = thePrefs.GetDownloadValidatorCaseInsensitive();
	m_bDownloadValidatorIgnoreExtension = thePrefs.GetDownloadValidatorIgnoreExtension();
	m_bDownloadValidatorIgnoreTags = thePrefs.GetDownloadValidatorIgnoreTags();
	m_bDownloadValidatorDontIgnoreNumericTags = thePrefs.GetDownloadValidatorDontIgnoreNumericTags();
	m_bDownloadValidatorIgnoreNonAlphaNumeric = thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric();
	m_bDownloadValidatorCleanMojibake = thePrefs.GetDownloadValidatorCleanMojibake();
	m_iDownloadValidatorMinimumComparisonLength = thePrefs.GetDownloadValidatorMinimumComparisonLength();
	m_bDownloadValidatorSkipIncompleteFileConfirmation = thePrefs.GetDownloadValidatorSkipIncompleteFileConfirmation();
	m_bDownloadValidatorMarkAsBlacklisted = thePrefs.GetDownloadValidatorMarkAsBlacklisted();
	m_bDownloadValidatorAutoMarkAsBlacklisted = thePrefs.GetDownloadValidatorAutoMarkAsBlacklisted();
	m_bDownloadValidatorDateTimeMatching = thePrefs.GetDownloadValidatorDateTimeMatching();
	m_bDownloadValidatorDateTimeUseYearRange = thePrefs.GetDownloadValidatorDateTimeUseYearRange();
	m_iDownloadValidatorDateTimeYearStart = thePrefs.GetDownloadValidatorDateTimeYearStart();
	m_iDownloadValidatorDateTimeYearEnd = thePrefs.GetDownloadValidatorDateTimeYearEnd();
	m_bDownloadValidatorDateTimeCheckSeconds = thePrefs.GetDownloadValidatorDateTimeCheckSeconds();
	m_bDownloadValidatorDateTimeIncludeFollowingNumericValues = thePrefs.GetDownloadValidatorDateTimeIncludeFollowingNumericValues();
	m_bDownloadValidatorRegexMatching = thePrefs.GetDownloadValidatorRegexMatching();
	m_bDownloadValidatorFuzzyMatching = thePrefs.GetDownloadValidatorFuzzyMatching();
	const uint32 uFuzzySimilarityThreshold = thePrefs.GetDownloadValidatorFuzzySimilarityThreshold();
	m_strDownloadValidatorFuzzySimilarityThreshold.Format(_T("%u"), uFuzzySimilarityThreshold);
	m_iDownloadValidatorFuzzyDisplayThreshold = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyDisplayThresholdPercent());
	m_iDownloadValidatorFuzzyMinimumSharedTokens = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyMinimumSharedTokens());
	m_iDownloadValidatorFuzzyMinimumTokenCoverage = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyMinimumTokenCoveragePercent());
	m_iDownloadValidatorFuzzyMinimumLengthSimilarity = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyMinimumLengthSimilarityPercent());
	m_iDownloadValidatorFuzzyMinimumEditSimilarity = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyMinimumEditSimilarityPercent());
	m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyStructuralMinimumGroupLetters());
	m_iDownloadValidatorFuzzyStructuralMinimumIDDigits = static_cast<int>(thePrefs.GetDownloadValidatorFuzzyStructuralMinimumIDDigits());
	m_bDownloadValidatorMediaLengthMatching = thePrefs.GetDownloadValidatorMediaLengthMatching();
	const uint32 uMediaLengthTolerance = thePrefs.GetDownloadValidatorMediaLengthToleranceSec();
	m_iDownloadValidatorMediaLengthTolerance = uMediaLengthTolerance > static_cast<uint32>(INT_MAX) ? INT_MAX : static_cast<int>(uMediaLengthTolerance);
}

void CPPgDownloadValidator::ResetToDefaults()
{
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	m_iDownloadValidator = 0;
	m_iDownloadValidatorAcceptPercentage = 10;
	m_bDownloadValidatorRejectCanceled = true;
	m_bDownloadValidatorRejectSameHash = true;
	m_bDownloadValidatorRejectBlacklisted = true;
	m_bDownloadValidatorCaseInsensitive = true;
	m_bDownloadValidatorIgnoreExtension = true;
	m_bDownloadValidatorIgnoreTags = true;
	m_bDownloadValidatorDontIgnoreNumericTags = true;
	m_bDownloadValidatorIgnoreNonAlphaNumeric = true;
	m_bDownloadValidatorCleanMojibake = true;
	m_iDownloadValidatorMinimumComparisonLength = 8;
	m_bDownloadValidatorSkipIncompleteFileConfirmation = false;
	m_bDownloadValidatorMarkAsBlacklisted = true;
	m_bDownloadValidatorAutoMarkAsBlacklisted = true;
	m_bDownloadValidatorDateTimeMatching = true;
	m_bDownloadValidatorDateTimeUseYearRange = true;
	m_iDownloadValidatorDateTimeYearStart = 1900;
	m_iDownloadValidatorDateTimeYearEnd = 2040;
	m_bDownloadValidatorDateTimeCheckSeconds = true;
	m_bDownloadValidatorDateTimeIncludeFollowingNumericValues = true;
	m_bDownloadValidatorRegexMatching = true;
	m_bDownloadValidatorFuzzyMatching = false;
	m_strDownloadValidatorFuzzySimilarityThreshold = _T("100");
	m_iDownloadValidatorFuzzyDisplayThreshold = 30;
	m_iDownloadValidatorFuzzyMinimumSharedTokens = 2;
	m_iDownloadValidatorFuzzyMinimumTokenCoverage = 75;
	m_iDownloadValidatorFuzzyMinimumLengthSimilarity = 75;
	m_iDownloadValidatorFuzzyMinimumEditSimilarity = 80;
	m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters = 3;
	m_iDownloadValidatorFuzzyStructuralMinimumIDDigits = 3;
	m_bDownloadValidatorMediaLengthMatching = true;
	m_iDownloadValidatorMediaLengthTolerance = 5;
	UpdateData(FALSE);
	UpdateDownloadValidatorDateTimeUi();
	UpdateDownloadValidatorFuzzyUi();
	UpdateDownloadValidatorMediaLengthUi();
	m_bSettingsDirty = true;
	SetModified(TRUE);
}

BOOL CPPgDownloadValidator::OnInitDialog()
{
	LoadSettings();
	m_ctrlTreeOptions.SetImageListColorFlags(theApp.m_iDfltImageListColorFlags);
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	m_ctrlViewTabs.m_bClosable = false;
	m_ctrlViewTabs.m_bShowCloseButton = false;
	m_ctrlViewTabs.SetVisualScalePercent(CPreferencesDlg::ScaleOptionsValue(100));
	InitializeViewTabs();

	if (m_tooltipTreeOptions.GetSafeHwnd() == NULL && m_tooltipTreeOptions.Create(this)) {
		m_tooltipTreeOptions.AddTool(&m_ctrlTreeOptions, _T(""));
		m_tooltipTreeOptions.SetMaxTipWidth(CPreferencesDlg::ScaleOptionsValue(420));
		m_tooltipTreeOptions.SetAutoTabHeaderIcon(false);
		m_tooltipTreeOptions.Activate(false);
	}

	CWnd* pRulesText = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX);
	if (pRulesText != NULL) {
		pRulesText->SendMessage(EM_LIMITTEXT, CDownloadValidator::RegexRulesTextLimit);
		pRulesText->SendMessage(EM_EXLIMITTEXT, 0, CDownloadValidator::RegexRulesTextLimit);
		pRulesText->SendMessage(EM_SETEVENTMASK, 0, pRulesText->SendMessage(EM_GETEVENTMASK) | ENM_CHANGE);
	}

	CDownloadValidator::SRegexRulesResult result;
	if (theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->ReadRegexRulesText(m_strRulesText, result);

	Localize();
	UpdateRulesTextBoxColors();
	UpdateDownloadValidatorDateTimeUi();
	UpdateDownloadValidatorFuzzyUi();
	UpdateDownloadValidatorMediaLengthUi();
	m_ctrlTreeOptions.Expand(m_htiDownloadValidatorFuzzyMatching, TVE_EXPAND);
	SwitchView(ViewSettings);
	UpdateLayout();
	return TRUE;
}

BOOL CPPgDownloadValidator::OnKillActive()
{
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	CaptureRulesText();
	return CPropertyPage::OnKillActive();
}

BOOL CPPgDownloadValidator::OnApply()
{
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	CaptureRulesText();
	if (!UpdateData())
		return FALSE;

	if (m_iDownloadValidatorDateTimeYearStart > m_iDownloadValidatorDateTimeYearEnd) {
		const int iTemp = m_iDownloadValidatorDateTimeYearStart;
		m_iDownloadValidatorDateTimeYearStart = m_iDownloadValidatorDateTimeYearEnd;
		m_iDownloadValidatorDateTimeYearEnd = iTemp;
	}

	uint32 uFuzzySimilarityThreshold = 0;
	if (!ParseFuzzySimilarityThreshold(uFuzzySimilarityThreshold)) {
		SwitchView(ViewSettings);
		m_ctrlTreeOptions.Expand(m_htiDownloadValidatorFuzzyMatching, TVE_EXPAND);
		m_ctrlTreeOptions.SelectItem(m_htiDownloadValidatorFuzzySimilarityThreshold);
		m_ctrlTreeOptions.SetFocus();
		CDarkMode::MessageBox(GetResString(_T("DOWNLOAD_VALIDATOR_FUZZY_SIMILARITY_THRESHOLD_INVALID")), MB_ICONWARNING);
		return FALSE;
	}

	CDownloadValidator::SRegexRulesResult result;
	if (!ValidateRules(result, false)) {
		SwitchView(ViewRules);
		SelectRegexErrorLine(result.uLineNumber);
		CDarkMode::MessageBox(GetRegexResultText(result, false), MB_ICONWARNING);
		return FALSE;
	}

	const bool bCaseInsensitiveChanged = thePrefs.GetDownloadValidatorCaseInsensitive() != m_bDownloadValidatorCaseInsensitive;
	const bool bRulesNeedApply = m_bRulesDirty || bCaseInsensitiveChanged;
	if (bRulesNeedApply && (theApp.DownloadValidator == NULL || !theApp.DownloadValidator->ApplyRegexRulesText(m_strRulesText, m_bDownloadValidatorCaseInsensitive, result))) {
		SwitchView(ViewRules);
		SelectRegexErrorLine(result.uLineNumber);
		CDarkMode::MessageBox(GetRegexResultText(result, false), MB_ICONWARNING);
		return FALSE;
	}

	const bool bDownloadValidatorStateChanged = thePrefs.GetDownloadValidator() != m_iDownloadValidator;
	const bool bFuzzyDecisionThresholdChanged = m_bDownloadValidatorFuzzyMatching && thePrefs.GetDownloadValidatorFuzzySimilarityThreshold() != uFuzzySimilarityThreshold;
	const bool bFuzzyDisplayThresholdChanged = m_bDownloadValidatorFuzzyMatching
		&& thePrefs.GetDownloadValidatorFuzzyDisplayThresholdPercent() != static_cast<uint32>(m_iDownloadValidatorFuzzyDisplayThreshold);
	const bool bFuzzyCandidateEvidenceChanged = m_bDownloadValidatorFuzzyMatching
		&& (thePrefs.GetDownloadValidatorFuzzyMinimumSharedTokens() != static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumSharedTokens)
		|| thePrefs.GetDownloadValidatorFuzzyMinimumTokenCoveragePercent() != static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumTokenCoverage)
		|| thePrefs.GetDownloadValidatorFuzzyMinimumLengthSimilarityPercent() != static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumLengthSimilarity)
		|| thePrefs.GetDownloadValidatorFuzzyMinimumEditSimilarityPercent() != static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumEditSimilarity));
	const bool bStructuralSettingsChanged = m_bDownloadValidatorFuzzyMatching
		&& (thePrefs.GetDownloadValidatorFuzzyStructuralMinimumGroupLetters() != static_cast<uint32>(m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters)
		|| thePrefs.GetDownloadValidatorFuzzyStructuralMinimumIDDigits() != static_cast<uint32>(m_iDownloadValidatorFuzzyStructuralMinimumIDDigits));
	const bool bMediaLengthToleranceChanged = m_bDownloadValidatorMediaLengthMatching
		&& thePrefs.GetDownloadValidatorMediaLengthToleranceSec() != static_cast<uint32>(m_iDownloadValidatorMediaLengthTolerance);
	const bool bMinimumComparisonLengthChanged = thePrefs.GetDownloadValidatorMinimumComparisonLength() != m_iDownloadValidatorMinimumComparisonLength;
	const bool bReloadMap = thePrefs.GetDownloadValidatorIgnoreExtension() != m_bDownloadValidatorIgnoreExtension
		|| thePrefs.GetDownloadValidatorIgnoreTags() != m_bDownloadValidatorIgnoreTags
		|| thePrefs.GetDownloadValidatorDontIgnoreNumericTags() != m_bDownloadValidatorDontIgnoreNumericTags
		|| thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() != m_bDownloadValidatorIgnoreNonAlphaNumeric
		|| thePrefs.GetDownloadValidatorCleanMojibake() != m_bDownloadValidatorCleanMojibake
		|| bCaseInsensitiveChanged
		|| thePrefs.GetDownloadValidatorDateTimeMatching() != m_bDownloadValidatorDateTimeMatching
		|| thePrefs.GetDownloadValidatorDateTimeUseYearRange() != m_bDownloadValidatorDateTimeUseYearRange
		|| thePrefs.GetDownloadValidatorDateTimeYearStart() != m_iDownloadValidatorDateTimeYearStart
		|| thePrefs.GetDownloadValidatorDateTimeYearEnd() != m_iDownloadValidatorDateTimeYearEnd
		|| thePrefs.GetDownloadValidatorDateTimeCheckSeconds() != m_bDownloadValidatorDateTimeCheckSeconds
		|| thePrefs.GetDownloadValidatorDateTimeIncludeFollowingNumericValues() != m_bDownloadValidatorDateTimeIncludeFollowingNumericValues
		|| thePrefs.GetDownloadValidatorRegexMatching() != m_bDownloadValidatorRegexMatching
		|| thePrefs.GetDownloadValidatorFuzzyMatching() != m_bDownloadValidatorFuzzyMatching
		|| thePrefs.GetDownloadValidatorMediaLengthMatching() != m_bDownloadValidatorMediaLengthMatching
		|| bStructuralSettingsChanged
		|| (m_bDownloadValidatorFuzzyMatching && (bMinimumComparisonLengthChanged || bDownloadValidatorStateChanged));

	if (bDownloadValidatorStateChanged) {
		thePrefs.SetDownloadValidator(m_iDownloadValidator);
		if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL)
			theApp.emuledlg->searchwnd->CreateMenus();
	}

	thePrefs.SetDownloadValidatorAcceptPercentage(m_iDownloadValidatorAcceptPercentage);
	thePrefs.SetDownloadValidatorRejectCanceled(m_bDownloadValidatorRejectCanceled);
	thePrefs.SetDownloadValidatorRejectSameHash(m_bDownloadValidatorRejectSameHash);
	thePrefs.SetDownloadValidatorRejectBlacklisted(m_bDownloadValidatorRejectBlacklisted);
	thePrefs.SetDownloadValidatorCaseInsensitive(m_bDownloadValidatorCaseInsensitive);
	thePrefs.SetDownloadValidatorIgnoreExtension(m_bDownloadValidatorIgnoreExtension);
	thePrefs.SetDownloadValidatorIgnoreTags(m_bDownloadValidatorIgnoreTags);
	thePrefs.SetDownloadValidatorDontIgnoreNumericTags(m_bDownloadValidatorDontIgnoreNumericTags);
	thePrefs.SetDownloadValidatorIgnoreNonAlphaNumeric(m_bDownloadValidatorIgnoreNonAlphaNumeric);
	thePrefs.SetDownloadValidatorCleanMojibake(m_bDownloadValidatorCleanMojibake);
	thePrefs.SetDownloadValidatorMinimumComparisonLength(m_iDownloadValidatorMinimumComparisonLength);
	thePrefs.SetDownloadValidatorSkipIncompleteFileConfirmation(m_bDownloadValidatorSkipIncompleteFileConfirmation);
	thePrefs.SetDownloadValidatorMarkAsBlacklisted(m_bDownloadValidatorMarkAsBlacklisted);
	thePrefs.SetDownloadValidatorAutoMarkAsBlacklisted(m_bDownloadValidatorAutoMarkAsBlacklisted);
	thePrefs.SetDownloadValidatorDateTimeMatching(m_bDownloadValidatorDateTimeMatching);
	thePrefs.SetDownloadValidatorDateTimeUseYearRange(m_bDownloadValidatorDateTimeUseYearRange);
	thePrefs.SetDownloadValidatorDateTimeYearStart(m_iDownloadValidatorDateTimeYearStart);
	thePrefs.SetDownloadValidatorDateTimeYearEnd(m_iDownloadValidatorDateTimeYearEnd);
	thePrefs.SetDownloadValidatorDateTimeCheckSeconds(m_bDownloadValidatorDateTimeCheckSeconds);
	thePrefs.SetDownloadValidatorDateTimeIncludeFollowingNumericValues(m_bDownloadValidatorDateTimeIncludeFollowingNumericValues);
	thePrefs.SetDownloadValidatorRegexMatching(m_bDownloadValidatorRegexMatching);
	thePrefs.SetDownloadValidatorFuzzyMatching(m_bDownloadValidatorFuzzyMatching);
	thePrefs.SetDownloadValidatorFuzzySimilarityThreshold(uFuzzySimilarityThreshold);
	thePrefs.SetDownloadValidatorFuzzyDisplayThresholdPercent(static_cast<uint32>(m_iDownloadValidatorFuzzyDisplayThreshold));
	thePrefs.SetDownloadValidatorFuzzyMinimumSharedTokens(static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumSharedTokens));
	thePrefs.SetDownloadValidatorFuzzyMinimumTokenCoveragePercent(static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumTokenCoverage));
	thePrefs.SetDownloadValidatorFuzzyMinimumLengthSimilarityPercent(static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumLengthSimilarity));
	thePrefs.SetDownloadValidatorFuzzyMinimumEditSimilarityPercent(static_cast<uint32>(m_iDownloadValidatorFuzzyMinimumEditSimilarity));
	thePrefs.SetDownloadValidatorFuzzyStructuralMinimumGroupLetters(static_cast<uint32>(m_iDownloadValidatorFuzzyStructuralMinimumGroupLetters));
	thePrefs.SetDownloadValidatorFuzzyStructuralMinimumIDDigits(static_cast<uint32>(m_iDownloadValidatorFuzzyStructuralMinimumIDDigits));
	thePrefs.SetDownloadValidatorMediaLengthMatching(m_bDownloadValidatorMediaLengthMatching);
	thePrefs.SetDownloadValidatorMediaLengthToleranceSec(static_cast<uint32>(m_iDownloadValidatorMediaLengthTolerance));

	const bool bAutomaticDecisionRefresh = bFuzzyDecisionThresholdChanged || bFuzzyCandidateEvidenceChanged || bMediaLengthToleranceChanged;
	if (theApp.DownloadValidator != NULL) {
		if (bReloadMap)
			theApp.DownloadValidator->QueueReloadMap();
		else if (bRulesNeedApply)
			theApp.DownloadValidator->QueueReloadRegexMap();
		else {
			if (bFuzzyDisplayThresholdChanged || bFuzzyCandidateEvidenceChanged || bMediaLengthToleranceChanged)
				theApp.DownloadValidator->InvalidatePossibleKnownResults(bFuzzyCandidateEvidenceChanged || bMediaLengthToleranceChanged);
			if (bAutomaticDecisionRefresh && !bFuzzyCandidateEvidenceChanged && !bMediaLengthToleranceChanged)
				theApp.DownloadValidator->InvalidateEvaluationResults();
		}
	}
	if (!bReloadMap && !bRulesNeedApply && bAutomaticDecisionRefresh && theApp.searchlist != NULL)
		theApp.searchlist->RequestDownloadValidatorRecheckForAllSearches();
	if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->m_pwndResults != NULL) {
		if (!bReloadMap && !bRulesNeedApply && (bFuzzyDisplayThresholdChanged || bFuzzyCandidateEvidenceChanged || bMediaLengthToleranceChanged))
			theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.QueuePossibleKnownRefresh(50);
		else if (bReloadMap || bRulesNeedApply || bDownloadValidatorStateChanged || bMinimumComparisonLengthChanged || bAutomaticDecisionRefresh)
			theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.QueueDeferredReload(false, LSF_SELECTION, 50);
	}

	m_bRulesDirty = false;
	m_bSettingsDirty = false;
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgDownloadValidator::LocalizeItemText(HTREEITEM item, LPCTSTR strid)
{
	if (item != NULL)
		m_ctrlTreeOptions.SetItemText(item, GetResString(strid));
}

void CPPgDownloadValidator::LocalizeItemInfoText(HTREEITEM item, LPCTSTR strid)
{
	if (item != NULL)
		m_ctrlTreeOptions.SetItemInfo(item, GetResString(strid));
}

void CPPgDownloadValidator::LocalizeEditLabel(HTREEITEM item, LPCTSTR strid)
{
	if (item != NULL)
		m_ctrlTreeOptions.SetEditLabel(item, GetResString(strid));
}

void CPPgDownloadValidator::Localize()
{
	if (m_hWnd == NULL)
		return;

	SetWindowText(GetResString(_T("DOWNLOAD_VALIDATOR")));
	UpdateViewTabs();
	SetDlgItemText(IDC_DOWNLOAD_VALIDATOR_REGEX_VALIDATE, GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_VALIDATE")));
	SetDlgItemText(IDC_DOWNLOAD_VALIDATOR_REGEX_RELOAD, GetResString(_T("SF_RELOAD")));

	LocalizeItemText(m_htiDownloadValidatorPassive, _T("UDPDISABLED"));
	LocalizeItemInfoText(m_htiDownloadValidatorPassive, _T("DOWNLOAD_VALIDATOR_PASSIVE_INFO"));
	LocalizeItemText(m_htiDownloadValidatorAlwaysAsk, _T("DOWNLOAD_VALIDATOR_ALWAYS_ASK"));
	LocalizeItemInfoText(m_htiDownloadValidatorAlwaysAsk, _T("DOWNLOAD_VALIDATOR_ALWAYS_ASK_INFO"));
	LocalizeItemText(m_htiDownloadValidatorReject, _T("DOWNLOAD_VALIDATOR_REJECT"));
	LocalizeItemInfoText(m_htiDownloadValidatorReject, _T("DOWNLOAD_VALIDATOR_REJECT_INFO"));
	LocalizeItemText(m_htiDownloadValidatorAccept, _T("DOWNLOAD_VALIDATOR_ACCEPT"));
	LocalizeItemInfoText(m_htiDownloadValidatorAccept, _T("DOWNLOAD_VALIDATOR_ACCEPT_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorAcceptPercentage, _T("DOWNLOAD_VALIDATOR_ACCEPT_PERCENTAGE"));
	LocalizeItemInfoText(m_htiDownloadValidatorAcceptPercentage, _T("DOWNLOAD_VALIDATOR_ACCEPT_PERCENTAGE_INFO"));
	LocalizeItemText(m_htiDownloadValidatorRejectCanceled, _T("DOWNLOAD_VALIDATOR_REJECT_CANCELED"));
	LocalizeItemInfoText(m_htiDownloadValidatorRejectCanceled, _T("DOWNLOAD_VALIDATOR_REJECT_CANCELED_INFO"));
	LocalizeItemText(m_htiDownloadValidatorRejectSameHash, _T("DOWNLOAD_VALIDATOR_REJECT_SAME_HASH"));
	LocalizeItemInfoText(m_htiDownloadValidatorRejectSameHash, _T("DOWNLOAD_VALIDATOR_REJECT_SAME_HASH_INFO"));
	LocalizeItemText(m_htiDownloadValidatorRejectBlacklisted, _T("DOWNLOAD_VALIDATOR_REJECT_BLACKLISTED"));
	LocalizeItemInfoText(m_htiDownloadValidatorRejectBlacklisted, _T("DOWNLOAD_VALIDATOR_REJECT_BLACKLISTED_INFO"));
	LocalizeItemText(m_htiDownloadValidatorCaseInsensitive, _T("DOWNLOAD_VALIDATOR_CASE_INSENSITIVE"));
	LocalizeItemInfoText(m_htiDownloadValidatorCaseInsensitive, _T("DOWNLOAD_VALIDATOR_CASE_INSENSITIVE_INFO"));
	LocalizeItemText(m_htiDownloadValidatorIgnoreExtension, _T("DOWNLOAD_VALIDATOR_IGNORE_EXTENSION"));
	LocalizeItemInfoText(m_htiDownloadValidatorIgnoreExtension, _T("DOWNLOAD_VALIDATOR_IGNORE_EXTENSION_INFO"));
	LocalizeItemText(m_htiDownloadValidatorIgnoreTags, _T("DOWNLOAD_VALIDATOR_IGNORE_TAGS"));
	LocalizeItemInfoText(m_htiDownloadValidatorIgnoreTags, _T("DOWNLOAD_VALIDATOR_IGNORE_TAGS_INFO"));
	LocalizeItemText(m_htiDownloadValidatorDontIgnoreNumericTags, _T("DOWNLOAD_VALIDATOR_DONT_IGNORE_NUMERIC_TAGS"));
	LocalizeItemInfoText(m_htiDownloadValidatorDontIgnoreNumericTags, _T("DOWNLOAD_VALIDATOR_DONT_IGNORE_NUMERIC_TAGS_INFO"));
	LocalizeItemText(m_htiDownloadValidatorIgnoreNonAlphaNumeric, _T("DOWNLOAD_VALIDATOR_IGNORE_NON_ALPHANUMERIC"));
	LocalizeItemInfoText(m_htiDownloadValidatorIgnoreNonAlphaNumeric, _T("DOWNLOAD_VALIDATOR_IGNORE_NON_ALPHANUMERIC_INFO"));
	LocalizeItemText(m_htiDownloadValidatorCleanMojibake, _T("DOWNLOAD_VALIDATOR_CLEAN_MOJIBAKE"));
	LocalizeItemInfoText(m_htiDownloadValidatorCleanMojibake, _T("DOWNLOAD_VALIDATOR_CLEAN_MOJIBAKE_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorMinimumComparisonLength, _T("DOWNLOAD_VALIDATOR_MINIMUM_COMPARISON_LENGTH"));
	LocalizeItemInfoText(m_htiDownloadValidatorMinimumComparisonLength, _T("DOWNLOAD_VALIDATOR_MINIMUM_COMPARISON_LENGTH_INFO"));
	LocalizeItemText(m_htiDownloadValidatorDateTimeMatching, _T("DOWNLOAD_VALIDATOR_DATETIME_MATCHING"));
	LocalizeItemInfoText(m_htiDownloadValidatorDateTimeMatching, _T("DOWNLOAD_VALIDATOR_DATETIME_MATCHING_INFO"));
	LocalizeItemText(m_htiDownloadValidatorDateTimeUseYearRange, _T("DOWNLOAD_VALIDATOR_DATETIME_USE_YEAR_RANGE"));
	LocalizeItemInfoText(m_htiDownloadValidatorDateTimeUseYearRange, _T("DOWNLOAD_VALIDATOR_DATETIME_USE_YEAR_RANGE_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorDateTimeYearStart, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_START"));
	LocalizeItemInfoText(m_htiDownloadValidatorDateTimeYearStart, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_START_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorDateTimeYearEnd, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_END"));
	LocalizeItemInfoText(m_htiDownloadValidatorDateTimeYearEnd, _T("DOWNLOAD_VALIDATOR_DATETIME_YEAR_END_INFO"));
	LocalizeItemText(m_htiDownloadValidatorDateTimeCheckSeconds, _T("DOWNLOAD_VALIDATOR_DATETIME_CHECK_SECONDS"));
	LocalizeItemInfoText(m_htiDownloadValidatorDateTimeCheckSeconds, _T("DOWNLOAD_VALIDATOR_DATETIME_CHECK_SECONDS_INFO"));
	LocalizeItemText(m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, _T("DOWNLOAD_VALIDATOR_DATETIME_INCLUDE_FOLLOWING_NUMERIC_VALUES"));
	LocalizeItemInfoText(m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, _T("DOWNLOAD_VALIDATOR_DATETIME_INCLUDE_FOLLOWING_NUMERIC_VALUES_INFO"));
	LocalizeItemText(m_htiDownloadValidatorRegexMatching, _T("DOWNLOAD_VALIDATOR_REGEX_MATCHING"));
	LocalizeItemInfoText(m_htiDownloadValidatorRegexMatching, _T("DOWNLOAD_VALIDATOR_REGEX_MATCHING_INFO"));
	LocalizeItemText(m_htiDownloadValidatorFuzzyMatching, _T("DOWNLOAD_VALIDATOR_FUZZY_MATCHING"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyMatching, _T("DOWNLOAD_VALIDATOR_FUZZY_MATCHING_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzySimilarityThreshold, _T("DOWNLOAD_VALIDATOR_FUZZY_SIMILARITY_THRESHOLD"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzySimilarityThreshold, _T("DOWNLOAD_VALIDATOR_FUZZY_SIMILARITY_THRESHOLD_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyDisplayThreshold, _T("DOWNLOAD_VALIDATOR_FUZZY_DISPLAY_THRESHOLD"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyDisplayThreshold, _T("DOWNLOAD_VALIDATOR_FUZZY_DISPLAY_THRESHOLD_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyMinimumSharedTokens, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_SHARED_TOKENS"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyMinimumSharedTokens, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_SHARED_TOKENS_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyMinimumTokenCoverage, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_TOKEN_COVERAGE"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyMinimumTokenCoverage, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_TOKEN_COVERAGE_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyMinimumLengthSimilarity, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_LENGTH_SIMILARITY"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyMinimumLengthSimilarity, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_LENGTH_SIMILARITY_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyMinimumEditSimilarity, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_EDIT_SIMILARITY"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyMinimumEditSimilarity, _T("DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_EDIT_SIMILARITY_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters, _T("DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_GROUP_LETTERS"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters, _T("DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_GROUP_LETTERS_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits, _T("DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_ID_DIGITS"));
	LocalizeItemInfoText(m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits, _T("DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_ID_DIGITS_INFO"));
	LocalizeItemText(m_htiDownloadValidatorMediaLengthMatching, _T("DOWNLOAD_VALIDATOR_MEDIA_LENGTH_MATCHING"));
	LocalizeItemInfoText(m_htiDownloadValidatorMediaLengthMatching, _T("DOWNLOAD_VALIDATOR_MEDIA_LENGTH_MATCHING_INFO"));
	LocalizeEditLabel(m_htiDownloadValidatorMediaLengthTolerance, _T("DOWNLOAD_VALIDATOR_MEDIA_LENGTH_TOLERANCE"));
	LocalizeItemInfoText(m_htiDownloadValidatorMediaLengthTolerance, _T("DOWNLOAD_VALIDATOR_MEDIA_LENGTH_TOLERANCE_INFO"));
	LocalizeItemText(m_htiDownloadValidatorSkipIncompleteFileConfirmation, _T("DOWNLOAD_VALIDATOR_SKIP_INCOMPLETE_CONFIRMATION"));
	LocalizeItemInfoText(m_htiDownloadValidatorSkipIncompleteFileConfirmation, _T("DOWNLOAD_VALIDATOR_SKIP_INCOMPLETE_CONFIRMATION_INFO"));
	LocalizeItemText(m_htiDownloadValidatorMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_MARK_AS_BLACKLISTED"));
	LocalizeItemInfoText(m_htiDownloadValidatorMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_MARK_AS_BLACKLISTED_INFO"));
	LocalizeItemText(m_htiDownloadValidatorAutoMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_AUTO_MARK_AS_BLACKLISTED"));
	LocalizeItemInfoText(m_htiDownloadValidatorAutoMarkAsBlacklisted, _T("DOWNLOAD_VALIDATOR_AUTO_MARK_AS_BLACKLISTED_INFO"));

	if (m_eViewMode == ViewHelp)
		SetRulesEditorText(GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_HELP_TEXT")));
	UpdateLayout();
}

void CPPgDownloadValidator::UpdateDownloadValidatorDateTimeUi()
{
	if (!m_bInitializedTreeOpts || m_htiDownloadValidatorDateTimeMatching == NULL)
		return;

	BOOL bDateTimeMatching = FALSE;
	m_ctrlTreeOptions.GetCheckBox(m_htiDownloadValidatorDateTimeMatching, bDateTimeMatching);
	m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDownloadValidatorDateTimeUseYearRange, bDateTimeMatching);
	m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDownloadValidatorDateTimeCheckSeconds, bDateTimeMatching);
	m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues, bDateTimeMatching);
	m_ctrlTreeOptions.Invalidate(FALSE);
}

void CPPgDownloadValidator::UpdateDownloadValidatorFuzzyUi()
{
	if (!m_bInitializedTreeOpts || m_htiDownloadValidatorFuzzyMatching == NULL)
		return;

	BOOL bFuzzyMatching = FALSE;
	m_ctrlTreeOptions.GetCheckBox(m_htiDownloadValidatorFuzzyMatching, bFuzzyMatching);
	m_ctrlTreeOptions.Expand(m_htiDownloadValidatorFuzzyMatching, bFuzzyMatching ? TVE_EXPAND : TVE_COLLAPSE);
	m_ctrlTreeOptions.Invalidate(FALSE);
}

void CPPgDownloadValidator::UpdateDownloadValidatorMediaLengthUi()
{
	if (!m_bInitializedTreeOpts || m_htiDownloadValidatorMediaLengthMatching == NULL)
		return;
	BOOL bMediaLengthMatching = FALSE;
	m_ctrlTreeOptions.GetCheckBox(m_htiDownloadValidatorMediaLengthMatching, bMediaLengthMatching);
	m_ctrlTreeOptions.Expand(m_htiDownloadValidatorMediaLengthMatching, bMediaLengthMatching ? TVE_EXPAND : TVE_COLLAPSE);
	m_ctrlTreeOptions.Invalidate(FALSE);
}

bool CPPgDownloadValidator::ParseFuzzySimilarityThreshold(uint32& uThreshold) const
{
	uThreshold = 0;
	CString strValue(m_strDownloadValidatorFuzzySimilarityThreshold);
	strValue.Trim();
	if (strValue.IsEmpty())
		return false;

	for (int i = 0; i < strValue.GetLength(); ++i) {
		const TCHAR ch = strValue.GetAt(i);
		if (ch < _T('0') || ch > _T('9'))
			return false;
	}

	const uint64 uValue = _tcstoui64(strValue, NULL, 10);
	if (uValue > 100)
		return false;
	uThreshold = static_cast<uint32>(uValue);
	return true;
}

void CPPgDownloadValidator::SwitchView(EViewMode eViewMode)
{
	if (m_eViewMode == ViewRules)
		CaptureRulesText();
	m_eViewMode = eViewMode;

	UpdateViewTabs();

	const bool bSettings = eViewMode == ViewSettings;
	const bool bRules = eViewMode == ViewRules;
	CWnd* pTree = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_OPTIONS);
	CWnd* pEdit = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX);
	CWnd* pValidate = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_VALIDATE);
	CWnd* pReload = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_RELOAD);
	if (pTree != NULL)
		pTree->ShowWindow(bSettings ? SW_SHOW : SW_HIDE);
	if (m_tooltipTreeOptions.GetSafeHwnd() != NULL)
		m_tooltipTreeOptions.Activate(bSettings);
	if (pEdit != NULL) {
		pEdit->ShowWindow(bSettings ? SW_HIDE : SW_SHOW);
		pEdit->SendMessage(EM_SETREADONLY, bRules ? FALSE : TRUE);
		SetRulesEditorText(bRules ? m_strRulesText : GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_HELP_TEXT")));
	}
	if (pValidate != NULL)
		pValidate->ShowWindow(bRules ? SW_SHOW : SW_HIDE);
	if (pReload != NULL)
		pReload->ShowWindow(bRules ? SW_SHOW : SW_HIDE);
	UpdateLayout();
}

void CPPgDownloadValidator::CaptureRulesText()
{
	if (m_eViewMode == ViewRules && GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX) != NULL)
		GetDlgItemText(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX, m_strRulesText);
}

void CPPgDownloadValidator::SetRulesEditorText(const CString& strText)
{
	m_bUpdatingRulesText = true;
	SetDlgItemText(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX, strText);
	m_bUpdatingRulesText = false;
}

CString CPPgDownloadValidator::GetRegexResultText(const CDownloadValidator::SRegexRulesResult& result, bool bSuccess) const
{
	CString strText;
	if (bSuccess) {
		strText.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_VALIDATION_SUCCESS")), result.uRuleCount);
		return strText;
	}

	switch (result.eError) {
		case CDownloadValidator::RegexRulesInvalidExpression:
			strText.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_INVALID_EXPRESSION")), result.uLineNumber);
			break;
		case CDownloadValidator::RegexRulesMissingCaptureGroup:
			strText.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_MISSING_CAPTURE")), result.uLineNumber);
			break;
		case CDownloadValidator::RegexRulesTextTooLarge:
			strText = GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_TEXT_TOO_LARGE"));
			break;
		case CDownloadValidator::RegexRulesFileWriteError:
			strText = GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_WRITE_FAILED"));
			break;
		default:
			strText = GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_READ_FAILED"));
			break;
	}
	return strText;
}

void CPPgDownloadValidator::SelectRegexErrorLine(UINT uLineNumber)
{
	if (uLineNumber == 0)
		return;
	CWnd* pEdit = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX);
	if (pEdit == NULL)
		return;
	const LRESULT iLineIndex = pEdit->SendMessage(EM_LINEINDEX, static_cast<WPARAM>(uLineNumber - 1), 0);
	if (iLineIndex < 0)
		return;
	const LRESULT iLineLength = pEdit->SendMessage(EM_LINELENGTH, static_cast<WPARAM>(iLineIndex), 0);
	const LRESULT iSelectionLength = iLineLength > 0 ? iLineLength : 0;
	pEdit->SendMessage(EM_SETSEL, static_cast<WPARAM>(iLineIndex), static_cast<LPARAM>(iLineIndex + iSelectionLength));
	pEdit->SetFocus();
}

bool CPPgDownloadValidator::ValidateRules(CDownloadValidator::SRegexRulesResult& result, bool bShowMessage)
{
	CaptureRulesText();
	const bool bValid = theApp.DownloadValidator != NULL && theApp.DownloadValidator->ValidateRegexRulesText(m_strRulesText, m_bDownloadValidatorCaseInsensitive, result);
	const CString strResult(GetRegexResultText(result, bValid));
	if (bShowMessage)
		CDarkMode::MessageBox(strResult, bValid ? MB_ICONINFORMATION : MB_ICONWARNING);
	return bValid;
}

void CPPgDownloadValidator::UpdateRulesTextBoxColors()
{
	CWnd* pEdit = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX);
	if (pEdit == NULL || pEdit->GetSafeHwnd() == NULL)
		return;

	pEdit->SendMessage(EM_SETBKGNDCOLOR, 0, GetCustomSysColor(COLOR_WINDOW));
	CHARFORMAT cf = {};
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_COLOR;
	cf.crTextColor = GetCustomSysColor(COLOR_WINDOWTEXT);
	pEdit->SendMessage(EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
	pEdit->SendMessage(EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
	pEdit->Invalidate();
}

void CPPgDownloadValidator::UpdateLayout()
{
	CWnd* pViewTabs = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_VIEW_TABS);
	CWnd* pTree = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_OPTIONS);
	CWnd* pEdit = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX);
	CWnd* pValidate = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_VALIDATE);
	CWnd* pReload = GetDlgItem(IDC_DOWNLOAD_VALIDATOR_REGEX_RELOAD);
	if (pViewTabs == NULL || pTree == NULL || pEdit == NULL || pValidate == NULL || pReload == NULL)
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	CRect rcDialogUnits(0, 0, 2, 2);
	MapDialogRect(&rcDialogUnits);
	const int iMargin = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.right));
	const int iGap = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.bottom));
	rcDialogUnits.SetRect(0, 0, 50, 14);
	MapDialogRect(&rcDialogUnits);
	const int iButtonWidth = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.right));
	rcDialogUnits.SetRect(0, 0, 0, 18);
	MapDialogRect(&rcDialogUnits);
	const int iTabHeight = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.bottom));
	pViewTabs->MoveWindow(iMargin, iMargin, max(1, rcClient.Width() - (iMargin * 2)), iTabHeight);

	const int iContentTop = iMargin + iTabHeight + iGap;
	rcDialogUnits.SetRect(0, 0, 0, 14);
	MapDialogRect(&rcDialogUnits);
	const int iActionHeight = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.bottom));
	const int iActionTop = rcClient.bottom - iMargin - iActionHeight;
	pReload->MoveWindow(rcClient.right - iMargin - iButtonWidth, iActionTop, iButtonWidth, iActionHeight);
	pValidate->MoveWindow(rcClient.right - iMargin - (iButtonWidth * 2) - iGap, iActionTop, iButtonWidth, iActionHeight);

	const int iTreeBottom = rcClient.bottom - iMargin;
	pTree->MoveWindow(iMargin, iContentTop, max(1, rcClient.Width() - (iMargin * 2)), max(1, iTreeBottom - iContentTop));

	const int iEditBottom = m_eViewMode == ViewRules ? iActionTop - iGap : rcClient.bottom - iMargin;
	pEdit->MoveWindow(iMargin, iContentTop, max(1, rcClient.Width() - (iMargin * 2)), max(1, iEditBottom - iContentTop));
}

void CPPgDownloadValidator::InitializeViewTabs()
{
	if (m_ctrlViewTabs.GetItemCount() == 0) {
		TCITEM tie;
		::ZeroMemory(&tie, sizeof(tie));
		tie.mask = TCIF_TEXT;
		tie.pszText = const_cast<LPTSTR>(_T(""));
		m_ctrlViewTabs.InsertItem(0, &tie);
		m_ctrlViewTabs.InsertItem(1, &tie);
		m_ctrlViewTabs.InsertItem(2, &tie);
	}
	m_ctrlViewTabs.SetCurSel(static_cast<int>(m_eViewMode));
	UpdateViewTabs();
}

void CPPgDownloadValidator::UpdateViewTabs()
{
	if (m_ctrlViewTabs.GetSafeHwnd() == NULL)
		return;
	TCITEM tie;
	::ZeroMemory(&tie, sizeof(tie));
	tie.mask = TCIF_TEXT;
	CString strText;
	strText = GetResString(_T("DOWNLOAD_VALIDATOR_VIEW_SETTINGS"));
	tie.pszText = const_cast<LPTSTR>((LPCTSTR)strText);
	m_ctrlViewTabs.SetItem(0, &tie);
	strText = GetResString(_T("DOWNLOAD_VALIDATOR_VIEW_RULES"));
	tie.pszText = const_cast<LPTSTR>((LPCTSTR)strText);
	m_ctrlViewTabs.SetItem(1, &tie);
	strText = GetResString(_T("EM_HELP"));
	tie.pszText = const_cast<LPTSTR>((LPCTSTR)strText);
	m_ctrlViewTabs.SetItem(2, &tie);
	m_ctrlViewTabs.SetCurSel(static_cast<int>(m_eViewMode));
}

CString CPPgDownloadValidator::FormatToolTipText(const CString& strText) const
{
	CString strNormalized(strText);
	strNormalized.Replace(_T("\r\n"), _T("\n"));
	strNormalized.Replace(_T('\r'), _T('\n'));

	CString strResult;
	int iParagraphStart = 0;
	const int iWrapColumn = 88;
	while (iParagraphStart <= strNormalized.GetLength()) {
		const int iParagraphEnd = strNormalized.Find(_T('\n'), iParagraphStart);
		CString strRemaining = iParagraphEnd >= 0
			? strNormalized.Mid(iParagraphStart, iParagraphEnd - iParagraphStart)
			: strNormalized.Mid(iParagraphStart);

		if (strRemaining.IsEmpty()) {
			if (!strResult.IsEmpty())
				strResult += _T("\r\n");
		} else {
			while (!strRemaining.IsEmpty()) {
				if (strRemaining.GetLength() <= iWrapColumn) {
					if (!strResult.IsEmpty())
						strResult += _T("\r\n");
					strResult += strRemaining;
					break;
				}

				int iBreakPos = strRemaining.Left(iWrapColumn + 1).ReverseFind(_T(' '));
				if (iBreakPos <= 0)
					iBreakPos = iWrapColumn;

				CString strWrappedLine = strRemaining.Left(iBreakPos);
				strWrappedLine.TrimRight();
				if (!strResult.IsEmpty())
					strResult += _T("\r\n");
				strResult += strWrappedLine;

				strRemaining = strRemaining.Mid(iBreakPos);
				strRemaining.TrimLeft();
			}
		}

		if (iParagraphEnd < 0)
			break;
		iParagraphStart = iParagraphEnd + 1;
	}

	strResult.TrimRight(_T("\r\n"));
	return strResult;
}

void CPPgDownloadValidator::UpdateTreeOptionsToolTip()
{
	if (m_tooltipTreeOptions.GetSafeHwnd() == NULL)
		return;

	CString strToolTip;
	if (m_eViewMode == ViewSettings && m_ctrlTreeOptions.GetSafeHwnd() != NULL && ::IsWindowVisible(m_ctrlTreeOptions.GetSafeHwnd())) {
		POINT ptScreen = {};
		if (::GetCursorPos(&ptScreen)) {
			CRect rcTree;
			m_ctrlTreeOptions.GetWindowRect(&rcTree);
			if (rcTree.PtInRect(ptScreen)) {
				CPoint ptClient(ptScreen);
				m_ctrlTreeOptions.ScreenToClient(&ptClient);
				UINT uFlags = 0;
				HTREEITEM hItem = m_ctrlTreeOptions.HitTest(ptClient, &uFlags);
				if (hItem != NULL && (uFlags & TVHT_ONITEM)) {
					CTreeOptionsItemData* pItemData = reinterpret_cast<CTreeOptionsItemData*>(m_ctrlTreeOptions.GetItemData(hItem));
					if (pItemData != NULL && !pItemData->m_sInfo.IsEmpty())
						strToolTip = pItemData->m_sInfo;
				}
			}
		}
	}

	strToolTip = FormatToolTipText(strToolTip);
	if (strToolTip != m_strTreeOptionsToolTip) {
		m_strTreeOptionsToolTip = strToolTip;
		if (!m_strTreeOptionsToolTip.IsEmpty())
			m_tooltipTreeOptions.UpdateTipText(m_strTreeOptionsToolTip, &m_ctrlTreeOptions);
	}
	m_tooltipTreeOptions.Activate(AreOptionsToolTipsEnabled(this) && !m_strTreeOptionsToolTip.IsEmpty() && m_eViewMode == ViewSettings);
}

void CPPgDownloadValidator::OnTcnSelchangeViewTabs(NMHDR*, LRESULT* pResult)
{
	if (pResult != NULL)
		*pResult = 0;
	const int iSelection = m_ctrlViewTabs.GetCurSel();
	if (iSelection == 1)
		SwitchView(ViewRules);
	else if (iSelection == 2)
		SwitchView(ViewHelp);
	else
		SwitchView(ViewSettings);
}

void CPPgDownloadValidator::OnValidateRules()
{
	CDownloadValidator::SRegexRulesResult result;
	if (!ValidateRules(result, true))
		SelectRegexErrorLine(result.uLineNumber);
}

void CPPgDownloadValidator::OnReloadRules()
{
	if (m_bRulesDirty && CDarkMode::MessageBox(GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_RELOAD_CONFIRM")), MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	CDownloadValidator::SRegexRulesResult result;
	CString strRulesText;
	if (theApp.DownloadValidator == NULL || !theApp.DownloadValidator->ReloadRegexRules(strRulesText, result)) {
		const CString strError(GetRegexResultText(result, false));
		CDarkMode::MessageBox(strError, MB_ICONWARNING);
		return;
	}

	m_strRulesText = strRulesText;
	m_bRulesDirty = false;
	SetRulesEditorText(m_strRulesText);
	if (!m_bSettingsDirty)
		SetModified(FALSE);
	if (thePrefs.GetDownloadValidatorRegexMatching())
		theApp.DownloadValidator->QueueReloadRegexMap();
}

void CPPgDownloadValidator::OnRulesChanged()
{
	if (m_bUpdatingRulesText || m_eViewMode != ViewRules)
		return;
	GetDlgItemText(IDC_DOWNLOAD_VALIDATOR_REGEX_TEXTBOX, m_strRulesText);
	m_bRulesDirty = true;
	SetModified();
}

LRESULT CPPgDownloadValidator::OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam)
{
	if (wParam == IDC_DOWNLOAD_VALIDATOR_OPTIONS) {
		TREEOPTSCTRLNOTIFY* pNotify = reinterpret_cast<TREEOPTSCTRLNOTIFY*>(lParam);
		if (pNotify != NULL && pNotify->hItem == m_htiDownloadValidatorDateTimeMatching)
			UpdateDownloadValidatorDateTimeUi();
		if (pNotify != NULL && pNotify->hItem == m_htiDownloadValidatorFuzzyMatching)
			UpdateDownloadValidatorFuzzyUi();
		if (pNotify != NULL && pNotify->hItem == m_htiDownloadValidatorMediaLengthMatching)
			UpdateDownloadValidatorMediaLengthUi();
		m_bSettingsDirty = true;
		SetModified();
	}
	return 0;
}

LRESULT CPPgDownloadValidator::DrawTreeItemHelp(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);
	return FALSE;
}

void CPPgDownloadValidator::OnSize(UINT nType, int cx, int cy)
{
	CPropertyPage::OnSize(nType, cx, cy);
	UpdateLayout();
}

void CPPgDownloadValidator::OnSysColorChange()
{
	CPropertyPage::OnSysColorChange();
	UpdateRulesTextBoxColors();
}

HBRUSH CPPgDownloadValidator::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	UNREFERENCED_PARAMETER(pDC);
	UNREFERENCED_PARAMETER(pWnd);
	UNREFERENCED_PARAMETER(nCtlColor);
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CPPgDownloadValidator::OnDestroy()
{
	if (m_tooltipTreeOptions.GetSafeHwnd() != NULL) {
		m_tooltipTreeOptions.DelTool(&m_ctrlTreeOptions);
		m_tooltipTreeOptions.CleanupWindow();
	}
	m_strTreeOptionsToolTip.Empty();
	m_ctrlTreeOptions.DeleteAllItems();
	m_bInitializedTreeOpts = false;
	m_htiDownloadValidator = NULL;
	m_htiDownloadValidatorPassive = NULL;
	m_htiDownloadValidatorAlwaysAsk = NULL;
	m_htiDownloadValidatorReject = NULL;
	m_htiDownloadValidatorAccept = NULL;
	m_htiDownloadValidatorAcceptPercentage = NULL;
	m_htiDownloadValidatorRejectCanceled = NULL;
	m_htiDownloadValidatorRejectSameHash = NULL;
	m_htiDownloadValidatorRejectBlacklisted = NULL;
	m_htiDownloadValidatorCaseInsensitive = NULL;
	m_htiDownloadValidatorIgnoreExtension = NULL;
	m_htiDownloadValidatorIgnoreTags = NULL;
	m_htiDownloadValidatorDontIgnoreNumericTags = NULL;
	m_htiDownloadValidatorIgnoreNonAlphaNumeric = NULL;
	m_htiDownloadValidatorCleanMojibake = NULL;
	m_htiDownloadValidatorMinimumComparisonLength = NULL;
	m_htiDownloadValidatorSkipIncompleteFileConfirmation = NULL;
	m_htiDownloadValidatorMarkAsBlacklisted = NULL;
	m_htiDownloadValidatorAutoMarkAsBlacklisted = NULL;
	m_htiDownloadValidatorDateTimeMatching = NULL;
	m_htiDownloadValidatorDateTimeUseYearRange = NULL;
	m_htiDownloadValidatorDateTimeYearStart = NULL;
	m_htiDownloadValidatorDateTimeYearEnd = NULL;
	m_htiDownloadValidatorDateTimeCheckSeconds = NULL;
	m_htiDownloadValidatorDateTimeIncludeFollowingNumericValues = NULL;
	m_htiDownloadValidatorRegexMatching = NULL;
	m_htiDownloadValidatorFuzzyMatching = NULL;
	m_htiDownloadValidatorFuzzySimilarityThreshold = NULL;
	m_htiDownloadValidatorFuzzyDisplayThreshold = NULL;
	m_htiDownloadValidatorFuzzyMinimumSharedTokens = NULL;
	m_htiDownloadValidatorFuzzyMinimumTokenCoverage = NULL;
	m_htiDownloadValidatorFuzzyMinimumLengthSimilarity = NULL;
	m_htiDownloadValidatorFuzzyMinimumEditSimilarity = NULL;
	m_htiDownloadValidatorFuzzyStructuralMinimumGroupLetters = NULL;
	m_htiDownloadValidatorFuzzyStructuralMinimumIDDigits = NULL;
	m_htiDownloadValidatorMediaLengthMatching = NULL;
	m_htiDownloadValidatorMediaLengthTolerance = NULL;
	CPropertyPage::OnDestroy();
}


BOOL CPPgDownloadValidator::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltipTreeOptions.GetSafeHwnd() != NULL) {
		if (pMsg != NULL && (pMsg->message == WM_MOUSEMOVE || pMsg->message == WM_NCMOUSEMOVE || pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_RBUTTONDOWN || pMsg->message == WM_MOUSEWHEEL))
			UpdateTreeOptionsToolTip();
		if (AreOptionsToolTipsEnabled(this))
			m_tooltipTreeOptions.RelayEvent(pMsg);
	}
	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgDownloadValidator::OnHelp()
{
	SwitchView(ViewHelp);
}

BOOL CPPgDownloadValidator::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return wParam == ID_HELP ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgDownloadValidator::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}
