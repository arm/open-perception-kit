/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

#define ASIO_STANDALONE
#include <asio.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtemplate-id-cdtor"
#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>
#pragma GCC diagnostic pop

#include "writer.h"

using OpkCommWsServer = websocketpp::server<websocketpp::config::asio>;
using OpkCommConnectionHdl = websocketpp::connection_hdl;

class WebSocketWriter : public Writer {
    uint16_t m_port;
    std::string m_endpoint;

    std::shared_ptr<OpkCommWsServer> m_ws;
    std::thread m_ws_thread;
    std::set<OpkCommConnectionHdl, std::owner_less<OpkCommConnectionHdl>> m_connections;
    mutable std::mutex m_connection_lock;

    bool validate(const OpkCommConnectionHdl &hdl);
    void on_open(const OpkCommConnectionHdl &hdl);
    void on_close(const OpkCommConnectionHdl &hdl);

  protected:
    bool io_open() override;
    void io_close() override;

    bool publish(const std::string &json_str) override;

  public:
    WebSocketWriter(_GstOpkComm *self, uint16_t port, std::string endpoint, size_t queue_size)
        : Writer(self, queue_size), m_port(port), m_endpoint(std::move(endpoint)) {}

    ~WebSocketWriter() override = default;
};
