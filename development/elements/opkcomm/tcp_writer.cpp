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

static SendResult send_all(int fileDescriptor, const std::string &data, size_t &offset) {
    while (offset < data.size()) {
        ssize_t bytesSent =
            ::send(fileDescriptor, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
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
        struct addrinfo *rawAddressLookupInformation = nullptr;
        const int gai_rc =
            getaddrinfo(m_host.c_str(), port_str.c_str(), &hints, &rawAddressLookupInformation);
        if (gai_rc != 0) {
            GST_INFO_OBJECT(self(),
                            "Failed to resolve TCP bind address '%s:%u': %s",
                            m_host.c_str(),
                            m_port,
                            gai_strerror(gai_rc));
            throw TcpError("getaddrinfo failed");
        }

        // freeaddrinfo is a system C library function declared in <netdb.h>.
        std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> addressLookupInformation(
            rawAddressLookupInformation, freeaddrinfo);

        for (auto *address = addressLookupInformation.get(); address != nullptr;
             address = address->ai_next) {
            const int fileDescriptor =
                socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fileDescriptor < 0) {
                continue;
            }

            int reuse = 1;
            (void)setsockopt(fileDescriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            if (bind(fileDescriptor, address->ai_addr, address->ai_addrlen) == 0 &&
                listen(fileDescriptor, 1) == 0) {
                int flags = fcntl(fileDescriptor, F_GETFL, 0);
                if (flags >= 0) {
                    (void)fcntl(fileDescriptor, F_SETFL, flags | O_NONBLOCK);
                }

                m_listen_fd = fileDescriptor;
                GST_INFO_OBJECT(self(),
                                "Listening for TCP metadata clients on '%s:%u'",
                                m_host.c_str(),
                                m_port);
                return true;
            }

            close(fileDescriptor);
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
    int fileDescriptor = -1;

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
        fileDescriptor = dup(m_client_fd);

    return fileDescriptor;
}

bool TcpWriter::publish(const std::string &json_str) {
    bool success = false;

    if (auto fileDescriptor = check_client(); fileDescriptor >= 0) {
        SendResult send_result = SendResult::Sent;
        bool dropped_frame = false;

        if (!m_pending.empty()) {
            send_result = send_all(fileDescriptor, m_pending, m_pending_offset);
            if (send_result == SendResult::Sent) {
                clear_pending();
            } else {
                dropped_frame = true;
            }
        }

        if (send_result == SendResult::Sent) {
            size_t offset = 0;
            send_result = send_all(fileDescriptor, json_str, offset);
            if (send_result == SendResult::Backpressure) {
                m_pending = json_str;
                m_pending_offset = offset;
            }
        }

        const int send_errno = errno;
        close(fileDescriptor);

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
