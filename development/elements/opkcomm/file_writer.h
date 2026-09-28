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

#pragma once

#include <mutex>

#include <sys/stat.h>

#include "writer.h"

class FileWriter : public Writer {
    std::string m_file_name;

    int m_fd = -1;

    std::recursive_mutex m_io_lock;

    int check_open();

    bool io_open_existing(const struct stat &st);
    bool io_create();

  protected:
    bool io_open() override;
    void io_close() override;

    bool publish(const std::string &json_str) override;

  public:
    explicit FileWriter(_GstOpkComm *self, const std::string &file_name, size_t queue_size)
        : Writer(self, queue_size), m_file_name(file_name) {}

    ~FileWriter() override = default;
};
