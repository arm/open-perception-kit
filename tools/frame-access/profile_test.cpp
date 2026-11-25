/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../../development/elements/opkcomm/frame_access_profile.h"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

class FixedAllocator final : public flatbuffers::Allocator {
  public:
    explicit FixedAllocator(const size_t capacity) : storage(capacity) {}
    uint8_t *allocate(const size_t size) override {
        assert(!allocated && size <= storage.size());
        allocated = true;
        return storage.data();
    }
    void deallocate(uint8_t *, size_t) override {}
    uint8_t *reallocate_downward(uint8_t *, size_t, size_t, size_t, size_t) override {
        throw std::runtime_error("construction bound exceeded");
    }

  private:
    std::vector<uint8_t> storage;
    bool allocated = false;
};

void checkConstruction(const opk::frame_access::DeploymentProfile &profile,
                       const opk::frame_access::v1::PixelFormat format,
                       const uint32_t width,
                       const uint32_t height) {
    namespace wire = opk::frame_access::v1;
    const auto bounds = opk::frame_access::constructionBounds(profile, format, width, height);
    assert(bounds && bounds->responseSlots == 24);
    FixedAllocator allocator(bounds->constructionBytes);
    flatbuffers::FlatBufferBuilder builder(bounds->constructionBytes, &allocator);
    std::array<flatbuffers::Offset<wire::Plane>, 3> planes{};
    for (size_t i = 0; i < bounds->planeCount; ++i) {
        uint8_t *data = nullptr;
        const auto pixels = builder.CreateUninitializedVector(bounds->planeBytes[i], &data);
        std::memset(data, 0, bounds->planeBytes[i]);
        planes[i] = wire::CreatePlane(builder, pixels);
    }
    const auto planeVector = builder.CreateVector(planes.data(), bounds->planeCount);
    const auto frame = wire::CreateFrame(builder, format, width, height, planeVector);
    const auto sample = wire::CreateSampleInfo(builder, 0, uint64_t{0});
    // Opaque bytes exercise construction only, not nested FLWD validity.
    uint8_t *resultBytes = nullptr;
    const auto results = builder.CreateUninitializedVector(profile.maxResultsBytes, &resultBytes);
    std::memset(resultBytes, 0, profile.maxResultsBytes);
    const auto reply =
        wire::CreateFrameReply(builder, wire::ReplyStatus_Ok, sample, results, frame);
    builder.Finish(reply);
    flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
    assert(wire::VerifyFrameReplyBuffer(verifier));
    assert(builder.GetSize() <= bounds->constructionBytes);
    auto tooSmall = profile;
    tooSmall.maxConstructionBytes = bounds->constructionBytes - 1;
    assert(!opk::frame_access::constructionBounds(tooSmall, format, width, height));
    tooSmall = profile;
    tooSmall.maxImageBytes = bounds->imageBytes - 1;
    assert(!opk::frame_access::constructionBounds(tooSmall, format, width, height));
}

int main() {
    using opk::frame_access::constructionBounds;
    using opk::frame_access::DeploymentProfile;
    namespace wire = opk::frame_access::v1;
    // Test-only capacities, not deployment defaults.
    const DeploymentProfile profile{2, 2, 4, 1024, 257, 4096};
    for (const auto format : wire::EnumValuesPixelFormat()) {
        if (format == wire::PixelFormat_Unknown) {
            continue;
        }
        for (const uint32_t width : {1, 2, 3, 4, 5}) {
            for (const uint32_t height : {1, 2, 3, 4, 5}) {
                checkConstruction(profile, format, width, height);
            }
        }
    }
    // The measured 64 MiB candidate is a test input, not a production default.
    constexpr uint64_t mib = 1024 * 1024;
    const DeploymentProfile representative{2, 2, 4, 32 * mib, 64 * mib, 97 * mib};
    for (const auto format : wire::EnumValuesPixelFormat()) {
        if (format != wire::PixelFormat_Unknown) {
            checkConstruction(representative, format, 640, 480);
            checkConstruction(representative, format, 1920, 1080);
            checkConstruction(representative, format, 3840, 2160);
            checkConstruction(representative, format, 3839, 2159);
        }
    }
    FixedAllocator allocator(1024);
    flatbuffers::FlatBufferBuilder errorBuilder(1024, &allocator);
    const auto message = errorBuilder.CreateString(std::string(512, 'x'));
    errorBuilder.Finish(
        wire::CreateFrameReply(errorBuilder, wire::ReplyStatus_TooLarge, 0, 0, 0, message));
    assert(errorBuilder.GetSize() <= 1024);
    const auto oddI420 = constructionBounds(profile, wire::PixelFormat_I420, 3, 3);
    assert(oddI420 && oddI420->planeBytes == (std::array<uint64_t, 3>{9, 4, 4}));
    assert(constructionBounds(profile, wire::PixelFormat_YUY2, 3, 3)->imageBytes == 24);
    assert(!constructionBounds(profile, wire::PixelFormat_Unknown, 1, 1));
    assert(!constructionBounds(profile, wire::PixelFormat_BGRA8, 0, 1));
    assert(!constructionBounds(profile, wire::PixelFormat_BGRA8, UINT32_MAX, UINT32_MAX));
    auto invalid = profile;
    invalid.clients = 0;
    assert(!constructionBounds(invalid, wire::PixelFormat_BGRA8, 1, 1));
    invalid = profile;
    invalid.maxConstructionBytes = 1;
    assert(!constructionBounds(invalid, wire::PixelFormat_BGRA8, 1, 1));
    invalid = profile;
    invalid.maxResultsBytes = UINT64_MAX;
    assert(!constructionBounds(invalid, wire::PixelFormat_BGRA8, 1, 1));
    invalid = profile;
    invalid.clients = UINT64_MAX;
    assert(!constructionBounds(invalid, wire::PixelFormat_BGRA8, 1, 1));
}
