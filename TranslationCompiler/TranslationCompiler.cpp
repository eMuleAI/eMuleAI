//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

namespace
{
	// Forward declaration for fallback computation used by WriteLanguageRegistry
	static std::vector<uint16_t> BuildFallbackIndexes(const std::vector<std::string> &languages);
	constexpr uint32_t kFnvOffset = 2166136261u;
	constexpr uint32_t kFnvPrime = 16777619u;
	constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

	[[nodiscard]] std::wstring Utf8ToWide(const std::string &text)
	{
		if (text.empty()) {
			return std::wstring();
		}
		const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		if (required <= 0) {
			throw std::runtime_error("UTF-8 conversion failed");
		}
		std::wstring result(static_cast<size_t>(required), L'\0');
		const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), result.data(), required);
		if (written != required) {
			throw std::runtime_error("UTF-8 conversion failed");
		}
		return result;
	}

	[[nodiscard]] std::string WideToUtf8(const std::wstring &text)
	{
		if (text.empty()) {
			return std::string();
		}
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (required <= 0) {
			throw std::runtime_error("UTF-8 conversion failed");
		}
		std::string result(static_cast<size_t>(required), '\0');
		const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
		if (written != required) {
			throw std::runtime_error("UTF-8 conversion failed");
		}
		return result;
	}

	void EnsureParentDirectory(const std::filesystem::path &path)
	{
		const std::filesystem::path parent = path.parent_path();
		if (!parent.empty()) {
			std::filesystem::create_directories(parent);
		}
	}

	[[nodiscard]] std::string ReadFileBinary(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			throw std::runtime_error("Could not open file: " + path.string());
		}
		std::string buffer;
		stream.seekg(0, std::ios::end);
		const std::streampos size = stream.tellg();
		if (size > 0) {
			const std::streamsize file_size = static_cast<std::streamsize>(size);
			buffer.resize(static_cast<size_t>(file_size));
			stream.seekg(0, std::ios::beg);
			stream.read(buffer.data(), file_size);
		}
		return buffer;
	}

	[[nodiscard]] std::string ReadRcFile(const std::filesystem::path &path)
	{
		const std::string file_data = ReadFileBinary(path);
		if (file_data.size() >= 2) {
			const unsigned char b0 = static_cast<unsigned char>(file_data[0]);
			const unsigned char b1 = static_cast<unsigned char>(file_data[1]);
			if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
				const wchar_t *wide = reinterpret_cast<const wchar_t *>(file_data.data() + 2);
				const size_t wide_length = (file_data.size() - 2) / sizeof(wchar_t);
				return WideToUtf8(std::wstring(wide, wide_length));
			}
		}
		if (file_data.size() >= 3) {
			const unsigned char bom0 = static_cast<unsigned char>(file_data[0]);
			const unsigned char bom1 = static_cast<unsigned char>(file_data[1]);
			const unsigned char bom2 = static_cast<unsigned char>(file_data[2]);
			if (bom0 == 0xEF && bom1 == 0xBB && bom2 == 0xBF) {
				return file_data.substr(3);
			}
		}
		try {
			(void)Utf8ToWide(file_data);
			return file_data;
		} catch (const std::exception &) {
		}
		const int required = MultiByteToWideChar(1252, 0, file_data.c_str(), static_cast<int>(file_data.size()), nullptr, 0);
		if (required <= 0) {
			throw std::runtime_error("Could not decode RC file: " + path.string());
		}
		std::wstring result(static_cast<size_t>(required), L'\0');
		const int written = MultiByteToWideChar(1252, 0, file_data.c_str(), static_cast<int>(file_data.size()), result.data(), required);
		if (written != required) {
			throw std::runtime_error("Could not decode RC file: " + path.string());
		}
		return WideToUtf8(result);
	}

	[[nodiscard]] std::vector<std::string> SplitLines(const std::string &content)
	{
		std::vector<std::string> lines;
		std::string current;
		for (char ch : content) {
			if (ch == '\r') {
				continue;
			}
			if (ch == '\n') {
				lines.push_back(current);
				current.clear();
			} else {
				current.push_back(ch);
			}
		}
		lines.push_back(current);
		return lines;
	}

	struct RcParseError : public std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	[[nodiscard]] std::vector<std::string> ExtractLiterals(const std::string &text, int line)
	{
		std::vector<std::string> literals;
		size_t index = 0;
		while (index < text.size()) {
			size_t quote_pos = text.find('"', index);
			if (quote_pos == std::string::npos) {
				break;
			}
			size_t cursor = quote_pos + 1;
			std::string buffer;
			while (cursor < text.size()) {
				const char ch = text[cursor];
				if (ch == '"') {
					size_t next_index = cursor + 1;
					if (next_index < text.size() && text[next_index] == '"') {
						buffer.push_back('"');
						cursor += 2;
						continue;
					}
					++cursor;
					break;
				}
				buffer.push_back(ch);
				++cursor;
			}
			if (cursor > text.size()) {
				throw RcParseError("Unterminated string literal on line " + std::to_string(line));
			}
			literals.push_back(buffer);
			index = cursor;
		}
		return literals;
	}

	[[nodiscard]] bool ShouldStartBlock(const std::string &line)
	{
		if (line.empty()) {
			return false;
		}
		if (line.size() >= 2 && line[0] == '/' && line[1] == '/') {
			return false;
		}
		return line.find("STRINGTABLE") != std::string::npos;
	}

	[[nodiscard]] std::map<std::string, std::string> ParseRcStringTable(const std::filesystem::path &path)
	{
		const std::vector<std::string> lines = SplitLines(ReadRcFile(path));
		std::map<std::string, std::string> entries;
		bool inside_block = false;
		bool pending_begin = false;
		std::string current_key;
		std::string current_value;
		for (size_t i = 0; i < lines.size(); ++i) {
		const std::string &raw_line = lines[i];
		std::string trimmed = raw_line;
			trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return !std::isspace(ch); }));
			trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmed.end());
			if (!inside_block) {
				if (pending_begin) {
					if (trimmed.rfind("BEGIN", 0) == 0) {
						inside_block = true;
						pending_begin = false;
					}
					continue;
				}
				if (ShouldStartBlock(trimmed)) {
					pending_begin = true;
					if (trimmed.find("BEGIN") != std::string::npos) {
						inside_block = true;
						pending_begin = false;
					}
				}
				continue;
			}
			if (trimmed.rfind("END", 0) == 0) {
				if (!current_key.empty()) {
					if (current_value.empty()) {
						throw RcParseError("Missing string for '" + current_key + "' before line " + std::to_string(i + 1));
					}
					if (entries.count(current_key) != 0u) {
						throw RcParseError("Duplicate identifier '" + current_key + "'");
					}
					entries[current_key] = current_value;
				}
				current_key.clear();
				current_value.clear();
				inside_block = false;
				continue;
			}
			if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || trimmed.rfind('#', 0) == 0) {
				continue;
			}
			if (!trimmed.empty() && trimmed[0] != '"') {
				std::string identifier;
				for (char ch : trimmed) {
					if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
						identifier.push_back(ch);
					} else {
						break;
					}
				}
				if (identifier == "BEGIN" || identifier == "END" || identifier.empty()) {
					continue;
				}
				if (!current_key.empty()) {
					if (current_value.empty()) {
						throw RcParseError("missing string for '" + current_key + "' before line " + std::to_string(i + 1));
					}
					if (entries.count(current_key) != 0u) {
						throw RcParseError("duplicate identifier '" + current_key + "'");
					}
					entries[current_key] = current_value;
				}
			current_key = identifier;
			current_value.clear();
			const std::string literal_source = trimmed.substr(identifier.length());
				const auto literals = ExtractLiterals(literal_source, static_cast<int>(i + 1));
				for (const auto &literal : literals) {
					current_value.append(literal);
				}
				continue;
			}
				if (current_key.empty()) {
					throw RcParseError("String literal before identifier on line " + std::to_string(i + 1));
			}
			const auto literals = ExtractLiterals(trimmed, static_cast<int>(i + 1));
			for (const auto &literal : literals) {
				current_value.append(literal);
			}
		}
		if (!current_key.empty()) {
			if (current_value.empty()) {
				throw RcParseError("Missing string for '" + current_key + "' at end of RC file");
			}
			if (entries.count(current_key) != 0u) {
				throw RcParseError("Duplicate identifier '" + current_key + "'");
			}
			entries[current_key] = current_value;
		}
		return entries;
	}

	void WriteTranslationsMap(const std::map<std::string, std::string> &entries, const std::filesystem::path &output, const std::string &language, bool overwrite)
	{
		if (std::filesystem::exists(output) && !overwrite) {
			throw RcParseError("Output file exists (use --overwrite)");
		}
		EnsureParentDirectory(output);
		std::ostringstream builder;
		bool first = true;
		for (const auto &pair : entries) {
			if (!first) {
				builder << "\r\n";
			}
			first = false;
			builder << pair.first << "\r\n\t" << language << "\t" << pair.second;
		}
		builder << "\r\n";
		std::ofstream stream(output, std::ios::binary | std::ios::trunc);
		if (!stream) {
			throw RcParseError("Failed to write translations map");
		}
		const std::string data = builder.str();
		stream.write(data.c_str(), static_cast<std::streamsize>(data.size()));
	}

	struct MapParseError : public std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	void AppendUtf8CodePoint(std::string &output, unsigned long code_point, int line)
	{
		if (code_point > 0x10FFFFu) {
			throw MapParseError("Unicode code point out of range (line " + std::to_string(line) + ")");
		}
		if (code_point <= 0x7Fu) {
			output.push_back(static_cast<char>(code_point));
			return;
		}
		if (code_point <= 0x7FFu) {
			output.push_back(static_cast<char>(0xC0u | ((code_point >> 6u) & 0x1Fu)));
			output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
			return;
		}
		if (code_point <= 0xFFFFu) {
			output.push_back(static_cast<char>(0xE0u | ((code_point >> 12u) & 0x0Fu)));
			output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3Fu)));
			output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
			return;
		}
		output.push_back(static_cast<char>(0xF0u | ((code_point >> 18u) & 0x07u)));
		output.push_back(static_cast<char>(0x80u | ((code_point >> 12u) & 0x3Fu)));
		output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3Fu)));
		output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
	}

	struct TranslationSegments
	{
		std::vector<std::pair<std::string, int>> segments;
	};

	struct MapEntry
	{
		std::string key;
		int line = 0;
		std::map<std::string, TranslationSegments> translations;
	};

	[[nodiscard]] std::string ReadUtf8Text(const std::filesystem::path &path)
	{
		std::string data = ReadFileBinary(path);
		if (data.size() >= 3) {
			if (static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
				data.erase(0, 3);
			}
		}
		return data;
	}

	[[nodiscard]] std::string DecodeEscapes(const std::string &value, int line)
	{
		std::string result;
		for (size_t index = 0; index < value.size();) {
			const char ch = value[index];
			if (ch != '\\') {
				result.push_back(ch);
				++index;
				continue;
			}
			++index;
			if (index >= value.size()) {
				throw MapParseError("Dangling backslash in text (line " + std::to_string(line) + ")");
			}
			const char esc = value[index++];
				switch (esc) {
			case 'n':
				result.push_back('\n');
				break;
			case 'r':
				result.push_back('\r');
				break;
			case 't':
				result.push_back('\t');
				break;
			case 'a':
				result.push_back('\a');
				break;
			case 'b':
				result.push_back('\b');
				break;
			case 'f':
				result.push_back('\f');
				break;
			case 'v':
				result.push_back('\v');
				break;
			case '\\':
				result.push_back('\\');
				break;
			case '"':
				result.push_back('"');
				break;
			case '0':
				result.push_back('\0');
				break;
			case 'x':
			{
				std::string hex_digits;
				while (index < value.size() && std::isxdigit(static_cast<unsigned char>(value[index])) != 0) {
					hex_digits.push_back(value[index]);
					++index;
				}
					if (hex_digits.empty()) {
						throw MapParseError("Incomplete hex escape in text (line " + std::to_string(line) + ")");
				}
				const unsigned long code_point = std::stoul(hex_digits, nullptr, 16);
					if (code_point > 0x10FFFFu) {
						throw MapParseError("Hex escape out of range (line " + std::to_string(line) + ")");
				}
				AppendUtf8CodePoint(result, code_point, line);
				break;
			}
			case 'u':
			{
				if (index + 4 > value.size()) {
					throw MapParseError("Incomplete unicode escape in text (line " + std::to_string(line) + ")");
				}
				const std::string digits = value.substr(index, 4);
				if (!std::all_of(digits.begin(), digits.end(), [](unsigned char d) { return std::isxdigit(d) != 0; })) {
					throw MapParseError("Incomplete unicode escape in text (line " + std::to_string(line) + ")");
				}
				index += 4;
				const unsigned long code_point = std::stoul(digits, nullptr, 16);
				AppendUtf8CodePoint(result, code_point, line);
				break;
			}
			default:
				throw MapParseError("Unsupported escape sequence '\\" + std::string(1, esc) + "' (line " + std::to_string(line) + ")");
			}
		}
		return result;
	}

	[[nodiscard]] std::vector<std::string> CollectPlaceholders(const std::string &text)
	{
		std::vector<std::string> placeholders;
		for (size_t index = 0; index < text.size();) {
			if (text[index] != '%') {
				++index;
				continue;
			}
			++index;
			if (index < text.size() && text[index] == '%') {
				++index;
				continue;
			}
			const size_t start = index - 1;
			while (index < text.size()) {
				const char ch = text[index];
				if (std::strchr("#0- +'IhlL0123456789.*", ch) == nullptr) {
					break;
				}
				++index;
			}
			if (index >= text.size()) {
				throw MapParseError("Incomplete format placeholder detected");
			}
			++index;
			placeholders.emplace_back(text.substr(start, index - start));
		}
		return placeholders;
	}

	// Placeholder span with original token and [start,end) in the source string.
	struct PlaceholderSpan { size_t start = 0; size_t end = 0; std::string token; };

	[[nodiscard]] std::vector<PlaceholderSpan> CollectPlaceholderSpans(const std::string &text)
	{
		std::vector<PlaceholderSpan> spans;
		for (size_t index = 0; index < text.size();) {
			if (text[index] != '%') { ++index; continue; }
			++index;
			if (index < text.size() && text[index] == '%') { ++index; continue; }
			const size_t start = index - 1;
			while (index < text.size()) {
				const char ch = text[index];
				if (std::strchr("#0- +'IhlL0123456789.*", ch) == nullptr) break;
				++index;
			}
			if (index >= text.size()) throw MapParseError("Incomplete format placeholder detected");
			++index; // include conversion char
			PlaceholderSpan sp; sp.start = start; sp.end = index; sp.token = text.substr(start, index - start);
			spans.emplace_back(std::move(sp));
		}
		return spans;
	}

	// Replace placeholders in 'text' at spans_target with tokens from spans_ref (order-preserving).
	[[nodiscard]] std::string ReplacePlaceholdersByReference(const std::string &text, const std::vector<PlaceholderSpan> &spans_target, const std::vector<PlaceholderSpan> &spans_ref)
	{
		if (spans_target.size() != spans_ref.size()) return text;
		std::string out; out.reserve(text.size());
		size_t cursor = 0;
		for (size_t i = 0; i < spans_target.size(); ++i) {
			const auto &t = spans_target[i];
			const auto &r = spans_ref[i];
			if (t.start > cursor) out.append(text.substr(cursor, t.start - cursor));
			out.append(r.token);
			cursor = t.end;
		}
		if (cursor < text.size()) out.append(text.substr(cursor));
		return out;
	}

	// Compare presence of common escape sequences (\\n, \\r, \\\\ and %%) between reference and target.
	[[nodiscard]] bool EscapeParityMatches(const std::string &en_raw, const std::string &other_raw)
	{
		const bool en_n = en_raw.find("\\n") != std::string::npos;
		const bool en_r = en_raw.find("\\r") != std::string::npos;
		const bool en_bs = en_raw.find("\\\\") != std::string::npos;
		const bool en_pp = en_raw.find("%%") != std::string::npos;
		const bool ot_n = other_raw.find("\\n") != std::string::npos;
		const bool ot_r = other_raw.find("\\r") != std::string::npos;
		const bool ot_bs = other_raw.find("\\\\") != std::string::npos;
		const bool ot_pp = other_raw.find("%%") != std::string::npos;
		return en_n == ot_n && en_r == ot_r && en_bs == ot_bs && en_pp == ot_pp;
	}

	[[nodiscard]] uint32_t Fnv1a(const std::string &value)
	{
		uint32_t hash = kFnvOffset;
		for (unsigned char ch : value) {
			hash ^= ch;
			hash *= kFnvPrime;
		}
		return hash;
	}

	struct ParsedMap
	{
		std::vector<MapEntry> entries;
		std::set<std::string> languages;
	};

	[[nodiscard]] ParsedMap ParseMap(const std::filesystem::path &path)
	{
		ParsedMap result;
		const std::vector<std::string> lines = SplitLines(ReadUtf8Text(path));
		MapEntry *current_entry = nullptr;
		std::string current_language;
		for (size_t i = 0; i < lines.size(); ++i) {
			const std::string &line = lines[i];
			if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
				current_language.clear();
				continue;
			}
			const std::string trimmed = line.substr(line.find_first_not_of(' '));
			if (!trimmed.empty() && trimmed[0] == '#') {
				continue;
			}
			if (!line.empty() && line[0] != '\t') {
				const std::string key = trimmed;
				if (key.empty()) {
					throw MapParseError("Empty key encountered on line " + std::to_string(i + 1));
				}
				for (const auto &entry : result.entries) {
					if (entry.key == key) {
						throw MapParseError("Duplicate key '" + key + "' (line " + std::to_string(i + 1) + ")");
					}
				}
				result.entries.push_back(MapEntry());
				current_entry = &result.entries.back();
				current_entry->key = key;
				current_entry->line = static_cast<int>(i + 1);
				current_language.clear();
				continue;
			}
			if (current_entry == nullptr) {
				throw MapParseError("Translation without key on line " + std::to_string(i + 1));
			}
			if (line.rfind("\t\t", 0) == 0) {
				if (current_language.empty()) {
					throw MapParseError("continuation line without language (line " + std::to_string(i + 1) + ")");
				}
				current_entry->translations[current_language].segments.emplace_back(line.substr(2), static_cast<int>(i + 1));
				continue;
			}
			if (line[0] != '\t') {
				throw MapParseError("Invalid indentation on line " + std::to_string(i + 1));
			}
			const size_t separator = line.find('\t', 1);
			if (separator == std::string::npos) {
				throw MapParseError("Missing tab separator on line " + std::to_string(i + 1));
			}
			std::string lang_code = line.substr(1, separator - 1);
			lang_code.erase(lang_code.begin(), std::find_if(lang_code.begin(), lang_code.end(), [](unsigned char ch) { return !std::isspace(ch); }));
			lang_code.erase(std::find_if(lang_code.rbegin(), lang_code.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), lang_code.end());
			if (lang_code.empty()) {
				throw MapParseError("Empty language code on line " + std::to_string(i + 1));
			}
			if (current_entry->translations.find(lang_code) != current_entry->translations.end()) {
				throw MapParseError("Duplicate language '" + lang_code + "' for key '" + current_entry->key + "' (line " + std::to_string(i + 1) + ")");
			}
			std::string text = line.substr(separator + 1);
			current_entry->translations[lang_code].segments.emplace_back(text, static_cast<int>(i + 1));
			current_language = lang_code;
			result.languages.insert(lang_code);
		}
		return result;
	}

	[[nodiscard]] std::vector<std::string> BuildLanguageList(const std::set<std::string> &languages)
	{
		std::vector<std::string> ordered(languages.begin(), languages.end());
		const auto en_it = std::find(ordered.begin(), ordered.end(), "en");
		if (en_it != ordered.end()) {
			ordered.erase(en_it);
		}
		ordered.insert(ordered.begin(), "en");
		return ordered;
	}

	struct FinalEntry
	{
		std::string key;
		int line = 0;
		std::map<std::string, std::string> translations;
	};

	[[nodiscard]] std::vector<FinalEntry> FinalizeEntries(const ParsedMap &parsed)
	{
		std::vector<FinalEntry> final_entries;
		final_entries.reserve(parsed.entries.size());
		for (const auto &entry : parsed.entries) {
			if (entry.translations.empty()) {
				throw MapParseError("Key '" + entry.key + "' has no translations (line " + std::to_string(entry.line) + ")");
			}
			FinalEntry final_entry;
			final_entry.key = entry.key;
			final_entry.line = entry.line;
			for (const auto &pair : entry.translations) {
				const std::string &lang = pair.first;
				const auto &segments = pair.second.segments;
				std::string collected;
				for (size_t i = 0; i < segments.size(); ++i) {
					if (i > 0) {
						collected.push_back('\n');
					}
					collected.append(segments[i].first);
				}
				if (segments.empty()) {
					throw MapParseError("Empty translation segments for key '" + entry.key + "'");
				}
				const int line = segments.front().second;
				final_entry.translations[lang] = DecodeEscapes(collected, line);
			}
			final_entries.push_back(std::move(final_entry));
		}
		return final_entries;
	}

	// Returns true if the given UTF-8 string is empty or contains only whitespace characters.
	[[nodiscard]] bool IsWhitespaceOnly(const std::string &text)
	{
		for (unsigned char ch : text) {
			if (std::isspace(ch) == 0) {
				return false;
			}
		}
		return true;
	}

	// For any missing or blank translation, copy the English text as a fallback for that language.
	void FillMissingTranslationsWithEnglish(std::vector<FinalEntry> &entries, const std::vector<std::string> &languages)
	{
		for (auto &entry : entries) {
			auto en_it = entry.translations.find("en");
			if (en_it == entry.translations.end())
				continue; // ValidateEntries will raise an error; nothing to fill.
			const std::string en_text = en_it->second;
			for (const auto &lang : languages) {
				if (lang == "en")
					continue;

				auto it = entry.translations.find(lang);
				if (it == entry.translations.end()) {
					entry.translations.emplace(lang, en_text);
					continue;
				}

				if (it->second.empty() || IsWhitespaceOnly(it->second))
					it->second = en_text;
			}
		}
	}

	void ValidateEntries(const std::vector<FinalEntry> &entries, const std::vector<std::string> &languages)
	{
		for (const auto &entry : entries) {
			const auto english_it = entry.translations.find("en");
			if (english_it == entry.translations.end()) {
				throw MapParseError("Missing 'en' translation for key '" + entry.key + "' (line " + std::to_string(entry.line) + ")");
			}
			if (english_it->second.empty()) {
				throw MapParseError("Empty 'en' translation for key '" + entry.key + "'");
			}
			const std::vector<std::string> english_placeholders = CollectPlaceholders(english_it->second);
			for (const auto &language : languages) {
				if (language == "en")
					continue;

				const auto it = entry.translations.find(language);
				if (it == entry.translations.end())
					continue; // Missing translation is allowed; fallback may apply in codegen or runtime.

				// Allow blank/whitespace-only translations without enforcing placeholder match.
				if (it->second.empty() || IsWhitespaceOnly(it->second))
					continue;

				if (CollectPlaceholders(it->second) != english_placeholders)
					throw MapParseError("Placeholder mismatch for key '" + entry.key + "' language '" + language + "'");
			}
		}
	}

	struct TranslationValue
	{
		uint16_t language = 0;
		uint32_t next = kInvalidIndex;
		std::string text;
	};

	struct TranslationBucket
	{
		uint32_t hash = 0;
		std::optional<std::string> key;
		uint32_t value = kInvalidIndex;
	};

	[[nodiscard]] uint32_t NextPowerOfTwo(uint32_t value)
	{
		if (value <= 1u) {
			return 1u;
		}
		uint32_t result = 1u;
		while (result < value) {
			result <<= 1u;
		}
		return result;
	}

	struct GeneratedTables
	{
		std::vector<TranslationValue> values;
		std::vector<TranslationBucket> buckets;
		uint32_t bucket_mask = 0;
	};

	[[nodiscard]] GeneratedTables BuildTables(const std::vector<FinalEntry> &entries, const std::vector<std::string> &languages)
	{
		std::unordered_map<std::string, uint16_t> language_index;
		for (size_t i = 0; i < languages.size(); ++i) {
			language_index.emplace(languages[i], static_cast<uint16_t>(i));
		}
		GeneratedTables tables;
		std::unordered_map<std::string, uint32_t> first_index;
		for (const auto &entry : entries) {
			uint32_t first = kInvalidIndex;
			uint32_t previous = kInvalidIndex;
			for (const auto &language : languages) {
				const auto it = entry.translations.find(language);
				if (it == entry.translations.end()) {
					continue;
				}
				TranslationValue value;
				value.language = language_index[language];
				value.next = kInvalidIndex;
				value.text = it->second;
				const uint32_t index = static_cast<uint32_t>(tables.values.size());
				tables.values.push_back(value);
				if (previous != kInvalidIndex) {
					tables.values[previous].next = index;
				} else {
					first = index;
				}
				previous = index;
			}
			first_index.emplace(entry.key, first);
		}
		const uint32_t desired_buckets = NextPowerOfTwo(static_cast<uint32_t>(entries.size() * 2));
		tables.bucket_mask = desired_buckets - 1u;
		tables.buckets.resize(desired_buckets);
		for (const auto &entry : entries) {
			const uint32_t hash_value = Fnv1a(entry.key);
			uint32_t position = hash_value & tables.bucket_mask;
			while (tables.buckets[position].key.has_value()) {
				if (tables.buckets[position].key.value() == entry.key) {
					throw MapParseError("Duplicate key '" + entry.key + "' in hash table build");
				}
				position = (position + 1u) & tables.bucket_mask;
			}
			tables.buckets[position].hash = hash_value;
			tables.buckets[position].key = entry.key;
			tables.buckets[position].value = first_index.at(entry.key);
		}
		return tables;
	}

	[[nodiscard]] std::string EscapeForCpp(const std::string &value)
	{
		std::string escaped;
		escaped.reserve(value.size() + 16);
		for (unsigned char ch : value) {
			switch (ch) {
			case '\\':
				escaped.append("\\\\");
				break;
			case '"':
				escaped.append("\\\"");
				break;
			case '\n':
				escaped.append("\\n");
				break;
			case '\r':
				escaped.append("\\r");
				break;
			case '\t':
				escaped.append("\\t");
				break;
			case '\0':
				escaped.append("\\0");
				break;
			case '\a':
				escaped.append("\\a");
				break;
			case '\b':
				escaped.append("\\b");
				break;
			case '\f':
				escaped.append("\\f");
				break;
			case '\v':
				escaped.append("\\v");
				break;
			default:
				escaped.push_back(static_cast<char>(ch));
				break;
			}
		}
		return escaped;
	}

	void WriteTranslationsHeader(const std::filesystem::path &path, const std::vector<std::string> &languages, const GeneratedTables &tables)
	{
		std::ostringstream builder;
		builder << "// Auto-generated by TranslationCompiler. Do not edit manually.\r\n";
		builder << "#pragma once\r\n\r\n";
		builder << "#include <cstdint>\r\n";
		builder << "#include <tchar.h>\r\n";
		builder << "#include \"lang_registry.gen.h\"\r\n\r\n";
		builder << "namespace Translations\r\n";
		builder << "{\r\n";
		builder << "\tstatic constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;\r\n";
		builder << "\tstatic constexpr uint32_t kBucketCount = " << tables.buckets.size() << "u;\r\n";
		builder << "\tstatic constexpr uint32_t kBucketMask = " << tables.bucket_mask << "u;\r\n";
		builder << "\tstatic constexpr uint32_t kValueCount = " << tables.values.size() << "u;\r\n\r\n";
		builder << "\tstruct TranslationValue\r\n\t{\r\n";
		builder << "\t\tuint16_t language;\r\n";
		builder << "\t\tuint32_t next;\r\n";
		builder << "\t\tLPCTSTR text;\r\n";
		builder << "\t};\r\n\r\n";
		builder << "\tstruct TranslationBucket\r\n\t{\r\n";
		builder << "\t\tuint32_t hash;\r\n";
		builder << "\t\tLPCTSTR key;\r\n";
		builder << "\t\tuint32_t value;\r\n";
		builder << "\t};\r\n\r\n";
		builder << "\tstatic const TranslationValue kValues[kValueCount] = {\r\n";
		for (size_t i = 0; i < tables.values.size(); ++i) {
			const auto &value = tables.values[i];
			builder << "\t\t{ " << value.language << "u, ";
			builder << (value.next == kInvalidIndex ? "kInvalidIndex" : std::to_string(value.next) + "u");
			builder << ", _T(\"" << EscapeForCpp(value.text) << "\") }";
			if (i + 1 != tables.values.size()) {
				builder << ",";
			}
			builder << "\r\n";
		}
		builder << "\t};\r\n\r\n";
		builder << "\tstatic const TranslationBucket kBuckets[kBucketCount] = {\r\n";
		for (size_t i = 0; i < tables.buckets.size(); ++i) {
			const auto &bucket = tables.buckets[i];
			if (!bucket.key.has_value()) {
				builder << "\t\t{ 0u, nullptr, kInvalidIndex }";
			} else {
				builder << "\t\t{ " << bucket.hash << "u, _T(\"" << bucket.key.value() << "\"), ";
				builder << (bucket.value == kInvalidIndex ? "kInvalidIndex" : std::to_string(bucket.value) + "u");
				builder << " }";
			}
			if (i + 1 != tables.buckets.size()) {
				builder << ",";
			}
			builder << "\r\n";
		}
		builder << "\t};\r\n";
		builder << "}\r\n\r\n";
		EnsureParentDirectory(path);
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream) {
			throw MapParseError("Failed to write translations header");
		}
		// Emit UTF-8 BOM so MSVC treats this header as UTF-8 source.
		static const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };
		stream.write(reinterpret_cast<const char*>(kBom), 3);
		const std::string data = builder.str();
		stream.write(data.c_str(), static_cast<std::streamsize>(data.size()));
	}

	void WriteLanguageRegistry(const std::filesystem::path &path, const std::vector<std::string> &languages)
	{
		std::ostringstream builder;
		builder << "// Auto-generated by TranslationCompiler. Do not edit manually.\r\n";
		builder << "#pragma once\r\n\r\n";
		builder << "#include <cstdint>\r\n";
		builder << "#include <tchar.h>\r\n\r\n";
		builder << "namespace Translations\r\n";
		builder << "{\r\n";
		builder << "\tstruct LanguageRecord\r\n\t{\r\n";
		builder << "\t\tLPCTSTR code;\r\n";
		builder << "\t\tuint16_t fallback;\r\n";
		builder << "\t};\r\n\r\n";
		builder << "\tstatic constexpr uint16_t kLanguageCount = " << languages.size() << "u;\r\n";
		builder << "\tstatic constexpr uint16_t kDefaultLanguage = 0u;\r\n\r\n";
		builder << "\tstatic const LanguageRecord kLanguages[kLanguageCount] = {\r\n";
		const std::vector<uint16_t> fb = BuildFallbackIndexes(languages);
		for (size_t i = 0; i < languages.size(); ++i) {
			builder << "\t\t{ _T(\"" << languages[i] << "\"), " << fb[i] << "u }";
			if (i + 1 != languages.size()) builder << ",";
			builder << "\r\n";
		}
		builder << "\t};\r\n";
		builder << "}\r\n\r\n";
		EnsureParentDirectory(path);
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream) {
			throw MapParseError("Failed to write language registry header");
		}
		static const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };
		stream.write(reinterpret_cast<const char*>(kBom), 3);
		const std::string data = builder.str();
		stream.write(data.c_str(), static_cast<std::streamsize>(data.size()));
	}


	void RunMapToCxx(const std::filesystem::path &map_file, const std::filesystem::path &data_out, const std::filesystem::path &registry_out)
	{
		const ParsedMap parsed = ParseMap(map_file);
		std::vector<std::string> languages = BuildLanguageList(parsed.languages);
		std::vector<FinalEntry> entries = FinalizeEntries(parsed);
		// Fill empty or missing translations with English fallback before validation.
		FillMissingTranslationsWithEnglish(entries, languages);
		ValidateEntries(entries, languages);
		const GeneratedTables tables = BuildTables(entries, languages);
		WriteLanguageRegistry(registry_out, languages);
		WriteTranslationsHeader(data_out, languages, tables);
	}

	struct RcOptions
	{
		std::filesystem::path rc_file;
		std::filesystem::path map_file;
		std::string language = "en";
		bool overwrite = false;
	};

	struct MapOptions
	{
		std::filesystem::path map_file;
		std::filesystem::path data_out;
		std::filesystem::path registry_out;
		// ids_out removed in string-key migration
	};

	void RunRcToMap(const RcOptions &options)
	{
		const std::map<std::string, std::string> entries = ParseRcStringTable(options.rc_file);
		if (entries.empty()) {
			throw RcParseError("No entries found in RC file");
		}
		WriteTranslationsMap(entries, options.map_file, options.language, options.overwrite);
	}

	[[nodiscard]] std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return text;
	}

	[[nodiscard]] std::string Narrow(const std::wstring &value)
	{
		return WideToUtf8(value);
	}

	[[nodiscard]] RcOptions ParseRcArguments(const std::vector<std::wstring> &args)
	{
		if (args.size() < 3) {
			throw std::runtime_error("Usage: TranslationCompiler rc2map <rc_file> <map_file> [--lang <code>] [--overwrite]");
		}
		RcOptions options;
		options.rc_file = args[1];
		options.map_file = args[2];
		for (size_t i = 3; i < args.size(); ++i) {
			const std::string token = ToLower(Narrow(args[i]));
			if (token == "--lang") {
				if (i + 1 >= args.size()) {
					throw std::runtime_error("Missing value for --lang");
				}
				options.language = Narrow(args[++i]);
				continue;
			}
			if (token == "--overwrite") {
				options.overwrite = true;
				continue;
			}
			throw std::runtime_error("Unknown argument: " + token);
		}
		return options;
	}

	[[nodiscard]] MapOptions ParseMapArguments(const std::vector<std::wstring> &args)
	{
		MapOptions options;
		if (args.size() < 2) {
			throw std::runtime_error("Usage: TranslationCompiler map2cpp <map_file> --data-out <path> --registry-out <path>");
		}
		options.map_file = args[1];
		for (size_t i = 2; i < args.size(); ++i) {
			const std::string token = ToLower(Narrow(args[i]));
			if (token == "--data-out") {
				if (i + 1 >= args.size()) {
					throw std::runtime_error("Missing value for --data-out");
				}
				options.data_out = args[++i];
				continue;
			}
			if (token == "--registry-out") {
				if (i + 1 >= args.size()) {
					throw std::runtime_error("Missing value for --registry-out");
				}
				options.registry_out = args[++i];
				continue;
			}
			throw std::runtime_error("Unknown argument: " + token);
		}
		if (options.data_out.empty() || options.registry_out.empty()) {
			throw std::runtime_error("Missing output arguments for map2cpp");
		}
		return options;
	}
}
 
namespace
{
static std::vector<FinalEntry> SortEntriesByKey(std::vector<FinalEntry> entries)
{
	std::sort(entries.begin(), entries.end(), [](const FinalEntry &a, const FinalEntry &b) { return a.key < b.key; });
	return entries;
}

static void WriteMapFile(const std::filesystem::path &path, const std::vector<FinalEntry> &entries, const std::vector<std::string> &languages)
{
	EnsureParentDirectory(path);
	std::ostringstream builder;
	bool first_key = true;
	for (const auto &entry : entries) {
		if (!first_key) builder << "\r\n";
		first_key = false;
		builder << entry.key << "\r\n";
		for (const auto &lang : languages) {
			auto it = entry.translations.find(lang);
			if (it == entry.translations.end()) continue;
			builder << "\t" << lang << "\t";
			// Encode newlines as continuation lines for readability
			const std::string &text = it->second;
			size_t start = 0;
			size_t pos = text.find('\n');
			if (pos == std::string::npos) {
				builder << text << "\r\n";
			} else {
				builder << text.substr(0, pos) << "\r\n";
				start = pos + 1;
				while ((pos = text.find('\n', start)) != std::string::npos) {
					builder << "\t\t" << text.substr(start, pos - start) << "\r\n";
					start = pos + 1;
				}
				if (start < text.size()) builder << "\t\t" << text.substr(start) << "\r\n";
			}
		}
	}
	builder << "\r\n";
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) throw std::runtime_error("Failed to write sorted translations map");
	const std::string data = builder.str();
	out.write(data.c_str(), static_cast<std::streamsize>(data.size()));
}

// Writes back the map using the raw segments as parsed, preserving escape sequences (e.g., \n, \r\n)
// and original continuation lines without decoding to real newlines. Used by 'sort' mode to avoid
// altering formatting.
static void WriteMapFileRaw(const std::filesystem::path &path, const std::vector<MapEntry> &entries, const std::vector<std::string> &languages)
{
	EnsureParentDirectory(path);
	std::ostringstream builder;
	bool first_key = true;
	for (const auto &entry : entries) {
		if (!first_key) builder << "\r\n";
		first_key = false;
		builder << entry.key << "\r\n";
		for (const auto &lang : languages) {
			auto it = entry.translations.find(lang);
			if (it == entry.translations.end()) continue;
			const auto &segs = it->second.segments;
			if (segs.empty()) continue;
			builder << "\t" << lang << "\t" << segs.front().first << "\r\n";
			for (size_t i = 1; i < segs.size(); ++i) {
				builder << "\t\t" << segs[i].first << "\r\n";
			}
		}
	}
	builder << "\r\n";
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) throw std::runtime_error("Failed to write sorted translations map (raw)");
	const std::string data = builder.str();
	out.write(data.c_str(), static_cast<std::streamsize>(data.size()));
}

static std::vector<uint16_t> BuildFallbackIndexes(const std::vector<std::string> &languages)
{
	// For codes like "pt-BR" fallback to "pt" if present, else to index 0 (en). For base codes fallback to en (0).
	std::vector<uint16_t> fallback(languages.size(), 0);
	for (size_t i = 0; i < languages.size(); ++i) {
		const std::string &code = languages[i];
		size_t dash = code.find('-');
		if (dash == std::string::npos) {
			fallback[i] = 0u; // base code → en
			continue;
		}
		std::string base = code.substr(0, dash);
		uint16_t fbi = 0u; // default to en
		for (size_t j = 0; j < languages.size(); ++j) {
			if (languages[j] == base) { fbi = static_cast<uint16_t>(j); break; }
		}
		if (fbi == i) fbi = 0u; // avoid self-loop
		fallback[i] = fbi;
	}
	return fallback;
}

}

// Join raw segments with \n for analysis (do not decode escapes).
static std::string JoinSegmentsRaw(const TranslationSegments &segs)
{
	std::string out;
	for (size_t i = 0; i < segs.segments.size(); ++i) {
		if (i)
			out.push_back('\n');
		out.append(segs.segments[i].first);
	}
	return out;
}

// Return true if there exists at least one non-empty raw segment.
static bool HasAnyContent(const TranslationSegments &segs)
{
	for (const auto &p : segs.segments) {
		if (!p.first.empty()) return true;
	}
	return false;
}

// Ensure all languages exist in entry; add blank line for missing ones.
static void EnsureAllLanguagesPresent(MapEntry &entry, const std::vector<std::string> &languages)
{
	for (const auto &lang : languages) {
		if (entry.translations.find(lang) == entry.translations.end()) {
			TranslationSegments blank;
			blank.segments.emplace_back(std::string(), entry.line);
			entry.translations.emplace(lang, std::move(blank));
		}
 	}
}

// Clear a translation but keep a blank "\tLANG\t" line.
static void ClearTranslationKeepBlank(MapEntry &entry, const std::string &lang)
{
	auto it = entry.translations.find(lang);
	if (it == entry.translations.end()) {
 		TranslationSegments blank;
 		blank.segments.emplace_back(std::string(), entry.line);
 		entry.translations.emplace(lang, std::move(blank));
 		return;
 	}
 	it->second.segments.clear();
 	it->second.segments.emplace_back(std::string(), entry.line);
}

// Overwrite translation raw text (joined by '\n') by splitting into segments.
static void SetRawForLanguage(MapEntry &entry, const std::string &lang, const std::string &raw)
{
	auto &ts = entry.translations[lang];
	int baseLine = entry.line;
	if (!ts.segments.empty()) baseLine = ts.segments.front().second;
	ts.segments.clear();
	// Split by '\n' without keeping empty trailing part.
	size_t start = 0; size_t pos = raw.find('\n');
	if (pos == std::string::npos) {
		ts.segments.emplace_back(raw, baseLine);
		return;
	}
	ts.segments.emplace_back(raw.substr(0, pos), baseLine);
	start = pos + 1;
	while ((pos = raw.find('\n', start)) != std::string::npos) {
		std::string part = raw.substr(start, pos - start);
		ts.segments.emplace_back(std::move(part), baseLine);
		start = pos + 1;
	}
	if (start < raw.size()) {
		ts.segments.emplace_back(raw.substr(start), baseLine);
	}
}

// Count UTF-8 code points in decoded text.
static size_t Utf8CodePointCount(const std::string &text)
{
	size_t count = 0;
	for (unsigned char b : text) {
		if ((b & 0xC0u) != 0x80u) ++count; // count non-continuation bytes
	}
	return count;
}

// Writer for fix mode: always emit all languages, preserving raw text and blanks.
static void WriteMapFileRawAllLanguages(const std::filesystem::path &path, const std::vector<MapEntry> &entries, const std::vector<std::string> &languages)
{
 	EnsureParentDirectory(path);
 	std::ostringstream builder;
 	bool first_key = true;
 	for (const auto &entry : entries) {
 		if (!first_key) builder << "\r\n";
 		first_key = false;
 		builder << entry.key << "\r\n";
 		for (const auto &lang : languages) {
 			auto it = entry.translations.find(lang);
 			builder << "\t" << lang << "\t";
 			if (it == entry.translations.end() || it->second.segments.empty() || (it->second.segments.size() == 1 && it->second.segments[0].first.empty())) {
 				builder << "\r\n";
 				continue;
 			}
 			const auto &segs = it->second.segments;
 			builder << segs.front().first << "\r\n";
 			for (size_t i = 1; i < segs.size(); ++i) builder << "\t\t" << segs[i].first << "\r\n";
 		}
 	}
 	builder << "\r\n";
 	std::ofstream out(path, std::ios::binary | std::ios::trunc);
 	if (!out) throw std::runtime_error("Failed to write fixed translations map");
 	const std::string data = builder.str();
 	out.write(data.c_str(), static_cast<std::streamsize>(data.size()));
}

// Implements the 'fix' operation.
static void RunFixOnMap(const std::filesystem::path &map_file)
{
 	ParsedMap parsed = ParseMap(map_file);
 	std::vector<std::string> languages = BuildLanguageList(parsed.languages);
	// Print languages list
	{
		std::ostringstream os;
		os << "languages:";
		for (size_t i = 0; i < languages.size(); ++i) {
			os << (i == 0 ? " " : ", ") << languages[i];
		}
		std::cout << os.str() << std::endl;
	}
 	// Step 1 is sorting by key; we will perform sort at the end after modifications.

 	for (auto &entry : parsed.entries) {
 		// Step 2: Escape sequence parity against English (raw: \\n, \\r, \\\\, %%)
 		auto en_it = entry.translations.find("en");
 		if (en_it != entry.translations.end()) {
 			const std::string en_raw = JoinSegmentsRaw(en_it->second);
 			const bool req_n = en_raw.find("\\n") != std::string::npos;
 			const bool req_r = en_raw.find("\\r") != std::string::npos;
 			const bool req_bs = en_raw.find("\\\\") != std::string::npos;
 			const bool req_pp = en_raw.find("%%") != std::string::npos;
 			if (req_n || req_r || req_bs || req_pp) {
 				for (const auto &lang : languages) {
 					if (lang == "en") continue;
 					auto it = entry.translations.find(lang);
 					if (it == entry.translations.end()) continue; // no line => handled in step 3
 					if (!HasAnyContent(it->second)) continue; // empty translation: skip
 					const std::string t = JoinSegmentsRaw(it->second);
 					bool ok = true;
 					if (req_n && t.find("\\n") == std::string::npos) ok = false;
 					if (req_r && t.find("\\r") == std::string::npos) ok = false;
 					if (req_bs && t.find("\\\\") == std::string::npos) ok = false;
 					if (req_pp && t.find("%%") == std::string::npos) ok = false;
 					if (!ok) {
 						ClearTranslationKeepBlank(entry, lang);
 					}
 				}
 			}
			// Step 2b: Placeholder synchronization vs English
			const auto en_spans = CollectPlaceholderSpans(en_raw);
			for (const auto &lang : languages) {
				if (lang == "en") continue;
				auto it = entry.translations.find(lang);
				if (it == entry.translations.end()) continue;
				if (!HasAnyContent(it->second)) continue;
				const std::string t_raw = JoinSegmentsRaw(it->second);
				const auto t_spans = CollectPlaceholderSpans(t_raw);
				if (t_spans.size() != en_spans.size()) {
					ClearTranslationKeepBlank(entry, lang);
					continue;
				}
				bool identical = true;
				for (size_t i = 0; i < t_spans.size(); ++i) { if (t_spans[i].token != en_spans[i].token) { identical = false; break; } }
				if (!identical) {
					std::string replaced = ReplacePlaceholdersByReference(t_raw, t_spans, en_spans);
					SetRawForLanguage(entry, lang, replaced);
				}
			}
 		}
 		// Step 3: Ensure all languages are present with at least a blank line.
 		EnsureAllLanguagesPresent(entry, languages);
 	}

	// Step 4: Character count sanity (decoded text). Perform after step 2&3.
 	for (auto &entry : parsed.entries) {
 		auto en_it = entry.translations.find("en");
 		if (en_it == entry.translations.end()) continue;
 		const std::string en_raw = JoinSegmentsRaw(en_it->second);
 		const int en_line = en_it->second.segments.empty() ? entry.line : en_it->second.segments.front().second;
 		std::string en_decoded;
 		try { en_decoded = DecodeEscapes(en_raw, en_line); } catch (...) { en_decoded = en_raw; }
		const size_t en_chars = Utf8CodePointCount(en_decoded);
		if (en_chars == 0) continue;
 		for (const auto &lang : languages) {
 			if (lang == "en") continue;
 			auto it = entry.translations.find(lang);
 			if (it == entry.translations.end()) continue;
 			if (!HasAnyContent(it->second)) continue; // blank stays blank
 			const std::string raw = JoinSegmentsRaw(it->second);
 			const int lno = it->second.segments.empty() ? entry.line : it->second.segments.front().second;
 			std::string dec;
 			try { dec = DecodeEscapes(raw, lno); } catch (...) { dec = raw; }
			const size_t c = Utf8CodePointCount(dec);
			if (c > en_chars * 10 || c * 10 < en_chars) {
 				ClearTranslationKeepBlank(entry, lang);
 			}
 		}
 	}

 	// Finally, sort keys and write back preserving raw text and blanks for all languages.
 	std::sort(parsed.entries.begin(), parsed.entries.end(), [](const MapEntry &a, const MapEntry &b){ return a.key < b.key; });
 	WriteMapFileRawAllLanguages(map_file, parsed.entries, languages);
}

int wmain(int argc, wchar_t *argv[])
{
	try {
		std::vector<std::wstring> raw_args(argv, argv + argc);
		// Helper lambdas for help text
		auto isHelp = [](const std::string &s){ return s == "--help" || s == "-h" || s == "help"; };
		auto printGlobalHelp = [](){
			std::cout
				<< "TranslationCompiler - Translations map toolkit\n\n"
				<< "USAGE:\n"
				<< "  TranslationCompiler help\n"
				<< "  TranslationCompiler rc2map <rc_file> <map_file> [--lang <code>] [--overwrite]\n"
				<< "  TranslationCompiler map2cpp <map_file> --data-out <header> --registry-out <header>\n"
				<< "  TranslationCompiler check <map_file>\n"
				<< "  TranslationCompiler fix <map_file>\n"
				<< "  TranslationCompiler add <map_file> --key <KEY> --lang <code> [--text <text>] [--backup] [--create]\n"
				<< "  TranslationCompiler remove <map_file> --key <KEY> [--backup]\n"
				<< "  TranslationCompiler clear_others <map_file> --key <KEY> [--backup]\n"
				<< "  TranslationCompiler sort <map_file>  (alias of 'fix')\n\n"
				<< "MODES AND OPTIONS:\n"
				<< "  rc2map: Extract STRINGTABLE from a .rc into translations.map.\n"
				<< "    --lang <code>     Language code to assign for extracted strings (default: en).\n"
				<< "    --overwrite       Overwrite output file if it exists.\n\n"
				<< "  map2cpp: Generate C++ headers from translations.map.\n"
				<< "    --data-out <h>    Output header with translation tables (e.g., translations.gen.h).\n"
				<< "    --registry-out <h> Output header with languages registry (lang_registry.gen.h).\n\n"
				<< "  check: Validate placeholders and the presence of 'en' translations.\n\n"
					<< "  fix: Normalize the map: ensure all languages exist per key (blank if missing),\n"
					<< "       enforce escape parity vs 'en' (\\n, \\r, \\\\ and %%), synchronize printf-style placeholders\n"
					<< "       to the 'en' reference when counts match, remove the translation if counts mismatch, then sort keys.\n\n"
				<< "  add: Add or update a translation line for an existing key.\n"
				<< "    --key <KEY>       Key name.\n"
				<< "    --lang <code>     Language code (not 'en'); pattern: [A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*.\n"
				<< "    --text <text>     Text to set. If omitted, the text is read from stdin.\n"
				<< "    --backup          Create a .bak copy before modifying the file.\n"
				<< "    --create          Create the key if it does not exist. Without this flag, a missing key is an error.\n\n"
				<< "  remove: Remove a key (entire block) from translations.map.\n"
				<< "    --key <KEY>       Key name.\n"
				<< "    --backup          Create a .bak copy before modifying the file.\n\n"
				<< "  clear_others: Clear all translations except 'en' for a specific key.\n"
				<< "    --key <KEY>       Key name.\n"
				<< "    --backup          Create a .bak copy before modifying the file.\n\n"
				<< "NOTES:\n"
				<< "  - Text must use C-style escapes in the map. For command line, escape quotes and backslashes, e.g.:\n"
				<< "      --text \"abc\\n \\\"def\\\" \\ %% %i %u\"\n"
				<< "  - 'en' translations are the canonical reference; placeholders must match across languages.\n"
				<< "  - 'sort' command is deprecated and behaves like 'fix'.\n" << std::endl;
		};
		if (raw_args.size() < 2) { printGlobalHelp(); return 1; }
	std::vector<std::wstring> args(raw_args.begin() + 1, raw_args.end());
		const std::string operation = ToLower(Narrow(args[0]));
		if (isHelp(operation)) { printGlobalHelp(); return 0; }
	if (operation == "rc2map") {
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler rc2map <rc_file> <map_file> [--lang <code>] [--overwrite]\n\n"
						<< "DESCRIPTION:\n"
						<< "  Extracts a STRINGTABLE block from a Windows .rc file and writes a translations.map file.\n\n"
						<< "OPTIONS:\n"
						<< "  --lang <code>     Language code to assign to extracted strings (default: en).\n"
						<< "  --overwrite       Overwrite the map file if it already exists.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler rc2map app.rc translations.map --lang en --overwrite\n\n"
						<< std::endl;
					return 0;
				}
			}
		RcOptions options = ParseRcArguments(args);
		RunRcToMap(options);
		return 0;
	}
	if (operation == "map2cpp") {
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler map2cpp <map_file> --data-out <header> --registry-out <header>\n\n"
						<< "DESCRIPTION:\n"
						<< "  Compiles translations.map into two C++ headers: the string tables and the language registry.\n\n"
						<< "OPTIONS:\n"
						<< "  --data-out <h>     Output header with translation tables (e.g., translations.gen.h).\n"
						<< "  --registry-out <h> Output header with languages registry (e.g., lang_registry.gen.h).\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler map2cpp srchybrid/translations/translations.map --data-out srchybrid/lang/translations.gen.h --registry-out srchybrid/lang/lang_registry.gen.h\n\n"
						<< std::endl;
					return 0;
				}
			}
		MapOptions options = ParseMapArguments(args);
		// Before codegen, compute fallbacks inside WriteLanguageRegistry by extending it to use regional fallback.
		RunMapToCxx(options.map_file, options.data_out, options.registry_out);
		return 0;
	}
	if (operation == "add") {
		// Add or update a translation line for a key/lang.
		// Usage: TranslationCompiler add <map_file> --key <KEY> --lang <code> [--text <text>] [--backup]
		// If --text is omitted, read from stdin.
			if (args.size() >= 2) {
				std::string t1 = ToLower(Narrow(args[1]));
				if (t1 == "--help" || t1 == "-h" || t1 == "help") {
					std::cout
						<< "USAGE: TranslationCompiler add <map_file> --key <KEY> --lang <code> [--text <text>] [--backup] [--create]\n\n"
						<< "DESCRIPTION:\n"
						<< "  Adds or updates a translation value for the given key and language.\n"
						<< "  If the key does not exist, it will only be created when --create is provided.\n\n"
						<< "OPTIONS:\n"
						<< "  --key, -k    Key to modify.\n"
						<< "  --lang, -l   Language code (cannot be 'en').\n"
						<< "  --text, -t   Text to set. Omit to read from stdin.\n"
						<< "  --backup     Create a backup .bak before saving.\n"
						<< "  --create     Create key block if it does not exist.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler add translations.map --key APP_TITLE --lang tr --text \"Merhaba\"\n"
						<< "  TranslationCompiler add translations.map --key NEW_KEY --lang de --text \"Hallo\" --create\n"
						<< std::endl;
					return 0;
				}
			}
			auto toLower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; };
			if (args.size() < 2) { std::cerr << "USAGE: TranslationCompiler add <map_file> --key <KEY> --lang <code> [--text <text>] [--backup] [--create]" << std::endl; return 1; }
		std::filesystem::path map_path = args[1];
		std::string key;
		std::string lang;
		std::optional<std::string> text_opt;
		bool backup = false;
			bool create_if_missing = false;
		for (size_t i = 2; i < args.size(); ++i) {
			const std::string tok = toLower(Narrow(args[i]));
			if (tok == "--key" || tok == "-k") {
				if (i + 1 >= args.size()) throw std::runtime_error("Missing value for --key");
				key = Narrow(args[++i]);
				continue;
			}
			if (tok == "--lang" || tok == "-l") {
				if (i + 1 >= args.size()) throw std::runtime_error("Missing value for --lang");
				lang = Narrow(args[++i]);
				continue;
			}
			if (tok == "--text" || tok == "-t") {
				if (i + 1 >= args.size()) throw std::runtime_error("Missing value for --text");
				text_opt = Narrow(args[++i]);
				continue;
			}
				if (tok == "--backup") { backup = true; continue; }
				if (tok == "--create" || tok == "-c") { create_if_missing = true; continue; }
			throw std::runtime_error("Unknown argument: " + tok);
		}
		if (key.empty()) throw std::runtime_error("--key is required");
		if (lang.empty()) throw std::runtime_error("--lang is required");
		// Read text from stdin if not provided
		std::string text;
		if (text_opt.has_value()) {
			text = *text_opt;
		} else {
			std::ostringstream ss; ss << std::cin.rdbuf(); text = ss.str();
		}
		// Validate lang code against pattern [A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*
		auto isAlphaNum = [](char c){ return std::isalnum(static_cast<unsigned char>(c)) != 0; };
		auto validateLang = [&](const std::string &code)->bool{
			if (code.size() < 2) return false;
			// Read first token 2-3 letters
			size_t i = 0; size_t letters = 0;
			while (i < code.size() && std::isalpha(static_cast<unsigned char>(code[i])) != 0 && letters < 3) { ++i; ++letters; }
			if (letters < 2 || letters > 3) return false;
			while (i < code.size()) {
				if (code[i] != '-') return false; ++i;
				size_t seg = 0;
				while (i < code.size() && isAlphaNum(code[i]) && seg < 8) { ++i; ++seg; }
				if (seg < 2) return false;
			}
			return true;
		};
		if (!validateLang(lang)) { std::cerr << "ERROR: Invalid language code: " << lang << std::endl; return 2; }

		// Read file bytes
		auto readBytes = [](const std::filesystem::path &p)->std::string{
			std::ifstream in(p, std::ios::binary); if (!in) throw std::runtime_error("file not found: " + p.string());
			std::string b; in.seekg(0, std::ios::end); auto sz = in.tellg(); if (sz > 0) { b.resize(static_cast<size_t>(sz)); in.seekg(0); in.read(&b[0], static_cast<std::streamsize>(b.size())); }
			return b;
		};
		std::string raw = readBytes(map_path);
		bool has_bom = false;
		if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
			has_bom = true;
			raw.erase(0, 3);
		}
		// Split lines preserving EOL
		auto splitKeepEol = [](const std::string &s){
			std::vector<std::string> out; out.reserve(1024);
			size_t i = 0, n = s.size();
			while (i < n) {
				size_t start = i;
				while (i < n && s[i] != '\r' && s[i] != '\n') ++i;
				if (i >= n) { out.emplace_back(s.substr(start)); break; }
				// capture EOL
				if (s[i] == '\r' && i + 1 < n && s[i+1] == '\n') { i += 2; out.emplace_back(s.substr(start, i - start)); }
				else { ++i; out.emplace_back(s.substr(start, i - start)); }
			}
			if (n == 0) out.emplace_back(std::string());
			return out;
		};
		std::vector<std::string> lines = splitKeepEol(raw);
		// Helper: strip only CR/LF
		auto stripEol = [](const std::string &line){ size_t a = 0, b = line.size(); while (b>0 && (line[b-1]=='\r' || line[b-1]=='\n')) --b; return line.substr(0, b); };
		auto lower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; };
		// Find key line index
		int key_idx = -1;
		for (size_t i = 0; i < lines.size(); ++i) { if (stripEol(lines[i]) == key) { key_idx = static_cast<int>(i); break; } }
		// Helper to collect languages present in the map with original casing; includes requested lang; returns list with 'en' first.
		auto collectLangs = [&](const std::vector<std::string> &ls){
			std::vector<std::string> order_original; // original-cased codes in discovery order
			std::unordered_map<std::string, std::string> lc_to_original; // lowercase -> original casing
			// Collect existing langs preserving discovery order and original casing
			for (const auto &ln : ls) {
				if (ln.empty() || ln[0] != '\t') continue;
				size_t i = 1, n = ln.size();
				size_t start = i, letters = 0;
				while (i < n && std::isalpha(static_cast<unsigned char>(ln[i])) != 0 && letters < 3) { ++i; ++letters; }
				if (letters < 2 || letters > 3) continue;
				while (i < n && ln[i] == '-') { ++i; size_t seg = 0; while (i < n && (std::isalnum(static_cast<unsigned char>(ln[i])) != 0) && seg < 8) { ++i; ++seg; } if (seg < 2) { i = n; break; } }
				if (i >= n || ln[i] != '\t') continue;
				std::string found = ln.substr(start, i - start);
				std::string low = lower(found);
				if (lc_to_original.find(low) == lc_to_original.end()) { lc_to_original.emplace(low, found); order_original.push_back(found); }
			}
			// Ensure 'en' is present; prefer existing casing, else use "en"
			if (lc_to_original.find("en") == lc_to_original.end()) { lc_to_original.emplace("en", std::string("en")); order_original.insert(order_original.begin(), std::string("en")); }
			// Ensure requested lang present with original casing as given by user if not present
			std::string lowreq = lower(lang);
			if (lc_to_original.find(lowreq) == lc_to_original.end()) { lc_to_original.emplace(lowreq, lang); order_original.push_back(lang); }
			// Build final list: 'en' first (original casing), then others sorted by lowercase code
			std::vector<std::string> others;
			for (const auto &code : order_original) { if (lower(code) != "en") others.push_back(code); }
			std::sort(others.begin(), others.end(), [&](const std::string &a, const std::string &b){ return lower(a) < lower(b); });
			std::vector<std::string> final_list; final_list.push_back(lc_to_original["en"]); for (const auto &c : others) final_list.push_back(c);
			return final_list;
		};
		if (key_idx < 0) {
			if (!create_if_missing) {
				std::cerr << "ERROR: Key not found: " << key << ". Use --create to add this key." << std::endl;
				return 1;
			}
			// Create a new key block at the end with all language lines; set provided lang text; others blank.
			const std::vector<std::string> lang_list = collectLangs(lines);
			// Detect EOL from last line, default to CRLF
			std::string eol = "\r\n";
			if (!lines.empty()) {
				const std::string &prev = lines.back();
				if (!prev.empty()) {
					char last = prev.back();
					if (last == '\n') { if (prev.size() >= 2 && prev[prev.size()-2] == '\r') eol = "\r\n"; else eol = "\n"; }
					else if (last == '\r') eol = "\r";
				}
			}
			// Optional backup
			if (backup) {
				std::filesystem::path bak = map_path; bak += L".bak";
				if (!std::filesystem::exists(bak)) {
					std::filesystem::copy_file(map_path, bak, std::filesystem::copy_options::overwrite_existing);
				} else {
					for (int idx = 1;; ++idx) { std::filesystem::path cand = map_path; cand += (L".bak." + std::to_wstring(idx)); if (!std::filesystem::exists(cand)) { std::filesystem::copy_file(map_path, cand); break; } }
				}
			}
			// Ensure leading blank separator before the new key
			if (!lines.empty() && !stripEol(lines.back()).empty()) {
				lines.emplace_back(eol);
			}
			// Append key and language lines
			lines.emplace_back(key + eol);
			for (const auto &lc : lang_list) {
				std::string val = (lower(lc) == lower(lang)) ? text : std::string();
				std::string line; line.reserve(2 + lc.size() + val.size());
				line.push_back('\t'); line.append(lc); line.push_back('\t'); line.append(val); line.append(eol);
				lines.emplace_back(std::move(line));
			}
			// Join and write back
			auto joinAll = [&](const std::vector<std::string> &ls){ std::string s; size_t total = 0; for (auto &x: ls) total += x.size(); s.reserve(total); for (auto &x: ls) s.append(x); return s; };
			std::string out = joinAll(lines);
			std::filesystem::path tmp = map_path; tmp += L".tmp";
			{
				std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
				if (!o) throw std::runtime_error("Failed to write temp file");
				if (has_bom) { const unsigned char bom[3] = {0xEF,0xBB,0xBF}; o.write(reinterpret_cast<const char*>(bom), 3); }
				o.write(out.data(), static_cast<std::streamsize>(out.size()));
				o.flush();
			}
			std::error_code re3; std::filesystem::rename(tmp, map_path, re3); if (re3) { std::filesystem::remove(map_path); std::filesystem::rename(tmp, map_path); }
			std::cout << "OK: Key \"" << key << "\" created with language \"" << lang << "\"." << std::endl;
			return 0;
		}
		// Match a language line: ^\t([A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*)\t
		auto matchLangLine = [&](const std::string &line, std::string *outLang, size_t *outPrefixEnd)->bool{
			if (line.empty() || line[0] != '\t') return false;
			size_t i = 1; size_t n = line.size();
			size_t start = i; size_t letters = 0;
			while (i < n && std::isalpha(static_cast<unsigned char>(line[i])) != 0 && letters < 3) { ++i; ++letters; }
			if (letters < 2 || letters > 3) return false;
			while (i < n && line[i] == '-') {
				++i; size_t seg = 0;
				while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) != 0) && seg < 8) { ++i; ++seg; }
				if (seg < 2) return false;
			}
			if (i >= n || line[i] != '\t') return false;
			if (outLang) *outLang = line.substr(start, i - start);
			if (outPrefixEnd) *outPrefixEnd = i + 1; // after second TAB
			return true;
		};
		// Find language line inside block
		auto findLangIdx = [&](int startIdx, const std::string &langCode)->int{
			std::string target = lower(langCode);
			for (int i = startIdx + 1; i < static_cast<int>(lines.size()); ++i) {
				const std::string &ln = lines[static_cast<size_t>(i)];
				if (!(ln.size() && ln[0] == '\t') && stripEol(ln) != std::string()) break;
				std::string found; size_t pref = 0; if (matchLangLine(ln, &found, &pref)) { if (lower(found) == target) return i; }
			}
			return -1;
		};
		// Extract raw value (with continuation lines) for a language line index
		auto getLangRawText = [&](int langLineIdx)->std::string{
			if (langLineIdx < 0 || langLineIdx >= static_cast<int>(lines.size())) return std::string();
			const std::string &first = lines[static_cast<size_t>(langLineIdx)];
			std::string dummy; size_t prefixEnd = 0; if (!matchLangLine(first, &dummy, &prefixEnd)) return std::string();
			std::string raw = stripEol(first).substr(prefixEnd);
			for (int i = langLineIdx + 1; i < static_cast<int>(lines.size()); ++i) {
				const std::string &ln = lines[static_cast<size_t>(i)];
				if (ln.size() >= 2 && ln[0] == '\t' && ln[1] == '\t') {
					if (!raw.empty()) raw.push_back('\n');
					raw.append(stripEol(ln).substr(2));
					continue;
				}
				break;
			}
			return raw;
		};
		// Find insertion point in alphabetical order
		auto findInsertion = [&](int startIdx, const std::string &langCode)->int{
			std::string target = lower(langCode);
			for (int i = startIdx + 1; i < static_cast<int>(lines.size()); ++i) {
				const std::string &ln = lines[static_cast<size_t>(i)];
				if (!(ln.size() && ln[0] == '\t') && stripEol(ln) != std::string()) return i;
				std::string cur; size_t pref = 0; if (matchLangLine(ln, &cur, &pref)) { if (lower(cur) > target) return i; }
				else { return i; }
			}
			return static_cast<int>(lines.size());
		};
		int lang_idx = findLangIdx(key_idx, lang);
		// Prepare English line index if exists for validation/sync
		int en_idx = findLangIdx(key_idx, "en");
		bool have_en = (en_idx >= 0);
		const bool is_en = (lower(lang) == "en");
		if (is_en && lang_idx >= 0) { std::cerr << "ERROR: \"En\" (English) lines cannot be updated; no changes made." << std::endl; return 1; }
		// Determine EOL to use near insertion point
		auto detectEolFrom = [&](int idx)->std::string{
			if (idx > 0 && idx <= static_cast<int>(lines.size())) {
				const std::string &prev = lines[static_cast<size_t>(idx - 1)];
				if (!prev.empty()) {
					char last = prev.back();
					if (last == '\n') { if (prev.size() >= 2 && prev[prev.size()-2] == '\r') return std::string("\r\n"); return std::string("\n"); }
					if (last == '\r') return std::string("\r");
				}
			}
			return std::string("\r\n");
		};
		if (lang_idx < 0) {
			// Escape and placeholder checks vs English (if present)
			if (have_en) {
				std::string en_raw = getLangRawText(en_idx);
				// Escape parity: required to match presence of \\n, \\r, \\\\, %%
				if (!EscapeParityMatches(en_raw, text)) {
					std::cerr << "Escape parity mismatch vs 'en' for key '" << key << "' language '" << lang << "'" << std::endl;
					return 2;
				}
				// Placeholder synchronization by count
				auto en_spans = CollectPlaceholderSpans(en_raw);
				auto new_spans = CollectPlaceholderSpans(text);
				if (new_spans.size() != en_spans.size()) {
					std::cerr << "Placeholder count mismatch (en=" << en_spans.size() << ", got=" << new_spans.size() << ") for key '" << key << "' language '" << lang << "'" << std::endl;
					return 2;
				}
				bool identical = true; for (size_t i=0;i<new_spans.size();++i){ if (new_spans[i].token != en_spans[i].token) { identical = false; break; } }
				if (!identical) { text = ReplacePlaceholdersByReference(text, new_spans, en_spans); }
				// Character-length sanity check (decoded UTF-8 code points), 10x bounds as in fix
				std::string en_decoded; try { en_decoded = DecodeEscapes(en_raw, 0); } catch (...) { en_decoded = en_raw; }
				std::string new_decoded; try { new_decoded = DecodeEscapes(text, 0); } catch (...) { new_decoded = text; }
				size_t en_chars = Utf8CodePointCount(en_decoded);
				size_t new_chars = Utf8CodePointCount(new_decoded);
				if (en_chars > 0 && (new_chars > en_chars * 10 || new_chars * 10 < en_chars)) {
					std::cerr << "Text length suspicious vs 'en' (ratio check failed) for key '" << key << "' language '" << lang << "'" << std::endl;
					return 2;
				}
			}
			int ins = findInsertion(key_idx, lang);
			const std::string eol = detectEolFrom(ins);
			std::string newline;
			newline.reserve(lang.size() + text.size() + 4);
			newline.push_back('\t'); newline.append(lang); newline.push_back('\t'); newline.append(text); newline.append(eol);
			// Optional backup
			if (backup) {
				std::filesystem::path bak = map_path; bak += L".bak";
				if (!std::filesystem::exists(bak)) {
					std::filesystem::copy_file(map_path, bak, std::filesystem::copy_options::overwrite_existing);
				} else {
					for (int idx = 1;; ++idx) {
						std::filesystem::path cand = map_path; cand += (L".bak." + std::to_wstring(idx));
						if (!std::filesystem::exists(cand)) { std::filesystem::copy_file(map_path, cand); break; }
					}
				}
			}
			lines.insert(lines.begin() + ins, newline);
			// Join and write atomically
			auto joinAll = [&](const std::vector<std::string> &ls){ std::string s; size_t total = 0; for (auto &x: ls) total += x.size(); s.reserve(total); for (auto &x: ls) s.append(x); return s; };
			std::string out = joinAll(lines);
			std::filesystem::path tmp = map_path; tmp += L".tmp";
			{
				std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
				if (!o) throw std::runtime_error("Failed to write temp file");
				if (has_bom) { const unsigned char bom[3] = {0xEF,0xBB,0xBF}; o.write(reinterpret_cast<const char*>(bom), 3); }
				o.write(out.data(), static_cast<std::streamsize>(out.size()));
				o.flush();
			}
			// Try atomic replace; if not supported, remove destination first.
			std::error_code re;
			std::filesystem::rename(tmp, map_path, re);
			if (re) { std::filesystem::remove(map_path); std::filesystem::rename(tmp, map_path); }
			std::cout << "OK: Key \"" << key << "\" / Lang \"" << lang << "\" added." << std::endl;
			return 0;
		}
		// Existing line: validate placeholders and replace content after second TAB
		std::string original = lines[static_cast<size_t>(lang_idx)];
		std::string detected; size_t prefixEnd = 0;
		if (!matchLangLine(original, &detected, &prefixEnd)) { std::cerr << "ERROR: Line does not start with <TAB>lang_code<TAB>; no changes made." << std::endl; return 1; }
		if (lower(detected) != lower(lang)) { std::cerr << "ERROR: Internal mismatch (expected lang=\"" << lang << "\", found \"" << detected << "\")." << std::endl; return 1; }
		// Reject blank (only CR/LF)
		if (stripEol(original).size() == 0) { std::cerr << "ERROR: Line is empty; no changes made." << std::endl; return 1; }
		// Escape and placeholder checks vs English (if present)
		if (have_en) {
			std::string en_raw = getLangRawText(en_idx);
			if (!EscapeParityMatches(en_raw, text)) {
				std::cerr << "Escape parity mismatch vs 'en' for key '" << key << "' language '" << lang << "'" << std::endl;
				return 2;
			}
			auto en_spans = CollectPlaceholderSpans(en_raw);
			auto new_spans = CollectPlaceholderSpans(text);
			if (new_spans.size() != en_spans.size()) {
				std::cerr << "Placeholder count mismatch (en=" << en_spans.size() << ", got=" << new_spans.size() << ") for key '" << key << "' language '" << lang << "'" << std::endl;
				return 2;
			}
			bool identical = true; for (size_t i=0;i<new_spans.size();++i){ if (new_spans[i].token != en_spans[i].token) { identical = false; break; } }
			if (!identical) { text = ReplacePlaceholdersByReference(text, new_spans, en_spans); }
			// Character-length sanity check (decoded UTF-8 code points), 10x bounds as in fix
			std::string en_decoded; try { en_decoded = DecodeEscapes(en_raw, 0); } catch (...) { en_decoded = en_raw; }
			std::string new_decoded; try { new_decoded = DecodeEscapes(text, 0); } catch (...) { new_decoded = text; }
			size_t en_chars = Utf8CodePointCount(en_decoded);
			size_t new_chars = Utf8CodePointCount(new_decoded);
			if (en_chars > 0 && (new_chars > en_chars * 10 || new_chars * 10 < en_chars)) {
				std::cerr << "Text length suspicious vs 'en' (ratio check failed) for key '" << key << "' language '" << lang << "'" << std::endl;
				return 2;
			}
		}
		// Preserve original EOL
		std::string eol;
		if (!original.empty() && (original.back()=='\n' || original.back()=='\r')) {
			if (original.back()=='\n' && original.size()>=2 && original[original.size()-2]=='\r') eol = "\r\n";
			else eol = std::string(1, original.back());
		}
		std::string core = original.substr(0, original.size() - eol.size());
		std::string updated = core.substr(0, prefixEnd) + text + eol;
		if (updated == original) { std::cout << "INFO: Line already has the same content; no changes made (key=" << key << ", lang=" << lang << ")." << std::endl; return 0; }
		if (backup) {
			std::filesystem::path bak = map_path; bak += L".bak";
			if (!std::filesystem::exists(bak)) {
				std::filesystem::copy_file(map_path, bak, std::filesystem::copy_options::overwrite_existing);
			} else {
				for (int idx = 1;; ++idx) { std::filesystem::path cand = map_path; cand += (L".bak." + std::to_wstring(idx)); if (!std::filesystem::exists(cand)) { std::filesystem::copy_file(map_path, cand); break; } }
			}
		}
		lines[static_cast<size_t>(lang_idx)] = updated;
		auto joinAll = [&](const std::vector<std::string> &ls){ std::string s; size_t total = 0; for (auto &x: ls) total += x.size(); s.reserve(total); for (auto &x: ls) s.append(x); return s; };
		std::string out = joinAll(lines);
		std::filesystem::path tmp = map_path; tmp += L".tmp";
		{
			std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
			if (!o) throw std::runtime_error("Failed to write temp file");
			if (has_bom) { const unsigned char bom[3] = {0xEF,0xBB,0xBF}; o.write(reinterpret_cast<const char*>(bom), 3); }
			o.write(out.data(), static_cast<std::streamsize>(out.size()));
			o.flush();
		}
		// Try atomic replace; if not supported, remove destination first.
		std::error_code re2;
		std::filesystem::rename(tmp, map_path, re2);
		if (re2) { std::filesystem::remove(map_path); std::filesystem::rename(tmp, map_path); }
		std::cout << "OK: Key \"" << key << "\" / Lang \"" << lang << "\" updated." << std::endl;
		return 0;
	}
	if (operation == "addlang") {
		auto toLower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; };
		if (args.size() < 2) { std::cerr << "USAGE: TranslationCompiler addlang <map_file> --lang <code> [--backup]" << std::endl; return 1; }
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler addlang <map_file> --lang <code> [--backup]\n\n"
						<< "DESCRIPTION:\n"
						<< "  Adds a language code as an empty value to ALL keys in the file.\n"
						<< "  It will also sort the language names within each block alphabetically (ensuring 'en' is first).\n\n"
						<< "OPTIONS:\n"
						<< "  --lang <CODE> Language code to add.\n"
						<< "  --backup      Create a backup .bak before saving.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler addlang translations.map --lang hi\n\n"
						<< std::endl;
					return 0;
				}
			}

		std::filesystem::path map_path = args[1];
		std::string lang;
		bool backup = false;
		for (size_t i = 2; i < args.size(); ++i) {
			const std::string tok = toLower(Narrow(args[i]));
			if (tok == "--lang" || tok == "-l") {
				if (i + 1 >= args.size()) throw std::runtime_error("Missing value for --lang");
				lang = Narrow(args[++i]);
				continue;
			}
			if (tok == "--backup") { backup = true; continue; }
			throw std::runtime_error("Unknown argument: " + tok);
		}
		if (lang.empty()) throw std::runtime_error("--lang is required");

		// Validate lang code
		auto isAlphaNum = [](char c){ return std::isalnum(static_cast<unsigned char>(c)) != 0; };
		auto validateLang = [&](const std::string &code)->bool{
			if (code.size() < 2) return false;
			size_t i = 0; size_t letters = 0;
			while (i < code.size() && std::isalpha(static_cast<unsigned char>(code[i])) != 0 && letters < 3) { ++i; ++letters; }
			if (letters < 2 || letters > 3) return false;
			while (i < code.size()) {
				if (code[i] != '-') return false; ++i;
				size_t seg = 0;
				while (i < code.size() && isAlphaNum(code[i]) && seg < 8) { ++i; ++seg; }
				if (seg < 2) return false;
			}
			return true;
		};
		if (!validateLang(lang)) { std::cerr << "ERROR: Invalid language code: " << lang << std::endl; return 2; }
		if (toLower(lang) == "en") { std::cerr << "ERROR: 'en' cannot be added." << std::endl; return 1; }

		// Read file bytes
		auto readBytes = [](const std::filesystem::path &p)->std::string{
			std::ifstream in(p, std::ios::binary); if (!in) throw std::runtime_error("file not found: " + p.string());
			std::string b; in.seekg(0, std::ios::end); auto sz = in.tellg(); if (sz > 0) { b.resize(static_cast<size_t>(sz)); in.seekg(0); in.read(&b[0], static_cast<std::streamsize>(b.size())); }
			return b;
		};
		std::string raw = readBytes(map_path);
		bool has_bom = false;
		if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
			has_bom = true;
			raw.erase(0, 3);
		}
		
		auto splitKeepEol = [](const std::string &s){
			std::vector<std::string> out; out.reserve(1024);
			size_t i = 0, n = s.size();
			while (i < n) {
				size_t start = i;
				while (i < n && s[i] != '\r' && s[i] != '\n') ++i;
				if (i >= n) { out.emplace_back(s.substr(start)); break; }
				if (s[i] == '\r' && i + 1 < n && s[i+1] == '\n') { i += 2; out.emplace_back(s.substr(start, i - start)); }
				else { ++i; out.emplace_back(s.substr(start, i - start)); }
			}
			if (n == 0) out.emplace_back(std::string());
			return out;
		};
		
		auto stripEol = [](const std::string &line){ size_t b = line.size(); while (b>0 && (line[b-1]=='\r' || line[b-1]=='\n')) --b; return line.substr(0, b); };
		auto lower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; };
		auto isKeyLine = [&](const std::string& line) {
			std::string k = stripEol(line);
			if (k.empty() || k[0] == '\t' || k[0] == '#' || k.substr(0, 2) == "//") return false;
			for (char c : k) { if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false; }
			return true;
		};
		auto isLangOrCont = [](const std::string &line){ return !line.empty() && line[0] == '\t'; };

		std::vector<std::string> lines = splitKeepEol(raw);

		if (backup) {
			std::filesystem::path bak = map_path; bak += L".bak";
			if (!std::filesystem::exists(bak)) { std::filesystem::copy_file(map_path, bak, std::filesystem::copy_options::overwrite_existing); }
			else { for (int idx = 1;; ++idx) { std::filesystem::path cand = map_path; cand += (L".bak." + std::to_wstring(idx)); if (!std::filesystem::exists(cand)) { std::filesystem::copy_file(map_path, cand); break; } } }
		}

		auto matchLangLine = [&](const std::string &line, std::string *outLang, size_t *outPrefixEnd)->bool{
			if (line.empty() || line[0] != '\t') return false;
			size_t i = 1; size_t n = line.size();
			size_t start = i; size_t letters = 0;
			while (i < n && std::isalpha(static_cast<unsigned char>(line[i])) != 0 && letters < 3) { ++i; ++letters; }
			if (letters < 2 || letters > 3) return false;
			while (i < n && line[i] == '-') {
				++i; size_t seg = 0;
				while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) != 0) && seg < 8) { ++i; ++seg; }
				if (seg < 2) return false;
			}
			if (i >= n || line[i] != '\t') return false;
			if (outLang) *outLang = line.substr(start, i - start);
			if (outPrefixEnd) *outPrefixEnd = i + 1;
			return true;
		};

		// Processing
		std::vector<std::string> newLines;
		newLines.reserve(lines.size() + 20000);
		size_t idx = 0;
		while (idx < lines.size()) {
			std::string current = lines[idx];
			if (isKeyLine(current)) {
				newLines.push_back(current);
				++idx;
				// Collect the block
				std::vector<std::string> blockLines;
				std::vector<std::string> trailingEmptyLines;
				while (idx < lines.size() && (isLangOrCont(lines[idx]) || stripEol(lines[idx]).empty())) {
					if (!stripEol(lines[idx]).empty()) {
						blockLines.push_back(lines[idx]);
					} else {
						trailingEmptyLines.push_back(lines[idx]);
					}
					++idx;
				}
				// Parse block languages
				struct LangEntry {
					std::string code;
					std::vector<std::string> textLines;
				};
				std::vector<LangEntry> langEntries;
				for (const auto& bl : blockLines) {
					std::string code;
					if (matchLangLine(bl, &code, nullptr)) {
						LangEntry le;
						le.code = code;
						le.textLines.push_back(bl);
						langEntries.push_back(le);
					} else if (!langEntries.empty() && bl.size() >= 2 && bl[0] == '\t' && bl[1] == '\t') {
						langEntries.back().textLines.push_back(bl);
					}
				}
				
				// check if lang exists
				bool langExists = false;
				for (const auto& le : langEntries) {
					if (lower(le.code) == lower(lang)) {
						langExists = true;
						break;
					}
				}
				if (!langExists) {
					std::string eol = "\r\n";
					if (!current.empty()) {
						if (current.back() == '\n') eol = (current.size() >= 2 && current[current.size() - 2] == '\r') ? "\r\n" : "\n";
						else if (current.back() == '\r') eol = "\r";
					}
					LangEntry newLe;
					newLe.code = lang;
					newLe.textLines.push_back("\t" + lang + "\t" + eol);
					langEntries.push_back(newLe);
				}
				
				// Sort langEntries (en first, then alphabetically)
				std::sort(langEntries.begin(), langEntries.end(), [&](const LangEntry& a, const LangEntry& b) {
					if (lower(a.code) == "en") return true;
					if (lower(b.code) == "en") return false;
					return lower(a.code) < lower(b.code);
				});

				// Append to newLines
				for (const auto& le : langEntries) {
					for (const auto& t : le.textLines) {
						newLines.push_back(t);
					}
				}
				for (const auto& t : trailingEmptyLines) {
					newLines.push_back(t);
				}
			} else {
				newLines.push_back(current);
				++idx;
			}
		}

		// Write back
		auto joinAll = [&](const std::vector<std::string> &ls){ std::string s; size_t total = 0; for (auto &x: ls) total += x.size(); s.reserve(total); for (auto &x: ls) s.append(x); return s; };
		std::string out = joinAll(newLines);
		std::filesystem::path tmp = map_path; tmp += L".tmp";
		{
			std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
			if (!o) throw std::runtime_error("Failed to write temp file");
			if (has_bom) { const unsigned char bom[3] = {0xEF,0xBB,0xBF}; o.write(reinterpret_cast<const char*>(bom), 3); }
			o.write(out.data(), static_cast<std::streamsize>(out.size()));
			o.flush();
		}
		std::error_code re2;
		std::filesystem::rename(tmp, map_path, re2);
		if (re2) { std::filesystem::remove(map_path); std::filesystem::rename(tmp, map_path); }
		std::cout << "OK: Language \"" << lang << "\" added to all keys and sorted block-wise." << std::endl;
		return 0;
	}
	if (operation == "remove") {
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler remove <map_file> --key <KEY> [--backup]\n\n"
						<< "DESCRIPTION:\n"
						<< "  Removes an entire key block (the key line and all its language lines).\n\n"
						<< "OPTIONS:\n"
						<< "  --key <KEY>   Key to remove.\n"
						<< "  --backup      Create a backup .bak before saving.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler remove translations.map --key UNUSED_KEY --backup\n\n"
						<< std::endl;
					return 0;
				}
			}
		// Remove an entire key block including its leading blank separator line.
		// Usage: TranslationCompiler remove <map_file> --key <KEY> [--backup]
		auto toLower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; };
		if (args.size() < 2) { std::cerr << "USAGE: TranslationCompiler remove <map_file> --key <KEY> [--backup]" << std::endl; return 1; }
		std::filesystem::path map_path = args[1];
		std::string key;
		bool backup = false;
		for (size_t i = 2; i < args.size(); ++i) {
			std::string tok = toLower(Narrow(args[i]));
			if (tok == "--key" || tok == "-k") { if (i + 1 >= args.size()) throw std::runtime_error("Missing value for --key"); key = Narrow(args[++i]); continue; }
			if (tok == "--backup") { backup = true; continue; }
			throw std::runtime_error("Unknown argument: " + tok);
		}
		if (key.empty()) throw std::runtime_error("--key is required");
		// Read file
		auto readBytes = [](const std::filesystem::path &p)->std::string{ std::ifstream in(p, std::ios::binary); if (!in) throw std::runtime_error("file not found: " + p.string()); std::string b; in.seekg(0, std::ios::end); auto sz = in.tellg(); if (sz > 0) { b.resize(static_cast<size_t>(sz)); in.seekg(0); in.read(&b[0], static_cast<std::streamsize>(b.size())); } return b; };
		std::string raw = readBytes(map_path);
		bool has_bom = false;
		if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) { has_bom = true; raw.erase(0,3); }
		auto splitKeepEol = [](const std::string &s){ std::vector<std::string> out; size_t i=0,n=s.size(); while(i<n){ size_t st=i; while(i<n && s[i] != '\r' && s[i] != '\n') ++i; if(i>=n){ out.emplace_back(s.substr(st)); break; } if (s[i]=='\r' && i+1<n && s[i+1]=='\n'){ i+=2; out.emplace_back(s.substr(st, i-st)); } else { ++i; out.emplace_back(s.substr(st, i-st)); } } if (n==0) out.emplace_back(std::string()); return out; };
		auto stripEol = [](const std::string &line){ size_t b=line.size(); while(b>0 && (line[b-1]=='\r'||line[b-1]=='\n')) --b; return line.substr(0,b); };
		auto isLangOrCont = [](const std::string &line){ return !line.empty() && line[0] == '\t'; };
		std::vector<std::string> lines = splitKeepEol(raw);
		// Find key line index
		int key_idx = -1; for (size_t i=0;i<lines.size();++i){ if (stripEol(lines[i]) == key) { key_idx = static_cast<int>(i); break; } }
		if (key_idx < 0) { std::cout << "INFO: Key not found: " << key << "; no changes." << std::endl; return 0; }
		// Optional backup
		if (backup) {
			std::filesystem::path bak = map_path; bak += L".bak";
			if (!std::filesystem::exists(bak)) { std::filesystem::copy_file(map_path, bak, std::filesystem::copy_options::overwrite_existing); }
			else { for (int idx=1;;++idx){ std::filesystem::path cand = map_path; cand += (L".bak." + std::to_wstring(idx)); if (!std::filesystem::exists(cand)) { std::filesystem::copy_file(map_path, cand); break; } } }
		}
		// Remove leading blank separator if present
		if (key_idx > 0 && stripEol(lines[static_cast<size_t>(key_idx - 1)]).empty()) {
			lines.erase(lines.begin() + (key_idx - 1));
			--key_idx;
		}
		// Erase key line
		lines.erase(lines.begin() + key_idx);
		// Erase following language and continuation lines
		while (key_idx < static_cast<int>(lines.size()) && isLangOrCont(lines[static_cast<size_t>(key_idx)])) {
			lines.erase(lines.begin() + key_idx);
		}
		// Write back
		auto joinAll = [&](const std::vector<std::string> &ls){ std::string s; size_t total = 0; for (auto &x: ls) total += x.size(); s.reserve(total); for (auto &x: ls) s.append(x); return s; };
		std::string out = joinAll(lines);
		std::filesystem::path tmp = map_path; tmp += L".tmp";
		{
			std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
			if (!o) throw std::runtime_error("Failed to write temp file");
			if (has_bom) { const unsigned char bom[3] = {0xEF,0xBB,0xBF}; o.write(reinterpret_cast<const char*>(bom), 3); }
			o.write(out.data(), static_cast<std::streamsize>(out.size()));
			o.flush();
		}
		std::error_code re; std::filesystem::rename(tmp, map_path, re); if (re) { std::filesystem::remove(map_path); std::filesystem::rename(tmp, map_path); }
		std::cout << "OK: Key \"" << key << "\" removed." << std::endl;
		return 0;
	}
	if (operation == "clear_others") {
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler clear_others <map_file> --key <KEY> [--backup]\n\n"
						<< "DESCRIPTION:\n"
						<< "  Clears all language translations except 'en' for a specific key.\n\n"
						<< "OPTIONS:\n"
						<< "  --key <KEY>   Key to clear other languages for.\n"
						<< "  --backup      Create a backup .bak before saving.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler clear_others translations.map --key UNUSED_KEY --backup\n\n"
						<< std::endl;
					return 0;
				}
			}
		auto toLower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; };
		if (args.size() < 2) { std::cerr << "USAGE: TranslationCompiler clear_others <map_file> --key <KEY> [--backup]" << std::endl; return 1; }
		std::filesystem::path map_path = args[1];
		std::string key;
		bool backup = false;
		for (size_t i = 2; i < args.size(); ++i) {
			std::string tok = toLower(Narrow(args[i]));
			if (tok == "--key" || tok == "-k") { if (i + 1 >= args.size()) throw std::runtime_error("Missing value for --key"); key = Narrow(args[++i]); continue; }
			if (tok == "--backup") { backup = true; continue; }
			throw std::runtime_error("Unknown argument: " + tok);
		}
		if (key.empty()) throw std::runtime_error("--key is required");
		// Read file
		auto readBytes = [](const std::filesystem::path &p)->std::string{ std::ifstream in(p, std::ios::binary); if (!in) throw std::runtime_error("file not found: " + p.string()); std::string b; in.seekg(0, std::ios::end); auto sz = in.tellg(); if (sz > 0) { b.resize(static_cast<size_t>(sz)); in.seekg(0); in.read(&b[0], static_cast<std::streamsize>(b.size())); } return b; };
		std::string raw = readBytes(map_path);
		bool has_bom = false;
		if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) { has_bom = true; raw.erase(0,3); }
		auto splitKeepEol = [](const std::string &s){ std::vector<std::string> out; size_t i=0,n=s.size(); while(i<n){ size_t st=i; while(i<n && s[i] != '\r' && s[i] != '\n') ++i; if(i>=n){ out.emplace_back(s.substr(st)); break; } if (s[i]=='\r' && i+1<n && s[i+1]=='\n'){ i+=2; out.emplace_back(s.substr(st, i-st)); } else { ++i; out.emplace_back(s.substr(st, i-st)); } } if (n==0) out.emplace_back(std::string()); return out; };
		auto stripEol = [](const std::string &line){ size_t b=line.size(); while(b>0 && (line[b-1]=='\r'||line[b-1]=='\n')) --b; return line.substr(0,b); };
		std::vector<std::string> lines = splitKeepEol(raw);
		// Find key line index
		int key_idx = -1; for (size_t i=0;i<lines.size();++i){ if (stripEol(lines[i]) == key) { key_idx = static_cast<int>(i); break; } }
		if (key_idx < 0) { std::cout << "INFO: Key not found: " << key << "; no changes." << std::endl; return 0; }
		// Optional backup
		if (backup) {
			std::filesystem::path bak = map_path; bak += L".bak";
			if (!std::filesystem::exists(bak)) { std::filesystem::copy_file(map_path, bak, std::filesystem::copy_options::overwrite_existing); }
			else { for (int idx=1;;++idx){ std::filesystem::path cand = map_path; cand += (L".bak." + std::to_wstring(idx)); if (!std::filesystem::exists(cand)) { std::filesystem::copy_file(map_path, cand); break; } } }
		}

		auto matchLangLine = [&](const std::string &line, std::string *outLang, size_t *outPrefixEnd)->bool{
			if (line.empty() || line[0] != '\t') return false;
			size_t i = 1; size_t n = line.size();
			size_t start = i; size_t letters = 0;
			while (i < n && std::isalpha(static_cast<unsigned char>(line[i])) != 0 && letters < 3) { ++i; ++letters; }
			if (letters < 2 || letters > 3) return false;
			while (i < n && line[i] == '-') {
				++i; size_t seg = 0;
				while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) != 0) && seg < 8) { ++i; ++seg; }
				if (seg < 2) return false;
			}
			if (i >= n || line[i] != '\t') return false;
			if (outLang) *outLang = line.substr(start, i - start);
			if (outPrefixEnd) *outPrefixEnd = i + 1;
			return true;
		};

		auto isLangOrCont = [](const std::string &line){ return !line.empty() && line[0] == '\t'; };

		std::vector<std::string> newLines;
		newLines.reserve(lines.size());
		
		for (int i = 0; i <= key_idx; ++i) {
			newLines.push_back(lines[static_cast<size_t>(i)]);
		}
		
		int idx = key_idx + 1;
		while (idx < static_cast<int>(lines.size()) && isLangOrCont(lines[static_cast<size_t>(idx)])) {
			std::string current = lines[static_cast<size_t>(idx)];
			std::string langCode;
			if (matchLangLine(current, &langCode, nullptr)) {
				if (toLower(langCode) != "en") {
					// Detect EOL
					std::string eol = "\r\n";
					if (!current.empty()) {
						if (current.back() == '\n') eol = (current.size() >= 2 && current[current.size() - 2] == '\r') ? "\r\n" : "\n";
						else if (current.back() == '\r') eol = "\r";
					}
					newLines.push_back("\t" + langCode + "\t" + eol);
					++idx;
					while (idx < static_cast<int>(lines.size()) && lines[static_cast<size_t>(idx)].size() >= 2 && lines[static_cast<size_t>(idx)][0] == '\t' && lines[static_cast<size_t>(idx)][1] == '\t') {
						++idx;
					}
				} else {
					newLines.push_back(current);
					++idx;
					while (idx < static_cast<int>(lines.size()) && lines[static_cast<size_t>(idx)].size() >= 2 && lines[static_cast<size_t>(idx)][0] == '\t' && lines[static_cast<size_t>(idx)][1] == '\t') {
						newLines.push_back(lines[static_cast<size_t>(idx)]);
						++idx;
					}
				}
			} else {
				// Continuation line directly? Should not happen cleanly here, but skip or keep safety.
				newLines.push_back(current);
				++idx;
			}
		}
		
		while (idx < static_cast<int>(lines.size())) {
			newLines.push_back(lines[static_cast<size_t>(idx)]);
			++idx;
		}

		auto joinAll = [&](const std::vector<std::string> &ls){ std::string s; size_t total = 0; for (auto &x: ls) total += x.size(); s.reserve(total); for (auto &x: ls) s.append(x); return s; };
		std::string out = joinAll(newLines);
		std::filesystem::path tmp = map_path; tmp += L".tmp";
		{
			std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
			if (!o) throw std::runtime_error("Failed to write temp file");
			if (has_bom) { const unsigned char bom[3] = {0xEF,0xBB,0xBF}; o.write(reinterpret_cast<const char*>(bom), 3); }
			o.write(out.data(), static_cast<std::streamsize>(out.size()));
			o.flush();
		}
		std::error_code re2; std::filesystem::rename(tmp, map_path, re2); if (re2) { std::filesystem::remove(map_path); std::filesystem::rename(tmp, map_path); }
		std::cout << "OK: All translations except 'en' cleared for key \"" << key << "\"." << std::endl;
		return 0;
	}
	if (operation == "check") {
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler check <map_file>\n\n"
						<< "DESCRIPTION:\n"
						<< "  Validates the translations map: ensures each key has an 'en' translation,\n"
						<< "  and that printf-style placeholders match across languages.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler check srchybrid/translations/translations.map\n\n"
						<< std::endl;
					return 0;
				}
			}
		if (args.size() != 2) {
			std::cerr << "Usage: TranslationCompiler check <map_file>" << std::endl;
			return 1;
		}
		const std::filesystem::path map_file = args[1];
		const ParsedMap parsed = ParseMap(map_file);
		const std::vector<std::string> languages = BuildLanguageList(parsed.languages);
		const std::vector<FinalEntry> entries = FinalizeEntries(parsed);
		ValidateEntries(entries, languages);
		std::cout << "Check: OK (" << entries.size() << " keys, " << languages.size() << " languages)" << std::endl;
		return 0;
	}
	if (operation == "fix") {
			if (args.size() >= 2) {
				std::string t = ToLower(Narrow(args[1]));
				if (t == "--help" || t == "-h" || t == "help") {
					std::cout
						<< "USAGE: TranslationCompiler fix <map_file>\n\n"
						<< "DESCRIPTION:\n"
						<< "  Normalizes the translations map: ensures every language appears for every key (blank if missing),\n"
						<< "  clears translations that do not conform to escape/placeholder rules, and sorts keys.\n\n"
						<< "EXAMPLES:\n"
						<< "  TranslationCompiler fix srchybrid/translations/translations.map\n\n"
						<< std::endl;
					return 0;
				}
			}
		if (args.size() != 2) {
			std::cerr << "Usage: TranslationCompiler fix <map_file>" << std::endl;
			return 1;
		}
		RunFixOnMap(args[1]);
		return 0;
	}
	if (operation == "sort") {
		// Backward compatibility: map to new fix behavior.
		if (args.size() != 2) {
			std::cerr << "Usage: TranslationCompiler sort <map_file> (deprecated, use 'fix')" << std::endl;
			return 1;
		}
		RunFixOnMap(args[1]);
		return 0;
	}
	std::wcerr << L"TranslationCompiler: Unknown mode" << std::endl;
	return 1;
	} catch (const RcParseError &error) {
		std::cerr << "Rc2map: Error: " << error.what() << std::endl;
		return 1;
	} catch (const MapParseError &error) {
		std::cerr << "Map2cpp: Error: " << error.what() << std::endl;
		return 1;
	} catch (const std::exception &error) {
		std::cerr << "TranslationCompiler: Error: " << error.what() << std::endl;
		return 1;
	}
}
