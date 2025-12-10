#pragma once

#include "amp/Result.h"
#include "tl/expected.hpp"

#include <filesystem>
#include <string>
#include <fstream>

namespace amp { struct fs {

    static bool available(const std::string& path) {
        if(std::filesystem::exists(path)) return true;
        return false;
    }

    static amp::Result<uint64_t> fileSize(const std::string& path) {
        if(!available(path)) {
            return tl::unexpected(amp::Error(amp::ErrorFlag::FileNotFound, path));
        }
        return (uint64_t)std::filesystem::file_size(path);
    }

    static amp::Result<std::vector<uint8_t>> load(const std::string& path, bool returnEmptyIfNotFound = true) {
    
        if(false == available(path)) {
            if(returnEmptyIfNotFound) return std::vector<uint8_t>{};
            return tl::unexpected(amp::Error(amp::ErrorFlag::FileNotFound, path));
        }
    
    }

    static amp::Result<std::string> loadText(const std::string& path, bool returnEmptyIfNotFound = true) {

        if(false == available(path)) {
            if(returnEmptyIfNotFound) return "";
            return tl::unexpected(amp::Error(amp::ErrorFlag::FileNotFound, path));
        }

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        const auto size = f.tellg();
        std::string out(size, '\0');
        f.seekg(0);
        f.read(out.data(), size);
        
        size_t bomCheck = checkBom(out);
        if(bomCheck == 0 || bomCheck == 3) {
            if(bomCheck == 3) {
                out.erase(0, 3);
            }
        } else {
            return tl::unexpected(amp::Error(amp::ErrorFlag::InvalidData, fmt::format("Text file [{}] is not UTF8 data", path)));
        }

        return out;
    }

    // ---

    static size_t checkBom(const std::string& s) {
        if (s.size() >= 3 &&
            (unsigned char)s[0] == 0xEF &&
            (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF)
        return 3; // UTF-8

        if (s.size() >= 2 &&
            (unsigned char)s[0] == 0xFF &&
            (unsigned char)s[1] == 0xFE)
        return 2; // UTF-16 LE

        if (s.size() >= 2 &&
            (unsigned char)s[0] == 0xFE &&
            (unsigned char)s[1] == 0xFF)
        return 2; // UTF-16 BE

        if (s.size() >= 4 &&
            (unsigned char)s[0] == 0xFF &&
            (unsigned char)s[1] == 0xFE &&
            (unsigned char)s[2] == 0x00 &&
            (unsigned char)s[3] == 0x00)
        return 4; // UTF-32 LE

        if (s.size() >= 4 &&
            (unsigned char)s[0] == 0x00 &&
            (unsigned char)s[1] == 0x00 &&
            (unsigned char)s[2] == 0xFE &&
            (unsigned char)s[3] == 0xFF)
        return 4; // UTF-32 BE

        return 0; // No BOM
    }

};}


