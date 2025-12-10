#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cstdarg>
#include <sstream>
#include <algorithm>

namespace amp {

    // Low–level UTF-8 codepoint codec working on const char*.
    struct codepoint
    {
        // Decode a single UTF-8 codepoint from `str`.
        // Assumes `str` points to a NUL-terminated UTF-8 sequence.
        static char32_t decode(const char* str)
        {
            const unsigned char b0 = static_cast<unsigned char>(str[0]);

            // 1-byte (ASCII)
            if ((b0 & 0x80u) == 0u) {
                return static_cast<char32_t>(b0);
            }

            // Determine sequence length from leading byte
            int length = 0;
            if      ((b0 & 0xE0u) == 0xC0u) length = 2;
            else if ((b0 & 0xF0u) == 0xE0u) length = 3;
            else if ((b0 & 0xF8u) == 0xF0u) length = 4;
            else
                return 0; // invalid leading byte → treat as null / error

            // Start from masked first byte
            uint32_t code = 0;
            switch (length) {
            case 2: code = b0 & 0x1Fu; break;
            case 3: code = b0 & 0x0Fu; break;
            case 4: code = b0 & 0x07u; break;
            default: return 0;
            }

            // Consume continuation bytes (no strict validation)
            for (int i = 1; i < length; ++i) {
                const unsigned char bi = static_cast<unsigned char>(str[i]);
                // Optionally check: if ((bi & 0xC0u) != 0x80u) → error
                code = (code << 6) | (bi & 0x3Fu);
            }

            return static_cast<char32_t>(code);
        }

        // Advance pointer by one UTF-8 codepoint.
        static const char* skip(const char* str)
        {
            const unsigned char b0 = static_cast<unsigned char>(str[0]);

            if ((b0 & 0x80u) == 0u) {
                return str + 1; // ASCII, single byte
            }

            // Determine sequence length from leading byte
            if      ((b0 & 0xE0u) == 0xC0u) return str + 2;
            else if ((b0 & 0xF0u) == 0xE0u) return str + 3;
            else if ((b0 & 0xF8u) == 0xF0u) return str + 4;

            // Invalid leading byte: advance by 1 as a fallback
            return str + 1;
        }

        // Get UTF-8 byte width of a codepoint.
        static std::size_t byteWidth(char32_t cp)
        {
            const uint32_t v = static_cast<uint32_t>(cp);

            if (v <= 0x7Fu)      return 1;
            if (v <= 0x7FFu)     return 2;
            if (v <= 0xFFFFu)    return 3;
            if (v <= 0x10FFFFu)  return 4;

            return 0; // outside Unicode range
        }

        // Encode a codepoint as UTF-8 into dst, return pointer past written bytes.
        // `space` is the max number of bytes available in dst (including a NUL).
        static char* encode(char* dst, char32_t cp, std::size_t space)
        {
            const uint32_t v_in = static_cast<uint32_t>(cp);
            const std::size_t width = byteWidth(cp);

            if (width == 0 || width + 1 > space) { // +1 for terminating 0 if needed
                return dst;
            }

            uint32_t v = v_in;
            unsigned char* out = reinterpret_cast<unsigned char*>(dst);

            switch (width) {
            case 1:
                out[0] = static_cast<unsigned char>(v);
                out[1] = 0;
                return dst + 1;

            case 2:
                v &= 0x7FFu;
                out[0] = static_cast<unsigned char>(0xC0u | (v >> 6));
                out[1] = static_cast<unsigned char>(0x80u | (v & 0x3Fu));
                out[2] = 0;
                return dst + 2;

            case 3:
                v &= 0xFFFFu;
                out[0] = static_cast<unsigned char>(0xE0u | (v >> 12));
                out[1] = static_cast<unsigned char>(0x80u | ((v >> 6) & 0x3Fu));
                out[2] = static_cast<unsigned char>(0x80u | (v & 0x3Fu));
                out[3] = 0;
                return dst + 3;

            case 4:
                v &= 0x1FFFFFu;
                out[0] = static_cast<unsigned char>(0xF0u | (v >> 18));
                out[1] = static_cast<unsigned char>(0x80u | ((v >> 12) & 0x3Fu));
                out[2] = static_cast<unsigned char>(0x80u | ((v >> 6) & 0x3Fu));
                out[3] = static_cast<unsigned char>(0x80u | (v & 0x3Fu));
                out[4] = 0;
                return dst + 4;

            default:
                return dst;
            }
        }

        // Count codepoints in a NULL-terminated UTF-8 string.
        static std::size_t count(const char* str)
        {
            std::size_t n = 0;
            while (true) {
                char32_t cp = decode(str);
                if (!cp) break;
                str = skip(str);
                ++n;
            }
            return n;
        }

        /// Check whether string contains codepoint `cp`.
        /// If `foundAt` is non-null, it is set to the pointer where `cp` starts.
        static bool contains(const char* str, char32_t cp, const char** foundAt = nullptr)
        {
            while (true) {
                char32_t current = decode(str);
                if (!current) break;
                if (current == cp) {
                    if (foundAt) *foundAt = str;
                    return true;
                }
                str = skip(str);
            }
            return false;
        }

        // Return true if `str` is a NULL-terminated string of *valid* UTF-8.
        static bool validate(const char* str)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(str);

            while (*p != 0) {
                unsigned char b0 = *p;

                // 1-byte ASCII: 0xxxxxxx
                if ((b0 & 0x80u) == 0u) {
                    ++p;
                    continue;
                }

                // Determine sequence length and initial codepoint bits
                uint32_t code = 0;
                int length = 0;

                if ((b0 & 0xE0u) == 0xC0u) {          // 110xxxxx → 2 bytes
                    length = 2;
                    code   = b0 & 0x1Fu;
                    // Overlong check: 2-byte sequence must be >= 0x80
                    if (code < 0x2u) return false;
                }
                else if ((b0 & 0xF0u) == 0xE0u) {     // 1110xxxx → 3 bytes
                    length = 3;
                    code   = b0 & 0x0Fu;
                }
                else if ((b0 & 0xF8u) == 0xF0u) {     // 11110xxx → 4 bytes
                    length = 4;
                    code   = b0 & 0x07u;
                }
                else {
                    // Invalid leading byte
                    return false;
                }

                // Make sure we actually *have* that many bytes before NUL.
                // (p[0] is b0; we need p[1..length-1])
                for (int i = 1; i < length; ++i) {
                    unsigned char bi = p[i];
                    if (bi == 0) {
                        // String ended in the middle of a sequence
                        return false;
                    }
                    // Continuation must be 10xxxxxx
                    if ((bi & 0xC0u) != 0x80u) {
                        return false;
                    }
                    code = (code << 6) | (bi & 0x3Fu);
                }

                // Now `code` is the decoded scalar value; apply further checks.

                // Check for overlong encodings:
                switch (length) {
                case 2:
                    if (code < 0x80u || code > 0x7FFu) return false;
                    break;
                case 3:
                    if (code < 0x800u || code > 0xFFFFu) return false;
                    break;
                case 4:
                    if (code < 0x10000u || code > 0x10FFFFu) return false;
                    break;
                default:
                    // Shouldn’t happen, but be defensive
                    return false;
                }

                // Exclude UTF-16 surrogate range
                if (code >= 0xD800u && code <= 0xDFFFu)
                    return false;

                // Exclude codepoints > Unicode max (already covered above, but keep)
                if (code > 0x10FFFFu)
                    return false;

                p += length;
            }

            return true;
        }
        
    };

    // Higher-level UTF-8 string utilities working on std::string.
    struct utf8
    {
        static constexpr std::size_t NOT_FOUND = static_cast<std::size_t>(-1);

        static std::size_t byteLength(const std::string& s)
        {
            return s.size();
        }

        // Number of Unicode codepoints in s.
        static std::size_t length(const std::string& s)
        {
            if (s.empty()) return 0;

            std::size_t count = 0;
            const char* p = s.c_str();
            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                p = codepoint::skip(p);
                ++count;
            }
            return count;
        }

        static void append(std::string& s, char32_t cp)
        {
            char buf[8]{};
            codepoint::encode(buf, cp, sizeof(buf));
            s += buf;
        }

        static void prepend(std::string& s, char32_t cp)
        {
            char buf[8]{};
            codepoint::encode(buf, cp, sizeof(buf));
            s = std::string(buf) + s;
        }

        static bool contains(const std::string& s, char32_t c)
        {
            const char* p = s.c_str();
            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                if (cp == c) return true;
                p = codepoint::skip(p);
            }
            return false;
        }

        static bool contains(const std::string& s, const std::string& sub)
        {
            if (sub.empty())
                return true;

            std::size_t sLen   = length(s);    // in codepoints
            std::size_t subLen = length(sub);  // in codepoints

            if (subLen > sLen)
                return false;

            for (std::size_t i = 0; i + subLen <= sLen; ++i) {
                if (substring(s, i, subLen) == sub)
                    return true;
            }
            return false;
        }

        static bool containsOnly(const std::string& str, const std::string& allowedChars)
        {
            const char* p = str.c_str();
            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                if (!contains(allowedChars, cp))
                    return false;
                p = codepoint::skip(p);
            }
            return true;
        }

        static std::size_t count(const std::string& s, char32_t target)
        {
            if (s.empty()) return 0;

            std::size_t cnt = 0;
            const char* p = s.c_str();
            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                if (cp == target)
                    ++cnt;
                p = codepoint::skip(p);
            }
            return cnt;
        }

        static std::string removeRight(const std::string& s, std::size_t codepointCount)
        {
            std::size_t len = length(s);
            if (len <= codepointCount) return {};

            std::size_t copyCount = len - codepointCount;
            std::string result;

            const char* p = s.c_str();
            for (std::size_t i = 0; i < copyCount; ++i)
            {
                char32_t cp = codepoint::decode(p);
                append(result, cp);
                p = codepoint::skip(p);
            }

            return result;
        }

        static std::string removeLeft(const std::string& s, std::size_t codepointCount)
        {
            std::size_t len = length(s);
            if (len <= codepointCount) return {};

            const char* p = s.c_str();
            for (std::size_t i = 0; i < codepointCount; ++i)
                p = codepoint::skip(p);

            return std::string(p);
        }

        static std::string right(const std::string& s, std::size_t codepointCount)
        {
            std::size_t len = length(s);
            if (codepointCount >= len) return s;
            return removeLeft(s, len - codepointCount);
        }

        static std::string left(const std::string& s, std::size_t codepointCount)
        {
            std::size_t len = length(s);
            if (codepointCount >= len) return s;
            return removeRight(s, len - codepointCount);
        }

        static char32_t first(const std::string& s)
        {
            if (s.empty()) return 0;
            return codepoint::decode(s.c_str());
        }

        static char32_t last(const std::string& s)
        {
            std::size_t len = length(s);
            if (!len) return 0;

            const char* p = s.c_str();
            for (std::size_t i = 0; i < len - 1; ++i)
                p = codepoint::skip(p);

            return codepoint::decode(p);
        }

        static std::string substring(const std::string& s,
                                     std::size_t firstCodepoint,
                                     std::size_t codepointCount)
        {
            std::string result;

            std::size_t len = length(s);
            if (firstCodepoint >= len) return {};

            const char* p = s.c_str();
            for (std::size_t i = 0; i < firstCodepoint; ++i)
                p = codepoint::skip(p);

            for (std::size_t i = 0; i < codepointCount; ++i) {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                append(result, cp);
                p = codepoint::skip(p);
            }

            return result;
        }

        static std::string trim(const std::string& s,
                                const std::string& whatToTrim = " \r\n\t")
        {
            if (s.empty())
                return {};

            const char* p = s.c_str();

            // 1) Count leading characters to trim
            std::size_t leading = 0;
            {
                const char* cur = p;
                while (true) {
                    char32_t cp = codepoint::decode(cur);
                    if (!cp) break;                          // end of string
                    if (!contains(whatToTrim, cp)) break;    // first non-trim
                    cur = codepoint::skip(cur);
                    ++leading;
                }
            }

            // 2) Count trailing characters to trim
            std::size_t trailing = 0;
            {
                const char* cur = p;
                std::size_t idx = 0;
                std::size_t totalLen = length(s);

                while (true) {
                    char32_t cp = codepoint::decode(cur);
                    if (!cp) break;                          // end of string
                    cur = codepoint::skip(cur);
                    ++idx;

                    if (contains(whatToTrim, cp))
                        ++trailing;                          // extend trailing run
                    else
                        trailing = 0;                        // reset trailing run
                }

                if (trailing > totalLen) trailing = totalLen;
            }

            // 3) Compute remaining length and slice
            std::size_t totalLen = length(s);
            if (leading + trailing >= totalLen)
                return {};

            return substring(s, leading, totalLen - leading - trailing);
        }

        static std::string replace(const std::string& s,
                                   char32_t from,
                                   char32_t to)
        {
            std::string result;
            const char* p = s.c_str();

            while (true) {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;

                if (cp == from)
                    append(result, to);
                else
                    append(result, cp);

                p = codepoint::skip(p);
            }

            return result;
        }

        static void replaceInPlace(std::string& s,
                                   const std::string& from,
                                   const std::string& to)
        {
            if (from.empty())
                return;

            std::size_t sLen    = length(s);    // in codepoints
            std::size_t fromLen = length(from);
            std::string result;
            result.reserve(s.size());

            const char* p = s.c_str();
            std::size_t i = 0; // codepoint index in s

            while (i < sLen) {
                // Do we have room for a full 'from' and does it match here?
                if (i + fromLen <= sLen && substring(s, i, fromLen) == from) {
                    // Append replacement
                    result += to;
                    i += fromLen;

                    // advance pointer p by fromLen codepoints
                    for (std::size_t k = 0; k < fromLen; ++k)
                        p = codepoint::skip(p);
                } else {
                    // Copy one codepoint from s to result
                    char32_t cp = codepoint::decode(p);
                    append(result, cp);
                    p = codepoint::skip(p);
                    ++i;
                }
            }

            s.swap(result);
        }

        static std::string replace(const std::string& s,
                                   const std::string& from,
                                   const std::string& to)
        {
            std::string copy = s;
            replaceInPlace(copy, from, to);
            return copy;
        }

        static std::string remove(const std::string& s, char32_t target)
        {
            std::string result;
            const char* p = s.c_str();

            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                if (cp != target)
                    append(result, cp);
                p = codepoint::skip(p);
            }

            return result;
        }

        static std::string removeCharacters(const std::string& s,
                                            const std::string& chars)
        {
            std::string result;
            const char* p = s.c_str();

            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                if (!contains(chars, cp))
                    append(result, cp);
                p = codepoint::skip(p);
            }

            return result;
        }

        static std::string simplify(const std::string& s,
                                    const std::string& whatToSimplify = " \r\n\t",
                                    char32_t simpleOne = U' ')
        {
            std::string simplified;
            std::string t = trim(s, whatToSimplify);
            const char* p = t.c_str();

            bool inWhitespace = false;

            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;

                if (contains(whatToSimplify, cp))
                {
                    if (!inWhitespace)
                        append(simplified, simpleOne);
                    inWhitespace = true;
                }
                else
                {
                    inWhitespace = false;
                    append(simplified, cp);
                }
                p = codepoint::skip(p);
            }

            return simplified;
        }

        static bool beginsWith(const std::string& s, const std::string& prefix)
        {
            if (length(prefix) > length(s))
                return false;

            std::size_t prefixLen = length(prefix);
            std::string slice = substring(s, 0, prefixLen);

            return slice == prefix;
        }

        static bool endsWith(const std::string& s, const std::string& suffix)
        {
            std::size_t sLen  = length(s);
            std::size_t sufLen = length(suffix);

            if (sLen < sufLen)
                return false;

            std::string slice = substring(s, sLen - sufLen, sufLen);
            return slice == suffix;
        }

        static bool beginsWith(const std::string& s, char32_t c)
        {
            if (s.empty()) return false;
            return codepoint::decode(s.c_str()) == c;
        }

        static bool endsWith(const std::string& s, char32_t c)
        {
            std::size_t len = length(s);
            if (len == 0) return false;

            const char* p = s.c_str();
            for (std::size_t i = 0; i < len - 1; ++i)
                p = codepoint::skip(p);

            return codepoint::decode(p) == c;
        }

        static std::size_t segmentCount(const std::string& s,
                                        const std::string& separator)
        {
            if (s.empty())
                return 0;

            if (separator.empty())
                return 1;

            std::size_t sLen   = length(s);
            std::size_t sepLen = length(separator);

            if (sepLen > sLen)
                return 1;

            std::size_t occurrences = 0;

            for (std::size_t i = 0; i + sepLen <= sLen; ++i)
            {
                if (substring(s, i, sepLen) == separator)
                    ++occurrences;
            }

            return occurrences + 1;
        }

        static std::string segmentAt(const std::string& s,
                                     const std::string& separator,
                                     std::size_t index)
        {
            if (s.empty())
                return {};

            if (separator.empty()) {
                return (index == 0) ? s : std::string{};
            }

            std::size_t sLen   = length(s);
            std::size_t sepLen = length(separator);

            if (sepLen > sLen) {
                return (index == 0) ? s : std::string{};
            }

            std::size_t currentSegmentIndex = 0;
            std::size_t segmentStart = 0; // in codepoints

            std::size_t i = 0; // current position in codepoints
            while (i + sepLen <= sLen)
            {
                if (substring(s, i, sepLen) == separator)
                {
                    if (currentSegmentIndex == index) {
                        return substring(s, segmentStart, i - segmentStart);
                    }

                    ++currentSegmentIndex;
                    segmentStart = i + sepLen;
                    i = segmentStart;
                }
                else
                {
                    ++i;
                }
            }

            if (currentSegmentIndex == index) {
                return substring(s, segmentStart, sLen - segmentStart);
            }

            return {};
        }

        static std::vector<std::string> split(const std::string& s,
                                              const std::string& separator)
        {
            std::vector<std::string> parts;

            if (separator.empty()) {
                parts.push_back(s);
                return parts;
            }

            std::size_t sLen   = length(s);
            std::size_t sepLen = length(separator);

            if (sLen == 0) {
                parts.emplace_back();
                return parts;
            }

            const char* p = s.c_str();
            std::size_t i = 0;           // codepoint index
            std::size_t segmentStart = 0; // codepoint index

            while (i + sepLen <= sLen) {
                if (substring(s, i, sepLen) == separator) {
                    parts.push_back(substring(s, segmentStart, i - segmentStart));
                    i += sepLen;
                    segmentStart = i;

                    for (std::size_t k = 0; k < sepLen; ++k)
                        p = codepoint::skip(p);
                } else {
                    p = codepoint::skip(p);
                    ++i;
                }
            }

            if (segmentStart <= sLen) {
                parts.push_back(substring(s, segmentStart, sLen - segmentStart));
            }

            return parts;
        }

        static std::vector<std::string> splitWithTrim(
            const std::string& s,
            const std::string& separator,
            bool keepEmptyItems = false,
            const std::string& whatToTrim = " \r\n\t")
        {
            std::vector<std::string> parts;

            if (separator.empty()) {
                std::string item = trim(s, whatToTrim);
                if (keepEmptyItems || !item.empty())
                    parts.push_back(item);
                return parts;
            }

            std::size_t sLen   = length(s);
            std::size_t sepLen = length(separator);

            if (sLen == 0) {
                if (keepEmptyItems)
                    parts.emplace_back();
                return parts;
            }

            const char* p = s.c_str();
            std::size_t i = 0;           // codepoint index
            std::size_t segmentStart = 0; // codepoint index

            while (i + sepLen <= sLen) {
                if (substring(s, i, sepLen) == separator) {
                    std::string item = substring(s, segmentStart, i - segmentStart);
                    item = trim(item, whatToTrim);

                    if (keepEmptyItems || !item.empty())
                        parts.push_back(item);

                    i += sepLen;
                    segmentStart = i;

                    for (std::size_t k = 0; k < sepLen; ++k)
                        p = codepoint::skip(p);
                } else {
                    p = codepoint::skip(p);
                    ++i;
                }
            }

            if (segmentStart <= sLen) {
                std::string item = substring(s, segmentStart, sLen - segmentStart);
                item = trim(item, whatToTrim);

                if (keepEmptyItems || !item.empty())
                    parts.push_back(item);
            }

            return parts;
        }

        static std::string toLowerAscii(const std::string& s)
        {
            std::string result;
            result.reserve(s.size());

            const char* p = s.c_str();

            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp)
                    break;

                if (cp >= U'A' && cp <= U'Z') {
                    cp = (cp - U'A') + U'a';
                }

                append(result, cp);
                p = codepoint::skip(p);
            }

            return result;
        }

        static std::size_t lastIndexOf(const std::string& s, char32_t c)
        {
            const char* p = s.c_str();
            std::size_t index = 0;
            std::size_t last  = NOT_FOUND;

            while (true)
            {
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

        // Remove the last segment delimited by `separator`.
        //
        // Segment is defined as the substring *after* the last occurrence
        // of `separator` (in UTF-8 codepoints). We do NOT try to interpret
        // or skip empty segments; we just look for the last separator and
        // chop off everything after it (optionally including the separator).
        //
        // Examples, separator = "/":
        //   "///a//b"  keepSeparator=true  -> "///a//"
        //   "///a//b"  keepSeparator=false -> "///a/"
        //   "abc"                          -> ""        (only one segment)
        //   "abc/"                         -> "abc/"    (last segment is empty, no change)
        inline std::string removeLastSegment(const std::string& s, const std::string& separator,
            bool keepSeparator = false)
        {
            if (s.empty())
                return {};

            if (separator.empty())
                return s;

            const std::size_t sLen   = utf8::length(s);         // in codepoints
            const std::size_t sepLen = utf8::length(separator); // in codepoints

            if (sepLen == 0 || sepLen > sLen)
                return s;

            // Find last occurrence of separator in codepoint space.
            std::size_t lastPos = utf8::NOT_FOUND;

            // We search from right to left over possible starting positions.
            // Valid start positions: [0 .. sLen - sepLen].
            if (sLen >= sepLen) {
                for (std::size_t pos = sLen - sepLen + 1; pos-- > 0; ) {
                    if (utf8::substring(s, pos, sepLen) == separator) {
                        lastPos = pos;
                        break;
                    }
                    if (pos == 0) break; // guard against size_t underflow
                }
            }

            if (lastPos == utf8::NOT_FOUND) {
                // No separator → single segment → removing it yields empty string.
                return {};
            }

            const std::size_t segmentStart = lastPos + sepLen;
            if (segmentStart >= sLen) {
                // Last separator is at the very end; last segment is empty → no change.
                return s;
            }

            // There are actual characters after the last separator.
            // Keep everything before the last segment, optionally including that separator.
            const std::size_t keepLen = keepSeparator ? segmentStart : lastPos;
            return utf8::substring(s, 0, keepLen);
        }

        // Remove the first segment delimited by `separator`.
        //
        // Segment is defined as the substring *before* the first occurrence
        // of `separator`. Again, we do not do anything special with empty
        // segments – if the string starts with the separator, the first
        // segment is just the empty prefix, and removing it drops exactly
        // one occurrence of the separator when keepSeparator == false.
        //
        // Examples, separator = "/":
        //   "///a//b"  keepSeparator=false -> "//a//b"   (drop first empty segment + one '/')
        //   "///a//b"  keepSeparator=true  -> "///a//b"  (first segment is empty; we keep sep)
        //   "a/b/c"    keepSeparator=false -> "b/c"
        //   "a/b/c"    keepSeparator=true  -> "/b/c"
        //   "abc"                          -> ""        (single segment)
        inline std::string removeFirstSegment(const std::string& s, const std::string& separator,
            bool keepSeparator = false)
        {
            if (s.empty())
                return {};

            if (separator.empty())
                return s;

            const std::size_t sLen   = utf8::length(s);
            const std::size_t sepLen = utf8::length(separator);

            if (sepLen == 0 || sepLen > sLen)
                return s;

            // Find first occurrence of separator in codepoint space.
            std::size_t firstPos = utf8::NOT_FOUND;

            for (std::size_t pos = 0; pos + sepLen <= sLen; ++pos) {
                if (utf8::substring(s, pos, sepLen) == separator) {
                    firstPos = pos;
                    break;
                }
            }

            if (firstPos == utf8::NOT_FOUND) {
                // No separator → the whole string is one segment → removing it yields empty.
                return {};
            }

            // Everything before firstPos is the first segment.
            // Decide where the new string should start.
            std::size_t newStart = keepSeparator ? firstPos : firstPos + sepLen;

            if (newStart >= sLen)
                return {}; // nothing left after removing first segment

            return utf8::substring(s, newStart, sLen - newStart);
        }

        static std::size_t firstIndexOf(const std::string& s, char32_t c)
        {
            const char* p = s.c_str();
            std::size_t index = 0;

            while (true)
            {
                char32_t cp = codepoint::decode(p);
                if (!cp) break;
                if (cp == c) return index;
                p = codepoint::skip(p);
                ++index;
            }

            return NOT_FOUND;
        }

        template <typename T>
        static T parse(const std::string& s, bool* parsedSuccessfully = nullptr)
        {
            if (s.empty()) {
                if (parsedSuccessfully) *parsedSuccessfully = false;
                return T{};
            }

            std::istringstream iss(s);
            iss >> std::ws; // skip leading whitespace

            T value{};
            iss >> value; // parse the number

            bool ok = !iss.fail();

            if (ok) {
                iss >> std::ws; // skip trailing whitespace
                if (!iss.eof()) ok = false;
            }

            if (parsedSuccessfully) *parsedSuccessfully = ok;

            return ok ? value : T{};
        }

        template <typename T>
        static bool tryParse(const std::string& s, T* outValue = nullptr)
        {
            bool ok = false;
            T value = parse<T>(s, &ok);

            if (ok && outValue)
                *outValue = value;

            return ok;
        }

        static void trimInPlace(std::string& s,
                                const std::string& whatToTrim = " \r\n\t")
        {
            s = trim(s, whatToTrim);
        }

        static void simplifyInPlace(std::string& s,
                                    const std::string& whatToSimplify = " \r\n\t",
                                    char32_t simpleOne = U' ')
        {
            s = simplify(s, whatToSimplify, simpleOne);
        }

        static void toLowerAsciiInPlace(std::string& s)
        {
            s = toLowerAscii(s);
        }

        static void replaceInPlace(std::string& s,
                                   char32_t from,
                                   char32_t to)
        {
            s = replace(s, from, to);
        }

        static void removeInPlace(std::string& s, char32_t target)
        {
            s = remove(s, target);
        }

        static void removeCharactersInPlace(std::string& s,
                                            const std::string& chars)
        {
            s = removeCharacters(s, chars);
        }
    };
}

