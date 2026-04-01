/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <zlib.h>

#include <nlohmann/json.hpp>

#include "Perception.h"
#include "amp/Bitmap.h"

namespace amp {

using nlohmann::json;

// ---------- Helpers ----------

std::string base64_encode_safe(std::span<const uint8_t> data);

// compress data with zlib (compress2) at 'level' (0-9).
// Returns true on success and fills out with the compressed bytes.
bool zlib_compress(std::span<const uint8_t> data,
                   std::vector<uint8_t> &out,
                   int level = Z_BEST_SPEED);

void to_json(json &j, const Perception::Object &o);

template <class T> static inline void add_object_fields(json &j, const T &x) {
    static_assert(std::is_base_of<Perception::Object, T>::value,
                  "T has to be derived from Perception::Object");
    // T derives from
    j["uuid"] = x.uuid;
    j["parentUuid"] = x.parentUuid;
    j["creationTsNs"] = x.creationTsNs;
}

// ---------- Leaf structs ----------

void to_json(json &j, const Perception::VideoFrame &v);
void to_json(json &j, const Perception::AudioFrame &a);
void to_json(json &j, const Perception::Rect &r);
void to_json(json &j, const Perception::Classification::Candidate &c);
void to_json(json &j, const Perception::Classification &c);
void to_json(json &j, const Perception::YawPitch &yp);
void to_json(json &j, const Perception::LocalizedText &lt);

// ---------- Bitmap strategy ----------

// returns a nlohmann::json object describing the bitmap and containing base64'd compressed pixels
nlohmann::json bitmap_to_json_zlib_b64(const amp::Bitmap &b, int zlib_level = Z_BEST_COMPRESSION);

void to_json(json &j, const amp::Bitmap &b);
void to_json(json &j, const Perception::SegmentationMap &sm);
void to_json(json &j, const Perception::ObjectEmbedding &oe);
void to_json(json &j, const Perception::TrackTrace::Point &p);
void to_json(json &j, const Perception::TrackTrace &tt);

// ---------- Variant (Detection) ----------

void to_json(json &j, const Perception::Detection &d);

// ---------- Layer + Perception ----------

void to_json(json &j, const Perception::Layer &l);
void to_json(json &j, const Perception &p);

} // namespace amp
