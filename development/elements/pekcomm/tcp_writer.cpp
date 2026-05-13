/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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
    explicit TcpError(const std::string &err) : std::runtime_error(err) {}
};

static bool send_all(int fd, const std::string &data) {
    size_t off = 0;

    while (off < data.size()) {
        ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += size_t(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;

        return false;
    }

    return true;
}

bool TcpWriter::io_open() {
    std::lock_guard g(m_io_lock);

    try {
        if (m_host.empty()) {
            GST_WARNING_OBJECT(self(), "tcp-host is not set");
            throw TcpError("tcp-host is not set");
        }
        if (m_port == 0) {
            GST_WARNING_OBJECT(self(), "tcp-port is not set");
            throw TcpError("tcp-port is not set");
        }

        struct addrinfo hints {};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_PASSIVE;

        const auto port_str = std::to_string(m_port);
        struct addrinfo *result_raw = nullptr;
        const int gai_rc = getaddrinfo(m_host.c_str(), port_str.c_str(), &hints, &result_raw);
        if (gai_rc != 0) {
            GST_INFO_OBJECT(
                self(), "Failed to resolve TCP bind address '%s:%u': %s", m_host.c_str(), m_port, gai_strerror(gai_rc));
            throw TcpError("getaddrinfo failed");
        }

        std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> result(result_raw, freeaddrinfo);

        for (auto *rp = result.get(); rp != nullptr; rp = rp->ai_next) {
            int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) {
                continue;
            }

            int reuse = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(fd, 1) == 0) {
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags >= 0) {
                    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                }

                m_listen_fd = fd;
                GST_INFO_OBJECT(self(), "Listening for TCP metadata clients on '%s:%u'", m_host.c_str(), m_port);
                return true;
            }

            close(fd);
        }

        GST_INFO_OBJECT(
            self(), "Failed to bind TCP metadata server on '%s:%u': %s", m_host.c_str(), m_port, g_strerror(errno));
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
}

void TcpWriter::io_close() {
    std::lock_guard g(m_io_lock);

    close_client();

    if (m_listen_fd >= 0) {
        close(m_listen_fd);
        m_listen_fd = -1;
    }
}

int TcpWriter::check_client() {
    int fd = -1;

    std::lock_guard g(m_io_lock);

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
            GST_INFO_OBJECT(self(), "Accepted TCP metadata client on '%s:%u'", m_host.c_str(), m_port);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            GST_DEBUG_OBJECT(self(), "TCP accept error: %s", g_strerror(errno));
        }
    }

    if (m_client_fd >= 0)
        fd = dup(m_client_fd);

    return fd;
}

bool TcpWriter::publish(const std::string &json_str) {
    bool ret = false;

    if (auto fd = check_client(); fd >= 0) {
        const bool ok = send_all(fd, json_str);
        close(fd);

        if (!ok) {
            if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN || errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                GST_INFO_OBJECT(
                    self(), "TCP metadata client on '%s:%u' disconnected", m_host.c_str(), m_port);
                std::lock_guard g(m_io_lock);
                close_client();
            } else {
                GST_DEBUG_OBJECT(self(), "TCP write error: %s", g_strerror(errno));
            }
        } else {
            ret = true;
        }
    }

    return ret;
}
