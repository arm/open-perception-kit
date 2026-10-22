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

#include "mediaio/VideoFrame.h"
#include "opk/ImageOpDesc.h"

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace opk::gles {

/** Owns a private GBM display and its render-device handle. */
class LinuxDisplay {
  public:
    explicit LinuxDisplay(const std::string &path);
    ~LinuxDisplay();
    LinuxDisplay(const LinuxDisplay &) = delete;
    LinuxDisplay &operator=(const LinuxDisplay &) = delete;
    EGLDisplay get() const;
    static std::vector<std::string> renderDevices();

  private:
    struct State;
    std::unique_ptr<State> state;
};

/** Keeps imported plane images alive until the caller has finished GPU reads. */
class ImportedVideo {
  public:
    explicit ImportedVideo(const EGLDisplay display);
    ~ImportedVideo();
    ImportedVideo(const ImportedVideo &) = delete;
    ImportedVideo &operator=(const ImportedVideo &) = delete;
    void bind() const;

    /** Returns nullptr when the driver cannot import this linear DMA-BUF layout. */
    static std::unique_ptr<ImportedVideo> tryImport(const mediaio::VideoFrame &frame,
                                                    const EGLDisplay display);

  private:
    EGLDisplay display;
    std::array<EGLImage, MaxImagePlaneCount> images{};
    std::array<GLuint, MaxImagePlaneCount> textures{};
    size_t planeCount{0};
};

} // namespace opk::gles
