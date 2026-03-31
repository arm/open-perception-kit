/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "PerceptionSerializer.h"

#include <cstdint>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

namespace amp {

using nlohmann::json;

std::string base64_encode_safe(std::span<const uint8_t> data) {
    static constexpr std::array<char,65> table = std::to_array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t v =
            (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
        out.push_back(table.at((v >> 18) & 0x3F));
        out.push_back(table.at((v >> 12) & 0x3F));
        out.push_back(table.at((v >> 6) & 0x3F));
        out.push_back(table.at(v & 0x3F));
        i += 3;
    }

    if (size_t rem = data.size() - i; rem == 1) {
        uint32_t v = uint32_t(data[i]) << 16;
        out.push_back(table.at((v >> 18) & 0x3F));
        out.push_back(table.at((v >> 12) & 0x3F));
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out.push_back(table.at((v >> 18) & 0x3F));
        out.push_back(table.at((v >> 12) & 0x3F));
        out.push_back(table.at((v >> 6) & 0x3F));
        out.push_back('=');
    }
    return out;
}

// compress data with zlib (compress2) at 'level' (0-9).
// Returns true on success and fills out with the compressed bytes.
bool zlib_compress(std::span<const uint8_t> data, std::vector<uint8_t> &out, int level) {
    if (data.empty()) {
        out.clear();
        return true;
    }

    auto bound = compressBound(data.size());
    out.resize(bound);

    if (int rc = compress2(out.data(), &bound, data.data(), data.size(), level); rc != Z_OK) {
        out.clear();
        return false;
    }
    out.resize(bound);
    return true;
}

void to_json(json &j, const Perception::Object &o) {
    j = json{
        {"uuid", o.uuid},
        {"parentUuid", o.parentUuid},
        {"creationTsNs", o.creationTsNs},
    };
}

void to_json(json &j, const Perception::VideoFrame &v) {
    j = json{};
    add_object_fields(j, v);
    j["originalWidth"] = v.originalWidth;
    j["originalHeight"] = v.originalHeight;
    j["sourceCropLeft"] = v.sourceCropLeft;
    j["sourceCropRight"] = v.sourceCropRight;
    j["sourceCropTop"] = v.sourceCropTop;
    j["sourceCropBottom"] = v.sourceCropBottom;
    j["letterboxLeft"] = v.letterboxLeft;
    j["letterboxRight"] = v.letterboxRight;
    j["letterboxTop"] = v.letterboxTop;
    j["letterboxBottom"] = v.letterboxBottom;
}

void to_json(json &j, const Perception::AudioFrame &a) {
    j = json{};
    add_object_fields(j, a);
    j["originalChannels"] = a.originalChannels;
    j["originalFrequency"] = a.originalFrequency;
    j["originalSampleCount"] = a.originalSampleCount;
}

void to_json(json &j, const Perception::Rect &r) {
    j = json{};
    add_object_fields(j, r);
    j["x"] = r.x;
    j["y"] = r.y;
    j["width"] = r.width;
    j["height"] = r.height;
    j["confidence"] = r.confidence;
    j["classId"] = r.classId;
    if (!r.text.empty())
        j["text"] = r.text;
}

void to_json(json &j, const Perception::Classification::Candidate &c) {
    j = json{
        {"confidence", c.confidence},
        {"classId", c.classId},
        {"text", c.text},
        {"x", c.x},
        {"y", c.y},
        {"w", c.w},
        {"h", c.h},
    };
}

void to_json(json &j, const Perception::Classification &c) {
    j = json{};
    add_object_fields(j, c);
    j["candidates"] = c.candidates;
}

void to_json(json &j, const Perception::YawPitch &yp) {
    j = json{};
    add_object_fields(j, yp);
    j["confidence"] = yp.confidence;
    j["yaw"] = yp.yaw;
    j["pitch"] = yp.pitch;
}

void to_json(json &j, const Perception::LocalizedText &lt) {
    j = json{};
    add_object_fields(j, lt);
    j["x"] = lt.x;
    j["y"] = lt.y;
    j["w"] = lt.w;
    j["h"] = lt.h;
    j["text"] = lt.text;
}

// ---------- Bitmap strategy ----------

// returns a nlohmann::json object describing the bitmap and containing base64'd compressed pixels
nlohmann::json bitmap_to_json_zlib_b64(const amp::Bitmap &b, int zlib_level) {
    using nlohmann::json;
    json j;
    j["width"] = b.getWidth();
    j["height"] = b.getHeight();
    j["type"] = (b.getType() == amp::Bitmap::Type::Uint8 ? "Uint8" : "Uint32");

    auto pixels = b.getPixels();
    if (pixels.empty()) {
        j["encoding"] = nullptr;
        return j;
    }

    if (pixels.empty()) {
        j["encoding"] = nullptr;
        return j;
    }

    // compress
    std::vector<uint8_t> compressed;

    if (bool ok = zlib_compress(pixels, compressed, zlib_level); !ok || compressed.empty()) {
        // compression failed: fallback to base64 of raw
        std::string raw_b64 = base64_encode_safe(pixels);
        j["encoding"] = "base64";
        j["raw_size"] = pixels.size();
        j["compressed_size"] = pixels.size();
        j["data_b64"] = raw_b64;
        return j;
    }

    // base64 the compressed bytes
    std::string b64 = base64_encode_safe(compressed);
    j["encoding"] = "zlib+base64";
    j["raw_size"] = pixels.size();
    j["compressed_size"] = compressed.size();
    j["data_b64"] = b64;
    return j;
}

void to_json(json &j, const amp::Bitmap &b) {
    j = bitmap_to_json_zlib_b64(b);
}

void to_json(json &j, const Perception::SegmentationMap &sm) {
    j = json{};
    add_object_fields(j, sm);
    j["bitmap"] = sm.bitmap;
}

void to_json(json &j, const Perception::ObjectEmbedding &oe) {
    j = json{};
    add_object_fields(j, oe);
    j["values"] = oe.values;
}

void to_json(json &j, const Perception::TrackTrace::Point &p) {
    j = json{};
    j["x"] = p.x;
    j["y"] = p.y;
}

void to_json(json &j, const Perception::TrackTrace &tt) {
    j = json{};
    add_object_fields(j, tt);
    j["trackId"] = tt.trackId;
    j["points"] = tt.points;
}

// ---------- Variant (Detection) ----------

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void to_json(json &j, const Perception::Detection &d) {
    std::visit(
        overloaded{
            [&j](const Perception::Rect &v) { j = json{{"type", "Rect"}, {"data", v}}; },
            [&j](const Perception::YawPitch &v) { j = json{{"type", "YawPitch"}, {"data", v}}; },
            [&j](const Perception::LocalizedText &v) {
                j = json{{"type", "LocalizedText"}, {"data", v}};
            },
            [&j](const Perception::SegmentationMap &v) {
                j = json{{"type", "SegmentationMap"}, {"data", v}};
            },
            [&j](const Perception::VideoFrame &v) {
                j = json{{"type", "VideoFrame"}, {"data", v}};
            },
            [&j](const Perception::Classification &v) {
                j = json{{"type", "Classification"}, {"data", v}};
            },
            [&j](const Perception::AudioFrame &v) {
                j = json{{"type", "AudioFrame"}, {"data", v}};
            },
            [&j](const Perception::ObjectEmbedding &v) {
                j = json{{"type", "ObjectEmbedding"}, {"data", v}};
            },
            [&j](const Perception::TrackTrace &v) {
                j = json{{"type", "TrackTrace"}, {"data", v}};
            }},
        d);
}

// ---------- Layer + Perception ----------

void to_json(json &j, const Perception::Layer &l) {
    j = json{
        {"engine", l.engine},
        {"model", l.model},
        {"tags", l.tags},
        {"labelFamily", l.labelFamily},
        {"contentType", l.contentType},
        {"detections", l.detections},
        {"infer-id", l.inferElementId},
    };
}

void to_json(json &j, const Perception &p) {
    j = json{
        {"perfdata", p.perfdata},
        {"layers", p.layers},
    };
}
} // namespace amp
