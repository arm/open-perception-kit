#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cstdarg>
#include <sstream>
#include <algorithm>

namespace amp { 

    struct utf8 {

        static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

        // Decode a single UTF-8 codepoint from `str`.
        static char32_t codepointDecode(const char* str) {
            unsigned int cp = static_cast<unsigned char>(str[0]);

            if ((cp & 0x80u) != 0u) {
                if ((cp & 0xE0u) == 0xC0u) {
                    cp = ((cp & 0x1Fu) << 6) |
                        (static_cast<unsigned char>(str[1]) & 0x3Fu);
                } else if ((cp & 0xF0u) == 0xE0u) {
                    cp = ((cp & 0x0Fu) << 12) |
                        ((static_cast<unsigned char>(str[1]) & 0x3Fu) << 6) |
                        (static_cast<unsigned char>(str[2]) & 0x3Fu);
                } else if ((cp & 0xF8u) == 0xF0u) {
                    cp = ((cp & 0x07u) << 18) |
                        ((static_cast<unsigned char>(str[1]) & 0x3Fu) << 12) |
                        ((static_cast<unsigned char>(str[2]) & 0x3Fu) << 6) |
                        (static_cast<unsigned char>(str[3]) & 0x3Fu);
                }
            }

            return static_cast<char32_t>(cp);
        }

        // Advance pointer by one UTF-8 codepoint.
        static const char* codepointSkip(const char* str) {
            unsigned char c = static_cast<unsigned char>(*str);
            if ((c & 0x80u) == 0x00u) {
                ++str;
            } else if ((c & 0xE0u) == 0xC0u) {
                str += 2;
            } else if ((c & 0xF0u) == 0xE0u) {
                str += 3;
            } else if ((c & 0xF8u) == 0xF0u) {
                str += 4;
            }
            return str;
        }

        // Get UTF-8 byte width of a codepoint.
        static std::size_t codepointByteWidth(char32_t cp) {
            auto v = static_cast<uint32_t>(cp);
            if (v <= 0x7Fu)      return 1;
            if (v <= 0x7FFu)     return 2;
            if (v <= 0xFFFFu)    return 3;
            if (v <= 0x10FFFFu)  return 4;
            return 0;
        }

        // Encode a codepoint as UTF-8 into dst, return pointer past written bytes.
        static char* codepointEncode(char* dst, char32_t cp, std::size_t space) {
            std::size_t width = codepointByteWidth(cp);
            if (width == 0 || width + 1 > space) // +1 for nul
                return dst;

            uint32_t v = static_cast<uint32_t>(cp);

            switch (width) {
            case 1:
                dst[0] = static_cast<unsigned char>(v & 0x7F);
                dst[1] = '\0';
                return dst + 1;
            case 2:
                v &= 0x7FFu;
                dst[0] = static_cast<unsigned char>(0xC0u | (v >> 6));
                dst[1] = static_cast<unsigned char>(0x80u | (v & 0x3Fu));
                dst[2] = '\0';
                return dst + 2;
            case 3:
                v &= 0xFFFFu;
                dst[0] = static_cast<unsigned char>(0xE0u | (v >> 12));
                dst[1] = static_cast<unsigned char>(0x80u | ((v >> 6) & 0x3Fu));
                dst[2] = static_cast<unsigned char>(0x80u | (v & 0x3Fu));
                dst[3] = '\0';
                return dst + 3;
            case 4:
                v &= 0x1FFFFFu;
                dst[0] = static_cast<unsigned char>(0xF0u | (v >> 18));
                dst[1] = static_cast<unsigned char>(0x80u | ((v >> 12) & 0x3Fu));
                dst[2] = static_cast<unsigned char>(0x80u | ((v >> 6) & 0x3Fu));
                dst[3] = static_cast<unsigned char>(0x80u | (v & 0x3Fu));
                dst[4] = '\0';
                return dst + 4;
            default:
                return dst;
            }
        }

        // Count codepoints in a NULL-terminated UTF-8 string.
        static std::size_t codepointCount(const char* str) {
            std::size_t count = 0;
            while (true) {
                char32_t cp = codepointDecode(str);
                if (!cp) break;
                str = codepointSkip(str);
                ++count;
            }
            return count;
        }

        /// Check whether string contains codepoint `cp`.
        static bool codepointContained(const char* str, char32_t cp, const char** foundAt = nullptr) {
            while (true) {
                char32_t current = codepointDecode(str);
                if (!current) break;
                if (current == cp) {
                    if(foundAt) *foundAt = str;
                    return true;
                }
                str = codepointSkip(str);
            }
            return false;
        }

        static std::size_t byteLength(const std::string& s) {
            return s.size();
        }

        static std::size_t length(const std::string& s) {
            if (s.empty()) return 0;

            std::size_t count = 0;
            const char* p = s.c_str();
            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                p = codepointSkip(p);
                ++count;
            }
            return count;
        }

        static void append(std::string& s, char32_t cp) {
            char buf[8]{};
            codepointEncode(buf, cp, 8);
            s += buf;
        }

        static void prepend(std::string& s, char32_t cp) {
            char buf[8]{};
            codepointEncode(buf, cp, 8);
            s = std::string(buf) + s;
        }

        static bool contains(const std::string& s, char32_t c)
        {
            const char* p = s.c_str();
            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                if (cp == c) return true;
                p = codepointSkip(p);
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

        static bool containsOnly(const std::string& str, const std::string& allowedChars) {
            const char* p = str.c_str(); 
            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                if (!contains(allowedChars, cp))
                    return false;
                p = codepointSkip(p);
            }
            return true;
        }

        static std::size_t count(const std::string& s, char32_t target) {
            if (s.empty()) return 0;

            std::size_t cnt = 0;
            const char* p = s.c_str();
            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                if (cp == target)
                    ++cnt;
                p = codepointSkip(p);
            }
            return cnt;
        }

        static std::string removeRight(const std::string& s, size_t codepointCount) {
            size_t len = static_cast<int>(length(s));
            if (len <= codepointCount) return {};

            int copyCount = len - codepointCount;
            std::string result;

            const char* p = s.c_str();
            for (size_t i = 0; i < copyCount; ++i)
            {
                char32_t cp = codepointDecode(p);
                append(result, cp);
                p = codepointSkip(p);
            }

            return result;
        }

        static std::string removeLeft(const std::string& s, size_t codepointCount) {
            size_t len = static_cast<int>(length(s));
            if (len <= codepointCount) return {};

            const char* p = s.c_str();
            for (size_t i = 0; i < codepointCount; ++i)
                p = codepointSkip(p);

            return std::string(p);
        }

        static std::string right(const std::string& s, size_t codepointCount) {
            size_t len = static_cast<int>(length(s));
            if (codepointCount >= len) return s;
            return removeLeft(s, len - codepointCount);
        }

        static std::string left(const std::string& s, size_t codepointCount) {
            size_t len = static_cast<int>(length(s));
            if (codepointCount >= len) return s;
            return removeRight(s, len - codepointCount);
        }

        static char32_t first(const std::string& s) {
            if(!s.length()) return 0;
            return codepointDecode(s.c_str());
        }

        static char32_t last(const std::string& s) {
            size_t len = length(s);
            const char* p = s.c_str();
            if(!len) return 0;

            for(size_t i = 0; i < len - 1; i++)
                p = codepointSkip(p); 

            return codepointDecode(p);
        }

        static std::string substring(const std::string& s, size_t firstCodepoint, size_t codepointCount) {
            std::string result;

            size_t len = length(s);
            if (firstCodepoint >= len) return {};

            const char* p = s.c_str();
            for (size_t i = 0; i < firstCodepoint; ++i)
                p = codepointSkip(p);

            for (size_t i = 0; i < codepointCount; ++i) {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                append(result, cp);
                p = codepointSkip(p);
            }

            return result;
        }

        static std::string trim(const std::string& s, const std::string& whatToTrim = " \r\n\t") {
            if (s.empty())
                return {};

            const char* p = s.c_str();

            // 1) Count leading "litter" codepoints
            std::size_t leading = 0;
            {
                const char* cur = p;
                while (true) {
                    char32_t cp = codepointDecode(cur);
                    if (!cp) break;                          // end of string
                    if (!contains(whatToTrim, cp)) break;        // first non-litter
                    cur = codepointSkip(cur);
                    ++leading;
                }
            }

            // 2) Count trailing "litter" codepoints
            std::size_t trailing = 0;
            {
                const char* cur = p;
                std::size_t idx = 0;
                std::size_t totalLen = length(s);

                while (true) {
                    char32_t cp = codepointDecode(cur);
                    if (!cp) break;                          // end of string
                    cur = codepointSkip(cur);
                    ++idx;

                    if (contains(whatToTrim, cp))
                        ++trailing;                          // extend trailing run
                    else
                        trailing = 0;                        // reset trailing run
                }

                // Safety: never let trailing exceed total leđngth
                if (trailing > totalLen) trailing = totalLen;
            }

            // 3) Compute remaining length and slice
            std::size_t totalLen = length(s);
            if (leading + trailing >= totalLen)
                return {};

            return substring(s, leading, totalLen - leading - trailing);
        }
 
         static std::string replace(const std::string& s, char32_t from, char32_t to) {
            std::string result;
            const char* p = s.c_str();

            while (true) {
                char32_t cp = codepointDecode(p);
                if (!cp) break;

                if (cp == from)
                    append(result, to);
                else
                    append(result, cp);

                p = codepointSkip(p);
            }

            return result;
        }

        static void replaceInPlace(std::string& s, const std::string& from, const std::string& to)
        {
            if (from.empty())
                return;

            std::size_t sLen   = length(s);    // in codepoints
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
                        p = codepointSkip(p);
                } else {
                    // Copy one codepoint from s to result
                    char32_t cp = codepointDecode(p);
                    append(result, cp);
                    p = codepointSkip(p);
                    ++i;
                }
            }

            s.swap(result);
        }

        static std::string replace(const std::string& s, const std::string& from, const std::string& to)
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
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                if (cp != target)
                    append(result, cp);
                p = codepointSkip(p);
            }

            return result;
        }

        static std::string removeCharacters(const std::string& s, const std::string& chars) {
            std::string result;
            const char* p = s.c_str();

            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                if (!contains(chars, cp))
                    append(result, cp);
                p = codepointSkip(p);
            }

            return result;
        }

        static std::string simplify(
            const std::string& s,
            const std::string& whatToSimplify = " \r\n\t",
            char32_t simpleOne = ' ')
        {
            std::string simplified;
            std::string trimmed = trim(s, whatToSimplify);
            const char* p = trimmed.c_str();

            bool inWhitespace = false;

            while (true)
            {
                char32_t cp = codepointDecode(p);
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
                p = codepointSkip(p);
            }

            return simplified;
        }


        static bool beginsWith(const std::string& s, const std::string& prefix)
        {
            // If prefix is longer in codepoints, cannot match
            if (length(prefix) > length(s))
                return false;

            // Compare just the prefix-sized portion
            std::size_t prefixLen = length(prefix);
            std::string slice = substring(s, 0, prefixLen);

            return slice == prefix;
        }

        static bool endsWith(const std::string& s, const std::string& suffix)
        {
            std::size_t sLen = length(s);
            std::size_t sufLen = length(suffix);

            if (sLen < sufLen)
                return false;

            // Take the last sufLen codepoints of s
            std::string slice = substring(s, sLen - sufLen, sufLen);
            return slice == suffix;
        }

        static bool beginsWith(const std::string& s, char32_t c)
        {
            if (s.empty()) return false;

            char32_t cp = codepointDecode(s.c_str());
            return cp == c;
        }

        static bool endsWith(const std::string& s, char32_t c)
        {
            std::size_t len = length(s);
            if (len == 0) return false;

            // Move to the last codepoint
            const char* p = s.c_str();
            for (std::size_t i = 0; i < len - 1; ++i)
                p = codepointSkip(p);

            return codepointDecode(p) == c;
        }

        static size_t segmentCount(const std::string& s, const std::string& separator)
        {
            if (s.empty())
                return 0;

            // Empty separator? Treat as 1 segment (or could throw)
            if (separator.empty())
                return 1;

            std::size_t sLen  = length(s);
            std::size_t sepLen = length(separator);

            // If separator longer than input → whole string is one segment
            if (sepLen > sLen)
                return 1;

            std::size_t occurrences = 0;

            // UTF-8 aware: walk codepoint by codepoint
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

            // If separator is empty: treat as a single segment
            if (separator.empty()) {
                return (index == 0) ? s : std::string{};
            }

            std::size_t sLen   = length(s);
            std::size_t sepLen = length(separator);

            // If separator longer than string, whole string is a single segment
            if (sepLen > sLen) {
                return (index == 0) ? s : std::string{};
            }

            std::size_t currentSegmentIndex = 0;
            std::size_t segmentStart = 0; // in codepoints

            std::size_t i = 0; // current position in codepoints
            while (i + sepLen <= sLen)
            {
                // Check if separator matches at position i (UTF-8 safe)
                if (substring(s, i, sepLen) == separator)
                {
                    // We reached the end of a segment [segmentStart, i)
                    if (currentSegmentIndex == index) {
                        return substring(s, segmentStart, i - segmentStart);
                    }

                    // Move to next segment
                    ++currentSegmentIndex;
                    segmentStart = i + sepLen;
                    i = segmentStart; // jump over the separator
                }
                else
                {
                    ++i; // move forward one codepoint
                }
            }

            // Handle last segment (from segmentStart to end of string)
            if (currentSegmentIndex == index) {
                return substring(s, segmentStart, sLen - segmentStart);
            }

            // Out of range
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
            std::size_t i = 0; // codepoint index
            std::size_t segmentStart = 0; // codepoint index

            while (i + sepLen <= sLen) {
                if (substring(s, i, sepLen) == separator) {
                    // segment [segmentStart, i)
                    parts.push_back(substring(s, segmentStart, i - segmentStart));
                    i += sepLen;
                    segmentStart = i;

                    // advance pointer by sepLen codepoints
                    for (std::size_t k = 0; k < sepLen; ++k)
                        p = codepointSkip(p);
                } else {
                    // move forward 1 codepoint
                    p = codepointSkip(p);
                    ++i;
                }
            }

            // tail segment
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
            std::size_t i = 0; // codepoint index
            std::size_t segmentStart = 0; // codepoint index

            while (i + sepLen <= sLen) {
                if (substring(s, i, sepLen) == separator) {
                    // raw segment [segmentStart, i)
                    std::string item = substring(s, segmentStart, i - segmentStart);
                    item = trim(item, whatToTrim);

                    if (keepEmptyItems || !item.empty())
                        parts.push_back(item);

                    i += sepLen;
                    segmentStart = i;

                    // advance pointer by sepLen codepoints
                    for (std::size_t k = 0; k < sepLen; ++k)
                        p = codepointSkip(p);
                } else {
                    p = codepointSkip(p);
                    ++i;
                }
            }

            // tail segment
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
            result.reserve(s.size()); // optimization: avoid reallocation

            const char* p = s.c_str();

            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp)
                    break;

                // ASCII uppercase range
                if (cp >= U'A' && cp <= U'Z')
                {
                    cp = (cp - U'A') + U'a';
                }

                append(result, cp);
                p = codepointSkip(p);
            }

            return result;
        }

        static size_t lastIndexOf(const std::string& s, char32_t c)
        {
            const char* p = s.c_str();
            size_t index = 0;
            size_t last = NOT_FOUND;   

            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp)
                    break;

                if (cp == c)
                    last = index;

                p = codepointSkip(p);
                ++index;
            }

            return last;
        }

        static size_t firstIndexOf(const std::string& s, char32_t c)
        {
            const char* p = s.c_str();
            size_t index = 0;

            while (true)
            {
                char32_t cp = codepointDecode(p);
                if (!cp) break;
                if (cp == c) return index;
                p = codepointSkip(p);
                ++index;
            }

            return NOT_FOUND;        // NOT FOUND
        }

        template <typename T>
        static T parse(const std::string& s, bool* parsedSuccessfully = nullptr)
        {
            if (s.empty()) {
                if (parsedSuccessfully) *parsedSuccessfully = false;
                return T {};
            }

            std::istringstream iss(s);
            iss >> std::ws; // skip leading whitespace

            T value{};
            iss >> value; // parse the number

            bool ok = !iss.fail();

            if (ok) {
                iss >> std::ws; // skip trailing whitespace
                // check: nothing except whitespace after the number
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

    };
}   



