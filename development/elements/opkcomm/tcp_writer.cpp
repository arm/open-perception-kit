/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "tcp_writer.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>

#include <glib.h>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gst/gst.h>

class TcpError : public std::runtime_error {
  public:
    explicit TcpError(const std::string &message) : std::runtime_error(message) {}
};

enum class SendResult {
    Sent,
    Backpressure,
    Disconnected,
    Error,
};

static SendResult send_all(int descriptor, const std::string &data, size_t &offset) {
    while (offset < data.size()) {
        ssize_t bytesSent =
            ::send(descriptor, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (bytesSent > 0) {
            offset += static_cast<size_t>(bytesSent);
            continue;
        }
        if (bytesSent < 0 && errno == EINTR)
            continue;

        if (bytesSent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return SendResult::Backpressure;

        if (bytesSent < 0 && (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN))
            return SendResult::Disconnected;

        return SendResult::Error;
    }

    return SendResult::Sent;
}

bool TcpWriter::io_open() {
    std::scoped_lock lock(m_io_lock);

    try {
        if (m_host.empty()) {
            GST_WARNING_OBJECT(self(), "tcp-host is not set");
            throw TcpError("tcp-host is not set");
        }
        if (m_port == 0) {
            GST_WARNING_OBJECT(self(), "tcp-port is not set");
            throw TcpError("tcp-port is not set");
        }

        struct addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_PASSIVE;

        const auto port_str = std::to_string(m_port);
        struct addrinfo *result_raw = nullptr;
        const int gai_rc = getaddrinfo(m_host.c_str(), port_str.c_str(), &hints, &result_raw);
        if (gai_rc != 0) {
            GST_INFO_OBJECT(self(),
                            "Failed to resolve TCP bind address '%s:%u': %s",
                            m_host.c_str(),
                            m_port,
                            gai_strerror(gai_rc));
            throw TcpError("getaddrinfo failed");
        }

        std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> result(result_raw, freeaddrinfo);

        for (auto *address = result.get(); address != nullptr; address = address->ai_next) {
            int descriptor = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (descriptor < 0) {
                continue;
            }

            int reuse = 1;
            (void)setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            if (bind(descriptor, address->ai_addr, address->ai_addrlen) == 0 &&
                listen(descriptor, 1) == 0) {
                int flags = fcntl(descriptor, F_GETFL, 0);
                if (flags >= 0) {
                    (void)fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
                }

                m_listen_fd = descriptor;
                GST_INFO_OBJECT(self(),
                                "Listening for TCP metadata clients on '%s:%u'",
                                m_host.c_str(),
                                m_port);
                return true;
            }

            close(descriptor);
        }

        GST_INFO_OBJECT(self(),
                        "Failed to bind TCP metadata server on '%s:%u': %s",
                        m_host.c_str(),
                        m_port,
                        g_strerror(errno));
        throw TcpError("bind/listen failed");

    } catch (const TcpError &) {
        return false;
    }
}

void TcpWriter::close_client() {
    if (m_client_fd >= 0) {
        close(m_client_fd);
        m_client_fd = -1;
    }
    clear_pending();
}

void TcpWriter::clear_pending() {
    m_pending.clear();
    m_pending_offset = 0;
}

void TcpWriter::io_close() {
    std::scoped_lock lock(m_io_lock);

    close_client();

    if (m_listen_fd >= 0) {
        close(m_listen_fd);
        m_listen_fd = -1;
    }
}

int TcpWriter::check_client() {
    int descriptor = -1;

    std::scoped_lock lock(m_io_lock);

    if (m_listen_fd < 0)
        io_open();

    if (m_client_fd < 0 && m_listen_fd >= 0) {
        int accepted_fd = accept(m_listen_fd, nullptr, nullptr);
        if (accepted_fd >= 0) {
            int flags = fcntl(accepted_fd, F_GETFL, 0);
            if (flags >= 0) {
                (void)fcntl(accepted_fd, F_SETFL, flags | O_NONBLOCK);
            }
            m_client_fd = accepted_fd;
            GST_INFO_OBJECT(
                self(), "Accepted TCP metadata client on '%s:%u'", m_host.c_str(), m_port);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            GST_DEBUG_OBJECT(self(), "TCP accept error: %s", g_strerror(errno));
        }
    }

    if (m_client_fd >= 0)
        descriptor = dup(m_client_fd);

    return descriptor;
}

bool TcpWriter::publish(const std::string &record) {
    bool success = false;

    if (auto descriptor = check_client(); descriptor >= 0) {
        SendResult send_result = SendResult::Sent;
        bool dropped_frame = false;

        if (!m_pending.empty()) {
            send_result = send_all(descriptor, m_pending, m_pending_offset);
            if (send_result == SendResult::Sent) {
                clear_pending();
            } else {
                dropped_frame = true;
            }
        }

        if (send_result == SendResult::Sent) {
            size_t offset = 0;
            send_result = send_all(descriptor, record, offset);
            if (send_result == SendResult::Backpressure) {
                m_pending = record;
                m_pending_offset = offset;
            }
        }

        const int send_errno = errno;
        close(descriptor);

        if (send_result == SendResult::Sent) {
            success = true;
        } else if (send_result == SendResult::Backpressure) {
            if (dropped_frame) {
                GST_DEBUG_OBJECT(self(),
                                 "TCP metadata client on '%s:%u' backpressured; dropping frame",
                                 m_host.c_str(),
                                 m_port);
            } else {
                GST_DEBUG_OBJECT(self(),
                                 "TCP metadata client on '%s:%u' backpressured; deferring frame",
                                 m_host.c_str(),
                                 m_port);
            }
        } else if (send_result == SendResult::Disconnected) {
            GST_INFO_OBJECT(
                self(), "TCP metadata client on '%s:%u' disconnected", m_host.c_str(), m_port);
            std::scoped_lock lock(m_io_lock);
            close_client();
        } else {
            GST_DEBUG_OBJECT(self(), "TCP write error: %s", g_strerror(send_errno));
        }
    }

    return success;
}
