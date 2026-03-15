# -*- coding: utf-8 -*-
"""
This file is part of eMule AI
Copyright (C)2026 eMule AI

eMule Translation Auto-Translator Script
Updates the translations.map file using the Gemini API.
"""

import os
import sys
import json
import time
import urllib.request
import urllib.error
import subprocess
import re

# ==============================================================================
# USER SETTINGS
# ==============================================================================
# Read Gemini API Key from the system environment variables
GEMINI_API_KEY = os.environ.get("GEMINI_API_KEY", "")

# You can specify the Gemini model name. gemini-2.5-flash or gemini-2.5-pro are suitable.
GEMINI_MODEL_NAME = "gemini-2.5-flash"

# File paths (relative to the C++ project directory structure)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRCHYBRID_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, '..'))
MAP_FILE_PATH = os.path.join(SCRIPT_DIR, "translations.map")
COMPILER_PATH = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', 'TranslationCompiler', 'Release', 'x64', 'TranslationCompiler.exe'))
RESUME_POINT_FILE = os.path.join(SCRIPT_DIR, "gemini_translator_resume.txt")
REF_FILE = os.path.join(SCRIPT_DIR, "used_keys_reference.txt")

# API Rate Limit Wait Time (Seconds) - One API call will be processed at a time
API_DELAY_SEC = 3.0
MAX_LANGUAGES_PER_BATCH = 25

# ==============================================================================

EXTERNAL_TOOL_PATH_CACHE = {}

PROTECTED_BRAND_TOKENS = {
    "eMule",
    "Kad",
    "Windows",
    "Microsoft",
    "MaxMind",
    "GeoLite",
    "GeoLite City"
}

TOKEN_SCAN_PATTERN = re.compile(r'[A-Za-z][A-Za-z0-9+._-]*')
ASCII_LEADING_WORD_PATTERN = re.compile(r"[A-Za-z]+(?:['’-][A-Za-z]+)*")
NON_WORD_BOUNDARY_PATTERN = r'[A-Za-z0-9_]'
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
    "Do"
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
    "would"
}
ALLOWED_SHARED_LEADING_LABELS = {
    "Bonus",
    "Nick",
    "Ratio",
    "RELEASE",
    "Scenario",
    "Start"
}
ESCAPE_ARTIFACT_PREFIXES = ("", "n", "r", "t")
TERMINAL_PUNCTUATION_CHARS = ".!?؟。！？።"
TERMINAL_PUNCTUATION_END_PATTERN = re.compile(r'[.!?؟。！？።]+\s*$')
ESCAPED_LINE_SPLIT_PATTERN = re.compile(r'(\\r\\n|\\n|\\r|\r\n|\n|\r)')
PROTECTED_TOKEN_LEADING_TRIM_CHARS = "\"'([{"
PROTECTED_TOKEN_TRAILING_TRIM_CHARS = "\"')]}:;,.!?؟。！？።"

def normalize_line_break_separator(separator):
    if separator in ('\\r\\n', '\r\n', '\\n', '\n', '\\r', '\r'):
        return '\n'
    return separator

def build_visible_prompt_text(text):
    if not isinstance(text, str) or not text:
        return text

    visible_text = text.replace('\\r\\n', '\n')
    visible_text = visible_text.replace('\\n', '\n')
    visible_text = visible_text.replace('\\r', '\n')
    visible_text = visible_text.replace('\\t', '\t')
    visible_text = visible_text.replace('\\\\', '\\')
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

    if token in PROTECTED_BRAND_TOKENS:
        return True

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

def extract_protected_terms(en_text):
    if not en_text:
        return []

    extraction_text = build_visible_prompt_text(en_text)
    terms = set()
    for match in TOKEN_SCAN_PATTERN.finditer(extraction_text):
        token = normalize_protected_token(match.group(0))
        if should_protect_token(token):
            terms.add(token)

    for fixed_term in PROTECTED_BRAND_TOKENS:
        if fixed_term in extraction_text:
            terms.add(fixed_term)

    return sorted(terms, key=lambda item: (-len(item), item))

def build_protected_placeholders(en_text, lang_dict):
    terms = extract_protected_terms(en_text)
    if not terms:
        return []

    occupied_text_parts = [en_text]
    if isinstance(lang_dict, dict):
        occupied_text_parts.extend(t for t in lang_dict.values() if isinstance(t, str))
    occupied_text = '\n'.join(occupied_text_parts)

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
        pattern = re.compile(rf'(?<!{NON_WORD_BOUNDARY_PATTERN}){re.escape(term)}(?!{NON_WORD_BOUNDARY_PATTERN})')
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
    return not (next_char.isalpha() or next_char.isdigit() or next_char == '_')

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
        return text[:idx] + upper_ch + text[idx + 1:]

    return text

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
        phrase = ' '.join(leading_words[:word_count])
        if word_count == 1 and len(first_word) < 4 and first_word.lower() not in {"do", "use"}:
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

            en_remainder = en_segment[len(phrase):]
            translated_remainder = translated_body[len(candidate):]
            trimmed_translated_remainder = translated_remainder.lstrip()
            trimmed_en_remainder = en_remainder.lstrip()

            is_full_english_copy = translated_remainder.startswith(en_remainder)
            if trimmed_en_remainder and trimmed_translated_remainder.startswith(trimmed_en_remainder):
                is_full_english_copy = True

            first_remainder_char = trimmed_translated_remainder[:1]
            is_removable = bool(first_remainder_char) and first_remainder_char.isalpha() and not is_full_english_copy

            return {
                "leading_whitespace": leading_whitespace,
                "translated_remainder": trimmed_translated_remainder,
                "removable": is_removable
            }

    return None

def strip_copied_english_leading_fragment(en_segment, translated_segment):
    detection = detect_copied_english_leading_fragment(en_segment, translated_segment)
    if detection and detection["removable"]:
        return detection["leading_whitespace"] + uppercase_first_cased_character(detection["translated_remainder"])
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
        if normalize_line_break_separator(en_parts[idx]) != normalize_line_break_separator(translated_parts[idx]):
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

    match = re.search(rf'{re.escape(term)}([{re.escape(TERMINAL_PUNCTUATION_CHARS)}]+)\s*$', en_segment)
    if not match:
        return ""

    return match.group(1)

def is_single_repeated_punctuation(text):
    return bool(text) and len(set(text)) == 1

def repair_leaked_terminal_punctuation_in_segment(en_segment, translated_segment, protected_terms):
    if not isinstance(en_segment, str) or not isinstance(translated_segment, str):
        return translated_segment
    if not en_segment or not translated_segment or not protected_terms:
        return translated_segment

    cleaned_segment = translated_segment
    for term in protected_terms:
        if term not in en_segment or term not in cleaned_segment:
            continue

        source_terminal_punctuation = get_source_terminal_punctuation_for_term(en_segment, term)
        if not source_terminal_punctuation:
            continue

        last_term_pos = cleaned_segment.rfind(term)
        if last_term_pos < 0:
            continue

        after_term_pos = last_term_pos + len(term)
        suffix = cleaned_segment[after_term_pos:]

        continuation_match = re.match(rf'([{re.escape(TERMINAL_PUNCTUATION_CHARS)}]+)(\s+)(?=\S)', suffix)
        if continuation_match:
            cleaned_segment = cleaned_segment[:after_term_pos] + suffix[len(continuation_match.group(1)):]
            suffix = cleaned_segment[after_term_pos:]

        if not is_single_repeated_punctuation(source_terminal_punctuation):
            continue

        source_punct_char = source_terminal_punctuation[0]
        duplicate_match = re.match(rf'({re.escape(source_punct_char)}{{{len(source_terminal_punctuation) + 1},}})(\s*)$', suffix)
        if duplicate_match:
            cleaned_segment = cleaned_segment[:after_term_pos] + source_terminal_punctuation + duplicate_match.group(2)

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
        if normalize_line_break_separator(en_parts[idx]) != normalize_line_break_separator(translated_parts[idx]):
            return translated_text

    protected_terms = [term for _, term in placeholder_pairs]
    cleaned_parts = []
    for idx, translated_part in enumerate(translated_parts):
        if idx % 2 == 1:
            cleaned_parts.append(translated_part)
            continue
        cleaned_parts.append(repair_leaked_terminal_punctuation_in_segment(en_parts[idx], translated_part, protected_terms))

    return ''.join(cleaned_parts)

def cleanup_translated_text(en_text, translated_text, placeholder_pairs):
    cleaned_text = strip_copied_english_leading_fragments(en_text, translated_text)
    return repair_leaked_terminal_punctuation(en_text, cleaned_text, placeholder_pairs)

def has_escape_or_punctuation_leak_issue(en_text, translated_text, placeholder_pairs):
    if has_copied_english_leading_fragments(en_text, translated_text):
        return True
    if placeholder_pairs and repair_leaked_terminal_punctuation(en_text, translated_text, placeholder_pairs) != translated_text:
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
        if normalize_line_break_separator(en_parts[idx]) != normalize_line_break_separator(translated_parts[idx]):
            return translated_text

    cleaned_parts = []
    for idx, translated_part in enumerate(translated_parts):
        if idx % 2 == 1:
            cleaned_parts.append(translated_part)
            continue
        if idx == 0:
            cleaned_parts.append(translated_part)
            continue
        cleaned_parts.append(strip_copied_english_leading_fragment(en_parts[idx], translated_part))

    return ''.join(cleaned_parts)

def collect_local_cleanup_updates(en_text, lang_dict):
    updates = {}
    if not isinstance(lang_dict, dict):
        return updates

    placeholder_pairs = build_protected_placeholders(en_text, lang_dict)

    for lang, text in lang_dict.items():
        if not isinstance(text, str):
            continue

        cleaned_text = cleanup_translated_text(en_text, text, placeholder_pairs)
        if cleaned_text != text:
            updates[lang] = cleaned_text

    return updates

def build_prompt_lang_dict(en_text, lang_dict, placeholder_pairs):
    prompt_lang_dict = {}
    if not isinstance(lang_dict, dict):
        return prompt_lang_dict

    for lang, text in lang_dict.items():
        cleaned_text = cleanup_translated_text(en_text, text, placeholder_pairs)
        if has_escape_or_punctuation_leak_issue(en_text, text, placeholder_pairs) or cleaned_text != text:
            # Force a full re-translation when an escape-related leak is detected.
            prompt_lang_dict[lang] = ""
            continue

        prompt_lang_dict[lang] = apply_protected_placeholders(cleaned_text, placeholder_pairs)

    return prompt_lang_dict

def get_external_tool_path(path):
    if not isinstance(path, str) or not path:
        return path
    if os.name == 'nt':
        return path

    cached_path = EXTERNAL_TOOL_PATH_CACHE.get(path)
    if cached_path:
        return cached_path

    converted_path = path
    try:
        result = subprocess.run(["wslpath", "-w", path], capture_output=True, text=True, check=False)
        if result.returncode == 0 and result.stdout.strip():
            converted_path = result.stdout.strip()
    except Exception:
        pass

    EXTERNAL_TOOL_PATH_CACHE[path] = converted_path
    return converted_path

def call_gemini_api(api_key, model_name, prompt, plain_text=False):
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{model_name}:generateContent?key={api_key}"
    headers = {"Content-Type": "application/json"}
    
    data = {
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {
             "temperature": 0.1,
             "response_mime_type": "text/plain" if plain_text else "application/json"
        },
        "safetySettings": [
            { "category": "HARM_CATEGORY_HARASSMENT", "threshold": "BLOCK_NONE" },
            { "category": "HARM_CATEGORY_HATE_SPEECH", "threshold": "BLOCK_NONE" },
            { "category": "HARM_CATEGORY_SEXUALLY_EXPLICIT", "threshold": "BLOCK_NONE" },
            { "category": "HARM_CATEGORY_DANGEROUS_CONTENT", "threshold": "BLOCK_NONE" }
        ]
    }
    
    req = urllib.request.Request(url, json.dumps(data).encode('utf-8'), headers)
    
    while True:
        try:
             with urllib.request.urlopen(req) as response:
                 response_data = response.read().decode('utf-8')
                 resp_json = json.loads(response_data)
                 candidates = resp_json.get("candidates", [])
                 if not candidates:
                     feedback = resp_json.get("promptFeedback", {})
                     if feedback.get("blockReason") == "PROHIBITED_CONTENT":
                         print(f"API Blocked due to PROHIBITED_CONTENT. Emulating empty response to skip.")
                         return "{}" if not plain_text else ""
                     print(f"API Returned no candidates: {resp_json}")
                     return None
                 
                 parts = candidates[0].get("content", {}).get("parts", [])
                 if not parts:
                     print(f"API Blocked or Empty Content: finishReason={candidates[0].get('finishReason', 'UNKNOWN')} ({resp_json})")
                     return None
                 content = parts[0].get("text", "")
                 return content.strip() if plain_text else content
        except urllib.error.HTTPError as e:
             print(f"API HTTP Error: {e.code} - {e.read().decode('utf-8')}")
             print("Waiting 60 seconds before retrying...")
             time.sleep(60)
        except Exception as e:
             print(f"API Call Error: {e}")
             print("Waiting 60 seconds before retrying...")
             time.sleep(60)

def check_and_translate_with_gemini(key_name, en_text, lang_dict):
    protected_placeholders = build_protected_placeholders(en_text, lang_dict)
    prompt_en_text = apply_protected_placeholders(en_text, protected_placeholders)
    prompt_en_text_block = build_prompt_text_block("ORIGINAL ENGLISH TEXT", en_text, prompt_en_text)
    prompt_lang_dict = build_prompt_lang_dict(en_text, lang_dict, protected_placeholders)
    protected_placeholders_json = get_protected_placeholders_prompt_block(protected_placeholders)

    prompt = f"""
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
5. You MUST strictly preserve all escape characters (\\n, \\r, \\\\, %%) and placeholders (%s, %i, %u, %lu, etc.) EXACTLY AS THEY ARE IN THE ORIGINAL ENGLISH TEXT!
6. If the existing translation is already correct, skip it (do not include it in the JSON output).
7. If the language code is "en", NEVER modify it.
8. RETURN ONLY the languages you have updated, fixed, or newly added in JSON format as `{{"language_code": "new_translation"}}`. If no languages need updating, return an empty `{{}}` JSON.
9. Your response MUST be in strictly parsable JSON format. It must NOT contain Markdown blocks (```json), extra text, or comments. Return pure JSON string only.
10. LOCKED TERM RULE: If you see placeholders like __LOCKED_TERM_0__, these represent protected special terms and acronyms. Never translate, never transliterate, never inflect, never remove, and never alter these placeholders. Keep them exactly unchanged in output strings.
11. NEVER keep a partial English lead-in at the beginning of any translated paragraph or line. If the source segment starts with English words like "Do", "Do you", "Please", or "Use", translate them fully instead of leaving them in English.
12. IMPORTANT: If the source string contains escaped line breaks such as \\n, \\r\\n, or \\r, the next word begins a new translated line or paragraph. For example, `Found.\\n\\nDo you want...` means the `Do you want...` sentence must also be translated fully.
13. IMPORTANT: Locked placeholders protect only the special term itself, not the punctuation around it. If English has a pattern like `MaxMind.\\n\\nCopy...`, the period ends the sentence, but it does NOT have to stay immediately after the protected term in the target language if the sentence structure changes.

LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}

EXISTING TRANSLATIONS JSON:
{json.dumps(prompt_lang_dict, ensure_ascii=False, indent=2)}
"""
    result_text = call_gemini_api(GEMINI_API_KEY, GEMINI_MODEL_NAME, prompt, plain_text=False)
    if not result_text:
        return None
    
    try:
        result_text = result_text.strip()
        
        # Robustly extract the JSON dictionary block
        start_idx = result_text.find('{')
        end_idx = result_text.rfind('}')
        if start_idx != -1 and end_idx != -1 and end_idx >= start_idx:
            result_text = result_text[start_idx:end_idx + 1]
            
        # Sanitize invalid JSON escapes (like \ + non-escape char) that Gemini might generate
        # and correctly handle consecutive backslashes by consuming valid escapes first.
        result_text = re.sub(r'(\\["\\/bfnrtu])|(\\)', lambda m: m.group(1) if m.group(1) else '\\\\', result_text)
        
        updates = json.loads(result_text)
        
        # Enforce exact trailing whitespace/newlines from English text
        en_trailing_match = re.search(r'(\\n|\n|\s)+$', en_text)
        en_trailing = en_trailing_match.group(0) if en_trailing_match else ""
        
        cleaned_updates = {}
        if isinstance(updates, dict):
            for lang, t_text in updates.items():
                if not isinstance(t_text, str):
                    continue

                restored_text = restore_protected_placeholders(t_text, protected_placeholders)
                cleaned_restored_text = cleanup_translated_text(en_text, restored_text, protected_placeholders)
                if protected_placeholders and not all_protected_terms_preserved(cleaned_restored_text, protected_placeholders):
                    missing_terms = get_missing_protected_terms(cleaned_restored_text, protected_placeholders)
                    print(f"[{key_name}] Warning: [{lang}] protected terms were not preserved ({', '.join(missing_terms)}). Skipping this language update.")
                    continue

                # Strip all trailing spaces/newlines from the translation
                clean_t = re.sub(r'(\\n|\n|\s)+$', '', cleaned_restored_text)
                # Add the exact trailing from English
                cleaned_updates[lang] = clean_t + en_trailing
                
        return cleaned_updates
    except Exception as e:
        print(f"JSON Parse Error ({key_name}): {e}")
        print(f"Broken JSON Response:\n{result_text}")
        return None

def fix_translation_with_gemini(key_name, lang_code, faulty_text, error_message, en_text):
    initial_placeholders = build_protected_placeholders(en_text, {lang_code: faulty_text})
    sanitized_faulty_text = cleanup_translated_text(en_text, faulty_text, initial_placeholders)
    protected_placeholders = build_protected_placeholders(en_text, {lang_code: sanitized_faulty_text})
    prompt_en_text = apply_protected_placeholders(en_text, protected_placeholders)
    prompt_faulty_text = apply_protected_placeholders(sanitized_faulty_text, protected_placeholders)
    prompt_en_text_block = build_prompt_text_block("Original English Text", en_text, prompt_en_text)
    prompt_faulty_text_block = build_prompt_text_block("Faulty Translation", sanitized_faulty_text, prompt_faulty_text)
    protected_placeholders_json = get_protected_placeholders_prompt_block(protected_placeholders)

    prompt = f"""
You are a professional translator and translation quality controller for the eMule software.
We tried to add your translation but the TranslationCompiler threw an error.

KEY: '{key_name}'
Language: '{lang_code}'
{prompt_en_text_block}

{prompt_faulty_text_block}
Compiler Error:
"{error_message}"

Fix the translation so it doesn't cause this error.
Rules:
1. Preserve all placeholders (%s, %i, %u, %lu, etc.) EXACTLY as they are in the English text.
2. Preserve all escape characters (\\n, \\r, \\\\, %%) EXACTLY.
3. If there are placeholders like __LOCKED_TERM_0__, they are protected special terms/acronyms. Keep them exactly unchanged.
4. Never keep a partial English lead-in at the beginning of any translated paragraph or line. If the source starts with phrases like "Do", "Do you", "Please", or "Use", translate them fully instead of leaving them in English.
5. If the source string contains escaped line breaks such as \\n, \\r\\n, or \\r, treat them as real line or paragraph boundaries and translate the first word after each boundary too.
6. Punctuation around a locked placeholder is NOT locked. If a protected term is followed by sentence-ending punctuation in English, move that punctuation to the natural sentence-ending position in the target language when needed.
7. RETURN ONLY JSON formatted response in the following format:
   {{"corrected_translation": "your_fixed_translation_string_here"}}
8. Do NOT add any explanations or conversations outside the JSON block. Return pure JSON string only.

LOCKED PLACEHOLDERS JSON:
{protected_placeholders_json}
"""
    result_text = call_gemini_api(GEMINI_API_KEY, GEMINI_MODEL_NAME, prompt, plain_text=False)
    if not result_text:
        return None
        
    try:
        result_text = result_text.strip()
        
        # Robustly extract the JSON dictionary block
        start_idx = result_text.find('{')
        end_idx = result_text.rfind('}')
        if start_idx != -1 and end_idx != -1 and end_idx >= start_idx:
            result_text = result_text[start_idx:end_idx + 1]
            
        # Sanitize invalid JSON escapes
        result_text = re.sub(r'(\\["\\/bfnrtu])|(\\)', lambda m: m.group(1) if m.group(1) else '\\\\', result_text)
        
        parsed = json.loads(result_text)
        corrected = parsed.get("corrected_translation")
        if not isinstance(corrected, str):
            return None
            
        en_trailing_match = re.search(r'(\\n|\n|\s)+$', en_text)
        en_trailing = en_trailing_match.group(0) if en_trailing_match else ""
        
        corrected_restored = restore_protected_placeholders(corrected, protected_placeholders)
        corrected_cleaned = strip_copied_english_leading_fragments(en_text, corrected_restored)
        clean_t = re.sub(r'(\\n|\n|\s)+$', '', corrected_cleaned)
        return clean_t + en_trailing
        
    except Exception as e:
        print(f"  -> [{lang_code}] JSON Parse Error during fix: {e}")
        print(f"  -> Broken JSON Response:\n{result_text}")
        return None

def remove_bidi_characters(text):
    # MSVC C5255: unterminated bidirectional character (Trojan Source mitigation)
    bidi_chars = [
        '\u200E', '\u200F',  # LRM, RLM
        '\u202A', '\u202B', '\u202C', '\u202D', '\u202E',  # LRE, RLE, PDF, LRO, RLO
        '\u2066', '\u2067', '\u2068', '\u2069'  # LRI, RLI, FSI, PDI
    ]
    for c in bidi_chars:
        text = text.replace(c, '')
    return text

def update_translation_via_compiler(key_name, lang_code, new_text, en_text=None):
    clean_text = new_text
    if isinstance(en_text, str) and en_text:
        placeholder_pairs = build_protected_placeholders(en_text, {lang_code: clean_text})
        clean_text = cleanup_translated_text(en_text, clean_text, placeholder_pairs)

    clean_text = remove_bidi_characters(clean_text)
    
    # Auto-correct AI translated escape characters that got transliterated
    # t -> ט(Hebrew/Yiddish), т(Cyrillic), τ(Greek), ط/ت(Arabic)
    clean_text = re.sub(r'\\([טтτطت])', r'\\t', clean_text)
    # n -> נ(Hebrew/Yiddish), н(Cyrillic), ν(Greek), ن(Arabic)
    clean_text = re.sub(r'\\([נнνن])', r'\\n', clean_text)
    # r -> ר(Hebrew/Yiddish), р(Cyrillic), ρ(Greek), ر(Arabic)
    clean_text = re.sub(r'\\([רрρر])', r'\\r', clean_text)
    
    # Escape invalid C++ escape sequences to avoid Map2cpp compiler errors (like \:)
    def escape_c_invalid(m):
        c = m.group(1)
        if not c:
            return '\\\\'
        if c in 'nrt"\'\\0xu':
            return m.group(0) # Keep valid escape intact
        return '\\\\' + c
    clean_text = re.sub(r'\\(.)|\\$', escape_c_invalid, clean_text)
    
    # Ensure any literal newlines/tabs decoded from JSON are converted back to literal C-style escape strings
    clean_text = clean_text.replace('\r', '\\r').replace('\n', '\\n').replace('\t', '\\t')
    
    cmd = [
        COMPILER_PATH,
        "add",
        get_external_tool_path(MAP_FILE_PATH),
        "--key", key_name,
        "--lang", lang_code,
        "--text", clean_text
    ]
    
    try:
        # Run process synchronously
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            err = result.stderr.strip() or result.stdout.strip()
            return False, err
        else:
            return True, result.stdout.strip()
    except Exception as e:
        return False, str(e)

def clear_other_translations_via_compiler(key_name, create_backup=False):
    cmd = [
        COMPILER_PATH,
        "clear_others",
        get_external_tool_path(MAP_FILE_PATH),
        "--key", key_name
    ]
    if create_backup:
        cmd.append("--backup")

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            err = result.stderr.strip() or result.stdout.strip()
            return False, err
        return True, result.stdout.strip()
    except Exception as e:
        return False, str(e)

def get_keys_from_map(filepath):
    """
    Parses the map file and returns an ordered list like:
    [{"key": key_name, "langs": {lang_code: text, ...}}, ...] 
    """
    keys_list = []
    
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found!")
        return keys_list

    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    current_key = None
    current_langs = {}

    for line in lines:
        line_stripped = line.rstrip('\n\r')
        
        if not line_stripped:
            if current_key:
                keys_list.append({"key": current_key, "langs": current_langs})
                current_key = None
                current_langs = {}
            continue

        if line_stripped[0] not in ('\t', ' '):
            if current_key:
                keys_list.append({"key": current_key, "langs": current_langs})
            current_key = line_stripped.strip()
            current_langs = {}
        else:
            if current_key:
                parts = line_stripped.lstrip().split('\t', 1)
                if len(parts) == 1:
                    parts = line_stripped.lstrip().split(' ', 1)
                
                lang = parts[0].strip()
                text = parts[1] if len(parts) > 1 else ""
                current_langs[lang] = text

    if current_key:
        keys_list.append({"key": current_key, "langs": current_langs})
        
    return keys_list

def parse_specific_keys_input(raw_value):
    keys = []
    seen = set()

    for part in raw_value.split(','):
        key_name = part.strip()
        if not key_name or key_name in seen:
            continue
        keys.append(key_name)
        seen.add(key_name)

    return keys

def get_all_source_files():
    cpp_files = []
    # Directories that should not be scanned or are unnecessary
    skip_dirs = {'.git', '_transmission', '_eMule_v0.70b', 'loglar', 'translations'}
    for root, dirs, files in os.walk(SRCHYBRID_DIR):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            if f.lower() == 'translations_data.gen.h':
                continue
            if f.lower().endswith(('.cpp', '.h', '.inl', '.rc', '.hpp', '.c')):
                cpp_files.append(os.path.join(root, f))
    return cpp_files

def collect_reference_lines():
    print("Scanning source files for lines containing '_T(\"'...")
    files = get_all_source_files()
    lines_collected = []
    for fp in files:
        try:
            with open(fp, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    if '_T("' in line:
                        lines_collected.append(line.strip())
        except Exception:
            pass

    # Save to the reference file
    with open(REF_FILE, 'w', encoding='utf-8') as f:
        for line in lines_collected:
            f.write(line + '\n')
            
    print(f"A total of {len(lines_collected)} lines containing '_T(\"' have been saved to {REF_FILE}.")
    return lines_collected

def remove_unused_keys_logic():
    print("\neMule Unused Translations Cleaner Tool Starting...")
    
    if not os.path.exists(COMPILER_PATH):
        print(f"ERROR: TranslationCompiler.exe not found: {COMPILER_PATH}")
        return
        
    lines = collect_reference_lines()
    keys_data = get_keys_from_map(MAP_FILE_PATH)
    keys = [item["key"] for item in keys_data]
    
    unused_keys = []
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
            
    print(f"Analysis complete: Out of {len(keys)} total keys, {len(unused_keys)} were found to be unused.")
    
    if len(unused_keys) > 0:
        print("Removing unused keys via TranslationCompiler.exe...")
        for key in unused_keys:
            cmd = [
                COMPILER_PATH,
                "remove",
                get_external_tool_path(MAP_FILE_PATH),
                "--key", key
            ]
            try:
                res = subprocess.run(cmd, capture_output=True, text=True, check=False)
                if res.returncode != 0:
                    print(f"ERROR: Failed to remove {key} -> {res.stderr.strip() or res.stdout.strip()}")
                else:
                    print(f"Deleted: {key}")
            except Exception as e:
                print(f"ERROR (System): Failed to run compiler ({key}): {e}")
                
        print("All unused keys have been successfully cleaned.")
    else:
        print("No unused keys found to delete. No action required.")

def find_missing_translations_logic():
    print("\n--- Find Missing Translations (Fast Scan) ---")
    start_line_str = input("Enter StartLine (default 0): ").strip()
    limit_str = input("Enter Limit (default 10): ").strip()
    
    start_line = int(start_line_str) if start_line_str.isdigit() else 0
    limit = int(limit_str) if limit_str.isdigit() else 10
    
    if not os.path.exists(MAP_FILE_PATH):
        print(f"ERROR: {MAP_FILE_PATH} not found!")
        return
        
    results = []
    current_key = ""
    current_en = ""
    current_line_num = 0
    
    with open(MAP_FILE_PATH, 'r', encoding='utf-8', errors='ignore') as f:
        for line_raw in f:
            current_line_num += 1
            line = line_raw.rstrip('\n\r')
            
            if current_line_num < start_line:
                if not line.startswith('\t') and line.strip():
                    current_key = line.strip()
                    current_en = ""
                elif line.startswith('\ten\t'):
                    parts = line.split('\t', 2)
                    if len(parts) > 2:
                        current_en = parts[2]
                continue
                
            if not line.strip():
                continue
                
            if not line.startswith('\t'):
                current_key = line.strip()
                current_en = ""
            elif line.startswith('\ten\t'):
                parts = line.split('\t', 2)
                if len(parts) > 2:
                    current_en = parts[2]
            else:
                m = re.match(r'^\t([^\t]+)\t\s*$', line)
                if m:
                    lang = m.group(1)
                    if current_key and current_en:
                        results.append({
                            "Key": current_key,
                            "Lang": lang,
                            "EnText": current_en,
                            "Line": current_line_num
                        })
                        if len(results) >= limit:
                            break
                            
    output_data = {
        "NextStartLine": current_line_num + 1,
        "Items": results
    }
    
    print("\n" + json.dumps(output_data, ensure_ascii=False, indent=2))

def fix_translations_map_logic():
    print("\n--- Fixing translations.map formatting ---")
    if not os.path.exists(COMPILER_PATH):
        print(f"ERROR: TranslationCompiler.exe not found: {COMPILER_PATH}")
        return
    cmd = [
        COMPILER_PATH,
        "fix",
        get_external_tool_path(MAP_FILE_PATH)
    ]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if res.returncode != 0:
            print(f"ERROR: Failed to run fix -> {res.stderr.strip() or res.stdout.strip()}")
        else:
            print("Successfully fixed translations.map formatting.")
            if res.stdout.strip():
                print(res.stdout.strip())
    except Exception as e:
        print(f"ERROR (System): Failed to run compiler: {e}")

def main():
    print("eMule Translation Auto-Translator Script Starting...")
    
    if not GEMINI_API_KEY:
        print("ERROR: GEMINI_API_KEY environment variable is not set.")
        print("Please set the GEMINI_API_KEY environment variable and try again.")
        return

    if not os.path.exists(COMPILER_PATH):
        print(f"ERROR: TranslationCompiler.exe not found: {COMPILER_PATH}")
        print("Please check the file paths.")
        return

    print("\nPlease select an operation to perform:")
    print("1. Find and complete missing translations only")
    print("2. Translate/update specific translation key(s)")
    print("3. Clean then translate specific translation key(s)")
    print("4. Resume mapping: Update translations from the resume point and only fill missing ones before it")
    print("5. Full mapping: Check and update all translations from the beginning (ignores resume file)")
    print("6. Find and remove unused translation keys")
    print("7. Find missing translations (fast structural scan + JSON output)")
    print("8. Fix translations.map formatting via TranslationCompiler.exe")
    
    choice = input("\nEnter your choice (1-8): ").strip()

    if choice == '6':
        remove_unused_keys_logic()
        return
    elif choice == '7':
        find_missing_translations_logic()
        return
    elif choice == '8':
        fix_translations_map_logic()
        return

    global_fill_only_missing = False
    skip_mode = False
    resume_key = None
    specific_key_mode = False
    specific_keys = []
    specific_keys_set = set()
    found_specific_keys = set()

    if choice == '1':
        global_fill_only_missing = True
    elif choice == '2':
        specific_key_mode = True
        specific_keys = parse_specific_keys_input(input("Enter the KEY or comma-separated KEY list to translate/update: ").strip())
        if not specific_keys:
            print("Invalid KEY input. Exiting.")
            return
        specific_keys_set = set(specific_keys)
    elif choice == '3':
        specific_key_mode = True
        specific_keys = parse_specific_keys_input(input("Enter the KEY or comma-separated KEY list to clean and translate: ").strip())
        if not specific_keys:
            print("Invalid KEY input. Exiting.")
            return

        prepared_keys = []
        for key_name in specific_keys:
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

        if not prepared_keys:
            print("No KEY could be prepared for translation. Exiting.")
            return

        specific_keys = prepared_keys
        specific_keys_set = set(specific_keys)
    elif choice == '4':
        if os.path.exists(RESUME_POINT_FILE):
            with open(RESUME_POINT_FILE, "r", encoding="utf-8") as rf:
                resume_key = rf.read().strip()
                if resume_key:
                    print(f"Info: Resuming from KEY [{resume_key}] (resume point detected).")
                    skip_mode = True
        else:
            print("Info: Resume file not found. Starting from the beginning.")
    elif choice == '5':
        print("Info: Ignoring resume point. Starting from the beginning.")
    else:
        print("Invalid choice. Exiting.")
        return

    print(f"\nReading \"{MAP_FILE_PATH}\"...")
    keys_list = get_keys_from_map(MAP_FILE_PATH)
    print(f"Found a total of {len(keys_list)} KEYs.\n")

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
                
        # English translation will be used as a reference
        en_text = k_langs.get("en", "")
        if not en_text:
            if not fill_only_missing:
                print(f"[{k_name}] Skipped: English (en) translation is missing.")
            continue
            
        # Prepare languages other than "en"
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
            
        # Determine batch size dynamically based on English text length to avoid token limit errors
        en_len = len(en_text) if len(en_text) > 0 else 1
        batch_size = max(1, 10000 // en_len)
        batch_size = min(batch_size, MAX_LANGUAGES_PER_BATCH)
        
        all_updates = {}
        target_langs_items = list(target_langs.items())
        if not fill_only_missing:
            local_cleanup_updates = collect_local_cleanup_updates(en_text, target_langs)
            if local_cleanup_updates:
                print(f"[{k_name}] Found {len(local_cleanup_updates)} local cleanup candidate(s).")
                all_updates.update(local_cleanup_updates)
        
        for i in range(0, len(target_langs_items), batch_size):
            batch_langs = dict(target_langs_items[i:i+batch_size])
            
            if len(target_langs_items) > batch_size:
                print(f"[{k_name}] Processing chunk {i//batch_size + 1}/{(len(target_langs_items) + batch_size - 1)//batch_size}...")
                
            updates = check_and_translate_with_gemini(k_name, en_text, batch_langs)
            
            retry_api = 0
            while updates is None and retry_api < 3:
                print(f"[{k_name}] API call or JSON parsing failed. Retrying in 60 seconds (Attempt {retry_api+1}/3)...")
                time.sleep(60)
                updates = check_and_translate_with_gemini(k_name, en_text, batch_langs)
                retry_api += 1
                
            if updates is None:
                print(f"[{k_name}] Completely failed to process chunk {i//batch_size + 1}. Skipping this chunk to prevent infinite loop.")
                continue
            
            if updates:
                all_updates.update(updates)
                
            if i + batch_size < len(target_langs_items):
                time.sleep(API_DELAY_SEC)
                
        updates = all_updates
        
        if not updates:
            print(f"[{k_name}] No changes required, all languages are appropriate.")
        else:
            for lang_code, new_text in updates.items():
                if lang_code == "en":
                    continue # en should never change!!!
                
                # Validation / Compilation Loop
                max_retries = 3
                retry_count = 0
                while True:
                    print(f"  -> [{lang_code}] Updating...")
                    success, msg = update_translation_via_compiler(k_name, lang_code, new_text, en_text)
                    if success:
                        print(f"  -> [{lang_code}] Successfully added/updated: {msg}")
                        break
                    
                    print(f"  -> [{lang_code}] TranslationCompiler Error: {msg}")
                    
                    if retry_count >= max_retries:
                        print(f"  -> [{lang_code}] Reached maximum retry limit ({max_retries}). Skipping fixing this language.")
                        break
                        
                    retry_count += 1
                    print(f"  -> [{lang_code}] Asking Gemini to fix the translation error (Attempt {retry_count}/{max_retries})...")
                    
                    # Prevent rapid hammering on API Limits
                    time.sleep(API_DELAY_SEC)
                    
                    fixed_text = fix_translation_with_gemini(k_name, lang_code, new_text, msg, en_text)
                    while fixed_text is None:
                        print(f"  -> [{lang_code}] Failed to get a fixed translation from Gemini. Retrying in 60 seconds...")
                        time.sleep(60)
                        fixed_text = fix_translation_with_gemini(k_name, lang_code, new_text, msg, en_text)
                    new_text = fixed_text

        if not fill_only_missing:
            # Update resume_point after a successful or skipping operation
            with open(RESUME_POINT_FILE, "w", encoding="utf-8") as wf:
                wf.write(k_name)
            
        # Wait to avoid rate limit violation
        time.sleep(API_DELAY_SEC)

    # Delete resume_point file if loop finishes without hitting the resume barrier
    if not skip_mode:
        print("\nAll operations completed successfully!")
        if os.path.exists(RESUME_POINT_FILE):
             os.remove(RESUME_POINT_FILE)
             print(f"{os.path.basename(RESUME_POINT_FILE)} deleted.")

    if specific_key_mode:
        missing_specific_keys = [key_name for key_name in specific_keys if key_name not in found_specific_keys]
        if missing_specific_keys:
            print(f"Warning: KEY not found in translations.map: {', '.join(missing_specific_keys)}")

if __name__ == "__main__":
    main()
