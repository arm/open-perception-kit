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

#pragma once

#include "frame_access_profile.h"
#include "mediaio/VideoFrame.h"
#include "open_perception_kit.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace opk::frame_access {

class Server {
  public:
    using MapFrame = std::function<std::shared_ptr<mediaio::VideoFrame>()>;
    using ReportError = std::function<void(std::string_view)>;

    Server(const DeploymentProfile &profile,
           std::string_view endpoint,
           std::string_view runtimePath,
           std::string_view schemaPath,
           v1::PixelFormat format,
           uint32_t width,
           uint32_t height,
           ReportError reportError);
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    bool setLayout(v1::PixelFormat format, uint32_t width, uint32_t height);
    void serve(const open_perception_kit::container::envelope *results,
               uint64_t sequence,
               std::optional<uint64_t> pts,
               const MapFrame &mapFrame);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace opk::frame_access
