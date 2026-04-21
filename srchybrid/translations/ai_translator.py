# -*- coding: utf-8 -*-
"""
This file is part of eMule AI
Copyright (C)2026 eMule AI

eMule AI Translator Script
Updates the translations.map file using the cloud or a locally hosted API.
"""

import os
import sys
import json
import time
import errno
import urllib.request
import urllib.error
import subprocess
import re
import builtins
import unicodedata
import argparse
import shutil

try:
    import pycountry
except Exception:
    pycountry = None

# ==============================================================================
# DEBUG LOG SETTINGS
# ==============================================================================
LOG_AI_RESPONSES = True
LOG_FILE_PATH = os.path.splitext(os.path.abspath(__file__))[0] + ".log"
ORIGINAL_PRINT = builtins.print
ORIGINAL_INPUT = builtins.input
CONSOLE_LOGGING_ACTIVE = True
SCRIPT_START_TIME = None
SHARED_API_SYSTEM_PROMPT = (
    "You are a professional translation engine for software UI text. "
    "Follow the user's format requirements exactly. "
    "Return only final translation lines in the requested format. "
    "Never reveal internal reasoning. Never output analysis, self-corrections, bullets, Markdown, JSON wrappers, <|channel>thought, or <channel|> tags. "
    "Never leave ordinary English UI labels untranslated inside quotes, parentheses, or slash-separated mode names unless they are protected placeholders or strict all-uppercase acronyms. "
    "Start immediately with the final plain-text answer in the requested format."
)


def write_log_line(message=""):
    if not LOG_AI_RESPONSES:
        return

    try:
        with open(LOG_FILE_PATH, "a", encoding="utf-8", errors="ignore") as log_file:
            log_file.write(message)
            if not message.endswith("\n"):
                log_file.write("\n")
    except Exception as log_error:
        ORIGINAL_PRINT(f"Warning: Failed to write log: {log_error}")


def print(*args, **kwargs):
    ORIGINAL_PRINT(*args, **kwargs)

    if not CONSOLE_LOGGING_ACTIVE:
        return

    sep = kwargs.get("sep", " ")
    end = kwargs.get("end", "\n")
    message = sep.join(str(arg) for arg in args) + end
    write_log_line(message)


def input(prompt=""):
    if prompt:
        ORIGINAL_PRINT(prompt, end="")
        if CONSOLE_LOGGING_ACTIVE:
            write_log_line(prompt)
        value = ORIGINAL_INPUT("")
    else:
        value = ORIGINAL_INPUT("")

    if CONSOLE_LOGGING_ACTIVE:
        write_log_line(f"> {value}")

    return value


def format_elapsed_time(seconds_value):
    total_seconds = max(0, int(round(seconds_value)))
    hours, remainder = divmod(total_seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    if hours > 0:
        return f"{hours}h {minutes}m {seconds}s"
    if minutes > 0:
        return f"{minutes}m {seconds}s"
    return f"{seconds}s"


# ==============================================================================
# USER SETTINGS
# ==============================================================================
API_TYPE = "ask"  # Valid values: ask, local, cloud.

CLOUD_API_KEY = os.environ.get("CLOUD_API_KEY", "")
CLOUD_MODEL_NAME = os.environ.get("CLOUD_MODEL_NAME", "gemini-3.1-flash-lite-preview")
CLOUD_API_URL = "https://generativelanguage.googleapis.com/v1beta/models/{model_name}:generateContent?key={api_key}"
CLOUD_API_REQUEST_TIMEOUT_SEC = 120
CLOUD_API_MAX_RETRY_COUNT = 0  # 0 means retry indefinitely for transient Cloud API availability errors.
CLOUD_API_RETRY_DELAY_SEC = 30
CLOUD_API_MAX_LANGUAGES_PER_BATCH = 24

LOCAL_API_BASE_URL = os.environ.get(
    "LOCAL_API_BASE_URL", "http://192.168.10.11:1234/v1"
)
LOCAL_API_KEY = os.environ.get("LOCAL_API_KEY", "")
LOCAL_MODEL_NAME = os.environ.get("LOCAL_MODEL_NAME", "")
LOCAL_API_REQUEST_TIMEOUT_SEC = 120
LOCAL_API_MAX_COMPLETION_TOKENS = 4096
LOCAL_API_RETRY_DELAY_SEC = 3
LOCAL_API_MAX_LANGUAGES_PER_BATCH = 12
LOCAL_API_FAILED_LANGUAGE_MAX_LANGUAGES_PER_BATCH = 1
ATOMIC_WRITE_RETRY_COUNT = 5
ATOMIC_WRITE_RETRY_DELAY_SEC = 0.2

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRCHYBRID_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
MAP_FILE_PATH = os.path.join(SCRIPT_DIR, "translations.map")
DEFAULT_RC_FILE_PATH = os.path.join(SRCHYBRID_DIR, "emule.rc")
DEFAULT_TRANSLATIONS_DATA_HEADER_PATH = os.path.join(
    SCRIPT_DIR, "translations_data.gen.h"
)
DEFAULT_LANGUAGE_REGISTRY_HEADER_PATH = os.path.join(
    SCRIPT_DIR, "lang_registry.gen.h"
)
RESUME_POINT_FILE = os.path.join(SCRIPT_DIR, "ai_translator_resume.txt")

API_DELAY_SEC = 3.0
TRANSLATION_COMPLETION_PER_LANGUAGE_FIX_RETRIES = 3
TRANSLATION_CHUNK_RETRY_COUNT = 3
TRANSLATION_FAILED_LANGUAGE_RETRY_COUNT = 3
TRANSLATION_PHRASE_ONLY_FIX_MIN_FAILURES = 1

API_BACKEND_LOCAL = "local"
API_BACKEND_CLOUD = "cloud"
API_BACKEND_ASK = "ask"
BACKEND_SELECTION_PREVIOUS = "previous"
BACKEND_SELECTION_NEXT = "next"
BACKEND_SELECTION_CONFIRM = "confirm"
BACKEND_SELECTION_OPTIONS = (
    (API_BACKEND_LOCAL, "Local API"),
    (API_BACKEND_CLOUD, "Cloud API"),
)
INTERACTIVE_MENU_OPTION_ITEMS = (
    ("backend", "Backend", BACKEND_SELECTION_OPTIONS),
    ("loop", "Run In Loop", ((True, "On"), (False, "Off"))),
    ("stop_on_error", "Stop On First Error", ((False, "Off"), (True, "On"))),
)
INTERACTIVE_MENU_ACTION_CONFIRM = "confirm"
INTERACTIVE_MENU_ACTION_OPTION_PREVIOUS = "option_previous"
INTERACTIVE_MENU_ACTION_OPTION_NEXT = "option_next"
INTERACTIVE_MENU_ACTION_VALUE_PREVIOUS = "value_previous"
INTERACTIVE_MENU_ACTION_VALUE_NEXT = "value_next"
INTERACTIVE_MENU_ACTION_BACKSPACE = "backspace"
INTERACTIVE_MENU_ACTION_DIGIT = "digit"

MENU_OPTION_ITEMS = (
    ("1", "Find and complete missing translations only"),
    ("2", "Translate/update specific translation key(s)"),
    ("3", "Clean then translate specific translation key(s)"),
    ("4", "Clean then translate specific line number(s)"),
    ("5", "Clean then translate specific translation key(s) and language code(s)"),
    (
        "6",
        "Resume the last interrupted multi-item translation job with its saved parameters and progress",
    ),
    (
        "7",
        "Full mapping: Check and update all translations from the beginning (ignores resume file)",
    ),
    ("8", "Find and remove unused translation keys"),
    ("9", "Find missing translations (fast structural scan + JSON output)"),
    ("10", "Fix and normalize translations.map formatting"),
    ("11", "Import STRINGTABLE entries from an RC file into translations.map"),
    ("12", "Compile translations.map into generated C++ headers"),
    ("13", "Check translations.map consistency"),
    ("14", "Set or add a translation entry by KEY and language"),
    ("15", "Add a language code to every KEY block"),
    ("16", "Remove a translation KEY block"),
    ("17", "Clear all non-English translations for a KEY"),
    ("18", "Exit"),
)
MENU_OPTION_LABELS = dict(MENU_OPTION_ITEMS)
MENU_OPTION_ALIASES = {
    "missing-only": "1",
    "specific-keys": "2",
    "clean-specific-keys": "3",
    "clean-line-numbers": "4",
    "clean-specific-key-languages": "5",
    "resume-mapping": "6",
    "full-mapping": "7",
    "remove-unused-keys": "8",
    "find-missing-translations": "9",
    "fix-translations-map": "10",
    "import-rc-to-map": "11",
    "compile-map-to-headers": "12",
    "check-translations-map": "13",
    "set-translation-entry": "14",
    "add-language-to-map": "15",
    "remove-translation-key": "16",
    "clear-non-english-translations": "17",
    "exit": "18",
}
MAP_TOOLKIT_ALIAS_TO_ACTION = {
    "rc2map": "import-rc",
    "map2cpp": "compile",
    "check": "check",
    "fix": "fix",
    "sort": "fix",
    "add": "set",
    "addlang": "add-language",
    "remove": "remove-key",
    "clear_others": "clear-other-languages",
}
TRANSLATE_COMMAND_TO_MENU_CHOICE = {
    "missing-only": "1",
    "key-list": "2",
    "clean-key-list": "3",
    "clean-lines": "4",
    "clean-key-languages": "5",
    "resume": "6",
    "full": "7",
    "remove-unused-keys": "8",
    "find-missing": "9",
    "fix-map-format": "10",
}
CLI_BOOLEAN_TRUE_VALUES = {"1", "true", "yes", "y", "on"}
CLI_BOOLEAN_FALSE_VALUES = {"0", "false", "no", "n", "off"}
STOP_ON_FIRST_ERROR = False
RESUME_STATE_VERSION = 2
RESUME_PHASE_PREPARE_KEYS = "prepare_specific_keys"
RESUME_PHASE_TRANSLATION_ROUND = "translation_round"
RESUME_PHASE_LINE_NUMBERS = "line_numbers"
RESUME_PHASE_KEY_LANGUAGE_PAIRS = "key_language_pairs"


class OperationAbortError(RuntimeError):
    pass


def append_ai_log(title, prompt=None, response=None):
    if not LOG_AI_RESPONSES:
        return

    try:
        with open(LOG_FILE_PATH, "a", encoding="utf-8", errors="ignore") as log_file:
            log_file.write("=" * 120 + "\n")
            log_file.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {title}\n")
            if isinstance(prompt, str):
                log_file.write("\n--- PROMPT ---\n")
                log_file.write(prompt)
                if not prompt.endswith("\n"):
                    log_file.write("\n")
            if isinstance(response, str):
                log_file.write("\n--- RESPONSE ---\n")
                log_file.write(response)
                if not response.endswith("\n"):
                    log_file.write("\n")
            log_file.write("\n")
    except Exception as log_error:
        ORIGINAL_PRINT(f"Warning: Failed to write AI log: {log_error}")


PROTECTED_TOKENS = {
    "eMule",
    "Kad",
    "Windows",
    "Microsoft",
    "MaxMind",
    "GeoLite",
    "GeoLite City",
    "UNC",
}
TRANSLATABLE_ALL_UPPERCASE_TOKENS = {"OK"}

TOKEN_SCAN_PATTERN = re.compile(r"[A-Za-z][A-Za-z0-9+._-]*")
ASCII_LEADING_WORD_PATTERN = re.compile(r"[A-Za-z]+(?:['’-][A-Za-z]+)*")
NON_WORD_BOUNDARY_PATTERN = r"[A-Za-z0-9_]"
SUSPICIOUS_ENGLISH_LEADING_FRAGMENTS = (
    "Would you",
    "Could you",
    "Should you",
    "Can you",
    "Will you",
    "Do you",
    "Are you",
    "Please",
    "This",
    "Copy",
    "Directory",
    "Source",
    "Failed",
    "Do",
)
SUSPICIOUS_DYNAMIC_LEADING_STARTERS = {
    "are",
    "can",
    "copy",
    "could",
    "directory",
    "do",
    "failed",
    "please",
    "should",
    "source",
    "this",
    "will",
    "would",
}
ALLOWED_SHARED_LEADING_LABELS = {
    "Bonus",
    "Nick",
    "Ratio",
    "RELEASE",
    "Scenario",
    "Start",
}
ENGLISH_LEAK_STOPWORDS = {
    "a",
    "an",
    "and",
    "are",
    "as",
    "at",
    "be",
    "been",
    "being",
    "but",
    "by",
    "for",
    "from",
    "had",
    "has",
    "have",
    "if",
    "in",
    "into",
    "is",
    "it",
    "its",
    "of",
    "on",
    "or",
    "that",
    "the",
    "their",
    "them",
    "there",
    "these",
    "this",
    "those",
    "to",
    "was",
    "were",
    "will",
    "with",
}
GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS = ENGLISH_LEAK_STOPWORDS | {
    "based",
    "rename",
    "renamed",
    "renaming",
    "change",
    "changed",
    "changing",
    "according",
    "using",
    "used",
}
SOURCE_ENGLISH_WORD_PATTERN = re.compile(r"[A-Za-z]+(?:['-][A-Za-z]+)*")
QUOTED_UI_PHRASE_PATTERN = re.compile(
    r"['\"]([A-Za-z][A-Za-z0-9+._ -]*(?:/[A-Za-z][A-Za-z0-9+._ -]*)*)['\"]"
)
QUOTED_UI_CONTEXT_PATTERN = re.compile(
    r"['\"]([A-Za-z][A-Za-z0-9+._ -]*(?:/[A-Za-z][A-Za-z0-9+._ -]*)*)['\"]\s+([A-Za-z][A-Za-z0-9+._-]*)"
)
PARENTHESIZED_UI_PHRASE_PATTERN = re.compile(r"\(([A-Za-z][A-Za-z0-9+._ \'’-]{2,})\)")
COLON_EQUIVALENT_CHARS = ":：﹕︓፡៖"
_SOURCE_PHRASE_CANDIDATES_CACHE = {}
GENERIC_LANGUAGE_IDENTITY_RULE_TEMPLATE = (
    "16. LANGUAGE IDENTITY RULE FOR `{lang_code}`: The language code `{lang_code}` means `{language_name}`. "
    "Translate into natural `{language_name}` only. Do NOT substitute a neighboring, regional, or more common language, "
    "even if it uses a similar script, alphabet, or vocabulary."
)
MIN_EMBEDDED_ENGLISH_PHRASE_LEN = 2
MIN_EMBEDDED_ENGLISH_PHRASE_CHAR_LEN = 10
ESCAPE_ARTIFACT_PREFIXES = ("", "n", "r", "t")
TERMINAL_PUNCTUATION_CHARS = ".!?؟。！？።"
TERMINAL_PUNCTUATION_END_PATTERN = re.compile(r"[.!?؟。！？።]+\s*$")
ESCAPED_LINE_SPLIT_PATTERN = re.compile(r"(\\r\\n|\\n|\\r|\r\n|\n|\r)")
PROTECTED_TOKEN_LEADING_TRIM_CHARS = "\"'([{"
PROTECTED_TOKEN_TRAILING_TRIM_CHARS = "\"')]}:;,.!?؟。！？።"
EXACT_KNOWN_PHRASE_MIN_WORDS = 2
PROMPT_SOURCE_PHRASE_MIN_WORDS = 2
PROMPT_SOURCE_PHRASE_MAX_COUNT = 8
ENGLISH_LEAK_WORD_MIN_LEN = 6
LOCAL_FORBIDDEN_SOURCE_WORD_MIN_LEN = 6
REPETITIVE_TOKEN_FLOOD_THRESHOLD = 8
LANGUAGE_CODE_PATTERN = re.compile(r"[A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*")
MAP_TOOL_LANGUAGE_CODE_PATTERN = re.compile(r"[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*")
PLACEHOLDER_TOKEN_PATTERN = re.compile(
    r"%%|%(?:\d+\$)?[-+#0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|I64|I32|w|z|j|t|L)?[diuoxXfFeEgGaAcCsSpn]"
)
COMPILER_PERCENT_FLAG_CHARS = set("#0- +'IhlL0123456789.*")
BRACKET_PAIRS = {
    "(": ")",
    "[": "]",
    "{": "}",
    "<": ">",
    "⟨": "⟩",
    "⟪": "⟫",
    "（": "）",
    "［": "］",
    "｛": "｝",
    "〔": "〕",
    "【": "】",
    "〈": "〉",
    "《": "》",
    "「": "」",
    "『": "』",
    "｢": "｣",
}
QUOTE_CHARS = {
    '"',
    "'",
    "`",
    "´",
    "«",
    "»",
    "‹",
    "›",
    "“",
    "”",
    "„",
    "‟",
    "‘",
    "’",
    "‚",
    "「",
    "」",
    "『",
    "』",
    "｢",
    "｣",
}


def extract_placeholder_tokens(text):
    if not isinstance(text, str) or not text:
        return []
    return [match.group(0) for match in PLACEHOLDER_TOKEN_PATTERN.finditer(text)]


def is_quote_char(ch):
    if ch in QUOTE_CHARS:
        return True
    category = unicodedata.category(ch)
    return category in {"Pi", "Pf"}


def is_direct_placeholder_wrapper_pair(left_char, right_char):
    expected_right = BRACKET_PAIRS.get(left_char)
    if expected_right is not None:
        return right_char == expected_right
    if is_quote_char(left_char) and is_quote_char(right_char):
        return True
    return False


def extract_placeholder_format_specs(text):
    if not isinstance(text, str) or not text:
        return []

    specs = []
    for match in PLACEHOLDER_TOKEN_PATTERN.finditer(text):
        left_index = match.start() - 1
        right_index = match.end()
        wrapper_pairs = []
        while left_index >= 0 and right_index < len(text):
            left_char = text[left_index]
            right_char = text[right_index]
            if not is_direct_placeholder_wrapper_pair(left_char, right_char):
                break
            wrapper_pairs.append((left_char, right_char))
            left_index -= 1
            right_index += 1

        prefix = "".join(left for left, _ in reversed(wrapper_pairs))
        suffix = "".join(right for _, right in wrapper_pairs)
        wrapper_depth = len(wrapper_pairs)
        specs.append(
            {
                "token": match.group(0),
                "prefix": prefix,
                "suffix": suffix,
                "prefix_start": match.start() - wrapper_depth,
                "token_start": match.start(),
                "token_end": match.end(),
                "suffix_end": match.end() + wrapper_depth,
            }
        )
    return specs


def source_has_placeholder_format_constraints(en_text):
    return bool(extract_placeholder_tokens(en_text))


def build_placeholder_format_examples(en_text):
    examples = []
    seen = set()
    for spec in extract_placeholder_format_specs(en_text):
        formatted = f"{spec['prefix']}{spec['token']}{spec['suffix']}"
        if formatted in seen:
            continue
        examples.append(formatted)
        seen.add(formatted)
    return examples


def build_placeholder_format_rule(en_text, rule_number="15"):
    placeholder_examples = build_placeholder_format_examples(en_text)
    if not placeholder_examples:
        return ""
    placeholder_examples_block = ", ".join(
        f"`{example}`" for example in placeholder_examples
    )
    return (
        f"{rule_number}. CRITICAL PLACEHOLDER FORMAT RULE: Preserve the exact source formatting around each placeholder occurrence independently. "
        f"If the English source uses different surrounding characters for different placeholders, keep the same surrounding characters on the corresponding placeholder occurrence in the translation. "
        f"Source placeholder examples for this string: {placeholder_examples_block}. "
        f"Do NOT add, remove, or change the direct balanced wrapper characters around placeholders. "
        f"Do NOT touch grammatical apostrophes, suffix markers, or nearby punctuation that are not directly enclosing the placeholder.\n"
    )


def visualize_compiler_percent_token(token):
    if not isinstance(token, str):
        return ""

    visible_parts = []
    for ch in token:
        if ch == " ":
            visible_parts.append("␠")
        elif ch == "\t":
            visible_parts.append("\\t")
        elif ch == "\r":
            visible_parts.append("\\r")
        elif ch == "\n":
            visible_parts.append("\\n")
        else:
            visible_parts.append(ch)
    return "".join(visible_parts)


def collect_compiler_percent_tokens(text):
    if not isinstance(text, str) or not text:
        return [], ""

    tokens = [
        match.group(0)
        for match in PLACEHOLDER_TOKEN_PATTERN.finditer(text)
        if match.group(0) != "%%"
    ]
    return tokens, ""


def is_printf_placeholder_token(token):
    return bool(
        isinstance(token, str)
        and token != "%%"
        and PLACEHOLDER_TOKEN_PATTERN.fullmatch(token)
    )


def build_literal_percent_format_rule(en_text, rule_number="17"):
    source_tokens, source_error = collect_compiler_percent_tokens(en_text)
    if source_error:
        return ""

    literal_tokens = []
    seen_tokens = set()
    for token in source_tokens:
        if is_printf_placeholder_token(token):
            continue
        visible_token = visualize_compiler_percent_token(token)
        if visible_token in seen_tokens:
            continue
        literal_tokens.append(visible_token)
        seen_tokens.add(visible_token)

    if not literal_tokens:
        return ""

    token_examples = ", ".join(f"`{token}`" for token in literal_tokens)
    return (
        f"{rule_number}. CRITICAL LITERAL PERCENT RULE: The translations.map tool reads every single literal `%` together with the immediately following raw boundary character from the English source. "
        f"Preserve that raw boundary pattern exactly for every literal percent occurrence. Source literal-percent token example(s) for this string: {token_examples}. "
        "If the source literal percent is followed by a space, keep translated wording continuing after the percent sign. Do NOT move sentence-ending punctuation or string end directly next to `%` unless the English source does so too.\n"
    )


def build_compiler_percent_fix_requirements(en_text, translated_text):
    source_tokens, source_error = collect_compiler_percent_tokens(en_text)
    if source_error:
        return ""

    target_tokens, target_error = collect_compiler_percent_tokens(translated_text)
    source_token_examples = ", ".join(
        f"`{visualize_compiler_percent_token(token)}`" for token in source_tokens
    )

    if target_error:
        return (
            "17. CRITICAL PERCENT TOKEN FIX: The current translation leaves a compiler-invalid percent token. "
            f"{target_error}. Match the English source percent-token sequence exactly. "
            f"Source percent token sequence for this string: {source_token_examples or '`(none)`'}.\n"
        )

    if source_tokens != target_tokens:
        target_token_examples = ", ".join(
            f"`{visualize_compiler_percent_token(token)}`" for token in target_tokens
        )
        return (
            "17. CRITICAL PERCENT TOKEN FIX: The current translation changed the raw percent-token pattern that the translations.map tool derives from the English source. "
            f"Expected source token sequence: {source_token_examples or '`(none)`'}. "
            f"Current translation token sequence: {target_token_examples or '`(none)`'}. "
            "Rewrite the translation so each single `%` keeps the same immediate raw boundary pattern as the English source.\n"
        )

    return ""


def normalize_placeholder_quote_style(en_text, translated_text):
    if not source_has_placeholder_format_constraints(en_text):
        return translated_text
    if not isinstance(translated_text, str) or not translated_text:
        return translated_text

    source_specs = extract_placeholder_format_specs(en_text)
    translated_specs = extract_placeholder_format_specs(translated_text)
    if len(source_specs) != len(translated_specs):
        return translated_text

    normalized_parts = []
    last_index = 0
    for source_spec, translated_spec in zip(source_specs, translated_specs):
        seg_start = translated_spec["prefix_start"]
        seg_end = translated_spec["suffix_end"]
        normalized_parts.append(translated_text[last_index:seg_start])
        normalized_parts.append(
            f"{source_spec['prefix']}{translated_spec['token']}{source_spec['suffix']}"
        )
        last_index = seg_end

    normalized_parts.append(translated_text[last_index:])
    return "".join(normalized_parts)


def validate_placeholder_sequence(en_text, translated_text):
    source_tokens = extract_placeholder_tokens(en_text)
    translated_tokens = extract_placeholder_tokens(translated_text)
    return source_tokens == translated_tokens, source_tokens, translated_tokens


def validate_placeholder_quote_style(en_text, translated_text):
    source_specs = extract_placeholder_format_specs(en_text)
    translated_specs = extract_placeholder_format_specs(translated_text)
    if len(source_specs) != len(translated_specs):
        return False

    for source_spec, translated_spec in zip(source_specs, translated_specs):
        if (
            source_spec["prefix"] != translated_spec["prefix"]
            or source_spec["suffix"] != translated_spec["suffix"]
        ):
            return False

    return True


def validate_compiler_percent_token_alignment(en_text, translated_text):
    source_tokens, source_error = collect_compiler_percent_tokens(en_text)
    if source_error:
        return False, source_error

    target_tokens, target_error = collect_compiler_percent_tokens(translated_text)
    if target_error:
        return False, target_error

    if source_tokens != target_tokens:
        expected_tokens = [visualize_compiler_percent_token(token) for token in source_tokens]
        actual_tokens = [visualize_compiler_percent_token(token) for token in target_tokens]
        return (
            False,
            f"map percent-token mismatch (expected {expected_tokens}, got {actual_tokens})",
        )

    return True, ""


def looks_like_source_echo_line(text, en_text):
    if not isinstance(text, str):
        return False
    stripped = text.strip().strip(",")
    if not stripped:
        return False
    if stripped.startswith('"') and stripped.endswith('"'):
        try:
            stripped = json.loads(stripped)
        except Exception:
            stripped = stripped.strip('"')
    if stripped == en_text and is_invariant_translation_source(en_text):
        return False
    return stripped == en_text


def normalize_candidate_source_phrase(phrase):
    if not isinstance(phrase, str):
        return ""
    normalized = re.sub(r"\s+", " ", phrase.strip())
    normalized = normalized.strip(" \t\r\n-–—:;,.!?()[]{}\"'`´")
    return normalized.strip()


def is_invariant_translation_source(en_text):
    if not isinstance(en_text, str):
        return False

    visible_text = build_visible_prompt_text(en_text)
    if not visible_text or not visible_text.strip():
        return False

    analysis_text = PLACEHOLDER_TOKEN_PATTERN.sub(" ", visible_text)
    analysis_text = re.sub(r"__LOCKED_TERM_\d+__", " ", analysis_text)

    for protected_term in extract_protected_terms(en_text):
        normalized_term = normalize_candidate_source_phrase(protected_term)
        if (
            "/" in normalized_term
            and "\\" not in normalized_term
            and "." not in normalized_term
            and not any(ch.isdigit() for ch in normalized_term)
        ):
            segments = [segment for segment in normalized_term.split("/") if segment]
            if (
                len(segments) >= 2
                and all(re.fullmatch(r"[A-Za-z]+", segment) for segment in segments)
                and not all(should_protect_token(segment) for segment in segments)
            ):
                continue
        pattern = re.compile(
            rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(protected_term)}(?!{NON_WORD_BOUNDARY_PATTERN})"
        )
        analysis_text = pattern.sub(" ", analysis_text)

    for match in TOKEN_SCAN_PATTERN.finditer(analysis_text):
        token = normalize_protected_token(match.group(0))
        if not token:
            continue
        if should_protect_token(token):
            continue
        return False

    return True


def count_source_phrase_words(phrase):
    return len(
        SOURCE_ENGLISH_WORD_PATTERN.findall(normalize_candidate_source_phrase(phrase))
    )


def is_high_confidence_exact_phrase_candidate(phrase):
    normalized = normalize_candidate_source_phrase(phrase)
    if not normalized:
        return False
    if count_source_phrase_words(normalized) < EXACT_KNOWN_PHRASE_MIN_WORDS:
        return False
    return len(normalized) >= MIN_EMBEDDED_ENGLISH_PHRASE_CHAR_LEN


def should_include_source_phrase_candidate(phrase):
    normalized = normalize_candidate_source_phrase(phrase)
    if not normalized:
        return False
    if normalized in PROTECTED_TOKENS:
        return False
    if not re.search(r"[A-Za-z]", normalized):
        return False

    words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
    if len(words) < PROMPT_SOURCE_PHRASE_MIN_WORDS:
        return False

    lowered_words = [word.casefold() for word in words]
    is_short_title_case_ui_label = (
        len(words) == 2 and words[0][:1].isupper() and words[1].islower()
    )
    if all(word in ENGLISH_LEAK_STOPWORDS for word in lowered_words):
        return False
    if lowered_words and (
        lowered_words[0] in GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS
        or lowered_words[-1] in GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS
    ) and not is_short_title_case_ui_label:
        return False

    return len(normalized) >= MIN_EMBEDDED_ENGLISH_PHRASE_CHAR_LEN


def should_include_quoted_ui_phrase_candidate(phrase):
    normalized = normalize_candidate_source_phrase(phrase)
    if not normalized:
        return False
    if normalized in PROTECTED_TOKENS:
        return False
    if not re.search(r"[A-Za-z]", normalized):
        return False

    words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
    if len(words) >= 2:
        lowered_words = [word.casefold() for word in words]
        if all(word in ENGLISH_LEAK_STOPWORDS for word in lowered_words):
            return False
        return len(normalized) >= 6

    if len(words) == 1:
        lowered_word = words[0].casefold()
        if lowered_word in ENGLISH_LEAK_STOPWORDS:
            return False
        return len(normalized) >= 4 and (words[0][0].isupper() or "/" in normalized)

    return "/" in normalized and len(normalized) >= 4


def extract_quoted_ui_context_phrases(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    phrases = []
    seen = set()

    for match in QUOTED_UI_CONTEXT_PATTERN.finditer(visible_text):
        phrase = f"{match.group(1)} {match.group(2)}"
        normalized = normalize_candidate_source_phrase(phrase)
        if not normalized:
            continue
        if not should_include_source_phrase_candidate(
            normalized
        ) and not should_include_quoted_ui_phrase_candidate(normalized):
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue
        phrases.append(normalized)
        seen.add(normalized_cf)

    return phrases


def extract_repeated_short_ui_label_phrases(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    candidate_map = {}
    for match in re.finditer(
        r"\b([A-Z][A-Za-z0-9+._-]*\s+[a-z][A-Za-z0-9+._-]*)\b", visible_text
    ):
        normalized = normalize_candidate_source_phrase(match.group(1))
        if not normalized:
            continue
        if not should_include_quoted_ui_phrase_candidate(normalized):
            continue
        candidate_map.setdefault(normalized.casefold(), normalized)

    repeated = []
    for normalized_cf, normalized in candidate_map.items():
        pattern = re.compile(
            rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(normalized)}(?!{NON_WORD_BOUNDARY_PATTERN})",
            re.IGNORECASE,
        )
        if len(pattern.findall(visible_text)) >= 2:
            repeated.append(normalized)

    return sorted(repeated, key=lambda item: (-len(item), item.casefold()))


def extract_plain_mode_label_phrases(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    phrases = []
    seen = set()

    for match in re.finditer(r"\b([A-Za-z][A-Za-z0-9+_-]*)\s+mode\b", visible_text):
        normalized = normalize_candidate_source_phrase(match.group(1) + " mode")
        if not normalized or not should_include_source_phrase_candidate(normalized):
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue
        phrases.append(normalized)
        seen.add(normalized_cf)

    for match in re.finditer(
        r"\b([A-Za-z][A-Za-z0-9+_-]*\s+[A-Za-z][A-Za-z0-9+_-]*)\s+mode\b", visible_text
    ):
        normalized = normalize_candidate_source_phrase(match.group(1) + " mode")
        if not normalized or not should_include_source_phrase_candidate(normalized):
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue
        phrases.append(normalized)
        seen.add(normalized_cf)

    return phrases


def extract_title_case_ui_phrases(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    phrases = []
    seen = set()

    for match in re.finditer(
        r"\b([A-Z][A-Za-z0-9+._-]*(?:\s+[A-Z][A-Za-z0-9+._-]*){1,2})\b", visible_text
    ):
        normalized = normalize_candidate_source_phrase(match.group(1))
        if not normalized or not should_include_source_phrase_candidate(normalized):
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue
        phrases.append(normalized)
        seen.add(normalized_cf)

    return phrases


def prune_redundant_source_fragments(candidates):
    ordered = list(dict.fromkeys(candidates))
    ordered.sort(
        key=lambda item: (-count_source_phrase_words(item), -len(item), item.casefold())
    )
    pruned = []
    for candidate in ordered:
        candidate_word_count = count_source_phrase_words(candidate)
        if candidate_word_count > 2 and any(
            phrase_is_contained_with_boundaries(candidate, existing)
            for existing in pruned
        ):
            continue
        pruned.append(candidate)
    return pruned


def is_low_value_source_phrase_fragment(phrase):
    normalized = normalize_candidate_source_phrase(phrase)
    if not normalized:
        return True

    words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
    if len(words) < 2:
        return False

    lowered_words = [word.casefold() for word in words]
    if lowered_words[-1] in {
        "does",
        "do",
        "did",
        "is",
        "are",
        "was",
        "were",
        "be",
        "been",
        "being",
        "not",
        "apply",
        "applies",
    }:
        return True
    if lowered_words[0] in {
        "if",
        "when",
        "while",
        "this",
        "that",
        "these",
        "those",
        "each",
    }:
        return True
    if (
        lowered_words[0] in GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS
        and lowered_words[-1] in GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS
    ):
        return True
    return False


def get_prompt_source_phrase_priority(candidate):
    normalized = normalize_candidate_source_phrase(candidate)
    if not normalized:
        return -9999
    words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
    lowered_words = [w.casefold() for w in words]
    word_count = len(words)
    score = 0
    if normalized.casefold().endswith(" mode"):
        score += 100
    if all(word[:1].isupper() for word in words if word):
        score += 70
    if word_count == 2:
        score += 25
    elif word_count == 3:
        score += 15
    elif word_count >= 5:
        score -= 20
    if lowered_words and (
        lowered_words[0] in GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS
        or lowered_words[-1] in GENERIC_SOURCE_PHRASE_EDGE_STOPWORDS
    ):
        score -= 25
    if lowered_words and lowered_words[0] in {"while", "when", "each", "only"}:
        score -= 20
    if "." in normalized or "_" in normalized:
        score -= 100
    return score


def extract_parenthesized_ui_phrases(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    phrases = []
    seen = set()

    for match in PARENTHESIZED_UI_PHRASE_PATTERN.finditer(visible_text):
        normalized = normalize_candidate_source_phrase(match.group(1))
        if not normalized:
            continue
        if not should_include_quoted_ui_phrase_candidate(normalized):
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue
        phrases.append(normalized)
        seen.add(normalized_cf)

    return phrases


def phrase_is_contained_with_boundaries(candidate, existing):
    if (
        not isinstance(candidate, str)
        or not isinstance(existing, str)
        or not candidate
        or not existing
    ):
        return False
    pattern = re.compile(
        rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(candidate)}(?!{NON_WORD_BOUNDARY_PATTERN})",
        re.IGNORECASE,
    )
    return bool(pattern.search(existing))


def prune_contained_source_phrases(candidates):
    pruned = []
    seen = set()
    for candidate in sorted(
        set(candidates), key=lambda item: (-len(item), item.casefold())
    ):
        candidate_cf = candidate.casefold()
        if candidate_cf in seen:
            continue
        candidate_word_count = count_source_phrase_words(candidate)
        if candidate_word_count > 2 and any(
            phrase_is_contained_with_boundaries(candidate, existing)
            for existing in pruned
        ):
            continue
        pruned.append(candidate)
        seen.add(candidate_cf)
    return pruned


def extract_leading_ui_label_phrase(en_text):
    if not isinstance(en_text, str) or not en_text:
        return ""

    visible_text = build_visible_prompt_text(en_text)
    leading_label_match = re.match(
        r"^\s*([A-Za-z][A-Za-z0-9+._-]*(?:[ 	]+[A-Za-z][A-Za-z0-9+._-]*){1,5})\s*:",
        visible_text,
    )
    if not leading_label_match:
        return ""

    normalized = normalize_candidate_source_phrase(leading_label_match.group(1))
    if not should_include_source_phrase_candidate(normalized):
        return ""
    return normalized


def build_exact_memory_phrase_candidates(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    visible_text_cf = visible_text.casefold()
    normalized_source = normalize_candidate_source_phrase(visible_text)
    candidate_map = {}

    def add_candidate(candidate):
        normalized = normalize_candidate_source_phrase(candidate)
        if not is_high_confidence_exact_phrase_candidate(normalized):
            return
        if is_low_value_source_phrase_fragment(normalized):
            return
        if normalized.casefold() == normalized_source.casefold():
            return
        candidate_map.setdefault(normalized.casefold(), normalized)

    leading_label = extract_leading_ui_label_phrase(en_text)
    if leading_label:
        add_candidate(leading_label)

    try:
        memory = load_translation_memory_by_english()
        for memory_source_text in memory.keys():
            if not isinstance(memory_source_text, str):
                continue
            normalized_memory_source = normalize_candidate_source_phrase(
                memory_source_text
            )
            if not is_high_confidence_exact_phrase_candidate(normalized_memory_source):
                continue
            if normalized_memory_source.casefold() not in visible_text_cf:
                continue
            add_candidate(normalized_memory_source)
    except Exception:
        pass

    return sorted(
        candidate_map.values(), key=lambda item: (-len(item), item.casefold())
    )


def build_dynamic_source_phrase_candidates(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    cached = _SOURCE_PHRASE_CANDIDATES_CACHE.get(en_text)
    if cached is not None:
        return cached

    visible_text = build_visible_prompt_text(en_text)
    normalized_source = normalize_candidate_source_phrase(visible_text)
    candidate_map = {}
    protected_terms_cf = {
        term.casefold() for term in extract_protected_terms(en_text)
    }

    def add_candidate(candidate, allow_short_quoted_ui_phrase=False):
        normalized = normalize_candidate_source_phrase(candidate)
        if not normalized:
            return
        if normalized.casefold() in protected_terms_cf:
            return
        if allow_short_quoted_ui_phrase:
            if not should_include_quoted_ui_phrase_candidate(normalized):
                return
        else:
            if not should_include_source_phrase_candidate(normalized):
                return
        if is_low_value_source_phrase_fragment(normalized):
            return
        if normalized.casefold() == normalized_source.casefold():
            return
        candidate_map.setdefault(normalized.casefold(), normalized)

    leading_label = extract_leading_ui_label_phrase(en_text)
    if leading_label:
        add_candidate(leading_label)

    for phrase in extract_repeated_short_ui_label_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for phrase in extract_plain_mode_label_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for phrase in extract_title_case_ui_phrases(en_text):
        add_candidate(phrase)

    for phrase in extract_parenthesized_ui_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for phrase in extract_quoted_ui_context_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for match in QUOTED_UI_PHRASE_PATTERN.finditer(visible_text):
        add_candidate(match.group(1), allow_short_quoted_ui_phrase=True)

    tokens = [
        (match.group(0), match.start(), match.end())
        for match in SOURCE_ENGLISH_WORD_PATTERN.finditer(visible_text)
    ]
    max_ngram = 3
    for start_idx in range(len(tokens)):
        for ngram_size in range(2, max_ngram + 1):
            end_idx = start_idx + ngram_size
            if end_idx > len(tokens):
                break
            is_contiguous_phrase = True
            for gap_idx in range(start_idx, end_idx - 1):
                gap_text = visible_text[tokens[gap_idx][2] : tokens[gap_idx + 1][1]]
                if not re.fullmatch(r"[\s/-]+", gap_text):
                    is_contiguous_phrase = False
                    break
            if not is_contiguous_phrase:
                break
            first_token = tokens[start_idx]
            last_token = tokens[end_idx - 1]
            phrase = visible_text[first_token[1] : last_token[2]]
            add_candidate(phrase)

    try:
        memory = load_translation_memory_by_english()
        visible_text_cf = visible_text.casefold()
        for memory_source_text in memory.keys():
            if not isinstance(memory_source_text, str):
                continue
            normalized_memory_source = normalize_candidate_source_phrase(
                memory_source_text
            )
            if not is_high_confidence_exact_phrase_candidate(normalized_memory_source):
                continue
            if normalized_memory_source.casefold() == normalized_source.casefold():
                continue
            if normalized_memory_source.casefold() not in visible_text_cf:
                continue
            if count_source_phrase_words(normalized_memory_source) > 4:
                continue
            add_candidate(normalized_memory_source)
    except Exception:
        pass

    candidates = prune_redundant_source_fragments(
        prune_contained_source_phrases(candidate_map.values())
    )
    _SOURCE_PHRASE_CANDIDATES_CACHE[en_text] = candidates
    return candidates


def get_source_words_covered_by_leading_ui_label(en_text):
    covered = set()
    leading_label = extract_leading_ui_label_phrase(en_text)
    if not leading_label:
        return covered

    for word in SOURCE_ENGLISH_WORD_PATTERN.findall(leading_label):
        normalized = normalize_candidate_source_phrase(word)
        if not normalized:
            continue
        covered.add(normalized.casefold())
    return covered


def build_dynamic_source_leak_word_candidates(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    candidate_map = {}
    for word in SOURCE_ENGLISH_WORD_PATTERN.findall(visible_text):
        normalized = normalize_candidate_source_phrase(word)
        if not normalized:
            continue
        lowered = normalized.casefold()
        if len(normalized) < ENGLISH_LEAK_WORD_MIN_LEN:
            continue
        if lowered in ENGLISH_LEAK_STOPWORDS:
            continue
        if should_protect_token(normalized):
            continue
        candidate_map.setdefault(lowered, normalized)

    return sorted(
        candidate_map.values(), key=lambda item: (-len(item), item.casefold())
    )


def build_local_forbidden_source_word_candidates(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    visible_text = build_visible_prompt_text(en_text)
    candidate_map = {}
    for word in SOURCE_ENGLISH_WORD_PATTERN.findall(visible_text):
        normalized = normalize_candidate_source_phrase(word)
        if not normalized:
            continue
        lowered = normalized.casefold()
        if len(normalized) < LOCAL_FORBIDDEN_SOURCE_WORD_MIN_LEN:
            continue
        if lowered in ENGLISH_LEAK_STOPWORDS:
            continue
        if should_protect_token(normalized):
            continue
        candidate_map.setdefault(lowered, normalized)

    return sorted(
        candidate_map.values(), key=lambda item: (-len(item), item.casefold())
    )


def iter_unprotected_source_word_matches(en_text):
    if not isinstance(en_text, str) or not en_text:
        return

    visible_text = build_visible_prompt_text(en_text)
    protected_terms_cf = {
        term.casefold() for term in extract_protected_terms(en_text)
    }
    for match in SOURCE_ENGLISH_WORD_PATTERN.finditer(visible_text):
        normalized = normalize_candidate_source_phrase(match.group(0))
        if not normalized:
            continue
        if normalized.casefold() in protected_terms_cf:
            continue
        yield match


def iter_embedded_english_phrase_candidates(en_text):
    seen = set()

    for candidate in build_authoritative_batch_phrase_candidates(en_text):
        normalized = normalize_candidate_source_phrase(candidate)
        if not normalized:
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue
        seen.add(normalized_cf)
        yield normalized

    for candidate in build_dynamic_source_phrase_candidates(en_text):
        normalized = normalize_candidate_source_phrase(candidate)
        if not normalized:
            continue
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            continue

        words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
        if len(words) == 2 and all(word.islower() for word in words):
            continue

        seen.add(normalized_cf)
        yield normalized


def is_prompt_friendly_source_phrase_candidate(phrase):
    normalized = normalize_candidate_source_phrase(phrase)
    if not should_include_source_phrase_candidate(normalized):
        return False

    words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
    if len(words) < 2:
        return False

    lowered_words = [word.casefold() for word in words]
    if lowered_words[0] in {"when", "while", "where", "which"}:
        return False
    if lowered_words[-1] in {"most", "least", "only", "each", "every"}:
        return False
    return True


def build_prompt_source_phrase_candidates(en_text):
    prioritized = []
    seen = set()
    protected_terms_cf = {
        term.casefold() for term in extract_protected_terms(en_text)
    }
    repeated_short_ui_labels_cf = {
        phrase.casefold() for phrase in extract_repeated_short_ui_label_phrases(en_text)
    }
    exact_memory_phrase_cf = {
        phrase.casefold() for phrase in build_exact_memory_phrase_candidates(en_text)
    }

    def add_candidate(candidate, allow_short_quoted_ui_phrase=False):
        normalized = normalize_candidate_source_phrase(candidate)
        if not normalized:
            return
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            return
        if normalized_cf in protected_terms_cf:
            return
        normalized_words = SOURCE_ENGLISH_WORD_PATTERN.findall(normalized)
        if (
            normalized_cf in exact_memory_phrase_cf
            and len(normalized_words) == 2
            and all(word[:1].isupper() for word in normalized_words if word)
        ):
            return
        if (
            not allow_short_quoted_ui_phrase
            and len(normalized_words) == 2
            and len(normalized) < 12
            and normalized_cf not in repeated_short_ui_labels_cf
            and not normalized_cf.endswith(" mode")
        ):
            return
        if allow_short_quoted_ui_phrase:
            if not should_include_quoted_ui_phrase_candidate(normalized):
                return
        else:
            if not is_prompt_friendly_source_phrase_candidate(normalized):
                return
        if is_low_value_source_phrase_fragment(normalized):
            return
        prioritized.append(normalized)
        seen.add(normalized_cf)

    visible_text = build_visible_prompt_text(en_text)

    for phrase in extract_repeated_short_ui_label_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for phrase in extract_plain_mode_label_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for phrase in extract_title_case_ui_phrases(en_text):
        if count_source_phrase_words(phrase) == 2:
            continue
        add_candidate(phrase)

    for phrase in extract_parenthesized_ui_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for phrase in extract_quoted_ui_context_phrases(en_text):
        add_candidate(phrase, allow_short_quoted_ui_phrase=True)

    for match in QUOTED_UI_PHRASE_PATTERN.finditer(visible_text):
        add_candidate(match.group(1), allow_short_quoted_ui_phrase=True)

    for phrase in build_exact_memory_phrase_candidates(en_text):
        add_candidate(phrase)

    for phrase in build_dynamic_source_phrase_candidates(en_text):
        add_candidate(phrase)

    prioritized = prune_redundant_source_fragments(prioritized)
    prioritized.sort(
        key=lambda item: (
            -(
                get_prompt_source_phrase_priority(item)
                + (60 if item.casefold() in repeated_short_ui_labels_cf else 0)
            ),
            count_source_phrase_words(item),
            len(item),
            item.casefold(),
        )
    )
    return prioritized


def build_source_phrase_translation_requirements(en_text):
    phrases = [
        phrase
        for phrase in build_authoritative_batch_phrase_candidates(en_text)
        if count_source_phrase_words(phrase) >= 2
    ]
    if not phrases:
        return ""

    phrase_lines = "\n".join(
        f"- `{phrase}`" for phrase in phrases[:PROMPT_SOURCE_PHRASE_MAX_COUNT]
    )
    return (
        "14. CRITICAL: The following English UI phrase(s) from the source are ordinary translatable interface text for this key. In EVERY non-English target language, translate them naturally and completely. Do NOT copy them literally in English.\n"
        "This rule also applies when the phrase appears inside quotes, parentheses, slashes, or mode labels. Those are still ordinary UI words unless they are protected placeholders or strict all-uppercase acronyms.\n"
        f"Source UI phrase(s) that MUST be translated in non-English languages:\n{phrase_lines}\n"
    )


_TRANSLATION_MEMORY_BY_ENGLISH_CACHE = None
_INFERRED_SCRIPT_FAMILIES_CACHE = {}
_INFERRED_LOANWORD_CACHE = {}
_UI_PHRASE_TRANSLATION_CACHE = {}
LAST_BATCH_LANGUAGE_ERRORS = {}
LAST_BATCH_LANGUAGE_CANDIDATES = {}
LAST_BATCH_RESULT_STATUS = ""


def load_translation_memory_by_english():
    global _TRANSLATION_MEMORY_BY_ENGLISH_CACHE
    if _TRANSLATION_MEMORY_BY_ENGLISH_CACHE is not None:
        return _TRANSLATION_MEMORY_BY_ENGLISH_CACHE

    memory = {}
    try:
        for item in get_keys_from_map(MAP_FILE_PATH):
            langs = item.get("langs", {})
            en_text = langs.get("en", "")
            if not isinstance(en_text, str):
                continue
            en_text = en_text.strip()
            if not en_text:
                continue

            lang_map = memory.setdefault(en_text, {})
            for lang_code, translated_text in langs.items():
                if lang_code == "en" or not isinstance(translated_text, str):
                    continue
                translated_text = translated_text.strip()
                if translated_text:
                    lang_map[lang_code] = translated_text
    except Exception:
        memory = {}

    _TRANSLATION_MEMORY_BY_ENGLISH_CACHE = memory
    return _TRANSLATION_MEMORY_BY_ENGLISH_CACHE


def count_language_memory_examples(lang_code, limit=400):
    if not isinstance(lang_code, str) or not lang_code:
        return 0

    memory = load_translation_memory_by_english()
    count = 0
    for en_text, lang_map in memory.items():
        translated_text = lang_map.get(lang_code, "")
        if not isinstance(translated_text, str):
            continue
        translated_text = translated_text.strip()
        if not translated_text:
            continue
        if not is_memory_context_example_usable(lang_code, en_text, translated_text):
            continue
        count += 1
        if count >= limit:
            break
    return count


def get_inferred_untranslated_loanwords_for_lang(lang_code):
    if not isinstance(lang_code, str) or not lang_code:
        return set()

    cached = _INFERRED_LOANWORD_CACHE.get(lang_code)
    if cached is not None:
        return set(cached)

    allowed_scripts = build_allowed_script_families_for_lang(lang_code, "")
    if "Latin" not in allowed_scripts:
        _INFERRED_LOANWORD_CACHE[lang_code] = tuple()
        return set()

    memory = load_translation_memory_by_english()
    source_counts = {}
    carried_counts = {}

    for en_text, lang_map in memory.items():
        translated_text = lang_map.get(lang_code, "")
        if not isinstance(translated_text, str):
            continue
        translated_text = translated_text.strip()
        if not translated_text:
            continue
        if translated_text.casefold() == en_text.strip().casefold():
            continue
        if contains_response_artifact_lines(translated_text):
            continue
        if detect_structural_translation_artifact(translated_text):
            continue
        if detect_repetitive_token_flood(translated_text):
            continue
        if contains_embedded_english_phrase_leak(en_text, translated_text, lang_code):
            continue
        placeholder_pairs = build_protected_placeholders(
            en_text, {lang_code: translated_text}
        )
        if detect_unexpected_script_mixture(
            lang_code, translated_text, en_text, placeholder_pairs
        ):
            continue

        visible_translation = build_visible_prompt_text(translated_text)
        for match in iter_unprotected_source_word_matches(en_text):
            word = normalize_candidate_source_phrase(match.group(0))
            if not word:
                continue
            lowered = word.casefold()
            if len(word) < ENGLISH_LEAK_WORD_MIN_LEN:
                continue
            if lowered in ENGLISH_LEAK_STOPWORDS:
                continue
            if should_protect_token(word):
                continue

            source_counts[lowered] = source_counts.get(lowered, 0) + 1
            if source_word_leaks_into_translation(word, visible_translation):
                carried_counts[lowered] = carried_counts.get(lowered, 0) + 1

    inferred = set()
    for word, seen_count in source_counts.items():
        carried_count = carried_counts.get(word, 0)
        if carried_count < 1:
            continue
        if seen_count >= 3 and carried_count * 2 < seen_count:
            continue
        inferred.add(word)

    _INFERRED_LOANWORD_CACHE[lang_code] = tuple(sorted(inferred))
    return inferred


def is_dynamically_allowed_untranslated_loanword(lang_code, word):
    if not isinstance(lang_code, str) or not isinstance(word, str):
        return False

    normalized = normalize_candidate_source_phrase(word).casefold()
    if not normalized:
        return False
    return normalized in get_inferred_untranslated_loanwords_for_lang(lang_code)


def get_known_translation_from_memory(lang_code, source_text):
    if not isinstance(lang_code, str) or not isinstance(source_text, str):
        return ""

    memory = load_translation_memory_by_english()
    lang_map = memory.get(source_text.strip(), {})
    candidate = lang_map.get(lang_code, "")
    if not isinstance(candidate, str):
        return ""
    candidate = candidate.strip()
    if not candidate:
        return ""
    if candidate.casefold() == source_text.strip().casefold():
        return ""
    if source_text.strip().casefold() in candidate.casefold():
        return ""
    return candidate


def is_memory_context_example_usable(lang_code, source_text, translated_text):
    if not isinstance(source_text, str) or not isinstance(translated_text, str):
        return False

    stripped_source = source_text.strip()
    stripped_translation = translated_text.strip()
    if not stripped_source or not stripped_translation:
        return False
    if stripped_source.casefold() == stripped_translation.casefold():
        return False

    placeholder_pairs = build_protected_placeholders(
        stripped_source, {lang_code: stripped_translation}
    )
    cleaned_text = cleanup_translated_text(
        stripped_source, stripped_translation, placeholder_pairs, lang_code
    )

    if contains_response_artifact_lines(cleaned_text):
        return False
    if detect_structural_translation_artifact(cleaned_text):
        return False
    if detect_repetitive_token_flood(cleaned_text):
        return False
    if contains_embedded_english_phrase_leak(stripped_source, cleaned_text, lang_code):
        return False
    if detect_unexpected_script_mixture(
        lang_code, cleaned_text, stripped_source, placeholder_pairs
    ):
        return False

    return True


def get_memory_context_examples(lang_code, source_phrase, limit=2):
    if (
        not isinstance(lang_code, str)
        or not isinstance(source_phrase, str)
        or not source_phrase
    ):
        return []

    memory = load_translation_memory_by_english()
    examples = []
    seen = set()
    source_phrase_cf = source_phrase.casefold()

    for en_text, lang_map in memory.items():
        if en_text.strip().casefold() == source_phrase_cf:
            continue
        if source_phrase_cf not in en_text.casefold():
            continue

        translated_text = lang_map.get(lang_code, "")
        if not isinstance(translated_text, str):
            continue
        translated_text = translated_text.strip()
        if not translated_text:
            continue
        if translated_text.casefold() == en_text.casefold():
            continue
        if not is_memory_context_example_usable(lang_code, en_text, translated_text):
            continue

        pair = (en_text, translated_text)
        if pair in seen:
            continue
        seen.add(pair)
        examples.append(pair)
        if len(examples) >= limit:
            break

    return examples


def get_known_phrase_requirements(en_text, lang_code):
    if not isinstance(en_text, str) or not isinstance(lang_code, str):
        return []

    visible_source = build_visible_prompt_text(en_text)
    stripped_visible_source = visible_source.lstrip()
    requirements = []
    seen = set()

    for phrase in build_exact_memory_phrase_candidates(en_text):
        known_translation = get_known_translation_from_memory(lang_code, phrase)
        if not known_translation:
            continue

        signature = (phrase.casefold(), known_translation.casefold())
        if signature in seen:
            continue
        seen.add(signature)

        is_leading = stripped_visible_source.startswith(phrase)
        separator = ""
        if is_leading:
            remainder = stripped_visible_source[len(phrase) :]
            separator_match = re.match(r"^\s*([^\w\s])", remainder, re.UNICODE)
            separator = separator_match.group(1) if separator_match else ""

        requirements.append(
            {
                "source_phrase": phrase,
                "known_translation": known_translation,
                "is_leading": is_leading,
                "separator": separator,
            }
        )

    return requirements


def get_known_leading_phrase_requirement(en_text, lang_code):
    for requirement in get_known_phrase_requirements(en_text, lang_code):
        if requirement.get("is_leading"):
            return requirement
    return None


def get_missing_known_phrase_requirements(en_text, translated_text, lang_code):
    if (
        not isinstance(en_text, str)
        or not isinstance(translated_text, str)
        or not isinstance(lang_code, str)
        or not lang_code
    ):
        return []

    requirements = get_known_phrase_requirements(en_text, lang_code)
    if not requirements:
        return []

    visible_translation = build_visible_prompt_text(translated_text)
    stripped_visible_translation = visible_translation.lstrip()
    visible_translation_cf = visible_translation.casefold()
    missing = []

    for requirement in requirements:
        known_translation = requirement["known_translation"]
        if requirement.get("is_leading"):
            if not stripped_visible_translation.startswith(known_translation):
                missing.append(requirement)
                continue
            remainder = stripped_visible_translation[len(known_translation) :]
            separator = requirement.get("separator", "")
            if not has_equivalent_separator_after_text(separator, remainder):
                missing.append(requirement)
            continue

        if known_translation.casefold() not in visible_translation_cf:
            missing.append(requirement)

    return missing


def build_known_phrase_translation_requirements(en_text, prompt_lang_dict):
    if not isinstance(prompt_lang_dict, dict) or not prompt_lang_dict:
        return ""

    requirement_entries = []
    for lang_code in prompt_lang_dict.keys():
        language_name = get_language_name_for_code(lang_code) or lang_code
        for requirement in get_known_phrase_requirements(en_text, lang_code):
            requirement_entries.append(
                {
                    "lang_code": lang_code,
                    "language_name": language_name,
                    "source_phrase": requirement["source_phrase"],
                    "known_translation": requirement["known_translation"],
                    "is_leading": bool(requirement.get("is_leading")),
                    "separator": requirement.get("separator", ""),
                }
            )

    if not requirement_entries:
        return ""

    compact_rules = {}
    for entry in requirement_entries:
        lang_entry = compact_rules.setdefault(
            entry["lang_code"], {"language": entry["language_name"], "phrases": []}
        )
        lang_entry["phrases"].append(
            {
                "source_phrase": entry["source_phrase"],
                "translation": entry["known_translation"],
                "leading": entry["is_leading"],
                "separator": entry["separator"],
            }
        )

    return (
        "17. EXACT ESTABLISHED UI TERM RULES: Some UI terms already have trusted translations in translations.map. "
        "Use the exact translations from the JSON mapping below unchanged. For any entry where `leading` is true, keep that exact translated label at the start of the line and only translate the remainder of the sentence naturally after the colon or locale-equivalent colon. Do NOT invent variants.\n"
        "Exact established UI term mapping JSON:\n"
        f"{json.dumps(compact_rules, ensure_ascii=False, indent=2)}\n"
    )


def build_authoritative_batch_phrase_candidates(en_text):
    if not isinstance(en_text, str) or not en_text:
        return []

    candidates = []
    seen = set()
    repeated_short_phrases = extract_repeated_short_ui_label_phrases(en_text)
    plain_mode_phrases = extract_plain_mode_label_phrases(en_text)
    exact_memory_phrases = build_exact_memory_phrase_candidates(en_text)
    repeated_short_cf = {candidate.casefold() for candidate in repeated_short_phrases}
    plain_mode_cf = {candidate.casefold() for candidate in plain_mode_phrases}
    exact_memory_cf = {candidate.casefold() for candidate in exact_memory_phrases}

    def add_candidate(phrase, high_priority_ui_label=False):
        normalized = normalize_candidate_source_phrase(phrase)
        if not normalized:
            return
        normalized_cf = normalized.casefold()
        if normalized_cf in seen:
            return
        if not high_priority_ui_label and not should_include_source_phrase_candidate(
            normalized
        ):
            return
        if not high_priority_ui_label and is_low_value_source_phrase_fragment(
            normalized
        ):
            return

        word_count = count_source_phrase_words(normalized)
        if word_count < 1 or word_count > 4:
            return
        if word_count >= 4 and len(normalized) > 32:
            return

        candidates.append(normalized)
        seen.add(normalized_cf)

    for phrase in repeated_short_phrases:
        add_candidate(phrase, high_priority_ui_label=True)
    for phrase in plain_mode_phrases:
        add_candidate(phrase, high_priority_ui_label=True)
    for phrase in extract_parenthesized_ui_phrases(en_text):
        add_candidate(phrase, high_priority_ui_label=True)
    for phrase in extract_quoted_ui_context_phrases(en_text):
        add_candidate(phrase, high_priority_ui_label=True)
    for phrase in exact_memory_phrases:
        add_candidate(phrase, high_priority_ui_label=True)
    candidates = prune_redundant_source_fragments(candidates)
    candidates.sort(
        key=lambda item: (
            -(120 if item.casefold() in repeated_short_cf else 0)
            - (80 if item.casefold() in plain_mode_cf else 0)
            - (60 if item.casefold() in exact_memory_cf else 0),
            count_source_phrase_words(item),
            len(item),
            item.casefold(),
        )
    )
    return candidates[:8]


def build_authoritative_ui_phrase_map_requirements(en_text, prompt_lang_dict):
    if not isinstance(prompt_lang_dict, dict) or not prompt_lang_dict:
        return ""

    phrases = build_authoritative_batch_phrase_candidates(en_text)
    if not phrases:
        return ""

    phrase_translations = translate_ui_phrases_for_batch(prompt_lang_dict, phrases)
    phrase_map = {}
    for lang_code in sorted(phrase_translations.keys()):
        language_name = get_language_name_for_code(lang_code) or lang_code
        translated_phrase_map = phrase_translations.get(lang_code, {})
        if not translated_phrase_map:
            continue
        phrase_map[lang_code] = {
            "language": language_name,
            "phrases": translated_phrase_map,
        }

    if not phrase_map:
        return ""

    return (
        "18. AUTHORITATIVE UI PHRASE MAP: The JSON map below contains trusted target-language translations for short translatable UI phrases extracted from the English source. "
        "When any mapped English phrase appears in the source, inside quotes, in parentheses, or inside the current translation candidate for that language, you MUST use the mapped target-language phrase instead of leaving the English phrase raw. "
        "Keep the mapped phrase text exact, but adapt surrounding grammar naturally.\n"
        "Authoritative UI phrase map JSON:\n"
        f"{json.dumps(phrase_map, ensure_ascii=False, indent=2)}\n"
    )


LANGUAGE_NAME_MAP = {
    "en": "English",
    "af": "Afrikaans",
    "am": "Amharic",
    "ar": "Arabic",
    "ast": "Asturian",
    "az": "Azerbaijani",
    "be": "Belarusian",
    "bg": "Bulgarian",
    "bn": "Bengali",
    "bs": "Bosnian",
    "ca": "Catalan",
    "ca-VAL": "Valencian",
    "ceb": "Cebuano",
    "co": "Corsican",
    "cs": "Czech",
    "cy": "Welsh",
    "da": "Danish",
    "de": "German",
    "el": "Greek",
    "eo": "Esperanto",
    "es": "Spanish",
    "es-AR": "Spanish (Argentina)",
    "et": "Estonian",
    "eu": "Basque",
    "fa": "Persian",
    "fi": "Finnish",
    "fr": "French",
    "fy": "Frisian",
    "ga": "Irish",
    "gd": "Gaelic (Scots)",
    "gl": "Galician",
    "gu": "Gujarati",
    "ha": "Hausa",
    "haw": "Hawaiian",
    "he": "Hebrew",
    "hi": "Hindi",
    "hmw": "Hmong",
    "hr": "Croatian",
    "ht": "Haitian Creole",
    "hu": "Hungarian",
    "hy": "Armenian",
    "id": "Indonesian",
    "ig": "Igbo",
    "is": "Icelandic",
    "it": "Italian",
    "ja": "Japanese",
    "jw": "Javanese",
    "ka": "Georgian",
    "kk": "Kazakh",
    "km": "Khmer",
    "kn": "Kannada",
    "ko": "Korean",
    "ku": "Kurdish",
    "ky": "Kyrgyz",
    "la": "Latin",
    "lb": "Luxembourgish",
    "lo": "Lao",
    "lt": "Lithuanian",
    "lv": "Latvian",
    "mg": "Malagasy",
    "mi": "Maori",
    "mk": "Macedonian",
    "ml": "Malayalam",
    "mn": "Mongolian",
    "mr": "Marathi",
    "ms": "Malay",
    "mt": "Maltese",
    "my": "Burmese",
    "nb": "Norwegian Bokmål",
    "ne": "Nepali",
    "nl": "Dutch",
    "nn": "Norwegian Nynorsk",
    "no": "Norwegian",
    "ny": "Nyanja",
    "or": "Oriya",
    "pa": "Punjabi",
    "pl": "Polish",
    "ps": "Pashto",
    "pt": "Portuguese",
    "pt-BR": "Portuguese (Brazil)",
    "ro": "Romanian",
    "ru": "Russian",
    "rw": "Kinyarwanda",
    "sd": "Sindhi",
    "si": "Sinhala",
    "sk": "Slovak",
    "sl": "Slovenian",
    "sm": "Samoan",
    "sn": "Shona",
    "so": "Somali",
    "sq": "Albanian",
    "sr": "Serbian",
    "st": "Sesotho",
    "su": "Sundanese",
    "sv": "Swedish",
    "sw": "Swahili",
    "ta": "Tamil",
    "te": "Telugu",
    "tg": "Tajik",
    "th": "Thai",
    "tk": "Turkmen",
    "tl": "Tagalog",
    "tr": "Turkish",
    "tt": "Tatar",
    "ug": "Uyghur",
    "uk": "Ukrainian",
    "ur": "Urdu",
    "uz": "Uzbek",
    "vi": "Vietnamese",
    "xh": "Xhosa",
    "yi": "Yiddish",
    "yo": "Yoruba",
    "zh-CN": "Chinese (Simplified)",
    "zh-TW": "Chinese (Traditional)",
    "zu": "Zulu",
}


SCRIPT_FAMILY_EXTRA_ALLOWLIST = {
    "ja": {"Han", "Hiragana", "Katakana"},
    "zh-CN": {"Han"},
    "zh-TW": {"Han"},
    "ko": {"Hangul", "Han"},
}

SUSPICIOUS_TRANSLATION_ARTIFACT_PATTERNS = (
    (r"<\|channel>", "translation contains internal channel tags"),
    (r"<channel\|>", "translation contains internal channel tags"),
    (r"\$\\rightarrow\$", "translation contains LaTeX arrow markup"),
    (r"\\rightarrow", "translation contains LaTeX arrow markup"),
    (r"\[(?:%[A-Za-z]+)\]", "translation wraps placeholders in square brackets"),
    (r"<\s*(?:%[A-Za-z]+)\s*>", "translation wraps placeholders in angle brackets"),
    (r"`\s*(?:%[A-Za-z]+)\s*`", "translation wraps placeholders in backticks"),
    (r"</?p\b|</?div\b|<br\s*/?>", "translation contains HTML markup artifact"),
)


def get_unicode_script_family(char):
    if not isinstance(char, str) or len(char) != 1:
        return ""
    if not char.isalpha():
        return ""

    name = unicodedata.name(char, "")
    if not name:
        return ""

    if "CJK UNIFIED IDEOGRAPH" in name or "IDEOGRAPH" in name:
        return "Han"

    script_keywords = (
        ("LATIN", "Latin"),
        ("CYRILLIC", "Cyrillic"),
        ("GREEK", "Greek"),
        ("ARABIC", "Arabic"),
        ("HEBREW", "Hebrew"),
        ("DEVANAGARI", "Devanagari"),
        ("BENGALI", "Bengali"),
        ("GURMUKHI", "Gurmukhi"),
        ("GUJARATI", "Gujarati"),
        ("ORIYA", "Oriya"),
        ("ODIA", "Oriya"),
        ("TAMIL", "Tamil"),
        ("TELUGU", "Telugu"),
        ("KANNADA", "Kannada"),
        ("MALAYALAM", "Malayalam"),
        ("SINHALA", "Sinhala"),
        ("THAI", "Thai"),
        ("LAO", "Lao"),
        ("GEORGIAN", "Georgian"),
        ("ARMENIAN", "Armenian"),
        ("ETHIOPIC", "Ethiopic"),
        ("MYANMAR", "Myanmar"),
        ("KHMER", "Khmer"),
        ("HIRAGANA", "Hiragana"),
        ("KATAKANA", "Katakana"),
        ("HANGUL", "Hangul"),
    )
    for keyword, family in script_keywords:
        if keyword in name:
            return family
    return ""


def extract_script_families(text):
    families = set()
    if not isinstance(text, str):
        return families
    for char in text:
        family = get_unicode_script_family(char)
        if family:
            families.add(family)
    return families


def get_inferred_script_families_for_lang(lang_code):
    if not isinstance(lang_code, str) or not lang_code:
        return set()

    cached = _INFERRED_SCRIPT_FAMILIES_CACHE.get(lang_code)
    if cached is not None:
        return set(cached)

    memory = load_translation_memory_by_english()
    script_counts = {}
    sample_count = 0

    for lang_map in memory.values():
        translated_text = lang_map.get(lang_code, "")
        if not isinstance(translated_text, str):
            continue
        translated_text = translated_text.strip()
        if not translated_text:
            continue

        families = extract_script_families(translated_text)
        if not families:
            continue

        for family in families:
            script_counts[family] = script_counts.get(family, 0) + 1
        sample_count += 1
        if sample_count >= 300:
            break

    if not script_counts:
        _INFERRED_SCRIPT_FAMILIES_CACHE[lang_code] = tuple()
        return set()

    ordered = sorted(script_counts.items(), key=lambda item: (-item[1], item[0]))
    max_count = ordered[0][1]
    second_count = ordered[1][1] if len(ordered) > 1 else 0

    inferred = {family for family, count in ordered if count == max_count}
    if len(inferred) == 1 and max_count >= max(3, second_count * 2):
        # Strong dominant-script preference. Ignore minority noisy memory examples.
        pass
    else:
        inferred = {
            family for family, count in ordered if count >= max(2, max_count - 1)
        }
        if "Latin" in inferred and len(inferred) > 1:
            inferred.discard("Latin")

    _INFERRED_SCRIPT_FAMILIES_CACHE[lang_code] = tuple(sorted(inferred))
    return set(inferred)


def build_allowed_script_families_for_lang(lang_code, en_text):
    allowed = set()
    for requirement in get_known_phrase_requirements(en_text, lang_code):
        allowed.update(
            extract_script_families(requirement.get("known_translation", ""))
        )
    if not allowed:
        allowed.update(get_inferred_script_families_for_lang(lang_code))
    allowed.update(SCRIPT_FAMILY_EXTRA_ALLOWLIST.get(lang_code, set()))
    return allowed


def get_preferred_script_signature_for_lang(lang_code, en_text=""):
    preferred = set(get_inferred_script_families_for_lang(lang_code))
    if not preferred:
        preferred = set(build_allowed_script_families_for_lang(lang_code, en_text))
    preferred.update(SCRIPT_FAMILY_EXTRA_ALLOWLIST.get(lang_code, set()))
    if "Latin" in preferred and len(preferred) > 1:
        preferred.discard("Latin")
    return tuple(sorted(preferred))


def should_force_single_language_local_batch(lang_codes, en_text=""):
    if (
        not resolve_backend_selection()
        or not isinstance(lang_codes, (list, tuple))
        or len(lang_codes) <= 1
    ):
        return False

    saw_non_latin_signature = False
    signatures = set()
    for lang_code in lang_codes:
        signature = get_preferred_script_signature_for_lang(lang_code, en_text)
        if not signature:
            return True
        signatures.add(signature)
        if any(script != "Latin" for script in signature):
            saw_non_latin_signature = True

    if saw_non_latin_signature:
        return True
    return len(signatures) > 1


def detect_unexpected_script_mixture(
    lang_code, translated_text, en_text, placeholder_pairs=None
):
    if (
        not isinstance(lang_code, str)
        or not lang_code
        or not isinstance(translated_text, str)
    ):
        return ""

    allowed_scripts = build_allowed_script_families_for_lang(lang_code, en_text)
    if not allowed_scripts:
        return ""

    cleaned_text = strip_known_translated_phrases(en_text, translated_text, lang_code)
    cleaned_text = PLACEHOLDER_TOKEN_PATTERN.sub(" ", cleaned_text)
    cleaned_text = re.sub(r"__LOCKED_TERM_\d+__", " ", cleaned_text)

    if placeholder_pairs:
        for _, term in placeholder_pairs:
            cleaned_text = re.sub(
                rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(term)}(?!{NON_WORD_BOUNDARY_PATTERN})",
                " ",
                cleaned_text,
                flags=re.IGNORECASE,
            )

    for literal in extract_source_ascii_literal_terms(en_text):
        cleaned_text = re.sub(
            re.escape(literal), " ", cleaned_text, flags=re.IGNORECASE
        )

    actual_scripts = extract_script_families(cleaned_text)
    unexpected_scripts = sorted(
        script for script in actual_scripts if script not in allowed_scripts
    )
    if unexpected_scripts:
        return ", ".join(unexpected_scripts)
    return ""


def contains_response_artifact_lines(translated_text):
    if not isinstance(translated_text, str) or not translated_text.strip():
        return False
    if re.search(
        r"(^|\n)\s*[A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*\s*(?:\t|	)", translated_text
    ):
        return True
    if (
        "(Note:" in translated_text
        or "representation of the output format requested" in translated_text
    ):
        return True
    if "<|channel>" in translated_text or "<channel|>" in translated_text:
        return True
    if "Wait," in translated_text or "Final check" in translated_text:
        return True
    return False


def detect_structural_translation_artifact(translated_text):
    if not isinstance(translated_text, str):
        return ""
    for pattern, message in SUSPICIOUS_TRANSLATION_ARTIFACT_PATTERNS:
        if re.search(pattern, translated_text):
            return message
    return ""


def detect_repetitive_token_flood(translated_text):
    if not isinstance(translated_text, str):
        return ""

    tokens = re.findall(r"[\w'’-]+", translated_text, re.UNICODE)
    if not tokens:
        return ""

    previous = None
    run_length = 0
    for token in tokens:
        normalized = token.casefold()
        if len(normalized) <= 1:
            previous = None
            run_length = 0
            continue
        if normalized == previous:
            run_length += 1
        else:
            previous = normalized
            run_length = 1
        if run_length >= REPETITIVE_TOKEN_FLOOD_THRESHOLD:
            return normalized
    return ""


def get_local_effective_batch_size(batch_size, en_text, lang_count):
    effective_batch_size = min(batch_size, LOCAL_API_MAX_LANGUAGES_PER_BATCH)
    if lang_count <= 1:
        return 1
    return max(1, effective_batch_size)


def build_local_target_language_map(prompt_lang_dict):
    if not isinstance(prompt_lang_dict, dict):
        return {}

    return {
        lang_code: (get_language_name_for_code(lang_code) or lang_code)
        for lang_code in sorted(prompt_lang_dict.keys())
    }


def build_batch_preferred_script_map(prompt_lang_dict, en_text):
    if not isinstance(prompt_lang_dict, dict):
        return {}

    script_map = {}
    for lang_code in sorted(prompt_lang_dict.keys()):
        preferred_scripts = get_preferred_script_signature_for_lang(lang_code, en_text)
        if preferred_scripts:
            script_map[lang_code] = list(preferred_scripts)
    return script_map


def build_batch_focus_requirements(en_text, prompt_lang_dict):
    if not isinstance(prompt_lang_dict, dict) or not prompt_lang_dict:
        return ""

    target_language_map = build_local_target_language_map(prompt_lang_dict)
    target_language_json = json.dumps(target_language_map, ensure_ascii=False, indent=2)
    requirements = [
        "17. TARGET LANGUAGE MAP: Translate each language code only into the language shown in this JSON map. Do NOT mix languages between lines and do NOT answer in a neighboring or more common language.\n",
        f"{target_language_json}\n",
    ]

    preferred_script_map = build_batch_preferred_script_map(prompt_lang_dict, en_text)
    if preferred_script_map:
        preferred_script_json = json.dumps(
            preferred_script_map, ensure_ascii=False, indent=2
        )
        requirements.extend(
            [
                "18. PROJECT SCRIPT MAP: For each language code below, use the listed script family or families for ordinary translated words whenever possible. This map is inferred from existing translations already stored in translations.map for this project. Do NOT switch to a fallback script unless the source token is a protected placeholder, brand, acronym, or file/path literal copied unchanged from the source.\n",
                f"{preferred_script_json}\n",
            ]
        )

    return "".join(requirements)


def build_local_batch_focus_requirements(en_text, prompt_lang_dict):
    if (
        not resolve_backend_selection()
        or not isinstance(prompt_lang_dict, dict)
        or not prompt_lang_dict
    ):
        return ""

    target_language_map = build_local_target_language_map(prompt_lang_dict)
    target_language_json = json.dumps(target_language_map, ensure_ascii=False, indent=2)
    requirements = [
        "18. LOCAL MODEL OUTPUT RULES: Never reveal internal reasoning. Never output self-corrections, analysis, checklists, bullets, or commentary. Never output internal channel tags such as <|channel>thought or <channel|>. Never output LaTeX, arrows like -> or $\rightarrow$, or mixed-script garbage. Never output HTML fragments. Return only final translation lines in the exact requested format.\n",
        "19. LOCAL TARGET LANGUAGE MAP: Translate each language code only into the language shown in this JSON map. Do NOT mix languages between lines and do NOT answer in a neighboring or more common language.\n",
        f"{target_language_json}\n",
        "20. LOCAL UI LABEL RULE: If you translate a quoted UI label or mode name, do NOT append the original English label in parentheses, quotes, or gloss form after the translated label. The final line must not contain duplicated English UI labels.\n",
        "20. LOCAL QUOTED UI LABEL RULE: If the English source contains quoted mode names, slash-separated menu labels, or parenthesized UI text, translate those words too unless they are protected placeholders or strict all-uppercase acronyms.\n",
        "21. LOCAL NO RAW ENGLISH RULE: Do NOT leave ordinary English source words, quoted UI labels, or parenthesized UI labels untranslated anywhere in the final non-English lines. Do NOT append the English original in parentheses after a translated UI label.\n",
    ]

    if len(prompt_lang_dict) == 1:
        requirements.append(
            build_local_single_language_guidance(en_text, prompt_lang_dict)
        )

    return "".join(requirements)


def build_local_single_language_guidance(en_text, prompt_lang_dict):
    if not resolve_backend_selection():
        return ""
    if not isinstance(prompt_lang_dict, dict) or len(prompt_lang_dict) != 1:
        return ""

    lang_code = next(iter(prompt_lang_dict.keys()))
    requirement = get_known_leading_phrase_requirement(en_text, lang_code)
    allowed_scripts = sorted(build_allowed_script_families_for_lang(lang_code, en_text))
    forbidden_source_words = build_local_forbidden_source_word_candidates(en_text)[:8]

    script_rule = ""
    language_name = get_language_name_for_code(lang_code)
    identity_rule = ""
    if language_name:
        identity_rule = (
            f"23. LOCAL LANGUAGE IDENTITY RULE: The target language is `{language_name}` for code `{lang_code}`. "
            f"Do NOT drift into a neighboring, regional, or more common language. Keep the wording natural for `{language_name}`.\n"
        )
    if allowed_scripts:
        if len(allowed_scripts) == 1:
            script_rule = (
                f"21. LOCAL SCRIPT RULE: Write the translation using only the `{allowed_scripts[0]}` script for normal letters. "
                "Do NOT mix in letters from other scripts. Placeholders, digits, and normal punctuation may remain unchanged.\n"
            )
        else:
            script_rule = (
                f"21. LOCAL SCRIPT RULE: Write the translation using only these script families for normal letters: {', '.join(allowed_scripts)}. "
                "Do NOT mix in letters from any other scripts. Placeholders, digits, and normal punctuation may remain unchanged.\n"
            )

    forbidden_words_rule = ""
    if forbidden_source_words:
        forbidden_words_lines = ", ".join(
            f"`{word}`" for word in forbidden_source_words
        )
        forbidden_words_rule = (
            "22. LOCAL FORBIDDEN RAW SOURCE WORDS: Do NOT leave these English source words untranslated anywhere in the final line: "
            f"{forbidden_words_lines}.\n"
        )

    if not requirement:
        return (
            "19. LOCAL STYLE RULE: Use a neutral UI status-message tone. Do NOT use first-person wording equivalent to 'I renamed' or 'we changed'. "
            "Do NOT leave raw English words like filename, source, sources, majority, or renamed in the final line unless they are protected terms.\n"
            + script_rule
            + forbidden_words_rule
            + identity_rule
        )

    visible_source = build_visible_prompt_text(en_text).lstrip()
    remainder = visible_source
    source_phrase = requirement.get("source_phrase", "")
    separator = requirement.get("separator", "")
    if source_phrase and visible_source.startswith(source_phrase):
        remainder = visible_source[len(source_phrase) :]
        remainder = remainder.lstrip()
        if separator and remainder.startswith(separator):
            remainder = remainder[len(separator) :]
        remainder = remainder.lstrip()
    if not remainder:
        remainder = visible_source

    return (
        f"19. LOCAL EXACT LABEL RULE: The leading label `{requirement['known_translation']}` is already fixed and correct. Keep it unchanged at the start of the line. Translate only the remainder of the message naturally after that label.\n"
        f"Remainder to translate after the label:\n<<<\n{remainder}\n>>>\n"
        "20. LOCAL STYLE RULE: Use a neutral UI status-message tone. Do NOT use first-person wording equivalent to 'I renamed' or 'we changed'. "
        "Do NOT leave raw English words like filename, source, sources, majority, or renamed in the final line unless they are protected terms.\n"
        + script_rule
        + forbidden_words_rule
        + identity_rule
    )


def normalize_line_break_separator(separator):
    if separator in ("\\r\\n", "\\n", "\\r"):
        return "\\n"
    return separator


def build_visible_prompt_text(text):
    if not isinstance(text, str) or not text:
        return text

    visible_text = text.replace("\\r\\n", "\n")
    visible_text = visible_text.replace("\\n", "\n")
    visible_text = visible_text.replace("\\r", "\n")
    visible_text = visible_text.replace("\\t", "\t")
    visible_text = visible_text.replace("\\\\", "\\")
    return visible_text


def build_prompt_text_block(label, visible_text, source_text=None):
    if source_text is None:
        source_text = visible_text

    visible_prompt_text = build_visible_prompt_text(visible_text)
    return (
        f"{label} (visible meaning; treat the line breaks below as real line or paragraph breaks):\n"
        f"<<<\n{visible_prompt_text}\n>>>\n\n"
        f"{label} (source string; preserve escape sequences and placeholders exactly in output):\n"
        f"{json.dumps(source_text, ensure_ascii=False)}"
    )


def is_simple_title_case(token):
    if len(token) <= 1:
        return False
    return token[0].isupper() and token[1:].islower()


def should_protect_token(token):
    if not token:
        return False

    if token in PROTECTED_TOKENS:
        return True

    if token in TRANSLATABLE_ALL_UPPERCASE_TOKENS:
        return False

    if any(separator in token for separator in (" ", "/")) and "\\" not in token:
        source_words = SOURCE_ENGLISH_WORD_PATTERN.findall(token)
        if source_words and all(
            not should_protect_token(word) for word in source_words
        ):
            return False

    has_upper = any(ch.isupper() for ch in token)
    has_lower = any(ch.islower() for ch in token)
    has_digit = any(ch.isdigit() for ch in token)

    # Keep canonical all-uppercase acronyms (AI, NAT, TCP, ...).
    if token.isupper() and len(token) >= 2:
        return True

    # Keep camel/mixed-case brand-like tokens (eMule, uTP, WinRAR, ...).
    if has_upper and has_lower and not is_simple_title_case(token):
        return True

    # Keep Windows-style versioned platform identifiers (Win32, IPv6, ...).
    if has_upper and has_digit:
        return True

    return False


def normalize_protected_token(token):
    if not isinstance(token, str) or not token:
        return ""

    normalized = token.lstrip(PROTECTED_TOKEN_LEADING_TRIM_CHARS)
    normalized = normalized.rstrip(PROTECTED_TOKEN_TRAILING_TRIM_CHARS)
    return normalized


def is_probable_translatable_ascii_slash_label(token, source_text):
    if (
        not isinstance(token, str)
        or "/" not in token
        or "\\" in token
        or not isinstance(source_text, str)
        or not source_text
    ):
        return False

    normalized = normalize_candidate_source_phrase(token)
    if not normalized or "." in normalized or "_" in normalized:
        return False
    if any(ch.isdigit() for ch in normalized):
        return False

    segments = [segment for segment in normalized.split("/") if segment]
    if len(segments) < 2:
        return False
    if any(not re.fullmatch(r"[A-Za-z]+", segment) for segment in segments):
        return False
    if any(should_protect_token(segment) for segment in segments):
        return False

    visible_source = build_visible_prompt_text(source_text)
    quoted_pattern = re.compile(
        rf"['\"“”‘’]\s*{re.escape(normalized)}\s*['\"“”‘’]", re.IGNORECASE
    )
    mode_pattern = re.compile(
        rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(normalized)}(?!{NON_WORD_BOUNDARY_PATTERN})\s+mode\b",
        re.IGNORECASE,
    )
    if quoted_pattern.search(visible_source) or mode_pattern.search(visible_source):
        return True

    return normalized in extract_parenthesized_ui_phrases(source_text)


def extract_source_ascii_literal_terms(en_text):
    literals = set()
    if not isinstance(en_text, str) or not en_text:
        return []

    source_text = build_visible_prompt_text(en_text)

    for match in re.finditer(r"[A-Za-z0-9_./\\-]+\.[A-Za-z0-9_./\\-]+", source_text):
        token = match.group(0).strip(".,:;!?()[]{}<>\"'")
        if token and re.search(r"[A-Za-z]", token):
            literals.add(token)

    for match in re.finditer(
        r"(?:[A-Za-z]:)?[A-Za-z0-9_.-]+(?:[\\/][A-Za-z0-9_.-]+)+", source_text
    ):
        token = match.group(0).strip(".,:;!?()[]{}<>\"'")
        if is_probable_translatable_ascii_slash_label(token, source_text):
            continue
        if token and re.search(r"[A-Za-z]", token):
            literals.add(token)

    return sorted(literals, key=lambda item: (-len(item), item.casefold()))


def extract_source_ascii_literals_to_ignore_for_script_detection(en_text):
    literals = set()
    if not isinstance(en_text, str) or not en_text:
        return []

    for term in extract_protected_terms(en_text):
        literals.add(term)
    for literal in extract_source_ascii_literal_terms(en_text):
        literals.add(literal)

    return sorted(literals, key=lambda item: (-len(item), item.casefold()))


def extract_protected_terms(en_text):
    if not en_text:
        return []

    extraction_text = build_visible_prompt_text(en_text)
    terms = set()
    for match in TOKEN_SCAN_PATTERN.finditer(extraction_text):
        token = normalize_protected_token(match.group(0))
        if should_protect_token(token):
            terms.add(token)

    for fixed_term in PROTECTED_TOKENS:
        if fixed_term in extraction_text:
            terms.add(fixed_term)

    for literal in extract_source_ascii_literal_terms(extraction_text):
        terms.add(literal)

    return sorted(terms, key=lambda item: (-len(item), item))


def build_protected_placeholders(en_text, lang_dict):
    terms = extract_protected_terms(en_text)
    if not terms:
        return []

    occupied_text_parts = [en_text]
    if isinstance(lang_dict, dict):
        occupied_text_parts.extend(t for t in lang_dict.values() if isinstance(t, str))
    occupied_text = "\n".join(occupied_text_parts)

    placeholder_pairs = []
    idx = 0
    for term in terms:
        placeholder = f"__LOCKED_TERM_{idx}__"
        while placeholder in occupied_text:
            idx += 1
            placeholder = f"__LOCKED_TERM_{idx}__"
        placeholder_pairs.append((placeholder, term))
        idx += 1

    return placeholder_pairs


def apply_protected_placeholders(text, placeholder_pairs):
    if not isinstance(text, str) or not placeholder_pairs:
        return text

    masked = text
    for placeholder, term in placeholder_pairs:
        pattern = re.compile(
            rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(term)}(?!{NON_WORD_BOUNDARY_PATTERN})"
        )
        masked = pattern.sub(placeholder, masked)

    return masked


def restore_protected_placeholders(text, placeholder_pairs):
    if not isinstance(text, str) or not placeholder_pairs:
        return text

    restored = text
    for placeholder, term in placeholder_pairs:
        restored = restored.replace(placeholder, term)

    return restored


def all_protected_terms_preserved(text, placeholder_pairs):
    if not isinstance(text, str):
        return False

    for _, term in placeholder_pairs:
        if term not in text:
            return False

    return True


def get_missing_protected_terms(text, placeholder_pairs):
    missing_terms = []
    if not isinstance(text, str):
        return [term for _, term in placeholder_pairs]

    for _, term in placeholder_pairs:
        if term not in text:
            missing_terms.append(term)

    return missing_terms


def get_protected_placeholders_prompt_block(placeholder_pairs):
    if not placeholder_pairs:
        return "{}"

    mapping = {placeholder: term for placeholder, term in placeholder_pairs}
    return json.dumps(mapping, ensure_ascii=False, indent=2)


def starts_with_phrase_boundary(text, phrase):
    if not isinstance(text, str) or not text.startswith(phrase):
        return False
    if len(text) == len(phrase):
        return True

    next_char = text[len(phrase)]
    return not (next_char.isalpha() or next_char.isdigit() or next_char == "_")


def uppercase_first_cased_character(text):
    if not isinstance(text, str) or not text:
        return text

    for idx, ch in enumerate(text):
        upper_ch = ch.upper()
        lower_ch = ch.lower()
        if ch.isspace():
            continue
        if not ch.isalpha():
            continue
        if upper_ch == lower_ch:
            return text
        return text[:idx] + upper_ch + text[idx + 1 :]

    return text


def has_any_cased_characters(text):
    if not isinstance(text, str) or not text:
        return False

    for ch in text:
        if ch.isalpha() and ch.upper() != ch.lower():
            return True

    return False


def is_all_cased_characters_upper(text):
    if not isinstance(text, str) or not text:
        return False

    has_cased_chars = False
    for ch in text:
        if not ch.isalpha():
            continue

        upper_ch = ch.upper()
        lower_ch = ch.lower()
        if upper_ch == lower_ch:
            continue

        has_cased_chars = True
        if ch != upper_ch:
            return False

    return has_cased_chars


def normalize_translated_ok_label(en_text, translated_text):
    if not isinstance(en_text, str) or not isinstance(translated_text, str):
        return translated_text

    if en_text.strip() != "OK":
        return translated_text

    leading_ws_len = len(translated_text) - len(translated_text.lstrip())
    trailing_ws_len = len(translated_text) - len(translated_text.rstrip())
    start_idx = leading_ws_len
    end_idx = (
        len(translated_text) - trailing_ws_len
        if trailing_ws_len
        else len(translated_text)
    )
    body = translated_text[start_idx:end_idx]
    if not body or body.upper() == "OK":
        return translated_text

    if not has_any_cased_characters(body) or not is_all_cased_characters_upper(body):
        return translated_text

    normalized_body = uppercase_first_cased_character(body.lower())
    return translated_text[:start_idx] + normalized_body + translated_text[end_idx:]


def extract_ascii_leading_words(text, max_words=3):
    if not isinstance(text, str) or not text:
        return []

    words = []
    idx = 0
    while len(words) < max_words:
        while idx < len(text) and text[idx].isspace():
            idx += 1

        match = ASCII_LEADING_WORD_PATTERN.match(text, idx)
        if not match:
            break

        words.append(match.group(0))
        idx = match.end()

    return words


def build_dynamic_leading_fragment_candidates(en_segment):
    candidates = set(SUSPICIOUS_ENGLISH_LEADING_FRAGMENTS)
    leading_words = extract_ascii_leading_words(en_segment, 3)
    if not leading_words or leading_words[0] in ALLOWED_SHARED_LEADING_LABELS:
        return sorted(candidates, key=lambda item: (-len(item), item))

    first_word = leading_words[0]
    if first_word.lower() not in SUSPICIOUS_DYNAMIC_LEADING_STARTERS:
        return sorted(candidates, key=lambda item: (-len(item), item))

    for word_count in range(1, len(leading_words) + 1):
        phrase = " ".join(leading_words[:word_count])
        if (
            word_count == 1
            and len(first_word) < 4
            and first_word.lower() not in {"do", "use"}
        ):
            continue
        candidates.add(phrase)

    return sorted(candidates, key=lambda item: (-len(item), item))


def detect_copied_english_leading_fragment(en_segment, translated_segment):
    if not isinstance(en_segment, str) or not isinstance(translated_segment, str):
        return None
    if not en_segment or not translated_segment:
        return None

    leading_whitespace_len = len(translated_segment) - len(translated_segment.lstrip())
    leading_whitespace = translated_segment[:leading_whitespace_len]
    translated_body = translated_segment[leading_whitespace_len:]
    if not translated_body:
        return None

    for phrase in build_dynamic_leading_fragment_candidates(en_segment):
        if not starts_with_phrase_boundary(en_segment, phrase):
            continue

        for prefix in ESCAPE_ARTIFACT_PREFIXES:
            candidate = prefix + phrase
            if not starts_with_phrase_boundary(translated_body, candidate):
                continue

            en_remainder = en_segment[len(phrase) :]
            translated_remainder = translated_body[len(candidate) :]
            trimmed_translated_remainder = translated_remainder.lstrip()
            trimmed_en_remainder = en_remainder.lstrip()

            is_full_english_copy = translated_remainder.startswith(en_remainder)
            if trimmed_en_remainder and trimmed_translated_remainder.startswith(
                trimmed_en_remainder
            ):
                is_full_english_copy = True

            first_remainder_char = trimmed_translated_remainder[:1]
            is_removable = (
                bool(first_remainder_char)
                and first_remainder_char.isalpha()
                and not is_full_english_copy
            )

            return {
                "leading_whitespace": leading_whitespace,
                "translated_remainder": trimmed_translated_remainder,
                "removable": is_removable,
            }

    return None


def strip_copied_english_leading_fragment(en_segment, translated_segment):
    detection = detect_copied_english_leading_fragment(en_segment, translated_segment)
    if detection and detection["removable"]:
        return detection["leading_whitespace"] + uppercase_first_cased_character(
            detection["translated_remainder"]
        )
    return translated_segment


def has_copied_english_leading_fragments(en_text, translated_text):
    if not isinstance(en_text, str) or not isinstance(translated_text, str):
        return False
    if not en_text or not translated_text or translated_text == en_text:
        return False

    en_parts = ESCAPED_LINE_SPLIT_PATTERN.split(en_text)
    translated_parts = ESCAPED_LINE_SPLIT_PATTERN.split(translated_text)
    if len(en_parts) != len(translated_parts) or len(en_parts) < 3:
        return False

    for idx in range(1, len(en_parts), 2):
        if normalize_line_break_separator(
            en_parts[idx]
        ) != normalize_line_break_separator(translated_parts[idx]):
            return False

    for idx in range(0, len(en_parts), 2):
        if idx == 0:
            continue
        if detect_copied_english_leading_fragment(en_parts[idx], translated_parts[idx]):
            return True

    return False


def get_source_terminal_punctuation_for_term(en_segment, term):
    if not isinstance(en_segment, str) or not isinstance(term, str):
        return ""
    if not en_segment or not term:
        return ""

    match = re.search(
        rf"{re.escape(term)}([{re.escape(TERMINAL_PUNCTUATION_CHARS)}]+)\s*$",
        en_segment,
    )
    if not match:
        return ""

    return match.group(1)


def is_single_repeated_punctuation(text):
    return bool(text) and len(set(text)) == 1


def repair_leaked_terminal_punctuation_in_segment(
    en_segment, translated_segment, protected_terms
):
    if not isinstance(en_segment, str) or not isinstance(translated_segment, str):
        return translated_segment
    if not en_segment or not translated_segment or not protected_terms:
        return translated_segment

    cleaned_segment = translated_segment
    for term in protected_terms:
        if term not in en_segment or term not in cleaned_segment:
            continue

        source_terminal_punctuation = get_source_terminal_punctuation_for_term(
            en_segment, term
        )
        if not source_terminal_punctuation:
            continue

        last_term_pos = cleaned_segment.rfind(term)
        if last_term_pos < 0:
            continue

        after_term_pos = last_term_pos + len(term)
        suffix = cleaned_segment[after_term_pos:]

        continuation_match = re.match(
            rf"([{re.escape(TERMINAL_PUNCTUATION_CHARS)}]+)(\s+)(?=\S)", suffix
        )
        if continuation_match:
            cleaned_segment = (
                cleaned_segment[:after_term_pos]
                + suffix[len(continuation_match.group(1)) :]
            )
            suffix = cleaned_segment[after_term_pos:]

        if not is_single_repeated_punctuation(source_terminal_punctuation):
            continue

        source_punct_char = source_terminal_punctuation[0]
        duplicate_match = re.match(
            rf"({re.escape(source_punct_char)}{{{len(source_terminal_punctuation) + 1},}})(\s*)$",
            suffix,
        )
        if duplicate_match:
            cleaned_segment = (
                cleaned_segment[:after_term_pos]
                + source_terminal_punctuation
                + duplicate_match.group(2)
            )

    return cleaned_segment


def repair_leaked_terminal_punctuation(en_text, translated_text, placeholder_pairs):
    if not isinstance(en_text, str) or not isinstance(translated_text, str):
        return translated_text
    if not en_text or not translated_text or not placeholder_pairs:
        return translated_text

    en_parts = ESCAPED_LINE_SPLIT_PATTERN.split(en_text)
    translated_parts = ESCAPED_LINE_SPLIT_PATTERN.split(translated_text)
    if len(en_parts) != len(translated_parts):
        return translated_text

    for idx in range(1, len(en_parts), 2):
        if normalize_line_break_separator(
            en_parts[idx]
        ) != normalize_line_break_separator(translated_parts[idx]):
            return translated_text

    protected_terms = [term for _, term in placeholder_pairs]
    cleaned_parts = []
    for idx, translated_part in enumerate(translated_parts):
        if idx % 2 == 1:
            cleaned_parts.append(translated_part)
            continue
        cleaned_parts.append(
            repair_leaked_terminal_punctuation_in_segment(
                en_parts[idx], translated_part, protected_terms
            )
        )

    return "".join(cleaned_parts)


def has_alpha_before_parenthetical_gloss(text, match_start):
    if not isinstance(text, str) or not text or not isinstance(match_start, int):
        return False

    idx = match_start - 1
    while idx >= 0:
        ch = text[idx]
        if ch.isspace():
            idx -= 1
            continue

        char_category = unicodedata.category(ch)
        if char_category.startswith("M"):
            idx -= 1
            continue

        return ch.isalpha()

    return False


def should_strip_parenthetical_source_gloss(
    en_text, fragment, protected_terms_cf=None
):
    normalized = normalize_candidate_source_phrase(fragment)
    if not normalized:
        return False
    if not re.search(r"[A-Za-z]", normalized):
        return False
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 /&+_.:-]*", normalized):
        return False

    normalized_cf = normalized.casefold()
    if normalized_cf in (protected_terms_cf or set()):
        return False
    if should_protect_token(normalized):
        return False

    return normalized_cf in build_visible_prompt_text(en_text).casefold()


def strip_english_parenthetical_glosses(
    en_text, translated_text, lang_code="", placeholder_pairs=None
):
    if (
        not isinstance(en_text, str)
        or not isinstance(translated_text, str)
        or not translated_text
    ):
        return translated_text

    protected_terms_cf = {
        term.casefold()
        for _, term in (placeholder_pairs or [])
        if isinstance(term, str) and term
    }
    candidate_phrases = []
    for phrase in get_leaked_source_phrases(en_text, translated_text, lang_code):
        normalized = normalize_candidate_source_phrase(phrase)
        if normalized:
            if normalized.casefold() in protected_terms_cf:
                continue
            candidate_phrases.append(normalized)

    if not candidate_phrases:
        return translated_text

    cleaned_text = translated_text
    for phrase in sorted(
        set(candidate_phrases), key=lambda item: (-len(item), item.casefold())
    ):
        escaped_phrase = re.escape(phrase)
        wrapper_patterns = (
            rf"\s*\(\s*{escaped_phrase}\s*\)",
            rf"\s*\[\s*{escaped_phrase}\s*\]",
            rf"\s*（\s*{escaped_phrase}\s*）",
        )
        for pattern in wrapper_patterns:
            cleaned_text = re.sub(pattern, "", cleaned_text, flags=re.IGNORECASE)

    generic_wrapper_patterns = (
        r"\s*\(\s*([^()\n]{1,80})\s*\)",
        r"\s*\[\s*([^\[\]\n]{1,80})\s*\]",
        r"\s*（\s*([^（）\n]{1,80})\s*）",
    )
    for pattern in generic_wrapper_patterns:
        current_text = cleaned_text

        def replace_generic_source_gloss(match):
            if not has_alpha_before_parenthetical_gloss(current_text, match.start()):
                return match.group(0)
            if not should_strip_parenthetical_source_gloss(
                en_text, match.group(1), protected_terms_cf
            ):
                return match.group(0)
            return ""

        cleaned_text = re.sub(pattern, replace_generic_source_gloss, cleaned_text)

    return cleaned_text


def cleanup_translated_text(en_text, translated_text, placeholder_pairs, lang_code=""):
    cleaned_text = strip_copied_english_leading_fragments(en_text, translated_text)
    cleaned_text = strip_english_parenthetical_glosses(
        en_text, cleaned_text, lang_code, placeholder_pairs
    )
    cleaned_text = normalize_placeholder_quote_style(en_text, cleaned_text)
    cleaned_text = repair_leaked_terminal_punctuation(
        en_text, cleaned_text, placeholder_pairs
    )
    return normalize_translated_ok_label(en_text, cleaned_text)


def has_escape_or_punctuation_leak_issue(en_text, translated_text, placeholder_pairs):
    if has_copied_english_leading_fragments(en_text, translated_text):
        return True
    if contains_embedded_english_phrase_leak(en_text, translated_text):
        return True
    if (
        placeholder_pairs
        and repair_leaked_terminal_punctuation(
            en_text, translated_text, placeholder_pairs
        )
        != translated_text
    ):
        return True
    return False


def strip_copied_english_leading_fragments(en_text, translated_text):
    if not isinstance(en_text, str) or not isinstance(translated_text, str):
        return translated_text
    if not en_text or not translated_text or translated_text == en_text:
        return translated_text

    en_parts = ESCAPED_LINE_SPLIT_PATTERN.split(en_text)
    translated_parts = ESCAPED_LINE_SPLIT_PATTERN.split(translated_text)
    if len(en_parts) != len(translated_parts) or len(en_parts) < 3:
        return translated_text

    for idx in range(1, len(en_parts), 2):
        if normalize_line_break_separator(
            en_parts[idx]
        ) != normalize_line_break_separator(translated_parts[idx]):
            return translated_text

    cleaned_parts = []
    for idx, translated_part in enumerate(translated_parts):
        if idx % 2 == 1:
            cleaned_parts.append(translated_part)
            continue


def get_language_name_for_code(lang_code):
    if not isinstance(lang_code, str) or not lang_code.strip():
        return ""
    normalized = lang_code.strip()
    if normalized in LANGUAGE_NAME_MAP:
        return LANGUAGE_NAME_MAP[normalized]
    base = normalized.split("-")[0]
    if base in LANGUAGE_NAME_MAP:
        return LANGUAGE_NAME_MAP[base]
    if pycountry is None:
        return ""
    lang = None
    try:
        if len(base) == 2:
            lang = pycountry.languages.get(alpha_2=base)
        if lang is None and len(base) == 3:
            lang = pycountry.languages.get(alpha_3=base)
        if lang is None:
            lang = pycountry.languages.get(alpha_2=base) or pycountry.languages.get(
                alpha_3=base
            )
    except Exception:
        lang = None
    return getattr(lang, "name", "") or ""


def strip_known_translated_phrases(en_text, translated_text, lang_code=""):
    if not isinstance(translated_text, str):
        return translated_text
    stripped_text = translated_text
    if not isinstance(lang_code, str) or not lang_code:
        return stripped_text
    for requirement in sorted(
        get_known_phrase_requirements(en_text, lang_code),
        key=lambda item: (
            -len(item["known_translation"]),
            item["known_translation"].casefold(),
        ),
    ):
        stripped_text = re.sub(
            re.escape(requirement["known_translation"]),
            " ",
            stripped_text,
            flags=re.IGNORECASE,
        )
    return stripped_text


def source_phrase_leaks_into_translation(
    phrase, visible_translation, lowered_visible_translation
):
    if not isinstance(phrase, str) or not phrase:
        return False
    phrase_cf = phrase.casefold()
    if phrase_cf not in lowered_visible_translation:
        return False
    if " " not in phrase:
        return True
    pattern = re.compile(
        rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(phrase)}(?!{NON_WORD_BOUNDARY_PATTERN})",
        re.IGNORECASE,
    )
    return bool(pattern.search(visible_translation))


def extract_quoted_english_fragment_candidates(text):
    fragments = []
    if not isinstance(text, str) or not text:
        return fragments

    seen = set()
    visible_text = build_visible_prompt_text(text)
    for match in QUOTED_UI_PHRASE_PATTERN.finditer(visible_text):
        fragment = normalize_candidate_source_phrase(match.group(1))
        if not fragment:
            continue
        fragment_cf = fragment.casefold()
        if fragment_cf in seen:
            continue
        seen.add(fragment_cf)
        fragments.append(fragment)

    return fragments


def extract_suspicious_transliterated_or_foreign_tokens(
    lang_code, translated_text, en_text, placeholder_pairs=None
):
    suspicious = []
    if (
        not isinstance(lang_code, str)
        or not lang_code
        or not isinstance(translated_text, str)
    ):
        return suspicious

    allowed_scripts = build_allowed_script_families_for_lang(lang_code, en_text)
    if not allowed_scripts:
        return suspicious

    visible_translation = build_visible_prompt_text(
        strip_known_translated_phrases(en_text, translated_text, lang_code)
    )
    visible_translation = PLACEHOLDER_TOKEN_PATTERN.sub(" ", visible_translation)
    protected_literals = {
        item.casefold()
        for item in extract_source_ascii_literals_to_ignore_for_script_detection(
            en_text
        )
    }
    protected_terms = {term.casefold() for _, term in (placeholder_pairs or [])}
    preferred_scripts = {
        script for script in allowed_scripts if script != "Latin"
    } or allowed_scripts

    seen = set()
    for token in re.findall(r"[\w'’+-]+", visible_translation, re.UNICODE):
        normalized_token = normalize_candidate_source_phrase(token)
        if not normalized_token:
            continue
        normalized_cf = normalized_token.casefold()
        if normalized_cf in seen:
            continue
        if normalized_cf in protected_literals or normalized_cf in protected_terms:
            continue
        if normalized_cf in ENGLISH_LEAK_STOPWORDS:
            continue
        if re.fullmatch(r"__LOCKED_TERM_\d+__", normalized_token):
            continue
        if PLACEHOLDER_TOKEN_PATTERN.fullmatch(normalized_token):
            continue

        token_scripts = extract_script_families(normalized_token)
        if not token_scripts:
            continue

        is_mixed_script = len(token_scripts) >= 2
        has_unexpected_script = any(
            script not in allowed_scripts for script in token_scripts
        )
        looks_like_transliteration = (
            "Latin" in token_scripts
            and preferred_scripts
            and all(
                script == "Latin" or script in preferred_scripts
                for script in token_scripts
            )
            and "Latin" not in preferred_scripts
            and len(normalized_token) >= 4
            and re.search(r"[A-Za-z]", normalized_token)
        )

        if not (is_mixed_script or has_unexpected_script or looks_like_transliteration):
            continue

        seen.add(normalized_cf)
        suspicious.append(normalized_token)

    suspicious.sort(key=lambda item: (-len(item), item.casefold()))
    return suspicious


def get_leaked_source_phrases(en_text, translated_text, lang_code=""):
    leaked = []
    if not isinstance(translated_text, str):
        return leaked
    visible_translation = build_visible_prompt_text(
        strip_known_translated_phrases(en_text, translated_text, lang_code)
    )
    lowered_visible_translation = visible_translation.casefold()
    protected_terms_cf = {
        term.casefold() for term in extract_protected_terms(en_text)
    }
    for phrase in iter_embedded_english_phrase_candidates(en_text):
        normalized_phrase = normalize_candidate_source_phrase(phrase)
        if not normalized_phrase:
            continue
        if (
            normalized_phrase.casefold() in protected_terms_cf
            or should_protect_token(normalized_phrase)
        ):
            continue
        if source_phrase_leaks_into_translation(
            phrase, visible_translation, lowered_visible_translation
        ):
            leaked.append(phrase)
    return leaked


def source_word_leaks_into_translation(word, visible_translation):
    if (
        not isinstance(word, str)
        or not word
        or not isinstance(visible_translation, str)
    ):
        return False
    pattern = re.compile(
        rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(word)}(?!{NON_WORD_BOUNDARY_PATTERN})",
        re.IGNORECASE,
    )
    return bool(pattern.search(visible_translation))


def get_leaked_source_words(en_text, translated_text, lang_code=""):
    leaked = []
    if not isinstance(translated_text, str):
        return leaked
    visible_translation = build_visible_prompt_text(
        strip_known_translated_phrases(en_text, translated_text, lang_code)
    )
    for word in build_dynamic_source_leak_word_candidates(en_text):
        if source_word_leaks_into_translation(word, visible_translation):
            leaked.append(word)
    return leaked


def build_missing_known_phrase_rewrite_requirements(
    en_text, translated_text, lang_code
):
    missing_requirements = get_missing_known_phrase_requirements(
        en_text, translated_text, lang_code
    )
    if not missing_requirements:
        return ""

    requirement_lines = "\n".join(
        f"- `{item['source_phrase']}` -> `{item['known_translation']}`"
        for item in missing_requirements[:10]
    )
    return (
        "9. CRITICAL: The final translation must use the exact established target-language UI term(s) below when the matching source phrase appears in this key. Do NOT paraphrase these specific UI term(s).\n"
        f"Exact UI term requirement(s):\n{requirement_lines}\n"
    )


def repair_candidate_text_with_script_rewrite(lang_code, candidate_text, en_text):
    if not isinstance(candidate_text, str) or not candidate_text.strip():
        return (
            False,
            candidate_text,
            "no candidate text available for script rewrite repair",
        )

    placeholder_pairs = build_protected_placeholders(
        en_text, {lang_code: candidate_text}
    )
    suspicious_tokens = extract_suspicious_transliterated_or_foreign_tokens(
        lang_code, candidate_text, en_text, placeholder_pairs
    )
    unexpected_scripts = detect_unexpected_script_mixture(
        lang_code, candidate_text, en_text, placeholder_pairs
    )
    if not suspicious_tokens and not unexpected_scripts:
        return (
            False,
            candidate_text,
            "no suspicious foreign-script or transliterated fragments found for script rewrite repair",
        )

    language_name = get_language_name_for_code(lang_code)
    language_label = language_name if language_name else f"language code {lang_code}"
    prompt_en_text = apply_protected_placeholders(en_text, placeholder_pairs)
    prompt_candidate_text = apply_protected_placeholders(
        candidate_text, placeholder_pairs
    )
    prompt_en_text_block = build_prompt_text_block(
        "Original English Text", en_text, prompt_en_text
    )
    prompt_candidate_text_block = build_prompt_text_block(
        "Current Translation", candidate_text, prompt_candidate_text
    )
    protected_placeholders_json = get_protected_placeholders_prompt_block(
        placeholder_pairs
    )
    exact_known_phrase_requirements = build_missing_known_phrase_rewrite_requirements(
        en_text, candidate_text, lang_code
    )
    local_fix_guidance = (
        build_local_single_language_guidance(en_text, {lang_code: candidate_text})
        if resolve_backend_selection()
        else ""
    )
    allowed_scripts = sorted(build_allowed_script_families_for_lang(lang_code, en_text))
    allowed_scripts_text = (
        ", ".join(allowed_scripts) if allowed_scripts else "the target-language script"
    )
    suspicious_lines = (
        "\n".join(f"- `{token}`" for token in suspicious_tokens[:12])
        if suspicious_tokens
        else "- None"
    )

    prompt = f"""
You are a professional translator and translation quality controller for the eMule software.
Rewrite the current translation so that it stays fully in the target language and does not contain foreign-script or transliterated fragments.

KEY language code: '{lang_code}'
KEY language name: '{language_label}'
{prompt_en_text_block}

{prompt_candidate_text_block}

Rules:
1. Return ONLY one plain text line in this exact format: `corrected_translation<TAB>your_fixed_translation_string_here`.
2. The separator must be a real TAB character.
3. Preserve all placeholders, protected placeholders, escape sequences, digits, protected brands, and file names copied from the source exactly.
4. Keep the text in `{language_label}` only. Do NOT leave ordinary target-language words written in another script or in Latin transliteration.
5. Do NOT explain anything. Do NOT return JSON, Markdown, notes, bullets, or extra lines.
6. Keep already-correct translated wording when possible and rewrite only what is necessary to remove the foreign-script or transliterated snippets naturally.
7. Use only these allowed script families for normal letters: `{allowed_scripts_text}`.
8. Do NOT keep the suspicious snippets below in the final line.
Suspicious foreign-script or transliterated snippet(s) that MUST be removed or rewritten:
{suspicious_lines}
{exact_known_phrase_requirements}{local_fix_guidance}
LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}
"""

    result_text = call_active_api(prompt, plain_text=True)
    if not result_text:
        return False, candidate_text, "script rewrite repair returned empty response"

    try:
        parsed = parse_line_based_updates_response(
            result_text.strip(), en_text, {"corrected_translation"}
        )
        corrected = parsed.get("corrected_translation", "")
    except Exception:
        corrected = extract_single_translation_candidate_from_plain_text(
            result_text, en_text
        )

    if not isinstance(corrected, str) or not corrected.strip():
        return (
            False,
            candidate_text,
            "script rewrite repair returned no corrected translation",
        )

    corrected_restored = restore_protected_placeholders(corrected, placeholder_pairs)
    corrected_cleaned = cleanup_translated_text(
        en_text, corrected_restored, placeholder_pairs, lang_code
    )
    is_valid, validation_message = validate_translation_text(
        en_text, corrected_cleaned, placeholder_pairs, lang_code
    )
    if not is_valid:
        return False, corrected_cleaned, validation_message

    return True, corrected_cleaned, ""


def extract_single_translation_candidate_from_plain_text(result_text, source_text=""):
    if not isinstance(result_text, str):
        return ""

    normalized = preprocess_line_response_text(result_text, expected_lang_codes=None)
    if not isinstance(normalized, str):
        return ""
    normalized = normalized.strip()
    if not normalized:
        return ""

    source_text_stripped = source_text.strip() if isinstance(source_text, str) else ""

    candidate_lines = []
    line_count = len(normalized.splitlines())
    for raw_line in normalized.splitlines():
        stripped = raw_line.strip()
        if not stripped:
            continue
        if stripped.startswith("```"):
            continue
        if stripped.startswith("- "):
            stripped = stripped[2:].strip()

        parts = re.split(r"\\t|\t", stripped, maxsplit=1)
        if len(parts) == 2 and parts[1].strip():
            label = parts[0].strip().strip("\"'")
            if (
                label.lower() in {"translated_phrase", "corrected_translation"}
                or label == source_text_stripped
                or line_count == 1
            ):
                return normalize_placeholder_quote_style(source_text, parts[1].strip())

        if stripped.lower().startswith("translated_phrase"):
            parts = re.split(r"\t|	", stripped, maxsplit=1)
            if len(parts) == 2 and parts[1].strip():
                return normalize_placeholder_quote_style(source_text, parts[1].strip())
            stripped = re.sub(
                r"^translated_phrase\s*[:=-]?\s*", "", stripped, flags=re.IGNORECASE
            ).strip()
        if stripped.lower().startswith("corrected_translation"):
            parts = re.split(r"\t|	", stripped, maxsplit=1)
            if len(parts) == 2 and parts[1].strip():
                return normalize_placeholder_quote_style(source_text, parts[1].strip())
            stripped = re.sub(
                r"^corrected_translation\s*[:=-]?\s*", "", stripped, flags=re.IGNORECASE
            ).strip()
        candidate_lines.append(stripped)

    if not candidate_lines:
        return ""

    if len(candidate_lines) >= 2 and re.fullmatch(
        LANGUAGE_CODE_PATTERN, candidate_lines[0]
    ):
        candidate = candidate_lines[1]
    else:
        candidate = (
            candidate_lines[0]
            if len(candidate_lines) == 1
            else " ".join(candidate_lines)
        )

    candidate = candidate.strip().rstrip(",")
    if (
        len(candidate) >= 2
        and candidate[0] == candidate[-1]
        and candidate[0] in ('"', "'")
    ):
        candidate = candidate[1:-1].strip()
    if not candidate:
        return ""
    if looks_like_source_echo_line(candidate, source_text):
        return ""

    return normalize_placeholder_quote_style(source_text, candidate)


def validate_ui_phrase_translation(lang_code, source_phrase, translated_phrase):
    if not isinstance(source_phrase, str) or not isinstance(translated_phrase, str):
        return False, "", "phrase translation is not a string"

    normalized_source_phrase = source_phrase.strip()
    normalized_translation = translated_phrase.strip()
    if not normalized_source_phrase or not normalized_translation:
        return False, "", "phrase translation is empty"
    if normalized_translation.casefold() == normalized_source_phrase.casefold():
        return False, "", "phrase translation is identical to the source phrase"
    if normalized_source_phrase.casefold() in normalized_translation.casefold():
        return (
            False,
            "",
            "phrase translation still contains the source phrase in English",
        )

    placeholder_pairs = build_protected_placeholders(
        normalized_source_phrase, {lang_code: normalized_translation}
    )
    cleaned_phrase = cleanup_translated_text(
        normalized_source_phrase, normalized_translation, placeholder_pairs, lang_code
    )
    if not cleaned_phrase.strip():
        return False, "", "phrase translation became empty after cleanup"

    if contains_response_artifact_lines(cleaned_phrase):
        return False, "", "phrase translation contains response-format artifacts"
    structural_artifact_message = detect_structural_translation_artifact(cleaned_phrase)
    if structural_artifact_message:
        return False, "", structural_artifact_message
    repetitive_token = detect_repetitive_token_flood(cleaned_phrase)
    if repetitive_token:
        return (
            False,
            "",
            f"phrase translation contains repetitive token flood artifact ({repetitive_token})",
        )
    if contains_embedded_english_phrase_leak(
        normalized_source_phrase, cleaned_phrase, lang_code
    ):
        return (
            False,
            "",
            "phrase translation still contains untranslated embedded English phrase(s) from the source phrase",
        )
    suspicious_tokens = extract_suspicious_transliterated_or_foreign_tokens(
        lang_code, cleaned_phrase, normalized_source_phrase, placeholder_pairs
    )
    if suspicious_tokens:
        return (
            False,
            "",
            f"phrase translation contains suspicious transliterated or foreign token(s) ({', '.join(suspicious_tokens[:3])})",
        )
    unexpected_scripts = detect_unexpected_script_mixture(
        lang_code, cleaned_phrase, normalized_source_phrase, placeholder_pairs
    )
    if unexpected_scripts:
        return (
            False,
            "",
            f"phrase translation contains unexpected script mixture ({unexpected_scripts})",
        )
    return True, cleaned_phrase, ""


def parse_phrase_translation_batch_response(result_text, phrase_ids):
    if not isinstance(result_text, str):
        raise ValueError("Could not parse phrase translation batch response")

    valid_ids = {phrase_id for phrase_id in phrase_ids if isinstance(phrase_id, str)}
    normalized = result_text.replace("\r\n", "\n").replace("\r", "\n").strip()
    if not normalized:
        raise ValueError("Could not parse phrase translation batch response")

    results = {}
    for raw_line in normalized.splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("```"):
            continue

        match = re.match(r"^(P\d+)\s*(?:\t|\\t)\s*(.+)$", stripped)
        if not match:
            match = re.match(r'^"?((?:P\d+))"?\s*:\s*(.+?)\s*,?$', stripped)
        if not match:
            continue

        phrase_id = match.group(1).strip()
        if phrase_id not in valid_ids:
            continue

        translation = match.group(2).strip().rstrip(",")
        if (
            len(translation) >= 2
            and translation[0] == translation[-1]
            and translation[0] in ('"', "'")
        ):
            translation = translation[1:-1].strip()
        if not translation:
            continue

        results[phrase_id] = translation

    if results:
        return results
    raise ValueError("Could not parse phrase translation batch response")


def parse_multilang_phrase_translation_batch_response(result_text, valid_pairs):
    if not isinstance(result_text, str):
        raise ValueError("Could not parse multi-language phrase translation batch response")

    normalized = result_text.replace("\r\n", "\n").replace("\r", "\n").strip()
    if not normalized:
        raise ValueError("Could not parse multi-language phrase translation batch response")

    valid_pairs = {
        (lang_code, phrase_id)
        for lang_code, phrase_id in valid_pairs
        if isinstance(lang_code, str) and isinstance(phrase_id, str)
    }
    results = {}

    for raw_line in normalized.splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("```"):
            continue

        match = re.match(
            r"^([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)\s*(?:\t|\\t)\s*(P\d+)\s*(?:\t|\\t)\s*(.+)$",
            stripped,
        )
        if not match:
            continue

        lang_code = match.group(1).strip()
        phrase_id = match.group(2).strip()
        if (lang_code, phrase_id) not in valid_pairs:
            continue

        translation = match.group(3).strip().rstrip(",")
        if (
            len(translation) >= 2
            and translation[0] == translation[-1]
            and translation[0] in ('"', "'")
        ):
            translation = translation[1:-1].strip()
        if not translation:
            continue

        results[(lang_code, phrase_id)] = translation

    if results:
        return results
    raise ValueError("Could not parse multi-language phrase translation batch response")


def translate_ui_phrases_for_batch(prompt_lang_dict, source_phrases):
    if not isinstance(prompt_lang_dict, dict) or not prompt_lang_dict:
        return {}

    phrase_list = []
    seen_phrases = set()
    for source_phrase in source_phrases:
        if not isinstance(source_phrase, str) or not source_phrase.strip():
            continue
        normalized_phrase = source_phrase.strip()
        normalized_cf = normalized_phrase.casefold()
        if normalized_cf in seen_phrases:
            continue
        seen_phrases.add(normalized_cf)
        phrase_list.append(normalized_phrase)

    if not phrase_list:
        return {}

    translations = {}
    pending_by_lang = {}

    for lang_code in sorted(prompt_lang_dict.keys()):
        if lang_code == "en":
            continue

        lang_translations = {}
        pending_phrases = []
        for source_phrase in phrase_list:
            cache_key = (lang_code, source_phrase)
            cached_translation = _UI_PHRASE_TRANSLATION_CACHE.get(cache_key)
            if isinstance(cached_translation, str) and cached_translation.strip():
                lang_translations[source_phrase] = cached_translation
                continue

            known_translation = get_known_translation_from_memory(
                lang_code, source_phrase
            )
            if known_translation:
                valid, cleaned, _ = validate_ui_phrase_translation(
                    lang_code, source_phrase, known_translation
                )
                if valid:
                    _UI_PHRASE_TRANSLATION_CACHE[cache_key] = cleaned
                    lang_translations[source_phrase] = cleaned
                    continue

            pending_phrases.append(source_phrase)

        if lang_translations:
            translations[lang_code] = lang_translations
        if pending_phrases:
            pending_by_lang[lang_code] = pending_phrases

    if not pending_by_lang:
        return translations

    phrase_id_map = {
        f"P{index + 1}": phrase for index, phrase in enumerate(phrase_list)
    }
    target_language_map = {
        lang_code: (get_language_name_for_code(lang_code) or lang_code)
        for lang_code in sorted(pending_by_lang.keys())
    }
    preferred_script_map = {
        lang_code: list(build_allowed_script_families_for_lang(lang_code, ""))
        for lang_code in sorted(pending_by_lang.keys())
        if build_allowed_script_families_for_lang(lang_code, "")
    }
    language_specific_requirements = "".join(
        build_language_specific_requirements(lang_code)
        for lang_code in sorted(pending_by_lang.keys())
    )

    last_error = ""
    pending_pairs = {
        (lang_code, phrase_id)
        for lang_code, phrase_list_for_lang in pending_by_lang.items()
        for phrase_id, phrase_text in phrase_id_map.items()
        if phrase_text in phrase_list_for_lang
    }

    for attempt in range(1, 4):
        attempt_pairs = set(pending_pairs)
        retry_guidance = ""
        if last_error:
            retry_guidance = (
                "10. IMPORTANT: The previous attempt was invalid or incomplete. "
                f"Fix this exact problem and do not repeat it: {last_error}\n"
            )

        prompt = f"""
You are a professional UI translator for the eMule software.
Translate each phrase ID into the correct target language for each language code in the JSON maps below.
Rules:
1. Return ONLY plain text lines in this exact format: `language_code<TAB>phrase_id<TAB>translated_phrase`.
2. Use a real TAB character between fields.
3. Return only the language codes listed in the target language map and only the phrase IDs listed in the phrase ID JSON.
4. Translate only the UI phrase itself. Do NOT return a full sentence.
5. Do NOT leave the English source phrase unchanged.
6. Do NOT return JSON, Markdown, explanations, notes, numbering, bullets, or extra lines.
7. Each `language_code` must be translated only into the language shown for that code in the target language map.
8. If a preferred script map is provided for a language code, use only those script families for normal translated words unless the source token is a protected brand, acronym, placeholder, or file name copied unchanged from the source.
9. One output line translates one `phrase_id` for one `language_code`.
{retry_guidance}{language_specific_requirements}Target language map JSON:
{json.dumps(target_language_map, ensure_ascii=False, indent=2)}

Preferred script map JSON:
{json.dumps(preferred_script_map, ensure_ascii=False, indent=2)}

Phrase ID JSON:
{json.dumps(phrase_id_map, ensure_ascii=False, indent=2)}

Response format example:
so\tP1\tTranslated phrase here
sq\tP1\tTranslated phrase here
so\tP2\tAnother translated phrase here
"""
        result_text = call_active_api(prompt, plain_text=True)
        if not result_text:
            last_error = "multi-language phrase batch request returned empty response"
            continue

        try:
            parsed = parse_multilang_phrase_translation_batch_response(
                result_text.strip(), attempt_pairs
            )
        except Exception:
            last_error = "multi-language phrase batch parser returned no usable candidates"
            continue

        valid_results = {}
        invalid_reasons = []
        for lang_code, phrase_id in sorted(attempt_pairs):
            phrase_text = phrase_id_map.get(phrase_id, "")
            translated_phrase = parsed.get((lang_code, phrase_id), "")
            if not isinstance(translated_phrase, str) or not translated_phrase.strip():
                invalid_reasons.append(f"{lang_code}/{phrase_text}: missing translation")
                continue

            valid, cleaned, reason = validate_ui_phrase_translation(
                lang_code, phrase_text, translated_phrase
            )
            if not valid:
                invalid_reasons.append(
                    f"{lang_code}/{phrase_text}: {reason or 'phrase translation validation failed'}"
                )
                continue

            valid_results[(lang_code, phrase_id, phrase_text)] = cleaned

        for (lang_code, phrase_id, phrase_text), cleaned in valid_results.items():
            cache_key = (lang_code, phrase_text)
            _UI_PHRASE_TRANSLATION_CACHE[cache_key] = cleaned
            translations.setdefault(lang_code, {})[phrase_text] = cleaned
            pending_pairs.discard((lang_code, phrase_id))

        if not pending_pairs:
            return translations

        last_error = "; ".join(invalid_reasons[:6]) or "multi-language phrase batch remained incomplete"

    return translations


def translate_ui_phrases_for_language(lang_code, source_phrases):
    if not isinstance(lang_code, str) or not lang_code:
        return {}

    translations = {}
    pending = []
    seen_phrases = set()

    for source_phrase in source_phrases:
        if not isinstance(source_phrase, str) or not source_phrase.strip():
            continue
        normalized_phrase = source_phrase.strip()
        normalized_cf = normalized_phrase.casefold()
        if normalized_cf in seen_phrases:
            continue
        seen_phrases.add(normalized_cf)

        cache_key = (lang_code, normalized_phrase)
        cached_translation = _UI_PHRASE_TRANSLATION_CACHE.get(cache_key)
        if isinstance(cached_translation, str) and cached_translation.strip():
            translations[normalized_phrase] = cached_translation
            continue

        known_translation = get_known_translation_from_memory(
            lang_code, normalized_phrase
        )
        if known_translation:
            valid, cleaned, _ = validate_ui_phrase_translation(
                lang_code, normalized_phrase, known_translation
            )
            if valid:
                _UI_PHRASE_TRANSLATION_CACHE[cache_key] = cleaned
                translations[normalized_phrase] = cleaned
                continue

        pending.append(normalized_phrase)

    if not pending:
        return translations

    language_name = get_language_name_for_code(lang_code)
    language_label = language_name if language_name else f"language code {lang_code}"
    language_specific_requirements = build_language_specific_requirements(lang_code)
    allowed_scripts = sorted(build_allowed_script_families_for_lang(lang_code, ""))
    script_rule = ""
    if allowed_scripts:
        if len(allowed_scripts) == 1:
            script_rule = (
                f"8. Write the translations using only the `{allowed_scripts[0]}` script for normal letters. "
                "Do NOT switch to Latin or a neighboring script unless the exact source token is a protected brand, acronym, placeholder, or file name copied unchanged from the source.\n"
            )
        else:
            script_rule = (
                "8. Write the translations using only these script families for normal letters: "
                f"{', '.join(allowed_scripts)}. "
                "Do NOT use letters from any other scripts unless the exact source token is a protected brand, acronym, placeholder, or file name copied unchanged from the source.\n"
            )

    last_error = ""
    for attempt in range(1, 4):
        phrase_id_map = {
            f"P{index + 1}": phrase for index, phrase in enumerate(pending)
        }
        retry_guidance = ""
        if last_error:
            retry_guidance = (
                "9. IMPORTANT: The previous attempt was invalid or incomplete. "
                f"Fix this exact problem and do not repeat it: {last_error}\n"
            )

        prompt = f"""
You are a professional UI translator for the eMule software.
Target language code: '{lang_code}'
Target language name: '{language_label}'
Translate each English UI phrase in the JSON map into the target language.
Rules:
1. Return ONLY plain text lines in this exact format: `phrase_id<TAB>translated_phrase`.
2. The separator must be a real TAB character.
3. Return only the phrase IDs listed in the JSON map. Do NOT return the English phrase itself as the key.
4. Translate only the UI phrase itself. Do NOT return a full sentence.
5. Do NOT leave the English source phrase unchanged.
6. Do NOT return JSON, Markdown, explanations, notes, numbering, bullets, or extra lines.
7. Translate into {language_label}, not into English and not into a neighboring or more common language.
{script_rule}{retry_guidance}{language_specific_requirements}Phrase ID JSON:
{json.dumps(phrase_id_map, ensure_ascii=False, indent=2)}

Response format example:
P1\tTranslated phrase here
P2\tTranslated phrase here
"""
        result_text = call_active_api(prompt, plain_text=True)
        if not result_text:
            last_error = "phrase batch translation request returned empty response"
            continue

        try:
            parsed = parse_phrase_translation_batch_response(
                result_text.strip(), phrase_id_map.keys()
            )
        except Exception:
            last_error = "phrase batch translation parser returned no usable candidates"
            continue

        valid_results = {}
        invalid_reasons = []
        for phrase_id, source_phrase in phrase_id_map.items():
            translated_phrase = parsed.get(phrase_id, "")
            if not isinstance(translated_phrase, str) or not translated_phrase.strip():
                invalid_reasons.append(f"{source_phrase}: missing translation")
                continue

            valid, cleaned, reason = validate_ui_phrase_translation(
                lang_code, source_phrase, translated_phrase
            )
            if not valid:
                invalid_reasons.append(
                    f"{source_phrase}: {reason or 'phrase translation validation failed'}"
                )
                continue

            cache_key = (lang_code, source_phrase)
            _UI_PHRASE_TRANSLATION_CACHE[cache_key] = cleaned
            valid_results[source_phrase] = cleaned

        translations.update(valid_results)
        pending = [
            source_phrase
            for source_phrase in pending
            if source_phrase not in valid_results
        ]
        if not pending:
            return translations

        last_error = "; ".join(invalid_reasons[:4]) or "phrase batch translation remained incomplete"

    return translations


def translate_ui_phrase_for_language(lang_code, source_phrase):
    if not isinstance(source_phrase, str) or not source_phrase.strip():
        return None

    translated_map = translate_ui_phrases_for_language(lang_code, [source_phrase])
    if isinstance(translated_map, dict):
        translated_phrase = translated_map.get(source_phrase.strip(), "")
        if isinstance(translated_phrase, str) and translated_phrase.strip():
            return translated_phrase

    language_name = get_language_name_for_code(lang_code)
    language_label = language_name if language_name else f"language code {lang_code}"
    language_specific_requirements = build_language_specific_requirements(lang_code)
    context_examples = get_memory_context_examples(lang_code, source_phrase, limit=2)
    example_block = ""
    if context_examples:
        example_lines = "\n".join(
            f"- `{example_en}` -> `{example_translated}`"
            for example_en, example_translated in context_examples
        )
        example_block = f"7. Use the style of these existing {language_label} translations from translations.map when helpful:\n{example_lines}\n"

    allowed_scripts = sorted(
        build_allowed_script_families_for_lang(lang_code, source_phrase)
    )
    script_rule = ""
    if allowed_scripts:
        if len(allowed_scripts) == 1:
            script_rule = f"8. Write the translation using only the `{allowed_scripts[0]}` script for normal letters. Do NOT switch to Latin or a neighboring script unless the exact source token is a protected brand, acronym, placeholder, or file name copied unchanged from the source.\n"
        else:
            script_rule = f"8. Write the translation using only these script families for normal letters: {', '.join(allowed_scripts)}. Do NOT use letters from any other scripts unless the exact source token is a protected brand, acronym, placeholder, or file name copied unchanged from the source.\n"

    last_error = ""
    for attempt in range(1, 4):
        retry_guidance = ""
        if last_error:
            retry_guidance = f"9. IMPORTANT: The previous attempt was invalid. Fix this exact problem and do not repeat it: {last_error}\n"

        prompt = f"""
You are a professional UI translator for the eMule software.
Target language code: '{lang_code}'
Target language name: '{language_label}'
Translate the following English UI phrase into the target language.
Phrase:
{source_phrase}
Rules:
1. Return ONLY one plain text line in this exact format: `translated_phrase<TAB>your_translation_here`.
2. The separator must be a real TAB character.
3. Do NOT return the English source phrase unchanged.
4. Do NOT return a full sentence. Translate only the UI phrase itself.
5. Do NOT return JSON, Markdown, explanations, examples, notes, or extra lines.
6. Translate into {language_label}, not into English and not into a neighboring or more common language.
7. Do NOT append pronunciation, transliteration, romanization, or glosses in parentheses.
{script_rule}{retry_guidance}{example_block}{language_specific_requirements}
"""
        result_text = call_active_api(prompt, plain_text=True)
        if not result_text:
            last_error = "phrase translation request returned empty response"
            continue

        translated_phrase = ""
        try:
            parsed = parse_line_based_updates_response(
                result_text.strip(), source_phrase, {"translated_phrase"}
            )
            translated_phrase = parsed.get("translated_phrase", "")
        except Exception:
            translated_phrase = extract_single_translation_candidate_from_plain_text(
                result_text, source_phrase
            )

        if not isinstance(translated_phrase, str) or not translated_phrase.strip():
            last_error = "phrase translation parser returned no usable candidate"
            continue

        valid, cleaned, reason = validate_ui_phrase_translation(
            lang_code, source_phrase, translated_phrase
        )
        if valid:
            cache_key = (lang_code, source_phrase.strip())
            _UI_PHRASE_TRANSLATION_CACHE[cache_key] = cleaned
            return cleaned

        last_error = reason or "phrase translation validation failed"

    return None


def replace_source_fragment_case_insensitive(
    text, source_fragment, replacement, require_word_boundaries=False
):
    if (
        not isinstance(text, str)
        or not isinstance(source_fragment, str)
        or not source_fragment
    ):
        return text, False
    if require_word_boundaries:
        pattern = re.compile(
            rf"(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(source_fragment)}(?!{NON_WORD_BOUNDARY_PATTERN})",
            re.IGNORECASE,
        )
    else:
        pattern = re.compile(re.escape(source_fragment), re.IGNORECASE)
    new_text, count = pattern.subn(replacement, text)
    return new_text, count > 0


def repair_candidate_text_in_memory(lang_code, candidate_text, en_text):
    leaked_phrases = get_leaked_source_phrases(en_text, candidate_text, lang_code)
    leaked_words = get_leaked_source_words(en_text, candidate_text, lang_code)
    if not leaked_phrases and not leaked_words:
        return (
            False,
            candidate_text,
            "no leaked source phrase or word found for phrase-only repair",
        )

    repaired_text = candidate_text
    repaired_any = False

    for phrase in leaked_phrases:
        translated_phrase = translate_ui_phrase_for_language(lang_code, phrase)
        if not translated_phrase:
            return (
                False,
                candidate_text,
                f"phrase-only repair could not translate UI phrase ({phrase})",
            )
        repaired_text, changed = replace_source_fragment_case_insensitive(
            repaired_text,
            phrase,
            translated_phrase,
            require_word_boundaries=(" " not in phrase),
        )
        repaired_any = repaired_any or changed

    for word in leaked_words:
        if any(word.casefold() == phrase.casefold() for phrase in leaked_phrases):
            continue
        translated_word = translate_ui_phrase_for_language(lang_code, word)
        if not translated_word:
            continue
        repaired_text, changed = replace_source_fragment_case_insensitive(
            repaired_text, word, translated_word, require_word_boundaries=True
        )
        repaired_any = repaired_any or changed

    if not repaired_any or repaired_text == candidate_text:
        return (
            False,
            candidate_text,
            "phrase-only repair could not safely replace leaked source fragments",
        )

    placeholder_pairs = build_protected_placeholders(
        en_text, {lang_code: repaired_text}
    )
    cleaned_text = cleanup_translated_text(
        en_text, repaired_text, placeholder_pairs, lang_code
    )
    is_valid, validation_message = validate_translation_text(
        en_text, cleaned_text, placeholder_pairs, lang_code
    )
    if not is_valid:
        return False, cleaned_text, validation_message

    return True, cleaned_text, ""


def repair_candidate_text_with_known_phrase_rewrite(
    lang_code, candidate_text, en_text
):
    missing_requirements = get_missing_known_phrase_requirements(
        en_text, candidate_text, lang_code
    )
    if not missing_requirements:
        return (
            False,
            candidate_text,
            "no exact established UI term mismatch found for known-phrase rewrite",
        )

    language_name = get_language_name_for_code(lang_code)
    language_label = language_name if language_name else f"language code {lang_code}"
    placeholder_pairs = build_protected_placeholders(
        en_text, {lang_code: candidate_text}
    )
    prompt_en_text = apply_protected_placeholders(en_text, placeholder_pairs)
    prompt_candidate_text = apply_protected_placeholders(
        candidate_text, placeholder_pairs
    )
    prompt_en_text_block = build_prompt_text_block(
        "Original English Text", en_text, prompt_en_text
    )
    prompt_candidate_text_block = build_prompt_text_block(
        "Current Translation", candidate_text, prompt_candidate_text
    )
    protected_placeholders_json = get_protected_placeholders_prompt_block(
        placeholder_pairs
    )
    requirement_lines = "\n".join(
        f"- `{item['source_phrase']}` -> `{item['known_translation']}`"
        for item in missing_requirements[:10]
    )
    language_specific_requirements = build_language_specific_requirements(lang_code)
    local_fix_guidance = (
        build_local_single_language_guidance(en_text, {lang_code: candidate_text})
        if resolve_backend_selection()
        else ""
    )

    prompt = f"""
You are a professional translator and translation quality controller for the eMule software.
Rewrite the current translation so that it preserves the meaning while using the exact established target-language UI term(s) required below.

KEY language code: '{lang_code}'
KEY language name: '{language_label}'
{prompt_en_text_block}

{prompt_candidate_text_block}

Exact established UI term(s) that MUST appear exactly in the final line:
{requirement_lines}

Rules:
1. Return ONLY one plain text line in this exact format: `corrected_translation<TAB>your_fixed_translation_string_here`.
2. The separator must be a real TAB character.
3. Preserve all placeholders, protected placeholders, escape sequences, digits, and protected brands exactly.
4. Use each required target-language UI term exactly as written above when the matching source phrase appears in this key.
5. Keep the text in `{language_label}` only. Do NOT switch to English or a neighboring language.
6. Do NOT explain anything. Do NOT return JSON, Markdown, notes, bullets, or extra lines.
7. Keep already-correct translated wording when possible and only rewrite what is needed to satisfy the exact UI term requirement(s).
{language_specific_requirements}{local_fix_guidance}{build_placeholder_format_rule(en_text, rule_number="8")}
LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}
"""

    result_text = call_active_api(prompt, plain_text=True)
    if not result_text:
        return False, candidate_text, "known-phrase rewrite repair returned empty response"

    try:
        corrected = ""
        try:
            parsed = parse_line_based_updates_response(
                result_text.strip(), en_text, {"corrected_translation"}
            )
            corrected = parsed.get("corrected_translation", "")
        except Exception:
            corrected = extract_single_translation_candidate_from_plain_text(
                result_text, en_text
            )
        if not isinstance(corrected, str) or not corrected.strip():
            return (
                False,
                candidate_text,
                "known-phrase rewrite repair returned no corrected translation",
            )

        corrected_restored = restore_protected_placeholders(
            corrected, placeholder_pairs
        )
        corrected_cleaned = cleanup_translated_text(
            en_text, corrected_restored, placeholder_pairs, lang_code
        )
        is_valid, validation_message = validate_translation_text(
            en_text, corrected_cleaned, placeholder_pairs, lang_code
        )
        if not is_valid:
            return False, corrected_cleaned, validation_message
        return True, corrected_cleaned, ""
    except Exception as parse_error:
        return (
            False,
            candidate_text,
            f"known-phrase rewrite repair parse failed ({parse_error})",
        )


def repair_candidate_text_with_targeted_rewrite(lang_code, candidate_text, en_text):
    leaked_phrases = get_leaked_source_phrases(en_text, candidate_text, lang_code)
    leaked_words = get_leaked_source_words(en_text, candidate_text, lang_code)
    if not leaked_phrases and not leaked_words:
        return (
            False,
            candidate_text,
            "no leaked source phrase or word found for targeted rewrite repair",
        )

    language_name = get_language_name_for_code(lang_code)
    language_label = language_name if language_name else f"language code {lang_code}"
    placeholder_pairs = build_protected_placeholders(
        en_text, {lang_code: candidate_text}
    )
    prompt_en_text = apply_protected_placeholders(en_text, placeholder_pairs)
    prompt_candidate_text = apply_protected_placeholders(
        candidate_text, placeholder_pairs
    )
    prompt_en_text_block = build_prompt_text_block(
        "Original English Text", en_text, prompt_en_text
    )
    prompt_candidate_text_block = build_prompt_text_block(
        "Current Translation", candidate_text, prompt_candidate_text
    )
    protected_placeholders_json = get_protected_placeholders_prompt_block(
        placeholder_pairs
    )
    leaked_phrase_lines = (
        "\n".join(f"- `{phrase}`" for phrase in leaked_phrases[:10])
        if leaked_phrases
        else "- None"
    )
    leaked_word_lines = (
        "\n".join(f"- `{word}`" for word in leaked_words[:10])
        if leaked_words
        else "- None"
    )
    language_specific_requirements = build_language_specific_requirements(lang_code)
    local_fix_guidance = (
        build_local_single_language_guidance(en_text, {lang_code: candidate_text})
        if resolve_backend_selection()
        else ""
    )

    prompt = f"""
You are a professional translator and translation quality controller for the eMule software.
Rewrite the current translation so that every remaining raw English UI fragment is translated naturally into the target language.
Keep the already-correct translated parts whenever possible.

KEY language code: '{lang_code}'
KEY language name: '{language_label}'
{prompt_en_text_block}

{prompt_candidate_text_block}

Remaining leaked English phrase(s) that MUST be translated:
{leaked_phrase_lines}

Remaining leaked English word(s) that MUST be translated if they still appear raw in the final line:
{leaked_word_lines}

Rules:
1. Return ONLY one plain text line in this exact format: `corrected_translation<TAB>your_fixed_translation_string_here`.
2. The separator must be a real TAB character.
3. Preserve all placeholders, escape sequences, digits, and protected placeholders exactly.
4. Keep the text in `{language_label}` only. Do NOT switch to English or a neighboring language.
5. Do NOT explain anything. Do NOT return JSON, Markdown, notes, bullets, or extra lines.
6. Do NOT keep the leaked English fragments listed above anywhere in the final line.
7. Do NOT append the English original in parentheses after translated labels or mode names.
8. Keep already-correct translated wording when possible and only rewrite what is needed to remove the leaked English fragments naturally.
{language_specific_requirements}{local_fix_guidance}{build_placeholder_format_rule(en_text, rule_number="9")}
LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}
"""

    result_text = call_active_api(prompt, plain_text=True)
    if not result_text:
        return False, candidate_text, "targeted rewrite repair returned empty response"

    try:
        corrected = ""
        try:
            parsed = parse_line_based_updates_response(
                result_text.strip(), en_text, {"corrected_translation"}
            )
            corrected = parsed.get("corrected_translation", "")
        except Exception:
            corrected = extract_single_translation_candidate_from_plain_text(
                result_text, en_text
            )
        if not isinstance(corrected, str) or not corrected.strip():
            return (
                False,
                candidate_text,
                "targeted rewrite repair returned no corrected translation",
            )

        corrected_restored = restore_protected_placeholders(
            corrected, placeholder_pairs
        )
        corrected_cleaned = cleanup_translated_text(
            en_text, corrected_restored, placeholder_pairs, lang_code
        )
        is_valid, validation_message = validate_translation_text(
            en_text, corrected_cleaned, placeholder_pairs, lang_code
        )
        if not is_valid:
            return False, corrected_cleaned, validation_message
        return True, corrected_cleaned, ""
    except Exception as parse_error:
        return (
            False,
            candidate_text,
            f"targeted rewrite repair parse failed ({parse_error})",
        )


def try_phrase_only_repair(key_name, lang_code, candidate_text, en_text):
    placeholder_pairs = build_protected_placeholders(
        en_text, {lang_code: candidate_text}
    )
    cleaned_text = cleanup_translated_text(
        en_text, candidate_text, placeholder_pairs, lang_code
    )
    if cleaned_text != candidate_text:
        is_valid, validation_message = validate_translation_text(
            en_text, cleaned_text, placeholder_pairs, lang_code
        )
        if is_valid:
            success, msg = update_translation_via_compiler(
                key_name, lang_code, cleaned_text, en_text
            )
            if success:
                return True, cleaned_text, msg

    repaired_success, repaired_text, repaired_msg = (
        repair_candidate_text_with_script_rewrite(lang_code, candidate_text, en_text)
    )
    if not repaired_success:
        repaired_success, repaired_text, repaired_msg = (
            repair_candidate_text_with_known_phrase_rewrite(
                lang_code, candidate_text, en_text
            )
        )
    if not repaired_success:
        repaired_success, repaired_text, repaired_msg = repair_candidate_text_in_memory(
            lang_code, candidate_text, en_text
        )
    if not repaired_success:
        repaired_success, repaired_text, repaired_msg = (
            repair_candidate_text_with_targeted_rewrite(
                lang_code, candidate_text, en_text
            )
        )
        if not repaired_success:
            return False, repaired_text, repaired_msg

    success, msg = update_translation_via_compiler(
        key_name, lang_code, repaired_text, en_text
    )
    if success:
        return True, repaired_text, msg
    return False, repaired_text, msg


def repair_candidate_text_for_validation(
    lang_code, candidate_text, en_text, validation_error=""
):
    placeholder_pairs = build_protected_placeholders(
        en_text, {lang_code: candidate_text}
    )
    cleaned_text = cleanup_translated_text(
        en_text, candidate_text, placeholder_pairs, lang_code
    )
    is_valid, cleaned_validation_error = validate_translation_text(
        en_text, cleaned_text, placeholder_pairs, lang_code
    )
    if is_valid:
        return True, cleaned_text, ""

    last_error = cleaned_validation_error or validation_error or ""
    repair_attempts = (
        repair_candidate_text_with_script_rewrite,
        repair_candidate_text_with_known_phrase_rewrite,
        repair_candidate_text_in_memory,
        repair_candidate_text_with_targeted_rewrite,
    )
    current_text = cleaned_text

    for repair_func in repair_attempts:
        repaired_success, repaired_text, repaired_error = repair_func(
            lang_code, current_text, en_text
        )
        if repaired_success:
            return True, repaired_text, ""
        if isinstance(repaired_text, str) and repaired_text.strip():
            current_text = repaired_text
        if isinstance(repaired_error, str) and repaired_error.strip():
            last_error = repaired_error

    return False, current_text, last_error


def contains_embedded_english_phrase_leak(en_text, translated_text, lang_code=""):
    if not isinstance(en_text, str) or not isinstance(translated_text, str):
        return False
    if not en_text or not translated_text or translated_text == en_text:
        return False

    return bool(get_leaked_source_phrases(en_text, translated_text, lang_code))


def build_embedded_phrase_fix_requirements(en_text, translated_text, lang_code=""):
    base_requirements = build_source_phrase_translation_requirements(en_text)
    if not isinstance(translated_text, str):
        return base_requirements

    leaked_phrases = get_leaked_source_phrases(en_text, translated_text, lang_code)
    leaked_words = get_leaked_source_words(en_text, translated_text, lang_code)
    if not leaked_phrases and not leaked_words:
        return base_requirements

    requirement_text = base_requirements
    if leaked_phrases:
        leaked_phrase_lines = "\n".join(
            f"- `{phrase}`" for phrase in leaked_phrases[:10]
        )
        requirement_text += (
            "15. CRITICAL: The current translation still contains untranslated English phrase(s) copied from the source. You must translate these phrase(s) naturally into the target language instead of leaving them in English.\n"
            f"Leaked English phrase(s):\n{leaked_phrase_lines}\n"
        )
    if leaked_words:
        leaked_word_lines = "\n".join(f"- `{word}`" for word in leaked_words[:10])
        requirement_text += (
            "16. CRITICAL: The current translation still contains untranslated English word(s) copied from the source. Translate these ordinary UI/content words naturally into the target language and do not leave them in English.\n"
            f"Leaked English word(s):\n{leaked_word_lines}\n"
        )
    return requirement_text


def build_language_specific_requirements(lang_code):
    if not isinstance(lang_code, str) or not lang_code:
        return ""
    language_name = get_language_name_for_code(lang_code)
    if not language_name:
        return ""
    return (
        GENERIC_LANGUAGE_IDENTITY_RULE_TEMPLATE.format(
            lang_code=lang_code, language_name=language_name
        )
        + "\n"
    )


def detect_language_specific_forbidden_fragments(lang_code, translated_text):
    return None


def has_equivalent_separator_after_text(separator, remainder_text):
    if not isinstance(remainder_text, str):
        return False
    stripped_remainder = remainder_text.lstrip()
    if not separator:
        return not stripped_remainder or not re.match(
            r"^\w", stripped_remainder, re.UNICODE
        )
    if not stripped_remainder:
        return False
    first_char = stripped_remainder[0]
    if separator == ":":
        return first_char in COLON_EQUIVALENT_CHARS
    return first_char == separator


def validate_required_known_phrase_usage(en_text, translated_text, lang_code):
    if (
        not isinstance(translated_text, str)
        or not isinstance(lang_code, str)
        or not lang_code
    ):
        return True, ""

    missing_requirements = get_missing_known_phrase_requirements(
        en_text, translated_text, lang_code
    )
    if not missing_requirements:
        return True, ""

    requirement = missing_requirements[0]
    source_phrase = requirement["source_phrase"]
    known_translation = requirement["known_translation"]
    if requirement.get("is_leading"):
        visible_translation = build_visible_prompt_text(translated_text)
        stripped_visible_translation = visible_translation.lstrip()
        if not stripped_visible_translation.startswith(known_translation):
            return (
                False,
                f"translation must start with exact established UI term ({known_translation})",
            )
        separator = requirement.get("separator", "")
        remainder = stripped_visible_translation[len(known_translation) :]
        if not has_equivalent_separator_after_text(separator, remainder):
            if separator == ":":
                return (
                    False,
                    f"translation must keep a colon or a locale-equivalent colon immediately after exact UI term ({known_translation})",
                )
            return (
                False,
                f"translation must keep `{separator}` immediately after exact UI term ({known_translation})",
            )

    return (
        False,
        f"translation must contain exact established UI term ({known_translation}) for source phrase ({source_phrase})",
    )


def validate_translation_text(
    en_text, translated_text, placeholder_pairs, lang_code=""
):
    if not isinstance(translated_text, str):
        return False, "translation is not a string"

    placeholders_ok, expected_placeholders, actual_placeholders = (
        validate_placeholder_sequence(en_text, translated_text)
    )
    if not placeholders_ok:
        return (
            False,
            f"placeholder sequence mismatch (expected {expected_placeholders}, got {actual_placeholders})",
        )

    if placeholder_pairs and not all_protected_terms_preserved(
        translated_text, placeholder_pairs
    ):
        missing_terms = get_missing_protected_terms(translated_text, placeholder_pairs)
        return False, f"protected terms were not preserved ({', '.join(missing_terms)})"

    if not validate_placeholder_quote_style(en_text, translated_text):
        return (
            False,
            "placeholder wrapper style does not match the source placeholder format",
        )

    compiler_percent_ok, compiler_percent_message = (
        validate_compiler_percent_token_alignment(en_text, translated_text)
    )
    if not compiler_percent_ok:
        return False, compiler_percent_message

    if contains_embedded_english_phrase_leak(en_text, translated_text, lang_code):
        return (
            False,
            "translation still contains untranslated embedded English phrase(s) from the source",
        )

    structural_artifact_message = detect_structural_translation_artifact(
        translated_text
    )
    if structural_artifact_message:
        return False, structural_artifact_message

    repetitive_token = detect_repetitive_token_flood(translated_text)
    if repetitive_token:
        return (
            False,
            f"translation contains repetitive token flood artifact ({repetitive_token})",
        )

    if contains_response_artifact_lines(translated_text):
        return False, "translation still contains response-format artifact lines"

    unexpected_scripts = detect_unexpected_script_mixture(
        lang_code, translated_text, en_text, placeholder_pairs
    )
    if unexpected_scripts:
        return (
            False,
            f"translation contains unexpected script mixture ({unexpected_scripts})",
        )

    exact_phrase_ok, exact_phrase_message = validate_required_known_phrase_usage(
        en_text, translated_text, lang_code
    )
    if not exact_phrase_ok:
        return False, exact_phrase_message

    return True, ""


def remove_bidi_characters(text):
    # MSVC C5255: unterminated bidirectional character (Trojan Source mitigation)
    bidi_chars = [
        "\u200e",
        "\u200f",  # LRM, RLM
        "\u202a",
        "\u202b",
        "\u202c",
        "\u202d",
        "\u202e",  # LRE, RLE, PDF, LRO, RLO
        "\u2066",
        "\u2067",
        "\u2068",
        "\u2069",  # LRI, RLI, FSI, PDI
    ]
    for c in bidi_chars:
        text = text.replace(c, "")
    return text


def normalize_percent_like_characters(text, source_text=""):
    if not isinstance(text, str) or not text:
        return text
    if isinstance(source_text, str) and "%" not in source_text:
        return text

    normalized_chars = []
    for ch in text:
        if ch == "%":
            normalized_chars.append(ch)
            continue

        char_name = unicodedata.name(ch, "")
        if "PERCENT SIGN" in char_name:
            normalized_chars.append("%")
            continue

        normalized_chars.append(ch)

    return "".join(normalized_chars)


def get_preferred_literal_percent_sequence(source_text):
    if not isinstance(source_text, str) or not source_text:
        return "%"

    idx = 0
    text_len = len(source_text)
    while idx < text_len:
        if source_text[idx] != "%":
            idx += 1
            continue

        if idx + 1 < text_len and source_text[idx + 1] == "%":
            return "%%"

        placeholder_match = PLACEHOLDER_TOKEN_PATTERN.match(source_text, idx)
        if placeholder_match:
            idx = placeholder_match.end()
            continue

        return "%"

    return "%"


def normalize_literal_percent_sequences(text, source_text=""):
    if not isinstance(text, str) or not text:
        return text

    preferred_percent_sequence = get_preferred_literal_percent_sequence(source_text)
    parts = []
    idx = 0
    text_len = len(text)
    while idx < text_len:
        if text[idx] != "%":
            parts.append(text[idx])
            idx += 1
            continue

        if idx + 1 < text_len and text[idx + 1] == "%":
            parts.append(preferred_percent_sequence)
            idx += 2
            continue

        placeholder_match = PLACEHOLDER_TOKEN_PATTERN.match(text, idx)
        if placeholder_match:
            parts.append(placeholder_match.group(0))
            idx = placeholder_match.end()
            continue

        parts.append(preferred_percent_sequence)
        idx += 1

    return "".join(parts)


class RcParseError(RuntimeError):
    pass


class MapParseError(RuntimeError):
    pass


def ensure_parent_directory(path):
    parent_dir = os.path.dirname(os.path.abspath(path))
    if parent_dir:
        os.makedirs(parent_dir, exist_ok=True)


def read_file_binary(path):
    with open(path, "rb") as input_file:
        return input_file.read()


def write_binary_file_atomically(path, payload):
    ensure_parent_directory(path)
    temp_path = f"{path}.tmp"
    last_error = None
    for attempt_index in range(ATOMIC_WRITE_RETRY_COUNT):
        try:
            with open(temp_path, "wb") as output_file:
                output_file.write(payload)
            os.replace(temp_path, path)
            return
        except PermissionError as err:
            last_error = err
        except OSError as err:
            if err.errno not in {errno.EACCES, errno.EPERM}:
                raise
            last_error = err

        if attempt_index + 1 >= ATOMIC_WRITE_RETRY_COUNT:
            break
        time.sleep(ATOMIC_WRITE_RETRY_DELAY_SEC)

    if last_error is not None:
        raise last_error


def create_backup_file(path):
    backup_path = f"{path}.bak"
    if not os.path.exists(backup_path):
        shutil.copy2(path, backup_path)
        return backup_path

    suffix = 1
    while True:
        candidate_path = f"{path}.bak.{suffix}"
        if not os.path.exists(candidate_path):
            shutil.copy2(path, candidate_path)
            return candidate_path
        suffix += 1


def read_utf8_text(path):
    file_data = read_file_binary(path)
    if file_data.startswith(b"\xef\xbb\xbf"):
        file_data = file_data[3:]
    return file_data.decode("utf-8", errors="surrogateescape")


def read_rc_text(path):
    file_data = read_file_binary(path)
    if file_data.startswith(b"\xff\xfe") or file_data.startswith(b"\xfe\xff"):
        return file_data.decode("utf-16")
    if file_data.startswith(b"\xef\xbb\xbf"):
        return file_data[3:].decode("utf-8")
    try:
        return file_data.decode("utf-8")
    except UnicodeDecodeError:
        return file_data.decode("cp1252")


def split_lines_without_eol(text):
    return text.replace("\r\n", "\n").replace("\r", "\n").split("\n")


def split_text_keep_eol(text):
    lines = []
    index = 0
    text_len = len(text)

    while index < text_len:
        line_start = index
        while index < text_len and text[index] not in "\r\n":
            index += 1
        if index >= text_len:
            lines.append(text[line_start:])
            break
        if text[index] == "\r" and index + 1 < text_len and text[index + 1] == "\n":
            index += 2
        else:
            index += 1
        lines.append(text[line_start:index])

    if text_len == 0:
        lines.append("")
    return lines


def strip_line_eol(line):
    end_index = len(line)
    while end_index > 0 and line[end_index - 1] in "\r\n":
        end_index -= 1
    return line[:end_index]


def detect_line_eol(line, default_eol="\r\n"):
    if line.endswith("\r\n"):
        return "\r\n"
    if line.endswith("\n"):
        return "\n"
    if line.endswith("\r"):
        return "\r"
    return default_eol


def validate_map_language_code(lang_code):
    return bool(
        isinstance(lang_code, str)
        and MAP_TOOL_LANGUAGE_CODE_PATTERN.fullmatch(lang_code.strip())
    )


def extract_rc_literals(text, line_number):
    literals = []
    index = 0
    text_len = len(text)

    while index < text_len:
        quote_pos = text.find('"', index)
        if quote_pos < 0:
            break
        cursor = quote_pos + 1
        buffer = []
        while cursor < text_len:
            current_char = text[cursor]
            if current_char == '"':
                next_index = cursor + 1
                if next_index < text_len and text[next_index] == '"':
                    buffer.append('"')
                    cursor += 2
                    continue
                cursor += 1
                break
            buffer.append(current_char)
            cursor += 1
        else:
            raise RcParseError(
                f"Unterminated string literal on line {line_number}"
            )
        literals.append("".join(buffer))
        index = cursor

    return literals


def should_start_rc_block(line):
    if not line:
        return False
    if line.startswith("//"):
        return False
    return "STRINGTABLE" in line


def parse_rc_stringtable(rc_path):
    lines = split_lines_without_eol(read_rc_text(rc_path))
    entries = {}
    inside_block = False
    pending_begin = False
    current_key = ""
    current_value = ""

    for line_index, raw_line in enumerate(lines, start=1):
        trimmed = raw_line.strip()
        if not inside_block:
            if pending_begin:
                if trimmed.startswith("BEGIN"):
                    inside_block = True
                    pending_begin = False
                continue
            if should_start_rc_block(trimmed):
                pending_begin = True
                if "BEGIN" in trimmed:
                    inside_block = True
                    pending_begin = False
            continue

        if trimmed.startswith("END"):
            if current_key:
                if not current_value:
                    raise RcParseError(
                        f"Missing string for '{current_key}' before line {line_index}"
                    )
                if current_key in entries:
                    raise RcParseError(f"Duplicate identifier '{current_key}'")
                entries[current_key] = current_value
            current_key = ""
            current_value = ""
            inside_block = False
            continue

        if not trimmed or trimmed.startswith("//") or trimmed.startswith("#"):
            continue

        if trimmed[0] != '"':
            identifier = []
            for ch in trimmed:
                if ch.isalnum() or ch == "_":
                    identifier.append(ch)
                    continue
                break
            identifier = "".join(identifier)
            if identifier in {"", "BEGIN", "END"}:
                continue
            if current_key:
                if not current_value:
                    raise RcParseError(
                        f"missing string for '{current_key}' before line {line_index}"
                    )
                if current_key in entries:
                    raise RcParseError(f"duplicate identifier '{current_key}'")
                entries[current_key] = current_value
            current_key = identifier
            current_value = "".join(
                extract_rc_literals(trimmed[len(identifier):], line_index)
            )
            continue

        if not current_key:
            raise RcParseError(
                f"String literal before identifier on line {line_index}"
            )
        current_value += "".join(extract_rc_literals(trimmed, line_index))

    if current_key:
        if not current_value:
            raise RcParseError(
                f"Missing string for '{current_key}' at end of RC file"
            )
        if current_key in entries:
            raise RcParseError(f"Duplicate identifier '{current_key}'")
        entries[current_key] = current_value

    return {key: entries[key] for key in sorted(entries.keys())}


def write_rc_import_map(output_path, entries, language_code, overwrite=False):
    if os.path.exists(output_path) and not overwrite:
        raise RcParseError("Output file exists (use --overwrite)")

    ensure_parent_directory(output_path)
    lines = []
    for index, (key_name, value_text) in enumerate(entries.items()):
        if index > 0:
            lines.append("")
        lines.append(key_name)
        lines.append(f"\t{language_code}\t{value_text}")
    payload = ("\r\n".join(lines) + "\r\n").encode("utf-8")
    write_binary_file_atomically(output_path, payload)


def join_translation_segments_raw(translation_segments):
    return "\n".join(segment_text for segment_text, _ in translation_segments)


def translation_has_any_content(translation_segments):
    return any(segment_text for segment_text, _ in translation_segments)


def collect_placeholder_spans(text):
    spans = []
    if not isinstance(text, str):
        return spans
    for match in PLACEHOLDER_TOKEN_PATTERN.finditer(text):
        token = match.group(0)
        if token == "%%":
            continue
        spans.append(
            {
                "start": match.start(),
                "end": match.end(),
                "token": token,
            }
        )
    return spans


def collect_placeholder_tokens_for_map(text):
    return [span["token"] for span in collect_placeholder_spans(text)]


def replace_placeholders_by_reference(text, target_spans, reference_spans):
    if len(target_spans) != len(reference_spans):
        return text

    rebuilt = []
    cursor = 0
    for target_span, reference_span in zip(target_spans, reference_spans):
        if target_span["start"] > cursor:
            rebuilt.append(text[cursor:target_span["start"]])
        rebuilt.append(reference_span["token"])
        cursor = target_span["end"]
    rebuilt.append(text[cursor:])
    return "".join(rebuilt)


def normalize_placeholder_wrapper_style_by_reference(reference_raw, target_raw):
    if not reference_raw or not target_raw:
        return target_raw
    if collect_placeholder_tokens_for_map(reference_raw) != collect_placeholder_tokens_for_map(target_raw):
        return target_raw
    return normalize_placeholder_quote_style(reference_raw, target_raw)


def escape_parity_matches(reference_raw, target_raw):
    return (
        ("\\n" in reference_raw) == ("\\n" in target_raw)
        and ("\\r" in reference_raw) == ("\\r" in target_raw)
        and ("\\\\" in reference_raw) == ("\\\\" in target_raw)
        and ("%%" in reference_raw) == ("%%" in target_raw)
    )


def append_decoded_code_point(output_parts, code_point, line_number):
    if code_point > 0x10FFFF:
        raise MapParseError(
            f"Unicode code point out of range (line {line_number})"
        )
    output_parts.append(chr(code_point))


def decode_map_escapes(value_text, line_number):
    output_parts = []
    index = 0
    value_len = len(value_text)

    while index < value_len:
        current_char = value_text[index]
        if current_char != "\\":
            output_parts.append(current_char)
            index += 1
            continue

        index += 1
        if index >= value_len:
            raise MapParseError(
                f"Dangling backslash in text (line {line_number})"
            )

        escape_char = value_text[index]
        index += 1
        if escape_char == "n":
            output_parts.append("\n")
        elif escape_char == "r":
            output_parts.append("\r")
        elif escape_char == "t":
            output_parts.append("\t")
        elif escape_char == "a":
            output_parts.append("\a")
        elif escape_char == "b":
            output_parts.append("\b")
        elif escape_char == "f":
            output_parts.append("\f")
        elif escape_char == "v":
            output_parts.append("\v")
        elif escape_char == "\\":
            output_parts.append("\\")
        elif escape_char == '"':
            output_parts.append('"')
        elif escape_char == "0":
            output_parts.append("\0")
        elif escape_char == "x":
            hex_start = index
            while index < value_len and value_text[index] in "0123456789abcdefABCDEF":
                index += 1
            hex_digits = value_text[hex_start:index]
            if not hex_digits:
                raise MapParseError(
                    f"Incomplete hex escape in text (line {line_number})"
                )
            append_decoded_code_point(
                output_parts, int(hex_digits, 16), line_number
            )
        elif escape_char == "u":
            if index + 4 > value_len:
                raise MapParseError(
                    f"Incomplete unicode escape in text (line {line_number})"
                )
            unicode_digits = value_text[index:index + 4]
            if not re.fullmatch(r"[0-9A-Fa-f]{4}", unicode_digits):
                raise MapParseError(
                    f"Incomplete unicode escape in text (line {line_number})"
                )
            index += 4
            append_decoded_code_point(
                output_parts, int(unicode_digits, 16), line_number
            )
        else:
            raise MapParseError(
                f"Unsupported escape sequence '\\{escape_char}' (line {line_number})"
            )

    return "".join(output_parts)


def parse_map_file(map_path):
    parsed = {"entries": [], "languages": set()}
    lines = split_lines_without_eol(read_utf8_text(map_path))
    current_entry = None
    current_language = ""
    seen_keys = set()

    for line_number, line in enumerate(lines, start=1):
        if not line.strip():
            current_language = ""
            continue

        trimmed = line.lstrip(" ")
        if trimmed.startswith("#"):
            continue

        if not line.startswith("\t"):
            key_name = trimmed
            if not key_name:
                raise MapParseError(
                    f"Empty key encountered on line {line_number}"
                )
            if key_name in seen_keys:
                raise MapParseError(
                    f"Duplicate key '{key_name}' (line {line_number})"
                )
            current_entry = {
                "key": key_name,
                "line": line_number,
                "translations": {},
            }
            parsed["entries"].append(current_entry)
            seen_keys.add(key_name)
            current_language = ""
            continue

        if current_entry is None:
            raise MapParseError(
                f"Translation without key on line {line_number}"
            )

        if line.startswith("\t\t"):
            if not current_language:
                raise MapParseError(
                    f"continuation line without language (line {line_number})"
                )
            current_entry["translations"][current_language]["segments"].append(
                (line[2:], line_number)
            )
            continue

        separator_index = line.find("\t", 1)
        if separator_index < 0:
            raise MapParseError(
                f"Missing tab separator on line {line_number}"
            )

        lang_code = line[1:separator_index].strip()
        if not lang_code:
            raise MapParseError(f"Empty language code on line {line_number}")
        if lang_code in current_entry["translations"]:
            raise MapParseError(
                f"Duplicate language '{lang_code}' for key '{current_entry['key']}' (line {line_number})"
            )

        current_entry["translations"][lang_code] = {
            "segments": [(line[separator_index + 1:], line_number)]
        }
        current_language = lang_code
        parsed["languages"].add(lang_code)

    return parsed


def build_map_language_list(languages):
    ordered_languages = sorted(languages)
    if "en" in ordered_languages:
        ordered_languages.remove("en")
    ordered_languages.insert(0, "en")
    return ordered_languages


def finalize_parsed_map_entries(parsed_map):
    finalized_entries = []
    for entry in parsed_map["entries"]:
        if not entry["translations"]:
            raise MapParseError(
                f"Key '{entry['key']}' has no translations (line {entry['line']})"
            )
        finalized_entry = {
            "key": entry["key"],
            "line": entry["line"],
            "translations": {},
        }
        for lang_code, translation in entry["translations"].items():
            segments = translation["segments"]
            if not segments:
                raise MapParseError(
                    f"Empty translation segments for key '{entry['key']}'"
                )
            raw_text = join_translation_segments_raw(segments)
            finalized_entry["translations"][lang_code] = decode_map_escapes(
                raw_text, segments[0][1]
            )
        finalized_entries.append(finalized_entry)
    return finalized_entries


def is_whitespace_only(text):
    return not text or text.isspace()


def has_meaningful_translation_text(raw_text):
    try:
        decoded_text = decode_map_escapes(raw_text, 0)
    except Exception:
        decoded_text = raw_text
    return not is_whitespace_only(decoded_text)


def fill_missing_translations_with_english(entries, languages):
    for entry in entries:
        english_text = entry["translations"].get("en")
        if english_text is None:
            continue
        for lang_code in languages:
            if lang_code == "en":
                continue
            if lang_code not in entry["translations"] or is_whitespace_only(entry["translations"][lang_code]):
                entry["translations"][lang_code] = english_text


def validate_finalized_entries(entries, languages):
    for entry in entries:
        english_text = entry["translations"].get("en")
        if english_text is None:
            raise MapParseError(
                f"Missing 'en' translation for key '{entry['key']}' (line {entry['line']})"
            )
        if english_text == "":
            raise MapParseError(
                f"Empty 'en' translation for key '{entry['key']}'"
            )
        english_placeholders = collect_placeholder_tokens_for_map(english_text)
        for lang_code in languages:
            if lang_code == "en":
                continue
            translated_text = entry["translations"].get(lang_code)
            if translated_text is None or is_whitespace_only(translated_text):
                continue
            if collect_placeholder_tokens_for_map(translated_text) != english_placeholders:
                raise MapParseError(
                    f"Placeholder mismatch for key '{entry['key']}' language '{lang_code}'"
                )


def fnv1a_hash(value_text):
    hash_value = 2166136261
    for byte_value in value_text.encode("utf-8"):
        hash_value ^= byte_value
        hash_value = (hash_value * 16777619) & 0xFFFFFFFF
    return hash_value


def next_power_of_two(value):
    if value <= 1:
        return 1
    result = 1
    while result < value:
        result <<= 1
    return result


def build_generated_tables(entries, languages):
    language_index = {
        lang_code: index for index, lang_code in enumerate(languages)
    }
    values = []
    first_indexes = {}

    for entry in entries:
        first_index = 0xFFFFFFFF
        previous_index = 0xFFFFFFFF
        for lang_code in languages:
            if lang_code not in entry["translations"]:
                continue
            value_index = len(values)
            values.append(
                {
                    "language": language_index[lang_code],
                    "next": 0xFFFFFFFF,
                    "text": entry["translations"][lang_code],
                }
            )
            if previous_index != 0xFFFFFFFF:
                values[previous_index]["next"] = value_index
            else:
                first_index = value_index
            previous_index = value_index
        first_indexes[entry["key"]] = first_index

    bucket_count = next_power_of_two(max(1, len(entries) * 2))
    bucket_mask = bucket_count - 1
    buckets = [
        {"hash": 0, "key": None, "value": 0xFFFFFFFF}
        for _ in range(bucket_count)
    ]

    for entry in entries:
        hash_value = fnv1a_hash(entry["key"])
        position = hash_value & bucket_mask
        while buckets[position]["key"] is not None:
            if buckets[position]["key"] == entry["key"]:
                raise MapParseError(
                    f"Duplicate key '{entry['key']}' in hash table build"
                )
            position = (position + 1) & bucket_mask
        buckets[position] = {
            "hash": hash_value,
            "key": entry["key"],
            "value": first_indexes[entry["key"]],
        }

    return {"values": values, "buckets": buckets, "bucket_mask": bucket_mask}


def escape_for_cpp_string(value_text):
    escaped_parts = []
    for current_char in value_text:
        if current_char == "\\":
            escaped_parts.append("\\\\")
        elif current_char == '"':
            escaped_parts.append('\\"')
        elif current_char == "\n":
            escaped_parts.append("\\n")
        elif current_char == "\r":
            escaped_parts.append("\\r")
        elif current_char == "\t":
            escaped_parts.append("\\t")
        elif current_char == "\0":
            escaped_parts.append("\\0")
        elif current_char == "\a":
            escaped_parts.append("\\a")
        elif current_char == "\b":
            escaped_parts.append("\\b")
        elif current_char == "\f":
            escaped_parts.append("\\f")
        elif current_char == "\v":
            escaped_parts.append("\\v")
        else:
            escaped_parts.append(current_char)
    return "".join(escaped_parts)


def build_language_fallback_indexes(languages):
    fallback_indexes = []
    for index, lang_code in enumerate(languages):
        if "-" not in lang_code:
            fallback_indexes.append(0)
            continue
        base_code = lang_code.split("-", 1)[0]
        fallback_index = 0
        for search_index, search_lang in enumerate(languages):
            if search_lang == base_code:
                fallback_index = search_index
                break
        if fallback_index == index:
            fallback_index = 0
        fallback_indexes.append(fallback_index)
    return fallback_indexes


def write_translation_data_header(path, tables):
    lines = [
        "// Auto-generated by ai_translator.py. Do not edit manually.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <tchar.h>",
        "#include \"lang_registry.gen.h\"",
        "",
        "namespace Translations",
        "{",
        f"\tstatic constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;",
        f"\tstatic constexpr uint32_t kBucketCount = {len(tables['buckets'])}u;",
        f"\tstatic constexpr uint32_t kBucketMask = {tables['bucket_mask']}u;",
        f"\tstatic constexpr uint32_t kValueCount = {len(tables['values'])}u;",
        "",
        "\tstruct TranslationValue",
        "\t{",
        "\t\tuint16_t language;",
        "\t\tuint32_t next;",
        "\t\tLPCTSTR text;",
        "\t};",
        "",
        "\tstruct TranslationBucket",
        "\t{",
        "\t\tuint32_t hash;",
        "\t\tLPCTSTR key;",
        "\t\tuint32_t value;",
        "\t};",
        "",
        "\tstatic const TranslationValue kValues[kValueCount] = {",
    ]

    for index, value in enumerate(tables["values"]):
        next_value = (
            "kInvalidIndex"
            if value["next"] == 0xFFFFFFFF
            else f"{value['next']}u"
        )
        suffix = "," if index + 1 != len(tables["values"]) else ""
        lines.append(
            f'\t\t{{ {value["language"]}u, {next_value}, _T("{escape_for_cpp_string(value["text"])}") }}{suffix}'
        )

    lines.extend(["\t};", "", "\tstatic const TranslationBucket kBuckets[kBucketCount] = {"])

    for index, bucket in enumerate(tables["buckets"]):
        suffix = "," if index + 1 != len(tables["buckets"]) else ""
        if bucket["key"] is None:
            lines.append(f"\t\t{{ 0u, nullptr, kInvalidIndex }}{suffix}")
        else:
            value_index = (
                "kInvalidIndex"
                if bucket["value"] == 0xFFFFFFFF
                else f"{bucket['value']}u"
            )
            lines.append(
                f'\t\t{{ {bucket["hash"]}u, _T("{escape_for_cpp_string(bucket["key"])}"), {value_index} }}{suffix}'
            )

    lines.extend(["\t};", "}", ""])
    payload = ("\r\n".join(lines)).encode("utf-8", errors="surrogateescape")
    write_binary_file_atomically(path, b"\xef\xbb\xbf" + payload)


def write_language_registry_header(path, languages):
    fallback_indexes = build_language_fallback_indexes(languages)
    lines = [
        "// Auto-generated by ai_translator.py. Do not edit manually.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <tchar.h>",
        "",
        "namespace Translations",
        "{",
        "\tstruct LanguageRecord",
        "\t{",
        "\t\tLPCTSTR code;",
        "\t\tuint16_t fallback;",
        "\t};",
        "",
        f"\tstatic constexpr uint16_t kLanguageCount = {len(languages)}u;",
        "\tstatic constexpr uint16_t kDefaultLanguage = 0u;",
        "",
        "\tstatic const LanguageRecord kLanguages[kLanguageCount] = {",
    ]

    for index, lang_code in enumerate(languages):
        suffix = "," if index + 1 != len(languages) else ""
        lines.append(
            f'\t\t{{ _T("{escape_for_cpp_string(lang_code)}"), {fallback_indexes[index]}u }}{suffix}'
        )

    lines.extend(["\t};", "}", ""])
    payload = ("\r\n".join(lines)).encode("utf-8", errors="surrogateescape")
    write_binary_file_atomically(path, b"\xef\xbb\xbf" + payload)


def normalize_entry_placeholder_wrappers_against_english(entry):
    english_translation = entry["translations"].get("en")
    if not english_translation or not translation_has_any_content(english_translation["segments"]):
        return

    english_raw = join_translation_segments_raw(english_translation["segments"])
    for lang_code, translation in entry["translations"].items():
        if lang_code == "en" or not translation_has_any_content(translation["segments"]):
            continue
        current_raw = join_translation_segments_raw(translation["segments"])
        normalized_raw = normalize_placeholder_wrapper_style_by_reference(
            english_raw, current_raw
        )
        if normalized_raw == current_raw:
            continue
        set_map_entry_language_raw(entry, lang_code, normalized_raw)


def set_map_entry_language_raw(entry, lang_code, raw_text):
    translation = entry["translations"].setdefault(
        lang_code, {"segments": [(raw_text, entry["line"])]}
    )
    base_line = (
        translation["segments"][0][1] if translation["segments"] else entry["line"]
    )
    parts = raw_text.split("\n")
    if raw_text.endswith("\n"):
        parts = parts[:-1]
    if not parts:
        parts = [""]
    translation["segments"] = [(part, base_line) for part in parts]


def ensure_all_map_languages_present(entry, languages):
    for lang_code in languages:
        if lang_code not in entry["translations"]:
            entry["translations"][lang_code] = {"segments": [("", entry["line"])]}


def clear_map_translation_keep_blank(entry, lang_code):
    if lang_code not in entry["translations"]:
        entry["translations"][lang_code] = {"segments": [("", entry["line"])]}
        return
    entry["translations"][lang_code]["segments"] = [("", entry["line"])]


def write_fixed_map_file(path, entries, languages):
    lines = []
    for entry_index, entry in enumerate(entries):
        if entry_index > 0:
            lines.append("")
        lines.append(entry["key"])
        for lang_code in languages:
            translation = entry["translations"].get(lang_code)
            if translation is None or not translation["segments"]:
                lines.append(f"\t{lang_code}\t")
                continue
            segments = translation["segments"]
            lines.append(f"\t{lang_code}\t{segments[0][0]}")
            for segment_text, _ in segments[1:]:
                lines.append(f"\t\t{segment_text}")
    payload = ("\r\n".join(lines) + "\r\n").encode("utf-8", errors="surrogateescape")
    write_binary_file_atomically(path, payload)


def fix_map_file(map_path):
    parsed_map = parse_map_file(map_path)
    languages = build_map_language_list(parsed_map["languages"])
    print("languages: " + ", ".join(languages))

    for entry in parsed_map["entries"]:
        normalize_entry_placeholder_wrappers_against_english(entry)
        english_translation = entry["translations"].get("en")
        if english_translation:
            english_raw = join_translation_segments_raw(english_translation["segments"])
            require_n = "\\n" in english_raw
            require_r = "\\r" in english_raw
            require_backslash = "\\\\" in english_raw
            require_double_percent = "%%" in english_raw

            for lang_code in languages:
                if lang_code == "en":
                    continue
                translation = entry["translations"].get(lang_code)
                if not translation or not translation_has_any_content(translation["segments"]):
                    continue
                translated_raw = join_translation_segments_raw(translation["segments"])
                is_valid = True
                if require_n and "\\n" not in translated_raw:
                    is_valid = False
                if require_r and "\\r" not in translated_raw:
                    is_valid = False
                if require_backslash and "\\\\" not in translated_raw:
                    is_valid = False
                if require_double_percent and "%%" not in translated_raw:
                    is_valid = False
                if not is_valid:
                    clear_map_translation_keep_blank(entry, lang_code)
                    continue

                english_spans = collect_placeholder_spans(english_raw)
                translated_spans = collect_placeholder_spans(translated_raw)
                if len(translated_spans) != len(english_spans):
                    clear_map_translation_keep_blank(entry, lang_code)
                    continue
                if [span["token"] for span in translated_spans] != [span["token"] for span in english_spans]:
                    translated_raw = replace_placeholders_by_reference(
                        translated_raw, translated_spans, english_spans
                    )
                normalized_raw = normalize_placeholder_wrapper_style_by_reference(
                    english_raw, translated_raw
                )
                if normalized_raw != join_translation_segments_raw(translation["segments"]):
                    set_map_entry_language_raw(entry, lang_code, normalized_raw)

        ensure_all_map_languages_present(entry, languages)

    for entry in parsed_map["entries"]:
        english_translation = entry["translations"].get("en")
        if not english_translation:
            continue
        english_raw = join_translation_segments_raw(english_translation["segments"])
        try:
            english_decoded = decode_map_escapes(english_raw, entry["line"])
        except Exception:
            english_decoded = english_raw
        english_char_count = len(english_decoded)
        if english_char_count == 0:
            continue

        for lang_code in languages:
            if lang_code == "en":
                continue
            translation = entry["translations"].get(lang_code)
            if not translation or not translation_has_any_content(translation["segments"]):
                continue
            translated_raw = join_translation_segments_raw(translation["segments"])
            try:
                translated_decoded = decode_map_escapes(translated_raw, entry["line"])
            except Exception:
                translated_decoded = translated_raw
            translated_char_count = len(translated_decoded)
            if translated_char_count > english_char_count * 10 or translated_char_count * 10 < english_char_count:
                clear_map_translation_keep_blank(entry, lang_code)

    parsed_map["entries"].sort(key=lambda entry: entry["key"])
    write_fixed_map_file(map_path, parsed_map["entries"], languages)


def compile_map_to_headers(map_path, data_header_path, registry_header_path):
    parsed_map = parse_map_file(map_path)
    for entry in parsed_map["entries"]:
        normalize_entry_placeholder_wrappers_against_english(entry)
    languages = build_map_language_list(parsed_map["languages"])
    finalized_entries = finalize_parsed_map_entries(parsed_map)
    fill_missing_translations_with_english(finalized_entries, languages)
    validate_finalized_entries(finalized_entries, languages)
    generated_tables = build_generated_tables(finalized_entries, languages)
    write_language_registry_header(registry_header_path, languages)
    write_translation_data_header(data_header_path, generated_tables)


def check_map_file(map_path):
    parsed_map = parse_map_file(map_path)
    languages = build_map_language_list(parsed_map["languages"])
    finalized_entries = finalize_parsed_map_entries(parsed_map)
    validate_finalized_entries(finalized_entries, languages)
    print(
        f"Check: OK ({len(finalized_entries)} keys, {len(languages)} languages)"
    )


def read_text_lines_with_bom(path):
    file_data = read_file_binary(path)
    has_bom = file_data.startswith(b"\xef\xbb\xbf")
    if has_bom:
        file_data = file_data[3:]
    text = file_data.decode("utf-8", errors="surrogateescape")
    return split_text_keep_eol(text), has_bom


def write_text_lines_with_bom(path, lines, has_bom=False):
    payload = "".join(lines).encode("utf-8", errors="surrogateescape")
    if has_bom:
        payload = b"\xef\xbb\xbf" + payload
    write_binary_file_atomically(path, payload)


def is_map_key_line(line):
    stripped_line = strip_line_eol(line)
    if not stripped_line or stripped_line[0] in "\t#" or stripped_line.startswith("//"):
        return False
    return all(ch.isalnum() or ch == "_" for ch in stripped_line)


def match_map_language_line(line):
    stripped_line = strip_line_eol(line)
    match = re.match(
        r"^\t([A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*)\t",
        stripped_line,
    )
    if not match:
        return None
    return match.group(1), match.end()


def collect_map_languages_from_lines(lines, requested_lang=None):
    discovered = []
    lower_to_original = {}

    for line in lines:
        match = match_map_language_line(line)
        if not match:
            continue
        lang_code = match[0]
        lower_lang = lang_code.lower()
        if lower_lang in lower_to_original:
            continue
        lower_to_original[lower_lang] = lang_code
        discovered.append(lang_code)

    if "en" not in lower_to_original:
        lower_to_original["en"] = "en"
        discovered.insert(0, "en")

    if requested_lang:
        lower_requested = requested_lang.lower()
        if lower_requested not in lower_to_original:
            lower_to_original[lower_requested] = requested_lang
            discovered.append(requested_lang)

    return [lower_to_original["en"]] + sorted(
        [
            lang_code
            for lang_code in discovered
            if lang_code.lower() != "en"
        ],
        key=lambda value: value.lower(),
    )


def find_map_key_line_index(lines, key_name):
    for index, line in enumerate(lines):
        if strip_line_eol(line) == key_name:
            return index
    return -1


def find_map_language_line_index(lines, key_index, lang_code):
    target_lang = lang_code.lower()
    for index in range(key_index + 1, len(lines)):
        line = lines[index]
        stripped_line = strip_line_eol(line)
        if stripped_line and not line.startswith("\t"):
            break
        match = match_map_language_line(line)
        if match and match[0].lower() == target_lang:
            return index
    return -1


def get_map_language_raw_text_from_lines(lines, lang_line_index):
    if lang_line_index < 0 or lang_line_index >= len(lines):
        return ""

    match = match_map_language_line(lines[lang_line_index])
    if not match:
        return ""

    _, prefix_end = match
    parts = [strip_line_eol(lines[lang_line_index])[prefix_end:]]
    for index in range(lang_line_index + 1, len(lines)):
        line = lines[index]
        if not line.startswith("\t\t"):
            break
        parts.append(strip_line_eol(line)[2:])
    return "\n".join(parts)


def find_map_language_insert_index(lines, key_index, lang_code):
    target_lang = lang_code.lower()
    for index in range(key_index + 1, len(lines)):
        line = lines[index]
        stripped_line = strip_line_eol(line)
        if stripped_line and not line.startswith("\t"):
            return index
        match = match_map_language_line(line)
        if match and match[0].lower() > target_lang:
            return index
    return len(lines)


def normalize_translation_text_for_map(new_text, en_text=None):
    clean_text = new_text if isinstance(new_text, str) else ""
    clean_text = remove_bidi_characters(clean_text)
    clean_text = re.sub(r"\\([טтτطت])", r"\\t", clean_text)
    clean_text = re.sub(r"\\([נнνن])", r"\\n", clean_text)
    clean_text = re.sub(r"\\([רрρر])", r"\\r", clean_text)

    def escape_invalid_sequence(match):
        escaped_char = match.group(1)
        if not escaped_char:
            return "\\\\"
        if escaped_char in "nrt\"\\0xuabfv":
            return match.group(0)
        return "\\\\" + escaped_char

    clean_text = re.sub(r"\\(.)|\\$", escape_invalid_sequence, clean_text)
    clean_text = (
        clean_text.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")
    )
    clean_text = normalize_percent_like_characters(clean_text, en_text)
    clean_text = normalize_literal_percent_sequences(clean_text, en_text)
    return clean_text


def normalize_candidate_translation_against_english(
    english_raw, candidate_text, key_name="", lang_code=""
):
    if not candidate_text:
        return candidate_text
    if not escape_parity_matches(english_raw, candidate_text):
        raise MapParseError(
            f"Escape parity mismatch vs 'en' for key '{key_name}' language '{lang_code}'"
        )

    english_spans = collect_placeholder_spans(english_raw)
    candidate_spans = collect_placeholder_spans(candidate_text)
    if len(candidate_spans) != len(english_spans):
        raise MapParseError(
            f"Placeholder count mismatch (en={len(english_spans)}, got={len(candidate_spans)}) for key '{key_name}' language '{lang_code}'"
        )

    if [span["token"] for span in candidate_spans] != [span["token"] for span in english_spans]:
        candidate_text = replace_placeholders_by_reference(
            candidate_text, candidate_spans, english_spans
        )
    candidate_text = normalize_placeholder_wrapper_style_by_reference(
        english_raw, candidate_text
    )

    try:
        english_decoded = decode_map_escapes(english_raw, 0)
    except Exception:
        english_decoded = english_raw
    try:
        candidate_decoded = decode_map_escapes(candidate_text, 0)
    except Exception:
        candidate_decoded = candidate_text

    english_char_count = len(english_decoded)
    candidate_char_count = len(candidate_decoded)
    if english_char_count > 0 and (
        candidate_char_count > english_char_count * 10
        or candidate_char_count * 10 < english_char_count
    ):
        raise MapParseError(
            f"Text length suspicious vs 'en' (ratio check failed) for key '{key_name}' language '{lang_code}'"
        )

    return candidate_text


def build_map_translation_lines(lang_code, raw_text, line_eol):
    parts = raw_text.split("\n")
    if raw_text.endswith("\n"):
        parts = parts[:-1]
    if not parts:
        parts = [""]

    lines = [f"\t{lang_code}\t{parts[0]}{line_eol}"]
    for part in parts[1:]:
        lines.append(f"\t\t{part}{line_eol}")
    return lines


def set_translation_entry_in_map(
    map_path, key_name, lang_code, text, create_if_missing=False, backup=False
):
    if not key_name:
        raise RuntimeError("--key is required")
    if not validate_map_language_code(lang_code):
        raise RuntimeError(f"Invalid language code: {lang_code}")

    normalized_text = normalize_translation_text_for_map(text)
    is_english = lang_code.lower() == "en"
    if is_english and not has_meaningful_translation_text(normalized_text):
        raise RuntimeError(
            'English text cannot be empty. Use language "en" with a non-empty text first.'
        )

    lines, has_bom = read_text_lines_with_bom(map_path)
    key_index = find_map_key_line_index(lines, key_name)
    if key_index < 0:
        if not create_if_missing:
            raise RuntimeError(
                f'Key not found: {key_name}. Use "--create" to add this key.'
            )
        if not is_english:
            raise RuntimeError(
                'New keys must start with English. Use "--create --language en --text ..." first.'
            )

        if backup:
            create_backup_file(map_path)

        language_list = collect_map_languages_from_lines(lines, lang_code)
        default_eol = detect_line_eol(lines[-1]) if lines else "\r\n"
        if lines and strip_line_eol(lines[-1]):
            lines.append(default_eol)
        lines.append(f"{key_name}{default_eol}")
        for existing_lang in language_list:
            raw_text = normalized_text if existing_lang.lower() == lang_code.lower() else ""
            lines.extend(build_map_translation_lines(existing_lang, raw_text, default_eol))
        write_text_lines_with_bom(map_path, lines, has_bom=has_bom)
        return f'OK: Key "{key_name}" created with language "{lang_code}".'

    english_index = find_map_language_line_index(lines, key_index, "en")
    english_raw = get_map_language_raw_text_from_lines(lines, english_index)
    if not is_english and not has_meaningful_translation_text(english_raw):
        raise RuntimeError(
            f'English text is missing for key "{key_name}". First set language "en".'
        )

    if english_raw:
        normalized_text = normalize_translation_text_for_map(text, english_raw)

    if normalized_text and not is_english:
        normalized_text = normalize_candidate_translation_against_english(
            english_raw, normalized_text, key_name, lang_code
        )

    lang_index = find_map_language_line_index(lines, key_index, lang_code)
    if lang_index < 0:
        if backup:
            create_backup_file(map_path)
        insert_index = find_map_language_insert_index(lines, key_index, lang_code)
        line_eol = detect_line_eol(lines[insert_index - 1]) if lines else "\r\n"
        replacement_lines = build_map_translation_lines(
            lang_code, normalized_text, line_eol
        )
        lines[insert_index:insert_index] = replacement_lines
        write_text_lines_with_bom(map_path, lines, has_bom=has_bom)
        return f'OK: Key "{key_name}" / Lang "{lang_code}" added.'

    replacement_end = lang_index + 1
    while replacement_end < len(lines) and lines[replacement_end].startswith("\t\t"):
        replacement_end += 1

    line_eol = detect_line_eol(lines[lang_index])
    replacement_lines = build_map_translation_lines(
        lang_code, normalized_text, line_eol
    )
    if lines[lang_index:replacement_end] == replacement_lines:
        return (
            f'INFO: Line already has the same content; no changes made (key={key_name}, lang={lang_code}).'
        )

    if backup:
        create_backup_file(map_path)
    lines[lang_index:replacement_end] = replacement_lines
    write_text_lines_with_bom(map_path, lines, has_bom=has_bom)
    return f'OK: Key "{key_name}" / Lang "{lang_code}" updated.'


def add_language_to_map(map_path, lang_code, backup=False):
    if not validate_map_language_code(lang_code):
        raise RuntimeError(f"Invalid language code: {lang_code}")
    if lang_code.lower() == "en":
        raise RuntimeError("'en' cannot be added.")

    lines, has_bom = read_text_lines_with_bom(map_path)
    if backup:
        create_backup_file(map_path)

    new_lines = []
    index = 0
    while index < len(lines):
        current_line = lines[index]
        if not is_map_key_line(current_line):
            new_lines.append(current_line)
            index += 1
            continue

        new_lines.append(current_line)
        index += 1
        block_lines = []
        trailing_empty_lines = []
        while index < len(lines):
            stripped_line = strip_line_eol(lines[index])
            if stripped_line and not lines[index].startswith("\t"):
                break
            if stripped_line:
                block_lines.append(lines[index])
            else:
                trailing_empty_lines.append(lines[index])
            index += 1

        lang_entries = []
        current_entry = None
        for block_line in block_lines:
            match = match_map_language_line(block_line)
            if match:
                current_entry = {"code": match[0], "text_lines": [block_line]}
                lang_entries.append(current_entry)
                continue
            if current_entry and block_line.startswith("\t\t"):
                current_entry["text_lines"].append(block_line)

        if not any(entry["code"].lower() == lang_code.lower() for entry in lang_entries):
            block_eol = detect_line_eol(current_line)
            lang_entries.append(
                {
                    "code": lang_code,
                    "text_lines": build_map_translation_lines(lang_code, "", block_eol),
                }
            )

        lang_entries.sort(
            key=lambda entry: (entry["code"].lower() != "en", entry["code"].lower())
        )
        for entry in lang_entries:
            new_lines.extend(entry["text_lines"])
        new_lines.extend(trailing_empty_lines)

    write_text_lines_with_bom(map_path, new_lines, has_bom=has_bom)
    return f'OK: Language "{lang_code}" added to all keys and sorted block-wise.'


def remove_key_from_map(map_path, key_name, backup=False):
    if not key_name:
        raise RuntimeError("--key is required")

    lines, has_bom = read_text_lines_with_bom(map_path)
    key_index = find_map_key_line_index(lines, key_name)
    if key_index < 0:
        return f"INFO: Key not found: {key_name}; no changes."

    if backup:
        create_backup_file(map_path)

    block_start = key_index
    if block_start > 0 and not strip_line_eol(lines[block_start - 1]):
        block_start -= 1
    block_end = key_index + 1
    while block_end < len(lines):
        stripped_line = strip_line_eol(lines[block_end])
        if stripped_line and not lines[block_end].startswith("\t"):
            break
        block_end += 1

    del lines[block_start:block_end]
    write_text_lines_with_bom(map_path, lines, has_bom=has_bom)
    return f'OK: Key "{key_name}" removed.'


def clear_other_languages_for_key(map_path, key_name, backup=False):
    if not key_name:
        raise RuntimeError("--key is required")

    lines, has_bom = read_text_lines_with_bom(map_path)
    key_index = find_map_key_line_index(lines, key_name)
    if key_index < 0:
        return f"INFO: Key not found: {key_name}; no changes."

    if backup:
        create_backup_file(map_path)

    new_lines = lines[:key_index + 1]
    index = key_index + 1
    while index < len(lines):
        current_line = lines[index]
        stripped_line = strip_line_eol(current_line)
        if stripped_line and not current_line.startswith("\t"):
            break
        match = match_map_language_line(current_line)
        if not match:
            new_lines.append(current_line)
            index += 1
            continue
        lang_code = match[0]
        line_eol = detect_line_eol(current_line)
        if lang_code.lower() == "en":
            new_lines.append(current_line)
            index += 1
            while index < len(lines) and lines[index].startswith("\t\t"):
                new_lines.append(lines[index])
                index += 1
            continue
        new_lines.extend(build_map_translation_lines(lang_code, "", line_eol))
        index += 1
        while index < len(lines) and lines[index].startswith("\t\t"):
            index += 1

    new_lines.extend(lines[index:])
    write_text_lines_with_bom(map_path, new_lines, has_bom=has_bom)
    return f'OK: All translations except "en" cleared for key "{key_name}".'


def update_translation_via_compiler(key_name, lang_code, new_text, en_text=None):
    clean_text = new_text
    if isinstance(en_text, str) and en_text:
        placeholder_pairs = build_protected_placeholders(
            en_text, {lang_code: clean_text}
        )
        clean_text = cleanup_translated_text(
            en_text, clean_text, placeholder_pairs, lang_code
        )
        is_valid, validation_message = validate_translation_text(
            en_text, clean_text, placeholder_pairs, lang_code
        )
        if not is_valid:
            return False, validation_message

    try:
        return True, set_translation_entry_in_map(
            MAP_FILE_PATH,
            key_name,
            lang_code,
            clean_text,
            create_if_missing=False,
            backup=False,
        )
    except Exception as err:
        return False, str(err)


def clear_other_translations_via_compiler(key_name, create_backup=False):
    try:
        return True, clear_other_languages_for_key(
            MAP_FILE_PATH, key_name, backup=create_backup
        )
    except Exception as err:
        return False, str(err)


def get_keys_from_map(filepath):
    try:
        parsed_map = parse_map_file(filepath)
    except Exception:
        print(f"ERROR: {filepath} could not be parsed.")
        return []

    keys_list = []
    for entry in parsed_map["entries"]:
        keys_list.append(
            {
                "key": entry["key"],
                "langs": {
                    lang_code: join_translation_segments_raw(translation["segments"])
                    for lang_code, translation in entry["translations"].items()
                },
            }
        )
    return keys_list


def parse_specific_keys_input(raw_value):
    keys = []
    seen = set()

    for part in raw_value.split(","):
        key_name = part.strip()
        if not key_name or key_name in seen:
            continue
        keys.append(key_name)
        seen.add(key_name)

    return keys


def parse_specific_key_language_pairs_input(raw_value):
    if not isinstance(raw_value, str):
        return [], "Input must be a string."

    tokens = [part.strip() for part in raw_value.split(",") if part.strip()]
    if not tokens:
        return [], "No KEY/language input provided."
    if len(tokens) % 2 != 0:
        return [], "Input must contain alternating KEY and language code values like KEYA,tr,KEYB,ar."

    pairs = []
    seen = set()
    for index in range(0, len(tokens), 2):
        key_name = tokens[index]
        lang_code = tokens[index + 1]
        if not re.fullmatch(LANGUAGE_CODE_PATTERN, lang_code):
            return [], f"Invalid language code: {lang_code}"
        pair = (key_name, lang_code)
        if pair in seen:
            continue
        pairs.append(pair)
        seen.add(pair)

    return pairs, ""


def parse_boolean_cli_value(raw_value, option_name):
    if raw_value is None:
        return None

    normalized = str(raw_value).strip().lower()
    if normalized in CLI_BOOLEAN_TRUE_VALUES:
        return True
    if normalized in CLI_BOOLEAN_FALSE_VALUES:
        return False
    raise ValueError(
        f"Invalid value for {option_name}: {raw_value}. Use one of: true, false, yes, no, 1, 0."
    )


def normalize_menu_choice(raw_value):
    if raw_value is None:
        return None

    normalized = str(raw_value).strip().lower()
    if not normalized:
        return None
    if normalized in MENU_OPTION_ALIASES:
        return MENU_OPTION_ALIASES[normalized]
    if normalized in MENU_OPTION_LABELS:
        return normalized
    raise ValueError(f"Invalid operation/choice value: {raw_value}")


def parse_map_toolkit_arguments(argv):
    if not argv:
        return None

    first_token = str(argv[0]).strip().lower()
    if first_token == "map":
        toolkit_argv = list(argv[1:])
    elif first_token in MAP_TOOLKIT_ALIAS_TO_ACTION:
        toolkit_argv = [MAP_TOOLKIT_ALIAS_TO_ACTION[first_token]] + list(argv[1:])
    else:
        return None

    parser = argparse.ArgumentParser(
        prog=f"{os.path.basename(__file__)} map",
        description="translations.map maintenance toolkit",
    )
    subparsers = parser.add_subparsers(dest="map_action", required=True)

    import_rc_parser = subparsers.add_parser(
        "import-rc", help="Import STRINGTABLE entries from an RC file."
    )
    import_rc_parser.add_argument("--rc", default=DEFAULT_RC_FILE_PATH)
    import_rc_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)
    import_rc_parser.add_argument("--language", default="en")
    import_rc_parser.add_argument("--overwrite", action="store_true")

    compile_parser = subparsers.add_parser(
        "compile", help="Generate C++ headers from translations.map."
    )
    compile_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)
    compile_parser.add_argument(
        "--data-header", default=DEFAULT_TRANSLATIONS_DATA_HEADER_PATH
    )
    compile_parser.add_argument(
        "--registry-header", default=DEFAULT_LANGUAGE_REGISTRY_HEADER_PATH
    )

    check_parser = subparsers.add_parser(
        "check", help="Validate translations.map structure and placeholders."
    )
    check_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)

    fix_parser = subparsers.add_parser(
        "fix", help="Normalize translations.map and sort KEY blocks."
    )
    fix_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)

    set_parser = subparsers.add_parser(
        "set", help="Set or add a translation entry by KEY and language."
    )
    set_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)
    set_parser.add_argument("--key", required=True)
    set_parser.add_argument("--language", required=True)
    set_parser.add_argument("--text")
    set_parser.add_argument("--create", action="store_true")
    set_parser.add_argument("--backup", action="store_true")

    add_language_parser = subparsers.add_parser(
        "add-language", help="Add a language code to every KEY block."
    )
    add_language_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)
    add_language_parser.add_argument("--language", required=True)
    add_language_parser.add_argument("--backup", action="store_true")

    remove_key_parser = subparsers.add_parser(
        "remove-key", help="Remove an entire KEY block."
    )
    remove_key_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)
    remove_key_parser.add_argument("--key", required=True)
    remove_key_parser.add_argument("--backup", action="store_true")

    clear_parser = subparsers.add_parser(
        "clear-other-languages",
        help='Clear all translations except "en" for a KEY.',
    )
    clear_parser.add_argument("--map", dest="map_path", default=MAP_FILE_PATH)
    clear_parser.add_argument("--key", required=True)
    clear_parser.add_argument("--backup", action="store_true")

    parsed_args = parser.parse_args(toolkit_argv)
    parsed_args.command_group = "map"
    return parsed_args


def add_common_translate_cli_arguments(parser):
    parser.add_argument(
        "--backend",
        choices=(API_BACKEND_ASK, API_BACKEND_LOCAL, API_BACKEND_CLOUD),
        help="Select the translation backend.",
    )
    parser.add_argument(
        "--loop",
        help="Keep running in menu loop mode after the command finishes.",
    )
    parser.add_argument(
        "--stop-on-error",
        help="Abort immediately on the first error.",
    )
    parser.add_argument("--cloud-api-key", help="Override CLOUD_API_KEY.")
    parser.add_argument("--cloud-model-name", help="Override CLOUD_MODEL_NAME.")
    parser.add_argument("--local-api-base-url", help="Override LOCAL_API_BASE_URL.")
    parser.add_argument("--local-api-key", help="Override LOCAL_API_KEY.")
    parser.add_argument("--local-model-name", help="Override LOCAL_MODEL_NAME.")


def finalize_cli_execution_arguments(args):
    args.loop = parse_boolean_cli_value(args.loop, "--loop")
    args.stop_on_error = parse_boolean_cli_value(
        args.stop_on_error, "--stop-on-error"
    )

    if args.rounds is not None and args.rounds < 1:
        raise ValueError("--rounds must be greater than or equal to 1.")
    if args.start_line is not None and args.start_line < 0:
        raise ValueError("--start-line must be greater than or equal to 0.")
    if args.limit is not None and args.limit < 1:
        raise ValueError("--limit must be greater than or equal to 1.")

    return args


def parse_structured_translate_arguments(argv):
    if not argv or str(argv[0]).strip().lower() != "translate":
        return None

    parser = argparse.ArgumentParser(
        prog=f"{os.path.basename(__file__)} translate",
        description="Structured AI translation commands.",
    )
    subparsers = parser.add_subparsers(dest="translate_action", required=True)

    missing_only_parser = subparsers.add_parser(
        "missing-only",
        aliases=["missing"],
        help="Find and complete missing translations only.",
    )
    add_common_translate_cli_arguments(missing_only_parser)
    missing_only_parser.add_argument(
        "--rounds", type=int, help="Translation round count."
    )

    key_list_parser = subparsers.add_parser(
        "key-list",
        aliases=["keys"],
        help="Translate or update specific KEY values.",
    )
    add_common_translate_cli_arguments(key_list_parser)
    key_list_parser.add_argument(
        "--keys", required=True, help="Comma-separated KEY list."
    )
    key_list_parser.add_argument(
        "--rounds", type=int, help="Translation round count."
    )

    clean_key_list_parser = subparsers.add_parser(
        "clean-key-list",
        aliases=["clean-keys"],
        help="Clear then translate specific KEY values.",
    )
    add_common_translate_cli_arguments(clean_key_list_parser)
    clean_key_list_parser.add_argument(
        "--keys", required=True, help="Comma-separated KEY list."
    )
    clean_key_list_parser.add_argument(
        "--rounds", type=int, help="Translation round count."
    )

    clean_lines_parser = subparsers.add_parser(
        "clean-lines",
        aliases=["lines"],
        help="Clear then translate specific translations.map line numbers.",
    )
    add_common_translate_cli_arguments(clean_lines_parser)
    clean_lines_parser.add_argument(
        "--lines",
        "--line-numbers",
        dest="line_numbers",
        required=True,
        help="Comma-separated line numbers.",
    )

    clean_key_languages_parser = subparsers.add_parser(
        "clean-key-languages",
        aliases=["key-languages"],
        help="Clear then translate alternating KEY/language pairs.",
    )
    add_common_translate_cli_arguments(clean_key_languages_parser)
    clean_key_languages_parser.add_argument(
        "--pairs",
        "--key-lang-pairs",
        dest="key_lang_pairs",
        required=True,
        help="Comma-separated alternating KEY and language code values.",
    )

    resume_parser = subparsers.add_parser(
        "resume", help="Resume the last interrupted translation job."
    )
    add_common_translate_cli_arguments(resume_parser)

    full_parser = subparsers.add_parser(
        "full",
        aliases=["full-mapping"],
        help="Run a full mapping pass from the beginning.",
    )
    add_common_translate_cli_arguments(full_parser)

    remove_unused_parser = subparsers.add_parser(
        "remove-unused-keys",
        aliases=["prune-unused-keys"],
        help="Find and remove unused translation keys.",
    )
    add_common_translate_cli_arguments(remove_unused_parser)

    find_missing_parser = subparsers.add_parser(
        "find-missing",
        aliases=["scan-missing"],
        help="Run the fast missing-translation scan.",
    )
    add_common_translate_cli_arguments(find_missing_parser)
    find_missing_parser.add_argument(
        "--start-line", type=int, help="Start scanning from this line."
    )
    find_missing_parser.add_argument(
        "--limit", type=int, help="Maximum number of results to emit."
    )

    fix_map_format_parser = subparsers.add_parser(
        "fix-map-format",
        aliases=["normalize-map"],
        help="Fix and normalize translations.map formatting.",
    )
    add_common_translate_cli_arguments(fix_map_format_parser)

    parsed_args = parser.parse_args(list(argv[1:]))
    choice = TRANSLATE_COMMAND_TO_MENU_CHOICE.get(parsed_args.translate_action)
    if choice is None:
        raise ValueError(
            f"Unsupported structured translation command: {parsed_args.translate_action}"
        )

    return finalize_cli_execution_arguments(
        argparse.Namespace(
            command_group="translate",
            choice=choice,
            operation=None,
            backend=getattr(parsed_args, "backend", None),
            keys=getattr(parsed_args, "keys", None),
            key_lang_pairs=getattr(parsed_args, "key_lang_pairs", None),
            line_numbers=getattr(parsed_args, "line_numbers", None),
            rounds=getattr(parsed_args, "rounds", None),
            start_line=getattr(parsed_args, "start_line", None),
            limit=getattr(parsed_args, "limit", None),
            loop=getattr(parsed_args, "loop", None),
            stop_on_error=getattr(parsed_args, "stop_on_error", None),
            cloud_api_key=getattr(parsed_args, "cloud_api_key", None),
            cloud_model_name=getattr(parsed_args, "cloud_model_name", None),
            local_api_base_url=getattr(parsed_args, "local_api_base_url", None),
            local_api_key=getattr(parsed_args, "local_api_key", None),
            local_model_name=getattr(parsed_args, "local_model_name", None),
        )
    )


def parse_command_line_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Legacy eMule AI translator CLI. Use `translate --help` or `map --help` for structured commands."
    )
    parser.add_argument(
        "--choice",
        help="Menu choice number or alias such as clean-specific-key-languages.",
    )
    parser.add_argument(
        "--operation",
        help="Alias for --choice. Accepts the same values as --choice.",
    )
    parser.add_argument(
        "--backend",
        choices=(API_BACKEND_ASK, API_BACKEND_LOCAL, API_BACKEND_CLOUD),
        help="Override the startup backend selection.",
    )
    parser.add_argument(
        "--keys",
        help="Comma-separated KEY list for operations 2 and 3.",
    )
    parser.add_argument(
        "--key-lang-pairs",
        help="Comma-separated alternating KEY and language code values for operation 5.",
    )
    parser.add_argument(
        "--line-numbers",
        help="Comma-separated line number list for operation 4.",
    )
    parser.add_argument(
        "--rounds",
        type=int,
        help="Translation round count for operations 1, 2, and 3.",
    )
    parser.add_argument(
        "--start-line",
        type=int,
        help="StartLine value for operation 9.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        help="Limit value for operation 9.",
    )
    parser.add_argument(
        "--loop",
        help="Whether to keep the menu loop active after the requested command-line operation.",
    )
    parser.add_argument(
        "--stop-on-error",
        help="Whether command-line execution should stop immediately on the first error.",
    )
    parser.add_argument("--cloud-api-key", help="Override CLOUD_API_KEY.")
    parser.add_argument("--cloud-model-name", help="Override CLOUD_MODEL_NAME.")
    parser.add_argument("--local-api-base-url", help="Override LOCAL_API_BASE_URL.")
    parser.add_argument("--local-api-key", help="Override LOCAL_API_KEY.")
    parser.add_argument("--local-model-name", help="Override LOCAL_MODEL_NAME.")

    args = parser.parse_args(argv)
    choice_values = [value for value in (args.choice, args.operation) if value is not None]
    normalized_choices = [normalize_menu_choice(value) for value in choice_values]
    if normalized_choices and any(choice != normalized_choices[0] for choice in normalized_choices[1:]):
        raise ValueError("--choice and --operation must point to the same menu item when both are provided.")

    args.choice = normalized_choices[0] if normalized_choices else None
    return finalize_cli_execution_arguments(args)


def apply_command_line_overrides(args):
    global API_TYPE
    global CLOUD_API_KEY
    global CLOUD_MODEL_NAME
    global LOCAL_API_BASE_URL
    global LOCAL_API_KEY
    global LOCAL_MODEL_NAME

    if args.backend is not None:
        API_TYPE = args.backend
    if args.cloud_api_key is not None:
        CLOUD_API_KEY = args.cloud_api_key
    if args.cloud_model_name is not None:
        CLOUD_MODEL_NAME = args.cloud_model_name
    if args.local_api_base_url is not None:
        LOCAL_API_BASE_URL = args.local_api_base_url
    if args.local_api_key is not None:
        LOCAL_API_KEY = args.local_api_key
    if args.local_model_name is not None:
        LOCAL_MODEL_NAME = args.local_model_name


def read_cli_or_prompt_value(cli_value, prompt_text, missing_error, default_value=None):
    if cli_value is not None:
        return str(cli_value).strip()
    if not sys.stdin.isatty():
        if default_value is not None:
            return default_value
        raise ValueError(missing_error)
    raw_value = input(prompt_text).strip()
    if not raw_value and default_value is not None:
        return default_value
    return raw_value


def prompt_boolean_option(prompt_text, default_value=False):
    if not sys.stdin.isatty():
        return bool(default_value)

    while True:
        suffix = "Y/n" if default_value else "y/N"
        raw_value = input(f"{prompt_text} [{suffix}]: ").strip()
        if not raw_value:
            return bool(default_value)
        try:
            return bool(parse_boolean_cli_value(raw_value, prompt_text))
        except ValueError:
            print("Invalid value. Enter yes/no, true/false, or 1/0.")


def execute_map_toolkit_action(map_args):
    action = getattr(map_args, "map_action", "")
    if action == "import-rc":
        requested_language = str(map_args.language).strip()
        if requested_language.lower() != "en":
            raise RuntimeError(
                "English ('en') must be imported first. Use `map import-rc --language en`."
            )
        entries = parse_rc_stringtable(map_args.rc)
        if not entries:
            raise RcParseError("No entries found in RC file")
        write_rc_import_map(
            map_args.map_path,
            entries,
            requested_language,
            overwrite=bool(map_args.overwrite),
        )
        print(
            f'OK: Imported {len(entries)} STRINGTABLE entries into "{map_args.map_path}".'
        )
        return 0

    if action == "compile":
        compile_map_to_headers(
            map_args.map_path,
            map_args.data_header,
            map_args.registry_header,
        )
        print(
            "OK: Generated translations_data.gen.h and lang_registry.gen.h compatible headers."
        )
        return 0

    if action == "check":
        check_map_file(map_args.map_path)
        return 0

    if action == "fix":
        fix_map_file(map_args.map_path)
        print("OK: translations.map normalized.")
        return 0

    if action == "set":
        text_value = map_args.text
        if text_value is None:
            if sys.stdin.isatty():
                raise RuntimeError(
                    "--text is required for `map set` when stdin is interactive."
                )
            text_value = sys.stdin.read()
        print(
            set_translation_entry_in_map(
                map_args.map_path,
                map_args.key,
                map_args.language,
                text_value,
                create_if_missing=bool(map_args.create),
                backup=bool(map_args.backup),
            )
        )
        return 0

    if action == "add-language":
        print(
            add_language_to_map(
                map_args.map_path,
                map_args.language,
                backup=bool(map_args.backup),
            )
        )
        return 0

    if action == "remove-key":
        print(
            remove_key_from_map(
                map_args.map_path, map_args.key, backup=bool(map_args.backup)
            )
        )
        return 0

    if action == "clear-other-languages":
        print(
            clear_other_languages_for_key(
                map_args.map_path, map_args.key, backup=bool(map_args.backup)
            )
        )
        return 0

    raise RuntimeError(f"Unsupported map action: {action}")


def run_map_toolkit_menu_operation(choice):
    if choice == "11":
        rc_path = read_cli_or_prompt_value(
            None,
            f'RC file path (default "{DEFAULT_RC_FILE_PATH}"): ',
            "",
            default_value=DEFAULT_RC_FILE_PATH,
        )
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        language = read_cli_or_prompt_value(
            None, 'Language code (default "en"): ', "", default_value="en"
        )
        overwrite = prompt_boolean_option("Overwrite destination file", False)
        return execute_map_toolkit_action(
            argparse.Namespace(
                map_action="import-rc",
                rc=rc_path,
                map_path=map_path,
                language=language,
                overwrite=overwrite,
            )
        )

    if choice == "12":
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        data_header = read_cli_or_prompt_value(
            None,
            f'Data header output (default "{DEFAULT_TRANSLATIONS_DATA_HEADER_PATH}"): ',
            "",
            default_value=DEFAULT_TRANSLATIONS_DATA_HEADER_PATH,
        )
        registry_header = read_cli_or_prompt_value(
            None,
            f'Registry header output (default "{DEFAULT_LANGUAGE_REGISTRY_HEADER_PATH}"): ',
            "",
            default_value=DEFAULT_LANGUAGE_REGISTRY_HEADER_PATH,
        )
        return execute_map_toolkit_action(
            argparse.Namespace(
                map_action="compile",
                map_path=map_path,
                data_header=data_header,
                registry_header=registry_header,
            )
        )

    if choice == "13":
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        return execute_map_toolkit_action(
            argparse.Namespace(map_action="check", map_path=map_path)
        )

    if choice == "14":
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        key_name = read_cli_or_prompt_value(None, "KEY: ", "KEY is required.")
        language = read_cli_or_prompt_value(
            None, "Language code: ", "Language code is required."
        )
        text_value = input(
            "Text to write (use literal escapes like \\n when needed): "
        )
        create_if_missing = prompt_boolean_option("Create KEY if missing", False)
        create_backup = prompt_boolean_option("Create backup before writing", False)
        return execute_map_toolkit_action(
            argparse.Namespace(
                map_action="set",
                map_path=map_path,
                key=key_name,
                language=language,
                text=text_value,
                create=create_if_missing,
                backup=create_backup,
            )
        )

    if choice == "15":
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        language = read_cli_or_prompt_value(
            None, "Language code to add: ", "Language code is required."
        )
        create_backup = prompt_boolean_option("Create backup before writing", False)
        return execute_map_toolkit_action(
            argparse.Namespace(
                map_action="add-language",
                map_path=map_path,
                language=language,
                backup=create_backup,
            )
        )

    if choice == "16":
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        key_name = read_cli_or_prompt_value(None, "KEY to remove: ", "KEY is required.")
        create_backup = prompt_boolean_option("Create backup before writing", False)
        return execute_map_toolkit_action(
            argparse.Namespace(
                map_action="remove-key",
                map_path=map_path,
                key=key_name,
                backup=create_backup,
            )
        )

    if choice == "17":
        map_path = read_cli_or_prompt_value(
            None,
            f'translations.map path (default "{MAP_FILE_PATH}"): ',
            "",
            default_value=MAP_FILE_PATH,
        )
        key_name = read_cli_or_prompt_value(None, "KEY to clear: ", "KEY is required.")
        create_backup = prompt_boolean_option("Create backup before writing", False)
        return execute_map_toolkit_action(
            argparse.Namespace(
                map_action="clear-other-languages",
                map_path=map_path,
                key=key_name,
                backup=create_backup,
            )
        )

    raise RuntimeError(f"Unsupported menu toolkit choice: {choice}")


def print_total_elapsed_time():
    if SCRIPT_START_TIME is not None:
        print(f"Total elapsed time: {format_elapsed_time(time.time() - SCRIPT_START_TIME)}")


def handle_operation_error(message, stop_on_error=False):
    if stop_on_error:
        raise OperationAbortError(message)


def get_active_backend_key():
    return API_BACKEND_LOCAL if resolve_backend_selection() else API_BACKEND_CLOUD


def collect_runtime_override_settings(cli_args=None):
    runtime_settings = {
        "backend": get_active_backend_key(),
    }
    if cli_args is None:
        return runtime_settings

    for attr_name in (
        "cloud_api_key",
        "cloud_model_name",
        "local_api_base_url",
        "local_api_key",
        "local_model_name",
    ):
        value = getattr(cli_args, attr_name, None)
        if value is not None:
            runtime_settings[attr_name] = value

    return runtime_settings


def apply_runtime_override_settings(runtime_settings):
    global API_TYPE
    global CLOUD_API_KEY
    global CLOUD_MODEL_NAME
    global LOCAL_API_BASE_URL
    global LOCAL_API_KEY
    global LOCAL_MODEL_NAME

    if not isinstance(runtime_settings, dict):
        return

    backend_value = runtime_settings.get("backend")
    if backend_value in (API_BACKEND_LOCAL, API_BACKEND_CLOUD, API_BACKEND_ASK):
        API_TYPE = backend_value

    if "cloud_api_key" in runtime_settings:
        CLOUD_API_KEY = runtime_settings.get("cloud_api_key", "")
    if "cloud_model_name" in runtime_settings:
        CLOUD_MODEL_NAME = runtime_settings.get("cloud_model_name", "")
    if "local_api_base_url" in runtime_settings:
        LOCAL_API_BASE_URL = runtime_settings.get("local_api_base_url", "")
    if "local_api_key" in runtime_settings:
        LOCAL_API_KEY = runtime_settings.get("local_api_key", "")
    if "local_model_name" in runtime_settings:
        LOCAL_MODEL_NAME = runtime_settings.get("local_model_name", "")


def ensure_translation_backend_ready(prompt_user=True):
    backend_is_local = resolve_backend_selection(prompt_user=prompt_user)
    if not backend_is_local and not CLOUD_API_KEY:
        raise RuntimeError("CLOUD_API_KEY environment variable is not set.")
    return backend_is_local


def print_active_backend_info():
    backend_is_local = ensure_translation_backend_ready(prompt_user=False)
    print(f"Info: Backend = {get_active_backend_label()}")
    if backend_is_local:
        print(f"Info: URL = {LOCAL_API_BASE_URL.rstrip('/')}/chat/completions")
    print(f"Info: Model = {get_active_model_name()}")


def sanitize_resume_unresolved_keys(unresolved_keys):
    sanitized = {}
    if not isinstance(unresolved_keys, dict):
        return sanitized

    for key_name, lang_list in unresolved_keys.items():
        if not isinstance(key_name, str):
            continue
        if not isinstance(lang_list, (list, tuple, set)):
            continue
        sanitized[key_name] = sorted(
            {
                str(lang_code).strip()
                for lang_code in lang_list
                if str(lang_code).strip()
            }
        )

    return sanitized


def build_resume_state(choice, params, runtime_settings=None, progress=None):
    return {
        "version": RESUME_STATE_VERSION,
        "saved_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "operation": {
            "choice": str(choice),
            "label": MENU_OPTION_LABELS.get(str(choice), ""),
        },
        "params": dict(params or {}),
        "runtime": dict(runtime_settings or {}),
        "progress": dict(progress or {}),
    }


def build_legacy_resume_state(resume_key):
    normalized_resume_key = str(resume_key).strip()
    return {
        "version": "legacy",
        "saved_at": "",
        "legacy_resume_mode": True,
        "operation": {
            "choice": "6",
            "label": "Legacy single-key mapping resume",
        },
        "params": {
            "resume_key": normalized_resume_key,
            "stop_on_error": False,
        },
        "runtime": {},
        "progress": {
            "legacy_resume_key": normalized_resume_key,
        },
    }


def is_legacy_resume_state(resume_state):
    return bool(
        isinstance(resume_state, dict) and resume_state.get("legacy_resume_mode")
    )


def save_resume_state(resume_state):
    if not isinstance(resume_state, dict):
        return

    resume_state["version"] = RESUME_STATE_VERSION
    resume_state["saved_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    with open(RESUME_POINT_FILE, "w", encoding="utf-8") as resume_file:
        json.dump(resume_state, resume_file, ensure_ascii=False, indent=2, sort_keys=True)
        resume_file.write("\n")


def load_resume_state():
    if not os.path.exists(RESUME_POINT_FILE):
        return None

    with open(RESUME_POINT_FILE, "r", encoding="utf-8", errors="ignore") as resume_file:
        raw_content = resume_file.read().strip()

    if not raw_content:
        return None

    try:
        resume_state = json.loads(raw_content)
    except json.JSONDecodeError:
        legacy_resume_key = raw_content.strip()
        if not legacy_resume_key:
            return None
        return build_legacy_resume_state(legacy_resume_key)

    if not isinstance(resume_state, dict):
        raise ValueError("Resume file format is invalid.")
    if is_legacy_resume_state(resume_state):
        return resume_state
    if str(resume_state.get("version", "")) != str(RESUME_STATE_VERSION):
        raise ValueError(
            f"Unsupported resume file version: {resume_state.get('version')}"
        )

    operation = resume_state.get("operation", {})
    if normalize_menu_choice(operation.get("choice")) not in {"1", "2", "3", "4", "5", "7"}:
        raise ValueError("Resume file does not contain a supported multi-item translation operation.")

    resume_state.setdefault("params", {})
    resume_state.setdefault("runtime", {})
    resume_state.setdefault("progress", {})
    resume_state["progress"]["accumulated_unresolved_keys"] = sanitize_resume_unresolved_keys(
        resume_state["progress"].get("accumulated_unresolved_keys", {})
    )
    return resume_state


def delete_resume_state_file():
    if os.path.exists(RESUME_POINT_FILE):
        os.remove(RESUME_POINT_FILE)
        print(f"{os.path.basename(RESUME_POINT_FILE)} deleted.")


def serialize_key_language_pairs(key_language_pairs):
    serialized_pairs = []
    for key_name, lang_code in key_language_pairs or []:
        serialized_pairs.append({"key": key_name, "lang_code": lang_code})
    return serialized_pairs


def deserialize_key_language_pairs(serialized_pairs):
    key_language_pairs = []
    for item in serialized_pairs or []:
        if not isinstance(item, dict):
            continue
        key_name = str(item.get("key", "")).strip()
        lang_code = str(item.get("lang_code", "")).strip()
        if not key_name or not lang_code:
            continue
        key_language_pairs.append((key_name, lang_code))
    return key_language_pairs


def parse_line_numbers_input(raw_input_text):
    line_numbers = []
    seen_lines = set()

    for part in str(raw_input_text).split(","):
        stripped_part = part.strip()
        if not stripped_part:
            continue
        try:
            line_num = int(stripped_part)
            if line_num <= 0:
                print(f"Warning: Line number {line_num} is not positive, skipping.")
                continue
            if line_num in seen_lines:
                continue
            line_numbers.append(line_num)
            seen_lines.add(line_num)
        except ValueError:
            print(f"Warning: '{stripped_part}' is not a valid number, skipping.")

    line_numbers.sort()
    return line_numbers


def get_all_source_files():
    cpp_files = []
    # Directories that should not be scanned or are unnecessary
    skip_dirs = {".git", "_transmission", "_eMule_v0.70b", "loglar", "translations"}
    for root, dirs, files in os.walk(SRCHYBRID_DIR):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            if f.lower() == "translations_data.gen.h":
                continue
            if f.lower().endswith((".cpp", ".h", ".inl", ".rc", ".hpp", ".c")):
                cpp_files.append(os.path.join(root, f))
    return cpp_files


def collect_reference_lines():
    print("Scanning source files for lines containing '_T(\"'...")
    files = get_all_source_files()
    lines_collected = []
    for fp in files:
        try:
            with open(fp, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    if '_T("' in line:
                        lines_collected.append(line.strip())
        except Exception:
            pass

    print(f"A total of {len(lines_collected)} lines containing '_T(\"' have been collected.")
    return lines_collected


def remove_unused_keys_logic(stop_on_error=False):
    print("\neMule Unused Translations Cleaner Tool Starting...")

    lines = collect_reference_lines()
    keys_data = get_keys_from_map(MAP_FILE_PATH)
    keys = [item["key"] for item in keys_data]

    unused_keys = []
    error_count = 0
    print("Analyzing unused keys (this may take a few seconds)...")
    for key in keys:
        used = False
        search_str1 = f'_T("{key}")'
        search_str2 = f'_T("<{key}>")'
        for line in lines:
            if search_str1 in line or search_str2 in line:
                used = True
                break
        if not used:
            unused_keys.append(key)

    print(
        f"Analysis complete: Out of {len(keys)} total keys, {len(unused_keys)} were found to be unused."
    )

    if len(unused_keys) > 0:
        print("Removing unused keys from translations.map...")
        for key in unused_keys:
            try:
                print(remove_key_from_map(MAP_FILE_PATH, key, backup=False))
            except Exception as e:
                print(f"ERROR (System): Failed to remove {key}: {e}")
                error_count += 1
                handle_operation_error(
                    f"Failed to remove {key}: {e}", stop_on_error
                )

        print("All unused keys have been successfully cleaned.")
    else:
        print("No unused keys found to delete. No action required.")

    return error_count


def find_missing_translations_logic(start_line=None, limit=None):
    print("\n--- Find Missing Translations (Fast Scan) ---")
    if start_line is None:
        start_line_str = read_cli_or_prompt_value(
            None, "Enter StartLine (default 0): ", "", default_value="0"
        )
        start_line = int(start_line_str) if start_line_str.isdigit() else 0
    if limit is None:
        limit_str = read_cli_or_prompt_value(
            None, "Enter Limit (default 10): ", "", default_value="10"
        )
        limit = int(limit_str) if limit_str.isdigit() else 10

    if not os.path.exists(MAP_FILE_PATH):
        print(f"ERROR: {MAP_FILE_PATH} not found!")
        return 1

    results = []
    current_key = ""
    current_en = ""
    current_line_num = 0

    with open(MAP_FILE_PATH, "r", encoding="utf-8", errors="ignore") as f:
        for line_raw in f:
            current_line_num += 1
            line = line_raw.rstrip("\n\r")

            if current_line_num < start_line:
                if not line.startswith("\t") and line.strip():
                    current_key = line.strip()
                    current_en = ""
                elif line.startswith("\ten\t"):
                    parts = line.split("\t", 2)
                    if len(parts) > 2:
                        current_en = parts[2]
                continue

            if not line.strip():
                continue

            if not line.startswith("\t"):
                current_key = line.strip()
                current_en = ""
            elif line.startswith("\ten\t"):
                parts = line.split("\t", 2)
                if len(parts) > 2:
                    current_en = parts[2]
            else:
                m = re.match(r"^\t([^\t]+)\t\s*$", line)
                if m:
                    lang = m.group(1)
                    if current_key and current_en:
                        results.append(
                            {
                                "Key": current_key,
                                "Lang": lang,
                                "EnText": current_en,
                                "Line": current_line_num,
                            }
                        )
                        if len(results) >= limit:
                            break

    output_data = {"NextStartLine": current_line_num + 1, "Items": results}

    print("\n" + json.dumps(output_data, ensure_ascii=False, indent=2))
    return 0


def fix_translations_map_logic(stop_on_error=False):
    print("\n--- Fixing translations.map formatting ---")
    try:
        fix_map_file(MAP_FILE_PATH)
        print("Successfully fixed translations.map formatting.")
    except Exception as e:
        print(f"ERROR (System): Failed to fix translations.map: {e}")
        handle_operation_error(f"Failed to fix translations.map: {e}", stop_on_error)
        return 1
    return 0


def prompt_translation_round_count(cli_value=None):
    if cli_value is not None:
        return int(cli_value)
    if not sys.stdin.isatty():
        return 1

    while True:
        raw_value = input("Enter translation round count (default 1): ").strip()
        if not raw_value:
            return 1
        if raw_value.isdigit() and int(raw_value) >= 1:
            return int(raw_value)
        print(
            "Invalid round count. Please enter a numeric value greater than or equal to 1."
        )


def should_require_translation_completion(en_text, existing_text):
    if is_invariant_translation_source(en_text):
        if not isinstance(existing_text, str):
            return True
        return existing_text != en_text

    if not isinstance(existing_text, str):
        return True

    if not existing_text.strip():
        return True

    if existing_text == en_text:
        return True

    placeholder_pairs = build_protected_placeholders(
        en_text, {"candidate": existing_text}
    )
    is_valid, _ = validate_translation_text(en_text, existing_text, placeholder_pairs)
    return not is_valid


def build_pending_language_map(en_text, target_langs):
    pending = {}
    for lang_code, current_text in target_langs.items():
        if should_require_translation_completion(en_text, current_text):
            pending[lang_code] = current_text
    return pending


def try_fix_language_until_valid(
    key_name, lang_code, candidate_text, en_text, last_error_message
):
    latest_text = candidate_text
    error_message = last_error_message or "translation requires completion"

    for attempt in range(1, TRANSLATION_COMPLETION_PER_LANGUAGE_FIX_RETRIES + 1):
        print(
            f"  -> [{lang_code}] Attempting targeted completion fix ({attempt}/{TRANSLATION_COMPLETION_PER_LANGUAGE_FIX_RETRIES})..."
        )
        time.sleep(API_DELAY_SEC)
        extra_requirements = build_embedded_phrase_fix_requirements(
            en_text, latest_text if isinstance(latest_text, str) else "", lang_code
        )
        extra_requirements += build_compiler_percent_fix_requirements(
            en_text, latest_text if isinstance(latest_text, str) else ""
        )
        fixed_text = fix_translation_with_cloud(
            key_name,
            lang_code,
            latest_text if isinstance(latest_text, str) else "",
            error_message,
            en_text,
            extra_requirements=extra_requirements,
        )

        retry_count = 0
        while fixed_text is None and retry_count < 3:
            retry_delay_sec = get_active_retry_delay_sec()
            print(
                f"  -> [{lang_code}] Failed to get targeted completion fix from {get_active_backend_label()}. Retrying in {retry_delay_sec} seconds (Attempt {retry_count + 1}/3)..."
            )
            time.sleep(retry_delay_sec)
            fixed_text = fix_translation_with_cloud(
                key_name,
                lang_code,
                latest_text if isinstance(latest_text, str) else "",
                error_message,
                en_text,
                extra_requirements=extra_requirements,
            )
            retry_count += 1

        if fixed_text is None:
            continue

        latest_text = fixed_text
        success, msg = update_translation_via_compiler(
            key_name, lang_code, latest_text, en_text
        )
        if success:
            print(f"  -> [{lang_code}] Successfully completed via targeted fix: {msg}")
            return True, latest_text, msg

        print(f"  -> [{lang_code}] Targeted completion fix failed: {msg}")
        error_message = msg

    return False, latest_text, error_message


def get_mini_retry_batch_size(en_text="", lang_count=0):
    if resolve_backend_selection():
        return get_local_effective_batch_size(
            LOCAL_API_MAX_LANGUAGES_PER_BATCH, en_text, lang_count
        )
    return CLOUD_API_MAX_LANGUAGES_PER_BATCH


def get_failed_language_batch_size():
    if resolve_backend_selection():
        return max(1, LOCAL_API_FAILED_LANGUAGE_MAX_LANGUAGES_PER_BATCH)
    return 1


def retry_translation_chunk(key_name, en_text, batch_langs, error_context=""):
    updates = check_and_translate_with_cloud(
        key_name, en_text, batch_langs, error_context=error_context
    )
    retry_count = 0
    while (
        updates is None
        and LAST_BATCH_RESULT_STATUS in ("call_failure", "parse_failure")
        and retry_count < TRANSLATION_CHUNK_RETRY_COUNT
    ):
        retry_delay_sec = get_active_retry_delay_sec()
        print(
            f"[{key_name}] Chunk API call or response parsing failed. Retrying in {retry_delay_sec} seconds (Attempt {retry_count + 1}/{TRANSLATION_CHUNK_RETRY_COUNT})..."
        )
        time.sleep(retry_delay_sec)
        updates = check_and_translate_with_cloud(
            key_name, en_text, batch_langs, error_context=error_context
        )
        retry_count += 1
    return updates


def should_attempt_candidate_repair(failure_reason):
    lowered_reason = str(failure_reason).lower()
    return any(
        fragment in lowered_reason
        for fragment in (
            "untranslated embedded english phrase",
            "untranslated embedded english word",
            "unexpected script mixture",
            "suspicious transliterated or foreign token",
        )
    )


def try_repair_skipped_candidate_immediately(
    key_name, lang_code, en_text, pending_states, failure_reason
):
    candidate_text = LAST_BATCH_LANGUAGE_CANDIDATES.get(lang_code, "")
    if not (isinstance(candidate_text, str) and candidate_text.strip()):
        return False
    if not should_attempt_candidate_repair(failure_reason):
        return False

    repaired_success, repaired_text, repaired_msg = try_phrase_only_repair(
        key_name, lang_code, candidate_text, en_text
    )
    if not repaired_success:
        print(
            f"  -> [{lang_code}] Phrase-only repair did not resolve the skipped candidate: {repaired_msg}"
        )
        pending_states[lang_code]["text"] = candidate_text
        return False

    print(
        f"  -> [{lang_code}] Successfully repaired skipped candidate and updated: {repaired_msg}"
    )
    if lang_code in pending_states:
        del pending_states[lang_code]
    return True


def process_failed_languages_immediately(
    key_name, en_text, failed_lang_codes, pending_states
):
    if not failed_lang_codes:
        return False

    made_any_change = False
    focused_batch_size = get_failed_language_batch_size()
    failed_queue = list(dict.fromkeys(failed_lang_codes))
    if should_force_single_language_local_batch(failed_queue, en_text):
        focused_batch_size = 1

    while failed_queue:
        current_langs = failed_queue[:focused_batch_size]
        del failed_queue[:focused_batch_size]

        batch_langs = {
            lang_code: pending_states[lang_code].get("text", "")
            for lang_code in current_langs
            if lang_code in pending_states
        }
        if not batch_langs:
            continue

        last_error_context = ""
        if len(batch_langs) == 1:
            only_lang_code = next(iter(batch_langs.keys()))
            last_error_context = pending_states.get(only_lang_code, {}).get(
                "last_error", ""
            )

        for attempt_index in range(1, TRANSLATION_FAILED_LANGUAGE_RETRY_COUNT + 1):
            if len(batch_langs) == 1:
                print(
                    f"[{key_name}] Failed-language retry for [{next(iter(batch_langs.keys()))}] ({attempt_index}/{TRANSLATION_FAILED_LANGUAGE_RETRY_COUNT})..."
                )
            else:
                print(
                    f"[{key_name}] Failed-language retry batch for {len(batch_langs)} language(s) ({attempt_index}/{TRANSLATION_FAILED_LANGUAGE_RETRY_COUNT})..."
                )

            updates = retry_translation_chunk(
                key_name, en_text, batch_langs, error_context=last_error_context
            )
            if updates is None:
                for lang_code in batch_langs.keys():
                    if lang_code not in pending_states:
                        continue
                    if LAST_BATCH_RESULT_STATUS in ("call_failure", "parse_failure"):
                        pending_states[lang_code]["last_error"] = (
                            "chunk API call failed"
                            if LAST_BATCH_RESULT_STATUS == "call_failure"
                            else "line-based response parse failed"
                        )
                    else:
                        missing_reason = LAST_BATCH_LANGUAGE_ERRORS.get(
                            lang_code, "translation was rejected during validation"
                        )
                        pending_states[lang_code]["last_error"] = missing_reason
                        if try_repair_skipped_candidate_immediately(
                            key_name, lang_code, en_text, pending_states, missing_reason
                        ):
                            made_any_change = True
                continue

            retry_needed = []
            for lang_code in list(batch_langs.keys()):
                if lang_code not in pending_states:
                    continue

                state = pending_states[lang_code]
                candidate_text = updates.get(lang_code, state.get("text", ""))
                if not (isinstance(candidate_text, str) and candidate_text.strip()):
                    missing_reason = LAST_BATCH_LANGUAGE_ERRORS.get(
                        lang_code, "language was not returned by the translation model"
                    )
                    state["last_error"] = missing_reason
                    if try_repair_skipped_candidate_immediately(
                        key_name, lang_code, en_text, pending_states, missing_reason
                    ):
                        made_any_change = True
                        continue
                    retry_needed.append(lang_code)
                    continue

                success, msg = update_translation_via_compiler(
                    key_name, lang_code, candidate_text, en_text
                )
                if success:
                    print(
                        f"  -> [{lang_code}] Successfully added/updated after immediate retry: {msg}"
                    )
                    made_any_change = True
                    del pending_states[lang_code]
                    continue

                print(
                    f"  -> [{lang_code}] Map update error after immediate retry: {msg}"
                )
                if should_attempt_candidate_repair(msg):
                    repaired_success, repaired_text, repaired_msg = (
                        try_phrase_only_repair(
                            key_name, lang_code, candidate_text, en_text
                        )
                    )
                    if repaired_success:
                        print(
                            f"  -> [{lang_code}] Successfully repaired leaked UI phrase and updated: {repaired_msg}"
                        )
                        made_any_change = True
                        del pending_states[lang_code]
                        continue
                    print(
                        f"  -> [{lang_code}] Phrase-only repair did not resolve the issue: {repaired_msg}"
                    )

                fixed_success, fixed_text, fixed_msg = try_fix_language_until_valid(
                    key_name, lang_code, candidate_text, en_text, msg
                )
                if fixed_success:
                    made_any_change = True
                    del pending_states[lang_code]
                    continue

                state["text"] = candidate_text
                state["last_error"] = fixed_msg
                retry_needed.append(lang_code)

            if not retry_needed:
                break

            batch_langs = {
                lang_code: pending_states[lang_code].get("text", "")
                for lang_code in retry_needed
                if lang_code in pending_states
            }
            if len(batch_langs) == 1:
                only_lang_code = next(iter(batch_langs.keys()))
                last_error_context = pending_states.get(only_lang_code, {}).get(
                    "last_error", ""
                )
            else:
                last_error_context = ""

            if attempt_index < TRANSLATION_FAILED_LANGUAGE_RETRY_COUNT:
                time.sleep(API_DELAY_SEC)

    return made_any_change


def complete_key_translations(key_name, en_text, target_langs, batch_size):
    pending_langs = build_pending_language_map(en_text, target_langs)
    if not pending_langs:
        return {
            "made_any_change": False,
            "remaining_languages": [],
            "had_pending_languages": False,
        }

    effective_batch_size = get_translation_completion_limits(
        batch_size, en_text, len(pending_langs)
    )
    made_any_change = False
    pending_states = {
        lang_code: {
            "text": text,
            "last_error": "",
        }
        for lang_code, text in pending_langs.items()
    }

    pending_order = sorted(pending_states.keys())
    if should_force_single_language_local_batch(pending_order, en_text):
        effective_batch_size = 1
    total_chunks = (
        len(pending_order) + effective_batch_size - 1
    ) // effective_batch_size
    print(
        f"[{key_name}] Processing {len(pending_order)} remaining language(s) in {total_chunks} chunk(s)..."
    )

    chunk_counter = 0
    start_index = 0
    while start_index < len(pending_order):
        current_langs = pending_order[start_index : start_index + effective_batch_size]
        start_index += effective_batch_size
        current_langs = [
            lang_code for lang_code in current_langs if lang_code in pending_states
        ]
        if not current_langs:
            continue

        chunk_counter += 1
        batch_langs = {
            lang_code: pending_states[lang_code].get("text", "")
            for lang_code in current_langs
        }
        print(
            f"[{key_name}] Completion chunk {chunk_counter}/{total_chunks} for {len(batch_langs)} language(s)..."
        )

        error_context = ""
        if len(batch_langs) == 1:
            only_lang_code = next(iter(batch_langs.keys()))
            error_context = pending_states.get(only_lang_code, {}).get("last_error", "")

        updates = retry_translation_chunk(
            key_name, en_text, batch_langs, error_context=error_context
        )
        failed_lang_codes = []

        if updates is None:
            if LAST_BATCH_RESULT_STATUS in ("call_failure", "parse_failure"):
                print(
                    f"[{key_name}] Chunk failed after grouped retries. Switching its languages to immediate retry mode."
                )
            for lang_code in current_langs:
                if lang_code not in pending_states:
                    continue
                state = pending_states[lang_code]
                if LAST_BATCH_RESULT_STATUS in ("call_failure", "parse_failure"):
                    state["last_error"] = (
                        "chunk API call failed"
                        if LAST_BATCH_RESULT_STATUS == "call_failure"
                        else "line-based response parse failed"
                    )
                else:
                    missing_reason = LAST_BATCH_LANGUAGE_ERRORS.get(
                        lang_code, "translation was rejected during validation"
                    )
                    state["last_error"] = missing_reason
                    if try_repair_skipped_candidate_immediately(
                        key_name, lang_code, en_text, pending_states, missing_reason
                    ):
                        made_any_change = True
                        continue
                failed_lang_codes.append(lang_code)
        else:
            for lang_code in current_langs:
                if lang_code not in pending_states:
                    continue

                state = pending_states[lang_code]
                candidate_text = updates.get(lang_code, state.get("text", ""))
                if not (isinstance(candidate_text, str) and candidate_text.strip()):
                    missing_reason = LAST_BATCH_LANGUAGE_ERRORS.get(
                        lang_code, "language was not returned by the translation model"
                    )
                    state["last_error"] = missing_reason
                    if try_repair_skipped_candidate_immediately(
                        key_name, lang_code, en_text, pending_states, missing_reason
                    ):
                        made_any_change = True
                        continue
                    failed_lang_codes.append(lang_code)
                    continue

                success, msg = update_translation_via_compiler(
                    key_name, lang_code, candidate_text, en_text
                )
                if success:
                    print(f"  -> [{lang_code}] Successfully added/updated: {msg}")
                    made_any_change = True
                    del pending_states[lang_code]
                    continue

                print(f"  -> [{lang_code}] Map update error: {msg}")
                if should_attempt_candidate_repair(msg):
                    repaired_success, repaired_text, repaired_msg = (
                        try_phrase_only_repair(
                            key_name, lang_code, candidate_text, en_text
                        )
                    )
                    if repaired_success:
                        print(
                            f"  -> [{lang_code}] Successfully repaired leaked UI phrase and updated: {repaired_msg}"
                        )
                        made_any_change = True
                        del pending_states[lang_code]
                        continue
                    print(
                        f"  -> [{lang_code}] Phrase-only repair did not resolve the issue: {repaired_msg}"
                    )

                fixed_success, fixed_text, fixed_msg = try_fix_language_until_valid(
                    key_name, lang_code, candidate_text, en_text, msg
                )
                if fixed_success:
                    made_any_change = True
                    del pending_states[lang_code]
                    continue

                state["text"] = candidate_text
                state["last_error"] = fixed_msg
                failed_lang_codes.append(lang_code)

        if failed_lang_codes:
            if process_failed_languages_immediately(
                key_name, en_text, failed_lang_codes, pending_states
            ):
                made_any_change = True

        if start_index < len(pending_order):
            time.sleep(API_DELAY_SEC)

    remaining_languages = sorted(pending_states.keys())
    if remaining_languages:
        print(
            f"[{key_name}] Warning: Could not complete all languages for this key. Remaining: {', '.join(remaining_languages)}"
        )
    else:
        print(
            f"[{key_name}] Completion check finished with no remaining untranslated languages."
        )

    return {
        "made_any_change": made_any_change,
        "remaining_languages": remaining_languages,
        "had_pending_languages": True,
    }


def process_translation_pass(
    keys_list,
    global_fill_only_missing=False,
    skip_mode=False,
    resume_key=None,
    specific_keys=None,
    update_resume_point=True,
    stop_on_error=False,
):
    specific_key_mode = specific_keys is not None
    specific_keys_set = set(specific_keys) if specific_keys is not None else set()
    found_specific_keys = set()
    processed_keys = []
    processed_keys_seen = set()
    unresolved_keys = {}

    for item in keys_list:
        k_name = item["key"]
        k_langs = item["langs"]

        if specific_key_mode:
            if k_name not in specific_keys_set:
                continue
            found_specific_keys.add(k_name)
            fill_only_missing = False
        else:
            fill_only_missing = global_fill_only_missing
            if skip_mode:
                if k_name == resume_key:
                    skip_mode = False
                    print(f"\n====== Resuming from ({k_name}) ======")
                else:
                    fill_only_missing = True

        en_text = k_langs.get("en", "")
        if not en_text:
            if not fill_only_missing:
                print(f"[{k_name}] Skipped: English (en) translation is missing.")
            continue

        target_langs = {l: t for l, t in k_langs.items() if l != "en"}

        if fill_only_missing:
            target_langs = {l: t for l, t in target_langs.items() if not t.strip()}
            if not target_langs:
                continue
            print(f"[{k_name}] Analyzing (Missing translations only)...")
        else:
            print(f"[{k_name}] Analyzing...")
            if not target_langs:
                print(f"[{k_name}] Skipped: No foreign languages to process.")
                continue

        if k_name not in processed_keys_seen:
            processed_keys.append(k_name)
            processed_keys_seen.add(k_name)

        key_start_time = time.time()
        en_len = len(en_text) if len(en_text) > 0 else 1
        batch_size = max(1, 10000 // en_len)
        batch_size = min(
            batch_size,
            LOCAL_API_MAX_LANGUAGES_PER_BATCH
            if resolve_backend_selection()
            else CLOUD_API_MAX_LANGUAGES_PER_BATCH,
        )

        key_result = complete_key_translations(
            k_name, en_text, target_langs, batch_size
        )
        if key_result["remaining_languages"]:
            unresolved_keys[k_name] = key_result["remaining_languages"]
            handle_operation_error(
                f"[{k_name}] Unresolved languages remain: {', '.join(key_result['remaining_languages'])}",
                stop_on_error,
            )
        if not key_result["had_pending_languages"] or (
            not key_result["made_any_change"] and not key_result["remaining_languages"]
        ):
            print(f"[{k_name}] No changes required, all languages are appropriate.")
        elif not key_result["made_any_change"] and key_result["remaining_languages"]:
            print(
                f"[{k_name}] No translations could be applied for the remaining language(s): {', '.join(key_result['remaining_languages'])}"
            )
        print(
            f"[{k_name}] Elapsed time: {format_elapsed_time(time.time() - key_start_time)}"
        )

        if not fill_only_missing and update_resume_point:
            with open(RESUME_POINT_FILE, "w", encoding="utf-8") as wf:
                wf.write(k_name)

        time.sleep(API_DELAY_SEC)

    return {
        "processed_keys": processed_keys,
        "found_specific_keys": found_specific_keys,
        "skip_mode": skip_mode,
        "unresolved_keys": unresolved_keys,
    }


def process_translation_key_item(
    key_name, key_languages, fill_only_missing=False, stop_on_error=False
):
    en_text = key_languages.get("en", "")
    if not en_text:
        if not fill_only_missing:
            print(f"[{key_name}] Skipped: English (en) translation is missing.")
        return {"processed": False, "remaining_languages": []}

    target_langs = {lang_code: text for lang_code, text in key_languages.items() if lang_code != "en"}
    if fill_only_missing:
        target_langs = {
            lang_code: text for lang_code, text in target_langs.items() if not text.strip()
        }
        if not target_langs:
            return {"processed": False, "remaining_languages": []}
        print(f"[{key_name}] Analyzing (Missing translations only)...")
    else:
        print(f"[{key_name}] Analyzing...")
        if not target_langs:
            print(f"[{key_name}] Skipped: No foreign languages to process.")
            return {"processed": False, "remaining_languages": []}

    key_start_time = time.time()
    en_len = len(en_text) if len(en_text) > 0 else 1
    batch_size = max(1, 10000 // en_len)
    batch_size = min(
        batch_size,
        LOCAL_API_MAX_LANGUAGES_PER_BATCH
        if resolve_backend_selection()
        else CLOUD_API_MAX_LANGUAGES_PER_BATCH,
    )

    key_result = complete_key_translations(key_name, en_text, target_langs, batch_size)
    if key_result["remaining_languages"]:
        handle_operation_error(
            f"[{key_name}] Unresolved languages remain: {', '.join(key_result['remaining_languages'])}",
            stop_on_error,
        )
    if not key_result["had_pending_languages"] or (
        not key_result["made_any_change"] and not key_result["remaining_languages"]
    ):
        print(f"[{key_name}] No changes required, all languages are appropriate.")
    elif not key_result["made_any_change"] and key_result["remaining_languages"]:
        print(
            f"[{key_name}] No translations could be applied for the remaining language(s): {', '.join(key_result['remaining_languages'])}"
        )
    print(f"[{key_name}] Elapsed time: {format_elapsed_time(time.time() - key_start_time)}")
    time.sleep(API_DELAY_SEC)

    return {
        "processed": True,
        "remaining_languages": list(key_result["remaining_languages"]),
    }


def prepare_translation_round_resume_state(
    resume_state,
    current_round,
    pass_keys,
    fill_only_missing,
    next_index=0,
    round_processed_keys=None,
):
    progress = resume_state.setdefault("progress", {})
    progress["phase"] = RESUME_PHASE_TRANSLATION_ROUND
    progress["current_round"] = int(current_round)
    progress["next_index"] = max(0, int(next_index))
    progress["pass_keys"] = list(pass_keys or [])
    progress["pass_fill_only_missing"] = bool(fill_only_missing)
    progress["round_processed_keys"] = list(round_processed_keys or [])
    progress["current_item_key"] = ""
    save_resume_state(resume_state)


def process_translation_round_with_resume(resume_state, keys_list, stop_on_error=False):
    progress = resume_state.setdefault("progress", {})
    pass_keys = list(progress.get("pass_keys", []))
    next_index = max(0, int(progress.get("next_index", 0)))
    fill_only_missing = bool(progress.get("pass_fill_only_missing", False))
    round_processed_keys = list(progress.get("round_processed_keys", []))
    round_processed_keys_seen = set(round_processed_keys)
    found_specific_keys = set(progress.get("found_specific_keys", []))
    accumulated_unresolved_keys = sanitize_resume_unresolved_keys(
        progress.get("accumulated_unresolved_keys", {})
    )
    key_map = {item["key"]: item for item in keys_list}

    for item_index in range(next_index, len(pass_keys)):
        key_name = pass_keys[item_index]
        progress["next_index"] = item_index
        progress["current_item_key"] = key_name
        progress["round_processed_keys"] = list(round_processed_keys)
        progress["found_specific_keys"] = sorted(found_specific_keys)
        progress["accumulated_unresolved_keys"] = sanitize_resume_unresolved_keys(
            accumulated_unresolved_keys
        )
        save_resume_state(resume_state)

        item = key_map.get(key_name)
        if item is None:
            progress["next_index"] = item_index + 1
            progress["current_item_key"] = ""
            save_resume_state(resume_state)
            continue

        found_specific_keys.add(key_name)
        key_result = process_translation_key_item(
            key_name,
            item["langs"],
            fill_only_missing=fill_only_missing,
            stop_on_error=stop_on_error,
        )
        if key_result["processed"] and key_name not in round_processed_keys_seen:
            round_processed_keys.append(key_name)
            round_processed_keys_seen.add(key_name)
        if key_result["remaining_languages"]:
            accumulated_unresolved_keys[key_name] = list(key_result["remaining_languages"])

        progress["next_index"] = item_index + 1
        progress["current_item_key"] = ""
        progress["round_processed_keys"] = list(round_processed_keys)
        progress["found_specific_keys"] = sorted(found_specific_keys)
        progress["accumulated_unresolved_keys"] = sanitize_resume_unresolved_keys(
            accumulated_unresolved_keys
        )
        save_resume_state(resume_state)

    return {
        "round_processed_keys": list(round_processed_keys),
        "found_specific_keys": sorted(found_specific_keys),
        "accumulated_unresolved_keys": sanitize_resume_unresolved_keys(
            accumulated_unresolved_keys
        ),
    }


def run_legacy_resume_mapping_operation(resume_key, stop_on_error=False):
    normalized_resume_key = str(resume_key).strip()
    if not normalized_resume_key:
        raise ValueError("Legacy resume file does not contain a valid KEY name.")

    print(
        f"Info: Legacy resume file detected. Continuing from KEY [{normalized_resume_key}] with the previous mapping-resume behavior."
    )
    print(f'\nReading "{MAP_FILE_PATH}"...')
    keys_list = get_keys_from_map(MAP_FILE_PATH)
    print(f"Found a total of {len(keys_list)} KEYs.\n")

    first_pass_result = process_translation_pass(
        keys_list=keys_list,
        global_fill_only_missing=False,
        skip_mode=True,
        resume_key=normalized_resume_key,
        update_resume_point=True,
        stop_on_error=stop_on_error,
    )
    unresolved_keys = dict(first_pass_result.get("unresolved_keys", {}))
    if first_pass_result.get("skip_mode"):
        print(
            f"Warning: Resume KEY [{normalized_resume_key}] was not found in translations.map."
        )
        return 1 + sum(
            len(remaining_languages)
            for remaining_languages in unresolved_keys.values()
        )

    if unresolved_keys:
        print("\nOperations completed with unresolved translations.")
    else:
        print("\nAll operations completed successfully!")

    return sum(len(remaining_languages) for remaining_languages in unresolved_keys.values())


def collect_local_cleanup_updates(en_text, lang_dict):
    updates = {}
    if not isinstance(lang_dict, dict):
        return updates

    placeholder_pairs = build_protected_placeholders(en_text, lang_dict)

    for lang, text in lang_dict.items():
        if not isinstance(text, str):
            continue

        cleaned_text = cleanup_translated_text(en_text, text, placeholder_pairs, lang)
        if cleaned_text != text:
            updates[lang] = cleaned_text

    return updates


def build_prompt_lang_dict(en_text, lang_dict, placeholder_pairs):
    prompt_lang_dict = {}
    if not isinstance(lang_dict, dict):
        return prompt_lang_dict

    for lang, text in lang_dict.items():
        cleaned_text = cleanup_translated_text(en_text, text, placeholder_pairs, lang)
        if (
            has_escape_or_punctuation_leak_issue(en_text, text, placeholder_pairs)
            or cleaned_text != text
        ):
            # Force a full re-translation when an escape-related leak is detected.
            prompt_lang_dict[lang] = ""
            continue

        prompt_lang_dict[lang] = apply_protected_placeholders(
            cleaned_text, placeholder_pairs
        )

    return prompt_lang_dict

def normalize_backend_setting(value):
    if isinstance(value, bool):
        return API_BACKEND_LOCAL if value else API_BACKEND_CLOUD

    if isinstance(value, str):
        normalized_value = value.strip().lower()
        if normalized_value == API_BACKEND_ASK:
            return API_BACKEND_ASK
        if normalized_value in (API_BACKEND_LOCAL, "local api", "local_api"):
            return API_BACKEND_LOCAL
        if normalized_value in (API_BACKEND_CLOUD, "cloud api", "cloud_api"):
            return API_BACKEND_CLOUD

    raise ValueError('API_TYPE must be set to "ask", "local", or "cloud".')


def render_backend_selection(selected_index):
    option_parts = []
    for idx, (_, label) in enumerate(BACKEND_SELECTION_OPTIONS):
        if idx == selected_index:
            option_parts.append(f"[> {label} <]")
        else:
            option_parts.append(f"[  {label}  ]")

    prompt_text = (
        "Select translation backend with arrow keys or Tab, then press Enter: "
    )
    sys.stdout.write("\r" + prompt_text + "  ".join(option_parts) + "   ")
    sys.stdout.flush()


def build_initial_interactive_menu_options(cli_args=None):
    backend_value = normalize_backend_setting(API_TYPE)
    if backend_value == API_BACKEND_ASK:
        backend_value = API_BACKEND_LOCAL

    return {
        "backend": backend_value,
        "loop": bool(cli_args.loop) if cli_args and cli_args.loop is not None else True,
        "stop_on_error": bool(cli_args.stop_on_error)
        if cli_args and cli_args.stop_on_error is not None
        else False,
    }


def get_interactive_menu_option_value_label(option_key, option_values):
    for key_name, _, choices in INTERACTIVE_MENU_OPTION_ITEMS:
        if key_name != option_key:
            continue

        current_value = option_values.get(key_name, choices[0][0])
        for value, label in choices:
            if value == current_value:
                return label
        return choices[0][1]

    return ""


def cycle_interactive_menu_option_value(option_values, selected_option_index, direction):
    key_name, _, choices = INTERACTIVE_MENU_OPTION_ITEMS[selected_option_index]
    current_value = option_values.get(key_name, choices[0][0])
    current_index = 0
    for idx, (value, _) in enumerate(choices):
        if value == current_value:
            current_index = idx
            break

    next_index = (current_index + direction) % len(choices)
    option_values[key_name] = choices[next_index][0]


def clear_interactive_menu_screen():
    if not sys.stdout.isatty():
        return
    sys.stdout.write("\x1b[2J\x1b[H")
    sys.stdout.flush()


def render_interactive_operation_menu(selected_option_index, option_values, mode_input):
    clear_interactive_menu_screen()
    print("Options")
    for idx, (key_name, label, _) in enumerate(INTERACTIVE_MENU_OPTION_ITEMS):
        selector = ">" if idx == selected_option_index else " "
        value_label = get_interactive_menu_option_value_label(key_name, option_values)
        print(f" {selector} {label}: [{value_label}]")

    print("\nModes")
    for choice, label in MENU_OPTION_ITEMS:
        print(f"  {choice}. {label}")

    print("\nUse Up/Down to select an option, Tab to change it, type a mode number, then press Enter.")
    print(f"Mode number: {mode_input}")


def read_interactive_operation_menu_action_windows():
    import msvcrt

    while True:
        key = msvcrt.getwch()
        if key in ("\r", "\n"):
            return INTERACTIVE_MENU_ACTION_CONFIRM, ""
        if key == "\t":
            return INTERACTIVE_MENU_ACTION_VALUE_NEXT, ""
        if key == "\b":
            return INTERACTIVE_MENU_ACTION_BACKSPACE, ""
        if key == "\x03":
            raise KeyboardInterrupt
        if key.isdigit():
            return INTERACTIVE_MENU_ACTION_DIGIT, key
        if key in ("\x00", "\xe0"):
            special_key = msvcrt.getwch()
            if special_key == "H":
                return INTERACTIVE_MENU_ACTION_OPTION_PREVIOUS, ""
            if special_key == "P":
                return INTERACTIVE_MENU_ACTION_OPTION_NEXT, ""
            if special_key == "K":
                return INTERACTIVE_MENU_ACTION_VALUE_PREVIOUS, ""
            if special_key == "M":
                return INTERACTIVE_MENU_ACTION_VALUE_NEXT, ""
            if special_key == "S":
                return INTERACTIVE_MENU_ACTION_BACKSPACE, ""


def read_interactive_operation_menu_action_posix():
    import termios
    import tty

    stdin_handle = sys.stdin
    file_descriptor = stdin_handle.fileno()
    original_settings = termios.tcgetattr(file_descriptor)
    try:
        tty.setraw(file_descriptor)
        while True:
            key = stdin_handle.read(1)
            if key in ("\r", "\n"):
                return INTERACTIVE_MENU_ACTION_CONFIRM, ""
            if key == "\t":
                return INTERACTIVE_MENU_ACTION_VALUE_NEXT, ""
            if key in ("\x7f", "\b"):
                return INTERACTIVE_MENU_ACTION_BACKSPACE, ""
            if key == "\x03":
                raise KeyboardInterrupt
            if key.isdigit():
                return INTERACTIVE_MENU_ACTION_DIGIT, key
            if key == "\x1b":
                next_char = stdin_handle.read(1)
                if next_char != "[":
                    continue

                arrow_key = stdin_handle.read(1)
                if arrow_key == "A":
                    return INTERACTIVE_MENU_ACTION_OPTION_PREVIOUS, ""
                if arrow_key == "B":
                    return INTERACTIVE_MENU_ACTION_OPTION_NEXT, ""
                if arrow_key == "D":
                    return INTERACTIVE_MENU_ACTION_VALUE_PREVIOUS, ""
                if arrow_key == "C":
                    return INTERACTIVE_MENU_ACTION_VALUE_NEXT, ""
    finally:
        termios.tcsetattr(file_descriptor, termios.TCSADRAIN, original_settings)


def read_interactive_operation_menu_action():
    if os.name == "nt":
        return read_interactive_operation_menu_action_windows()
    return read_interactive_operation_menu_action_posix()


def prompt_for_interactive_operation_selection(option_values):
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print_operation_menu()
        return input("\nEnter your choice (1-18): ").strip(), option_values

    selected_option_index = 0
    mode_input = ""
    render_interactive_operation_menu(selected_option_index, option_values, mode_input)

    while True:
        action, payload = read_interactive_operation_menu_action()
        if action == INTERACTIVE_MENU_ACTION_CONFIRM:
            if mode_input:
                sys.stdout.write("\n")
                sys.stdout.flush()
                return mode_input, option_values
        elif action == INTERACTIVE_MENU_ACTION_OPTION_PREVIOUS:
            selected_option_index = (selected_option_index - 1) % len(
                INTERACTIVE_MENU_OPTION_ITEMS
            )
        elif action == INTERACTIVE_MENU_ACTION_OPTION_NEXT:
            selected_option_index = (selected_option_index + 1) % len(
                INTERACTIVE_MENU_OPTION_ITEMS
            )
        elif action == INTERACTIVE_MENU_ACTION_VALUE_PREVIOUS:
            cycle_interactive_menu_option_value(
                option_values, selected_option_index, -1
            )
        elif action == INTERACTIVE_MENU_ACTION_VALUE_NEXT:
            cycle_interactive_menu_option_value(
                option_values, selected_option_index, 1
            )
        elif action == INTERACTIVE_MENU_ACTION_BACKSPACE:
            mode_input = mode_input[:-1]
        elif action == INTERACTIVE_MENU_ACTION_DIGIT:
            if len(mode_input) < 2:
                mode_input += payload

        render_interactive_operation_menu(selected_option_index, option_values, mode_input)


def read_backend_selection_action_windows():
    import msvcrt

    while True:
        key = msvcrt.getwch()
        if key in ("\r", "\n"):
            return BACKEND_SELECTION_CONFIRM
        if key == "\t":
            return BACKEND_SELECTION_NEXT
        if key == "\x03":
            raise KeyboardInterrupt
        if key in ("\x00", "\xe0"):
            special_key = msvcrt.getwch()
            if special_key in ("H", "K"):
                return BACKEND_SELECTION_PREVIOUS
            if special_key in ("P", "M"):
                return BACKEND_SELECTION_NEXT


def read_backend_selection_action_posix():
    import termios
    import tty

    stdin_handle = sys.stdin
    file_descriptor = stdin_handle.fileno()
    original_settings = termios.tcgetattr(file_descriptor)
    try:
        tty.setraw(file_descriptor)
        while True:
            key = stdin_handle.read(1)
            if key in ("\r", "\n"):
                return BACKEND_SELECTION_CONFIRM
            if key == "\t":
                return BACKEND_SELECTION_NEXT
            if key == "\x03":
                raise KeyboardInterrupt
            if key == "\x1b":
                next_char = stdin_handle.read(1)
                if next_char != "[":
                    continue

                arrow_key = stdin_handle.read(1)
                if arrow_key in ("A", "D"):
                    return BACKEND_SELECTION_PREVIOUS
                if arrow_key in ("B", "C"):
                    return BACKEND_SELECTION_NEXT
    finally:
        termios.tcsetattr(file_descriptor, termios.TCSADRAIN, original_settings)


def read_backend_selection_action():
    if os.name == "nt":
        return read_backend_selection_action_windows()
    return read_backend_selection_action_posix()


def prompt_for_backend_selection():
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print(
            'Info: API_TYPE is set to "ask" but no interactive console is available. Defaulting to Local API.'
        )
        return API_BACKEND_LOCAL

    selected_index = 0
    print("Please choose the translation backend.")
    print("Use arrow keys or Tab to switch, then press Enter.")
    render_backend_selection(selected_index)

    while True:
        action = read_backend_selection_action()
        if action == BACKEND_SELECTION_CONFIRM:
            sys.stdout.write("\n")
            sys.stdout.flush()
            return BACKEND_SELECTION_OPTIONS[selected_index][0]
        if action == BACKEND_SELECTION_PREVIOUS:
            selected_index = (selected_index - 1) % len(BACKEND_SELECTION_OPTIONS)
        elif action == BACKEND_SELECTION_NEXT:
            selected_index = (selected_index + 1) % len(BACKEND_SELECTION_OPTIONS)

        render_backend_selection(selected_index)


def resolve_backend_selection(prompt_user=False):
    global API_TYPE

    backend_setting = normalize_backend_setting(API_TYPE)
    if backend_setting == API_BACKEND_ASK:
        if not prompt_user:
            raise RuntimeError(
                'API_TYPE is set to "ask" but the backend has not been selected yet.'
            )
        API_TYPE = prompt_for_backend_selection()
        backend_setting = normalize_backend_setting(API_TYPE)
    else:
        API_TYPE = backend_setting

    return backend_setting == API_BACKEND_LOCAL


def get_active_backend_label():
    return "Local API" if resolve_backend_selection() else "Cloud API"


def get_active_model_name():
    return LOCAL_MODEL_NAME if resolve_backend_selection() else CLOUD_MODEL_NAME


def get_active_retry_delay_sec():
    return (
        LOCAL_API_RETRY_DELAY_SEC
        if resolve_backend_selection()
        else CLOUD_API_RETRY_DELAY_SEC
    )


def format_cloud_retry_attempt(attempt_index):
    if CLOUD_API_MAX_RETRY_COUNT > 0:
        return f"{attempt_index}/{CLOUD_API_MAX_RETRY_COUNT}"
    return f"{attempt_index}/unbounded"


def has_cloud_retry_attempts_remaining(attempt_index):
    return CLOUD_API_MAX_RETRY_COUNT <= 0 or attempt_index < CLOUD_API_MAX_RETRY_COUNT


def should_retry_cloud_http_error(status_code):
    return status_code in {408, 429, 500, 502, 503, 504}


def call_cloud_api(api_key, model_name, prompt, plain_text=False):
    url = CLOUD_API_URL.format(model_name=model_name, api_key=api_key)
    headers = {"Content-Type": "application/json"}

    data = {
        "systemInstruction": {"parts": [{"text": SHARED_API_SYSTEM_PROMPT}]},
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {"temperature": 0.1, "response_mime_type": "text/plain"},
        "safetySettings": [
            {"category": "HARM_CATEGORY_HARASSMENT", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_HATE_SPEECH", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_SEXUALLY_EXPLICIT", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_DANGEROUS_CONTENT", "threshold": "BLOCK_NONE"},
        ],
    }

    req = urllib.request.Request(url, json.dumps(data).encode("utf-8"), headers)

    attempt_index = 0
    while True:
        attempt_index += 1
        attempt_label = format_cloud_retry_attempt(attempt_index)
        try:
            with urllib.request.urlopen(req, timeout=CLOUD_API_REQUEST_TIMEOUT_SEC) as response:
                response_data = response.read().decode("utf-8")
                resp_json = json.loads(response_data)
                candidates = resp_json.get("candidates", [])
                if not candidates:
                    feedback = resp_json.get("promptFeedback", {})
                    if feedback.get("blockReason") == "PROHIBITED_CONTENT":
                        print(
                            f"API Blocked due to PROHIBITED_CONTENT. Emulating empty response to skip."
                        )
                        return "{}" if not plain_text else ""
                    print(f"API Returned no candidates: {resp_json}")
                    return None

                parts = candidates[0].get("content", {}).get("parts", [])
                if not parts:
                    print(
                        f"API Blocked or Empty Content: finishReason={candidates[0].get('finishReason', 'UNKNOWN')} ({resp_json})"
                    )
                    return None
                content = parts[0].get("text", "")
                append_ai_log(
                    f"Cloud API | Model={model_name} | PlainText={plain_text}",
                    prompt=prompt,
                    response=content,
                )
                return content.strip() if plain_text else content
        except urllib.error.HTTPError as e:
            error_body = e.read().decode("utf-8", errors="replace")
            append_ai_log(
                f"Cloud API HTTP Error | Model={model_name} | Status={e.code} | Attempt={attempt_label}",
                prompt=prompt,
                response=error_body,
            )
            print(f"API HTTP Error: {e.code} - {error_body}")
            if not should_retry_cloud_http_error(e.code) or not has_cloud_retry_attempts_remaining(attempt_index):
                return None
            print(
                f"Waiting {CLOUD_API_RETRY_DELAY_SEC} seconds before retrying ({attempt_label})..."
            )
            time.sleep(CLOUD_API_RETRY_DELAY_SEC)
        except urllib.error.URLError as e:
            append_ai_log(
                f"Cloud API URL Error | Model={model_name} | Attempt={attempt_label}",
                prompt=prompt,
                response=str(e),
            )
            print(f"API URL Error: {e}")
            if not has_cloud_retry_attempts_remaining(attempt_index):
                return None
            print(
                f"Waiting {CLOUD_API_RETRY_DELAY_SEC} seconds before retrying ({attempt_label})..."
            )
            time.sleep(CLOUD_API_RETRY_DELAY_SEC)
        except TimeoutError:
            append_ai_log(
                f"Cloud API Timeout | Model={model_name} | Attempt={attempt_label}",
                prompt=prompt,
                response=f"timeout after {CLOUD_API_REQUEST_TIMEOUT_SEC} seconds",
            )
            print(f"API Call Error: timeout after {CLOUD_API_REQUEST_TIMEOUT_SEC} seconds")
            if not has_cloud_retry_attempts_remaining(attempt_index):
                return None
            print(
                f"Waiting {CLOUD_API_RETRY_DELAY_SEC} seconds before retrying ({attempt_label})..."
            )
            time.sleep(CLOUD_API_RETRY_DELAY_SEC)
        except Exception as e:
            append_ai_log(
                f"Cloud API Call Error | Model={model_name} | Attempt={attempt_label}",
                prompt=prompt,
                response=str(e),
            )
            print(f"API Call Error: {e}")
            if not has_cloud_retry_attempts_remaining(attempt_index):
                return None
            print(
                f"Waiting {CLOUD_API_RETRY_DELAY_SEC} seconds before retrying ({attempt_label})..."
            )
            time.sleep(CLOUD_API_RETRY_DELAY_SEC)


def call_local_api(api_base_url, api_key, model_name, prompt, plain_text=False):
    base_url = api_base_url.rstrip("/")
    url = f"{base_url}/chat/completions"
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"

    data = {
        "messages": [
            {"role": "system", "content": SHARED_API_SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.1,
        "max_tokens": LOCAL_API_MAX_COMPLETION_TOKENS,
        "stream": False,
    }
    if model_name:
        data["model"] = model_name

    req = urllib.request.Request(url, json.dumps(data).encode("utf-8"), headers)
    try:
        with urllib.request.urlopen(
            req, timeout=LOCAL_API_REQUEST_TIMEOUT_SEC
        ) as response:
            response_data = response.read().decode("utf-8")
            resp_json = json.loads(response_data)
            choices = resp_json.get("choices", [])
            if not choices:
                print(f"API Returned no choices: {resp_json}")
                return None

            message = choices[0].get("message", {})
            content = message.get("content", "")
            append_ai_log(
                f"Local API | Model={model_name or '(default)'} | PlainText={plain_text}",
                prompt=prompt,
                response=content
                if isinstance(content, str)
                else json.dumps(content, ensure_ascii=False),
            )
            if not isinstance(content, str) or not content.strip():
                finish_reason = choices[0].get("finish_reason", "UNKNOWN")
                print(
                    f"API Returned empty content: finishReason={finish_reason} ({resp_json})"
                )
                return None

            return content.strip() if plain_text else content
    except urllib.error.HTTPError as e:
        print(
            f"API HTTP Error: {e.code} - {e.read().decode('utf-8', errors='replace')}"
        )
        return None
    except urllib.error.URLError as e:
        print(f"API URL Error: {e}")
        return None
    except TimeoutError:
        print(f"API Call Error: timeout after {LOCAL_API_REQUEST_TIMEOUT_SEC} seconds")
        return None
    except Exception as e:
        print(f"API Call Error: {e}")
        return None


def call_active_api(prompt, plain_text=False):
    if resolve_backend_selection():
        return call_local_api(
            LOCAL_API_BASE_URL,
            LOCAL_API_KEY,
            LOCAL_MODEL_NAME,
            prompt,
            plain_text=plain_text,
        )
    return call_cloud_api(
        CLOUD_API_KEY, CLOUD_MODEL_NAME, prompt, plain_text=plain_text
    )


def sanitize_model_json_text(result_text):
    if not isinstance(result_text, str):
        return result_text

    sanitized = result_text.strip()
    start_idx = sanitized.find("{")
    end_idx = sanitized.rfind("}")
    if start_idx != -1 and end_idx != -1 and end_idx >= start_idx:
        sanitized = sanitized[start_idx : end_idx + 1]

    sanitized = re.sub(
        r'(\["\/bfnrtu])|(\)', lambda m: m.group(1) if m.group(1) else "\\", sanitized
    )
    sanitized = re.sub(r",\s*}", "}", sanitized)
    return sanitized


def salvage_json_object_entries(raw_text):
    if not isinstance(raw_text, str):
        return {}

    text = raw_text.strip()
    start_idx = text.find("{")
    end_idx = text.rfind("}")
    if start_idx != -1 and end_idx != -1 and end_idx >= start_idx:
        text = text[start_idx : end_idx + 1]

    results = {}
    length = len(text)
    index = 0

    def skip_ws(pos):
        while pos < length and text[pos].isspace():
            pos += 1
        return pos

    def read_json_string(pos):
        if pos >= length or text[pos] != '"':
            return None, pos

        pos += 1
        chars = []
        while pos < length:
            ch = text[pos]
            if ch == "\\":
                if pos + 1 >= length:
                    return None, pos
                chars.append(ch)
                chars.append(text[pos + 1])
                pos += 2
                continue
            if ch == '"':
                return "".join(chars), pos + 1
            chars.append(ch)
            pos += 1
        return None, pos

    while index < length:
        index = skip_ws(index)
        if index >= length:
            break
        if text[index] in "{,":
            index += 1
            continue
        if text[index] == "}":
            break
        if text[index] != '"':
            index += 1
            continue

        raw_key, next_index = read_json_string(index)
        if raw_key is None:
            index += 1
            continue

        index = skip_ws(next_index)
        if index >= length or text[index] != ":":
            continue

        index = skip_ws(index + 1)
        if index >= length or text[index] != '"':
            continue

        raw_value, next_index = read_json_string(index)
        if raw_value is None:
            index += 1
            continue

        try:
            decoded_key = json.loads('"' + raw_key + '"')
            decoded_value = json.loads('"' + raw_value + '"')
        except Exception:
            index = next_index
            continue

        results[decoded_key] = decoded_value
        index = next_index

    return results


def preprocess_line_response_text(result_text, expected_lang_codes=None):
    if not isinstance(result_text, str):
        return result_text

    expected_lang_codes = set(expected_lang_codes or [])
    normalized = result_text.replace("\r\n", "\n").replace("\r", "\n").strip()
    if not normalized:
        return normalized

    if "<channel|>" in normalized:
        normalized = normalized.rsplit("<channel|>", 1)[-1].strip()
    elif "<|channel>thought" in normalized:
        strict_lines = []
        for raw_line in normalized.splitlines():
            stripped = raw_line.strip()
            match = re.match(
                r"^([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)\s*(?:\t|\\t)\s*(.+)$", stripped
            )
            if not match:
                continue
            lang_code = match.group(1)
            if expected_lang_codes and lang_code not in expected_lang_codes:
                continue
            strict_lines.append(stripped)
        if strict_lines:
            normalized = "\n".join(strict_lines)

    return normalized.strip()


def parse_line_based_updates_response(
    result_text, en_text="", expected_lang_codes=None
):
    if not isinstance(result_text, str):
        raise ValueError("Could not parse model line response")

    expected_lang_codes = set(expected_lang_codes or [])
    result_text = preprocess_line_response_text(result_text, expected_lang_codes)

    def is_ignorable_line(raw_line):
        stripped = raw_line.strip()
        if not stripped:
            return True
        if stripped.startswith("```"):
            return True
        if stripped in ("{", "}", "},"):
            return True
        if stripped.startswith("Response format example"):
            return True
        if stripped.startswith("(Note:"):
            return True
        if stripped.startswith("LOCKED PLACEHOLDERS JSON"):
            return True
        if stripped.startswith("EXISTING TRANSLATIONS JSON"):
            return True
        return False

    def cleanup_parsed_translation(value):
        if not isinstance(value, str):
            return ""
        cleaned = value.strip().rstrip(",")
        if len(cleaned) >= 2 and cleaned[0] == cleaned[-1] and cleaned[0] in ('"', "'"):
            cleaned = cleaned[1:-1].strip()
        cleaned = cleaned.replace("\\t", "	").replace('\\"', '"')
        cleaned = normalize_placeholder_quote_style(en_text, cleaned)
        return cleaned.strip()

    def looks_like_nested_language_line(value):
        if not isinstance(value, str):
            return False
        stripped_value = value.lstrip()
        nested_match = re.match(
            r"^([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)\s*(?:\t|\\t)\s*", stripped_value
        )
        if not nested_match:
            return False
        nested_lang = nested_match.group(1)
        return not expected_lang_codes or nested_lang in expected_lang_codes

    def parse_lang_translation_line(raw_line):
        stripped = raw_line.strip()
        direct_match = re.match(
            r"^([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)\s*(?:\t|\\t)\s*(.+)$", stripped
        )
        if direct_match:
            candidate = cleanup_parsed_translation(direct_match.group(2))
            if candidate and not looks_like_nested_language_line(candidate):
                return direct_match.group(1), candidate
            return None, None

        json_line_match = re.match(
            r'^"?([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)"?\s*:\s*(.+?)\s*,?$', stripped
        )
        if json_line_match:
            candidate = cleanup_parsed_translation(json_line_match.group(2))
            if candidate and not looks_like_nested_language_line(candidate):
                return json_line_match.group(1), candidate
            return None, None

        loose_match = re.match(
            r"^([A-Za-z]{2,3}(?:-[A-Za-z0-9]+)*)[\"']?\s+(.+)$", stripped
        )
        if loose_match:
            candidate = cleanup_parsed_translation(loose_match.group(2))
            if (
                candidate
                and not looks_like_source_echo_line(candidate, en_text)
                and not looks_like_nested_language_line(candidate)
            ):
                return loose_match.group(1), candidate
        return None, None

    raw_lines = result_text.splitlines()
    updates = {}
    idx = 0
    while idx < len(raw_lines):
        raw_line = raw_lines[idx]
        stripped_line = raw_line.strip()
        idx += 1

        if is_ignorable_line(raw_line):
            continue

        lang_code, translation = parse_lang_translation_line(raw_line)
        if not lang_code and re.fullmatch(LANGUAGE_CODE_PATTERN, stripped_line):
            lookahead_idx = idx
            while lookahead_idx < len(raw_lines):
                lookahead_line = raw_lines[lookahead_idx]
                if is_ignorable_line(lookahead_line):
                    lookahead_idx += 1
                    continue
                if looks_like_source_echo_line(lookahead_line, en_text):
                    lookahead_idx += 1
                    continue
                next_lang_code, next_translation = parse_lang_translation_line(
                    lookahead_line
                )
                if next_lang_code:
                    break
                continuation = cleanup_parsed_translation(lookahead_line)
                if continuation and not looks_like_nested_language_line(continuation):
                    lang_code = stripped_line
                    translation = continuation
                    idx = lookahead_idx + 1
                break

        if not lang_code:
            continue

        lang_code = lang_code.strip()
        translation = cleanup_parsed_translation(translation)
        if expected_lang_codes and lang_code not in expected_lang_codes:
            continue
        if not re.fullmatch(LANGUAGE_CODE_PATTERN, lang_code):
            continue
        if not translation:
            continue
        if looks_like_source_echo_line(translation, en_text):
            continue
        if translation.startswith("{") and translation.endswith("}"):
            continue
        if looks_like_nested_language_line(translation):
            continue
        updates[lang_code] = translation

    if updates:
        return updates

    raise ValueError("Could not parse model line response")


def build_translation_batch_prompt(
    key_name,
    en_text,
    prompt_en_text_block,
    prompt_lang_dict,
    protected_placeholders_json,
    ok_translation_guidance,
    extra_requirements,
    authoritative_phrase_map_requirements,
    language_specific_requirements,
    response_mode="lines",
):
    response_rules = (
        "8. RETURN ONLY the languages you have updated, fixed, or newly added as plain text lines. Use EXACTLY one language per line in this format: `language_code<TAB>translation`. The separator must be a real TAB character, not the two characters `\t`.\n"
        "9. Do NOT return JSON, Markdown, explanations, numbering, bullet points, comments, example blocks, or note lines. Return pure plain text lines only.\n"
        "10. If no languages need updating, return an empty string.\n"
    )
    response_examples = (
        "Response format example:\naf\tTranslated text here\nfr\tTranslated text here\n"
    )
    placeholder_quote_rule = build_placeholder_format_rule(en_text, rule_number="15")
    literal_percent_rule = build_literal_percent_format_rule(en_text, rule_number="17")
    batch_focus_requirements = build_batch_focus_requirements(en_text, prompt_lang_dict)

    known_phrase_requirements = build_known_phrase_translation_requirements(
        en_text, prompt_lang_dict
    )

    single_language_rule = ""
    if len(prompt_lang_dict) == 1:
        only_lang_code = next(iter(prompt_lang_dict.keys()))
        only_lang_name = get_language_name_for_code(only_lang_code)
        only_lang_label = only_lang_name if only_lang_name else only_lang_code
        single_language_rule = (
            f"16. SINGLE-LANGUAGE RULE: This batch contains exactly one target language: `{only_lang_code}`. "
            f"The target language is `{only_lang_label}`. Translate into `{only_lang_label}`, not into English and not into a neighboring or more common language. "
            f"Return exactly one non-empty line starting with `{only_lang_code}` followed by a real TAB character. "
            "Do NOT return any other language codes, examples, notes, or extra lines.\n"
        )

    local_model_rules = ""
    if resolve_backend_selection():
        local_model_rules = (
            "18. LOCAL MODEL OUTPUT RULES: Never reveal internal reasoning. Never output self-corrections, analysis, checklists, bullets, or commentary. "
            "Never output internal channel tags such as <|channel>thought or <channel|>. "
            "Never output LaTeX, arrows like -> or $\\rightarrow$, or mixed-script garbage. "
            "Never output HTML fragments. Return only final translation lines in the exact requested format.\n"
        ) + build_local_single_language_guidance(en_text, prompt_lang_dict)

    return f"""
You are a professional translator and translation quality controller for the eMule software.
Below is the original English (en) text for the KEY named '{key_name}':
{prompt_en_text_block}

You are also given the existing translations in other languages as a JSON dictionary (If a translation is missing, it might be empty or a copy of the English original).
Your task is to check and, if necessary, correct or translate each language in this JSON dictionary according to the following rules:
Rules:
1. If the existing translation for a target language is completely missing (empty or exactly the same as the English original), translate it correctly and completely into the target language.
2. If the existing translation has errors, grammatical mistakes, or English leftovers, fix them.
3. The translations should be close in length to the original English line. In particular, consider translations shorter than 2/3 of the English line as faulty and translate them completely!
4. CRITICAL RULE FOR FEATURES AND ACRONYMS: Translate ALL feature names, descriptive phrases, sentences, and UI text elements IN FULL, even if they are enclosed in double quotes (like "Find As You Type" or "Adjust NTFS daylight..."). Do NOT leave English phrases untranslated in other languages. Keep strict system names, brand names, product names, and all-uppercase acronyms from the source in English. However, common translatable abbreviations (like mt for meter, sec for second) MUST be translated to the target language's equivalent.
5. You MUST strictly preserve all placeholders (%s, %i, %u, %lu, etc.), backslash escape sequences (\\n, \\r, \\\\), and literal percent formatting so they match the ORIGINAL ENGLISH TEXT exactly. If English uses a single literal `%`, keep a single `%`. If English uses `%%`, keep `%%`.
6. If the existing translation is already correct, skip it (do not include it in the output).
7. If the language code is "en", NEVER modify it.
{response_rules}11. LOCKED TERM RULE: If you see placeholders like __LOCKED_TERM_0__, these represent protected special terms and acronyms. Never translate, never transliterate, never inflect, never remove, and never alter these placeholders. Keep them exactly unchanged in output strings.
12. NEVER keep a partial English lead-in at the beginning of any translated paragraph or line. If the source segment starts with English words like "Do", "Do you", "Please", or "Use", translate them fully instead of leaving them in English.
13. IMPORTANT: Do NOT keep the English original in parentheses after you translate a quoted UI label, mode name, menu item, or option name. Translate it once naturally into the target language and omit the English copy.
14. IMPORTANT: If the source string contains escaped line breaks such as \\n, \\r\\n, or \\r, the next word begins a new translated line or paragraph. For example, `Found.\\n\\nDo you want...` means the `Do you want...` sentence must also be translated fully.
15. IMPORTANT: Locked placeholders protect only the special term itself, not the punctuation around it. If English has a pattern like `MaxMind.\\n\\nCopy...`, the period ends the sentence, but it does NOT have to stay immediately after the protected term in the target language if the sentence structure changes.
{placeholder_quote_rule}{literal_percent_rule}{single_language_rule}{batch_focus_requirements}{ok_translation_guidance}{known_phrase_requirements}{authoritative_phrase_map_requirements}{extra_requirements}{language_specific_requirements}{local_model_rules}{response_examples}
LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}

EXISTING TRANSLATIONS JSON:
{json.dumps(prompt_lang_dict, ensure_ascii=False, indent=2)}
"""


def build_translation_failure_signature(candidate_text, error_message):
    signature_source = ""
    if isinstance(candidate_text, str) and candidate_text.strip():
        signature_source = candidate_text
    elif isinstance(error_message, str):
        signature_source = error_message

    signature_source = signature_source.strip().lower()
    signature_source = re.sub(r"\s+", " ", signature_source)
    signature_source = re.sub(r"[\"'`]+", "", signature_source)
    return signature_source[:240]


def get_translation_completion_limits(batch_size, en_text="", lang_count=0):
    if resolve_backend_selection():
        return get_local_effective_batch_size(batch_size, en_text, lang_count)
    return min(batch_size, CLOUD_API_MAX_LANGUAGES_PER_BATCH)


def check_and_translate_with_cloud(key_name, en_text, lang_dict, error_context=""):
    global \
        LAST_BATCH_LANGUAGE_ERRORS, \
        LAST_BATCH_LANGUAGE_CANDIDATES, \
        LAST_BATCH_RESULT_STATUS
    LAST_BATCH_LANGUAGE_ERRORS = {}
    LAST_BATCH_LANGUAGE_CANDIDATES = {}
    LAST_BATCH_RESULT_STATUS = ""
    if is_invariant_translation_source(en_text):
        invariant_updates = {}
        for lang_code, current_text in lang_dict.items():
            LAST_BATCH_LANGUAGE_CANDIDATES[lang_code] = en_text
            if current_text != en_text:
                invariant_updates[lang_code] = en_text
        LAST_BATCH_RESULT_STATUS = "success"
        return invariant_updates

    protected_placeholders = build_protected_placeholders(en_text, lang_dict)
    prompt_en_text = apply_protected_placeholders(en_text, protected_placeholders)
    prompt_en_text_block = build_prompt_text_block(
        "ORIGINAL ENGLISH TEXT", en_text, prompt_en_text
    )
    prompt_lang_dict = build_prompt_lang_dict(
        en_text, lang_dict, protected_placeholders
    )
    protected_placeholders_json = get_protected_placeholders_prompt_block(
        protected_placeholders
    )
    ok_translation_guidance = ""
    if re.search(r"(?<![A-Za-z0-9_])OK(?![A-Za-z0-9_])", en_text):
        ok_translation_guidance = "15. IMPORTANT: The English token `OK` is a normal UI/status word, not a locked brand or technical acronym. Translate it when the target language normally uses a translated form, and if you translate it, use natural target-language capitalization instead of unnecessary full uppercase.\n"

    existing_translation_snapshot = "\n".join(
        text for text in lang_dict.values() if isinstance(text, str) and text
    )
    extra_requirements = build_embedded_phrase_fix_requirements(
        en_text, existing_translation_snapshot
    )
    if isinstance(error_context, str) and error_context.strip():
        extra_requirements += f"16. IMPORTANT: The previous attempt for this request failed validation or map-tool checks. Fix this specific problem and do not repeat it: {error_context.strip()}\n"
    authoritative_phrase_map_requirements = (
        build_authoritative_ui_phrase_map_requirements(en_text, prompt_lang_dict)
    )
    language_specific_requirements = "".join(
        build_language_specific_requirements(code)
        for code in sorted(prompt_lang_dict.keys())
        if code != "en"
    )

    def normalize_updates(updates):
        en_trailing_match = re.search(r"(\\n|\n|\s)+$", en_text)
        en_trailing = en_trailing_match.group(0) if en_trailing_match else ""
        cleaned_updates = {}
        if isinstance(updates, dict):
            for lang, t_text in updates.items():
                if not isinstance(t_text, str):
                    continue

                restored_text = restore_protected_placeholders(
                    t_text, protected_placeholders
                )
                if (
                    restored_text == en_text
                    and not is_invariant_translation_source(en_text)
                ):
                    continue
                cleaned_restored_text = cleanup_translated_text(
                    en_text, restored_text, protected_placeholders, lang
                )
                LAST_BATCH_LANGUAGE_CANDIDATES[lang] = cleaned_restored_text
                is_valid, validation_error = validate_translation_text(
                    en_text, cleaned_restored_text, protected_placeholders, lang
                )
                if not is_valid:
                    repaired_success, repaired_text, repaired_error = (
                        repair_candidate_text_for_validation(
                            lang,
                            cleaned_restored_text,
                            en_text,
                            validation_error=validation_error,
                        )
                    )
                    if repaired_success:
                        LAST_BATCH_LANGUAGE_CANDIDATES[lang] = repaired_text
                        clean_t = re.sub(r"(\\n|\n|\s)+$", "", repaired_text)
                        cleaned_updates[lang] = clean_t + en_trailing
                        print(
                            f"[{key_name}] Info: [{lang}] Candidate auto-repaired after validation failure."
                        )
                        continue

                    LAST_BATCH_LANGUAGE_ERRORS[lang] = repaired_error or validation_error
                    print(
                        f"[{key_name}] Warning: [{lang}] {LAST_BATCH_LANGUAGE_ERRORS[lang]}. Skipping this language update."
                    )
                    continue

                clean_t = re.sub(r"(\\n|\n|\s)+$", "", cleaned_restored_text)
                cleaned_updates[lang] = clean_t + en_trailing
        return cleaned_updates

    line_prompt = build_translation_batch_prompt(
        key_name,
        en_text,
        prompt_en_text_block,
        prompt_lang_dict,
        protected_placeholders_json,
        ok_translation_guidance,
        extra_requirements,
        authoritative_phrase_map_requirements,
        language_specific_requirements,
        response_mode="lines",
    )

    result_text = call_active_api(line_prompt, plain_text=True)
    if not result_text:
        LAST_BATCH_RESULT_STATUS = "call_failure"
        print(
            f"[{key_name}] Warning: Empty line-based response from {get_active_backend_label()}."
        )
        return None

    try:
        updates = parse_line_based_updates_response(
            result_text.strip(), en_text, set(lang_dict.keys())
        )
        normalized_updates = normalize_updates(updates)
        if normalized_updates:
            LAST_BATCH_RESULT_STATUS = "success"
            return normalized_updates

        if LAST_BATCH_LANGUAGE_ERRORS or LAST_BATCH_LANGUAGE_CANDIDATES:
            LAST_BATCH_RESULT_STATUS = "validation_failure"
        else:
            LAST_BATCH_RESULT_STATUS = "empty_after_parse"
        print(
            f"[{key_name}] Warning: Parsed line-based response but no valid translations remained after validation."
        )
        return None
    except Exception as parse_error:
        LAST_BATCH_RESULT_STATUS = "parse_failure"
        print(f"[{key_name}] Warning: Line parse failed ({parse_error}).")
        print(f"Broken Line Response:\n{result_text}")
        return None


def fix_translation_with_cloud(
    key_name, lang_code, faulty_text, error_message, en_text, extra_requirements=""
):
    initial_placeholders = build_protected_placeholders(
        en_text, {lang_code: faulty_text}
    )
    sanitized_faulty_text = cleanup_translated_text(
        en_text, faulty_text, initial_placeholders, lang_code
    )
    protected_placeholders = build_protected_placeholders(
        en_text, {lang_code: sanitized_faulty_text}
    )
    prompt_en_text = apply_protected_placeholders(en_text, protected_placeholders)
    prompt_faulty_text = apply_protected_placeholders(
        sanitized_faulty_text, protected_placeholders
    )
    prompt_en_text_block = build_prompt_text_block(
        "Original English Text", en_text, prompt_en_text
    )
    prompt_faulty_text_block = build_prompt_text_block(
        "Faulty Translation", sanitized_faulty_text, prompt_faulty_text
    )
    protected_placeholders_json = get_protected_placeholders_prompt_block(
        protected_placeholders
    )
    ok_translation_guidance = ""
    if re.search(r"(?<![A-Za-z0-9_])OK(?![A-Za-z0-9_])", en_text):
        ok_translation_guidance = "9. The English token `OK` is translatable when the target language normally uses a translated UI/status form. If you translate it, use natural capitalization instead of unnecessary full uppercase.\n"

    language_specific_requirements = build_language_specific_requirements(lang_code)
    known_phrase_requirements = build_known_phrase_translation_requirements(
        en_text, {lang_code: sanitized_faulty_text}
    )
    local_fix_guidance = (
        build_local_single_language_guidance(
            en_text, {lang_code: sanitized_faulty_text}
        )
        if resolve_backend_selection()
        else ""
    )

    prompt = f"""
You are a professional translator and translation quality controller for the eMule software.
We tried to add your translation but the translations.map update layer rejected it.

KEY: '{key_name}'
Language: '{lang_code}'
{prompt_en_text_block}

{prompt_faulty_text_block}
Compiler Error:
"{error_message}"

Fix the translation so it doesn't cause this error.
Rules:
1. Preserve all placeholders (%s, %i, %u, %lu, etc.) EXACTLY as they are in the English text.
2. Preserve all backslash escape sequences (\n, \r, \\) and literal percent formatting exactly as the English source uses them. If English uses a single literal `%`, keep a single `%`. If English uses `%%`, keep `%%`.
3. If there are placeholders like __LOCKED_TERM_0__, they are protected special terms/acronyms. Keep them exactly unchanged.
4. Never keep a partial English lead-in at the beginning of any translated paragraph or line. If the source starts with phrases like "Do", "Do you", "Please", or "Use", translate them fully instead of leaving them in English.
5. If the source string contains escaped line breaks such as \n, \r\n, or \r, treat them as real line or paragraph boundaries and translate the first word after each boundary too.
6. Punctuation around a locked placeholder is NOT locked. If a protected term is followed by sentence-ending punctuation in English, move that punctuation to the natural sentence-ending position in the target language when needed.
7. RETURN ONLY the corrected translation as a single plain text line in this format: `corrected_translation<TAB>your_fixed_translation_string_here`. The separator must be a real TAB character, not the two characters `\t`.
8. Do NOT return JSON, Markdown, explanations, numbering, bullet points, or comments. Return pure plain text only.
9. Use a neutral UI status-message tone. Do NOT use first-person wording equivalent to 'I renamed' or 'we changed'.
10. Do NOT leave raw English words like filename, source, sources, majority, or renamed in the corrected translation unless they are protected terms.
11. If you translate a quoted UI label or mode name, do NOT append the original English label in parentheses or as a gloss after the translated label.
{ok_translation_guidance}{known_phrase_requirements}{extra_requirements}{language_specific_requirements}{local_fix_guidance}{build_placeholder_format_rule(en_text, rule_number="11")}{build_literal_percent_format_rule(en_text, rule_number="12")}
LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}
"""
    result_text = call_active_api(prompt, plain_text=True)
    if not result_text:
        return None

    try:
        corrected = ""
        try:
            parsed = parse_line_based_updates_response(
                result_text.strip(), en_text, {"corrected_translation"}
            )
            corrected = parsed.get("corrected_translation", "")
        except Exception:
            corrected = extract_single_translation_candidate_from_plain_text(
                result_text, en_text
            )
        if not isinstance(corrected, str) or not corrected.strip():
            return None

        en_trailing_match = re.search(r"(\\n|\n|\s)+$", en_text)
        en_trailing = en_trailing_match.group(0) if en_trailing_match else ""

        corrected_restored = restore_protected_placeholders(
            corrected, protected_placeholders
        )
        corrected_cleaned = cleanup_translated_text(
            en_text, corrected_restored, protected_placeholders, lang_code
        )
        clean_t = re.sub(r"(\\n|\n|\s)+$", "", corrected_cleaned)
        return clean_t + en_trailing
    except Exception as e:
        print(f"  -> [{lang_code}] Line Parse Error during fix: {e}")
        print(f"  -> Broken Line Response:\n{result_text}")
        return None


def get_translation_line_context(target_line_num):
    if not os.path.exists(MAP_FILE_PATH):
        return None, f"{MAP_FILE_PATH} not found."

    with open(MAP_FILE_PATH, "r", encoding="utf-8", errors="ignore") as map_file:
        all_lines = map_file.readlines()

    total_file_lines = len(all_lines)
    if target_line_num < 1 or target_line_num > total_file_lines:
        return None, f"[Line {target_line_num}] Out of range, skipping."

    target_line = all_lines[target_line_num - 1]
    target_line_stripped = target_line.rstrip("\n\r")
    if not target_line_stripped or target_line_stripped[0] not in ("\t", " "):
        return (
            None,
            f"[Line {target_line_num}] Not a language line (empty or KEY definition), skipping.",
        )

    line_content = target_line_stripped.lstrip()
    parts = line_content.split("\t", 1)
    if len(parts) < 2:
        parts = line_content.split(" ", 1)
    if len(parts) < 2 or not parts[0].strip():
        return None, f"[Line {target_line_num}] Invalid format, skipping."

    lang_code = parts[0].strip()
    current_text = parts[1] if len(parts) > 1 else ""

    key_name = None
    en_text = None
    for search_line_idx in range(target_line_num - 2, -1, -1):
        search_line = all_lines[search_line_idx].rstrip("\n\r")
        if not search_line:
            continue
        if search_line[0] not in ("\t", " "):
            key_name = search_line.strip()
            break

    if not key_name:
        return None, f"[Line {target_line_num}] Could not find KEY, skipping."

    for search_line_idx in range(target_line_num - 2, -1, -1):
        search_line = all_lines[search_line_idx].rstrip("\n\r")
        if not search_line:
            continue
        search_stripped = search_line.lstrip()
        if search_line[0] in ("\t", " ") and search_stripped.startswith("en\t"):
            en_parts = search_stripped.split("\t", 1)
            if len(en_parts) > 1:
                en_text = en_parts[1].strip()
            break

    if not en_text:
        return None, f"[Line {target_line_num}] Could not find English source text."

    return {
        "line_number": target_line_num,
        "total_lines": total_file_lines,
        "key_name": key_name,
        "lang_code": lang_code,
        "current_text": current_text,
        "en_text": en_text,
    }, ""


def get_map_total_line_count():
    if not os.path.exists(MAP_FILE_PATH):
        return 0

    with open(MAP_FILE_PATH, "r", encoding="utf-8", errors="ignore") as map_file:
        return sum(1 for _ in map_file)


def clean_then_translate_line_numbers_logic(
    raw_input_text=None,
    stop_on_error=False,
    line_numbers=None,
    resume_state=None,
    runtime_settings=None,
):
    print("\nClean then Translate Specific Line Number(s)")

    if resume_state is None:
        if line_numbers is None:
            if raw_input_text is None:
                raw_input_text = read_cli_or_prompt_value(
                    None,
                    "Enter line numbers (comma-separated, extra spaces allowed): ",
                    "Line numbers must be provided for operation 4 when stdin is not interactive.",
                )
            raw_input_text = raw_input_text.strip()
            if not raw_input_text:
                print("No input provided. Exiting.")
                handle_operation_error("No line numbers were provided.", stop_on_error)
                return 1
            line_numbers = parse_line_numbers_input(raw_input_text)
        if not line_numbers:
            print("No valid line numbers provided. Exiting.")
            handle_operation_error(
                "No valid line numbers were provided.", stop_on_error
            )
            return 1

        resume_state = build_resume_state(
            "4",
            {
                "line_numbers": list(line_numbers),
                "stop_on_error": bool(stop_on_error),
            },
            runtime_settings=runtime_settings,
            progress={
                "phase": RESUME_PHASE_LINE_NUMBERS,
                "next_index": 0,
                "processed_count": 0,
                "skipped_count": 0,
                "error_count": 0,
                "current_item_line_number": 0,
            },
        )
        save_resume_state(resume_state)
    else:
        line_numbers = [int(value) for value in resume_state.get("params", {}).get("line_numbers", [])]
        stop_on_error = bool(
            resume_state.get("params", {}).get("stop_on_error", stop_on_error)
        )
        print(
            f"Info: Resuming saved line-number translation job from item {int(resume_state.get('progress', {}).get('next_index', 0)) + 1}/{len(line_numbers)}."
        )
    if not line_numbers:
        print("No valid line numbers provided. Exiting.")
        handle_operation_error("No valid line numbers were provided.", stop_on_error)
        return 1

    progress = resume_state.setdefault("progress", {})
    next_index = max(0, int(progress.get("next_index", 0)))
    processed_count = int(progress.get("processed_count", 0))
    skipped_count = int(progress.get("skipped_count", 0))
    error_count = int(progress.get("error_count", 0))

    current_total_lines = get_map_total_line_count()
    print(f"\nTotal lines in translations.map: {current_total_lines}")
    print(f"Processing {len(line_numbers)} line(s)...")

    for item_index in range(next_index, len(line_numbers)):
        target_line_num = line_numbers[item_index]
        progress["next_index"] = item_index
        progress["current_item_line_number"] = target_line_num
        progress["processed_count"] = processed_count
        progress["skipped_count"] = skipped_count
        progress["error_count"] = error_count
        save_resume_state(resume_state)

        context, message = get_translation_line_context(target_line_num)
        if context is None:
            print(message)
            if "skipping." in message:
                skipped_count += 1
            else:
                error_count += 1
                handle_operation_error(message, stop_on_error)
            progress["next_index"] = item_index + 1
            progress["current_item_line_number"] = 0
            progress["processed_count"] = processed_count
            progress["skipped_count"] = skipped_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        key_name = context["key_name"]
        lang_code = context["lang_code"]
        en_text = context["en_text"]
        current_text = context["current_text"]
        print(f"\n[Line {target_line_num}] KEY={key_name}, LANG={lang_code}")
        print(
            f"  Current text: {current_text[:80]}{'...' if len(current_text) > 80 else ''}"
        )
        print(f"  Source (en): {en_text[:80]}{'...' if len(en_text) > 80 else ''}")

        success, msg = update_translation_via_compiler(key_name, lang_code, "")
        if not success:
            print(f"  Clear failed: {msg}")
            error_count += 1
            handle_operation_error(
                f"[Line {target_line_num}] Failed to clear the current translation: {msg}",
                stop_on_error,
            )
            progress["next_index"] = item_index + 1
            progress["current_item_line_number"] = 0
            progress["processed_count"] = processed_count
            progress["skipped_count"] = skipped_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        print("  Cleared existing translation.")
        translated = translate_single_line(key_name, lang_code, en_text)
        if not isinstance(translated, str) or not translated.strip():
            print("  Translation failed or empty.")
            error_count += 1
            handle_operation_error(
                f"[Line {target_line_num}] Translation failed or returned empty text.",
                stop_on_error,
            )
            progress["next_index"] = item_index + 1
            progress["current_item_line_number"] = 0
            progress["processed_count"] = processed_count
            progress["skipped_count"] = skipped_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        success, msg = update_translation_via_compiler(
            key_name, lang_code, translated, en_text
        )
        if not success:
            print(f"  Update failed: {msg}")
            error_count += 1
            handle_operation_error(
                f"[Line {target_line_num}] Failed to update the translated text: {msg}",
                stop_on_error,
            )
        else:
            processed_count += 1
            print(
                f"  Updated with translation: {translated[:80]}{'...' if len(translated) > 80 else ''}"
            )

        progress["next_index"] = item_index + 1
        progress["current_item_line_number"] = 0
        progress["processed_count"] = processed_count
        progress["skipped_count"] = skipped_count
        progress["error_count"] = error_count
        save_resume_state(resume_state)

    print("\nOperation completed!")
    print(f"Processed: {processed_count}, Skipped: {skipped_count}, Errors: {error_count}")
    return error_count


def clean_then_translate_specific_key_languages_logic(
    raw_input_text=None,
    stop_on_error=False,
    key_language_pairs=None,
    resume_state=None,
    runtime_settings=None,
):
    print("\nClean then Translate Specific Translation Key(s) And Language Code(s)")

    if resume_state is None:
        if key_language_pairs is None:
            if raw_input_text is None:
                raw_input_text = read_cli_or_prompt_value(
                    None,
                    "Enter alternating KEY and language code values (comma-separated, extra spaces allowed): ",
                    "KEY/language pairs must be provided for operation 5 when stdin is not interactive.",
                )
            raw_input_text = raw_input_text.strip()
            if not raw_input_text:
                print("No input provided. Exiting.")
                handle_operation_error(
                    "No KEY/language pairs were provided.", stop_on_error
                )
                return 1
            key_language_pairs, parse_error = parse_specific_key_language_pairs_input(
                raw_input_text
            )
            if parse_error:
                print(f"ERROR: {parse_error}")
                handle_operation_error(parse_error, stop_on_error)
                return 1

        resume_state = build_resume_state(
            "5",
            {
                "key_language_pairs": serialize_key_language_pairs(key_language_pairs),
                "stop_on_error": bool(stop_on_error),
            },
            runtime_settings=runtime_settings,
            progress={
                "phase": RESUME_PHASE_KEY_LANGUAGE_PAIRS,
                "next_index": 0,
                "processed_count": 0,
                "error_count": 0,
                "current_item_key": "",
                "current_item_lang_code": "",
            },
        )
        save_resume_state(resume_state)
    else:
        key_language_pairs = deserialize_key_language_pairs(
            resume_state.get("params", {}).get("key_language_pairs", [])
        )
        stop_on_error = bool(
            resume_state.get("params", {}).get("stop_on_error", stop_on_error)
        )
        print(
            f"Info: Resuming saved KEY/language translation job from item {int(resume_state.get('progress', {}).get('next_index', 0)) + 1}/{len(key_language_pairs)}."
        )

    if not os.path.exists(MAP_FILE_PATH):
        print(f"ERROR: {MAP_FILE_PATH} not found!")
        handle_operation_error(f"{MAP_FILE_PATH} not found.", stop_on_error)
        return 1

    progress = resume_state.setdefault("progress", {})
    next_index = max(0, int(progress.get("next_index", 0)))
    processed_count = int(progress.get("processed_count", 0))
    error_count = int(progress.get("error_count", 0))

    for item_index in range(next_index, len(key_language_pairs)):
        key_name, lang_code = key_language_pairs[item_index]
        progress["next_index"] = item_index
        progress["current_item_key"] = key_name
        progress["current_item_lang_code"] = lang_code
        progress["processed_count"] = processed_count
        progress["error_count"] = error_count
        save_resume_state(resume_state)

        keys_by_name = {item["key"]: item for item in get_keys_from_map(MAP_FILE_PATH)}
        key_data = keys_by_name.get(key_name)
        if key_data is None:
            print(f"[{key_name}/{lang_code}] KEY not found in translations.map.")
            error_count += 1
            handle_operation_error(
                f"[{key_name}/{lang_code}] KEY not found in translations.map.",
                stop_on_error,
            )
            progress["next_index"] = item_index + 1
            progress["current_item_key"] = ""
            progress["current_item_lang_code"] = ""
            progress["processed_count"] = processed_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        en_text = key_data.get("langs", {}).get("en", "")
        if not isinstance(en_text, str) or not en_text.strip():
            print(f"[{key_name}/{lang_code}] English source text is missing.")
            error_count += 1
            handle_operation_error(
                f"[{key_name}/{lang_code}] English source text is missing.",
                stop_on_error,
            )
            progress["next_index"] = item_index + 1
            progress["current_item_key"] = ""
            progress["current_item_lang_code"] = ""
            progress["processed_count"] = processed_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        print(f"\n[{key_name}/{lang_code}] Clearing the current translation...")
        success, msg = update_translation_via_compiler(key_name, lang_code, "")
        if not success:
            print(f"[{key_name}/{lang_code}] Clear failed: {msg}")
            error_count += 1
            handle_operation_error(
                f"[{key_name}/{lang_code}] Failed to clear the current translation: {msg}",
                stop_on_error,
            )
            progress["next_index"] = item_index + 1
            progress["current_item_key"] = ""
            progress["current_item_lang_code"] = ""
            progress["processed_count"] = processed_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        if msg:
            print(f"[{key_name}/{lang_code}] Clear completed: {msg}")
        else:
            print(f"[{key_name}/{lang_code}] Clear completed.")

        translated = translate_single_line(key_name, lang_code, en_text)
        if not isinstance(translated, str) or not translated.strip():
            print(f"[{key_name}/{lang_code}] Translation failed or returned empty text.")
            error_count += 1
            handle_operation_error(
                f"[{key_name}/{lang_code}] Translation failed or returned empty text.",
                stop_on_error,
            )
            progress["next_index"] = item_index + 1
            progress["current_item_key"] = ""
            progress["current_item_lang_code"] = ""
            progress["processed_count"] = processed_count
            progress["error_count"] = error_count
            save_resume_state(resume_state)
            continue

        success, msg = update_translation_via_compiler(
            key_name, lang_code, translated, en_text
        )
        if not success:
            print(f"[{key_name}/{lang_code}] Update failed: {msg}")
            error_count += 1
            handle_operation_error(
                f"[{key_name}/{lang_code}] Failed to update the translated text: {msg}",
                stop_on_error,
            )
        else:
            processed_count += 1
            print(
                f"[{key_name}/{lang_code}] Updated with translation: {translated[:80]}{'...' if len(translated) > 80 else ''}"
            )

        progress["next_index"] = item_index + 1
        progress["current_item_key"] = ""
        progress["current_item_lang_code"] = ""
        progress["processed_count"] = processed_count
        progress["error_count"] = error_count
        save_resume_state(resume_state)

    print(f"\nOperation completed! Processed: {processed_count}, Errors: {error_count}")
    return error_count


def translate_single_line(key_name, lang_code, en_text):
    if not key_name or not lang_code or not en_text:
        return None

    language_name = get_language_name_for_code(lang_code)
    language_label = language_name if language_name else f"language code {lang_code}"

    prompt = f"""
You are a professional translator for the eMule software.
Target language code: '{lang_code}'
Target language name: '{language_label}'
Translate the following English UI text into the target language.

Source text (English):
{en_text}

Rules:
1. Return ONLY one plain text line with your translation.
2. Do NOT return the English source text unchanged.
3. Do NOT return JSON, Markdown, explanations, or extra lines.
4. Translate into {language_label}, not into English and not into a neighboring language.
5. Preserve all placeholders, escape sequences, and protected terms from the source exactly.
"""

    result_text = call_active_api(prompt, plain_text=True)
    if not result_text:
        return None

    cleaned = extract_single_translation_candidate_from_plain_text(
        result_text.strip(), en_text
    )
    if not cleaned or not cleaned.strip():
        return None

    return cleaned


def run_multi_key_translation_operation(
    choice,
    specific_keys=None,
    translation_round_count=1,
    stop_on_error=False,
    resume_state=None,
    runtime_settings=None,
):
    specific_key_mode = choice in ("2", "3")

    if resume_state is None:
        params = {
            "specific_keys": list(specific_keys or []),
            "stop_on_error": bool(stop_on_error),
            "translation_round_count": int(translation_round_count),
        }
        progress = {
            "accumulated_unresolved_keys": {},
            "current_item_key": "",
            "current_round": 1,
            "found_specific_keys": [],
            "next_index": 0,
            "pass_fill_only_missing": choice == "1",
            "pass_keys": [],
            "preparation_error_count": 0,
            "prepared_keys": [],
            "round_processed_keys": [],
        }
        if choice == "3":
            progress["phase"] = RESUME_PHASE_PREPARE_KEYS
        else:
            progress["phase"] = RESUME_PHASE_TRANSLATION_ROUND
        resume_state = build_resume_state(
            choice, params, runtime_settings=runtime_settings, progress=progress
        )
        save_resume_state(resume_state)
    else:
        choice = normalize_menu_choice(resume_state.get("operation", {}).get("choice"))
        params = resume_state.get("params", {})
        specific_keys = list(params.get("specific_keys", []))
        translation_round_count = int(params.get("translation_round_count", 1))
        stop_on_error = bool(params.get("stop_on_error", stop_on_error))
        specific_key_mode = choice in ("2", "3")
        print(
            f"Info: Resuming saved {MENU_OPTION_LABELS.get(choice, 'translation')} job."
        )

    params = resume_state.setdefault("params", {})
    progress = resume_state.setdefault("progress", {})
    params["translation_round_count"] = int(translation_round_count)
    params["stop_on_error"] = bool(stop_on_error)
    params["specific_keys"] = list(specific_keys or params.get("specific_keys", []))

    if choice == "3" and progress.get("phase") == RESUME_PHASE_PREPARE_KEYS:
        source_keys = list(params.get("specific_keys", []))
        prepared_keys = list(progress.get("prepared_keys", []))
        next_index = max(0, int(progress.get("next_index", 0)))
        preparation_error_count = int(progress.get("preparation_error_count", 0))

        for item_index in range(next_index, len(source_keys)):
            key_name = source_keys[item_index]
            progress["phase"] = RESUME_PHASE_PREPARE_KEYS
            progress["current_item_key"] = key_name
            progress["next_index"] = item_index
            progress["prepared_keys"] = list(prepared_keys)
            progress["preparation_error_count"] = preparation_error_count
            save_resume_state(resume_state)

            print(f"[{key_name}] Clearing all translations except [en]...")
            success, msg = clear_other_translations_via_compiler(key_name)
            if success:
                prepared_keys.append(key_name)
                if msg:
                    print(f"[{key_name}] Clear completed: {msg}")
                else:
                    print(f"[{key_name}] Clear completed.")
            else:
                print(f"[{key_name}] Clear failed: {msg}")
                preparation_error_count += 1
                handle_operation_error(
                    f"[{key_name}] Failed to clear translations: {msg}",
                    stop_on_error,
                )

            progress["current_item_key"] = ""
            progress["next_index"] = item_index + 1
            progress["prepared_keys"] = list(prepared_keys)
            progress["preparation_error_count"] = preparation_error_count
            save_resume_state(resume_state)

        if not prepared_keys:
            print("No KEY could be prepared for translation.")
            handle_operation_error(
                "No KEY could be prepared for translation.", stop_on_error
            )
            return 1

        specific_keys = list(prepared_keys)
        params["specific_keys"] = list(params.get("specific_keys", []))
        progress["prepared_keys"] = list(prepared_keys)
        progress["phase"] = RESUME_PHASE_TRANSLATION_ROUND
        progress["current_round"] = 1
        progress["next_index"] = 0
        progress["pass_keys"] = []
        progress["round_processed_keys"] = []
        progress["current_item_key"] = ""
        save_resume_state(resume_state)

    current_round = max(1, int(progress.get("current_round", 1)))
    total_rounds = max(1, int(params.get("translation_round_count", 1)))
    if choice not in ("1", "2", "3"):
        total_rounds = 1

    while current_round <= total_rounds:
        print(f'\nReading "{MAP_FILE_PATH}"...')
        keys_list = get_keys_from_map(MAP_FILE_PATH)
        print(f"Found a total of {len(keys_list)} KEYs.\n")

        if not progress.get("pass_keys"):
            if current_round == 1:
                if choice == "1":
                    pass_keys = [item["key"] for item in keys_list]
                    fill_only_missing = True
                elif choice == "2":
                    pass_keys = list(params.get("specific_keys", []))
                    fill_only_missing = False
                elif choice == "3":
                    pass_keys = list(progress.get("prepared_keys", []))
                    fill_only_missing = False
                else:
                    pass_keys = [item["key"] for item in keys_list]
                    fill_only_missing = False
            else:
                pass_keys = list(progress.get("round_processed_keys", []))
                fill_only_missing = False

            prepare_translation_round_resume_state(
                resume_state,
                current_round,
                pass_keys,
                fill_only_missing,
                next_index=0,
                round_processed_keys=[],
            )
            progress = resume_state["progress"]

        if current_round > 1:
            print(f"\n====== Translation round {current_round}/{total_rounds} ======")

        round_result = process_translation_round_with_resume(
            resume_state, keys_list, stop_on_error=stop_on_error
        )
        progress = resume_state["progress"]
        progress["found_specific_keys"] = list(round_result["found_specific_keys"])
        progress["accumulated_unresolved_keys"] = sanitize_resume_unresolved_keys(
            round_result["accumulated_unresolved_keys"]
        )
        save_resume_state(resume_state)

        round_processed_keys = list(round_result["round_processed_keys"])
        if current_round >= total_rounds:
            break
        if not round_processed_keys:
            print(
                "Info: No KEY was processed in this round. Remaining rounds are skipped."
            )
            break

        current_round += 1
        prepare_translation_round_resume_state(
            resume_state,
            current_round,
            round_processed_keys,
            False,
            next_index=0,
            round_processed_keys=[],
        )
        progress = resume_state["progress"]

    progress = resume_state.setdefault("progress", {})
    accumulated_unresolved_keys = sanitize_resume_unresolved_keys(
        progress.get("accumulated_unresolved_keys", {})
    )
    preparation_error_count = int(progress.get("preparation_error_count", 0))
    if accumulated_unresolved_keys:
        print("\nOperations completed with unresolved translations.")
    else:
        print("\nAll operations completed successfully!")

    operation_error_count = preparation_error_count + sum(
        len(lang_list) for lang_list in accumulated_unresolved_keys.values()
    )
    if specific_key_mode:
        existing_keys = {item["key"] for item in get_keys_from_map(MAP_FILE_PATH)}
        missing_specific_keys = [
            key_name
            for key_name in params.get("specific_keys", [])
            if key_name not in existing_keys
        ]
        if missing_specific_keys:
            warning_text = ", ".join(missing_specific_keys)
            print(f"Warning: KEY not found in translations.map: {warning_text}")
            operation_error_count += len(missing_specific_keys)
            handle_operation_error(
                f"KEY not found in translations.map: {warning_text}", stop_on_error
            )

    return operation_error_count


def print_operation_menu():
    print("\nPlease select an operation to perform:")
    for choice, label in MENU_OPTION_ITEMS:
        print(f"{choice}. {label}")


def execute_menu_operation(
    choice, cli_args=None, use_cli_values=False, interactive_menu_options=None
):
    if use_cli_values and cli_args:
        stop_on_error = bool(cli_args.stop_on_error)
    elif interactive_menu_options is not None:
        stop_on_error = bool(interactive_menu_options.get("stop_on_error", False))
    else:
        stop_on_error = False
    runtime_settings = None

    if choice == "8":
        error_count = remove_unused_keys_logic(stop_on_error=stop_on_error)
        print_total_elapsed_time()
        return error_count
    if choice == "9":
        error_count = find_missing_translations_logic(
            start_line=cli_args.start_line if use_cli_values and cli_args else None,
            limit=cli_args.limit if use_cli_values and cli_args else None,
        )
        print_total_elapsed_time()
        return error_count
    if choice == "10":
        error_count = fix_translations_map_logic(stop_on_error=stop_on_error)
        print_total_elapsed_time()
        return error_count
    if choice in {"11", "12", "13", "14", "15", "16", "17"}:
        error_count = run_map_toolkit_menu_operation(choice)
        print_total_elapsed_time()
        return error_count

    if choice == "6":
        resume_state = load_resume_state()
        if resume_state is None:
            print("Info: Resume file not found. There is no interrupted translation job to continue.")
            return 1

        if is_legacy_resume_state(resume_state):
            ensure_translation_backend_ready(prompt_user=True)
            print_active_backend_info()
            error_count = run_legacy_resume_mapping_operation(
                resume_state.get("params", {}).get("resume_key", ""),
                stop_on_error=bool(
                    resume_state.get("params", {}).get("stop_on_error", False)
                ),
            )
            print_total_elapsed_time()
            delete_resume_state_file()
            return error_count

        apply_runtime_override_settings(resume_state.get("runtime", {}))
        ensure_translation_backend_ready(prompt_user=True)
        resumed_choice = normalize_menu_choice(
            resume_state.get("operation", {}).get("choice")
        )
        print(
            f"Info: Restored saved operation: {resumed_choice}. {MENU_OPTION_LABELS.get(resumed_choice, '')}"
        )
        print_active_backend_info()

        if resumed_choice in ("1", "2", "3", "7"):
            error_count = run_multi_key_translation_operation(
                resumed_choice, resume_state=resume_state
            )
        elif resumed_choice == "4":
            error_count = clean_then_translate_line_numbers_logic(
                resume_state=resume_state
            )
        elif resumed_choice == "5":
            error_count = clean_then_translate_specific_key_languages_logic(
                resume_state=resume_state
            )
        else:
            raise ValueError("Saved resume operation is not supported.")

        print_total_elapsed_time()
        delete_resume_state_file()
        return error_count

    if choice in {"1", "2", "3", "4", "5", "7"}:
        ensure_translation_backend_ready(prompt_user=True)
        print_active_backend_info()
        runtime_settings = collect_runtime_override_settings(
            cli_args if use_cli_values else None
        )

    if choice == "1":
        translation_round_count = prompt_translation_round_count(
            cli_args.rounds if use_cli_values and cli_args else None
        )
        error_count = run_multi_key_translation_operation(
            "1",
            translation_round_count=translation_round_count,
            stop_on_error=stop_on_error,
            runtime_settings=runtime_settings,
        )
    elif choice == "2":
        raw_keys_input = read_cli_or_prompt_value(
            cli_args.keys if use_cli_values and cli_args else None,
            "Enter the KEY or comma-separated KEY list to translate/update: ",
            "KEY list must be provided for operation 2 when stdin is not interactive.",
        )
        specific_keys = parse_specific_keys_input(raw_keys_input)
        if not specific_keys:
            print("Invalid KEY input.")
            handle_operation_error("Invalid KEY input for operation 2.", stop_on_error)
            return 1
        translation_round_count = prompt_translation_round_count(
            cli_args.rounds if use_cli_values and cli_args else None
        )
        error_count = run_multi_key_translation_operation(
            "2",
            specific_keys=specific_keys,
            translation_round_count=translation_round_count,
            stop_on_error=stop_on_error,
            runtime_settings=runtime_settings,
        )
    elif choice == "3":
        raw_keys_input = read_cli_or_prompt_value(
            cli_args.keys if use_cli_values and cli_args else None,
            "Enter the KEY or comma-separated KEY list to clean and translate: ",
            "KEY list must be provided for operation 3 when stdin is not interactive.",
        )
        specific_keys = parse_specific_keys_input(raw_keys_input)
        if not specific_keys:
            print("Invalid KEY input.")
            handle_operation_error("Invalid KEY input for operation 3.", stop_on_error)
            return 1
        translation_round_count = prompt_translation_round_count(
            cli_args.rounds if use_cli_values and cli_args else None
        )
        error_count = run_multi_key_translation_operation(
            "3",
            specific_keys=specific_keys,
            translation_round_count=translation_round_count,
            stop_on_error=stop_on_error,
            runtime_settings=runtime_settings,
        )
    elif choice == "4":
        error_count = clean_then_translate_line_numbers_logic(
            raw_input_text=cli_args.line_numbers if use_cli_values and cli_args else None,
            stop_on_error=stop_on_error,
            runtime_settings=runtime_settings,
        )
    elif choice == "5":
        error_count = clean_then_translate_specific_key_languages_logic(
            raw_input_text=cli_args.key_lang_pairs if use_cli_values and cli_args else None,
            stop_on_error=stop_on_error,
            runtime_settings=runtime_settings,
        )
    elif choice == "7":
        print("Info: Ignoring resume point. Starting from the beginning.")
        error_count = run_multi_key_translation_operation(
            "7",
            translation_round_count=1,
            stop_on_error=stop_on_error,
            runtime_settings=runtime_settings,
        )
    else:
        print("Invalid choice.")
        handle_operation_error(f"Invalid choice: {choice}", stop_on_error)
        return 1

    print_total_elapsed_time()
    delete_resume_state_file()
    return error_count


def print_top_level_command_help():
    script_name = os.path.basename(__file__)
    print("eMule AI translator and translations.map toolkit\n")
    print("Interactive UI:")
    print(f"  python {script_name}")
    print("\nStructured CLI:")
    print(f"  python {script_name} translate --help")
    print(f"  python {script_name} map --help")
    print("\nExamples:")
    print(
        f"  python {script_name} translate missing-only --backend cloud --loop true --stop-on-error true"
    )
    print(
        f"  python {script_name} translate clean-key-languages --pairs KEYA,tr,KEYB,ar --backend local"
    )
    print(f"  python {script_name} map compile --map {MAP_FILE_PATH}")
    print(
        "\nLegacy CLI compatibility is still available via --choice / --operation."
    )


def main():
    global SCRIPT_START_TIME
    global API_TYPE

    SCRIPT_START_TIME = time.time()
    first_cli_token = str(sys.argv[1]).strip().lower() if len(sys.argv) > 1 else ""
    if first_cli_token in {"help", "--help", "-h"}:
        print_top_level_command_help()
        return 0

    try:
        map_cli_args = parse_map_toolkit_arguments(sys.argv[1:])
    except SystemExit as exit_error:
        return int(exit_error.code)
    if map_cli_args is not None:
        try:
            return execute_map_toolkit_action(map_cli_args)
        except (RcParseError, MapParseError, RuntimeError, ValueError) as err:
            print(f"ERROR: {err}")
            return 1

    try:
        cli_args = parse_structured_translate_arguments(sys.argv[1:])
    except SystemExit as exit_error:
        return int(exit_error.code)
    except ValueError as err:
        print(f"ERROR: {err}")
        return 1
    if cli_args is None:
        try:
            cli_args = parse_command_line_arguments(sys.argv[1:])
        except ValueError as err:
            print(f"ERROR: {err}")
            return 1

    apply_command_line_overrides(cli_args)

    if LOG_AI_RESPONSES:
        try:
            with open(LOG_FILE_PATH, "w", encoding="utf-8") as log_file:
                log_file.write(f"AI response log for {os.path.basename(__file__)}\n")
                log_file.write(f"Started at {time.strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        except Exception as log_error:
            print(f"Warning: Failed to initialize AI log file: {log_error}")

    print("eMule Translation Auto-Translator Script Starting...")

    if LOG_AI_RESPONSES:
        print(f"Info: AI response log = {LOG_FILE_PATH}")

    run_in_loop = cli_args.loop if cli_args.loop is not None else cli_args.choice is None
    use_cli_choice_once = cli_args.choice is not None
    last_exit_code = 0
    interactive_menu_options = build_initial_interactive_menu_options(cli_args)

    while True:
        if use_cli_choice_once:
            choice = cli_args.choice
            print(f"\nCLI selected operation: {choice}. {MENU_OPTION_LABELS[choice]}")
            use_cli_choice_once = False
        else:
            choice, interactive_menu_options = prompt_for_interactive_operation_selection(
                interactive_menu_options
            )
            API_TYPE = normalize_backend_setting(
                interactive_menu_options.get("backend", API_BACKEND_LOCAL)
            )
            run_in_loop = bool(interactive_menu_options.get("loop", True))

        try:
            normalized_choice = normalize_menu_choice(choice)
        except ValueError:
            normalized_choice = None
        if normalized_choice is None:
            print("Invalid choice.")
            last_exit_code = 1
            if not run_in_loop:
                return last_exit_code
            continue

        if normalized_choice == "18":
            print("Exiting...")
            return last_exit_code

        try:
            operation_error_count = execute_menu_operation(
                normalized_choice,
                cli_args=cli_args,
                use_cli_values=cli_args.choice is not None and not use_cli_choice_once,
                interactive_menu_options=interactive_menu_options,
            )
            last_exit_code = 1 if operation_error_count else 0
        except OperationAbortError as err:
            return 1
        except RuntimeError as err:
            print(f"ERROR: {err}")
            return 1
        except ValueError as err:
            print(f"ERROR: {err}")
            return 1
        except KeyboardInterrupt:
            print("\nOperation cancelled by user.")
            return 1

        if cli_args.choice is not None:
            cli_args.choice = None

        if not run_in_loop:
            return last_exit_code


if __name__ == "__main__":
    sys.exit(main())
