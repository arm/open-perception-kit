/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

/**
 * @file Common.cpp
 * @brief Backend-neutral media IO primitive implementations.
 */

#include "mediaio/Common.h"

namespace opk::mediaio {

DmaBufSync::DmaBufSync(int acquireFenceFd, int releaseFenceFd) noexcept
    : acquireFd(acquireFenceFd), releaseFd(releaseFenceFd) {}

int DmaBufSync::acquireFenceFd() const noexcept {
    return acquireFd;
}

int DmaBufSync::releaseFenceFd() const noexcept {
    return releaseFd;
}

bool DmaBufSync::hasAcquireFence() const noexcept {
    return acquireFd >= 0;
}

bool DmaBufSync::hasReleaseFence() const noexcept {
    return releaseFd >= 0;
}

DataView DataView::host(void *data,
                        size_t byteSize,
                        uint32_t strideBytes,
                        opk::AccessMode accessMode,
                        size_t offsetBytes) noexcept {
    DataView view;
    view.memory = opk::MemoryType::Host;
    view.access = accessMode;
    view.hostData = data;
    view.dataByteSize = byteSize;
    view.dataStrideBytes = strideBytes;
    view.dataOffset = offsetBytes;
    return view;
}

DataView DataView::dmaBuf(int fd,
                          size_t byteSize,
                          uint32_t strideBytes,
                          size_t offsetBytes,
                          opk::AccessMode accessMode,
                          DmaBufSync sync) noexcept {
    DataView view;
    view.memory = opk::MemoryType::DmaBuf;
    view.access = accessMode;
    view.dmaBufFd = fd;
    view.dataByteSize = byteSize;
    view.dataStrideBytes = strideBytes;
    view.dataOffset = offsetBytes;
    view.dmaBufSync = sync;
    return view;
}

opk::MemoryType DataView::memoryType() const noexcept {
    return memory;
}

opk::AccessMode DataView::accessMode() const noexcept {
    return access;
}

const void *DataView::data() const noexcept {
    return hostData;
}

void *DataView::mutableData() const noexcept {
    return canWrite() ? hostData : nullptr;
}

int DataView::fd() const noexcept {
    return dmaBufFd;
}

size_t DataView::offset() const noexcept {
    return dataOffset;
}

size_t DataView::byteSize() const noexcept {
    return dataByteSize;
}

uint32_t DataView::strideBytes() const noexcept {
    return dataStrideBytes;
}

const DmaBufSync &DataView::sync() const noexcept {
    return dmaBufSync;
}

bool DataView::hasHostData() const noexcept {
    return memory == opk::MemoryType::Host && hostData != nullptr;
}

bool DataView::hasDmaBuf() const noexcept {
    return memory == opk::MemoryType::DmaBuf && dmaBufFd >= 0;
}

bool DataView::canRead() const noexcept {
    return access == opk::AccessMode::Read || access == opk::AccessMode::ReadWrite;
}

bool DataView::canWrite() const noexcept {
    return access == opk::AccessMode::Write || access == opk::AccessMode::ReadWrite;
}

} // namespace opk::mediaio
