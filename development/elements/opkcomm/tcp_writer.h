/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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
