/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "websocket_writer.h"

#include <utility>

#include <gst/gst.h>

bool WebSocketWriter::validate(const PekCommConnectionHdl &hdl) {
    if (!m_ws) {
        return false;
    }

    auto con = m_ws->get_con_from_hdl(hdl);
    const auto resource = con->get_resource();
    const bool ok = resource == m_endpoint;
    if (!ok) {
        GST_INFO_OBJECT(self(),
                        "Rejected pekcomm WebSocket connection for endpoint '%s' (expected '%s')",
                        resource.c_str(),
                        m_endpoint.c_str());
    }
    return ok;
}

void WebSocketWriter::on_open(const PekCommConnectionHdl &hdl) {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.insert(hdl);
    GST_INFO_OBJECT(self(), "pekcomm WebSocket client connected");
}

void WebSocketWriter::on_close(const PekCommConnectionHdl &hdl) {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.erase(hdl);
    GST_INFO_OBJECT(self(), "pekcomm WebSocket client disconnected");
}

bool WebSocketWriter::io_open() {
    if (m_endpoint.empty() || m_endpoint.front() != '/') {
        GST_WARNING_OBJECT(self(), "endpoint must start with '/': '%s'", m_endpoint.c_str());
        return false;
    }

    try {
        m_ws = std::make_shared<PekCommWsServer>();
        m_ws->clear_access_channels(websocketpp::log::alevel::all);
        m_ws->clear_error_channels(websocketpp::log::elevel::all);
        m_ws->init_asio();
        m_ws->set_reuse_addr(true);
        m_ws->set_validate_handler([this](const PekCommConnectionHdl &hdl) { return validate(hdl); });
        m_ws->set_open_handler([this](const PekCommConnectionHdl &hdl) { on_open(hdl); });
        m_ws->set_close_handler([this](const PekCommConnectionHdl &hdl) { on_close(hdl); });
        m_ws->listen(m_port);
        m_ws->start_accept();
        m_ws_thread = std::thread([server = m_ws] { server->run(); });

        GST_INFO_OBJECT(self(),
                        "pekcomm WebSocket server listening on port %u endpoint '%s'",
                        static_cast<unsigned>(m_port),
                        m_endpoint.c_str());
        return true;
    } catch (const std::exception &e) {
        GST_WARNING_OBJECT(self(), "Failed to start pekcomm WebSocket server: %s", e.what());
        io_close();
        return false;
    }
}

void WebSocketWriter::io_close() {
    {
        std::lock_guard<std::mutex> lock(m_connection_lock);
        m_connections.clear();
    }

    if (m_ws) {
        websocketpp::lib::error_code ec;
        m_ws->stop_listening(ec);
        m_ws->stop();
    }

    if (m_ws_thread.joinable()) {
        m_ws_thread.join();
    }

    m_ws.reset();
}

bool WebSocketWriter::publish(const std::string &json_str) {
    if (!m_ws) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_connection_lock);
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        websocketpp::lib::error_code ec;
        m_ws->send(*it, json_str, websocketpp::frame::opcode::text, ec);
        if (ec) {
            GST_INFO_OBJECT(self(),
                            "Dropping pekcomm WebSocket client after send error: %s",
                            ec.message().c_str());
            it = m_connections.erase(it);
        } else {
            ++it;
        }
    }

    return true;
}
