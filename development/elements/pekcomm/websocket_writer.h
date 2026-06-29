/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
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

using PekCommWsServer = websocketpp::server<websocketpp::config::asio>;
using PekCommConnectionHdl = websocketpp::connection_hdl;

class WebSocketWriter : public Writer {
    uint16_t m_port;
    std::string m_endpoint;

    std::shared_ptr<PekCommWsServer> m_ws;
    std::thread m_ws_thread;
    std::set<PekCommConnectionHdl, std::owner_less<PekCommConnectionHdl>> m_connections;
    mutable std::mutex m_connection_lock;

    bool validate(PekCommConnectionHdl hdl);
    void on_open(PekCommConnectionHdl hdl);
    void on_close(PekCommConnectionHdl hdl);

  protected:
    bool io_open() override;
    void io_close() override;

    bool publish(const std::string &json_str) override;

  public:
    WebSocketWriter(_GstPekComm *self, uint16_t port, std::string endpoint, size_t queue_size)
        : Writer(self, queue_size), m_port(port), m_endpoint(std::move(endpoint)) {}

    ~WebSocketWriter() override = default;
};
