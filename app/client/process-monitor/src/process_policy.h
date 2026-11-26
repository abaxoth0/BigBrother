/**
 * @file process_policy.h
 * @brief Matching of normalized server rules against executable basenames.
 */
#pragma once
#include <windows.h>
#include <algorithm>
#include <climits>
#include <string>
#include <string_view>
#include <vector>

namespace process_policy {
/** @brief Exact basename or stem prefix, with its winning priority and disposition. */
struct Rule {
    std::wstring pattern;
    int priority;
    bool prefix;
    bool exception;
};

/** @brief Compare UTF-16 names using Windows ordinal case folding. */
inline bool equal(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() &&
           CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

/** @brief Decode one fully normalized rule; reject unsupported patterns or priorities. */
inline bool parse(std::wstring_view line, Rule& rule) {
    int priority = 0;
    if (line.starts_with(L"[")) {
        const size_t end = line.find(L']');
        if (end == line.npos || end == 1) {
            return false;
        }
        for (wchar_t digit : line.substr(1, end - 1)) {
            if (digit < L'0' || digit > L'9' || priority > (INT_MAX - (digit - L'0')) / 10) {
                return false;
            }
            priority = priority * 10 + (digit - L'0');
        }
        line.remove_prefix(end + 1);
    }

    bool exception = line.starts_with(L"!");
    if (exception) {
        line.remove_prefix(1);
    }
    if (line.size() > 255 || !line.ends_with(L".exe")) {
        return false;
    }

    auto stem = line.substr(0, line.size() - 4);
    bool prefix = stem.ends_with(L"*");
    if (prefix) {
        stem.remove_suffix(1);
    }
    if (stem.empty() || stem.find_first_of(L"<>:\"/\\|?*![]") != stem.npos ||
        std::any_of(stem.begin(), stem.end(), [](wchar_t c) { return c < 32 || (c >= 127 && c <= 159); })) {
        return false;
    }

    rule = {std::wstring(prefix ? stem : line), priority, prefix, exception};
    return true;
}

/** @brief Decode a complete download before handing the rules to a worker. */
inline bool parse_list(std::wstring_view text, std::vector<Rule>& result) {
    std::vector<Rule> parsed;
    while (!text.empty()) {
        const auto end = text.find(L'\n');
        Rule rule;
        if (!parse(text.substr(0, end), rule)) {
            return false;
        }
        parsed.push_back(std::move(rule));
        if (end == text.npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }

    result = std::move(parsed);
    return true;
}

/** @brief Decode one UTF-8 server snapshot, retaining the application's last valid rules on failure. */
inline bool apply(std::string_view bytes, std::vector<Rule>& rules) {
    if (bytes.empty()) {
        rules.clear();
        return true;
    }
    if (bytes.size() > INT_MAX) {
        return false;
    }

    int length = static_cast<int>(bytes.size());
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
    if (!size) {
        return false;
    }

    std::wstring text(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, text.data(), size)) {
        return false;
    }

    return parse_list(text, rules);
}

/** @brief Unmatched names run. A matching exception wins a tie at the highest priority. */
inline bool blocked(const std::vector<Rule>& rules, std::wstring_view name) {
    if (name.size() < 5 || !equal(name.substr(name.size() - 4), L".exe")) {
        return false;
    }

    int highest = -1;
    bool block = false;
    for (const auto& rule : rules) {
        if (rule.priority < highest) {
            continue;
        }
        auto candidate = rule.prefix ? name.substr(0, std::min(rule.pattern.size(), name.size() - 4)) : name;
        if (equal(candidate, rule.pattern) && (rule.priority > highest || rule.exception)) {
            highest = rule.priority;
            block = !rule.exception;
        }
    }
    return block;
}
}
