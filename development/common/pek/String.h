/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pek {

/**
 * @brief Low-level UTF-8 codepoint helpers operating on NUL-terminated byte strings.
 */
struct codepoint {
    /**
     * @brief Decodes a single UTF-8 codepoint from @p str.
     * @param str Pointer to a NUL-terminated UTF-8 string.
     * @return Decoded codepoint, or 0 on invalid leading byte / end of string.
     */
    static char32_t decode(const char *str) {
        const unsigned char b0 = static_cast<unsigned char>(str[0]);
        if ((b0 & 0x80u) == 0u) {
            return static_cast<char32_t>(b0);
        }

        int length = 0;
        if ((b0 & 0xE0u) == 0xC0u)
            length = 2;
        else if ((b0 & 0xF0u) == 0xE0u)
            length = 3;
        else if ((b0 & 0xF8u) == 0xF0u)
            length = 4;
        else
            return 0;

        uint32_t code = 0;
        switch (length) {
        case 2:
            code = b0 & 0x1Fu;
            break;
        case 3:
            code = b0 & 0x0Fu;
            break;
        case 4:
            code = b0 & 0x07u;
            break;
        default:
            return 0;
        }

        for (int i = 1; i < length; ++i) {
            const unsigned char bi = static_cast<unsigned char>(str[i]);
            code = (code << 6) | (bi & 0x3Fu);
        }

        return static_cast<char32_t>(code);
    }

    /**
     * @brief Advances a UTF-8 pointer by one codepoint.
     * @param str Pointer to a UTF-8 byte sequence.
     * @return Pointer advanced past the current codepoint.
     */
    static const char *skip(const char *str) {
        const unsigned char b0 = static_cast<unsigned char>(str[0]);
        if ((b0 & 0x80u) == 0u) {
            return str + 1;
        }

        if ((b0 & 0xE0u) == 0xC0u)
            return str + 2;
        else if ((b0 & 0xF0u) == 0xE0u)
            return str + 3;
        else if ((b0 & 0xF8u) == 0xF0u)
            return str + 4;

        return str + 1;
    }

    /**
     * @brief Validates that @p str contains well-formed UTF-8.
     * @param str Pointer to a NUL-terminated UTF-8 string.
     * @return true if the full string is valid UTF-8, false otherwise.
     */
    static bool validate(const char *str) {
        const unsigned char *p = reinterpret_cast<const unsigned char *>(str);

        while (*p != 0) {
            unsigned char b0 = *p;

            if ((b0 & 0x80u) == 0u) {
                ++p;
                continue;
            }

            uint32_t code = 0;
            int length = 0;

            if ((b0 & 0xE0u) == 0xC0u) {
                length = 2;
                code = b0 & 0x1Fu;
                if (code < 0x2u)
                    return false;
            } else if ((b0 & 0xF0u) == 0xE0u) {
                length = 3;
                code = b0 & 0x0Fu;
            } else if ((b0 & 0xF8u) == 0xF0u) {
                length = 4;
                code = b0 & 0x07u;
            } else {
                return false;
            }

            for (int i = 1; i < length; ++i) {
                unsigned char bi = p[i];
                if (bi == 0)
                    return false;
                if ((bi & 0xC0u) != 0x80u)
                    return false;
                code = (code << 6) | (bi & 0x3Fu);
            }

            switch (length) {
            case 2:
                if (code < 0x80u || code > 0x7FFu)
                    return false;
                break;
            case 3:
                if (code < 0x800u || code > 0xFFFFu)
                    return false;
                break;
            case 4:
                if (code < 0x10000u || code > 0x10FFFFu)
                    return false;
                break;
            default:
                return false;
            }

            if (code >= 0xD800u && code <= 0xDFFFu)
                return false;
            if (code > 0x10FFFFu)
                return false;

            p += length;
        }

        return true;
    }
};

/**
 * @brief UTF-8 string helpers working on std::string values.
 */
struct utf8 {
    /// @brief Sentinel value returned when a search does not find a match.
    static constexpr std::size_t NOT_FOUND = static_cast<std::size_t>(-1);

    /**
     * @brief Returns the number of UTF-8 codepoints in @p s.
     * @param s Input UTF-8 string.
     * @return Codepoint count.
     */
    static std::size_t length(const std::string &s) {
        if (s.empty())
            return 0;

        std::size_t count = 0;
        const char *p = s.c_str();
        while (true) {
            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;
            p = codepoint::skip(p);
            ++count;
        }
        return count;
    }

    /**
     * @brief Returns true if @p s contains the codepoint @p c.
     * @param s Input UTF-8 string.
     * @param c Codepoint to search for.
     * @return true if found, false otherwise.
     */
    static bool contains(const std::string &s, char32_t c) {
        const char *p = s.c_str();
        while (true) {
            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;
            if (cp == c)
                return true;
            p = codepoint::skip(p);
        }
        return false;
    }

    /**
     * @brief Returns true if @p s contains substring @p sub at a codepoint boundary.
     * @param s Input UTF-8 string.
     * @param sub UTF-8 substring to search for.
     * @return true if found, false otherwise.
     */
    static bool contains(const std::string &s, const std::string &sub) {
        if (sub.empty())
            return true;

        const char *p = s.c_str();
        const char *end = p + s.size();
        const size_t subByteLen = sub.size();

        while (p < end) {
            const size_t remaining = static_cast<size_t>(end - p);
            if (remaining >= subByteLen && std::memcmp(p, sub.data(), subByteLen) == 0)
                return true;

            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;
            p = codepoint::skip(p);
        }

        return false;
    }

    /**
     * @brief Counts occurrences of codepoint @p target in @p s.
     * @param s Input UTF-8 string.
     * @param target Codepoint to count.
     * @return Number of matches.
     */
    static std::size_t count(const std::string &s, char32_t target) {
        if (s.empty())
            return 0;

        std::size_t cnt = 0;
        const char *p = s.c_str();
        while (true) {
            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;
            if (cp == target)
                ++cnt;
            p = codepoint::skip(p);
        }
        return cnt;
    }

    /**
     * @brief Removes the first @p codepointCount codepoints from @p s.
     * @param s Input UTF-8 string.
     * @param codepointCount Number of leading codepoints to remove.
     * @return Remaining suffix after removal.
     */
    static std::string removeLeft(const std::string &s, std::size_t codepointCount) {
        std::size_t len = length(s);
        if (len <= codepointCount)
            return {};

        const char *p = s.c_str();
        for (std::size_t i = 0; i < codepointCount; ++i)
            p = codepoint::skip(p);

        return std::string(p);
    }

    /**
     * @brief Returns the first @p codepointCount codepoints of @p s.
     * @param s Input UTF-8 string.
     * @param codepointCount Number of leading codepoints to keep.
     * @return UTF-8 prefix containing up to @p codepointCount codepoints.
     */
    static std::string left(const std::string &s, std::size_t codepointCount) {
        std::size_t len = length(s);
        if (codepointCount >= len)
            return s;

        const char *p = s.c_str();
        for (std::size_t i = 0; i < codepointCount; ++i)
            p = codepoint::skip(p);

        return std::string(s.c_str(), p - s.c_str());
    }

    /**
     * @brief Trims leading and trailing codepoints contained in @p whatToTrim.
     * @param s Input UTF-8 string.
     * @param whatToTrim Set of trim codepoints expressed as a UTF-8 string.
     * @return Trimmed string.
     */
    static std::string trim(const std::string &s, const std::string &whatToTrim = " \r\n\t") {
        if (s.empty())
            return {};

        const char *begin = s.c_str();
        const char *end = begin + s.size();

        while (begin < end) {
            char32_t cp = codepoint::decode(begin);
            if (!cp)
                break;
            if (!contains(whatToTrim, cp))
                break;
            begin = codepoint::skip(begin);
        }

        if (begin >= end)
            return {};

        const char *p = begin;
        const char *lastNonTrimEnd = nullptr;
        while (p < end) {
            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;
            const char *next = codepoint::skip(p);
            if (!contains(whatToTrim, cp))
                lastNonTrimEnd = next;
            p = next;
        }

        if (!lastNonTrimEnd)
            return {};

        return std::string(begin, static_cast<size_t>(lastNonTrimEnd - begin));
    }

    /**
     * @brief Returns true if @p s begins with UTF-8 prefix @p prefix.
     * @param s Input UTF-8 string.
     * @param prefix Prefix to test.
     * @return true if @p s starts with @p prefix, false otherwise.
     */
    static bool beginsWith(const std::string &s, const std::string &prefix) {
        if (length(prefix) > length(s))
            return false;
        if (prefix.size() > s.size())
            return false;
        return std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
    }

    /**
     * @brief Returns true if @p s ends with UTF-8 suffix @p suffix.
     * @param s Input UTF-8 string.
     * @param suffix Suffix to test.
     * @return true if @p s ends with @p suffix, false otherwise.
     */
    static bool endsWith(const std::string &s, const std::string &suffix) {
        const size_t sLen = length(s);
        const size_t sufLen = length(suffix);
        if (sufLen > sLen)
            return false;

        const char *p = s.c_str();
        for (size_t i = 0; i < sLen - sufLen; ++i)
            p = codepoint::skip(p);

        return std::memcmp(p, suffix.data(), suffix.size()) == 0 &&
               *(p + static_cast<ptrdiff_t>(suffix.size())) == '\0';
    }

    /**
     * @brief Splits @p s on occurrences of UTF-8 separator @p separator.
     * @param s Input UTF-8 string.
     * @param separator Separator substring.
     * @return Vector of split segments.
     */
    static std::vector<std::string> split(const std::string &s, const std::string &separator) {
        std::vector<std::string> parts;

        if (separator.empty()) {
            parts.push_back(s);
            return parts;
        }

        const char *begin = s.c_str();
        const char *end = begin + s.size();
        const char *segmentStart = begin;
        const size_t sepByteLen = separator.size();

        const char *p = begin;
        while (p < end) {
            const size_t remaining = static_cast<size_t>(end - p);
            if (remaining >= sepByteLen && std::memcmp(p, separator.data(), sepByteLen) == 0) {
                parts.emplace_back(segmentStart, static_cast<size_t>(p - segmentStart));
                p += static_cast<ptrdiff_t>(sepByteLen);
                segmentStart = p;
                continue;
            }

            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;
            p = codepoint::skip(p);
        }

        parts.emplace_back(segmentStart, static_cast<size_t>(end - segmentStart));

        return parts;
    }

    /**
     * @brief Converts ASCII uppercase letters in @p s to lowercase.
     * @param s Input UTF-8 string.
     * @return Copy of @p s with ASCII A-Z folded to a-z.
     */
    static std::string toLowerAscii(const std::string &s) {
        std::string out;
        out.reserve(s.size());

        const char *p = s.c_str();
        while (true) {
            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;

            const char *next = codepoint::skip(p);
            if (cp >= U'A' && cp <= U'Z') {
                out.push_back(static_cast<char>((cp - U'A') + U'a'));
            } else {
                out.append(p, static_cast<size_t>(next - p));
            }
            p = next;
        }

        return out;
    }

    /**
     * @brief Returns the codepoint index of the last occurrence of @p c in @p s.
     * @param s Input UTF-8 string.
     * @param c Codepoint to search for.
     * @return Zero-based codepoint index, or NOT_FOUND when absent.
     */
    static std::size_t lastIndexOf(const std::string &s, char32_t c) {
        const char *p = s.c_str();
        std::size_t index = 0;
        std::size_t last = NOT_FOUND;

        while (true) {
            char32_t cp = codepoint::decode(p);
            if (!cp)
                break;

            if (cp == c)
                last = index;

            p = codepoint::skip(p);
            ++index;
        }

        return last;
    }
};

} // namespace pek
