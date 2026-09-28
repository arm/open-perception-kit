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

#include <cstdint>
#include <mutex>
#include <string>

#include "writer.h"

class TcpWriter : public Writer {
    std::string m_host;
    uint16_t m_port;

    int m_listen_fd = -1;
    int m_client_fd = -1;

    std::string m_pending;
    size_t m_pending_offset = 0;

    std::recursive_mutex m_io_lock;

    int check_client();
    void close_client();
    void clear_pending();

  protected:
    bool io_open() override;
    void io_close() override;

    bool publish(const std::string &json_str) override;

  public:
    explicit TcpWriter(_GstOpkComm *self, std::string host, uint16_t port, size_t queue_size)
        : Writer(self, queue_size), m_host(std::move(host)), m_port(port) {}

    ~TcpWriter() override = default;
};
