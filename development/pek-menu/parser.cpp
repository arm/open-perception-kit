/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <stdexcept>

#include "parser.hpp"

static const char *getenv_raw(const std::string &name) {
    return std::getenv(name.c_str()); // may return nullptr
}

static bool is_var_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
static bool is_var_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

ExecArgs tokenize_and_expand_argv(const std::string &s) {
    enum class Quote { None, Single, Double };

    ExecArgs out;
    std::string cur = "gst-launch-1.0";
    Quote q = Quote::None;

    auto flush = [&]() {
        if (!cur.empty()) {
            out.storage.push_back(cur);
            cur.clear();
        }
    };

    flush();

    const size_t n = s.size();
    size_t i = 0;

    while (i < n) {
        char c = s[i];

        if (q == Quote::None && std::isspace(static_cast<unsigned char>(c))) {
            flush();
            while (i < n && std::isspace(static_cast<unsigned char>(s[i])))
                i++;
            continue;
        }

        if (q == Quote::None && (c == '"' || c == '\'')) {
            q = (c == '"') ? Quote::Double : Quote::Single;
            i++;
            continue;
        }
        if (q == Quote::Double && c == '"') {
            q = Quote::None;
            i++;
            continue;
        }
        if (q == Quote::Single && c == '\'') {
            q = Quote::None;
            i++;
            continue;
        }

        if (c == '\\') {
            if (i + 1 < n) {
                cur.push_back(s[i + 1]);
                i += 2;
                continue;
            }
            cur.push_back('\\');
            i++;
            continue;
        }

        if (c == '$' && i + 1 < n && s[i + 1] == '{') {
            size_t j = i + 2; // after ${
            if (j < n && is_var_start(s[j])) {
                size_t k = j + 1;
                while (k < n && is_var_char(s[k]))
                    k++;

                std::string varname = s.substr(j, k - j);
                const char *raw = getenv_raw(varname);
                std::string value = raw ? std::string(raw) : std::string();

                // ${VAR}
                if (k < n && s[k] == '}') {
                    cur += value;
                    i = k + 1;
                    continue;
                }

                // ${VAR:-default}
                if (k + 2 < n && s[k] == ':' && s[k + 1] == '-') {
                    size_t d = k + 2;
                    size_t end = s.find('}', d);
                    if (end == std::string::npos)
                        throw std::runtime_error("Unterminated ${VAR:-default}");

                    if (value.empty())
                        cur += s.substr(d, end - d);
                    else
                        cur += value;

                    i = end + 1;
                    continue;
                }

                // ${VAR?error}
                if (k + 1 < n && s[k] == '?') {
                    size_t d = k + 1;
                    size_t end = s.find('}', d);
                    if (end == std::string::npos)
                        throw std::runtime_error("Unterminated ${VAR?error}");

                    if (value.empty()) {
                        std::string msg = s.substr(d, end - d);
                        if (msg.empty())
                            msg = "Environment variable " + varname + " is required";
                        throw std::runtime_error(msg);
                    }

                    cur += value;
                    i = end + 1;
                    continue;
                }
            }

            // malformed ${...} → treat '$' literally
            cur.push_back('$');
            i++;
            continue;
        }
        cur.push_back(c);
        i++;
    }

    if (q != Quote::None) {
        throw std::runtime_error("Unterminated quote in pipeline string");
    }

    flush();

    // Build argv pointers into storage.
    out.argv.reserve(out.storage.size() + 1);
    for (auto &str : out.storage) {
        // C++17: data() returns char* for non-const std::string
        out.argv.push_back(str.data());
    }
    out.argv.push_back(nullptr);

    return out;
}

std::string trim(std::string trimmed_str) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    trimmed_str.erase(trimmed_str.begin(),
                      std::find_if(trimmed_str.begin(), trimmed_str.end(), not_space));
    trimmed_str.erase(std::find_if(trimmed_str.rbegin(), trimmed_str.rend(), not_space).base(),
                      trimmed_str.end());
    return trimmed_str;
}
