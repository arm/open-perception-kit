/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/String.h"
#include <algorithm>
#include <string>
#include <vector>

namespace amp {

struct Tags {
    static bool hasTag(const std::string &tags, const std::string &tag) {
        return hasTag(std::string_view(tags), std::string_view(tag));
    }

    static void addTag(std::string &tags, const std::string &tag) {
        if (tag.empty())
            return;
        if (hasTag(tags, tag))
            return;

        if (!tags.empty())
            tags += ";";
        tags += tag;
    }

    static void removeTag(std::string &tags, const std::string &tag) {
        if (tags.empty() || tag.empty())
            return;

        auto list = amp::utf8::split(tags, ";");

        std::string newTags;
        for (const auto &t : list) {
            if (t == tag)
                continue;
            if (!newTags.empty())
                newTags += ";";
            newTags += t;
        }

        tags = std::move(newTags);
    }

  private:
    static bool hasTag(std::string_view tags, std::string_view tag) {
        if (tags.empty() || tag.empty())
            return false;

        size_t i = 0;
        while (i < tags.size()) {
            // find end of current token
            size_t j = tags.find(';', i);
            if (j == std::string_view::npos)
                j = tags.size();

            // token is [i, j)
            if ((j - i) == tag.size() && tags.compare(i, tag.size(), tag) == 0) {
                return true;
            }

            i = j + 1;
        }
        return false;
    }
};

} // namespace amp