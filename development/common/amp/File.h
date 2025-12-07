#pragma once

#include "amp/Result.h"

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
            return tl::unexpected(amp::Error(amp::ResultFlag::FileNotFound, path));
        }
        return (uint64_t)std::filesystem::file_size(path);
    }

    static std::string loadText(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        const auto size = f.tellg();
        std::string out(size, '\0');
        f.seekg(0);
        f.read(out.data(), size);
        return out;
    }

};}


